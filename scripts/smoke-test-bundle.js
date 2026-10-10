#!/usr/bin/env node
'use strict';
/**
 * Offline smoke test for src/pkjs/lib/telegram-bundle.js.
 *
 *   node smoke-test-bundle.js [path/to/telegram-bundle.js]
 *
 * 1. Parses the bundle with the Pebble SDK's parser (acorn 3.3.0, ES2015).
 * 2. Statically checks that no `require(...)` call survived bundling.
 * 3. Loads the bundle in fresh `vm` contexts that mimic PebbleKit JS (no Node
 *    built-ins, no `require`, no `self`/`navigator`/`Buffer`/`process`, no
 *    `crypto.subtle`), asserts the export surface Squire relies on, builds
 *    Api objects / event builders offline, and constructs a TelegramClient
 *    with an empty StringSession WITHOUT connecting. Any WebSocket, XHR or
 *    fetch use, or any `require` call, fails the test. No network access.
 */

const fs = require('fs');
const path = require('path');
const vm = require('vm');
const nodeCrypto = require('crypto');

const { DEFAULT_BUNDLE, REPO_ROOT, checkPebbleWebpackParse, sha256 } = require('./bundle-checks');

const bundlePath = path.resolve(process.argv[2] || DEFAULT_BUNDLE);
let failures = 0;

function ok(condition, message) {
    if (condition) {
        console.log('  ok   ' + message);
    } else {
        failures++;
        console.error('  FAIL ' + message);
    }
}

function section(title) {
    console.log('\n# ' + title);
}

function staticChecks(code) {
    section('static checks');
    const sourceType = checkPebbleWebpackParse(code);
    ok(true, 'parses with acorn 3.3.0 ecmaVersion 6 (sourceType ' + sourceType + ') like Pebble SDK webpack 1');
    // Strip string literals/comments roughly is overkill; esbuild emits no
    // `require(` at all for a fully bundled CJS output.
    const requireCalls = code.match(/(^|[^A-Za-z0-9_$.])require\s*\(/g) || [];
    ok(requireCalls.length === 0, 'no require(...) calls left in bundle (found ' + requireCalls.length + ')');
    ok(!/__require\b/.test(code), 'no esbuild __require shim (dynamic require) in bundle');
    const amd = code.match(/(^|[^A-Za-z0-9_$.])define(\s*\(|\.amd)/g) || [];
    ok(amd.length === 0, 'no free AMD define()/define.amd use (webpack 1 would rewrite it) (found ' + amd.length + ')');
    ok(/^\/\/ squire-telegram-bundle telegram@\S+ gramjs=\S+ esbuild@\S+ target=es2015 inputs-sha256=[0-9a-f]{64}\n/.test(code),
        'starts with squire-telegram-bundle provenance marker');
}

/**
 * Build a PKJS-like global object. Only things PebbleKit JS actually has are
 * provided; everything Node-specific is deliberately absent.
 */
function createPkjsContext(options) {
    const events = { websockets: [], xhr: 0, fetch: 0, requires: [], defines: 0, logs: [] };
    const timers = new Set();

    function FakeWebSocket(url, protocols) {
        events.websockets.push({ url: url, protocols: protocols });
        throw new Error('smoke test: network access attempted (WebSocket ' + url + ')');
    }
    FakeWebSocket.CONNECTING = 0;
    FakeWebSocket.OPEN = 1;
    FakeWebSocket.CLOSING = 2;
    FakeWebSocket.CLOSED = 3;

    function FakeXMLHttpRequest() {
        events.xhr++;
        throw new Error('smoke test: network access attempted (XMLHttpRequest)');
    }

    const store = {};
    const sandbox = {
        console: {
            log: function () { events.logs.push(['log'].concat(Array.from(arguments))); },
            info: function () { events.logs.push(['info'].concat(Array.from(arguments))); },
            warn: function () { events.logs.push(['warn'].concat(Array.from(arguments))); },
            error: function () { events.logs.push(['error'].concat(Array.from(arguments))); },
            debug: function () {},
        },
        setTimeout: function (fn, ms) {
            const args = Array.prototype.slice.call(arguments, 2);
            const handle = setTimeout(function () {
                timers.delete(handle);
                fn.apply(null, args);
            }, ms);
            timers.add(handle);
            return handle;
        },
        clearTimeout: function (handle) {
            timers.delete(handle);
            clearTimeout(handle);
        },
        setInterval: function (fn, ms) {
            const handle = setInterval(fn, ms);
            timers.add(handle);
            return handle;
        },
        clearInterval: function (handle) {
            timers.delete(handle);
            clearInterval(handle);
        },
        localStorage: {
            getItem: function (k) { return Object.prototype.hasOwnProperty.call(store, k) ? store[k] : null; },
            setItem: function (k, v) { store[k] = String(v); },
            removeItem: function (k) { delete store[k]; },
            clear: function () { Object.keys(store).forEach(function (k) { delete store[k]; }); },
        },
        Pebble: {
            addEventListener: function () {},
            sendAppMessage: function () {},
            getAccountToken: function () { return 'smoke'; },
            platform: 'smoke',
        },
        WebSocket: FakeWebSocket,
        XMLHttpRequest: FakeXMLHttpRequest,
        fetch: function () {
            events.fetch++;
            return Promise.reject(new Error('smoke test: network access attempted (fetch)'));
        },
    };
    if (options.cryptoGetRandomValues) {
        // Like PKJS hosts that expose getRandomValues but no crypto.subtle.
        sandbox.crypto = {
            getRandomValues: function (array) {
                nodeCrypto.randomFillSync(array);
                return array;
            },
        };
    }
    if (options.nodeLike) {
        // Roughly what vitest (tests/setup.js) provides.
        sandbox.Buffer = Buffer;
        sandbox.process = { env: {}, versions: { node: process.versions.node }, nextTick: process.nextTick };
    }
    const context = vm.createContext(sandbox, { name: 'pkjs-smoke-' + options.name });
    if (options.windowIsGlobal) {
        vm.runInContext('this.window = this;', context);
    }
    if (options.nodeLike) {
        vm.runInContext('this.global = this; this.self = this;', context);
    }
    return { context, events, timers };
}

function loadBundle(code, env) {
    const wrapper = new vm.Script('(function (module, exports, require, define) {' + code + '\n})', {
        filename: 'telegram-bundle.js',
    });
    const fn = wrapper.runInContext(env.context);
    const module = { exports: {} };
    const trapRequire = function (name) {
        env.events.requires.push(name);
        throw new Error('smoke test: bundle called require(' + JSON.stringify(name) + ')');
    };
    // Like webpack 1's AMD support: define.amd is truthy and define(factory)
    // replaces module.exports.
    const trapDefine = function (factory) {
        env.events.defines++;
        const result = typeof factory === 'function' ? factory() : factory;
        if (result !== undefined) module.exports = result;
    };
    trapDefine.amd = {};
    fn.call(module.exports, module, module.exports, trapRequire, trapDefine);
    return module.exports;
}

const REQUIRED_API = [
    'auth.SendCode',
    'auth.SignIn',
    'auth.LogOut',
    'CodeSettings',
    'contacts.ResolveUsername',
    'UpdateUserTyping',
    'UpdateNewMessage',
    'UpdateShortMessage',
    'UpdateEditMessage',
    'SendMessageTypingAction',
    'PeerUser',
];

const REQUIRED_CLIENT_METHODS = [
    'connect',
    'disconnect',
    'destroy',
    'invoke',
    'addEventHandler',
    'removeEventHandler',
    'getEntity',
    'getInputEntity',
    'getMe',
    'getMessages',
    'sendMessage',
    'signInWithPassword',
    'isUserAuthorized',
];

const REQUIRED_EVENTS = ['Raw', 'NewMessage', 'NewMessageEvent', 'EditedMessage', 'EditedMessageEvent'];

function lookup(root, dotted) {
    return dotted.split('.').reduce(function (obj, key) {
        return obj == null ? undefined : obj[key];
    }, root);
}

async function runScenario(code, options) {
    section('runtime scenario: ' + options.name);
    const env = createPkjsContext(options);
    let bundled;
    try {
        bundled = loadBundle(code, env);
        ok(true, 'bundle evaluates in a fresh vm context');
    } catch (err) {
        ok(false, 'bundle evaluates in a fresh vm context: ' + (err && err.stack));
        return;
    }

    const exportNames = Object.keys(bundled).sort();
    ok(JSON.stringify(exportNames) === JSON.stringify(['Api', 'StringSession', 'TelegramClient']),
        'module.exports keys are Api, StringSession, TelegramClient (got ' + exportNames.join(', ') + ')');
    const { TelegramClient, StringSession, Api } = bundled;
    ok(typeof TelegramClient === 'function', 'TelegramClient is a constructor');
    ok(typeof StringSession === 'function', 'StringSession is a constructor');
    ok(Api && typeof Api === 'object', 'Api is an object');

    const evts = TelegramClient && TelegramClient.events;
    for (const name of REQUIRED_EVENTS) {
        ok(evts && typeof evts[name] === 'function', 'TelegramClient.events.' + name + ' is a function');
    }
    for (const name of REQUIRED_API) {
        ok(typeof lookup(Api, name) === 'function', 'Api.' + name + ' is a constructor');
    }
    for (const name of REQUIRED_CLIENT_METHODS) {
        ok(TelegramClient && typeof TelegramClient.prototype[name] === 'function', 'TelegramClient#' + name + '() exists');
    }

    // PKJS globals the bundle is expected to have installed.
    const g = vm.runInContext('this', env.context);
    ok(typeof g.self === 'object' && g.self !== null, 'self is defined after load');
    ok(g.navigator && g.navigator.onLine === true, 'navigator.onLine polyfilled');
    ok(typeof g.Buffer === 'function', 'global Buffer available after load');
    ok(g.window && g.window.location && typeof g.window.location.protocol === 'string', 'window.location present');
    ok(g.crypto && typeof g.crypto.getRandomValues === 'function', 'crypto.getRandomValues present');
    try {
        vm.runInContext('crypto.getRandomValues(new Uint8Array(16))', env.context);
        ok(!!options.cryptoGetRandomValues, 'randomness comes from the host runtime');
    } catch (err) {
        ok(!options.cryptoGetRandomValues && /Secure random source unavailable/.test(err.message),
            'runtime without secure randomness refuses to generate auth secrets');
    }
    ok(typeof g.WebSocket === 'function' && g.WebSocket.__pebblegramWrapped === true, 'WebSocket wrapped with event buffering');

    try {
        // Offline TL serialisation of the requests Squire sends.
        const sendCode = new Api.auth.SendCode({
            phoneNumber: '+15550000000',
            apiId: 12345,
            apiHash: '0123456789abcdef0123456789abcdef',
            settings: new Api.CodeSettings({}),
        });
        const bytes = sendCode.getBytes();
        ok(bytes && bytes.length > 0, 'Api.auth.SendCode serialises offline (' + bytes.length + ' bytes)');
        const resolve = new Api.contacts.ResolveUsername({ username: 'squire_test_bot' });
        ok(resolve.getBytes().length > 0, 'Api.contacts.ResolveUsername serialises offline');

        // Event builders, fed synthetic updates (no client, no network).
        const peer = new Api.PeerUser({ userId: 42 });
        const message = new Api.Message({ id: 7, peerId: peer, message: 'hi', date: 0 });
        const edited = new evts.EditedMessage({}).build(new Api.UpdateEditMessage({ message: message, pts: 1, ptsCount: 1 }));
        ok(edited instanceof evts.EditedMessageEvent, 'EditedMessage builds an EditedMessageEvent from UpdateEditMessage');
        const created = new evts.NewMessage({}).build(new Api.UpdateNewMessage({ message: message, pts: 1, ptsCount: 1 }));
        ok(created instanceof evts.NewMessageEvent, 'NewMessage builds a NewMessageEvent from UpdateNewMessage');
        const typing = new Api.UpdateUserTyping({ userId: 42, action: new Api.SendMessageTypingAction() });
        ok(new evts.Raw({}).build(typing) === typing, 'Raw builder passes UpdateUserTyping through');

        // Client construction must not touch the network.
        const session = new StringSession('');
        ok(session.save() === '', 'StringSession("").save() === ""');
        ok(typeof session.setDC === 'function', 'StringSession#setDC exists');
        const client = new TelegramClient(session, 12345, '0123456789abcdef0123456789abcdef', {
            connectionRetries: 5,
            deviceModel: 'Squire',
            systemVersion: '1.0',
            appVersion: '1.0',
        });
        ok(client instanceof TelegramClient, 'TelegramClient constructed with StringSession("")');
        ok(client.connected === false || client.connected === undefined, 'client is not connected after construction');
        ok(client.session === session, 'client.session is the StringSession passed in');
        // Class names are minified; PromisedWebSockets#toString() is a constant.
        ok(client.networkSocket && client.networkSocket.prototype.toString() === 'PromisedWebSocket',
            'client uses the WebSocket transport (PromisedWebSockets), not raw TCP');
        // Let any scheduled microtasks/timers run.
        await new Promise(function (resolve) { setTimeout(resolve, 100); });
    } catch (err) {
        ok(false, 'offline API/client exercise threw: ' + (err && err.stack));
    }

    ok(env.events.websockets.length === 0, 'no WebSocket opened (' + env.events.websockets.length + ')');
    ok(env.events.xhr === 0 && env.events.fetch === 0, 'no XMLHttpRequest/fetch used');
    ok(env.events.requires.length === 0, 'bundle never called require() (' + env.events.requires.join(', ') + ')');
    ok(env.events.defines === 0, 'bundle never called AMD define() (' + env.events.defines + ')');
    const errors = env.events.logs.filter(function (l) { return l[0] === 'error'; });
    ok(errors.length === 0, 'no console.error output during load/construction' +
        (errors.length ? ': ' + JSON.stringify(errors.slice(0, 3)) : ''));

    for (const handle of env.timers) {
        clearTimeout(handle);
        clearInterval(handle);
    }
}

function shimSelfTest() {
    section('crypto shim self-test (scripts/shims/crypto.js vs Node crypto)');
    const shim = require('./shims/crypto.js');
    const data = Buffer.from('squire smoke test ✓');
    for (const algo of ['sha1', 'sha256', 'sha512']) {
        ok(shim.createHash(algo).update(data).digest('hex') === nodeCrypto.createHash(algo).update(data).digest('hex'),
            algo + ' digest matches');
    }
    const salt = Buffer.from('salt');
    ok(shim.pbkdf2Sync('pw', salt, 1000, 64, 'sha512').equals(nodeCrypto.pbkdf2Sync('pw', salt, 1000, 64, 'sha512')),
        'pbkdf2Sync sha512 matches');
}

async function main() {
    console.log('Smoke testing ' + path.relative(REPO_ROOT, bundlePath));
    const code = fs.readFileSync(bundlePath, 'utf8');
    console.log('sha256 ' + sha256(fs.readFileSync(bundlePath)) + ', ' + fs.statSync(bundlePath).size + ' bytes');
    try {
        staticChecks(code);
    } catch (err) {
        ok(false, err.message);
    }
    shimSelfTest();
    await runScenario(code, { name: 'pkjs-minimal (no window/self/navigator/crypto/Buffer/process)' });
    await runScenario(code, { name: 'pkjs-window (window === global, crypto.getRandomValues, no subtle)', windowIsGlobal: true, cryptoGetRandomValues: true });
    await runScenario(code, { name: 'node-like (vitest-style globals)', windowIsGlobal: true, cryptoGetRandomValues: true, nodeLike: true });

    console.log('');
    if (failures) {
        console.error(failures + ' smoke check(s) FAILED');
        process.exit(1);
    }
    console.log('All smoke checks passed');
}

main().catch(function (err) {
    console.error(err && err.stack ? err.stack : err);
    process.exit(1);
});
