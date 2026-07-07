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

var bundleLoader = require('./lib/bundle_loader');
var ensureTelegramBundle = bundleLoader.ensureTelegramBundle;

var location = require('./location');
var session = require('./session');
var telegram = require('./telegram');
var history = require('./history');
var Clay = require('@rebble/clay');
var clayConfig = require('./config.json');
var customConfigFunction = require('./custom_config');
var config = require('./config');
var package_json = require('package.json');


// Auth fields on the config page are handled by us, never persisted, and
// never forwarded to the watch. Everything else follows Clay's normal path.
var CONFIG_AUTH_KEYS = ['TELEGRAM_PHONE', 'TELEGRAM_CODE', 'TELEGRAM_2FA_PASSWORD'];

// autoHandleEvents is off so we can intercept the sign-in fields before Clay
// would persist and forward them.
var clay = new Clay(clayConfig, customConfigFunction, { autoHandleEvents: false });

Pebble.addEventListener('showConfiguration', function() {
    Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function(e) {
    if (!e || !e.response) {
        return;
    }
    var settings;
    try {
        settings = clay.getSettings(e.response, false);
    } catch (err) {
        console.error('[index] Failed to parse config response: ' + (err.message || err));
        return;
    }

    function fieldValue(setting) {
        var value = (setting && typeof setting === 'object') ? setting.value : setting;
        return (typeof value === 'string') ? value.trim() : '';
    }

    var phone = fieldValue(settings.TELEGRAM_PHONE);
    var code = fieldValue(settings.TELEGRAM_CODE);
    var password = fieldValue(settings.TELEGRAM_2FA_PASSWORD);

    // getSettings() has already persisted everything to clay-settings; scrub
    // the sensitive fields back out so codes and passwords never stick around
    // (and don't reappear pre-filled the next time the page opens).
    for (var i = 0; i < CONFIG_AUTH_KEYS.length; i++) {
        delete settings[CONFIG_AUTH_KEYS[i]];
    }
    try {
        var stored = JSON.parse(localStorage.getItem('clay-settings')) || {};
        for (var j = 0; j < CONFIG_AUTH_KEYS.length; j++) {
            delete stored[CONFIG_AUTH_KEYS[j]];
        }
        localStorage.setItem('clay-settings', JSON.stringify(stored));
    } catch (err) {
        console.error('[index] Failed to scrub auth fields from clay-settings: ' + (err.message || err));
    }

    Pebble.sendAppMessage(Clay.prepareSettingsForAppMessage(settings), function() {
        console.log('[index] Sent config data to watch');
    }, function(error) {
        console.error('[index] Failed to send config data: ' + JSON.stringify(error));
    });

    handleConfigAuth(phone, code, password);
});

// Drive the Telegram sign-in from fields entered on the config page. The flow
// spans two visits: phone number first (Telegram then sends a code), then the
// code — plus the cloud password for two-step verification accounts.
function handleConfigAuth(phone, code, password) {
    if (telegram.hasSession()) {
        return;
    }
    if (code || password) {
        ensureTelegramBundle();
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
                    Pebble.sendAppMessage({ TELEGRAM_PASSWORD_NEEDED: 1 });
                    throw new Error('Two-step verification password required');
                }
                throw new Error('Sign-in failed');
            });
        } else {
            flow = telegram.providePassword(password);
        }
        flow.then(function(result) {
            if (result && result.success) {
                console.log('[index] Config-page sign-in complete');
                notifySignedIn();
            }
        }).catch(function(err) {
            console.error('[index] Config-page sign-in failed: ' + (err.message || err));
            Pebble.sendAppMessage({ TELEGRAM_AUTH_ERROR: 1 });
        });
    } else if (phone) {
        ensureTelegramBundle();
        telegram.startAuth(phone).then(function(result) {
            if (result.success) {
                console.log('[index] Config-page auth started, code sent');
                Pebble.sendAppMessage({ TELEGRAM_CODE_SENT: 1 });
            } else {
                Pebble.sendAppMessage({ TELEGRAM_AUTH_ERROR: 1 });
            }
        }).catch(function(err) {
            console.error('[index] Config-page startAuth failed: ' + (err.message || err));
            Pebble.sendAppMessage({ TELEGRAM_AUTH_ERROR: 1 });
        });
    }
}

// Tell the watch we're signed in and refresh everything that depends on the
// session. resetClient() drops any client that was created before sign-in so
// the next use connects with the newly saved session.
function notifySignedIn() {
    telegram.resetClient();
    var username = telegram.getBotUsername();
    Pebble.sendAppMessage({
        TELEGRAM_CONNECTED: 1,
        AGENT_TELEGRAM_USERNAME: username
    });
    history.fetchAndSendHistory();
}

function main() {
    location.update();
    sendTelegramStatus();
    history.fetchAndSendHistory();
    Pebble.addEventListener('appmessage', handleAppMessage);
}

function sendTelegramStatus() {
    var isConnected = telegram.hasSession();
    console.log('Telegram connected: ' + isConnected);
    Pebble.sendAppMessage({
        TELEGRAM_CONNECTED: isConnected ? 1 : 0
    });
}

function handleTelegramDisconnect() {
    console.log('[index] Disconnecting from Telegram');
    telegram.logout().then(function() {
        console.log('[index] Disconnected successfully');
        sendTelegramStatus();
    }).catch(function(err) {
        console.error('[index] Failed to disconnect: ' + err.message);
    });
}

function handleAppMessage(e) {
    console.log("Inbound app message!");
    console.log(JSON.stringify(e));
    var data = e.payload;
    if (data.PROMPT) {
        console.log("Starting a new Session...");
        var s = new session.Session(data.PROMPT, data.THREAD_ID);
        s.run();
        return;
    }

    if ('TELEGRAM_START_AUTH' in data) {
        ensureTelegramBundle();
        var phone = data.TELEGRAM_START_AUTH;
        console.log('[index] Watch requested start_auth for: ' + phone);
        telegram.startAuth(phone).then(function(result) {
            console.log('[index] startAuth result: ' + JSON.stringify(result));
            if (result.success) {
                Pebble.sendAppMessage({ TELEGRAM_CODE_SENT: 1 });
            } else {
                Pebble.sendAppMessage({ TELEGRAM_AUTH_ERROR: 1 });
            }
        }).catch(function(err) {
            console.error('[index] startAuth failed: ' + err.message);
            Pebble.sendAppMessage({ TELEGRAM_AUTH_ERROR: 1 });
        });
        return;
    }

    if ('TELEGRAM_PROVIDE_CODE' in data) {
        ensureTelegramBundle();
        var code = data.TELEGRAM_PROVIDE_CODE;
        console.log('[index] Watch provided code');
        telegram.provideCode(code).then(function(result) {
            console.log('[index] provideCode result: ' + JSON.stringify(result));
            if (result.success) {
                notifySignedIn();
            } else if (result.status === 'password_needed') {
                // The account has a Telegram cloud password; the watch directs
                // the user to the config page to finish signing in.
                Pebble.sendAppMessage({ TELEGRAM_PASSWORD_NEEDED: 1 });
            } else {
                Pebble.sendAppMessage({ TELEGRAM_AUTH_ERROR: 1 });
            }
        }).catch(function(err) {
            console.error('[index] provideCode failed: ' + err.message);
            Pebble.sendAppMessage({ TELEGRAM_AUTH_ERROR: 1 });
        });
        return;
    }

    if ('TELEGRAM_DISCONNECT' in data) {
        ensureTelegramBundle();
        handleTelegramDisconnect();
        return;
    }

    if ('LOCATION_ENABLED' in data) {
        config.setSetting("LOCATION_ENABLED", !!data.LOCATION_ENABLED);
        console.log("Location enabled: " + config.isLocationEnabled());
        // We need to confirm that we received this for the watch to proceed.
        Pebble.sendAppMessage({
            LOCATION_ENABLED: data.LOCATION_ENABLED,
        });
    }
}

function doCobbleWarning() {
    if (window.cobble) {
        console.log("WARNING: Running Squire on Cobble is not supported, and has multiple known issues.");
        Pebble.sendAppMessage({COBBLE_WARNING: 1});
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

// Export message handler for testing
exports.handleAppMessage = handleAppMessage;
exports.handleTelegramDisconnect = handleTelegramDisconnect;