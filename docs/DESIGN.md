# Squire design notes

Squire is a small, loyal attendant for an AI agent you run. The interface should feel like
him: plain-spoken, quick to tell you what happens next, and never busy for its own sake.
These notes explain the choices in `src/c/util/style.h` and the screens built on it.

## Principles

1. **Say what SELECT does.** Every screen where the next step isn't obvious names it: the
   squire's speech bubble on the home screen, the footer on onboarding pages, the hint at
   the end of a conversation, the gesture line on sign-in. Hidden gestures (hold SELECT)
   are always also reachable by name, from the More menu or a hint.
2. **The answer is the hero.** In a conversation the agent's reply is the largest, plainest
   text on screen. The user's own words are a compact bubble; asides (thoughts, actions,
   errors) are tinted cards.
3. **One state, one look.** Status cards have five tones (info, success, warning, error,
   progress) with a coloured band, a drawn glyph and a title. Only one progress card exists
   at a time, and the phone's answer always replaces it.
4. **Still by default.** Nothing animates while idle. The only motion is a three-dot
   "working" indicator that steps every 400 ms (an app timer, not a 30 fps animation), and it
   becomes a static hint as soon as the agent finishes.
5. **Every platform, same hierarchy.** Colours have black-and-white fallbacks chosen for
   contrast, not hue, so diorite keeps the same structure. Round screens (chalk, gabbro)
   centre content inside a safe inset and pad the end of scrolling content so the last line
   can leave the bezel's curve. Emery, the primary target, gets one step larger body type.

## Palette

Heraldic: azure and gold on white paper.

| Token | Colour | Monochrome | Use |
|---|---|---|---|
| `SQUIRE_COLOR_AZURE` | Cobalt blue | Black | Bands, the user's bubbles, status bar, menus |
| `SQUIRE_COLOR_AZURE_INK` | Duke blue | Black | Small text on white (labels, captions) |
| `SQUIRE_COLOR_GOLD` | Chrome yellow | White | Selection, progress seals, the "Squire" seal |
| `SQUIRE_COLOR_MUTED` | Dark grey | Black | Secondary text, idle hint |
| `*_TINT` | Pastel yellow / celeste / melon | White + outline | Thought / action / error cards |
| `SQUIRE_COLOR_SUCCESS/WARNING/ERROR` | Green / chrome yellow / folly | Black | Status card bands |

Gold is always paired with black text. On monochrome, cards that lose their tint get a
1 px outline instead.

## Type and spacing

Gothic throughout (the system face, best hinted on Pebble). Display 28 bold for the
squire's greeting and card titles on large screens; 24 bold for replies; body 24 on
emery/gabbro and 18 elsewhere; 14 bold captions for labels and hints. LECO numbers for the
sign-in digit picker. Spacing is a 4 px grid (`SQUIRE_SPACE_*`); `SQUIRE_ROUND_INSET` adds
18 px (chalk) or 26 px (gabbro) horizontal inset.

## Screens and flow

- **Onboarding** (first launch): three pages, each with an azure band, title and progress
  seals, and a footer "● Continue" (or "● Choose" on the location page). SELECT before the
  end scrolls on a page instead of being silently ignored. If the phone can't be reached to
  save the location choice, an error card says so and the choice can be made again.
- **Home**: the squire on an azure field, speaking a random greeting plus what SELECT will do
  (for example "Ask me anything", or "Let's sign you in" before sign-in; shorter lines on
  144 px screens), right beside the SELECT microphone button. UP opens prompts; DOWN opens
  More: *Resume last chat* (the hold-SELECT shortcut, by name), *About Squire*, and
  *Sign out* or *Sign in to Telegram* ("Sign in" on round screens).
- **Sign-in**: phone number (step 1 of 2) and login code (step 2 of 2) share one layout: band,
  value, a spinner-style digit picker with UP/DOWN chevrons, and a footer naming the next
  gesture. Codes show one slot per digit; long phone numbers elide their start so the newest
  digits stay visible. Progress cards ("Requesting code", "Checking code") cover the waits;
  errors are specific and say what to do next.
- **Conversation**: azure status bar; the user's prompts in azure bubbles (right-aligned, or
  centred on round); the first reply after a prompt is labelled with a gold seal and
  "Squire". The conversation ends in a tail: working dots, then "● Reply · hold for more".
- **Prompts** (UP): "Ask again" (recent history) or "Try asking" (samples), two lines per row,
  gold selection; clear loading and empty rows.
- **Status cards**: tone band and glyph, title, scrollable body; SELECT or BACK dismisses.

## Costs and limits

- The redesign and the transport fixes add about 3.5 KB of code and strings, so free heap on
  the 64 KB platforms (basalt, chalk, diorite) drops from about 27.4 KB to about 23.9 KB. Long replies are still
  bounded by the memory-pressure handling, but test long answers on a real watch.
- During onboarding, the location choice waits at most 15 seconds for the phone to confirm.
  If it doesn't, an error card explains and SELECT offers the choice again; a late
  confirmation of an earlier choice is ignored.
- A new prompt is refused, with an explanation, while a previously sent one is still awaiting
  its reply (see the README's "Agent integration" section).
- Status-bar time, the azure field and gold may render slightly differently on hardware
  than in emulator screenshots, which apply their own colour mapping.

## Verifying

`docs/screenshots/` holds real emulator screenshots (SDK 4.9.169 emulator, offline build
with no Telegram credentials, conversation history injected with `pebble send-app-message`)
for emery, basalt, chalk and diorite. Gabbro's emulator would not complete its connection
handshake during this pass, so gabbro has no screenshots of the new design; check it on a
watch. Screenshots show layout only: they don't prove dictation, Bluetooth delivery or
Telegram sign-in. Check every change on emery first, then basalt, chalk, diorite and gabbro.
