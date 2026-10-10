# Squire Plus 0.1.0 release record

- Source tag: `squire-plus-v0.1.0`
- Store: https://apps.repebble.com/dd12fd7290054f6aaaa4e41f
- App UUID: `6f97ab75-3f6b-461b-9750-e6486c6d01cb`
- Platforms built: basalt, chalk, diorite, emery, gabbro.
- Local configured PBW SHA-256: `ede2d07810b2b0c2f6efe1ffda3adffb0bf3b3b493d6d90b71e21a925229eb34`.

The local PBW produced during Store submission is byte-identical to the test PBW supplied to Andreas. This hash is for the local upload input, not an independently downloaded Store artifact. Application credentials were supplied through the build environment, not committed. No personal Telegram session or bot token was used in the build.

## Android acceptance

Andreas reported successful real-watch tests of the configured test PBW:

- Sending a question and receiving the agent's reply on the watch.
- Closing and reopening the app, then restoring the previous conversation.
- Waiting for a slower, tool-using reply.
- Bluetooth interruption and recovery.
- Signing out, signing back in and sending another question.

These are user-reported acceptance results, not automated device telemetry. The precise watch model and Android/Pebble app versions were not recorded. They do not establish hardware acceptance on every build target. Installation through the Store has not yet been independently confirmed. iOS remains unverified.

## Local verification at source release

- 101 JavaScript tests passed.
- 281 C sanitizer checks passed, plus formatted-text tests.
- Telegram bundle reproducibility and smoke tests passed.
- Configured five-platform PBW build succeeded during submission.
- The original Apache licence and upstream credits are retained.

## Publication caveat

The CLI was invoked without `--is-published`, intending a nonpublic release. A subsequent authenticated Store read nevertheless reported `visibility: listed`, `visible: true`, and release `is_published: true`. This discrepancy was disclosed to the maintainer. Do not rely on omission of that flag to guarantee a draft for a new app.

## Known limitations and follow-up

- Telegram notifications can duplicate Squire Plus reply notifications; deferred by the maintainer.
- The Store currently has a signed-out home screenshot from the earlier design verification, not a new full set of Squire Plus screenshots.
- Telegram API terms applicability to an assistant client remains an unresolved release consideration. Store acceptance is not confirmation of compliance.
- Live sign-out was tested functionally; server-side session revocation was not independently inspected.

This source release records the already-published app identity and documentation. It introduces no further app behaviour changes or Store upload.

## Addendum, 2026-10-10: Store install on Android

Added after the record above; the record itself is left as written at the time.

Andreas later reported installing Squire Plus 0.1.0 **from the Store** on Android and
confirming that the Store-installed app works. The individual messaging, slow-reply,
history, Bluetooth and sign-out/sign-in checks listed above were reported earlier
against the sideloaded test build, not separately repeated after Store installation.

As before, this is user-reported hardware evidence, not automated telemetry. The exact watch
model and Android and Pebble app versions were not recorded. It is not coverage of every
build target, and iOS remains unverified. This supersedes only the earlier statement that
Store installation had not been independently confirmed. It confirms installation as
reported by the user, not an independent download and hash check of the Store artifact.
