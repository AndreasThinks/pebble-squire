# Emulator screenshots

Real screenshots from the Pebble SDK 4.9.169 emulator, taken with `pebble screenshot` on
2026-10-10. Every build was an offline development build (no Telegram credentials), so
sign-in ends in the "not configured" error card and requests end in "Not signed in". Signed-in
state and conversation history were injected with `pebble send-app-message` (keys
`TELEGRAM_CONNECTED`, `HISTORY_*`); no Telegram account was used.

They show layout only, not dictation, Bluetooth delivery or Telegram behaviour. There are no
gabbro screenshots: its emulator would not complete the connection handshake in this pass.

## Provenance

**From the final watch code:**

- `emery_home`, `emery_more_menu`, `emery_conversation`, `emery_prompts_history`, `emery_conversation_error`, `emery_quick_replies`
- `basalt_home`, `basalt_home_signed_out`, `basalt_signin_phone`
- `diorite_home_signed_out`, `diorite_more_menu`, `diorite_signin_phone`
- `chalk_home`, `chalk_more_menu`, `chalk_signin_phone`

**From earlier builds the same day.** These screens' code has since changed only in the ways listed:

- `emery_onboarding`, `emery_onboarding_end`, `emery_signin_phone`, `emery_signin_phone_digits`, `emery_signin_error_card`: out-of-memory guards only.
- `emery_home_signed_out`: the caption has since moved 4 px closer to the greeting.
- `basalt_signin_phone_elided`: its footer shows the old, truncated "Hold SELECT when d…". On 144 px screens it now reads "Hold SELECT: done".
- `basalt_conversation`, `basalt_conversation_error`, `basalt_quick_replies`, `diorite_onboarding`, `diorite_progress_card`, `diorite_conversation_error`, `chalk_onboarding`, `chalk_progress_card`, `chalk_conversation_error`, `chalk_prompts`: unchanged apart from out-of-memory guards.

The full raw set, including superseded captures that show the bugs fixed (caption collisions,
truncated round menu label), is outside the repository in `/home/avery/projects/squire-verification/`.
