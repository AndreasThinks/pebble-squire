'use strict';
// Stand-in for Node-only npm packages that GramJS's Node build references but
// that can never work inside PebbleKit JS:
//   - `socks`             (SOCKS proxy support for PromisedNetSockets / raw TCP)
//   - `node-localstorage` (StoreSession fallback when no global localStorage)
// Both are only reachable on GramJS's Node code paths. Replacing them keeps
// graceful-fs, write-file-atomic, ip-address, etc. out of the bundle. Any use
// fails with an explicit error instead of a confusing crash deep in fs code.
function unsupported(name) {
    return function () {
        throw new Error(name + ' is not supported in the Squire PebbleKit JS Telegram bundle');
    };
}

module.exports = {
    // socks
    SocksClient: {
        createConnection: unsupported('SOCKS proxy (socks.SocksClient.createConnection)'),
    },
    // node-localstorage
    LocalStorage: unsupported('node-localstorage LocalStorage'),
    JSONStorage: unsupported('node-localstorage JSONStorage'),
};
