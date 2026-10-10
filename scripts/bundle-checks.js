'use strict';
// Shared checks used by build-gramjs.js and smoke-test-bundle.js.

const crypto = require('crypto');
const fs = require('fs');
const path = require('path');

const SCRIPTS_DIR = __dirname;
const REPO_ROOT = path.resolve(SCRIPTS_DIR, '..');
const DEFAULT_BUNDLE = path.join(REPO_ROOT, 'src', 'pkjs', 'lib', 'telegram-bundle.js');
const CHECKSUM_FILE = path.join(SCRIPTS_DIR, 'telegram-bundle.sha256');
// Path recorded in the checksum file, relative to the repo root, so that
// `sha256sum -c scripts/telegram-bundle.sha256` works from the repo root.
const CHECKSUM_TARGET = 'src/pkjs/lib/telegram-bundle.js';

function sha256(data) {
    return crypto.createHash('sha256').update(data).digest('hex');
}

function sha256File(file) {
    return sha256(fs.readFileSync(file));
}

function readChecksumFile() {
    if (!fs.existsSync(CHECKSUM_FILE)) return null;
    const line = fs.readFileSync(CHECKSUM_FILE, 'utf8').split('\n')[0].trim();
    const match = /^([0-9a-f]{64})\s+\*?(.+)$/.exec(line);
    if (!match) throw new Error('Malformed checksum file: ' + CHECKSUM_FILE);
    return { hash: match[1], target: match[2] };
}

function formatChecksumFile(hash) {
    return hash + '  ' + CHECKSUM_TARGET + '\n';
}

/**
 * Parse `code` exactly the way the Pebble SDK's webpack 1.15 does (acorn
 * 3.3.0, ecmaVersion 6, module first then script). Throws with a location if
 * the bundle uses syntax newer than ES2015, which would break `pebble build`.
 */
function checkPebbleWebpackParse(code) {
    const acorn = require('acorn');
    if (acorn.version !== '3.3.0') {
        throw new Error('Expected acorn 3.3.0 (Pebble SDK webpack 1 parser), found ' + acorn.version);
    }
    const attempts = ['module', 'script'];
    let lastError = null;
    for (const sourceType of attempts) {
        try {
            acorn.parse(code, { ecmaVersion: 6, sourceType: sourceType, ranges: true, locations: true });
            return sourceType;
        } catch (err) {
            lastError = err;
        }
    }
    const loc = lastError && lastError.loc;
    let context = '';
    if (loc) {
        const line = code.split('\n')[loc.line - 1] || '';
        context = ' near: ' + JSON.stringify(line.slice(Math.max(0, loc.column - 60), loc.column + 60));
    }
    throw new Error('Bundle is not parseable by the Pebble SDK webpack (acorn 3, ES2015): ' +
        (lastError && lastError.message) + context);
}

module.exports = {
    CHECKSUM_FILE,
    CHECKSUM_TARGET,
    DEFAULT_BUNDLE,
    REPO_ROOT,
    SCRIPTS_DIR,
    checkPebbleWebpackParse,
    formatChecksumFile,
    readChecksumFile,
    sha256,
    sha256File,
};
