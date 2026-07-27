#!/usr/bin/env python3
"""Generate the RuneCircle (E5.2) texture set — no font, no AI, no hand-drawing.

    python3 scripts/gen_rune_textures.py

Writes into assets/textures/:
    rune_line.png        the band cross-section for the plain rings
    rune_glyphs_0..3.png four strips of runic glyphs, one per ring

WHY PROCEDURAL RATHER THAN A FONT OR AN IMAGE MODEL
    - A font would have to exist on every machine that regenerates these, and
      Unicode's Runic block is not installed by default on macOS.
    - An image model cannot produce what this actually needs: a strip that tiles
      SEAMLESSLY end-to-end (the strip wraps around a circle, so the join is
      on screen at all times), an exactly uniform glyph height, and a clean
      alpha channel. Models give none of the three reliably.
    Drawing the glyphs as strokes gives all of it by construction, and the seam
    is guaranteed because the glyph pitch divides the width exactly.

OUTPUT CONVENTION (matches what the engine samples)
    RGB is solid white and the artwork lives in ALPHA, so one file serves every
    element and the colour comes from VFX_Material at the call site.
    Straight alpha, not premultiplied.

    The files are written TRANSPOSED: a ribbon's `u` runs across the band's
    WIDTH and its `v` runs along the LENGTH, so image X = band width and
    image Y = circumference. The glyphs are drawn upright and rotated on save;
    open a file and you will see the writing running down the image.
"""

import math
import os
import random

from PIL import Image, ImageDraw, ImageFont

OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "assets", "textures")

# Periodic, seam-safe wobble: a sum of sines whose frequencies are INTEGER
# cycles over the strip, so f(0) == f(1) exactly. A noise hash would not close.
def wobble(t01, seed, freqs=(3, 7, 13, 23), amps=(0.5, 0.28, 0.14, 0.08)):
    v = 0.0
    for i, (f, a) in enumerate(zip(freqs, amps)):
        v += a * math.sin(2.0 * math.pi * f * t01 + seed * (1.7 + 0.9 * i))
    return v  # roughly -1..1


# ── rune_line.png ────────────────────────────────────────────────────────────
def gen_line(width=64, height=1024, path="rune_line.png"):
    """The plain ring's cross-section.

    Not a clean gradient: a perfectly even band reads as a printed circle, not
    as a channel of energy. The filament's brightness, thickness and centre all
    wander along the ring's length, and a few spots thin out almost to nothing.
    All of it is periodic so the ring closes without a seam.
    """
    img = Image.new("RGBA", (width, height))
    px = img.load()
    for y in range(height):
        t = (y + 0.5) / height
        bright = 0.78 + 0.22 * (0.5 + 0.5 * wobble(t, 1.0))
        # NO near-breaks. An earlier version dimmed the band almost to zero in
        # places to suggest uneven energy; on the small inner rings, whose whole
        # circumference is only a few hundred pixels, that reads as the ring
        # being BROKEN rather than as flowing energy. The wobble above is the
        # whole variation now — it modulates, it never interrupts.
        widthMul = 0.75 + 0.5 * (0.5 + 0.5 * wobble(t, 4.3, (2, 5, 11), (0.6, 0.3, 0.1)))
        centre = 0.14 * wobble(t, 9.1, (2, 6), (0.7, 0.3))

        for x in range(width):
            u = (x + 0.5) / width * 2.0 - 1.0
            d = (u - centre) / max(0.15, widthMul)
            core = math.exp(-(d * d) * 26.0)
            halo = math.exp(-(d * d) * 3.2) * 0.42
            a = min(1.0, (core + halo) * bright)
            # Hard zero at the very edge: a band that never reaches 0 draws a
            # visible hairline along its own border.
            if abs(u) > 0.985:
                a = 0.0
            px[x, y] = (255, 255, 255, int(a * 255))
    img.save(os.path.join(OUT_DIR, path))
    return path, img.size


# ── rune_glyphs_N.png ────────────────────────────────────────────────────────
# Real typefaces, checked for actual coverage rather than assumed. The first
# version drew glyphs as strokes because "macOS has no runic font"; that was
# wrong — probing every system font against the tofu glyph finds Apple Symbols
# carrying BOTH the Runic block and, more usefully for this project, all 64
# I Ching hexagrams. Hand-drawn strokes cannot compete with a designed typeface,
# which is what the owner said when they saw them.
#
# The alphabets are chosen for the game's own cosmology first: hexagrams and Han
# characters (heavenly stems, earthly branches, the five elements, the eight
# trigrams) before anything European.
FONT_CANDIDATES = {
    "hexagram": ["/System/Library/Fonts/Apple Symbols.ttf"],
    "runic":    ["/System/Library/Fonts/Apple Symbols.ttf", "/System/Library/Fonts/Geneva.ttf"],
    "han":      ["/System/Library/Fonts/Supplemental/Songti.ttc",
                 "/System/Library/Fonts/STHeiti Medium.ttc",
                 "/System/Library/Fonts/Hiragino Sans GB.ttc"],
    "tifinagh": ["/System/Library/Fonts/Supplemental/NotoSansTifinagh-Regular.ttf",
                 "/Library/Fonts/NotoSansTifinagh-Regular.ttf"],
}

ALPHABETS = {
    # 64 hexagrams — the project's own symbol set (Kinh Dịch).
    "hexagram": "".join(chr(c) for c in range(0x4DC0, 0x4E00)),
    "runic":    "".join(chr(c) for c in range(0x16A0, 0x16F1)),
    # Heavenly stems + earthly branches + five elements + eight trigrams.
    "han":      "甲乙丙丁戊己庚辛壬癸子丑寅卯辰巳午未申酉戌亥金木水火土乾坤震巽坎離艮兌",
    "tifinagh": "".join(chr(c) for c in range(0x2D30, 0x2D68)),
}


def _has_glyph(font_path, ch, size=48):
    """Coverage test that actually works: a missing glyph renders as the SAME
    tofu box for every codepoint, so compare against U+FFFF rather than just
    asking whether any ink landed."""
    try:
        f = ImageFont.truetype(font_path, size)
    except Exception:
        return False
    def bits(c):
        im = Image.new("L", (size * 2, size * 2), 0)
        ImageDraw.Draw(im).text((4, 4), c, font=f, fill=255)
        return list(im.getdata())
    a = bits(ch)
    return sum(a) > 0 and a != bits("\uffff")


def pick_font(style, size):
    for path in FONT_CANDIDATES[style]:
        if os.path.exists(path) and _has_glyph(path, ALPHABETS[style][0]):
            try:
                return ImageFont.truetype(path, size), path
            except Exception:
                continue
    return None, None


def gen_glyph_strip(index, style, length=4096, band=128, glyphs=44, seed=None):
    """One ring's worth of writing.

    Drawn upright at `length` x `band`, then rotated so the file matches the
    ribbon's UV layout (see the module docstring). `glyphs` divides `length`
    evenly, which is what makes the strip tile with no seam.
    """
    rng = random.Random(1000 + index if seed is None else seed)
    img = Image.new("L", (length, band), 0)
    d = ImageDraw.Draw(img)

    # Two constraints fight here: the glyph must be tall enough to read across
    # the band, and narrow enough that 44 of them fit around the ring without
    # colliding. The strip's 32:1 aspect is what lets both hold — at 8:1 a Han
    # character was three times wider than its own pitch and the glyphs piled
    # into each other (which is what the first font pass looked like).
    pitch_limit = (length / glyphs) * 0.92
    size = int(min(band * 0.78, pitch_limit))
    font, path = pick_font(style, size)
    if font is None:
        raise SystemExit("No system font covers '%s'. Edit FONT_CANDIDATES." % style)

    alphabet = ALPHABETS[style]
    pitch = length / glyphs
    for i in range(glyphs):
        ch = alphabet[rng.randrange(len(alphabet))]
        cx = (i + 0.5) * pitch
        # anchor="mm" keeps every glyph on one baseline and one centre line,
        # which is what stops the ring from looking ragged.
        d.text((cx, band * 0.5), ch, font=font, fill=255, anchor="mm")

    # Energy texture: the same periodic wobble the plain ring uses, so glyph
    # rings and plain rings look like the same material.
    px = img.load()
    for x in range(length):
        t = (x + 0.5) / length
        m = 0.62 + 0.38 * (0.5 + 0.5 * wobble(t, 2.0 + index))
        for y in range(band):
            v = px[x, y]
            if v:
                px[x, y] = int(v * m)

    rgba = Image.merge("RGBA", (Image.new("L", img.size, 255),
                                Image.new("L", img.size, 255),
                                Image.new("L", img.size, 255),
                                img))
    rgba = rgba.transpose(Image.ROTATE_90)
    name = "rune_glyphs_%d.png" % index
    rgba.save(os.path.join(OUT_DIR, name))
    return name, rgba.size, os.path.basename(path)


def seam_check(path):
    """The strip wraps a circle, so its join is always on screen. Verify it."""
    img = Image.open(os.path.join(OUT_DIR, path)).convert("RGBA")
    a = img.split()[3]
    w, h = img.size
    first = [a.getpixel((x, 0)) for x in range(w)]
    last = [a.getpixel((x, h - 1)) for x in range(w)]
    diff = sum(abs(f - l) for f, l in zip(first, last)) / max(1, w)
    return diff


if __name__ == "__main__":
    os.makedirs(OUT_DIR, exist_ok=True)
    name, size = gen_line()
    print("wrote %-20s %s  seam delta %.1f/255" % (name, size, seam_check(name)))
    for i, style in enumerate(["hexagram", "han", "runic", "tifinagh"]):
        name, size, font = gen_glyph_strip(i, style)
        print("wrote %-20s %s  %-9s %-28s seam %.1f/255"
              % (name, size, style, font, seam_check(name)))
