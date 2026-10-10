# Telegram bundle build scripts

This directory builds `src/pkjs/lib/telegram-bundle.js`, the GramJS
(Telegram MTProto client) bundle that Squire's PebbleKit JS code loads through
`src/pkjs/lib/bundle_loader.js`. The bundle is committed. The build is pinned
and reproducible, so anyone can check that the committed file really comes from
these sources.

## Quick reference

```bash
cd scripts
npm ci                     # install exactly what package-lock.json pins
npm run build:telegram     # regenerate src/pkjs/lib/telegram-bundle.js + telegram-bundle.sha256
npm run verify:telegram    # rebuild to a temp dir; fail unless byte-identical to the committed bundle
npm run smoke:telegram     # offline load/export/construct test in a PKJS-like vm context
```

Other options: `node build-gramjs.js --out /tmp/bundle.js` builds to a different
path and leaves the checksum file unchanged. `node smoke-test-bundle.js <file>`
smoke-tests any bundle file. From the repo root,
`sha256sum -c scripts/telegram-bundle.sha256` checks the committed bundle
against the recorded hash.

After changing anything here (dependency versions, shims, banner, polyfills,
entry, or build script), run `npm run build:telegram` and
`npm run smoke:telegram`, then commit the new bundle, `telegram-bundle.sha256`
and `package-lock.json` together.

## Files

| File | Purpose |
| --- | --- |
| `build-gramjs.js` | The build. It checks dependencies, runs esbuild, prepends the banner, runs an ES2015 parse check, and writes the bundle and checksum. Use `--check` to verify instead of writing. |
| `telegram-entry.js` | Bundle entry point (CommonJS). It loads the PKJS polyfills first, then GramJS, and defines the exports. |
| `bundle-banner.js` | Raw prelude copied verbatim to the top of the bundle (after the provenance line). It runs before any module and defines `globalThis`/`window`/`window.location`/`navigator`/`self`/`process` stubs and the js-sha flags. |
| `pkjs-polyfills.js` | The "Pebblegram" polyfills. It is bundled as the first module, so it runs before GramJS is evaluated. It provides `window.addEventListener` no-ops, `Buffer`, `crypto.getRandomValues`, a minimal `Response`, and the buffering WebSocket wrapper. |
| `shims/*.js` | Replacements for Node built-ins and Node-only packages (see below). |
| `bundle-checks.js` | Shared helpers: SHA-256, checksum file I/O, and the acorn 3 / ES2015 parse check. |
| `smoke-test-bundle.js` | Offline smoke test (see below). |
| `telegram-bundle.sha256` | `sha256sum`-format hash of the committed bundle. The build writes it and `--check` compares against it. |
| `package.json` / `package-lock.json` | Exact version pins, plus a lockfile v3 with `sha512` integrity hashes for every package. |
| `.npmrc` | `ignore-scripts=true` and `save-exact=true`. None of the inputs need install scripts: esbuild's binary comes from its optional `@esbuild/<platform>` package. |
| `prune-telegram-bundle.js` | **Not part of the pipeline.** This is a legacy experiment that strips TL schema lines from a built bundle. The shipped bundle has always contained the full schema. Pruning is unsafe because GramJS throws `TypeNotFoundError` when the server sends any constructor that was removed, and that can break whole update batches. If you run it, `verify:telegram` fails. |

## Pinned versions

| Package | Version | Why |
| --- | --- | --- |
| `telegram` | **2.26.22** | GramJS's *Node* flavour. Its `telegram/Version.js` reports `2.26.21`, which is the version string the previously shipped bundle printed. GramJS publishes two builds at once: the odd-numbered `2.26.21` on npm is the *browser* flavour and its `Version.js` says `2.26.20`. The browser flavour sends all SHA-1/SHA-256/PBKDF2 through WebCrypto `crypto.subtle`, which PebbleKit JS does not have. The Node flavour calls Node's `crypto` API instead, and we replace that with a synchronous pure-JS shim. The build fails if the installed package or its `Version.js` differ from these values. Note: upstream has archived the `telegram` package (npm points to the `teleproto` fork). |
| `esbuild` | 0.27.7 | Bundler and minifier. Its version is part of the provenance line. |
| `js-sha1` / `js-sha256` / `js-sha512` | 0.7.0 / 0.11.1 / 0.9.0 | Pure-JS hashing and HMAC for `shims/crypto.js`. These are the same major/minor lines as in the previously shipped bundle. |
| `buffer` | 6.0.3 | Buffer implementation. GramJS imports it as `buffer/`, and the polyfills install it as the global `Buffer`. |
| `acorn` | 3.3.0 | The exact parser the Pebble SDK's webpack 1.15 uses. It verifies that the output parses as ES2015. |

Transitive dependencies are pinned by `package-lock.json`. Before every build,
`build-gramjs.js` checks the following:

- every version in `package.json` is exact (no `^`/`~`);
- the lockfile is v3, matches `package.json`, and every entry has a `sha512`
  integrity hash and was resolved from `registry.npmjs.org`;
- every installed package in `node_modules` has the version the lockfile
  records (so run `npm ci`, not `npm install`);
- the installed `telegram` and `esbuild` versions match the pins.

## How the build works

1. esbuild bundles `telegram-entry.js` with these options: `format: cjs`,
   `platform: browser`, `target: es2015`, `minify`, `charset: ascii`, and
   `legalComments: eof`. The bundled third-party license notices go at the end
   of the file.
2. A resolver plugin intercepts every Node built-in. Each one maps to a
   PKJS-safe replacement, and **any built-in without a mapping fails the
   build**:

   | Import | Replacement | Used by |
   | --- | --- | --- |
   | `crypto` | `shims/crypto.js`: `createHash` (sha1/256/512), `pbkdf2Sync`, `randomBytes`, built on js-sha* and `crypto.getRandomValues` | `telegram/CryptoFile.js` (auth key handshake hashing, random bytes, 2FA PBKDF2) |
   | `os` | `shims/os.js`: constant `type()`/`release()` and similar | `telegram/client/os.js` (default device model) |
   | `path` | `path-browserify` | `telegram/client/path.js` |
   | `util` | `shims/util.js` (`inspect`, `inspect.custom`, `inherits`, `format`) | `telegram/inspect.js` |
   | `fs`, `net` | `shims/empty.js` (`{}`) | Node-only GramJS paths: TCP sockets, file uploads/downloads |
   | `buffer` | the npm `buffer` package | js-sha* Node fast path (disabled, but still resolved) |
   | `websocket` (npm) | `shims/websocket.js` looks up `WebSocket` at construction time | `telegram/extensions/PromisedWebSockets.js` |
   | `socks`, `node-localstorage` (npm) | `shims/unsupported.js` throws a clear error if called | Node-only: SOCKS proxies over TCP, and the file-backed `StoreSession` |

   GramJS picks its WebSocket transport (`PromisedWebSockets`,
   `ConnectionTCPObfuscated`) whenever `typeof window !== "undefined"`, and the
   banner guarantees that. So the Node-only paths are never used in PKJS.
3. The build adds the output to a provenance line and `bundle-banner.js`:

   ```
   // squire-telegram-bundle telegram@2.26.22 gramjs=2.26.21 esbuild@0.27.7 target=es2015 inputs-sha256=<hash>
   ```

   `inputs-sha256` hashes the esbuild version, the build options, this
   directory's build files (including `package-lock.json`), and the content of
   every file esbuild read. The line has no timestamps or absolute paths, and
   CRLF line endings are normalised. You can grep for it in
   `build/pebble-js-app.js` to see which bundle a PBW contains.
4. The output is parsed with acorn 3.3.0 at `ecmaVersion: 6`, the same way
   webpack 1 parses it in `pebble build`. Then the build writes the bundle and
   `telegram-bundle.sha256`.

### Target syntax: ES2015 (not ES5)

The Pebble SDK (4.x) bundles PKJS with webpack 1.15, which parses with
acorn 3.3 at `ecmaVersion: 6`. ES2015 syntax (arrow functions, classes,
`let`/`const`, template literals, generators) is therefore the ceiling.
esbuild lowers everything newer: `async`/`await` becomes generator helpers,
`**` becomes `Math.pow`, object spread and optional chaining are rewritten,
and so on. The previously shipped bundle also targeted ES2015. An earlier
version of this README said the bundle was Babel/ES5, but that was never
true, and Babel is no longer a dependency.

## What is patched and why

GramJS itself is **not** modified. There are no string replacements on the
output. All adaptation happens through the resolver mappings above, the
banner, `pkjs-polyfills.js`, and the entry file:

- **`self = globalThis`** (banner and polyfills). GramJS reads `self.crypto`
  and `self.localStorage`, and PKJS has no `self`. (Historically this was
  `patches/002`.)
- **`navigator` polyfill** with `onLine: true`. GramJS checks
  `navigator.onLine` before reconnecting. (Historically `patches/001`.)
- **`window` / `window.location.protocol = 'https:'`**. These make GramJS
  choose the browser WebSocket transport, using `wss://` on port 443.
- **WebSocket event buffering** (`pkjs-polyfills.js`). PKJS can fire `open`
  before GramJS assigns `onopen`. The wrapper queues events until a handler
  exists. It marks itself with `WebSocket.__pebblegramWrapped`.
  (Historically `patches/003`.)
- **`crypto.getRandomValues` required**. A host-provided secure random source
  is required for Telegram nonces, session IDs and DH secrets. If it is
  missing, the fallback throws; Squire reports an unsupported phone runtime
  before connecting. It never substitutes `Math.random()` for cryptography.
- **`Response.prototype.arrayBuffer`**, **`window.addEventListener`** no-ops,
  and a global **`Buffer`**, which the Node flavour of GramJS uses as a
  global.
- **js-sha flags** (`JS_SHA*_NO_NODE_JS`, `JS_SHA*_NO_BUFFER_FROM`). These
  force the pure-JS code paths in every environment, including Node/vitest,
  so hashing never calls back into the `crypto` shim.
- **AMD `define` disabled** (esbuild `define: { define: 'undefined' }`).
  big-integer and js-sha* have UMD wrappers. Pebble's webpack 1 rewrites any
  free `define` into its AMD shim, and that shim's `define.amd` is truthy.
  If those wrappers stayed, a dependency could replace this module's
  `module.exports` while the bundle is being evaluated. The previously
  shipped bundle had this latent problem.
- **`process` stub** (`{ env: {}, nextTick }`) for code that probes
  `process`. Nothing in the bundle reads configuration or credentials from
  `process.env`.

## Exports

```js
module.exports = { Api, StringSession, TelegramClient };
// TelegramClient.events: Raw, NewMessage, NewMessageEvent, EditedMessage, EditedMessageEvent
```

GramJS 2.26.x ships `telegram/events/EditedMessage` but does not re-export it
from `telegram/events`. The entry adds `EditedMessage` and
`EditedMessageEvent` to the object that `TelegramClient.events` returns.

## Smoke test

`smoke-test-bundle.js` runs without network access. It does the following:

- parses the bundle like the Pebble SDK webpack does (acorn 3.3.0, ES2015);
- checks that no `require(...)` call, no esbuild `__require` shim and no free
  AMD `define` remain, and that the provenance line is present;
- compares `shims/crypto.js` against Node's crypto (SHA-1/256/512 and PBKDF2);
- loads the bundle in three fresh `vm` contexts:
  - bare PKJS: no `window`, `self`, `navigator`, `crypto`, `Buffer`,
    `process` or `global`;
  - `window === global` with `crypto.getRandomValues` but no `crypto.subtle`;
  - vitest-like globals.

  In each context, `require` and a webpack-style AMD `define` are traps, and
  `WebSocket`, `XMLHttpRequest` and `fetch` throw if used. The test then:
  - asserts the export surface: every class and method that Squire uses, and
    every `TelegramClient.events` builder;
  - serialises `auth.SendCode` and `contacts.ResolveUsername` offline;
  - runs the `NewMessage`, `EditedMessage` and `Raw` builders on synthetic
    updates;
  - constructs a `TelegramClient` with `new StringSession('')` without
    connecting, and asserts that no socket, XHR, fetch or `require` was used.

The smoke test also checks that a runtime without secure randomness refuses
entropy requests, while runtimes providing `crypto.getRandomValues` keep their
native implementation. It does not cover a real MTProto handshake or login.
The tests in `tests/` use offline fakes; live sign-in and backend messaging need
real API credentials, a compatible phone runtime and a Telegram test account.
