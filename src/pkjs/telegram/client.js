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
 */

var session = require('./session');

// Client instance
var client = null;
var isConnected = false;
var currentUser = null;

/**
 * Initialize the Telegram client.
 * @returns {Promise<boolean>} True if successfully initialized
 */
function initClient() {
    console.log('[client] initClient called, client: ' + (client ? (client.connected ? 'connected' : 'disconnected') : 'null'));
    if (client && client.connected) {
        console.log('[client] Reusing existing connected client');
        return Promise.resolve(true);
    }

    return new Promise(function(resolve, reject) {
        try {
            var storedSession = session.loadSession();
            console.log('[client] Stored session: ' + (storedSession ? 'present (length: ' + storedSession.length + ')' : 'none'));

            if (typeof TelegramClient !== 'undefined') {
                console.log('[client] GramJS available, creating TelegramClient...');
                var apiId = parseInt(process.env.TELEGRAM_APP_ID, 10);
                var apiHash = process.env.TELEGRAM_APP_HASH || '';
                if (!apiId || !apiHash) {
                    console.error('[client] TELEGRAM_APP_ID or TELEGRAM_APP_HASH not set! apiId: ' + apiId + ', apiHash length: ' + apiHash.length);
                }
                var stringSession = new StringSession(storedSession || '');
                client = new TelegramClient(stringSession, apiId || 0, apiHash, {
                    connectionRetries: 5,
                    deviceModel: 'Squire',
                    systemVersion: '1.0',
                    appVersion: '1.0',
                });

                console.log('[client] Calling client.connect()...');
                var connectTimeout = setTimeout(function() {
                    console.error('[client] Connection timed out after 30s');
                    reject(new Error('Connection to Telegram timed out'));
                }, 30000);
                var authCheckDone = false;
                client.connect().then(function() {
                    clearTimeout(connectTimeout);
                    if (!client.connected) {
                        console.error('[client] connect() resolved but client.connected is false');
                        isConnected = false;
                        reject(new Error('Failed to connect to Telegram (not connected after retries)'));
                        return null;
                    }
                    isConnected = true;
                    console.log('[client] Telegram client connected successfully');
                    console.log('[client] Client details - connected: ' + client.connected + ', session DC: ' + (client.session && client.session.dcId ? client.session.dcId : 'unknown'));
                    if (typeof NewMessage !== 'undefined') {
                        try {
                            client.addEventHandler(function(event) {
                                try {
                                    var msg = event.message;
                                    if (!msg || !msg.message) return;
                                    console.log('[client] global raw message out=', msg.out, 'id=', msg.id, 'text=', msg.message.substring(0, 50));
                                } catch (err) {
                                    console.error('[client] Error in global message handler:', err);
                                }
                            }, new NewMessage({}));
                            console.log('[client] Global NewMessage event handler registered');
                        } catch (err) {
                            console.error('[client] Failed to register global NewMessage handler:', err);
                        }
                    } else {
                        console.log('[client] NewMessage not available for global handler');
                    }
                    return storedSession ? client.isUserAuthorized() : null;
                }).then(function(authorized) {
                    if (authCheckDone) return;
                    authCheckDone = true;
                    if (authorized === null || authorized === undefined) {
                        console.log('[client] No stored session, proceeding (auth will be needed)');
                        resolve(true);
                        return;
                    }
                    if (!authorized) {
                        console.error('[client] Client connected but NOT authorized (stored session may be invalid)');
                        isConnected = false;
                        client = null;
                        session.clearSession();

                        try { Pebble.sendAppMessage({TELEGRAM_CONNECTED: 0}); } catch(e) {}
                        reject(new Error('Telegram session expired. Please re-authenticate.'));
                        return;
                    }
                    console.log('[client] Client is authorized');
                    resolve(true);
                }).catch(function(err) {
                    clearTimeout(connectTimeout);
                    var errorMsg = (err.message || err || '').toString();
                    console.error('[client] Failed to connect to Telegram: ' + errorMsg);
                    console.error('[client] Error stack: ' + (err.stack || 'no stack'));
                    if (errorMsg.indexOf('AUTH_KEY_DUPLICATED') !== -1 || errorMsg.indexOf('AUTH_KEY_UNREGISTERED') !== -1) {
                        console.log('[client] Session key invalid, clearing session for re-auth');
                        session.clearSession();

                        isConnected = false;
                        client = null;
                        try { Pebble.sendAppMessage({TELEGRAM_CONNECTED: 0}); } catch(e) {}
                        reject(new Error('Telegram session expired. Please re-authenticate.'));
                        return;
                    }
                    if (errorMsg.indexOf('re-authenticate') !== -1 || errorMsg.indexOf('session expired') !== -1) {
                        console.log('[client] Clearing invalid session');
                        session.clearSession();

                        isConnected = false;
                        client = null;
                        try { Pebble.sendAppMessage({TELEGRAM_CONNECTED: 0}); } catch(e) {}
                    }
                    reject(err);
                });
            } else {
                console.log('[client] GramJS not loaded, checking for stored session...');
                if (storedSession) {
                    console.log('[client] Using stored session (GramJS unavailable)');
                    isConnected = true;
                    resolve(true);
                } else {
                    console.error('[client] No session available and GramJS not loaded');
                    reject(new Error('No Telegram session available'));
                }
            }
        } catch (err) {
            console.error('[client] Error initializing Telegram client: ' + (err.message || err));
            reject(err);
        }
    });
}

/**
 * Check if client is connected.
 * @returns {boolean}
 */
function isClientConnected() {
    return isConnected && client !== null;
}

/**
 * Get current user info.
 * @returns {object|null}
 */
function getCurrentUser() {
    return currentUser;
}

/**
 * Disconnect from Telegram.
 * @returns {Promise<void>}
 */
function disconnect() {
    console.log('[client] disconnect called, client: ' + (client ? 'present' : 'null'));
    return new Promise(function(resolve, reject) {
        if (client) {
            client.disconnect().then(function() {
                console.log('[client] Disconnected successfully');
                isConnected = false;
                client = null;
                currentUser = null;
                session.clearSession();
                resolve();
            }).catch(function(err) {
                console.error('[client] Disconnect failed: ' + (err.message || err));
                reject(err);
            });
        } else {
            console.log('[client] No client to disconnect, clearing session');
            isConnected = false;
            session.clearSession();
            resolve();
        }
    });
}

/**
 * Get the client instance.
 * @returns {object|null}
 */
function getClient() {
    if (!client) {
        console.log('[client] getClient called but client is null');
    }
    return client;
}

// For GramJS compatibility, export these
exports.StringSession = typeof StringSession !== 'undefined' ? StringSession : function(sessionStr) {
    this.sessionStr = sessionStr || '';
    this.save = function() {
        return this.sessionStr;
    };
};

function resetClient() {
    console.log('[client] Resetting client — will reconnect with new session');
    client = null;
    isConnected = false;
}

exports.initClient = initClient;
exports.isClientConnected = isClientConnected;
exports.getCurrentUser = getCurrentUser;
exports.disconnect = disconnect;
exports.getClient = getClient;
exports.resetClient = resetClient;