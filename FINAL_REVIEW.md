# Squire final review (close-out)

- **Reviewed tree:** `main` at `950a420d7d8fd8831b6beaf6f10ad0c1de4533bf` (diff `fb6dc78..950a420`).
- **Date / model:** 2026-10-10, Claude Opus 5.5 (`claude-opus-5-5`).
- **Scope:** a bounded close-out. There was no redesign and there are no new features. Nothing was committed or pushed.

## Result

**No blocking defects were found, and no code was changed.** This file is the only change to the repository.

The five final-correction requirements were all checked against the current code and tests. All five are implemented. Where an automated test was feasible, one exists:

| # | Requirement | Implementation | Regression test |
|---|---|---|---|
| 1 | No duplicate CHAT text after a lost ACK and resend | The phone stamps every message with `SEQ` (`src/pkjs/lib/message_queue.js`), and a resend keeps the same `SEQ`. Each non-idempotent watch inbox handler drops a repeat of the last `SEQ` (`src/c/util/transport.c`), used in `conversation_manager.c`, `assistant.c` (history) and `settings.c` (auth). Only the queue head is ever resent, so "last SEQ" is sufficient. The watch ACKs a dropped replay, so the phone stops resending. | `message_queue.test.js`: resend keeps the same SEQ; a late callback from a timed-out attempt is ignored. `test_watch.c`: `test_resent_fragment_is_not_appended_twice`. |
| 2 | A late unthreaded reply can't answer the next prompt | Once a prompt has been sent to Telegram (`promptSent`), `startSession` refuses new prompts with `BUSY_MESSAGE` until the earlier request is terminal (complete, error, delivery failure or the 5-minute hard timeout). Unsent prompts are still superseded. The watch also drops CHAT tagged with a non-current `REQUEST_ID`. | `session.test.js` "refuses a new prompt while a sent one is outstanding…": close/reopen is modelled by a new `REQUEST_ID`, and the late reply goes only to the first request. Also "supersedes a prompt that has not reached Telegram yet". `test_watch.c`: `test_replies_are_correlated_with_request`. |
| 3 | Location consent recovers if the phone never answers | A 15 s `app_timer` (`consent.c`, `LOCATION_REPLY_TIMEOUT_MS`) closes the frozen menu and shows an error card; SELECT then reopens the choice. The timer is cancelled on success and on window unload. A late ACK is ignored once `expected_app_response` is cleared, or when it doesn't match the pending choice. A failed `outbox_begin` or `outbox_send` shows a "Phone unreachable" card. | None automated. The C host harness compiles only conversation, history, memory and transport code; testing this would need ActionMenu, ScrollLayer, window-stack and resource stubs. It was checked by code inspection and is on the device checklist. |
| 4 | Missing or expired Telegram session updates the watch's connected state | With no session, `Session.run` enqueues `TELEGRAM_CONNECTED: 0` before the warning. On revoked or expired auth errors, it calls `telegram.forgetInvalidSession`, which clears the session and notifies the watch. The root window re-reads `settings_is_telegram_connected()` on appear. | `session.test.js` "signed-in state" (2 tests). |
| 5 | Hard queue budget with explicit failure | Limits are 48 items and 32 KB, plus a small reserve (8 items, 2 KB) for terminal failure reports. A message that doesn't fit is refused whole and `enqueue` returns `false`. A grouped reply fails once via `onFailed('queue-full')`, which purges the group and sends "too long to send" plus CHAT_DONE. The in-flight item stays at the head, so purges never disturb in-flight accounting. | `message_queue.test.js`: saturation and recovery, ungrouped refusal, and all-or-nothing chunking. |

### Other review coverage (fb6dc78..950a420, focused on the latest transport and consent changes)

- **C lifecycle and memory:**
  - `consent.c`: the timer is cancelled on unload. The header and footer layers are optional and NULL-checked. The `action_menu` pointer is only dereferenced while a frozen menu is open.
  - `result_window.c`: OOM rollback; the single progress card is cleared on unload.
  - `auth_flow.c`: the progress card is dismissed only while a watch sign-in is active.
  - `conversation_manager.c`: the replay guard runs before request correlation.
  - No leaks, use-after-free or unbalanced subscriptions were found.
- **JS ordering, correlation and auth:**
  - Queue settle tokens, retry backoff at the head, and terminal reserve.
  - Group purge on cancel and the failed-group latch.
  - The auth lock token is released by logout, and refusal is set during sign-out.
  - The bot-change abort sends WARNING plus CHAT_DONE.
  - No regressions were found.
- **Build warnings:** these are unchanged from before this pass and are not defects:
  - `-Wzero-length-bounds` on `tuple->value->cstring[0]` (`conversation_manager.c:361`): the SDK's flexible-member idiom.
  - `-Wexpansion-to-defined` in the `pebble-events` header.
  - The RWX LOAD segment linker note.

## Verification (run on this tree, 2026-10-10)

| Command | Result |
|---|---|
| `npm test` | 8 files, **101 passed**, exit 0 |
| `make -C tests/c test` | **281 checks, 0 failures**; formatted-text test ok (ASan/UBSan, leak detection), exit 0 |
| `npm --prefix scripts run verify:telegram` | rebuilt = committed = recorded sha256 `359a7c86…cfef7f6`: "OK: committed bundle is reproducible", exit 0 |
| `npm --prefix scripts run smoke:telegram` | "All smoke checks passed", exit 0 |
| `pebble clean` then `env -u PYTHONPATH -u PYTHONHOME -u TELEGRAM_APP_ID -u TELEGRAM_APP_HASH SQUIRE_ALLOW_UNCONFIGURED_TELEGRAM=1 pebble build -- -j8` | exit 0, 0 errors, "build finished successfully"; prints "WARNING: building without Telegram API credentials" |
| `git diff --check` | clean |

**Free heap reported by the build:** basalt 23,838 B, chalk 23,943 B, diorite 23,922 B, emery 89,301 B and gabbro 89,390 B.

## Artifact

`build/pebble-squire-opus-pass.pbw` (a build output; not committed):

- **Label: UNCONFIGURED development build.** It cannot sign in to Telegram.
- 2,456,899 bytes, sha256 `a07f18e8c6548e21bdc08651884f7fcacf6f6888f0178f9f0efa79bbc60c7f61`.
- `appinfo.json`: targetPlatforms are basalt, chalk, diorite, emery and gabbro; uuid `1d272bb4-cf00-4e3e-a7f3-7394309e0341`; version 0.1.0.
- Per-platform `manifest.json` (application bytes / resource bytes):

  | Platform | Application | Resources |
  |---|---|---|
  | basalt | 33392 | 13027 |
  | chalk | 33288 | 13027 |
  | diorite | 33308 | 12915 |
  | emery | 33464 | 13027 |
  | gabbro | 33376 | 13027 |

- The embedded credentials are `"apiId": 0` and `"apiHash": ""`. No production credentials are present.
- A sign-in-ready build needs real `TELEGRAM_APP_ID`/`TELEGRAM_APP_HASH` (from the user's shell or repository secrets). That was not in scope.

## Visual evidence

- **In the checkout:** 32 real SDK 4.9.169 emulator screenshots in `docs/screenshots/`, with per-file provenance in `docs/screenshots/README.md`:

  | Platform | Screenshots |
  |---|---|
  | emery | 12 |
  | basalt | 7 |
  | chalk | 7 |
  | diorite | 6 |

- **Contact sheets** (inspected):
  - `/home/avery/projects/squire-verification/emery_final.png`: home, More, conversation, prompts, "Not signed in" error, quick replies.
  - `/home/avery/projects/squire-verification/final_small.png`: basalt, diorite and chalk home (signed in and out), More, sign-in phone.
- **Gabbro:** the PBW includes a gabbro build, but there are no screenshots. Its emulator failed the WatchVersion handshake on three attempts, and this pass did not retry.
- The UI was not changed in this pass, so the screenshots were not regenerated. They show layout only. They do **not** show live sign-in, dictation, Bluetooth delivery or backend replies. Signed-in state was injected with `pebble send-app-message`.

## On-device acceptance checklist

This needs a build made with real Telegram credentials. The full list is in `README.md` under "Real-device test checklist". The essentials:

1. **Onboarding:** choose Allow, then Don't allow. With Bluetooth off, choose a location option. About 15 s later an error card appears, and SELECT offers the choice again.
2. **Sign-in on the watch:** cover phone number, code, a wrong code, two-step password (entered on the phone), and sign out (check Telegram → Devices).
3. **Replies:** test a fast reply, a reply slower than 10 s, a multi-part reply ending in `[done]`, an edited reply and a media-only reply. Unrelated DMs and group messages must not appear.
4. **Bluetooth:** drop the connection while waiting and while a reply streams in. The watch must report the failure and must not show duplicated text after reconnecting.
5. **Overlap:** close and reopen a conversation mid-request and ask again. The prompt is refused ("Still waiting…") until the first request ends.
6. **Expired session:** revoke the session from another device, then ask. The watch shows "session expired" and the home screen returns to the signed-out state.
7. **Long replies:** a very long reply ends with "too long to send" instead of hanging. Check heap on basalt and diorite.
8. **Gabbro:** check layout, since there is no emulator evidence for it.

## Residual constraints (true, not fixable in the client)

- **Unthreaded bot replies cannot be correlated with certainty.** Refusing overlapping prompts narrows this. Still, a bot that keeps sending after a reply is judged complete (no `[done]`, beyond the quiet window or the 5-minute timeout), or that posts unprompted, will have those messages shown as the answer to the next prompt. The fix is for backends to reply-to the prompt and end with `[done]`. This is documented in the README.
- **Overlap refusal can block a new prompt for up to 5 minutes** if the bot never answers. This is intentional.
- **SEQ deduplication covers lost ACKs within one running watch app.** Its guard resets on app restart, which is harmless because the previous app's state is gone. The phone's starting SEQ is random per PebbleKit JS restart.
- **Queue saturation:** if even the terminal reserve is full, the failure report is dropped and the watch falls back to its reply watchdog.
- **Location consent:** a phone-side confirmation that arrives after the 15 s timeout is ignored. The user chooses again, and the phone keeps the last setting it received.
- **Platforms:** heap on the 64 KB platforms is about 23.9 KB free. Gabbro is visually unverified. Nothing has been tested on real hardware, with live Telegram or with real dictation.
- **Toolchain:** this pass used SDK 4.9.169 and pebble-tool 5.0.37 locally. CI pins Node 24 and npm 11.9.0 (`.github/workflows/build-pbw.yaml`), unchanged.

## Files

- Added: `FINAL_REVIEW.md` (this file).
- No source, test, documentation or CI files were changed. README and `docs/DESIGN.md` already describe the corrected behaviour accurately and were left as they are.
