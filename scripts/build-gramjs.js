#!/usr/bin/env node
'use strict';
/**
 * Reproducible build of src/pkjs/lib/telegram-bundle.js (GramJS for PebbleKit JS).
 *
 *   node build-gramjs.js                 build and write the bundle + scripts/telegram-bundle.sha256
 *   node build-gramjs.js --out <path>    build to another path (checksum file is left untouched)
 *   node build-gramjs.js --check         rebuild into a temp dir and fail unless it is byte-for-byte
 *                                        identical (SHA-256) to the committed bundle and checksum file
 *
 * Inputs are pinned: exact versions in package.json, integrity hashes in
 * package-lock.json (install with `npm ci`), and the installed node_modules are
 * verified against the lockfile before building. The output contains no
 * timestamps or absolute paths, so identical inputs give identical bytes.
 * See README.md in this directory for the rationale behind every shim.
 */

const fs = require('fs');
const os = require('os');
const path = require('path');
const { builtinModules } = require('module');

const {
    CHECKSUM_FILE,
    DEFAULT_BUNDLE,
    REPO_ROOT,
    SCRIPTS_DIR,
    checkPebbleWebpackParse,
    formatChecksumFile,
    readChecksumFile,
    sha256,
    sha256File,
} = require('./bundle-checks');

// npm `telegram@2.26.22` is GramJS's *Node* flavour; its telegram/Version.js
// reports "2.26.21" (GramJS publishes a browser flavour as 2.26.21 whose
// Version.js says "2.26.20"). The Node flavour is used on purpose: its hashing
// and PBKDF2 go through Node's `crypto` API, which we replace with a
// synchronous pure-JS shim, whereas the browser flavour hard-requires
// WebCrypto `crypto.subtle`, which PebbleKit JS does not provide.
const EXPECTED_GRAMJS_VERSION_STRING = '2.26.21';

const SHIMS_DIR = path.join(SCRIPTS_DIR, 'shims');
const ENTRY = 'telegram-entry.js';
const BANNER_FILE = path.join(SCRIPTS_DIR, 'bundle-banner.js');
// Files that are not esbuild inputs but still determine the output bytes.
const EXTRA_HASHED_FILES = ['build-gramjs.js', 'bundle-banner.js', 'bundle-checks.js', 'package.json', 'package-lock.json'];

// Node built-in modules referenced by the dependency graph and what they are
// replaced with. Any built-in not listed here fails the build, so new
// dependencies cannot silently drag Node-only code into the PKJS bundle.
const BUILTIN_REPLACEMENTS = {
    crypto: path.join(SHIMS_DIR, 'crypto.js'),
    fs: path.join(SHIMS_DIR, 'empty.js'),
    net: path.join(SHIMS_DIR, 'empty.js'),
    os: path.join(SHIMS_DIR, 'os.js'),
    path: 'path-browserify',
    util: path.join(SHIMS_DIR, 'util.js'),
};
// The npm `buffer` package (also what GramJS imports as "buffer/").
const BUFFER_PACKAGE = 'buffer/';

// npm packages replaced wholesale.
const PACKAGE_REPLACEMENTS = {
    // Lazy native-WebSocket lookup; the package's browser build pulls in
    // es5-ext and captures globalThis.WebSocket at load time.
    websocket: path.join(SHIMS_DIR, 'websocket.js'),
    // Node-only features (SOCKS proxies over raw TCP, file-backed StoreSession).
    socks: path.join(SHIMS_DIR, 'unsupported.js'),
    'node-localstorage': path.join(SHIMS_DIR, 'unsupported.js'),
};

const ESBUILD_OPTIONS = {
    absWorkingDir: SCRIPTS_DIR,
    entryPoints: [ENTRY],
    bundle: true,
    write: false,
    metafile: true,
    format: 'cjs',
    platform: 'browser',
    // Pebble SDK 4.x bundles PKJS with webpack 1.15 / acorn 3.3 (ecmaVersion 6):
    // anything newer than ES2015 syntax breaks `pebble build`.
    target: 'es2015',
    minify: true,
    charset: 'ascii',
    legalComments: 'eof',
    sourcemap: false,
    define: {
        'process.env.NODE_ENV': '"production"',
        // Disable UMD/AMD branches (big-integer, js-sha*). Pebble's webpack 1
        // rewrites any free `define` into its AMD shim (with truthy
        // define.amd), which would let a dependency overwrite this bundle's
        // module.exports while it is being evaluated.
        define: 'undefined',
    },
    logLevel: 'silent',
};

function fail(message) {
    console.error('[build-gramjs] ERROR: ' + message);
    process.exit(1);
}

function log(message) {
    console.log('[build-gramjs] ' + message);
}

function parseArgs(argv) {
    const args = { out: DEFAULT_BUNDLE, check: false };
    for (let i = 0; i < argv.length; i++) {
        const arg = argv[i];
        if (arg === '--check') {
            args.check = true;
        } else if (arg === '--out') {
            if (!argv[i + 1]) fail('--out needs a path');
            args.out = path.resolve(process.cwd(), argv[++i]);
        } else if (arg.startsWith('--out=')) {
            args.out = path.resolve(process.cwd(), arg.slice('--out='.length));
        } else if (arg === '--help' || arg === '-h') {
            console.log('Usage: node build-gramjs.js [--out <path>] [--check]');
            process.exit(0);
        } else {
            fail('Unknown argument: ' + arg);
        }
    }
    return args;
}

function readJson(file) {
    return JSON.parse(fs.readFileSync(file, 'utf8'));
}

function readText(file) {
    // Normalise line endings so a CRLF checkout cannot change the output.
    return fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n');
}

/** Verify pins, lockfile integrity metadata and installed node_modules. */
function verifyDependencies() {
    const pkg = readJson(path.join(SCRIPTS_DIR, 'package.json'));
    const lockPath = path.join(SCRIPTS_DIR, 'package-lock.json');
    if (!fs.existsSync(lockPath)) fail('scripts/package-lock.json is missing');
    const lock = readJson(lockPath);

    const declared = Object.assign({}, pkg.dependencies, pkg.devDependencies);
    for (const name of Object.keys(declared)) {
        if (!/^\d+\.\d+\.\d+$/.test(declared[name])) {
            fail('package.json must pin exact versions; ' + name + ' is "' + declared[name] + '"');
        }
    }

    if (lock.lockfileVersion !== 3) fail('package-lock.json must be lockfileVersion 3');
    const root = lock.packages && lock.packages[''];
    const lockDeclared = Object.assign({}, root && root.dependencies, root && root.devDependencies);
    if (JSON.stringify(sortKeys(lockDeclared)) !== JSON.stringify(sortKeys(declared))) {
        fail('package-lock.json is out of sync with package.json (run `npm install` and commit the lockfile)');
    }

    let checked = 0;
    for (const key of Object.keys(lock.packages).sort()) {
        if (!key) continue;
        const entry = lock.packages[key];
        if (entry.link) fail('Unexpected linked package in lockfile: ' + key);
        if (!entry.integrity || !/^sha512-/.test(entry.integrity)) fail('No sha512 integrity hash for ' + key);
        if (!entry.resolved || !entry.resolved.startsWith('https://registry.npmjs.org/')) {
            fail('Package not resolved from registry.npmjs.org: ' + key + ' (' + entry.resolved + ')');
        }
        const installedPkg = path.join(SCRIPTS_DIR, key, 'package.json');
        if (!fs.existsSync(installedPkg)) {
            if (entry.optional) continue; // e.g. esbuild binaries for other platforms
            fail(key + ' is not installed; run `npm ci` in scripts/');
        }
        const installedVersion = readJson(installedPkg).version;
        if (installedVersion !== entry.version) {
            fail(key + ' is ' + installedVersion + ' but the lockfile pins ' + entry.version + '; run `npm ci` in scripts/');
        }
        checked++;
    }

    const telegramVersion = readJson(path.join(SCRIPTS_DIR, 'node_modules', 'telegram', 'package.json')).version;
    if (telegramVersion !== pkg.dependencies.telegram) {
        fail('Installed telegram is ' + telegramVersion + ', pinned ' + pkg.dependencies.telegram);
    }
    const gramjsVersion = require(path.join(SCRIPTS_DIR, 'node_modules', 'telegram', 'Version.js')).version;
    if (gramjsVersion !== EXPECTED_GRAMJS_VERSION_STRING) {
        fail('telegram/Version.js reports ' + gramjsVersion + ', expected ' + EXPECTED_GRAMJS_VERSION_STRING +
            ' (update EXPECTED_GRAMJS_VERSION_STRING deliberately when upgrading)');
    }
    const esbuild = require('esbuild');
    if (esbuild.version !== pkg.devDependencies.esbuild) {
        fail('esbuild JS API is ' + esbuild.version + ', pinned ' + pkg.devDependencies.esbuild);
    }
    log('dependencies OK: ' + checked + ' installed packages match package-lock.json; telegram@' + telegramVersion +
        ' (GramJS ' + gramjsVersion + '), esbuild@' + esbuild.version);
    return { pkg, telegramVersion, gramjsVersion, esbuild };
}

function sortKeys(obj) {
    const out = {};
    for (const key of Object.keys(obj || {}).sort()) out[key] = obj[key];
    return out;
}

function makeResolvePlugin(usage) {
    const builtins = new Set(builtinModules.filter((m) => !m.startsWith('_')));
    const escape = (s) => s.replace(/[.*+?^${}()|[\]\\/-]/g, '\\$&');
    const packageFilter = new RegExp('^(' + Object.keys(PACKAGE_REPLACEMENTS).map(escape).join('|') + ')$');
    const record = (request, importer, target) => {
        const from = path.relative(SCRIPTS_DIR, importer).split(path.sep).join('/');
        const key = request + ' -> ' + target;
        if (!usage[key]) usage[key] = new Set();
        usage[key].add(from);
    };

    return {
        name: 'squire-pkjs-resolve',
        setup(build) {
            build.onResolve({ filter: packageFilter }, (args) => {
                const target = PACKAGE_REPLACEMENTS[args.path];
                record(args.path, args.importer, path.relative(SCRIPTS_DIR, target));
                return { path: target };
            });
            build.onResolve({ filter: /^(node:)?[a-z_/]+$/ }, (args) => {
                const name = args.path.replace(/^node:/, '');
                if (!builtins.has(name)) return undefined;
                // js-sha* do `require('buffer').Buffer`; GramJS uses "buffer/".
                if (name === 'buffer') {
                    record(args.path, args.importer, 'npm:buffer');
                    return { path: require.resolve(BUFFER_PACKAGE, { paths: [SCRIPTS_DIR] }) };
                }
                const replacement = BUILTIN_REPLACEMENTS[name];
                if (!replacement) {
                    return {
                        errors: [{
                            text: 'Unmapped Node built-in "' + args.path + '" imported by ' +
                                path.relative(SCRIPTS_DIR, args.importer) +
                                ' - add a PKJS-safe replacement to BUILTIN_REPLACEMENTS',
                        }],
                    };
                }
                const resolved = path.isAbsolute(replacement)
                    ? replacement
                    : require.resolve(replacement, { paths: [SCRIPTS_DIR] });
                record(args.path, args.importer, path.isAbsolute(replacement)
                    ? path.relative(SCRIPTS_DIR, replacement)
                    : 'npm:' + replacement);
                return { path: resolved };
            });
        },
    };
}

function computeInputsHash(metafile, esbuildVersion) {
    const lines = [];
    lines.push('esbuild ' + esbuildVersion);
    // absWorkingDir is machine-specific and does not affect the output bytes.
    lines.push('options ' + JSON.stringify(Object.assign({}, ESBUILD_OPTIONS, { absWorkingDir: '.' })));
    lines.push('builtins ' + JSON.stringify(sortKeys(BUILTIN_REPLACEMENTS), (k, v) =>
        typeof v === 'string' && path.isAbsolute(v) ? path.relative(SCRIPTS_DIR, v) : v));
    for (const file of EXTRA_HASHED_FILES) {
        lines.push('file ' + file + ' ' + sha256(readText(path.join(SCRIPTS_DIR, file))));
    }
    const inputs = Object.keys(metafile.inputs).sort();
    for (const input of inputs) {
        const abs = path.join(SCRIPTS_DIR, input);
        const content = fs.existsSync(abs) ? fs.readFileSync(abs) : Buffer.from('<virtual>');
        lines.push('input ' + input.split(path.sep).join('/') + ' ' + sha256(content));
    }
    return { hash: sha256(lines.join('\n') + '\n'), count: inputs.length };
}

async function build() {
    const deps = verifyDependencies();
    const usage = {};
    let result;
    try {
        result = await deps.esbuild.build(Object.assign({}, ESBUILD_OPTIONS, { plugins: [makeResolvePlugin(usage)] }));
    } catch (err) {
        const messages = (err.errors || []).map((e) =>
            (e.location ? e.location.file + ':' + e.location.line + ': ' : '') + e.text);
        fail('esbuild failed:\n  ' + (messages.length ? messages.join('\n  ') : err.message));
    }
    if (result.warnings.length) {
        const messages = result.warnings.map((w) =>
            (w.location ? w.location.file + ':' + w.location.line + ': ' : '') + w.text);
        fail('esbuild reported warnings (treated as errors):\n  ' + messages.join('\n  '));
    }

    const code = result.outputFiles[0].text;
    const inputs = computeInputsHash(result.metafile, deps.esbuild.version);
    const marker = '// squire-telegram-bundle telegram@' + deps.telegramVersion +
        ' gramjs=' + deps.gramjsVersion + ' esbuild@' + deps.esbuild.version +
        ' target=' + ESBUILD_OPTIONS.target + ' inputs-sha256=' + inputs.hash + '\n';
    const output = marker + readText(BANNER_FILE) + code;

    checkPebbleWebpackParse(output);

    log('replaced Node built-ins / packages:');
    for (const key of Object.keys(usage).sort()) {
        log('  ' + key + '  (from ' + Array.from(usage[key]).sort().join(', ') + ')');
    }
    log(inputs.count + ' input files, inputs-sha256=' + inputs.hash);
    return output;
}

function writeOutput(file, contents) {
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, contents);
}

async function main() {
    const args = parseArgs(process.argv.slice(2));
    const output = await build();
    const outputHash = sha256(output);

    if (args.check) {
        const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'squire-telegram-bundle-'));
        const tmpFile = path.join(tmpDir, 'telegram-bundle.js');
        try {
            writeOutput(tmpFile, output);
            const rebuiltHash = sha256File(tmpFile);
            const target = args.out;
            if (!fs.existsSync(target)) fail('Committed bundle not found: ' + target);
            const committedHash = sha256File(target);
            const recorded = readChecksumFile();
            log('rebuilt   sha256 ' + rebuiltHash);
            log('committed sha256 ' + committedHash + '  ' + path.relative(REPO_ROOT, target));
            log('recorded  sha256 ' + (recorded ? recorded.hash : '(missing)') + '  ' + path.relative(REPO_ROOT, CHECKSUM_FILE));
            let ok = true;
            if (rebuiltHash !== committedHash) {
                console.error('[build-gramjs] MISMATCH: rebuilt bundle differs from ' + path.relative(REPO_ROOT, target));
                ok = false;
            }
            if (target === DEFAULT_BUNDLE && (!recorded || recorded.hash !== committedHash)) {
                console.error('[build-gramjs] MISMATCH: ' + path.relative(REPO_ROOT, CHECKSUM_FILE) +
                    ' does not match the committed bundle');
                ok = false;
            }
            if (!ok) {
                console.error('[build-gramjs] Run `npm ci && npm run build:telegram` in scripts/ and commit the result.');
                process.exit(1);
            }
            log('OK: committed bundle is reproducible from pinned inputs');
        } finally {
            fs.rmSync(tmpDir, { recursive: true, force: true });
        }
        return;
    }

    writeOutput(args.out, output);
    log('wrote ' + path.relative(REPO_ROOT, args.out) + ' (' + Buffer.byteLength(output) + ' bytes, sha256 ' + outputHash + ')');
    if (args.out === DEFAULT_BUNDLE) {
        fs.writeFileSync(CHECKSUM_FILE, formatChecksumFile(outputHash));
        log('wrote ' + path.relative(REPO_ROOT, CHECKSUM_FILE));
    } else {
        log('custom --out: ' + path.relative(REPO_ROOT, CHECKSUM_FILE) + ' left unchanged');
    }
}

main().catch((err) => fail(err && err.stack ? err.stack : String(err)));
