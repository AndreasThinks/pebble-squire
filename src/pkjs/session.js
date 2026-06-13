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

var LOGGING_ENABLED = false;

var bundleLoader = require('./lib/bundle_loader');
var location = require('./location');
var config = require('./config');
var actions = require('./actions');
var messageQueue = require('./lib/message_queue').Queue;
var telegram = require('./telegram');

var package_json = require('package.json');

function Session(prompt, threadId) {
    this.prompt = prompt;
    this.threadId = threadId;
    this.hasOpenDialog = false;
    this.messageBuffer = '';
}

function getSettings() {
    return JSON.parse(localStorage.getItem('clay-settings')) || {};
}

Session.prototype.run = function() {
    if (LOGGING_ENABLED) {
        messageQueue.startLogging();
    }
    console.log("Starting Telegram session...");

    var self = this;

    // Check if we have a Telegram session
    if (!telegram.hasSession()) {
        this.enqueue({
            CHAT: 'Not connected to Telegram. Please configure your Telegram connection in the app settings.'
        });
        this.enqueue({
            CHAT_DONE: true
        });
        return;
    }

    // Build the message with metadata
    var message = this.buildMessage();

    // Send to agent via Telegram
    this.sendToAgent(message).catch(function(error) {
        console.error('Telegram session error:', error);
        self.enqueue({
            CHAT: 'Error communicating with agent: ' + error.message
        });
        self.enqueue({
            CHAT_DONE: true
        });
    });
};

Session.prototype.buildMessage = function() {
    var tzOffset = -(new Date()).getTimezoneOffset();
    var locationLine = '';
    if (location.isReady() && config.isLocationEnabled()) {
        var loc = location.getPos();
        locationLine = ' The user is located at lat ' + loc.lat + ', lon ' + loc.lon + '.';
    }

    var systemPrompt = '<system>Respond concisely for a tiny smartwatch screen. Keep answers brief but include important details. The user is looking at their watch and waiting, so prioritize speed. The user timezone offset is ' + tzOffset + ' minutes from UTC.' + locationLine + '</system>';
    var formattedMessage = systemPrompt + '\n\n' + this.prompt.trim();
    if (this.threadId) {
        formattedMessage = '[thread:' + this.threadId + '] ' + formattedMessage;
    }

    return formattedMessage;
};

Session.prototype.sendToAgent = function(message) {
    var self = this;
    var botUsername = telegram.getBotUsername();

    return new Promise(function(resolve, reject) {
        bundleLoader.ensureTelegramBundle();

        if (typeof TelegramClient !== 'undefined' && typeof StringSession !== 'undefined') {
            self.sendViaGramJS(message, botUsername, resolve, reject);
        } else {
            reject(new Error('Telegram client not available. Please reconnect.'));
        }
    });
};

Session.prototype.sendViaGramJS = function(message, botUsername, resolve, reject) {
    var self = this;
    var cleanUsername = botUsername.replace(/^@/, '');

    telegram.initClient().then(function() {
        var tgClient = telegram.getClient();
        return tgClient.sendMessage(cleanUsername, { message: message });
    }).then(function(result) {
        console.log('[session] Message sent to', botUsername, 'id:', result ? result.id : 'unknown');
        self.listenForResponse(telegram.getClient(), cleanUsername, resolve, reject);
    }).catch(function(error) {
        console.error('[session] GramJS error:', error);
        reject(error);
    });
};

Session.prototype.listenForResponse = function(client, botUsername, resolve, reject) {
    var self = this;
    var timeout = 120000;
    var resolved = false;
    var processedIds = {};

    function done(result) {
        if (resolved) return;
        resolved = true;
        clearTimeout(timeoutId);
        resolve(result);
    }

    function fail(error) {
        if (resolved) return;
        resolved = true;
        clearTimeout(timeoutId);
        reject(error);
    }

    var timeoutId = setTimeout(function() {
        fail(new Error('Timeout waiting for response from agent'));
    }, timeout);

    console.log('[session] listenForResponse registering handler, NewMessage available: ' + (typeof NewMessage !== 'undefined'));

    if (typeof NewMessage !== 'undefined') {
        try {
            client.addEventHandler(function(event) {
                try {
                    if (resolved) return;
                    var msg = event.message;
                    if (!msg || !msg.message) return;
                    if (processedIds[msg.id]) return;
                    processedIds[msg.id] = true;
                    console.log('[session] raw message out=', msg.out, 'id=', msg.id, 'text=', formatLoggedMessage(msg.message));
                    self.handleIncomingMessage(msg.message, done);
                } catch (err) {
                    console.error('[session] Error handling message:', err);
                }
            }, new NewMessage({ incoming: true }));
            console.log('[session] Event handler registered successfully (incoming only)');
        } catch (err) {
            console.error('[session] Failed to register event handler:', err);
            fail(new Error('Failed to register Telegram event handler'));
        }
    } else {
        console.error('[session] NewMessage is undefined, cannot register event handler');
        fail(new Error('Telegram event support not available'));
    }

    // Fallback: poll the chat since NewMessage events are unreliable on Pebble.
    self.pollForMessages(client, botUsername, Date.now(), timeoutId, done, processedIds);
};

Session.prototype.pollForMessages = function(client, botUsername, startTime, timeoutId, done, processedIds) {
    var self = this;
    var pollInterval = 2000;
    var stopped = false;

    function stop() {
        stopped = true;
        clearTimeout(timeoutId);
    }

    var originalDone = done;
    done = function(result) {
        stop();
        originalDone(result);
    };

    function poll() {
        if (stopped || Date.now() - startTime > 120000) {
            console.log('[session] poll stopping, stopped=' + stopped + ' elapsed=' + (Date.now() - startTime));
            return;
        }

        console.log('[session] polling bot=' + botUsername);
        try {
            client.getMessages(botUsername, { limit: 5 }).then(function(messages) {
                if (stopped) return;
                console.log('[session] poll got ' + (messages ? messages.length : 0) + ' messages');
                if (messages && messages.length > 0) {
                    for (var i = messages.length - 1; i >= 0; i--) {
                        var msg = messages[i];
                        if (!msg || !msg.message) continue;
                        console.log('[session] poll msg out=' + msg.out + ' id=' + msg.id + ' date=' + msg.date);
                        if (msg.out === true || msg.out === 1) continue;
                        if (processedIds[msg.id]) continue;
                        if (msg.date * 1000 <= startTime) continue;
                        processedIds[msg.id] = true;
                        console.log('[session] polled agent message id=', msg.id, 'text=', formatLoggedMessage(msg.message));
                        self.handleIncomingMessage(msg.message, done);
                        return;
                    }
                }
                setTimeout(poll, pollInterval);
            }).catch(function(err) {
                if (stopped) return;
                console.log('[session] Poll error (non-fatal):', err.message || err);
                setTimeout(poll, pollInterval);
            });
        } catch (e) {
            if (stopped) return;
            console.log('[session] Poll error:', e);
            setTimeout(poll, pollInterval);
        }
    }

    poll();
};

function formatLoggedMessage(message) {
    if (!message) return '';
    var withoutSystem = message.replace(/<system>[\s\S]*?<\/system>/g, '').trim();
    if (withoutSystem.length <= 100) return withoutSystem;
    return withoutSystem.substring(0, 50) + '...' + withoutSystem.substring(withoutSystem.length - 50);
}

Session.prototype.handleIncomingMessage = function(message, resolve) {
    console.log('Received message:', formatLoggedMessage(message));

    // Non-streaming mode: a single plain-text message is the complete response.
    this.hasOpenDialog = true;
    this.enqueue({
        CHAT: message
    });
    this.hasOpenDialog = false;
    this.enqueue({
        CHAT_DONE: true
    });
    resolve({ complete: true });
};

Session.prototype.enqueue = function(message) {
    messageQueue.enqueue(message);
};

Session.prototype.dequeue = function() {
    messageQueue.dequeue();
};

// Keep the original WebSocket-based session as fallback
Session.prototype.runLegacy = function() {
    if (LOGGING_ENABLED) {
        messageQueue.startLogging();
    }
    console.log("Opening websocket connection...");

    var API_URL = require('./urls').QUERY_URL;
    var url = API_URL + '?prompt=' + encodeURIComponent(this.prompt) + '&token=' + exports.userToken;

    if (location.isReady() && config.isLocationEnabled()) {
        var loc = location.getPos();
        url += '&lon=' + loc.lon + '&lat=' + loc.lat;
    } else {
        url += '&location=unknown';
    }
    if (this.threadId) {
        url += '&threadId=' + encodeURIComponent(this.threadId);
    }
    url += '&tzOffset=' + (-(new Date()).getTimezoneOffset());
    url += '&actions=' + actions.getSupportedActions().join(',');
    var settings = getSettings();
    url += '&units=' + settings['UNIT_PREFERENCE'] || '';
    url += '&lang=' + settings['LANGUAGE_CODE'] || '';
    url += '&version=' + package_json['version'];

    console.log(url);
    this.ws = new WebSocket(url);
    this.ws.addEventListener('message', this.handleLegacyMessage.bind(this));
    this.ws.addEventListener('close', this.handleClose.bind(this));
};

Session.prototype.handleLegacyMessage = function(event) {
    var message = event.data;
    console.log(message);

    if (message[0] == 'c') {
        this.hasOpenDialog = true;
        this.enqueue({
            CHAT: message.substring(1)
        });
    } else if (message[0] == 'd') {
        this.hasOpenDialog = false;
        this.enqueue({
            CHAT_DONE: true
        });
        if (LOGGING_ENABLED) {
            console.log(JSON.stringify(messageQueue.getLog()));
            messageQueue.stopLogging();
        }
    } else if (message[0] == 'a') {
        actions.handleAction(this, this.ws, message.substring(1));
    } else if (message[0] == 't') {
        this.enqueue({
            THREAD_ID: message.substring(1)
        });
    } else if (message[0] == 'w') {
        this.enqueue({
            WARNING: message.substring(1)
        });
    }
};

Session.prototype.handleClose = function(event) {
    console.log("Connection closed. Code: " + event.code + ". Reason: \"" + event.reason + "\". Was clean: " + event.wasClean);
    this.enqueue({
        CLOSE_CODE: event.code,
        CLOSE_REASON: event.reason,
        CLOSE_WAS_CLEAN: event.wasClean
    });
};

exports.Session = Session;
exports.userToken = null;