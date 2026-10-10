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

// Delivers app messages to the watch strictly in order, one at a time.
//
// Only one message is ever in flight. A failed send is retried (with
// backoff) while it stays at the head of the queue, so nothing behind it can
// overtake it: CHAT_DONE can never arrive before the CHAT it follows. After
// MAX_SEND_ATTEMPTS the message is given up on; if it belonged to a group
// (e.g. one agent reply), the rest of that group is discarded and the
// group's onFailed callback runs, so a reply is never shown with a hole in
// it and presented as complete.
//
// A timed-out send is ambiguous: the watch may have applied it and only the
// ack was lost. Every message therefore carries a SEQ number that stays the
// same when it is resent, and the watch ignores a message repeating the SEQ
// it applied last (src/c/util/transport.c). Because only the head is ever
// resent, "the last one" is the only possible duplicate.
//
// The queue has a hard budget. A message that doesn't fit is refused whole
// (enqueue returns false); for a group that fails the group, whose terminal
// report may use a small reserve so the watch still hears why.

// The watch requests a 5000-byte inbox (SQUIRE_APP_MESSAGE_BUFFER_SIZE in
// src/c/converse/conversation_manager.c). A dictionary costs 1 byte, plus 7
// bytes of header per tuple, plus the value (strings include their NUL).
var WATCH_INBOX_BYTES = 5000;
var DICT_HEADER_BYTES = 1;
var TUPLE_HEADER_BYTES = 7;
// Headroom for differences between phone apps' encodings.
var SAFETY_MARGIN_BYTES = 64;
var MAX_MESSAGE_BYTES = WATCH_INBOX_BYTES - SAFETY_MARGIN_BYTES;

// Keep individual CHAT chunks modest: every byte of a chunk must also fit in
// the watch's heap alongside the reply it's appended to.
var MAX_CHAT_CHUNK_BYTES = 4000;

var MAX_QUEUE_ITEMS = 48;
// Room for a full 4096-character Telegram message in 4-byte UTF-8, twice over.
var MAX_QUEUE_BYTES = 32768;
var TERMINAL_RESERVE_ITEMS = 8;
var TERMINAL_RESERVE_BYTES = 2048;

// SEQ is an int32 tuple on every message.
var SEQ_BYTES = TUPLE_HEADER_BYTES + 4;
var MAX_SEQ = 0x7fffffff;

var MAX_SEND_ATTEMPTS = 4;
var RETRY_BASE_DELAY_MS = 250;
// If the phone app never calls either callback, treat the send as failed.
var SEND_TIMEOUT_MS = 15000;

// Keys whose string payloads the watch reassembles across multiple app
// messages (conversation_add_response_fragment appends CHAT fragments).
var CHUNKABLE_KEYS = ['CHAT'];
// Keys copied onto every chunk of a split message, so each chunk is still
// correlated with its request.
var STICKY_KEYS = ['REQUEST_ID'];

// App messages carry strings as UTF-8. Lone surrogates can't be encoded and
// NUL would truncate the C string, so normalise both before measuring.
function sanitizeString(str) {
    var out = '';
    for (var i = 0; i < str.length; i++) {
        var code = str.charCodeAt(i);
        if (code === 0) {
            continue;
        }
        if (code >= 0xD800 && code <= 0xDBFF) {
            var next = i + 1 < str.length ? str.charCodeAt(i + 1) : 0;
            if (next >= 0xDC00 && next <= 0xDFFF) {
                out += str.charAt(i) + str.charAt(i + 1);
                i++;
            } else {
                out += '�';
            }
        } else if (code >= 0xDC00 && code <= 0xDFFF) {
            out += '�';
        } else {
            out += str.charAt(i);
        }
    }
    return out;
}

// UTF-8 length of a sanitized string.
function utf8Length(str) {
    var bytes = 0;
    for (var i = 0; i < str.length; i++) {
        var code = str.charCodeAt(i);
        if (code < 0x80) {
            bytes += 1;
        } else if (code < 0x800) {
            bytes += 2;
        } else if (code >= 0xD800 && code <= 0xDBFF) {
            // Surrogate pair (guaranteed by sanitizeString): 4 bytes.
            bytes += 4;
            i++;
        } else {
            bytes += 3;
        }
    }
    return bytes;
}

// Split a sanitized string into pieces of at most maxBytes UTF-8 bytes each,
// never splitting a surrogate pair.
function chunkByUtf8Bytes(str, maxBytes) {
    var chunks = [];
    var start = 0;
    var bytes = 0;
    var i = 0;
    while (i < str.length) {
        var code = str.charCodeAt(i);
        var charUnits = 1;
        var charBytes;
        if (code < 0x80) {
            charBytes = 1;
        } else if (code < 0x800) {
            charBytes = 2;
        } else if (code >= 0xD800 && code <= 0xDBFF) {
            charBytes = 4;
            charUnits = 2;
        } else {
            charBytes = 3;
        }
        if (bytes + charBytes > maxBytes && i > start) {
            chunks.push(str.substring(start, i));
            start = i;
            bytes = 0;
        }
        bytes += charBytes;
        i += charUnits;
    }
    if (start < str.length) {
        chunks.push(str.substring(start));
    }
    return chunks;
}

// Truncate a sanitized string to at most maxBytes UTF-8 bytes on a code
// point boundary, appending an ellipsis if anything was cut.
function truncateUtf8(str, maxBytes) {
    if (utf8Length(str) <= maxBytes) {
        return str;
    }
    var ellipsis = '…';
    return chunkByUtf8Bytes(str, maxBytes - utf8Length(ellipsis))[0] + ellipsis;
}

function valueBytes(value) {
    if (typeof value === 'string') {
        return utf8Length(value) + 1;
    }
    if (typeof value === 'number' || typeof value === 'boolean') {
        return 4;
    }
    if (Array.isArray(value) || (typeof Uint8Array !== 'undefined' && value instanceof Uint8Array)) {
        return value.length;
    }
    return 0;
}

// Size of the dictionary the watch will receive for message.
function messageBytes(message) {
    var bytes = DICT_HEADER_BYTES;
    for (var key in message) {
        if (Object.prototype.hasOwnProperty.call(message, key)) {
            bytes += TUPLE_HEADER_BYTES + valueBytes(message[key]);
        }
    }
    return bytes;
}

function sanitizeMessage(message) {
    var clean = {};
    for (var key in message) {
        if (Object.prototype.hasOwnProperty.call(message, key)) {
            var value = message[key];
            clean[key] = typeof value === 'string' ? sanitizeString(value) : value;
        }
    }
    return clean;
}

// Splits a message whose chunkable string is too big into several messages
// that each fit. Non-sticky keys other than the chunked one ride on the last
// chunk, so e.g. a CHAT_DONE combined with a CHAT still comes after all of
// the text.
function splitMessage(message) {
    for (var i = 0; i < CHUNKABLE_KEYS.length; i++) {
        var key = CHUNKABLE_KEYS[i];
        if (typeof message[key] !== 'string') {
            continue;
        }
        var text = message[key];
        var others = {};
        var sticky = {};
        for (var k in message) {
            if (!Object.prototype.hasOwnProperty.call(message, k) || k === key) {
                continue;
            }
            if (STICKY_KEYS.indexOf(k) !== -1) {
                sticky[k] = message[k];
            } else {
                others[k] = message[k];
            }
        }
        var overhead = messageBytes(merge(sticky, others)) + TUPLE_HEADER_BYTES + 1 + SEQ_BYTES;
        var budget = Math.min(MAX_CHAT_CHUNK_BYTES, MAX_MESSAGE_BYTES - overhead);
        if (utf8Length(text) <= budget) {
            return [message];
        }
        var chunks = chunkByUtf8Bytes(text, budget);
        var result = [];
        for (var c = 0; c < chunks.length; c++) {
            var part = merge(sticky, c === chunks.length - 1 ? others : {});
            part[key] = chunks[c];
            result.push(part);
        }
        return result;
    }
    return [message];
}

function merge(a, b) {
    var out = {};
    var k;
    for (k in a) { if (Object.prototype.hasOwnProperty.call(a, k)) out[k] = a[k]; }
    for (k in b) { if (Object.prototype.hasOwnProperty.call(b, k)) out[k] = b[k]; }
    return out;
}

function describe(message) {
    // Key names and sizes only: message contents are user data.
    return Object.keys(message).join(',') + ' (' + messageBytes(message) + ' bytes)';
}

function MessageQueue() {
    // A fresh random start after a PebbleKit JS restart, so a new message is
    // never mistaken for a resend of the last one before the restart.
    this.nextSeq = 1 + Math.floor(Math.random() * (MAX_SEQ / 2));
    this.reset();
}

MessageQueue.prototype.takeSeq = function() {
    var seq = this.nextSeq;
    this.nextSeq = seq >= MAX_SEQ ? 1 : seq + 1;
    return seq;
};

MessageQueue.prototype.reset = function() {
    if (this.retryTimer) clearTimeout(this.retryTimer);
    if (this.sendTimer) clearTimeout(this.sendTimer);
    this.queue = [];
    this.inFlight = null;
    this.retryTimer = null;
    this.sendTimer = null;
    this.attemptToken = 0;
    this.failedGroups = {};
};

// options.group: messages sharing a group are one logical unit (e.g. one
//   agent reply). If any of them can't be delivered, the rest are dropped
//   and options.onFailed(group) is called once.
// options.terminal: the message reports a failure itself; if it fails too,
//   nothing more is attempted for the group.
MessageQueue.prototype.enqueue = function(message, options) {
    options = options || {};
    var group = options.group !== undefined ? options.group : null;
    var terminal = !!options.terminal;
    if (group !== null && this.failedGroups[group] && !terminal) {
        return false;
    }
    var parts = splitMessage(sanitizeMessage(message));
    var items = [];
    var addedBytes = 0;
    for (var i = 0; i < parts.length; i++) {
        parts[i].SEQ = this.takeSeq();
        var item = {
            message: parts[i],
            bytes: messageBytes(parts[i]),
            group: group,
            terminal: terminal,
            onFailed: options.onFailed || null,
            attempts: 0
        };
        if (item.bytes > MAX_MESSAGE_BYTES) {
            // Can't be split and would just be dropped by the watch.
            console.error('[queue] message too large for the watch: ' + describe(item.message));
            this.failItem(item, 'too-large');
            return false;
        }
        items.push(item);
        addedBytes += item.bytes;
    }
    var maxItems = MAX_QUEUE_ITEMS + (terminal ? TERMINAL_RESERVE_ITEMS : 0);
    var maxBytes = MAX_QUEUE_BYTES + (terminal ? TERMINAL_RESERVE_BYTES : 0);
    if (this.queue.length + items.length > maxItems || this.bytes() + addedBytes > maxBytes) {
        console.error('[queue] queue full (' + this.queue.length + ' messages, ' + this.bytes() +
                      ' bytes); refusing ' + describe(message));
        if (items.length) {
            this.failItem(items[0], 'queue-full');
        }
        return false;
    }
    for (var j = 0; j < items.length; j++) {
        this.queue.push(items[j]);
    }
    this.pump();
    return true;
};

// Drops queued (not yet in-flight) messages of a group, e.g. when a request
// is cancelled.
MessageQueue.prototype.purgeGroup = function(group) {
    this.queue = this.queue.filter(function(item) {
        return item.group !== group || item === this.inFlight;
    }, this);
};

// Includes the in-flight message, which stays at the head until it settles.
MessageQueue.prototype.size = function() {
    return this.queue.length;
};

MessageQueue.prototype.bytes = function() {
    var total = 0;
    for (var i = 0; i < this.queue.length; i++) {
        total += this.queue[i].bytes;
    }
    return total;
};

MessageQueue.prototype.pump = function() {
    if (this.inFlight || this.retryTimer || this.queue.length === 0) {
        return;
    }
    var item = this.queue[0];
    this.inFlight = item;
    item.attempts++;
    var token = ++this.attemptToken;
    var self = this;
    var settled = false;
    function settle(ok) {
        if (settled || token !== self.attemptToken) {
            return;
        }
        settled = true;
        clearTimeout(self.sendTimer);
        self.sendTimer = null;
        self.inFlight = null;
        if (ok) {
            self.queue.shift();
            self.pump();
        } else {
            self.handleFailure(item);
        }
    }
    this.sendTimer = setTimeout(function() {
        console.warn('[queue] send timed out: ' + describe(item.message));
        settle(false);
    }, SEND_TIMEOUT_MS);
    try {
        Pebble.sendAppMessage(item.message, function() { settle(true); }, function() { settle(false); });
    } catch (err) {
        console.error('[queue] sendAppMessage threw: ' + (err && err.message));
        settle(false);
    }
};

MessageQueue.prototype.handleFailure = function(item) {
    var self = this;
    if (item.attempts < MAX_SEND_ATTEMPTS) {
        var delay = RETRY_BASE_DELAY_MS * Math.pow(2, item.attempts - 1);
        console.log('[queue] send failed, retrying in ' + delay + 'ms (attempt ' + item.attempts + '/' + MAX_SEND_ATTEMPTS + ')');
        // The item stays at the head of the queue: nothing overtakes it.
        this.retryTimer = setTimeout(function() {
            self.retryTimer = null;
            self.pump();
        }, delay);
        return;
    }
    console.error('[queue] giving up after ' + item.attempts + ' attempts: ' + describe(item.message));
    this.queue.shift();
    this.failItem(item, 'undelivered');
    this.pump();
};

// reason: 'undelivered' (the watch never acknowledged it), 'queue-full' or
// 'too-large'.
MessageQueue.prototype.failItem = function(item, reason) {
    if (item.group === null || item.terminal) {
        return;
    }
    if (this.failedGroups[item.group]) {
        return;
    }
    this.failedGroups[item.group] = true;
    this.purgeGroup(item.group);
    if (item.onFailed) {
        try {
            item.onFailed(item.group, reason);
        } catch (err) {
            console.error('[queue] onFailed threw: ' + (err && err.message));
        }
    }
};

exports.Queue = new MessageQueue();
exports.MessageQueue = MessageQueue;
exports.utf8Length = function(str) { return utf8Length(sanitizeString(str)); };
exports.truncateUtf8 = function(str, maxBytes) { return truncateUtf8(sanitizeString(str), maxBytes); };
exports.messageBytes = messageBytes;
exports.sanitizeString = sanitizeString;
exports.MAX_MESSAGE_BYTES = MAX_MESSAGE_BYTES;
exports.MAX_SEND_ATTEMPTS = MAX_SEND_ATTEMPTS;
exports.MAX_QUEUE_ITEMS = MAX_QUEUE_ITEMS;
exports.MAX_QUEUE_BYTES = MAX_QUEUE_BYTES;
