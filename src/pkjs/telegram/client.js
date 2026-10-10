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
var cancelConnection = null;
var generation = 0;
// Wait for teardown before reusing an auth key on another connection.
var closing = Promise.resolve();

function retireClient(c) {
    if (c) {
        closing = closing.then(function() { return destroyClient(c); });
    }
    return closing;
}

var CLIENT_OPTIONS = {
    connectionRetries: 5,
    deviceModel: 'Squire Plus',
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
    try {
        crypto.getRandomValues(new Uint8Array(1));
    } catch (err) {
        var unsupported = new Error('This phone runtime has no secure random source. Use a runtime with crypto.getRandomValues support.');
        unsupported.code = 'SECURE_RANDOM_UNAVAILABLE';
        throw unsupported;
    }
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
    retireClient(old);
    session.clearSession();
    notifyWatchDisconnected();
}

function connectNewClient(mine) {
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
        var stopped = false;
        function stop(err) {
            if (stopped) return;
            stopped = true;
            clearTimeout(timer);
            if (cancelConnection === cancel) cancelConnection = null;
            retireClient(newClient);
            reject(err);
        }
        function cancel() {
            stop(new Error('Telegram connection cancelled'));
        }
        cancelConnection = cancel;
        var timer = setTimeout(function() {
            console.error('[client] Connection timed out after ' + (CONNECT_TIMEOUT_MS / 1000) + 's');
            stop(new Error('Connection to Telegram timed out'));
        }, CONNECT_TIMEOUT_MS);
        // Promise.resolve().then also catches synchronous runtime failures.
        Promise.resolve().then(function() { return newClient.connect(); }).then(function() {
            if (stopped || mine !== generation) {
                return destroyClient(newClient).then(function() { return null; });
            }
            if (!newClient.connected) {
                throw new Error('Failed to connect to Telegram (not connected after retries)');
            }
            return newClient.isUserAuthorized();
        }).then(function(authorized) {
            if (stopped || mine !== generation) return;
            stopped = true;
            clearTimeout(timer);
            if (cancelConnection === cancel) cancelConnection = null;
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
            if (stopped || mine !== generation) return;
            var errorMsg = String((err && (err.errorMessage || err.message)) || err);
            console.error('[client] Failed to connect to Telegram: ' + errorMsg);
            if (errorMsg.indexOf('AUTH_KEY_DUPLICATED') !== -1 ||
                errorMsg.indexOf('AUTH_KEY_UNREGISTERED') !== -1 ||
                errorMsg.indexOf('SESSION_REVOKED') !== -1 ||
                errorMsg.indexOf('USER_DEACTIVATED') !== -1) {
                stopped = true;
                clearTimeout(timer);
                if (cancelConnection === cancel) cancelConnection = null;
                client = newClient;
                forgetInvalidSession(errorMsg);
                reject(new Error('Telegram session expired. Please re-authenticate.'));
                return;
            }
            stop(err);
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
    var mine = generation;
    var stale = client;
    client = null;
    isConnected = false;
    var attempt = retireClient(stale).then(function() {
        if (mine !== generation) throw new Error('Telegram connection cancelled');
        return connectNewClient(mine);
    }).then(function(result) {
        if (connecting === attempt) connecting = null;
        return result;
    }, function(err) {
        if (connecting === attempt) connecting = null;
        throw err;
    });
    connecting = attempt;
    return connecting;
}

function isClientConnected() {
    return isConnected && client !== null && !!client.connected;
}

function getCurrentUser() {
    return null;
}

function logoutOnServer(current) {
    return new Promise(function(resolve) {
        var timer = setTimeout(function() { resolve(false); }, CONNECT_TIMEOUT_MS);
        Promise.resolve().then(function() {
            return current.invoke(new TelegramApi.auth.LogOut());
        }).then(function() {
            clearTimeout(timer);
            console.log('[client] Session revoked server-side');
            resolve(true);
        }, function(err) {
            clearTimeout(timer);
            console.error('[client] auth.LogOut failed: ' + (err && (err.errorMessage || err.message)) + ' - signing out locally only');
            resolve(false);
        });
    });
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
        return logoutOnServer(current);
    }).then(function(revoked) {
        session.clearSession();
        return resetClient().then(function() {
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
    generation++;
    if (cancelConnection) cancelConnection();
    connecting = null;
    var old = client;
    client = null;
    isConnected = false;
    return retireClient(old);
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
exports.forgetInvalidSession = forgetInvalidSession;
