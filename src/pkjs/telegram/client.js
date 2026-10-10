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
 * Telegram client using GramJS for direct MTProto communication.
 * This eliminates the need for a backend server - all Telegram communication
 * happens directly from the phone app.
 *
 * Exactly one connected client exists at a time: concurrent initClient()
 * calls share one connection attempt, and replaced clients are torn down.
 * Two live connections on the same auth key make Telegram revoke it
 * (AUTH_KEY_DUPLICATED), which signs the user out.
 */

var bundleLoader = require('../lib/bundle_loader');
var messageQueue = require('../lib/message_queue').Queue;
var appConfig = require('./app_config');
var session = require('./session');

var CONNECT_TIMEOUT_MS = 30000;

var client = null;
var isConnected = false;
var connecting = null;

var CLIENT_OPTIONS = {
    connectionRetries: 5,
    deviceModel: 'Squire',
    systemVersion: '1.0',
    appVersion: '1.0'
};

function telegramGlobals() {
    bundleLoader.ensureTelegramBundle();
    if (typeof TelegramClient === 'undefined' || typeof StringSession === 'undefined') {
        throw new Error('Telegram client library failed to load.');
    }
    return { TelegramClient: TelegramClient, StringSession: StringSession };
}

/**
 * Creates (but doesn't connect) a client for sessionString ('' for a fresh
 * auth key). Throws APP_NOT_CONFIGURED if the build has no API credentials.
 */
function createClient(sessionString) {
    var creds = appConfig.getCredentials();
    var g = telegramGlobals();
    var options = {};
    for (var k in CLIENT_OPTIONS) {
        options[k] = CLIENT_OPTIONS[k];
    }
    return new g.TelegramClient(new g.StringSession(sessionString || ''), creds.apiId, creds.apiHash, options);
}

/**
 * Best-effort teardown of a client: closes the connection and stops its
 * update loop. Never rejects.
 */
function destroyClient(c) {
    if (!c) {
        return Promise.resolve();
    }
    var closing;
    try {
        closing = typeof c.destroy === 'function' ? c.destroy() : c.disconnect();
    } catch (err) {
        closing = Promise.reject(err);
    }
    return Promise.resolve(closing).catch(function(err) {
        console.log('[client] Error closing client: ' + (err && (err.errorMessage || err.message)));
    });
}

function notifyWatchDisconnected() {
    messageQueue.enqueue({ TELEGRAM_CONNECTED: 0 });
}

function forgetInvalidSession(reason) {
    console.error('[client] Stored session rejected by Telegram (' + reason + '); clearing it');
    var old = client;
    client = null;
    isConnected = false;
    destroyClient(old);
    session.clearSession();
    notifyWatchDisconnected();
}

function connectNewClient() {
    var storedSession = session.loadSession();
    if (!storedSession) {
        return Promise.reject(new Error('No Telegram session available'));
    }
    var newClient;
    try {
        newClient = createClient(storedSession);
    } catch (err) {
        return Promise.reject(err);
    }
    return new Promise(function(resolve, reject) {
        var timedOut = false;
        var timer = setTimeout(function() {
            timedOut = true;
            console.error('[client] Connection timed out after ' + (CONNECT_TIMEOUT_MS / 1000) + 's');
            destroyClient(newClient);
            reject(new Error('Connection to Telegram timed out'));
        }, CONNECT_TIMEOUT_MS);
        Promise.resolve(newClient.connect()).then(function() {
            if (timedOut) return null;
            if (!newClient.connected) {
                throw new Error('Failed to connect to Telegram (not connected after retries)');
            }
            return newClient.isUserAuthorized();
        }).then(function(authorized) {
            if (timedOut) return;
            clearTimeout(timer);
            if (!authorized) {
                client = newClient;
                forgetInvalidSession('not authorized');
                reject(new Error('Telegram session expired. Please re-authenticate.'));
                return;
            }
            client = newClient;
            isConnected = true;
            console.log('[client] Telegram client connected');
            resolve(true);
        }).catch(function(err) {
            if (timedOut) return;
            clearTimeout(timer);
            var errorMsg = String((err && (err.errorMessage || err.message)) || err);
            console.error('[client] Failed to connect to Telegram: ' + errorMsg);
            if (errorMsg.indexOf('AUTH_KEY_DUPLICATED') !== -1 ||
                errorMsg.indexOf('AUTH_KEY_UNREGISTERED') !== -1 ||
                errorMsg.indexOf('SESSION_REVOKED') !== -1 ||
                errorMsg.indexOf('USER_DEACTIVATED') !== -1) {
                client = newClient;
                forgetInvalidSession(errorMsg);
                reject(new Error('Telegram session expired. Please re-authenticate.'));
                return;
            }
            destroyClient(newClient);
            reject(err);
        });
    });
}

/**
 * Connects the shared client using the stored session.
 * @returns {Promise<boolean>} resolves true once connected and authorized.
 */
function initClient() {
    if (client && client.connected && isConnected) {
        return Promise.resolve(true);
    }
    if (connecting) {
        return connecting;
    }
    var stale = client;
    client = null;
    isConnected = false;
    destroyClient(stale);
    connecting = connectNewClient().then(function(result) {
        connecting = null;
        return result;
    }, function(err) {
        connecting = null;
        throw err;
    });
    return connecting;
}

function isClientConnected() {
    return isConnected && client !== null;
}

function getCurrentUser() {
    return null;
}

/**
 * Signs out: revokes the session on Telegram's servers if we can reach them,
 * and always deletes the local copy.
 * @returns {Promise<{revoked: boolean}>} revoked is false if only the local
 *   copy was deleted (the session may still be listed under Telegram >
 *   Settings > Devices).
 */
function disconnect() {
    var current = client;
    var prep = Promise.resolve(false);
    if (!(current && current.connected) && session.hasSession() && appConfig.isConfigured()) {
        prep = initClient().then(function() { return true; }, function(err) {
            console.log('[client] Could not connect for server-side logout: ' + (err && err.message));
            return false;
        });
    }
    return prep.then(function() {
        current = client;
        if (!current || !current.connected || typeof TelegramApi === 'undefined') {
            return false;
        }
        return Promise.resolve(current.invoke(new TelegramApi.auth.LogOut())).then(function() {
            console.log('[client] Session revoked server-side');
            return true;
        }, function(err) {
            console.error('[client] auth.LogOut failed: ' + (err && (err.errorMessage || err.message)) + ' - signing out locally only');
            return false;
        });
    }).then(function(revoked) {
        var old = client;
        client = null;
        isConnected = false;
        session.clearSession();
        return destroyClient(old).then(function() {
            return { revoked: revoked };
        });
    });
}

function getClient() {
    return client;
}

/**
 * Event builder classes from the bundle. EditedMessage is only present in
 * bundles built by scripts/build-gramjs.js; callers fall back to Raw.
 */
function getEventBuilders() {
    bundleLoader.ensureTelegramBundle();
    return {
        NewMessage: typeof NewMessage !== 'undefined' ? NewMessage : null,
        Raw: typeof Raw !== 'undefined' ? Raw : null,
        EditedMessage: typeof EditedMessage !== 'undefined' ? EditedMessage : null
    };
}

/**
 * Drops the current client (e.g. after signing in with a new session) so the
 * next initClient() connects with the stored session.
 */
function resetClient() {
    console.log('[client] Resetting client - will reconnect with stored session');
    var old = client;
    client = null;
    isConnected = false;
    return destroyClient(old);
}

exports.initClient = initClient;
exports.createClient = createClient;
exports.destroyClient = destroyClient;
exports.isClientConnected = isClientConnected;
exports.getCurrentUser = getCurrentUser;
exports.disconnect = disconnect;
exports.getClient = getClient;
exports.getEventBuilders = getEventBuilders;
exports.resetClient = resetClient;
