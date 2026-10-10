# Squire Plus

<img src="docs/store/icon-144.png" alt="The Squire Plus companion: a rounded face with a dark visor, two bright eyes, a smile and a gold plus-sign crest" width="96" align="right">

An independently maintained fork of [Squire by Justin Sunseri](https://github.com/jmsunseri/pebble-squire), based on Bobby by Katharine Berry. Squire Plus is maintained by Andreas Varotsis; the original authors are credited [below](#credits). Original Apache 2.0 licence and copyright notices are retained.

Squire Plus has its own app UUID and can coexist with Squire. It requires fresh onboarding and Telegram sign-in; settings and login sessions are not migrated from the original app.

Squire Plus connects your Pebble smartwatch to a [Hermes](https://github.com/nousresearch/hermes-agent) or [OpenClaw](https://github.com/openclaw/openclaw) agent through Telegram.

```text
Watch app → Pebble phone app (PebbleKit JS) → Telegram MTProto → your agent bot
```

The phone connects directly to Telegram. There is no separate Squire Plus server, but a running agent backend and its Telegram bot are required for useful replies.

## Requirements

To use Squire Plus from the Store you need:

- A supported Pebble: basalt, chalk, diorite, emery or gabbro. Aplite and flint are not supported.
- A Pebble phone app with working PebbleKit JS, WebSockets and a secure `crypto.getRandomValues` implementation. Squire Plus refuses Telegram connections when secure randomness is unavailable. Cobble has known compatibility issues and is not supported.
- Your own Telegram account. You sign in with it on the watch or in the phone settings.
- An agent bot configured in your own Hermes or OpenClaw backend (see [Set up the agent bot](#set-up-the-agent-bot)).

Store users do **not** need Telegram application credentials. Those (`api_id` and `api_hash`) are only needed if you [build Squire Plus yourself](#build-it-yourself).

## Install from the Store

1. [Set up your agent bot](#set-up-the-agent-bot) and make sure your backend is running.
2. Install [Squire Plus from the Rebble Store](https://apps.repebble.com/dd12fd7290054f6aaaa4e41f) through your Pebble phone app.
3. Follow [Configure and use Squire Plus](#configure-and-use-squire-plus) to choose your bot and sign in to Telegram.

### Set up the agent bot

1. Create a bot with [@BotFather](https://t.me/BotFather) using `/newbot`.
2. Configure Hermes or OpenClaw with the bot token and start that backend.
3. Note the bot's username, for example `@MyHermesBot`. The bot token belongs in the backend configuration, **not** Squire Plus's settings.
4. Make sure the backend permits your Telegram account to use the bot. Send `/start` to the bot in Telegram if your backend needs it.

## Build it yourself

You need, in addition to the requirements above:

- Your own Telegram application credentials (`api_id` and `api_hash`) from [my.telegram.org/apps](https://my.telegram.org/apps). These identify the Telegram client; they are different from your bot's BotFather token. A build you make yourself uses your credentials, not the Store build's.
- Node.js 24 with npm 11.9.0, Python 3.10+ (tested with 3.12), a C compiler and Make for host tests, and the Pebble SDK. The validated tool versions are `pebble-tool` 5.0.40 and SDK 4.33.1. npm 10.9.9 crashes resolving the root dependencies; use the validated npm version (`npm install --global npm@11.9.0`).

A successful build or emulator launch does not establish phone compatibility.

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

Enable the developer connection in the Pebble phone app before installing. Alternatively, install the generated `.pbw` from `build/` through your phone (it is named after the checkout directory, for example `build/pebble-squire.pbw`). The package contains all five supported watch targets and the phone JavaScript.

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

## Configure and use Squire Plus

1. Open Squire Plus's settings in the Pebble phone app. Enter your agent's **bot username** and save. Squire Plus validates the username and verifies the account is a bot before sending prompts; it never picks a default recipient.
2. Complete the watch's introductory consent screens. Location is optional and disabled until allowed. You can change it later in settings.
3. Sign in to Telegram:
   - **Watch:** start a conversation, enter the phone number's digits including country code (the watch adds `+`), then enter the code sent by Telegram. UP/DOWN changes the digit, SELECT adds it, BACK deletes it, and holding SELECT opens confirmation.
   - **Phone settings:** enter the number in international format, save, then reopen settings and enter the login code within 15 minutes. Save again.
   - **Two-step verification:** enter the cloud password in the phone settings. You may supply it with the code, or on a later visit after Squire Plus asks for it. The password stage expires after 30 minutes.
4. Open a conversation again after sign-in. SELECT dictates a prompt. UP on the home screen opens recent prompts (or sample prompts when there is no history); holding SELECT, or **Resume last chat** in the More menu (DOWN), resumes recent history when available. In a conversation, SELECT speaks a reply and holding SELECT offers quick replies. Only one agent request is active at a time.
5. **Sign out** in the home screen's More menu signs out. Squire Plus attempts server-side session revocation and always forgets its local session. If Telegram cannot be reached, remove the Squire Plus session manually under **Telegram → Settings → Devices**.

Quick launch can open the home screen, start a conversation and stay open, or close after a minute of inactivity. It does not close while a request is still pending. The confirm-prompts setting lets you review dictation before sending.

### Agent integration

Squire Plus prepends a `<system>...</system>` block asking for concise watch-sized answers and providing the phone's UTC offset. If location is allowed and available, it includes **precise latitude and longitude**, not just a city name. A resumed conversation with a known thread ID also uses a `[thread:<id>]` prefix. Your backend determines how those prefixes affect conversation state.

Replies are accepted only from the configured bot's private chat, after the prompt was sent. Consecutive messages are joined with blank lines. Message edits update the displayed reply; non-append edits replace the transcript. Media-only replies show a placeholder directing you to Telegram.

End the final reply with **`[done]`** for reliable completion of multi-part responses. Squire Plus strips that trailing marker and finishes immediately. Otherwise, it finishes after **10 seconds** without messages, edits or typing activity. Once that bot has used `[done]`, the quiet window increases to **60 seconds**. It waits up to **2 minutes of silence** before the first reply and up to **5 minutes overall**, including connecting and sending. Later replies and edits remain visible in Telegram but do not reopen a finished watch request.

Only one request is outstanding at a time, across conversations. Once a prompt has been sent to Telegram, a new prompt (even from a reopened conversation) is refused with "Still waiting for the agent to answer your previous request" until that request finishes: its reply completes, it fails or it times out (at most 5 minutes). Bots rarely mark which message they answer, so a late reply could not otherwise be told apart from an answer to the new prompt. A prompt that hasn't reached Telegram yet is simply replaced. This narrows, but cannot remove, misattribution: a bot that keeps sending after its reply was judged complete (no `[done]`, more than the quiet window later), or that posts unprompted messages, will have those messages shown as the answer to the next prompt. End replies with `[done]` and reply-to the prompt if your backend can.

Phone-to-watch messages carry a sequence number (`SEQ`). If the phone resends a message whose acknowledgement was lost, the watch recognises and ignores the copy, so reply text is never duplicated. The phone's outgoing queue is bounded (48 messages, 32 KB). A reply that doesn't fit ends with "The reply was too long to send to the watch. Read the rest in Telegram."

If the phone has no Telegram session, or Telegram rejects it (revoked or expired), a prompt is answered with a sign-in message and the watch is marked signed out, so its home screen offers sign-in again.

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

The watch's visual design, palette and screen flow are documented in [docs/DESIGN.md](docs/DESIGN.md); the Squire Plus companion and icons in [docs/IDENTITY-REFRESH.md](docs/IDENTITY-REFRESH.md).

The JavaScript tests use offline Telegram and Pebble fakes. The C tests exercise conversation storage, request correlation, watchdogs, history, formatted-text rendering and allocation failures with AddressSanitizer and UndefinedBehaviorSanitizer, without needing the SDK. The bundle checks rebuild to temporary files, compare the committed checksum and exercise a PebbleKit-like runtime.

For changes to the bundle inputs, see [scripts/README.md](scripts/README.md). Regenerate with `npm run build:telegram`, then commit both `src/pkjs/lib/telegram-bundle.js` and `scripts/telegram-bundle.sha256`. Do not hand-edit the generated bundle. Root dependencies have no tracked lockfile; `scripts/package-lock.json` pins the bundle dependencies.

```text
src/c/                 Watch UI, conversations, auth and memory handling
docs/DESIGN.md         Design rationale: palette, type, screens and flow
docs/IDENTITY-REFRESH.md  The Squire Plus companion, icons and their sources
src/pkjs/              Phone settings, location, history and agent sessions
src/pkjs/telegram/     Telegram client, sign-in and session storage
resources/             Watch icons, animations and consent text
tools/identity/        Generator for the launcher and Store icons
scripts/               Reproducible Telegram bundle tooling
tests/                 Offline JavaScript and host C regression tests
wscript                Watch build and Telegram credential injection
```

### Real-device test checklist

Automated tests and emulator screenshots can't cover these; check them on a watch with a sign-in-ready build (one built with real `TELEGRAM_APP_ID`/`TELEGRAM_APP_HASH`; an offline development build reports "not configured" at sign-in).

- [ ] **First launch:** onboarding pages, location Allow and Don't allow, then the home screen.
- [ ] **Sign-in on the watch:** phone number, login code, wrong code (re-entry), expired code; and the "Signed in" card names the right bot.
- [ ] **Two-step verification:** password entered in phone settings completes a watch-started sign-in.
- [ ] **Sign-in from phone settings**, including the code entered on a later visit.
- [ ] **Dictation:** SELECT on home, confirm-transcripts on and off, cancelled dictation.
- [ ] **Replies:** a fast reply; a slow (over 10 s) reply; a multi-part reply ending in `[done]`; a reply the bot edits; a media-only reply.
- [ ] **Isolation:** an unrelated DM, and a message in another chat, arriving while waiting; neither appears on the watch.
- [ ] **Bluetooth:** disconnect while waiting and while a reply streams in; reconnect; the watch reports the failure rather than hanging.
- [ ] **History:** reopen the app, hold SELECT (and More → Resume last chat) during and after history loading; UP → Ask again.
- [ ] **Repeat sessions:** several prompts in one conversation, quick replies, retrying after an error.
- [ ] **Overlap:** close and reopen a conversation mid-request, then ask again; the new prompt is refused until the first request finishes, then works.
- [ ] **Location consent without the phone:** with Bluetooth off, choose a location option during onboarding; after about 15 s an error card appears and SELECT offers the choice again.
- [ ] **Bot change:** change the bot username in settings mid-reply; the watch says why the request ended.
- [ ] **Sign out:** More → Sign out with and without network; check Telegram → Settings → Devices.
- [ ] **Quick launch:** each quick-launch setting; the one-minute timeout never closes mid-reply.
- [ ] **Low memory:** a very long reply and a long conversation on basalt or diorite.
- [ ] **Every platform:** layout on each watch you have, especially gabbro (see `docs/screenshots/README.md` for which targets have emulator screenshots of the current home screen).

### Troubleshooting

- **Build asks for credentials:** export both application credentials in the build shell, or explicitly use the offline mode above.
- **Secure random source unavailable:** the phone runtime cannot safely run the Telegram client. Use a runtime providing native `crypto.getRandomValues`; Squire Plus does not fall back to `Math.random()`.
- **Not signed in / session expired:** sign in again. Telegram may revoke sessions, including when the same auth key is used by duplicate clients.
- **No agent bot / bot not found:** verify the username in settings, your backend's configuration and its access policy. Squire Plus refuses regular user accounts.
- **No reply:** check the phone's network, backend status and the Telegram chat. Long multi-part replies should use `[done]`. A late reply can still arrive in Telegram after the watch stops waiting.
- **Reply cut short / delivery failed:** check Bluetooth and phone connectivity. Long replies can exhaust the watch heap; shorten the backend's output.
- **SDK fails on a sandbox path:** Pebble tools store data under `XDG_DATA_HOME` on Linux; point it and npm's cache at writable directories. `pebble-tool` also requires `/var/tmp/pebble-sdk` to point to its SDK data directory. Preserve unexpected existing files when configuring that link.

## Privacy and security

### Where your data goes

| Data | Path | Stored where |
|---|---|---|
| Dictated prompts | Watch → phone (Bluetooth) → Telegram → your agent bot | In your Telegram chat with the bot, and wherever your backend keeps it |
| Replies | Your backend → Telegram → phone → watch | In the Telegram chat; the watch keeps the current conversation in memory only |
| Location (only if allowed) | Phone → Telegram → your backend, inside the prompt | In the Telegram chat and your backend; the phone caches the last position |
| Telegram session | Created on the phone at sign-in | Phone app's private storage only; never sent to the watch or settings page |
| Recent history | Telegram → phone → watch, when you open Squire Plus | Watch memory; cleared on sign-out |

Squire Plus has no maintainer-operated relay or analytics server. Your phone's dictation service may also process speech according to its own policy. Telegram and your agent backend do: their own terms and privacy policies apply, and your backend decides what the agent can see, keep and do.

### Details

- Prompts, replies and enabled location coordinates travel through Telegram to your configured backend. Backend permissions, retention and actions depend on your agent setup.
- The Telegram session is a **bearer credential** stored in the Pebble phone app's private localStorage. Anyone obtaining it may act as your account until revocation. It is never sent to the watch or settings page. Encrypting it with a key embedded in the same app would not provide meaningful protection.
- Pending sign-in temporarily stores the phone number, code hash and temporary auth session, so sign-in can survive phone-JavaScript restarts. Expired state is discarded on the next auth access. Login codes and cloud passwords are never persisted or logged.
- Location is acquired only when allowed. Disabling it clears Squire Plus's coordinate cache and ignores outstanding position results. When acquisition fails, an allowed previously cached position may still be used.
- Telegram application credentials are embedded in the generated phone JavaScript and therefore in the PBW, as in other Telegram clients. Build artifacts containing them should be handled accordingly. Bot tokens never belong in this app.
- Automated tests and emulator checks do not prove live Telegram sign-in, real-device dictation or backend actions work. Test those with your own account, compatible phone runtime and backend before distributing a release.

## Telegram

Squire Plus is an unofficial client built on the Telegram API. It is not affiliated with or endorsed by Telegram. Whether Telegram's API terms permit this kind of assistant client has not been resolved, and Store acceptance is not confirmation of compliance; review Telegram's terms yourself before relying on it.

## Credits

- **Squire Plus** is maintained by Andreas Varotsis. The Squire Plus companion, its home screen and the Squire Plus icons were designed for this fork.
- **Squire**, the app this is forked from, is by Justin Sunseri.
- **Bobby**, on which Squire is based, is by Katharine Berry.
- Original iconography by Stasia Michalska and Justin Sunseri. The microphone, question, more and status icons are theirs, unchanged.

The original authors do not maintain or support Squire Plus; please report problems here, not upstream.

## Contributing and license

See [CONTRIBUTING.md](CONTRIBUTING.md) for the Developer Certificate of Origin requirements. Licensed under Apache 2.0; see [LICENSE](LICENSE).

Report issues at [AndreasThinks/pebble-squire](https://github.com/AndreasThinks/pebble-squire/issues).

This project is not an official Google project. Google does not support it and disclaims warranties as to its quality, merchantability or fitness for a particular purpose.
