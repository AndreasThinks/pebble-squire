'use strict';
// PebbleKit JS runtime polyfills ("Pebblegram" polyfills).
//
// Bundled as the first module required by telegram-entry.js, so it runs after
// bundle-banner.js but BEFORE any GramJS module is evaluated (esbuild's
// CommonJS wrappers are lazy). Everything here only fills gaps: existing host
// implementations are never replaced, except WebSocket, which is wrapped once.
//
// History: these started life as hand-applied patches (see patches/001-003 in
// the repo root) to bundle_loader.js and the bundle itself.

var BufferImpl = require('buffer/').Buffer;

var G = typeof globalThis !== 'undefined' ? globalThis : typeof global !== 'undefined' ? global : exports;

// patches/002: GramJS reads `self.crypto` / `self.localStorage`.
if (!G.self) G.self = G;
if (!G.window) G.window = G;
if (!G.window.location) G.window.location = { protocol: 'http:' };

// PromisedWebSockets registers an "offline" listener on window in browsers.
if (!G.window.addEventListener) G.window.addEventListener = function () {};
if (!G.window.removeEventListener) G.window.removeEventListener = function () {};

// patches/001: GramJS checks navigator.onLine before reconnecting.
if (!G.navigator) G.navigator = { onLine: true, userAgent: 'Pebblegram PKJS' };
if (!G.window.navigator) G.window.navigator = G.navigator;

// The Node build of GramJS uses the global `Buffer` everywhere.
if (!G.Buffer) G.Buffer = BufferImpl;
if (!G.window.Buffer) G.window.Buffer = G.Buffer;

// crypto.getRandomValues is used for nonces, session ids and DH secrets.
// Never substitute Math.random(): predictable DH secrets compromise the
// Telegram session. Unsupported hosts must fail before creating an auth key.
if (!G.crypto) G.crypto = {};
if (!G.crypto.getRandomValues) {
    G.crypto.getRandomValues = function () {
        throw new Error('Secure random source unavailable in this phone runtime');
    };
}
if (!G.window.crypto) G.window.crypto = G.crypto;

// Minimal fetch Response (only .arrayBuffer()) for code that wraps binary data.
if (!G.Response) {
    G.Response = function (body) {
        this._body = body;
    };
    G.Response.prototype.arrayBuffer = function () {
        var body = this._body;
        var view;
        var bytes;
        var i;
        if (body == null) return Promise.resolve(new ArrayBuffer(0));
        if (body instanceof ArrayBuffer || String(body) === '[object ArrayBuffer]') return Promise.resolve(body);
        if (typeof ArrayBuffer !== 'undefined' && ArrayBuffer.isView && ArrayBuffer.isView(body)) {
            view = new Uint8Array(body.buffer, body.byteOffset || 0, body.byteLength || body.length || 0);
            return Promise.resolve(view.slice().buffer);
        }
        if (typeof body.length === 'number') {
            bytes = new Uint8Array(body.length);
            for (i = 0; i < body.length; i += 1) bytes[i] = body[i] & 255;
            return Promise.resolve(bytes.buffer);
        }
        return Promise.resolve(new ArrayBuffer(0));
    };
}
if (!G.window.Response) G.window.Response = G.Response;

// patches/003: PKJS can fire WebSocket events (notably `open`) before GramJS
// has assigned its on* handlers. Wrap the native WebSocket so events are queued
// until a handler for that event type exists, then replayed in order.
if (typeof G.WebSocket === 'function' && G.WebSocket.__pebblegramWrapped !== true) {
    (function () {
        var NativeWebSocket = G.WebSocket;
        var EVENT_TYPES = ['open', 'message', 'error', 'close'];

        function PebblegramWebSocket(url, protocols) {
            var socket = protocols ? new NativeWebSocket(url, protocols) : new NativeWebSocket(url);
            var listeners = { open: [], message: [], error: [], close: [] };
            var queue = [];
            var facade = {};

            function hasHandler(type) {
                return typeof facade['on' + type] === 'function' || listeners[type].length > 0;
            }

            function dispatch(type, event) {
                var handler = facade['on' + type];
                var k;
                if (typeof handler === 'function') handler.call(facade, event || { type: type });
                for (k = 0; k < listeners[type].length; k += 1) {
                    listeners[type][k].call(facade, event || { type: type });
                }
            }

            function flush(type) {
                var remaining = [];
                var d;
                var item;
                for (d = 0; d < queue.length; d += 1) {
                    item = queue[d];
                    if ((!type || item.kind === type) && hasHandler(item.kind)) {
                        dispatch(item.kind, item.event);
                    } else {
                        remaining.push(item);
                    }
                }
                queue = remaining;
            }

            function receive(type, event) {
                if (!hasHandler(type)) {
                    queue.push({ kind: type, event: event });
                    return;
                }
                dispatch(type, event);
            }

            socket.onopen = function (e) {
                receive('open', e);
            };
            socket.onmessage = function (e) {
                receive('message', e);
            };
            socket.onerror = function (e) {
                receive('error', e);
            };
            socket.onclose = function (e) {
                receive('close', e);
            };

            facade.send = function (data) {
                return socket.send(data);
            };
            facade.close = function (code, reason) {
                return socket.close(code, reason);
            };
            facade.addEventListener = function (type, fn) {
                if (listeners[type] && typeof fn === 'function') {
                    listeners[type].push(fn);
                    flush(type);
                }
            };
            facade.removeEventListener = function (type, fn) {
                var d;
                if (!listeners[type] || typeof fn !== 'function') return;
                for (d = listeners[type].length - 1; d >= 0; d -= 1) {
                    if (listeners[type][d] === fn) listeners[type].splice(d, 1);
                }
            };
            Object.defineProperty(facade, 'readyState', {
                get: function () {
                    return socket.readyState;
                },
            });
            Object.defineProperty(facade, 'bufferedAmount', {
                get: function () {
                    return socket.bufferedAmount;
                },
            });
            Object.defineProperty(facade, 'binaryType', {
                get: function () {
                    return socket.binaryType;
                },
                set: function (value) {
                    socket.binaryType = value;
                },
            });
            EVENT_TYPES.forEach(function (type) {
                Object.defineProperty(facade, 'on' + type, {
                    get: function () {
                        return facade['__on' + type] || null;
                    },
                    set: function (fn) {
                        facade['__on' + type] = fn;
                        flush(type);
                    },
                });
            });
            return facade;
        }

        ['CONNECTING', 'OPEN', 'CLOSING', 'CLOSED'].forEach(function (name) {
            if (NativeWebSocket[name] !== undefined) PebblegramWebSocket[name] = NativeWebSocket[name];
        });
        PebblegramWebSocket.__pebblegramWrapped = true;
        G.WebSocket = PebblegramWebSocket;
        G.window.WebSocket = PebblegramWebSocket;
    })();
}
