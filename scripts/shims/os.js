'use strict';
// Minimal `os` replacement. GramJS (telegram/client/os.js) only uses
// os.type() / os.release() to fill InitConnection.deviceModel/systemVersion
// when the caller does not pass them (Squire always does).
function constant(value) {
    return function () {
        return value;
    };
}

module.exports = {
    EOL: '\n',
    arch: constant('pebblekit'),
    cpus: function () {
        return [];
    },
    endianness: constant('LE'),
    freemem: constant(0),
    homedir: constant('/'),
    hostname: constant('pebblekit'),
    platform: constant('browser'),
    release: constant('1.0'),
    tmpdir: constant('/tmp'),
    totalmem: constant(0),
    type: constant('PebbleKitJS'),
};
