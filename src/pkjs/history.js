var messageQueue = require('./lib/message_queue').Queue;
var bundleLoader = require('./lib/bundle_loader');
var telegram = require('./telegram');

var HISTORY_LIMIT = 4;

function formatLoggedMessage(message) {
    if (!message) return '';
    var withoutSystem = message.replace(/<system>[\s\S]*?<\/system>/g, '').trim();
    if (withoutSystem.length <= 100) return withoutSystem;
    return withoutSystem.substring(0, 50) + '...' + withoutSystem.substring(withoutSystem.length - 50);
}

function fetchAndSendHistory() {
    if (!telegram.hasSession()) {
        console.log('[history] No Telegram session, skipping history fetch');
        sendHistoryDone();
        return;
    }

    bundleLoader.ensureTelegramBundle();

    if (typeof TelegramClient === 'undefined') {
        console.log('[history] TelegramClient not available after bundle load');
        sendHistoryDone();
        return;
    }

    telegram.initClient().then(function() {
        var client = telegram.getClient();
        var botUsername = telegram.getBotUsername();
        if (!botUsername) {
            console.log('[history] No bot username configured');
            sendHistoryDone();
            return;
        }
        var cleanUsername = botUsername.replace(/^@/, '');

        client.getMessages(cleanUsername, { limit: 20 }).then(function(messages) {
            console.log('[history] total messages returned: ' + (messages ? messages.length : 0));
            if (!messages || messages.length === 0) {
                console.log('[history] No messages found');
                sendHistoryDone();
                return;
            }

            for (var d = 0; d < Math.min(messages.length, 3); d++) {
                var dm = messages[d];
                if (dm.out === false || dm.out === 0) {
                    console.log('[history] bot msg _text=' + JSON.stringify(dm._text) + ' originalArgs=' + JSON.stringify(dm.originalArgs));
                }
            }

            var historyEntries = [];
            for (var i = 0; i < messages.length && historyEntries.length < HISTORY_LIMIT; i++) {
                var msg = messages[i];
                if (!msg || !(msg.message && msg.message.length > 0)) {
                    if (msg && msg.out !== true && msg.out !== 1) {
                        console.log('[history] bot empty msg _text=' + JSON.stringify(msg._text) + ' media=' + (msg.media ? msg.media.className : 'none') + ' action=' + (msg.action ? msg.action.className : 'none'));
                    }
                    continue;
                }

                var text = msg.message;
                var isOwn = msg.out === true || msg.out === 1;
                console.log('[history] raw message out=', msg.out, 'text=', formatLoggedMessage(text));

                if (isOwn) {
                    var promptText = text;
                    if (promptText.indexOf('[thread:') === 0) {
                        promptText = promptText.substring(promptText.indexOf('] ') + 2);
                    }
                    promptText = promptText.replace(/\s*<system>[\s\S]*?<\/system>\s*/, '');
                    promptText = promptText.trim();
                    if (promptText.length > 0) {
                        historyEntries.push({ type: 'prompt', text: promptText });
                    }
                } else {
                    if (text.length > 0) {
                        historyEntries.push({ type: 'response', text: text });
                    }
                }
            }

            if (historyEntries.length === 0) {
                console.log('[history] No usable history entries');
                sendHistoryDone();
                return;
            }

            var threadId = null;
            for (var j = 0; j < messages.length; j++) {
                var m = messages[j];
                if (m && m.message && m.message.indexOf('[thread:') === 0) {
                    var end = m.message.indexOf(']');
                    if (end !== -1) {
                        threadId = m.message.substring('[thread:'.length, end);
                        break;
                    }
                }
            }

            if (threadId) {
                messageQueue.enqueue({ HISTORY_THREAD_ID: threadId });
            }

            for (var k = historyEntries.length - 1; k >= 0; k--) {
                var entry = historyEntries[k];
                if (entry.type === 'prompt') {
                    messageQueue.enqueue({ HISTORY_PROMPT: entry.text });
                } else {
                    messageQueue.enqueue({ HISTORY_RESPONSE: entry.text });
                }
            }

            sendHistoryDone();
        }).catch(function(err) {
            console.error('[history] Failed to fetch messages: ' + (err.message || err));
            sendHistoryDone();
        });
    }).catch(function(err) {
        console.error('[history] Failed to init client: ' + (err.message || err));
        sendHistoryDone();
    });
}

function sendHistoryDone() {
    messageQueue.enqueue({ HISTORY_DONE: 1 });
}

exports.fetchAndSendHistory = fetchAndSendHistory;