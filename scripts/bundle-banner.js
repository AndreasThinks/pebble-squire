// Telegram/GramJS bundle for Squire (Pebblegram edition)
// GENERATED FILE - do not edit by hand. Rebuild with `cd scripts && npm run build:telegram`.
// Polyfills for JS runtimes without Web Crypto API, navigator, self, window.location, etc.
// Pebble PKJS lacks: crypto, crypto.subtle, window.addEventListener, Response, self, navigator
//
// This prelude is copied verbatim from scripts/bundle-banner.js and runs before
// any bundled module is evaluated. It must stay ES2015-parseable (the Pebble
// SDK's webpack 1 / acorn 3 parser) and must not reference `require`.
var __squireGlobal =
    typeof globalThis !== 'undefined' ? globalThis :
    typeof window !== 'undefined' ? window :
    typeof global !== 'undefined' ? global :
    typeof self !== 'undefined' ? self :
    Function('return this')();
if (typeof globalThis === 'undefined') {
    __squireGlobal.globalThis = __squireGlobal;
}
if (typeof window === 'undefined') {
    // GramJS decides between its browser (WebSocket) and Node (TCP) transport
    // with `typeof window !== "undefined"`, so a window object must exist.
    __squireGlobal.window = { location: { protocol: 'https:' } };
} else if (!window.location) {
    window.location = { protocol: 'https:' };
}
if (typeof navigator === 'undefined') {
    // GramJS checks navigator.onLine when deciding whether to reconnect.
    __squireGlobal.navigator = { onLine: true, userAgent: 'Squire PKJS' };
}
if (typeof self === 'undefined') {
    // GramJS (tl/api.js, crypto) reads `self.*`; PKJS has no `self`.
    __squireGlobal.self = __squireGlobal;
}
if (typeof __squireGlobal.Buffer === 'undefined' && typeof Buffer !== 'undefined') {
    __squireGlobal.Buffer = Buffer;
}
if (typeof process === 'undefined') {
    // Harmless stub: nothing in Squire reads configuration from process.env.
    __squireGlobal.process = { env: {}, nextTick: function(fn) { setTimeout(fn, 0); } };
}
// js-sha1/js-sha256/js-sha512 (used by the pure-JS crypto shim): always take
// the pure-JS path, never their Node fast path or Buffer.from probing.
var JS_SHA1_NO_BUFFER_FROM = true;
var JS_SHA256_NO_BUFFER_FROM = true;
(function(targets) {
    var flags = [
        'JS_SHA1_NO_BUFFER_FROM', 'JS_SHA256_NO_BUFFER_FROM', 'JS_SHA512_NO_BUFFER_FROM',
        'JS_SHA1_NO_NODE_JS', 'JS_SHA256_NO_NODE_JS', 'JS_SHA512_NO_NODE_JS'
    ];
    for (var t = 0; t < targets.length; t++) {
        if (!targets[t] || typeof targets[t] !== 'object') continue;
        for (var f = 0; f < flags.length; f++) targets[t][flags[f]] = true;
    }
})([__squireGlobal, typeof window !== 'undefined' ? window : null]);
