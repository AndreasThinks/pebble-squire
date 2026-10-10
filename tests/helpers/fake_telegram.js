// A fake GramJS client for offline session tests. It records event handlers
// and lets tests deliver updates exactly as GramJS would.
const BOT_ID = '777';

function NewMessage(options) { this.options = options || {}; }
function Raw(options) { this.options = options || {}; }

function peerUser(id) { return { className: 'PeerUser', userId: { toString: () => String(id) } }; }
function peerChat(id) { return { className: 'PeerChat', chatId: { toString: () => String(id) } }; }

function botMessage(id, text, extra) {
    return Object.assign({
        id,
        message: text,
        out: false,
        peerId: peerUser(BOT_ID),
        senderId: { toString: () => BOT_ID },
    }, extra || {});
}

function makeFakeClient(options) {
    options = options || {};
    const calls = [];
    const handlers = [];
    let resolveSend;
    const client = {
        connected: true,
        calls,
        handlers,
        sentMessages: [],
        addEventHandler(fn, builder) {
            calls.push('addEventHandler');
            handlers.push({ fn, builder });
        },
        removeEventHandler(fn, builder) {
            calls.push('removeEventHandler');
            const i = handlers.findIndex(h => h.fn === fn && h.builder === builder);
            if (i !== -1) handlers.splice(i, 1);
        },
        getEntity(username) {
            calls.push('getEntity');
            if (options.entityError) return Promise.reject(options.entityError);
            if (options.entity !== undefined) return Promise.resolve(options.entity);
            return Promise.resolve({ className: 'User', id: { toString: () => BOT_ID }, bot: true, username });
        },
        sendMessage(entity, params) {
            calls.push('sendMessage');
            client.sentMessages.push({ entity, params });
            if (options.deferSend) {
                return new Promise(r => { resolveSend = r; });
            }
            return Promise.resolve({ id: options.sentId || 100 });
        },
        resolveSend(id) { resolveSend({ id: id || options.sentId || 100 }); },
        emitNewMessage(message) {
            for (const h of handlers.slice()) {
                if (h.builder instanceof NewMessage) h.fn({ message });
            }
        },
        emitRaw(update) {
            for (const h of handlers.slice()) {
                if (h.builder instanceof Raw) h.fn(update);
            }
        },
        typing() {
            client.emitRaw({ className: 'UpdateUserTyping', userId: { toString: () => BOT_ID }, action: { className: 'SendMessageTypingAction' } });
        },
        edit(message) {
            client.emitRaw({ className: 'UpdateEditMessage', message });
        },
    };
    return client;
}

module.exports = { BOT_ID, NewMessage, Raw, makeFakeClient, botMessage, peerUser, peerChat };
