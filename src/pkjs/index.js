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

function clearTelegramCodeField() {
    try {
        var settings = JSON.parse(localStorage.getItem('clay-settings')) || {};
        if (settings.TELEGRAM_CODE) {
            delete settings.TELEGRAM_CODE;
            localStorage.setItem('clay-settings', JSON.stringify(settings));
            console.log('[index] Cleared TELEGRAM_CODE from clay settings');
        }
    } catch (e) {}
}

function handleTelegramStartAuth(action) {
    console.log('[index] Starting Telegram auth for: ' + action.phoneNumber);
    telegram.startAuth(action.phoneNumber).then(function(result) {
        console.log('[index] Auth result: ' + JSON.stringify(result));
        if (result.success) clearTelegramCodeField();
        authInProgress = false;
        sendTelegramStatus();
    }).catch(function(err) {
        console.error('[index] Auth failed: ' + err.message);
        console.error('[index] Auth error stack: ' + (err.stack || 'no stack'));
        authInProgress = false;
        sendTelegramStatus();
    });
}

function handleTelegramProvideCode(action) {
    console.log('[index] Providing verification code');
    telegram.provideCode(action.code).then(function(result) {
        console.log('[index] ProvideCode result: ' + JSON.stringify(result));
        if (result.success) {
            telegram.resetClient();
            clearTelegramCodeField();
            history.fetchAndSendHistory();
        }
        sendTelegramStatus();
    }).catch(function(err) {
        console.error('[index] ProvideCode failed: ' + err.message);
        sendTelegramStatus();
    });
}

function handleTelegramProvidePassword(action) {
    console.log('[index] Providing 2FA password');
    var accepted = telegram.providePassword(action.password);
    if (!accepted) {
        console.error('[index] No pending password request');
    }
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

var authInProgress = false;

function handleTelegramAction(action) {
    if (action.action === 'start_auth' && action.phoneNumber) {
        if (authInProgress) {
            console.log('[index] Auth already in progress, skipping start_auth');
            return;
        }
        authInProgress = true;
        handleTelegramStartAuth(action);
    } else if (action.action === 'provide_code' && action.code) {
        handleTelegramProvideCode(action);
    } else if (action.action === 'provide_password' && action.password) {
        handleTelegramProvidePassword(action);
    } else if (action.action === 'disconnect') {
        handleTelegramDisconnect();
    } else {
        console.log('[index] Unknown or incomplete telegram action: ' + JSON.stringify(action));
    }
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

    if ('TELEGRAM_PENDING_ACTION' in data) {
        ensureTelegramBundle();
        var action = {};
        try { action = JSON.parse(data.TELEGRAM_PENDING_ACTION); } catch (e) { console.error('[index] Failed to parse TELEGRAM_PENDING_ACTION: ' + data.TELEGRAM_PENDING_ACTION); }
        console.log('[index] Telegram pending action: ' + JSON.stringify(action));
        telegram.initClient().then(function() {
            console.log('[index] Telegram client initialized, connected: ' + telegram.isClientConnected());
            handleTelegramAction(action);
        }).catch(function(err) {
            console.error('[index] Failed to initialize Telegram client: ' + err.message);
            console.error('[index] Error stack: ' + (err.stack || 'no stack'));
        });
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
exports.handleTelegramAction = handleTelegramAction;
exports.handleTelegramStartAuth = handleTelegramStartAuth;
exports.handleTelegramProvideCode = handleTelegramProvideCode;
exports.handleTelegramProvidePassword = handleTelegramProvidePassword;
exports.handleTelegramDisconnect = handleTelegramDisconnect;