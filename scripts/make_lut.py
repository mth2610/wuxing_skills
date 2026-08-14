#!/usr/bin/env python3
"""Generate 16^3 strip LUTs for core/color_grade_lut.c.

Layout MUST match core/color_grade_lut.h: 256x16, tiles left-to-right by blue
slice, +U red, +V green, image row 0 = green 0. Get the green direction wrong
and the result is not a mirrored image — it is an inexplicable colour cast, so
this file is the one place the convention is implemented for authoring.

  python3 scripts/make_lut.py neutral   > writes assets/luts/neutral.png
  python3 scripts/make_lut.py moonlight > writes assets/luts/moonlight.png

To make a grade live, copy it to assets/luts/grade.png (the path the engine
adopts automatically) or edit a look below. No rebuild needed — it is an asset.

Depends only on the stdlib: writes PNG by hand rather than requiring Pillow,
because a build-adjacent script that needs a pip install is a script that stops
being run.
"""
import sys, os, zlib, struct

SIZE = 16
WIDTH, HEIGHT = SIZE * SIZE, SIZE


def clamp01(x):
    return 0.0 if x < 0.0 else (1.0 if x > 1.0 else x)


def luma(r, g, b):
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def neutral(r, g, b):
    return r, g, b


def moonlight(r, g, b):
    """Wuxing night look: cool shadows, warm highlights, restrained mids.

    Tuned to REINFORCE the dark arena rather than lift it — the shadow end is
    tinted, not raised. Lifting blacks is the usual reflex for "cinematic" and
    it is exactly what would destroy this art direction.
    """
    l = luma(r, g, b)

    # Split-tone: cool the darks toward moonlit blue, warm the speculars.
    shadow = (0.86, 0.94, 1.16)
    highlight = (1.12, 1.03, 0.88)
    t = l * l * (3.0 - 2.0 * l)  # smoothstep — keeps the mids from muddying
    tint = [shadow[i] + (highlight[i] - shadow[i]) * t for i in range(3)]
    r, g, b = r * tint[0], g * tint[1], b * tint[2]

    # Gentle S-curve for contrast. Applied per channel AFTER the tint so the
    # tint cannot be crushed back out by the curve.
    def s_curve(x):
        x = clamp01(x)
        return x * x * (3.0 - 2.0 * x) * 0.30 + x * 0.70

    r, g, b = s_curve(r), s_curve(g), s_curve(b)

    # Pull saturation out of the greens only: foliage and ground read as noisy
    # colour at night, while the elemental VFX must keep their identity.
    l2 = luma(r, g, b)
    greenness = clamp01(g - max(r, b))
    g = g + (l2 - g) * greenness * 0.55

    return clamp01(r), clamp01(g), clamp01(b)


LOOKS = {"neutral": neutral, "moonlight": moonlight}


def build(look):
    rows = []
    denom = float(SIZE - 1)
    for gy in range(HEIGHT):          # row 0 = green 0
        row = bytearray()
        for x in range(WIDTH):
            sl, rx = divmod(x, SIZE)
            r, g, b = look(rx / denom, gy / denom, sl / denom)
            row += bytes((round(r * 255), round(g * 255), round(b * 255)))
        rows.append(bytes(row))
    return rows


def write_png(path, rows):
    raw = b"".join(b"\x00" + r for r in rows)   # filter type 0 per scanline

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9))
           + chunk(b"IEND", b""))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(png)


def main():
    name = sys.argv[1] if len(sys.argv) > 1 else "moonlight"
    if name not in LOOKS:
        sys.exit("unknown look %r — have: %s" % (name, ", ".join(sorted(LOOKS))))
    out = os.path.join("assets", "luts", name + ".png")
    write_png(out, build(LOOKS[name]))
    print("wrote %s (%dx%d, %d^3 strip)" % (out, WIDTH, HEIGHT, SIZE))
    if name != "neutral":
        print("to activate:  cp %s assets/luts/grade.png" % out)


if __name__ == "__main__":
    main()
