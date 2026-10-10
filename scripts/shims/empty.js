'use strict';
// Stand-in for Node built-ins that have no PebbleKit JS equivalent (fs, net,
// tls, assert, ...). GramJS only touches these on its Node-only code paths
// (TCP sockets, file uploads/downloads from disk, StoreSession on Node), which
// are never taken in PKJS because `window` is defined there (see
// telegram/platform.js -> isBrowser). An empty object keeps module evaluation
// side-effect free; any accidental use fails loudly with a TypeError.
module.exports = {};
