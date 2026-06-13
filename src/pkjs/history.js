var messageQueue = require('./lib/message_queue').Queue;
var bundleLoader = require('./lib/bundle_loader');
var telegram = require('./telegram');

var HISTORY_LIMIT = 4;

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
            if (!messages || messages.length === 0) {
                console.log('[history] No messages found');
                sendHistoryDone();
                return;
            }

            var historyEntries = [];
            for (var i = 0; i < messages.length && historyEntries.length < HISTORY_LIMIT; i++) {
                var msg = messages[i];
                if (!msg || !msg.message) continue;

                var text = msg.message;
                var isOwn = msg.out === true;

                if (isOwn) {
                    var promptText = text;
                    var metaIdx = promptText.indexOf('\n\n---METADATA---\n');
                    if (metaIdx !== -1) {
                        promptText = promptText.substring(0, metaIdx);
                    }
                    promptText = promptText.replace(/\n*<system>[\s\S]*?<\/system>\n*/, '');
                    if (promptText.indexOf('[thread:') === 0) {
                        promptText = promptText.substring(promptText.indexOf('] ') + 2);
                    }
                    if (promptText.length > 0) {
                        historyEntries.push({ type: 'prompt', text: promptText });
                    }
                } else {
                    var responseText = text;
                    if (responseText.startsWith('c:')) {
                        responseText = responseText.substring(2);
                    } else if (responseText.startsWith('d:')) {
                        continue;
                    } else if (responseText.startsWith('t:')) {
                        continue;
                    } else if (responseText.startsWith('w:')) {
                        continue;
                    } else if (responseText.startsWith('f:')) {
                        continue;
                    } else if (responseText.startsWith('a:')) {
                        continue;
                    }
                    var widgetIdx = responseText.indexOf('<<!!WIDGET:');
                    if (widgetIdx !== -1) {
                        responseText = responseText.substring(0, widgetIdx).trim();
                    }
                    if (responseText.length > 0) {
                        historyEntries.push({ type: 'response', text: responseText });
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