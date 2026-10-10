import { vi } from 'vitest';
import nodeCrypto from 'crypto';

const webcrypto = {
  getRandomValues(arr) {
    const buf = nodeCrypto.randomBytes(arr.length);
    arr.set(buf);
    return arr;
  },
  subtle: {
    digest(algo, data) {
      const name = algo.replace('-', '').toLowerCase();
      return Promise.resolve(nodeCrypto.createHash(name).update(Buffer.from(data)).digest());
    },
    importKey(format, keyData, algo, extractable, usages) {
      return Promise.resolve({ keyData, algo });
    },
    deriveBits(algo, key, bits) {
      const result = nodeCrypto.pbkdf2Sync(key.keyData, algo.salt, algo.iterations, bits / 8, 'sha512');
      return Promise.resolve(result);
    }
  }
};

// Node 22 has global.crypto as a read-only getter; override it
try {
  global.crypto = webcrypto;
} catch {
  Object.defineProperty(global, 'crypto', { value: webcrypto, writable: true });
}

// The bundle calls self.crypto.subtle.digest (async) and self.crypto.getRandomValues (sync)
// Patch self.crypto.subtle.digest to be synchronous with Node's crypto
// since the bundle's Hash.digest() awaits it but some call sites use it synchronously
const origDigest = global.crypto.subtle.digest.bind(global.crypto.subtle);
global.crypto.subtle.digest = function(algo, data) {
  const name = algo.replace('-', '').toLowerCase();
  const hash = nodeCrypto.createHash(name);
  hash.update(Buffer.from(data));
  return Promise.resolve(hash.digest());
};

const store = {};
global.localStorage = {
  getItem: vi.fn((key) => {
    if (key === 'GramJs:apiCache') return null;
    return store[key] ?? null;
  }),
  setItem: vi.fn((key, value) => { store[key] = value; }),
  removeItem: vi.fn((key) => { delete store[key]; }),
  clear: vi.fn(() => { Object.keys(store).forEach(k => delete store[k]); }),
};

global.Pebble = {
  sendAppMessage: vi.fn((msg, success, failure) => {
    if (success) success();
  }),
  addEventListener: vi.fn(),
  platform: 'pypkjs',
};

global.window = global;
global.window.location = { protocol: 'https:' };
global.self = global;
global.window.addEventListener = vi.fn();
// The queue stamps every message with a transport SEQ; most tests compare
// payloads without it.
global.stripSeq = function(msg) {
  const copy = Object.assign({}, msg);
  delete copy.SEQ;
  return copy;
};
