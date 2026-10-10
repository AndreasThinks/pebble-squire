# Squire Plus identity refresh

The home screen's character and greeting were redesigned so Squire Plus has its own face,
distinct from Squire's, while staying recognisably part of the same family. This note
records why it looks the way it does, which files carry it, and how it was checked.

## Before

The inherited home screen showed a large side-profile person with swept hair, drawn as one
PDC vector (`resources/images_root_screen_squire_large.pdc`). It read clearly on emery, but
on 144 px and round screens most of the head was cropped away, and the greeting text had to
wrap around the drawing. The hairstyle and profile also read as a specific person, and the
brief asked for a mascot without a specific gender.

## The companion

A front-facing companion drawn as pixel art from a handful of primitives:

- **A soft pebble of a head.** A wide rounded square, a nod to the watch itself. No hair,
  eyebrows, lashes, lips or other cues commonly read as masculine or feminine.
- **A helm visor.** A dark band across the face holds two tall, rounded eyes. It recalls a
  squire's helm without the weight of armour, and the strong dark/light contrast is what
  keeps the face legible at 25 px and on black-and-white screens.
- **An open smile,** the lower half of a disc, so the companion is visibly mid-greeting
  ("Heyo!").
- **A gold "+" crest** where a helm's plume would be. It's the "Plus" in the name and the
  identity's signature. It is gold on colour screens and white with an ink outline on
  diorite.
- **Helm hinges** at the temples (small dark discs), which balance the silhouette and read
  as hinges or ear pieces, not ears.
- **Shoulders** in a light azure, running off the bottom edge as the old squire's did, so
  the companion still rises into the frame from below.

What carries over from the original: bold black outlines on white, the azure heraldic field,
gold as the accent, the speech bubble at the top, and the random greetings with the
"what SELECT does" caption.

What's improved:

- The whole face is visible on every screen. It's sized at runtime (largest head width
  that fits under the bubble, odd so it's symmetric about one pixel column) instead of
  being one fixed drawing cropped by smaller screens.
- The bubble has a tail pointing at the companion, so it reads as speech. Its outline is
  2 px instead of a heavy 4 px, with cleaner rounded corners, and the caption no longer
  collides with greeting descenders (the "y" in "Heyo!").
- Hard pixel edges (anti-aliasing off) and integer proportions give crisp results at every
  size, with no smeared vector edges.
- Colour comes from `style.h` tokens with black-and-white fallbacks, so diorite gets a
  deliberately designed monochrome version rather than an automatic colour reduction.
- Nothing animates. The companion is drawn only when the layer redraws.

## Files

| File | Role |
|---|---|
| `src/c/companion.c`, `src/c/companion.h` | The companion, drawn from primitives. Canonical geometry. |
| `src/c/talking_squire_layer.c` | Home layout: bubble with tail, greeting and caption, companion sizing and placement on rectangular and round screens. |
| `src/c/util/style.h` | `SQUIRE_COLOR_COMPANION_*` tokens and monochrome fallbacks. |
| `tools/identity/squire_plus_identity.py` | Generator for the bitmaps the watch can't draw for itself, and for labelled previews. Mirrors `companion.c` integer for integer. |
| `resources/images_icons_app_icon.png` | Launcher and About menu icon (25 × 25, generated). |
| `docs/store/icon-80.png`, `docs/store/icon-144.png` | Store icons (generated). |
| `docs/identity/preview-*.png` | Deterministic renders, stamped PREVIEW. Not screenshots. |
| `package.json` | The `ROOT_SCREEN_SQUIRE` PDC resource entry is removed; the drawing replaces it. |

The old artwork (`resources/squire*.svg`, `resources/squire_large*.png`,
`resources/images_root_screen_squire*.pdc`) stays in the tree as upstream history but is no
longer packaged. The other upstream icons (microphone, question mark, more, status glyphs)
are unchanged and keep their original credits.

To regenerate the bitmaps, use a checkout-local environment (Pillow is the only
dependency):

```bash
uv venv .venv && uv pip install --python .venv/bin/python pillow
.venv/bin/python -I tools/identity/squire_plus_identity.py icons
.venv/bin/python -I tools/identity/squire_plus_identity.py previews
```

If you change proportions in `companion.c`, make the same change in the generator and
regenerate. The 25 px icon uses hand-tuned crest, eye and hinge sizes (`MENU_ICON_CREST`),
because the watch's ratios don't fit that small.

## Costs

The companion and the new layout add about 1.5 KB of code. On the 64 KB platforms (basalt,
chalk, diorite) the build's reported free heap drops from about 23.8–23.9 KB to about
22.2–22.3 KB. While the home screen is showing, about 0.3 KB of that comes back: the old PDC
image and text-flow attributes are no longer loaded into the heap. Emery and gabbro are
unaffected in practice (about 87.7 KB free). Long replies on basalt and diorite are still
bounded by the existing memory-pressure handling, but test them on a watch.

## Verification

Design-pass emulator captures are preserved in `docs/store/screenshots/`; read its README for provenance and limitations. They are not certified captures of the final rebuilt binary. Existing `docs/screenshots/` files predate the refresh. Final builds pass for all five targets, but fresh emulator installation failed on emery and timed out on chalk during parent verification. Gabbro has a labelled deterministic preview only. Final visual acceptance on a real watch remains required.
