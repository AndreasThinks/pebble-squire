#!/usr/bin/env python3
# Copyright 2026 Andreas Varotsis
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Source of the Squire Plus companion artwork outside the watch.

The watch draws the companion at runtime (src/c/companion.c). This script
mirrors that geometry, integer for integer, to produce the bitmaps the watch
can't draw for itself:

  icons     resources/images_icons_app_icon.png (launcher and About menu icon)
            docs/store/icon-48.png, docs/store/icon-144.png (Store icons)
  previews  docs/identity/preview-*.png: deterministic renders of the
            companion alone, labelled PREVIEW in the image. They are not
            screenshots and carry no text: the watch's Gothic fonts aren't
            available here.

Usage (Pillow required; see docs/IDENTITY-REFRESH.md):
  python tools/identity/squire_plus_identity.py icons
  python tools/identity/squire_plus_identity.py previews
"""

import os
import sys

from PIL import Image, ImageDraw

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))

# Pebble's 64-colour palette entries used by the identity (see src/c/util/style.h).
COBALT = (0x00, 0x55, 0xAA, 255)
OXFORD = (0x00, 0x00, 0x55, 255)
GOLD = (0xFF, 0xAA, 0x00, 255)
PICTON = (0x55, 0xAA, 0xFF, 255)
WHITE = (255, 255, 255, 255)
BLACK = (0, 0, 0, 255)
CLEAR = (0, 0, 0, 0)

COLOUR = dict(field=COBALT, ink=BLACK, face=WHITE, visor=OXFORD, eye=WHITE, crest=GOLD, body=PICTON)
MONO = dict(field=WHITE, ink=BLACK, face=WHITE, visor=BLACK, eye=WHITE, crest=WHITE, body=WHITE)


class Canvas:
    """Aliased fills with the same rectangle and disc semantics as the C code."""

    def __init__(self, img):
        self.img = img
        self.px = img.load()
        self.w, self.h = img.size

    def rect(self, x, y, w, h, c):
        for yy in range(max(y, 0), min(y + h, self.h)):
            for xx in range(max(x, 0), min(x + w, self.w)):
                self.px[xx, yy] = c

    def disc(self, cx, cy, r, c):
        # graphics_fill_circle(center, r) covers a (2r + 1) px wide disc.
        for yy in range(cy - r, cy + r + 1):
            for xx in range(cx - r, cx + r + 1):
                if 0 <= xx < self.w and 0 <= yy < self.h and (xx - cx) ** 2 + (yy - cy) ** 2 <= r * r + r:
                    self.px[xx, yy] = c

    def round_rect(self, x, y, w, h, r, c):
        # A disc of radius r is 2r + 1 px wide, so this keeps the corners inside.
        r = min(r, (min(w, h) - 1) // 2)
        self.rect(x + r, y, w - 2 * r, h, c)
        self.rect(x, y + r, w, h - 2 * r, c)
        if r <= 0:
            return
        for cx, cy in ((x + r, y + r), (x + w - 1 - r, y + r), (x + r, y + h - 1 - r), (x + w - 1 - r, y + h - 1 - r)):
            self.disc(cx, cy, r, c)

    def outlined_round_rect(self, x, y, w, h, r, s, fill, ink):
        self.round_rect(x, y, w, h, r, ink)
        self.round_rect(x + s, y + s, w - 2 * s, h - 2 * s, r - s, fill)

    def plus(self, cx, cy, arm, bar, c):
        self.rect(cx - arm // 2, cy - bar // 2, arm, bar, c)
        self.rect(cx - bar // 2, cy - arm // 2, bar, arm, c)


def cdiv(a, b):
    """C integer division (truncates toward zero)."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def geometry(w):
    """Mirrors the helpers at the top of src/c/companion.c."""
    s = max(2, w // 16)
    return dict(
        s=s,
        h=w * 7 // 8,
        arm=(w * 5 // 16) | 1,
        bar=max(3, (w // 10) | 1),
        stem=w // 10,
        outline=max(2, s - 1),
        neck=w // 16,
    )


def crest_height(g):
    return g['stem'] + g['arm'] + g['outline']


def draw_companion(cv, cx, top, w, pal, bottom, shoulders=True, crest=None):
    """Mirrors companion_draw(). `crest` overrides the crest proportions for
    the tiny launcher icon, where the watch's own ratios don't fit 25 px
    (crest, eyes and hinges are hand-tuned there)."""
    g = dict(geometry(w))
    if crest:
        g.update(crest)
    s, h = g['s'], g['h']
    left = cx - w // 2
    ink = pal['ink']

    arm, bar, stem, outline = g['arm'], g['bar'], g['stem'], g['outline']
    plus_y = top - stem - arm // 2
    if stem > 0:
        cv.rect(cx - s // 2, top - stem - 1, s | 1, stem + 2, ink)
    cv.plus(cx, plus_y, arm + 2 * outline, bar + 2 * outline, ink)
    cv.plus(cx, plus_y, arm, bar, pal['crest'])

    if shoulders:
        sw = (w * 9 // 8) | 1
        st = top + h + g['neck']
        sh = max(bottom - st, 0) + w
        cv.outlined_round_rect(cx - sw // 2, st, sw, sh, w * 3 // 8, s, pal['body'], ink)

    stud_r = g.get('stud_r', max(3, w * 5 // 32))
    stud_y = top + h // 2
    for sx in (left + s // 2, left + w - 1 - s // 2):
        cv.disc(sx, stud_y, stud_r, ink)
        cv.disc(sx, stud_y, stud_r - s, pal['visor'])

    cv.outlined_round_rect(left, top, w, h, w * 3 // 8, s, pal['face'], ink)

    vw, vh = (w * 13 // 16) | 1, w * 11 // 32
    vy = top + h * 5 // 16
    mr = w // 10
    my = vy + vh + g.get('mouth_gap', max(2, w // 24))
    cv.disc(cx, my, mr, ink)
    cv.rect(cx - mr, my - mr, 2 * mr + 1, mr, pal['face'])
    cv.round_rect(cx - vw // 2, vy, vw, vh, vh // 2, pal['visor'])
    ew = g.get('eye_w', max(5, (w // 8) | 1))
    eh, edx = vh * 3 // 4, g.get('eye_dx', w * 3 // 16)
    for ecx in (cx - edx, cx + edx):
        cv.round_rect(ecx - ew // 2, vy + (vh - eh) // 2, ew, eh, ew // 2, pal['eye'])



# The 25 px launcher icon: head and crest only, the crest sitting straight on
# the head with a 1 px outline, so the face keeps every pixel it can.
MENU_ICON_CREST = dict(stem=0, arm=7, bar=3, outline=1, mouth_gap=1, eye_w=3, eye_dx=3, stud_r=2)


def menu_icon(pal):
    img = Image.new('RGBA', (25, 25), CLEAR)
    cv = Canvas(img)
    w = 19
    top = 25 - geometry(w)['h'] - 1
    draw_companion(cv, 12, top, w, pal, 25, shoulders=False, crest=MENU_ICON_CREST)
    return img


def store_icon(size):
    """Store icons: the companion rising from the bottom of an azure tile."""
    img = Image.new('RGBA', (size, size), COBALT)
    cv = Canvas(img)
    if size >= 96:
        w = (size * 7 // 12) | 1
        g = geometry(w)
        top = size - (w // 4) - g['neck'] - g['h']
        draw_companion(cv, size // 2, top, w, COLOUR, size)
    else:
        w = (size * 2 // 3) | 1
        g = geometry(w)
        top = (size - g['h'] + crest_height(g)) // 2
        draw_companion(cv, size // 2, top, w, COLOUR, size, shoulders=False)
    return img


def label(img, text):
    """Stamps PREVIEW so a render can never pass for a screenshot."""
    d = ImageDraw.Draw(img)
    tw = 6 * len(text) + 4
    d.rectangle([0, img.height - 11, tw, img.height - 1], fill=(255, 0, 255, 255))
    d.text((2, img.height - 11), text, fill=(0, 0, 0, 255))
    return img


def preview(width, height, bar, w, pal, round_screen):
    """The companion as the home screen places it, without the bubble text."""
    img = Image.new('RGBA', (width, height), pal['field'])
    cv = Canvas(img)
    g = geometry(w)
    if round_screen:
        cx = (width - bar) // 2 + bar // 4
        shoulders = w // 2
    else:
        cx = (width - bar) // 2
        shoulders = w // 4
    top = height - shoulders - g['neck'] - g['h']
    draw_companion(cv, cx, top, w, pal, height)
    cv.rect(width - bar, 0, bar, height, OXFORD if pal is COLOUR else BLACK)
    if round_screen:
        px = img.load()
        r = width / 2
        for y in range(height):
            for x in range(width):
                if (x + 0.5 - r) ** 2 + (y + 0.5 - r) ** 2 > r * r:
                    px[x, y] = BLACK
    return img


def write(img, *parts):
    path = os.path.join(ROOT, *parts)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.save(path, optimize=True)
    print(os.path.relpath(path, ROOT))


def main(argv):
    what = argv[1] if len(argv) > 1 else ''
    if what == 'icons':
        write(menu_icon(COLOUR), 'resources', 'images_icons_app_icon.png')
        write(store_icon(48), 'docs', 'store', 'icon-48.png')
        write(store_icon(80), 'docs', 'store', 'icon-80.png')
        write(store_icon(144), 'docs', 'store', 'icon-144.png')
    elif what == 'previews':
        # Head widths are the ones talking_squire_layer.c settles on with a
        # one-line greeting and caption (COMPANION_MAX_W, shrunk to fit).
        # Round previews draw the whole frame; the bezel crop is approximate.
        write(label(preview(260, 260, 34, 107, COLOUR, True), 'PREVIEW gabbro'), 'docs', 'identity', 'preview-gabbro.png')
        write(label(preview(200, 228, 34, 95, COLOUR, False), 'PREVIEW emery'), 'docs', 'identity', 'preview-emery.png')
        write(label(preview(144, 168, 30, 63, MONO, False), 'PREVIEW diorite'), 'docs', 'identity', 'preview-diorite.png')
        mono_icon = menu_icon(MONO)
        write(label(mono_icon.resize((100, 100), Image.NEAREST), 'PREVIEW'), 'docs', 'identity', 'preview-menu-icon-mono.png')
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
