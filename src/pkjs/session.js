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
var config = require('./config');
var messageQueue = require('./lib/message_queue').Queue;
var telegram = require('./telegram');

/*
 * One Session = one prompt sent to the agent bot and the reply streamed back
 * to the watch. Protocol and completion policy (see also README "Agent
 * integration notes"):
 *
 * - Only one session is active at a time; starting another cancels the
 *   previous one, removing its Telegram handlers and dropping its queued,
 *   undelivered watch messages.
 * - Every message to the watch carries the REQUEST_ID the watch sent with the
 *   prompt, so the watch ignores anything from an abandoned request.
 * - Listeners are registered before the prompt is sent. Only messages in the
 *   private chat with the configured bot account, sent after our prompt
 *   (higher message id) and not explicitly replying to some other message,
 *   count as the reply. Everything else - other chats, groups, earlier
 *   messages - is ignored without being logged.
 * - Consecutive bot messages are joined with a blank line. Edits to them
 *   (agents that stream by editing) update the text: an edit that only
 *   appends is sent as more CHAT; any other change resends the whole reply
 *   after CHAT_RESET, so edits never show up as duplicate output.
 * - The reply is complete when:
 *     * a message (or edit) ends with "[done]" (stripped before display), or
 *     * nothing (message, edit or typing indicator) has happened for
 *       QUIET_COMPLETE_MS after at least one reply. Once a bot has been seen
 *       using "[done]", it is trusted to keep doing so and the quiet window
 *       becomes MARKER_MODE_QUIET_MS instead.
 *   Before the first reply we wait up to FIRST_REPLY_TIMEOUT_MS of silence,
 *   and never longer than HARD_TIMEOUT_MS overall; those end with a WARNING.
 * - Edits arriving after completion can't be shown.
 */

var QUIET_COMPLETE_MS = 10000;
var MARKER_MODE_QUIET_MS = 60000;
var FIRST_REPLY_TIMEOUT_MS = 120000;
var HARD_TIMEOUT_MS = 300000;
var MAX_PROMPT_CHARS = 3000;
var MAX_BUFFERED_EVENTS = 50;
var MARKER_MODE_KEY = 'squire_agent_uses_done_marker';

var END_MARKER_RE = /\s*\[done\]\s*$/i;
var THREAD_ID_RE = /^[A-Za-z0-9_-]{1,36}$/;
var MEDIA_PLACEHOLDER = '[Media message - open Telegram to view it]';

var activeSession = null;
var localGroupCounter = 0;

function idString(value) {
    if (value === null || value === undefined) {
        return null;
    }
    return String(value);
}

function idEquals(a, b) {
    var sa = idString(a);
    return sa !== null && sa === idString(b);
}

function idGreater(a, b) {
    // Telegram message ids fit comfortably in a double.
    return Number(a) > Number(b);
}

function Session(prompt, threadId, requestId) {
    this.prompt = typeof prompt === 'string' ? prompt : '';
    this.threadId = (typeof threadId === 'string' && THREAD_ID_RE.test(threadId)) ? threadId : null;
    this.requestId = (typeof requestId === 'number' && requestId > 0) ? requestId : null;
    this.group = this.requestId !== null ? 'request:' + this.requestId : 'local:' + (++localGroupCounter);
    this.finished = false;
    this.client = null;
    this.botId = null;
    this.botUsername = '';
    this.sentId = null;
    this.buffered = [];
    this.segments = {};
    this.segmentOrder = [];
    this.shown = '';
    this.typingSent = false;
    this.handlers = [];
    this.activityTimer = null;
    this.hardTimer = null;
    var self = this;
    this.done = new Promise(function(resolve) { self.resolveDone = resolve; });
}

Session.prototype.emit = function(message, terminal) {
    var self = this;
    if (this.requestId !== null) {
        message.REQUEST_ID = this.requestId;
    }
    messageQueue.enqueue(message, {
        group: this.group,
        terminal: !!terminal,
        onFailed: function() { self.onDeliveryFailed(); }
    });
};

Session.prototype.run = function() {
    var self = this;
    if (!telegram.hasSession()) {
        this.finish('Not signed in to Telegram. Sign in from the watch or from Squire\'s settings on your phone.');
        return this.done;
    }
    if (!telegram.isAppConfigured()) {
        this.finish('This build of Squire has no Telegram API credentials, so it can\'t talk to Telegram.');
        return this.done;
    }
    this.botUsername = telegram.getBotUsername();
    if (!this.botUsername) {
        this.finish('No agent bot is set up. Enter your bot\'s username (e.g. @MyHermesBot) in Squire\'s settings on your phone.');
        return this.done;
    }
    var trimmed = this.prompt.trim();
    if (!trimmed) {
        this.finish('Nothing to send.');
        return this.done;
    }
    if (trimmed.length > MAX_PROMPT_CHARS) {
        this.finish('That prompt is too long to send.');
        return this.done;
    }

    var message = this.buildMessage();
    console.log('[session] Sending prompt (' + trimmed.length + ' chars) to agent');
    telegram.initClient().then(function() {
        if (self.finished) return null;
        self.client = telegram.getClient();
        if (!self.client) {
            throw new Error('Telegram client not available. Please reconnect.');
        }
        return self.resolveBot();
    }).then(function(entity) {
        if (self.finished || !entity) return null;
        // Listen before sending, so a fast reply can't slip past us.
        self.startListening();
        if (self.finished) return null;
        return self.client.sendMessage(entity, { message: message });
    }).then(function(sent) {
        if (self.finished || !sent) return;
        if (sent.id === undefined || sent.id === null) {
            throw new Error('Telegram did not confirm the message was sent.');
        }
        self.sentId = sent.id;
        console.log('[session] Prompt sent, waiting for reply');
        self.hardTimer = setTimeout(function() { self.onHardTimeout(); }, HARD_TIMEOUT_MS);
        self.armActivityTimer();
        var buffered = self.buffered;
        self.buffered = [];
        for (var i = 0; i < buffered.length && !self.finished; i++) {
            self.acceptBotMessage(buffered[i].message, buffered[i].isEdit);
        }
    }).catch(function(err) {
        if (self.finished) return;
        console.error('[session] Failed: ' + describeError(err));
        self.finish(userFacingError(err, self.botUsername));
    });
    return this.done;
};

Session.prototype.resolveBot = function() {
    var self = this;
    var username = this.botUsername.replace(/^@/, '');
    return Promise.resolve(this.client.getEntity(username)).then(function(entity) {
        if (self.finished) return null;
        if (!entity || entity.className !== 'User' || entity.id === undefined || entity.id === null) {
            self.finish(self.botUsername + ' isn\'t a Telegram bot account. Check the agent bot username in Squire\'s settings.');
            return null;
        }
        if (entity.bot !== true) {
            // Never send prompts (and location) to a person by mistake.
            self.finish(self.botUsername + ' is a regular user, not a bot. Squire only talks to agent bots.');
            return null;
        }
        self.botId = entity.id;
        return entity;
    }, function(err) {
        if (self.finished) return null;
        console.error('[session] Could not resolve agent bot: ' + describeError(err));
        self.finish('Couldn\'t find ' + self.botUsername + ' on Telegram. Check the agent bot username in Squire\'s settings.');
        return null;
    });
};

Session.prototype.startListening = function() {
    var self = this;
    var builders = telegram.getEventBuilders();
    if (!builders.NewMessage) {
        this.finish('Telegram event support is not available.');
        return;
    }
    function add(handler, builder) {
        try {
            self.client.addEventHandler(handler, builder);
            self.handlers.push({ handler: handler, builder: builder });
            return true;
        } catch (err) {
            console.error('[session] Failed to register event handler: ' + describeError(err));
            return false;
        }
    }
    var messageHandler = function(event) {
        try {
            self.onMessage(event && event.message, false);
        } catch (err) {
            console.error('[session] Error handling message: ' + describeError(err));
        }
    };
    if (!add(messageHandler, new builders.NewMessage({ incoming: true }))) {
        this.finish('Couldn\'t listen for the agent\'s reply.');
        return;
    }
    if (builders.Raw) {
        var rawHandler = function(update) {
            try {
                self.onRawUpdate(update);
            } catch (err) {
                console.error('[session] Error handling update: ' + describeError(err));
            }
        };
        add(rawHandler, new builders.Raw({}));
    }
};

Session.prototype.stopListening = function() {
    var handlers = this.handlers;
    this.handlers = [];
    for (var i = 0; i < handlers.length; i++) {
        try {
            this.client.removeEventHandler(handlers[i].handler, handlers[i].builder);
        } catch (err) {
            console.log('[session] removeEventHandler failed: ' + describeError(err));
        }
    }
};

// True if msg is an incoming message in the private chat with our bot.
Session.prototype.isFromBot = function(msg) {
    if (!msg || msg.out || this.botId === null) {
        return false;
    }
    var peer = msg.peerId;
    if (!peer || peer.chatId !== undefined || peer.channelId !== undefined ||
        (peer.className && peer.className !== 'PeerUser')) {
        return false;
    }
    if (!idEquals(peer.userId, this.botId)) {
        return false;
    }
    var sender = msg.fromId && msg.fromId.userId !== undefined ? msg.fromId.userId : msg.senderId;
    if (sender !== undefined && sender !== null && !idEquals(sender, this.botId)) {
        return false;
    }
    return true;
};

Session.prototype.onMessage = function(msg, isEdit) {
    if (this.finished || !this.isFromBot(msg)) {
        return;
    }
    if (this.sentId === null) {
        // Our prompt isn't confirmed yet, so we can't tell old from new.
        if (this.buffered.length < MAX_BUFFERED_EVENTS) {
            this.buffered.push({ message: msg, isEdit: isEdit });
        }
        return;
    }
    this.acceptBotMessage(msg, isEdit);
};

Session.prototype.onRawUpdate = function(update) {
    if (this.finished || !update) {
        return;
    }
    var name = update.className;
    if (name === 'UpdateUserTyping') {
        if (idEquals(update.userId, this.botId) && this.sentId !== null) {
            this.onTyping();
        }
    } else if (name === 'UpdateEditMessage') {
        this.onMessage(update.message, true);
    }
};

Session.prototype.onTyping = function() {
    if (!this.shown && !this.typingSent) {
        this.typingSent = true;
        this.emit({ TYPING: 1 });
    }
    this.armActivityTimer();
};

Session.prototype.acceptBotMessage = function(msg, isEdit) {
    var id = idString(msg.id);
    if (id === null || !idGreater(msg.id, this.sentId)) {
        // Sent before our prompt: not part of this reply.
        return;
    }
    var replyTo = msg.replyTo && msg.replyTo.replyToMsgId;
    if (replyTo !== undefined && replyTo !== null && !idEquals(replyTo, this.sentId) &&
        !Object.prototype.hasOwnProperty.call(this.segments, idString(replyTo))) {
        // Explicitly answering some other message (e.g. an earlier prompt).
        return;
    }
    var known = Object.prototype.hasOwnProperty.call(this.segments, id);
    if (known && !isEdit) {
        return;  // duplicate delivery
    }
    var text = typeof msg.message === 'string' ? msg.message : '';
    if (!text && msg.media) {
        text = MEDIA_PLACEHOLDER;
    }
    var isFinal = END_MARKER_RE.test(text);
    if (isFinal) {
        text = text.replace(END_MARKER_RE, '');
    }
    if (!known) {
        this.segmentOrder.push(id);
        this.segmentOrder.sort(function(a, b) { return Number(a) - Number(b); });
    }
    this.segments[id] = text;
    this.showTranscript();
    if (isFinal) {
        rememberMarkerMode(this.botUsername);
        console.log('[session] End marker received, reply complete');
        this.finish(null);
        return;
    }
    this.armActivityTimer();
};

Session.prototype.transcript = function() {
    var parts = [];
    for (var i = 0; i < this.segmentOrder.length; i++) {
        var text = this.segments[this.segmentOrder[i]];
        if (text) {
            parts.push(text);
        }
    }
    return parts.join('\n\n');
};

Session.prototype.showTranscript = function() {
    var full = this.transcript();
    if (full === this.shown) {
        return;
    }
    if (full.indexOf(this.shown) === 0) {
        this.emit({ CHAT: full.substring(this.shown.length) });
    } else {
        // Something already on the watch changed: replace the whole reply.
        this.emit({ CHAT_RESET: 1 });
        if (full) {
            this.emit({ CHAT: full });
        }
    }
    this.shown = full;
};

Session.prototype.armActivityTimer = function() {
    var self = this;
    if (this.finished) {
        return;
    }
    clearTimeout(this.activityTimer);
    var hasReply = this.segmentOrder.length > 0;
    var delay = !hasReply ? FIRST_REPLY_TIMEOUT_MS
        : (usesMarkerMode(this.botUsername) ? MARKER_MODE_QUIET_MS : QUIET_COMPLETE_MS);
    this.activityTimer = setTimeout(function() {
        self.activityTimer = null;
        if (self.segmentOrder.length > 0) {
            console.log('[session] Reply quiet for ' + (delay / 1000) + 's, treating as complete');
            self.finish(null);
        } else {
            self.finish('No reply from ' + self.botUsername + ' yet. It may still answer in Telegram.');
        }
    }, delay);
};

Session.prototype.onHardTimeout = function() {
    this.hardTimer = null;
    if (this.segmentOrder.length > 0) {
        this.finish('Stopped waiting for the rest of the reply after ' + (HARD_TIMEOUT_MS / 60000) + ' minutes.');
    } else {
        this.finish('No reply from ' + this.botUsername + ' after ' + (HARD_TIMEOUT_MS / 60000) + ' minutes.');
    }
};

Session.prototype.cleanup = function() {
    this.finished = true;
    clearTimeout(this.activityTimer);
    clearTimeout(this.hardTimer);
    this.activityTimer = null;
    this.hardTimer = null;
    this.buffered = [];
    if (this.client) {
        this.stopListening();
    }
    if (activeSession === this) {
        activeSession = null;
    }
};

// Ends the session, telling the watch: an optional WARNING, then CHAT_DONE.
Session.prototype.finish = function(warning) {
    if (this.finished) {
        return;
    }
    this.cleanup();
    if (warning) {
        this.emit({ WARNING: warning });
    }
    this.emit({ CHAT_DONE: 1 });
    this.resolveDone({ complete: !warning });
};

// Abandons the session without telling the watch (it has moved on).
Session.prototype.cancel = function() {
    if (this.finished) {
        return;
    }
    console.log('[session] Cancelling superseded request');
    this.cleanup();
    messageQueue.purgeGroup(this.group);
    this.resolveDone({ complete: false, cancelled: true });
};

// Part of the reply couldn't be delivered to the watch: stop, and say so
// rather than leaving a silently incomplete reply.
Session.prototype.onDeliveryFailed = function() {
    var wasFinished = this.finished;
    this.cleanup();
    this.emit({ WARNING: 'Part of the reply couldn\'t be delivered to the watch. Check the Bluetooth connection and try again.' }, true);
    this.emit({ CHAT_DONE: 1 }, true);
    if (!wasFinished) {
        this.resolveDone({ complete: false, deliveryFailed: true });
    }
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

function rememberMarkerMode(botUsername) {
    try {
        localStorage.setItem(MARKER_MODE_KEY, botUsername.toLowerCase());
    } catch (e) {}
}

function usesMarkerMode(botUsername) {
    try {
        return !!botUsername && localStorage.getItem(MARKER_MODE_KEY) === botUsername.toLowerCase();
    } catch (e) {
        return false;
    }
}

function resetMarkerMode() {
    try {
        localStorage.removeItem(MARKER_MODE_KEY);
    } catch (e) {}
}

// Telegram errors are codes like USERNAME_NOT_OCCUPIED; never user content.
function describeError(err) {
    return String((err && (err.errorMessage || err.code || err.message)) || err);
}

function userFacingError(err, botUsername) {
    var text = describeError(err);
    if (err && err.code === 'APP_NOT_CONFIGURED') {
        return 'This build of Squire has no Telegram API credentials, so it can\'t talk to Telegram.';
    }
    if (/re-authenticate|session expired|No Telegram session/i.test(text)) {
        return 'Telegram session expired. Sign in again from the watch or Squire\'s settings.';
    }
    if (/USERNAME_(NOT_OCCUPIED|INVALID)|Cannot find any entity|No user has/i.test(text)) {
        return 'Couldn\'t find ' + botUsername + ' on Telegram. Check the agent bot username in Squire\'s settings.';
    }
    if (/YOU_BLOCKED_USER|USER_IS_BLOCKED|PEER_ID_INVALID/.test(text)) {
        return 'Telegram won\'t deliver messages to ' + botUsername + '. Unblock it or start a chat with it in Telegram first.';
    }
    if (/timed out|timeout|Not connected|network/i.test(text)) {
        return 'Couldn\'t reach Telegram. Check your phone\'s connection.';
    }
    return 'Error communicating with agent: ' + text;
}

/**
 * Starts a session for a prompt from the watch, cancelling any session still
 * in progress (one request at a time).
 */
function startSession(prompt, threadId, requestId) {
    if (activeSession) {
        activeSession.cancel();
    }
    var session = new Session(prompt, threadId, requestId);
    activeSession = session;
    session.run();
    return session;
}

function cancelActiveSession() {
    if (activeSession) {
        activeSession.cancel();
    }
}

exports.Session = Session;
exports.startSession = startSession;
exports.cancelActiveSession = cancelActiveSession;
exports.getActiveSession = function() { return activeSession; };
exports.resetMarkerMode = resetMarkerMode;
exports.END_MARKER_RE = END_MARKER_RE;
exports.QUIET_COMPLETE_MS = QUIET_COMPLETE_MS;
exports.MARKER_MODE_QUIET_MS = MARKER_MODE_QUIET_MS;
exports.FIRST_REPLY_TIMEOUT_MS = FIRST_REPLY_TIMEOUT_MS;
exports.HARD_TIMEOUT_MS = HARD_TIMEOUT_MS;
