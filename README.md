# Squire (formerly Bobby Assistant)

Squire connects your Pebble smartwatch to a [Hermes](https://github.com/nousresearch/hermes-agent) or [OpenClaw](https://github.com/openclaw/openclaw) agent through Telegram.

```text
Watch app → Pebble phone app (PebbleKit JS) → Telegram MTProto → your agent bot
```

The phone connects directly to Telegram. There is no separate Squire server, but a running agent backend and its Telegram bot are required for useful replies.

## Requirements

- A supported Pebble: basalt, chalk, diorite, emery or gabbro. Aplite and flint are not build targets.
- A Pebble phone app with working PebbleKit JS, WebSockets and a secure `crypto.getRandomValues` implementation. Squire refuses Telegram connections when secure randomness is unavailable. Cobble has known compatibility issues and is not supported. A successful build or emulator launch does not establish phone compatibility.
- An existing Telegram account and a bot configured in your Hermes or OpenClaw backend.
- Telegram application credentials (`api_id` and `api_hash`) from [my.telegram.org/apps](https://my.telegram.org/apps). These identify the Telegram client; they are different from your bot's BotFather token.
- For development: Node.js 22 or 24, Python 3.10+ (tested with 3.12), a C compiler and Make for host tests, and the Pebble SDK. The validated tool versions are `pebble-tool` 5.0.40 and SDK 4.33.1.

### Set up the agent bot

1. Create a bot with [@BotFather](https://t.me/BotFather) using `/newbot`.
2. Configure Hermes or OpenClaw with the bot token and start that backend.
3. Note the bot's username, for example `@MyHermesBot`. The bot token belongs in the backend configuration, **not** Squire's settings.
4. Make sure the backend permits your Telegram account to use the bot. Send `/start` to the bot in Telegram if your backend needs it.

## Build and install

Clone this repository and open its root directory. Use an isolated Python environment:

```bash
python3 -m venv .venv
. .venv/bin/activate
python -m pip install pebble-tool==5.0.40
pebble sdk install 4.33.1
pebble sdk activate 4.33.1
npm install --package-lock=false
```

Export `TELEGRAM_APP_ID` and `TELEGRAM_APP_HASH` in the shell running the build, using the values from my.telegram.org/apps. The ID must be a positive integer and the hash must contain 32 hexadecimal characters. `pebble build` does **not** automatically load a `.env` file. Keep credentials out of commits and build logs.

```bash
pebble build -- -j4
pebble install --phone PHONE_IP
```

Enable the developer connection in the Pebble phone app before installing. Alternatively, install the generated `build/pebble-squire.pbw` through your phone. The package contains all five supported watch targets and the phone JavaScript.

For an **offline development build** that cannot sign in to Telegram:

```bash
SQUIRE_ALLOW_UNCONFIGURED_TELEGRAM=1 pebble build -- -j4
pebble install --emulator basalt
```

The override permits missing credentials only when **both** are absent. Partially configured or malformed credentials still fail the build. Do not distribute this development build as a working Telegram client.

To open the Clay settings page in a local emulator with a browser, activate the Python environment above and run `python open-clay-config.py`. That helper builds and installs on **emery**; it inherits the same build environment variables. Emulator testing does not validate real phone sign-in, Bluetooth delivery or dictation.

### CI builds

The GitHub workflow runs the JavaScript suite, the host C suite, reproducible Telegram bundle verification and smoke tests, then builds all five watch targets with the pinned SDK. It runs on relevant pushes, pull requests and manual dispatches.

Set **both** repository secrets `TELEGRAM_APP_ID` and `TELEGRAM_APP_HASH` to produce a configured app. When both are unavailable (including external pull requests), CI explicitly creates an offline development build. Downloaded CI artifacts from such runs cannot sign in. CI does not log in to Telegram or send messages to a bot.

## Configure and use Squire

1. Open Squire's settings in the Pebble phone app. Enter your agent's **bot username** and save. Squire validates the username and verifies the account is a bot before sending prompts; it never picks a default recipient.
2. Complete the watch's introductory consent screens. Location is optional and disabled until allowed. You can change it later in settings.
3. Sign in to Telegram:
   - **Watch:** start a conversation, enter the phone number's digits including country code (the watch adds `+`), then enter the code sent by Telegram. UP/DOWN changes the digit, SELECT adds it, BACK deletes it, and holding SELECT opens confirmation.
   - **Phone settings:** enter the number in international format, save, then reopen settings and enter the login code within 15 minutes. Save again.
   - **Two-step verification:** enter the cloud password in the phone settings. You may supply it with the code, or on a later visit after Squire asks for it. The password stage expires after 30 minutes.
4. Open a conversation again after sign-in. SELECT dictates a prompt. UP on the home screen opens recent prompts (or sample prompts when there is no history); holding SELECT resumes recent history when available. Only one agent request is active at a time.
5. **Disconnect** in the home screen's More menu signs out. Squire attempts server-side session revocation and always forgets its local session. If Telegram cannot be reached, remove the Squire session manually under **Telegram → Settings → Devices**.

Quick launch can open the home screen, start a conversation and stay open, or close after a minute of inactivity. It does not close while a request is still pending. The confirm-prompts setting lets you review dictation before sending.

### Agent integration

Squire prepends a `<system>...</system>` block asking for concise watch-sized answers and providing the phone's UTC offset. If location is allowed and available, it includes **precise latitude and longitude**, not just a city name. A resumed conversation with a known thread ID also uses a `[thread:<id>]` prefix. Your backend determines how those prefixes affect conversation state.

Replies are accepted only from the configured bot's private chat, after the prompt was sent. Consecutive messages are joined with blank lines. Message edits update the displayed reply; non-append edits replace the transcript. Media-only replies show a placeholder directing you to Telegram.

End the final reply with **`[done]`** for reliable completion of multi-part responses. Squire strips that trailing marker and finishes immediately. Otherwise, it finishes after **10 seconds** without messages, edits or typing activity. Once that bot has used `[done]`, the quiet window increases to **60 seconds**. It waits up to **2 minutes of silence** before the first reply and up to **5 minutes overall**, including connecting and sending. Later replies and edits remain visible in Telegram but do not reopen a finished watch request.

Recent history contains at most eight entries, oldest first, with each entry truncated on a UTF-8 boundary to fit the watch. Fetch or delivery failures clear partial history; a stalled fetch times out after one minute. Signing out clears the watch's fetched history; it does not delete the chat in Telegram.

## Development and verification

From the repository root:

```bash
npm test
make -C tests/c
cd scripts
npm ci
npm run verify:telegram
npm run smoke:telegram
```

The JavaScript tests use offline Telegram and Pebble fakes. The C tests exercise conversation storage, request correlation, watchdogs, history, formatted-text rendering and allocation failures with AddressSanitizer and UndefinedBehaviorSanitizer, without needing the SDK. The bundle checks rebuild to temporary files, compare the committed checksum and exercise a PebbleKit-like runtime.

For changes to the bundle inputs, see [scripts/README.md](scripts/README.md). Regenerate with `npm run build:telegram`, then commit both `src/pkjs/lib/telegram-bundle.js` and `scripts/telegram-bundle.sha256`. Do not hand-edit the generated bundle. Root dependencies have no tracked lockfile; `scripts/package-lock.json` pins the bundle dependencies.

```text
src/c/                 Watch UI, conversations, auth and memory handling
src/pkjs/              Phone settings, location, history and agent sessions
src/pkjs/telegram/     Telegram client, sign-in and session storage
resources/             Watch icons, animations and consent text
scripts/               Reproducible Telegram bundle tooling
tests/                 Offline JavaScript and host C regression tests
wscript                Watch build and Telegram credential injection
```

### Troubleshooting

- **Build asks for credentials:** export both application credentials in the build shell, or explicitly use the offline mode above.
- **Secure random source unavailable:** the phone runtime cannot safely run the Telegram client. Use a runtime providing native `crypto.getRandomValues`; Squire does not fall back to `Math.random()`.
- **Not signed in / session expired:** sign in again. Telegram may revoke sessions, including when the same auth key is used by duplicate clients.
- **No agent bot / bot not found:** verify the username in settings, your backend's configuration and its access policy. Squire refuses regular user accounts.
- **No reply:** check the phone's network, backend status and the Telegram chat. Long multi-part replies should use `[done]`. A late reply can still arrive in Telegram after the watch stops waiting.
- **Reply cut short / delivery failed:** check Bluetooth and phone connectivity. Long replies can exhaust the watch heap; shorten the backend's output.
- **SDK fails on a sandbox path:** Pebble tools store data under `XDG_DATA_HOME` on Linux; point it and npm's cache at writable directories. `pebble-tool` also requires `/var/tmp/pebble-sdk` to point to its SDK data directory. Preserve unexpected existing files when configuring that link.

## Privacy and security

- Prompts, replies and enabled location coordinates travel through Telegram to your configured backend. Backend permissions, retention and actions depend on your agent setup.
- The Telegram session is a **bearer credential** stored in the Pebble phone app's private localStorage. Anyone obtaining it may act as your account until revocation. It is never sent to the watch or settings page. Encrypting it with a key embedded in the same app would not provide meaningful protection.
- Pending sign-in temporarily stores the phone number, code hash and temporary auth session, so sign-in can survive phone-JavaScript restarts. Expired state is discarded on the next auth access. Login codes and cloud passwords are never persisted or logged.
- Location is acquired only when allowed. Disabling it clears Squire's coordinate cache and ignores outstanding position results. When acquisition fails, an allowed previously cached position may still be used.
- Telegram application credentials are embedded in the generated phone JavaScript and therefore in the PBW, as in other Telegram clients. Build artifacts containing them should be handled accordingly. Bot tokens never belong in this app.
- Automated tests and emulator checks do not prove live Telegram sign-in, real-device dictation or backend actions work. Test those with your own account, compatible phone runtime and backend before distributing a release.

## Contributing and license

See [CONTRIBUTING.md](CONTRIBUTING.md) for the Developer Certificate of Origin requirements. Licensed under Apache 2.0; see [LICENSE](LICENSE).

Report issues at [AndreasThinks/pebble-squire](https://github.com/AndreasThinks/pebble-squire/issues).

This project is not an official Google project. Google does not support it and disclaims warranties as to its quality, merchantability or fitness for a particular purpose.
