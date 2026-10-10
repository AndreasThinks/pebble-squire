var messageQueue = require('./lib/message_queue');
var telegram = require('./telegram');

/*
 * Sends recent conversation history to the watch:
 *
 *   HISTORY_BEGIN, then HISTORY_THREAD_ID (optional), then HISTORY_PROMPT /
 *   HISTORY_RESPONSE oldest first, then HISTORY_DONE = 1 (ok) or 2 (error).
 *
 * The watch stores each entry in a fixed 512-byte buffer (src/c/converse/
 * history.h), so entries are cut to HISTORY_MAX_ENTRY_BYTES on a UTF-8
 * character boundary here, which also keeps every message far below the
 * watch's 5000-byte inbox. The queue delivers messages in order, and every
 * batch starts with HISTORY_BEGIN, so overlapping fetches can't interleave.
 */

var HISTORY_LIMIT = 8;
var FETCH_LIMIT = 50;
var HISTORY_MAX_ENTRY_BYTES = 500;
var STATUS_OK = 1;
var STATUS_ERROR = 2;

var END_MARKER_RE = /\s*\[done\]\s*$/i;
var THREAD_RE = /^\[thread:([A-Za-z0-9_-]{1,36})\]\s?/;

var generation = 0;

function isOwn(msg) {
    return msg.out === true || msg.out === 1;
}

function promptText(text) {
    return text.replace(THREAD_RE, '').replace(/\s*<system>[\s\S]*?<\/system>\s*/, '').trim();
}

// Builds watch history (oldest first) from messages (newest first, as
// returned by getMessages).
function buildEntries(messages) {
    var entries = [];
    for (var i = 0; i < messages.length && entries.length < HISTORY_LIMIT; i++) {
        var msg = messages[i];
        if (!msg || typeof msg.message !== 'string' || msg.message.length === 0) {
            continue;
        }
        var text = isOwn(msg) ? promptText(msg.message) : msg.message.replace(END_MARKER_RE, '');
        if (text.length === 0) {
            continue;
        }
        entries.push({
            type: isOwn(msg) ? 'prompt' : 'response',
            text: messageQueue.truncateUtf8(text, HISTORY_MAX_ENTRY_BYTES)
        });
    }
    entries.reverse();
    return entries;
}

function findThreadId(messages) {
    for (var j = 0; j < messages.length; j++) {
        var m = messages[j];
        if (m && isOwn(m) && typeof m.message === 'string') {
            var match = THREAD_RE.exec(m.message);
            if (match) {
                return match[1];
            }
        }
    }
    return null;
}

function send(message) {
    messageQueue.Queue.enqueue(message);
}

function sendBatch(entries, threadId) {
    send({ HISTORY_BEGIN: 1 });
    if (threadId) {
        send({ HISTORY_THREAD_ID: threadId });
    }
    for (var k = 0; k < entries.length; k++) {
        if (entries[k].type === 'prompt') {
            send({ HISTORY_PROMPT: entries[k].text });
        } else {
            send({ HISTORY_RESPONSE: entries[k].text });
        }
    }
    send({ HISTORY_DONE: STATUS_OK });
}

function sendEmpty(status) {
    send({ HISTORY_BEGIN: 1 });
    send({ HISTORY_DONE: status });
}

function fetchAndSendHistory() {
    var mine = ++generation;
    function current() {
        // A newer fetch has started; it will send its own batch.
        return mine === generation;
    }

    if (!telegram.hasSession()) {
        console.log('[history] No Telegram session, nothing to fetch');
        sendEmpty(STATUS_OK);
        return Promise.resolve();
    }
    var botUsername = telegram.getBotUsername();
    if (!botUsername || !telegram.isAppConfigured()) {
        console.log('[history] No agent bot configured (or app not configured), nothing to fetch');
        sendEmpty(STATUS_OK);
        return Promise.resolve();
    }

    return telegram.initClient().then(function() {
        if (!current()) return null;
        var client = telegram.getClient();
        return Promise.resolve(client.getEntity(botUsername.replace(/^@/, ''))).then(function(entity) {
            if (!entity || entity.className !== 'User' || entity.bot !== true) {
                throw new Error('configured agent is not a bot account');
            }
            return client.getMessages(entity, { limit: FETCH_LIMIT });
        });
    }).then(function(messages) {
        if (!current() || messages === null) return;
        messages = messages || [];
        var entries = buildEntries(messages);
        console.log('[history] Sending ' + entries.length + ' entries');
        sendBatch(entries, entries.length ? findThreadId(messages) : null);
    }).catch(function(err) {
        if (!current()) return;
        console.error('[history] Failed to fetch history: ' + String((err && (err.errorMessage || err.message)) || err));
        sendEmpty(STATUS_ERROR);
    });
}

exports.fetchAndSendHistory = fetchAndSendHistory;
exports.buildEntries = buildEntries;
exports.HISTORY_MAX_ENTRY_BYTES = HISTORY_MAX_ENTRY_BYTES;
