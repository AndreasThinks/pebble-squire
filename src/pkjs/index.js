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


var clay = new Clay(clayConfig, customConfigFunction);

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

var authInProgress = false;

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
                Pebble.sendAppMessage({ TELEGRAM_CONNECTED: 1 });
                history.fetchAndSendHistory();
            } else if (result.status === 'password_needed') {
                Pebble.sendAppMessage({ TELEGRAM_AUTH_ERROR: 1 });
            } else {
                Pebble.sendAppMessage({ TELEGRAM_AUTH_ERROR: 1 });
            }
        }).catch(function(err) {
            console.error('[index] provideCode failed: ' + err.message);
            Pebble.sendAppMessage({ TELEGRAM_AUTH_ERROR: 1 });
        });
        return;
    }

    if ('TELEGRAM_PENDING_ACTION' in data) {
        ensureTelegramBundle();
        var action = {};
        try { action = JSON.parse(data.TELEGRAM_PENDING_ACTION); } catch (e) { console.error('[index] Failed to parse TELEGRAM_PENDING_ACTION: ' + data.TELEGRAM_PENDING_ACTION); }
        console.log('[index] Telegram pending action: ' + JSON.stringify(action));
        if (action.action === 'disconnect') {
            handleTelegramDisconnect();
            return;
        }
        // Auth via config page is no longer supported; sign in from the watch instead.
        console.log('[index] Ignoring non-disconnect pending action from config page');
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

        // Timeline token only available on real devices, not emulator
        if (Pebble.platform !== 'pypkjs' && Pebble.getTimelineToken) {
            Pebble.getTimelineToken(function(token) {
                session.userToken = token;
                main();
            }, function(e) {
                console.log("Get timeline token failed???", e);
                main(); // Continue anyway
            });
        } else {
            console.log("Entering emulator mode.");
            main();
        }
    }
);

// Export function to notify watch of Telegram status changes
exports.updateTelegramStatus = function() {
    sendTelegramStatus();
};

// Export message handler for testing
exports.handleAppMessage = handleAppMessage;
exports.handleTelegramDisconnect = handleTelegramDisconnect;