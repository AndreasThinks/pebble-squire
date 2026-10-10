'use strict';
// Replacement for the `websocket` npm package (used by GramJS's
// PromisedWebSockets via `require("websocket").w3cwebsocket`).
//
// The package's own browser build captures `globalThis.WebSocket` once, at
// module-evaluation time, and pulls in es5-ext. This version resolves the
// native WebSocket lazily on every construction so that the buffering wrapper
// installed by pkjs-polyfills.js (or one installed later by the app) is always
// honoured, and it has no dependencies.
function resolveGlobal() {
    if (typeof globalThis !== 'undefined') return globalThis;
    if (typeof window !== 'undefined') return window;
    if (typeof global !== 'undefined') return global;
    return {};
}

function W3CWebSocket(uri, protocols) {
    var g = resolveGlobal();
    var NativeWebSocket = g.WebSocket || g.MozWebSocket;
    if (!NativeWebSocket) {
        throw new Error('WebSocket is not available in PebbleKit JS.');
    }
    return protocols ? new NativeWebSocket(uri, protocols) : new NativeWebSocket(uri);
}

W3CWebSocket.CONNECTING = 0;
W3CWebSocket.OPEN = 1;
W3CWebSocket.CLOSING = 2;
W3CWebSocket.CLOSED = 3;

module.exports = {
    w3cwebsocket: W3CWebSocket,
    version: 'pebblekit',
};
