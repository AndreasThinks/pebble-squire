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
var widgets = require('./widgets');
var messageQueue = require('./lib/message_queue').Queue;
var features = require('./features');
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
        self.listenForResponse(telegram.getClient(), botUsername, resolve, reject);
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

    var timeoutId = setTimeout(function() {
        done({ complete: false, error: 'Timeout waiting for response from agent' });
    }, timeout);

    if (typeof NewMessage !== 'undefined') {
        client.addEventHandler(function(event) {
            try {
                if (resolved) return;
                var msg = event.message;
                if (!msg || !msg.message) return;
                // Ignore the user's own outgoing messages (Telegram echoes them back).
                // msg.out may be boolean true or the number 1 depending on GramJS version/platform.
                console.log('[session] raw message out=', msg.out, 'id=', msg.id, 'text=', msg.message.substring(0, 50));
                if (msg.out === true || msg.out === 1) return;
                if (processedIds[msg.id]) return;
                processedIds[msg.id] = true;
                self.handleIncomingMessage(msg.message, done);
            } catch (err) {
                console.error('[session] Error handling message:', err);
            }
        }, new NewMessage({}));
    }
};

Session.prototype.pollForMessages = function(client, botUsername, startTime, timeoutId, resolve, reject) {
    var self = this;
    var pollInterval = 2000; // Poll every 2 seconds

    function poll() {
        if (Date.now() - startTime > 120000) {
            clearTimeout(timeoutId);
            return;
        }

        // Use getMessages to fetch new messages
        // This is a simplified approach
        try {
            client.getMessages(botUsername.replace(/^@/, ''), { limit: 1 }).then(function(messages) {
                if (messages && messages.length > 0) {
                    var latestMsg = messages[0];
                    // Check if this is a new message (after our sent message)
                    if (latestMsg && latestMsg.date * 1000 > startTime) {
                        self.handleIncomingMessage(latestMsg.message, resolve);
                    }
                }
            }).catch(function(err) {
                console.log('Poll error (non-fatal):', err);
            });
        } catch (e) {
            console.log('Poll error:', e);
        }

        setTimeout(poll, pollInterval);
    }

    poll();
};

Session.prototype.handleIncomingMessage = function(message, resolve) {
    console.log('Received message:', message.substring(0, 100));

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

Session.prototype.processWidget = function(widgetData) {
    widgets.handleWidget(this, widgetData);
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
    url += '&widgets=weather,number';
    if (features.FEATURE_MAP_WIDGET) {
        url += ',map';
    }
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
        var widgetRegex = /<<!!WIDGET:(.+?)!!>>/;
        var content = message.substring(1);
        var match;

        while (content.length > 0) {
            match = widgetRegex.exec(content);
            if (!match) {
                break;
            }
            var widget = match[1];
            console.log("Widget found: " + widget);
            var start = match.index;
            if (start != 0) {
                this.enqueue({
                    CHAT: content.substring(0, start)
                });
            }
            this.processWidget(widget);
            this.hasOpenDialog = false;
            content = content.substring(match.index + match[0].length);
        }
        if (content.length > 0) {
            this.hasOpenDialog = true;
            this.enqueue({
                CHAT: content
            });
        }
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