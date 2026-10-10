'use strict';
// Pure-JavaScript replacement for the parts of Node's `crypto` module that the
// Node build of GramJS uses (via telegram/CryptoFile.js):
//
//   Helpers.sha1 / Helpers.sha256   -> createHash('sha1' | 'sha256').update().digest()
//   Helpers.generateRandomBytes     -> randomBytes(n)
//   Password (2FA SRP)              -> pbkdf2Sync(pw, salt, 100000, 64, 'sha512')
//
// PebbleKit JS has no Node crypto and no WebCrypto `crypto.subtle`, so hashing
// is done synchronously with js-sha1 / js-sha256 / js-sha512 and PBKDF2 is
// implemented on top of their HMAC functions. Randomness comes from
// crypto.getRandomValues() of the host runtime (see pkjs-polyfills.js for the
// fallback that is installed when the host has none).
//
// The js-sha* libraries are required lazily: they themselves contain a
// `require('crypto')` (for their Node fast path) which resolves back to this
// file, and requiring them at the top level would create an init-order cycle.

function getBuffer() {
    var g = typeof globalThis !== 'undefined' ? globalThis : {};
    if (g.Buffer && typeof g.Buffer.from === 'function') return g.Buffer;
    return require('buffer').Buffer;
}

function sha1() {
    return require('js-sha1');
}

function sha256() {
    return require('js-sha256');
}

function sha512() {
    return require('js-sha512');
}

function normalizeAlgorithm(name) {
    return String(name || '').toLowerCase().replace(/-/g, '');
}

function toBuffer(data) {
    var B = getBuffer();
    return B.isBuffer(data) ? data : B.from(data);
}

function digestBytes(algorithm, data) {
    if (algorithm === 'sha1') return sha1().array(data);
    if (algorithm === 'sha256') return sha256().sha256.array(data);
    if (algorithm === 'sha512') return sha512().sha512.array(data);
    throw new Error('Unsupported hash algorithm: ' + algorithm);
}

function Hash(algorithm) {
    this.algorithm = normalizeAlgorithm(algorithm);
    if (this.algorithm !== 'sha1' && this.algorithm !== 'sha256' && this.algorithm !== 'sha512') {
        throw new Error('Unsupported hash algorithm: ' + algorithm);
    }
    this.parts = [];
}

Hash.prototype.update = function (data, encoding) {
    var B = getBuffer();
    this.parts.push(typeof data === 'string' ? B.from(data, encoding || 'utf8') : toBuffer(data));
    return this;
};

Hash.prototype.digest = function (encoding) {
    var B = getBuffer();
    var bytes = digestBytes(this.algorithm, B.concat(this.parts));
    var out = B.from(bytes);
    return encoding ? out.toString(encoding) : out;
};

function createHash(algorithm) {
    return new Hash(algorithm);
}

function hmacBytes(algorithm, key, data) {
    if (algorithm === 'sha512') return sha512().sha512.hmac.array(key, data);
    if (algorithm === 'sha256') return sha256().sha256.hmac.array(key, data);
    if (algorithm === 'sha1') return sha1().hmac.array(key, data);
    throw new Error('Unsupported PBKDF2 digest: ' + algorithm);
}

function uint32BE(i) {
    var b = getBuffer().alloc(4);
    b[0] = (i >>> 24) & 255;
    b[1] = (i >>> 16) & 255;
    b[2] = (i >>> 8) & 255;
    b[3] = i & 255;
    return b;
}

// RFC 8018 PBKDF2 (synchronous).
function pbkdf2Sync(password, salt, iterations, keylen, digest) {
    var B = getBuffer();
    var algorithm = normalizeAlgorithm(digest || 'sha1');
    var hLen = algorithm === 'sha512' ? 64 : algorithm === 'sha256' ? 32 : 20;
    if (!iterations || iterations < 1) throw new Error('PBKDF2 iterations must be positive.');
    var blocks = Math.ceil(keylen / hLen);
    var out = B.alloc(blocks * hLen);
    var pw = toBuffer(password);
    var saltBuf = toBuffer(salt);
    for (var block = 1; block <= blocks; block += 1) {
        var u = hmacBytes(algorithm, pw, B.concat([saltBuf, uint32BE(block)]));
        var t = B.from(u);
        for (var iter = 1; iter < iterations; iter += 1) {
            u = hmacBytes(algorithm, pw, u);
            for (var k = 0; k < hLen; k += 1) t[k] ^= u[k];
        }
        t.copy(out, (block - 1) * hLen);
    }
    return out.slice(0, keylen);
}

function randomBytes(count) {
    var bytes = new Uint8Array(count);
    var source =
        (typeof globalThis !== 'undefined' && globalThis.crypto) ||
        (typeof self !== 'undefined' && self.crypto) ||
        (typeof window !== 'undefined' && window.crypto) ||
        null;
    if (!source || typeof source.getRandomValues !== 'function') {
        throw new Error('Secure random source unavailable');
    }
    source.getRandomValues(bytes);
    return getBuffer().from(bytes.buffer, bytes.byteOffset, bytes.byteLength);
}

module.exports = {
    createHash: createHash,
    pbkdf2Sync: pbkdf2Sync,
    randomBytes: randomBytes,
};
