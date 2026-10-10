'use strict';
// Entry point for the Squire Telegram bundle (built by build-gramjs.js).
//
// CommonJS on purpose: esbuild wraps every CommonJS module lazily, so the
// polyfills below are guaranteed to run before any GramJS module is evaluated.
//
// Export surface consumed by src/pkjs/lib/bundle_loader.js:
//   TelegramClient, StringSession, Api,
//   TelegramClient.events.{Raw, NewMessage, NewMessageEvent, EditedMessage, EditedMessageEvent}

require('./pkjs-polyfills');

var telegram = require('telegram');
var sessions = require('telegram/sessions');
var events = require('telegram/events');
var editedMessage = require('telegram/events/EditedMessage');

// GramJS ships an EditedMessage event builder (telegram/events/EditedMessage)
// but does not re-export it from telegram/events. `TelegramClient.events` is a
// getter returning that same module object, so add the builder there.
function addEventExport(name, value) {
    if (typeof value !== 'function') {
        throw new Error('telegram-entry: expected ' + name + ' to be a function');
    }
    if (!Object.prototype.hasOwnProperty.call(events, name)) {
        Object.defineProperty(events, name, { enumerable: true, value: value });
    }
}
addEventExport('EditedMessage', editedMessage.EditedMessage);
addEventExport('EditedMessageEvent', editedMessage.EditedMessageEvent);

module.exports = {
    Api: telegram.Api,
    StringSession: sessions.StringSession,
    TelegramClient: telegram.TelegramClient,
};
