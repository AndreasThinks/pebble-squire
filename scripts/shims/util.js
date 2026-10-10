'use strict';
// Minimal `util` replacement. GramJS's Node build only reads util.inspect
// (telegram/inspect.js) to obtain `inspect.custom` for pretty console output.
// graceful-fs (pulled in transitively) probes util.debuglog/format.
function inspect(value) {
    try {
        return JSON.stringify(value);
    } catch (e) {
        return String(value);
    }
}
inspect.custom =
    typeof Symbol === 'function' && typeof Symbol.for === 'function'
        ? Symbol.for('nodejs.util.inspect.custom')
        : '__squire_inspect_custom__';

function inherits(ctor, superCtor) {
    ctor.super_ = superCtor;
    ctor.prototype = Object.create(superCtor.prototype, {
        constructor: { value: ctor, enumerable: false, writable: true, configurable: true },
    });
}

function format() {
    return Array.prototype.map
        .call(arguments, function (arg) {
            return typeof arg === 'string' ? arg : inspect(arg);
        })
        .join(' ');
}

module.exports = {
    _extend: Object.assign,
    format: format,
    inherits: inherits,
    inspect: inspect,
};
