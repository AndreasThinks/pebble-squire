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
 * Telegram authentication functions.
 * Uses client.invoke() with auth.SendCode and auth.SignIn,
 * matching the Pebblegram bundle's API surface.
 */

var client = require('./client');
var session = require('./session');

var phoneCodeHash = null;
var pendingPhone = null;
var authSession = null;

var AUTH_STATE_KEY = 'telegram_auth_waiting_for_code';

var AUTH_DC = {
    id: 1,
    host: 'pluto.web.telegram.org'
};

function createAuthClient() {
    var StringSessionCls = StringSession;
    var apiId = parseInt(process.env.TELEGRAM_APP_ID, 10);
    var apiHash = process.env.TELEGRAM_APP_HASH || '';
    var newClient = new TelegramClient(new StringSessionCls(''), apiId, apiHash, {
        connectionRetries: 5,
        deviceModel: 'Squire',
        systemVersion: '1.0',
        appVersion: '1.0'
    });
    if (newClient.session && typeof newClient.session.setDC === 'function') {
        newClient.session.setDC(AUTH_DC.id, AUTH_DC.host, 443);
    }
    return newClient;
}

function startAuth(phoneNumber) {
    console.log('[auth] startAuth called for phone: ' + phoneNumber);
    pendingPhone = phoneNumber;
    return new Promise(function(resolve, reject) {
        var authClient = createAuthClient();
        var Api = (typeof TelegramApi !== 'undefined') ? TelegramApi : (authClient.Api || {});
        var apiId = parseInt(process.env.TELEGRAM_APP_ID, 10);
        var apiHash = process.env.TELEGRAM_APP_HASH || '';

        authClient.connect().then(function() {
            console.log('[auth] Auth client connected, sending code...');
            return authClient.invoke(new Api.auth.SendCode({
                phoneNumber: phoneNumber,
                apiId: apiId,
                apiHash: apiHash,
                settings: new Api.CodeSettings({})
            }));
        }).then(function(result) {
            console.log('[auth] SendCode result: phoneCodeHash=' + (result.phoneCodeHash ? 'received' : 'missing'));
            if (!result || !result.phoneCodeHash) {
                if (result && result.className === 'auth.SentCodeSuccess') {
                    reject(new Error('This session is already authorized.'));
                    return;
                }
                reject(new Error('Telegram did not return a login code hash.'));
                return;
            }
            phoneCodeHash = result.phoneCodeHash;
            authSession = authClient.session.save();
            try { localStorage.setItem(AUTH_STATE_KEY, 'true'); } catch(e) {}
            resolve({
                success: true,
                status: 'code_sent'
            });
        }).catch(function(err) {
            console.error('[auth] startAuth failed: ' + (err.message || err));
            try { localStorage.removeItem(AUTH_STATE_KEY); } catch(e) {}
            reject(new Error('Auth failed: ' + (err.errorMessage || err.message)));
        });
    });
}

function provideCode(code) {
    console.log('[auth] provideCode called');
    try { localStorage.removeItem(AUTH_STATE_KEY); } catch(e) {}
    return new Promise(function(resolve, reject) {
        if (!phoneCodeHash) {
            reject(new Error('No pending code request — call startAuth first'));
            return;
        }
        var apiId = parseInt(process.env.TELEGRAM_APP_ID, 10);
        var apiHash = process.env.TELEGRAM_APP_HASH || '';
        var phone = pendingPhone || '';
        var StringSessionCls = StringSession;
        var signInClient = new TelegramClient(new StringSessionCls(authSession || ''), apiId, apiHash, {
            connectionRetries: 5,
            deviceModel: 'Squire',
            systemVersion: '1.0',
            appVersion: '1.0'
        });
        var Api = (typeof TelegramApi !== 'undefined') ? TelegramApi : (signInClient.Api || {});

        signInClient.connect().then(function() {
            console.log('[auth] SignIn client connected, signing in...');
            return signInClient.invoke(new Api.auth.SignIn({
                phoneNumber: phone,
                phoneCodeHash: phoneCodeHash,
                phoneCode: code
            }));
        }).then(function(result) {
            console.log('[auth] SignIn successful');
            var sessionStr = signInClient.session.save();
            session.saveSession(sessionStr);
            phoneCodeHash = null;
            authSession = null;
            resolve({
                success: true,
                status: 'signed_in'
            });
        }).catch(function(err) {
            var msg = err.errorMessage || err.message || '';
            console.error('[auth] SignIn failed: ' + msg);
            if (msg === 'SESSION_PASSWORD_NEEDED') {
                console.log('[auth] 2FA required');
                authSession = signInClient.session.save();
                phoneCodeHash = null;
                resolve({
                    success: false,
                    status: 'password_needed',
                    hint: err.hint || ''
                });
                return;
            }
            phoneCodeHash = null;
            authSession = null;
            reject(new Error('SignIn failed: ' + msg));
        });
    });
}

function providePassword(password) {
    console.log('[auth] providePassword called');
    return new Promise(function(resolve, reject) {
        if (!authSession) {
            reject(new Error('No pending 2FA request — sign in first'));
            return;
        }
        var apiId = parseInt(process.env.TELEGRAM_APP_ID, 10);
        var apiHash = process.env.TELEGRAM_APP_HASH || '';
        var StringSessionCls = StringSession;
        var pwClient = new TelegramClient(new StringSessionCls(authSession), apiId, apiHash, {
            connectionRetries: 5,
            deviceModel: 'Squire',
            systemVersion: '1.0',
            appVersion: '1.0'
        });

        pwClient.connect().then(function() {
            return pwClient.signInWithPassword({
                apiId: apiId,
                apiHash: apiHash
            }, {
                password: function() {
                    return Promise.resolve(password);
                },
                onError: function(err) {
                    reject(new Error('2FA failed: ' + (err.message || err)));
                }
            });
        }).then(function() {
            console.log('[auth] 2FA successful');
            var sessionStr = pwClient.session.save();
            session.saveSession(sessionStr);
            authSession = null;
            resolve({
                success: true,
                status: 'signed_in'
            });
        }).catch(function(err) {
            console.error('[auth] 2FA failed: ' + (err.message || err));
            authSession = null;
            reject(new Error('2FA failed: ' + (err.message || err)));
        });
    });
}

function checkConnection() {
    console.log('[auth] checkConnection called');
    return new Promise(function(resolve) {
        var storedSession = session.loadSession();
        if (storedSession) {
            console.log('[auth] Stored session found, attempting to connect...');
            client.initClient().then(function() {
                console.log('[auth] Connected successfully with stored session');
                resolve({ connected: true, hasSession: true });
            }).catch(function(err) {
                console.error('[auth] Failed to connect with stored session: ' + (err.message || err));
                resolve({ connected: false, hasSession: true, needsReauth: true });
            });
        } else {
            console.log('[auth] No stored session found');
            resolve({ connected: false, hasSession: false });
        }
    });
}

function logout() {
    console.log('[auth] logout called');
    return new Promise(function(resolve, reject) {
        client.disconnect().then(function() {
            console.log('[auth] Disconnected and session cleared');
            phoneCodeHash = null;
            resolve();
        }).catch(function(err) {
            console.error('[auth] Logout failed: ' + (err.message || err));
            reject(err);
        });
    });
}

function getAuthState() {
    return {
        isWaitingForCode: phoneCodeHash !== null,
        isWaitingForPassword: false,
        isAuthInProgress: phoneCodeHash !== null,
        isCodeViaApp: false
    };
}

exports.startAuth = startAuth;
exports.provideCode = provideCode;
exports.providePassword = providePassword;
exports.checkConnection = checkConnection;
exports.logout = logout;
exports.getAuthState = getAuthState;