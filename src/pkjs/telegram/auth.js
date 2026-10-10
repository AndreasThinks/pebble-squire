/**
 * Copyright 2025 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * Telegram sign-in: phone number -> login code -> (optional) cloud password.
 *
 * Each step runs on its own short-lived client, created from the temporary
 * (not yet authorized) auth key of the previous step and torn down
 * afterwards, so only one connection ever uses a given key.
 *
 * The state needed to continue an interrupted sign-in (phone number,
 * phone_code_hash and that temporary auth key) is kept in localStorage for a
 * few minutes, because PebbleKit JS is routinely restarted between steps
 * (e.g. while the user reads the code in Telegram and reopens the settings
 * page). On its own it can't sign anyone in: that still needs the login code
 * and, for two-step verification accounts, the password. The code and the
 * password are never stored or logged.
 */

var appConfig = require('./app_config');
var client = require('./client');
var session = require('./session');

var PENDING_KEY = 'squire_pending_auth';
var LEGACY_WAITING_KEY = 'telegram_auth_waiting_for_code';
var CODE_STAGE_TTL_MS = 15 * 60 * 1000;
var PASSWORD_STAGE_TTL_MS = 30 * 60 * 1000;
var CONNECT_TIMEOUT_MS = 30000;

// Values sent to the watch with TELEGRAM_AUTH_ERROR. Keep in sync with
// AuthErrorReason in src/c/util/auth_flow.h.
var REASONS = {
    GENERIC: 1,
    INVALID_PHONE: 2,
    INVALID_CODE: 3,
    CODE_EXPIRED: 4,
    FLOOD_WAIT: 5,
    BAD_PASSWORD: 6,
    NO_PENDING: 7,
    APP_NOT_CONFIGURED: 8,
    NETWORK: 9,
    NO_ACCOUNT: 10
};

var PHONE_RE = /^\+[1-9][0-9]{6,14}$/;
var CODE_RE = /^[0-9]{4,8}$/;

// The DC we first ask for a login code; GramJS follows PHONE_MIGRATE_X
// redirects from there and the resulting DC is saved with the auth key.
var AUTH_DC = {
    id: 1,
    host: 'pluto.web.telegram.org'
};

var authInProgress = false;
var pending = null;

function AuthError(reason, message) {
    var err = new Error(message);
    err.reason = reason;
    return err;
}

function withAuthLock(fn) {
    return function() {
        if (authInProgress) {
            return Promise.reject(AuthError(REASONS.GENERIC, 'Authentication already in progress'));
        }
        authInProgress = true;
        var args = arguments;
        return Promise.resolve()
            .then(function() { return fn.apply(null, args); })
            .then(function(result) {
                authInProgress = false;
                return result;
            }, function(err) {
                authInProgress = false;
                throw err;
            });
    };
}

function telegramErrorText(err) {
    return String((err && (err.errorMessage || err.message)) || err || '');
}

// Maps a Telegram/GramJS error to an AuthError with a watch-facing reason.
// Messages are Telegram error codes, never user input.
function classify(err) {
    if (err && err.reason) {
        return err;
    }
    if (err && err.code === 'APP_NOT_CONFIGURED') {
        return AuthError(REASONS.APP_NOT_CONFIGURED, err.message);
    }
    var text = telegramErrorText(err);
    var reason = REASONS.GENERIC;
    if (/PHONE_NUMBER_UNOCCUPIED/.test(text)) {
        reason = REASONS.NO_ACCOUNT;
    } else if (/PHONE_NUMBER_(INVALID|BANNED|FLOOD)|PHONE_NUMBER_APP_SIGNUP_FORBIDDEN/.test(text)) {
        reason = REASONS.INVALID_PHONE;
    } else if (/PHONE_CODE_(INVALID|EMPTY)/.test(text)) {
        reason = REASONS.INVALID_CODE;
    } else if (/PHONE_CODE_EXPIRED|PHONE_CODE_HASH_EMPTY/.test(text)) {
        reason = REASONS.CODE_EXPIRED;
    } else if (/FLOOD_WAIT|FLOOD|A wait of/i.test(text) || (err && err.seconds)) {
        reason = REASONS.FLOOD_WAIT;
    } else if (/PASSWORD_HASH_INVALID/.test(text)) {
        reason = REASONS.BAD_PASSWORD;
    } else if (/timed out|timeout|Not connected|connection|network|WebSocket/i.test(text)) {
        reason = REASONS.NETWORK;
    }
    return AuthError(reason, 'Telegram sign-in failed: ' + text);
}

function clearPending() {
    pending = null;
    try {
        localStorage.removeItem(PENDING_KEY);
    } catch (e) {}
}

function savePending(state) {
    pending = state;
    try {
        localStorage.setItem(PENDING_KEY, JSON.stringify(state));
    } catch (e) {
        console.error('[auth] Could not persist pending sign-in; it will not survive a restart');
    }
}

function loadPending() {
    try {
        localStorage.removeItem(LEGACY_WAITING_KEY);
    } catch (e) {}
    var state = pending;
    if (!state) {
        try {
            state = JSON.parse(localStorage.getItem(PENDING_KEY));
        } catch (e) {
            state = null;
        }
    }
    var valid = state && state.v === 1 &&
        (state.stage === 'code' || state.stage === 'password') &&
        typeof state.phone === 'string' && typeof state.authSession === 'string' &&
        typeof state.expiresAt === 'number' &&
        (state.stage === 'password' || typeof state.phoneCodeHash === 'string');
    if (!valid || state.expiresAt <= Date.now()) {
        if (state) {
            clearPending();
        }
        return null;
    }
    pending = state;
    return state;
}

function withTimeout(promise, ms) {
    return new Promise(function(resolve, reject) {
        var timer = setTimeout(function() {
            reject(new Error('Connection to Telegram timed out'));
        }, ms);
        Promise.resolve(promise).then(function(value) {
            clearTimeout(timer);
            resolve(value);
        }, function(err) {
            clearTimeout(timer);
            reject(err);
        });
    });
}

// Runs step(authClient, Api) on a fresh client built from sessionString and
// always tears the client down afterwards.
function withAuthClient(sessionString, setDc, step) {
    var authClient;
    try {
        authClient = client.createClient(sessionString);
    } catch (err) {
        return Promise.reject(classify(err));
    }
    if (setDc && authClient.session && typeof authClient.session.setDC === 'function') {
        authClient.session.setDC(AUTH_DC.id, AUTH_DC.host, 443);
    }
    var Api = TelegramApi;
    return withTimeout(authClient.connect(), CONNECT_TIMEOUT_MS).then(function() {
        return step(authClient, Api);
    }).then(function(result) {
        return client.destroyClient(authClient).then(function() { return result; });
    }, function(err) {
        return client.destroyClient(authClient).then(function() { throw err; });
    });
}

function startAuth(phoneNumber) {
    var phone = typeof phoneNumber === 'string' ? phoneNumber.replace(/[\s()-]/g, '') : '';
    if (!PHONE_RE.test(phone)) {
        return Promise.reject(AuthError(REASONS.INVALID_PHONE, 'Phone number must be in international format, e.g. +15551234567'));
    }
    console.log('[auth] Requesting login code');
    // A new request supersedes any earlier one.
    clearPending();
    var creds;
    try {
        creds = appConfig.getCredentials();
    } catch (err) {
        return Promise.reject(classify(err));
    }
    return withAuthClient('', true, function(authClient, Api) {
        return authClient.invoke(new Api.auth.SendCode({
            phoneNumber: phone,
            apiId: creds.apiId,
            apiHash: creds.apiHash,
            settings: new Api.CodeSettings({})
        })).then(function(result) {
            if (!result || !result.phoneCodeHash) {
                throw AuthError(REASONS.GENERIC, 'Telegram did not return a login code hash.');
            }
            var codeLength = result.type && typeof result.type.length === 'number' ? result.type.length : 0;
            savePending({
                v: 1,
                stage: 'code',
                phone: phone,
                phoneCodeHash: result.phoneCodeHash,
                authSession: authClient.session.save(),
                codeLength: codeLength,
                expiresAt: Date.now() + CODE_STAGE_TTL_MS
            });
            console.log('[auth] Login code sent');
            return { success: true, status: 'code_sent', codeLength: codeLength };
        });
    }).catch(function(err) {
        var classified = classify(err);
        console.error('[auth] startAuth failed (reason ' + classified.reason + ')');
        throw classified;
    });
}

function provideCode(code) {
    var cleanCode = typeof code === 'string' || typeof code === 'number' ? String(code).replace(/[\s-]/g, '') : '';
    if (!CODE_RE.test(cleanCode)) {
        return Promise.reject(AuthError(REASONS.INVALID_CODE, 'Login codes are 4-8 digits.'));
    }
    var state = loadPending();
    if (!state || state.stage !== 'code') {
        return Promise.reject(AuthError(REASONS.NO_PENDING, 'No pending sign-in. Request a new code.'));
    }
    console.log('[auth] Submitting login code');
    return withAuthClient(state.authSession, false, function(authClient, Api) {
        return authClient.invoke(new Api.auth.SignIn({
            phoneNumber: state.phone,
            phoneCodeHash: state.phoneCodeHash,
            phoneCode: cleanCode
        })).then(function(result) {
            if (result && result.className === 'auth.AuthorizationSignUpRequired') {
                clearPending();
                throw AuthError(REASONS.NO_ACCOUNT, 'No Telegram account for this number.');
            }
            if (!session.saveSession(authClient.session.save())) {
                throw AuthError(REASONS.GENERIC, 'Could not save the Telegram session.');
            }
            clearPending();
            console.log('[auth] Signed in');
            return { success: true, status: 'signed_in' };
        }, function(err) {
            if (telegramErrorText(err).indexOf('SESSION_PASSWORD_NEEDED') !== -1) {
                savePending({
                    v: 1,
                    stage: 'password',
                    phone: state.phone,
                    authSession: authClient.session.save(),
                    expiresAt: Date.now() + PASSWORD_STAGE_TTL_MS
                });
                console.log('[auth] Two-step verification password required');
                return { success: false, status: 'password_needed' };
            }
            throw err;
        });
    }).catch(function(err) {
        var classified = classify(err);
        if (classified.reason === REASONS.CODE_EXPIRED || classified.reason === REASONS.NO_ACCOUNT) {
            clearPending();
        }
        // Wrong code or a network hiccup: keep the pending sign-in so the
        // user can simply try again.
        console.error('[auth] Code sign-in failed (reason ' + classified.reason + ')');
        throw classified;
    });
}

function providePassword(password) {
    if (typeof password !== 'string' || password.length === 0) {
        return Promise.reject(AuthError(REASONS.BAD_PASSWORD, 'Password is empty.'));
    }
    var state = loadPending();
    if (!state || state.stage !== 'password') {
        return Promise.reject(AuthError(REASONS.NO_PENDING, 'No sign-in is waiting for a password.'));
    }
    var creds;
    try {
        creds = appConfig.getCredentials();
    } catch (err) {
        return Promise.reject(classify(err));
    }
    console.log('[auth] Submitting two-step verification password');
    return withAuthClient(state.authSession, false, function(authClient) {
        var failure = null;
        return authClient.signInWithPassword({
            apiId: creds.apiId,
            apiHash: creds.apiHash
        }, {
            password: function() {
                return Promise.resolve(password);
            },
            // GramJS retries in a loop, re-asking for the password, unless
            // onError returns true. Our password can't change between
            // attempts, so stop after the first error instead of hammering
            // Telegram with the same wrong password.
            onError: function(err) {
                failure = err;
                return true;
            }
        }).then(function() {
            if (!session.saveSession(authClient.session.save())) {
                throw AuthError(REASONS.GENERIC, 'Could not save the Telegram session.');
            }
            clearPending();
            console.log('[auth] Signed in with two-step verification');
            return { success: true, status: 'signed_in' };
        }, function(err) {
            throw failure || err;
        });
    }).catch(function(err) {
        var classified = classify(err);
        console.error('[auth] Password sign-in failed (reason ' + classified.reason + ')');
        throw classified;
    });
}

function checkConnection() {
    if (!session.loadSession()) {
        return Promise.resolve({ connected: false, hasSession: false });
    }
    return client.initClient().then(function() {
        return { connected: true, hasSession: true };
    }, function() {
        return { connected: false, hasSession: session.hasSession(), needsReauth: !session.hasSession() };
    });
}

/**
 * Signs out and abandons any pending sign-in.
 * @returns {Promise<{revoked: boolean}>}
 */
function logout() {
    clearPending();
    return client.disconnect().then(function(result) {
        clearPending();
        return result;
    });
}

function getAuthState() {
    var state = loadPending();
    return {
        stage: state ? state.stage : null,
        isWaitingForCode: !!state && state.stage === 'code',
        isWaitingForPassword: !!state && state.stage === 'password',
        codeLength: state && state.codeLength ? state.codeLength : 0,
        isAuthInProgress: authInProgress
    };
}

exports.REASONS = REASONS;
exports.startAuth = withAuthLock(startAuth);
exports.provideCode = withAuthLock(provideCode);
exports.providePassword = withAuthLock(providePassword);
exports.checkConnection = checkConnection;
exports.logout = logout;
exports.getAuthState = getAuthState;
exports.clearPendingAuth = clearPending;
exports.isValidPhoneNumber = function(phone) { return PHONE_RE.test(String(phone || '').replace(/[\s()-]/g, '')); };
