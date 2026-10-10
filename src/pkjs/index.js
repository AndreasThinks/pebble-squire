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

var location = require('./location');
var session = require('./session');
var telegram = require('./telegram');
var history = require('./history');
var messageQueue = require('./lib/message_queue').Queue;
var Clay = require('@rebble/clay');
var clayConfig = require('./config.json');
var customConfigFunction = require('./custom_config');
var config = require('./config');
var package_json = require('package.json');


// Sign-in fields on the config page. They are removed from the page's
// response before Clay sees it, so they are never written to localStorage
// (not even briefly) and never forwarded to the watch.
var CONFIG_AUTH_KEYS = ['TELEGRAM_PHONE', 'TELEGRAM_CODE', 'TELEGRAM_2FA_PASSWORD'];

var REASONS = telegram.AUTH_ERROR_REASONS;

// autoHandleEvents is off so we can intercept the sign-in fields before Clay
// would persist and forward them.
var clay = new Clay(clayConfig, customConfigFunction, { autoHandleEvents: false });

function send(message) {
    messageQueue.enqueue(message);
}

// Describes an AppMessage payload for logs without its values: payloads
// carry prompts, phone numbers and login codes.
function describePayload(payload) {
    if (!payload || typeof payload !== 'object') {
        return '(none)';
    }
    return Object.keys(payload).join(',') || '(empty)';
}

// Parses the config page response the same way Clay does.
function parseConfigResponse(response) {
    var text = /^\{/.test(response) ? response : decodeURIComponent(response);
    return JSON.parse(text);
}

function fieldValue(setting) {
    var value = (setting && typeof setting === 'object') ? setting.value : setting;
    return (typeof value === 'string') ? value.trim() : '';
}

Pebble.addEventListener('showConfiguration', function() {
    Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function(e) {
    if (!e || !e.response) {
        return;
    }
    var raw;
    try {
        raw = parseConfigResponse(e.response);
    } catch (err) {
        console.error('[index] Failed to parse config response');
        return;
    }
    if (!raw || typeof raw !== 'object') {
        return;
    }

    var phone = fieldValue(raw.TELEGRAM_PHONE);
    var code = fieldValue(raw.TELEGRAM_CODE);
    var password = (raw.TELEGRAM_2FA_PASSWORD && typeof raw.TELEGRAM_2FA_PASSWORD === 'object')
        ? raw.TELEGRAM_2FA_PASSWORD.value : raw.TELEGRAM_2FA_PASSWORD;
    password = typeof password === 'string' ? password : '';
    for (var i = 0; i < CONFIG_AUTH_KEYS.length; i++) {
        delete raw[CONFIG_AUTH_KEYS[i]];
    }

    // Normalise the bot username before Clay stores it; an invalid one is
    // stored as empty rather than guessed at.
    if (raw.AGENT_TELEGRAM_USERNAME !== undefined) {
        var normalized = telegram.normalizeBotUsername(fieldValue(raw.AGENT_TELEGRAM_USERNAME));
        if (raw.AGENT_TELEGRAM_USERNAME && typeof raw.AGENT_TELEGRAM_USERNAME === 'object') {
            raw.AGENT_TELEGRAM_USERNAME.value = normalized;
        } else {
            raw.AGENT_TELEGRAM_USERNAME = normalized;
        }
    }

    var previousBot = telegram.getBotUsername();
    var settings;
    try {
        // Clay persists exactly what it is given: the response without the
        // sign-in fields.
        settings = clay.getSettings(JSON.stringify(raw), false);
    } catch (err) {
        console.error('[index] Failed to apply config response');
        return;
    }

    send(Clay.prepareSettingsForAppMessage(settings));

    var newBot = telegram.getBotUsername();
    if (newBot !== previousBot) {
        console.log('[index] Agent bot changed');
        session.resetMarkerMode();
        session.cancelActiveSession();
        history.fetchAndSendHistory();
    }

    handleConfigAuth(phone, code, password);
});

function sendAuthError(err) {
    var reason = (err && err.reason) || REASONS.GENERIC;
    send({ TELEGRAM_AUTH_ERROR: reason });
}

// Drive the Telegram sign-in from fields entered on the config page. The flow
// spans two visits: phone number first (Telegram then sends a code), then the
// code - plus the cloud password for two-step verification accounts. The
// pending sign-in survives PebbleKit JS restarts between visits.
function handleConfigAuth(phone, code, password) {
    if (!phone && !code && !password) {
        return;
    }
    if (telegram.hasSession()) {
        console.log('[index] Already signed in; ignoring sign-in fields');
        return;
    }
    var flow;
    if (code) {
        flow = telegram.provideCode(code).then(function(result) {
            if (result.success) {
                return result;
            }
            if (result.status === 'password_needed') {
                if (password) {
                    return telegram.providePassword(password);
                }
                send({ TELEGRAM_PASSWORD_NEEDED: 1 });
                return null;
            }
            throw new Error('Sign-in failed');
        });
    } else if (password) {
        flow = telegram.providePassword(password);
    } else {
        flow = telegram.startAuth(phone).then(function(result) {
            send({ TELEGRAM_CODE_SENT: result.codeLength || 1 });
            return null;
        });
    }
    flow.then(function(result) {
        if (result && result.success) {
            console.log('[index] Config-page sign-in complete');
            notifySignedIn();
        }
    }).catch(function(err) {
        console.error('[index] Config-page sign-in failed (reason ' + ((err && err.reason) || REASONS.GENERIC) + ')');
        sendAuthError(err);
    });
}

// Tell the watch we're signed in and refresh everything that depends on the
// session. resetClient() drops any client that was created before sign-in so
// the next use connects with the newly saved session.
function notifySignedIn() {
    telegram.resetClient();
    var message = { TELEGRAM_CONNECTED: 1 };
    var username = telegram.getBotUsername();
    if (username) {
        message.AGENT_TELEGRAM_USERNAME = username;
    }
    send(message);
    history.fetchAndSendHistory();
}

function main() {
    telegram.removeLegacyKeys();
    location.update();
    sendTelegramStatus();
    history.fetchAndSendHistory();
    Pebble.addEventListener('appmessage', handleAppMessage);
}

function sendTelegramStatus() {
    var isConnected = telegram.hasSession();
    console.log('Telegram connected: ' + isConnected);
    send({
        TELEGRAM_CONNECTED: isConnected ? 1 : 0
    });
}

function handleTelegramDisconnect() {
    console.log('[index] Disconnecting from Telegram');
    session.cancelActiveSession();
    return telegram.logout().then(function(result) {
        var revoked = !!(result && result.revoked);
        console.log('[index] Signed out (' + (revoked ? 'revoked on server' : 'local only') + ')');
        send({ TELEGRAM_LOGOUT_RESULT: revoked ? 1 : 2 });
        sendTelegramStatus();
    }).catch(function(err) {
        console.error('[index] Sign-out failed: ' + (err && err.message));
        // logout() clears local state even when revocation fails, so this
        // only happens on unexpected errors; make sure we're signed out.
        telegram.clearSession();
        send({ TELEGRAM_LOGOUT_RESULT: 2 });
        sendTelegramStatus();
    });
}

function handleAppMessage(e) {
    var data = (e && e.payload) || {};
    console.log('[index] App message from watch: ' + describePayload(data));
    if (typeof data.PROMPT === 'string') {
        var requestId = typeof data.REQUEST_ID === 'number' ? data.REQUEST_ID : null;
        var threadId = typeof data.THREAD_ID === 'string' ? data.THREAD_ID : null;
        session.startSession(data.PROMPT, threadId, requestId);
        return;
    }

    if ('TELEGRAM_START_AUTH' in data) {
        var phone = data.TELEGRAM_START_AUTH;
        telegram.startAuth(typeof phone === 'string' ? phone : '').then(function(result) {
            send({ TELEGRAM_CODE_SENT: result.codeLength || 1 });
        }).catch(function(err) {
            console.error('[index] startAuth failed (reason ' + ((err && err.reason) || REASONS.GENERIC) + ')');
            sendAuthError(err);
        });
        return;
    }

    if ('TELEGRAM_PROVIDE_CODE' in data) {
        var code = data.TELEGRAM_PROVIDE_CODE;
        telegram.provideCode(typeof code === 'string' || typeof code === 'number' ? String(code) : '').then(function(result) {
            if (result.success) {
                notifySignedIn();
            } else if (result.status === 'password_needed') {
                // The account has a Telegram cloud password; the watch directs
                // the user to the config page to finish signing in.
                send({ TELEGRAM_PASSWORD_NEEDED: 1 });
            } else {
                send({ TELEGRAM_AUTH_ERROR: REASONS.GENERIC });
            }
        }).catch(function(err) {
            console.error('[index] provideCode failed (reason ' + ((err && err.reason) || REASONS.GENERIC) + ')');
            sendAuthError(err);
        });
        return;
    }

    if ('TELEGRAM_DISCONNECT' in data) {
        handleTelegramDisconnect();
        return;
    }

    if ('LOCATION_ENABLED' in data) {
        config.setSetting("LOCATION_ENABLED", !!data.LOCATION_ENABLED);
        console.log("Location enabled: " + config.isLocationEnabled());
        // We need to confirm that we received this for the watch to proceed.
        send({
            LOCATION_ENABLED: data.LOCATION_ENABLED,
        });
    }
}

function doCobbleWarning() {
    if (window.cobble) {
        console.log("WARNING: Running Squire on Cobble is not supported, and has multiple known issues.");
        send({COBBLE_WARNING: 1});
    }
}

Pebble.addEventListener("ready",
    function(e) {
        // This happens before anything else because I don't trust Cobble to get through the normal flow,
        // given how many things bizarrely don't work.
        doCobbleWarning();
        console.log("Squire " + package_json['version']);

        main();
    }
);

// Export function to notify watch of Telegram status changes
exports.updateTelegramStatus = function() {
    sendTelegramStatus();
};

// Exported for testing
exports.handleAppMessage = handleAppMessage;
exports.handleTelegramDisconnect = handleTelegramDisconnect;
exports.describePayload = describePayload;
