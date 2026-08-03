#!/usr/bin/env python3
"""Generate the 4-channel packed sheet for the SMOKE strand-trail style.

Channel contract (trail_deform.fs mode 2):
    R = trail pattern 1   (a whole smoke wisp, head-to-tail)
    G = trail pattern 2   (a second, different wisp)
    B = distortion noise  (smooth flow field, tiles + pans)
    A = dissolve noise    (finer, higher contrast, tiles + pans)

READ THIS BEFORE CHANGING ANYTHING HERE — R/G ARE NOT A REPEATING PATTERN.

The reference sheet (ryanzengvfx.blogspot.com, 2019-04) shows R and G as ONE
complete smoke streak each: a soft band that is thick through the middle and
thins to nothing at BOTH ends, with a few curls rolling through it. The Chinese
label is 拖尾紋理 — "trail texture", singular. Its U axis maps ONCE across the
whole length of the trail. It is the trail's SHAPE, not a tileable material.

The first version of this file made hair-like filaments and the shader tiled
them by metres of path. That can only ever produce a rope: a bundle of strands
of uniform density, with no head, no tail and no silhouette of its own. Three
sin-offset samples of a rope are three ropes. Three sin-offset samples of a
WISP are overlapping smoke.

So, in this sheet:
  * The wisp runs along +V (the shader samples y = the along-trail coordinate,
    x = across), i.e. the reference image rotated 90 degrees. Same content.
  * It fades to zero at V=0 and V=1 — that is the trail's own head and tail
    taper, baked in, and the reason the sheet must NOT tile along V.
  * It fades to zero at U=0 and U=1 so a wave crest that pushes the sample
    sideways runs out of wisp instead of hitting an edge.
  * Several offset sub-wisps of differing width and brightness give the
    internal curl structure; a single Gaussian band reads as a painted stripe.

B and A are unchanged in kind: seamless in both axes, because those two ARE
panned and wrapped every frame.

Uses only the Python standard library.
"""

from __future__ import annotations

import math
import random
import struct
import zlib
from pathlib import Path

SIZE = 512
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "assets" / "textures" / "smoke_strand.png"
TAU = math.tau


def smoothstep(edge0: float, edge1: float, value: float) -> float:
    t = max(0.0, min(1.0, (value - edge0) / (edge1 - edge0)))
    return t * t * (3.0 - 2.0 * t)


def make_modes(rng: random.Random, octaves: list[int], count: int) -> list[tuple[int, int, float, float]]:
    modes: list[tuple[int, int, float, float]] = []
    for octave in octaves:
        for _ in range(count):
            m = rng.randint(1, octave + 1)
            n = rng.randint(1, max(1, octave // 2))
            if rng.random() < 0.5:
                m = -m
            if rng.random() < 0.5:
                n = -n
            modes.append((m, n, 1.0 / octave, rng.random() * TAU))
    return modes


def field(u: float, v: float, modes: list[tuple[int, int, float, float]]) -> float:
    """Periodic in BOTH axes; zero-centred, -1..1. For B and A only."""
    value = 0.0
    weight = 0.0
    for m, n, amp, phase in modes:
        value += amp * math.sin(TAU * (m * u + n * v) + phase)
        weight += amp
    return value / weight


class SubWisp:
    """One curl inside the streak: a soft band whose centre and thickness both
    drift along V. Several of these stacked give the rolling internal structure
    the reference sheet has; one alone is a painted stripe."""

    __slots__ = ("offset", "width", "gain", "drift", "swell")

    def __init__(self, rng: random.Random, width: float, offset_spread: float):
        self.offset = rng.uniform(-offset_spread, offset_spread)
        self.width = width * rng.uniform(0.7, 1.45)
        self.gain = rng.uniform(0.45, 1.0)
        # Non-integer frequencies are fine here: this axis does NOT tile.
        self.drift = [
            (rng.uniform(0.6, 1.6), rng.uniform(0.05, 0.13), rng.random() * TAU),
            (rng.uniform(2.0, 4.0), rng.uniform(0.02, 0.055), rng.random() * TAU),
        ]
        self.swell = [
            (rng.uniform(0.8, 2.2), rng.uniform(0.25, 0.55), rng.random() * TAU),
            (rng.uniform(3.0, 6.0), rng.uniform(0.1, 0.3), rng.random() * TAU),
        ]

    def centre(self, v: float) -> float:
        c = 0.5 + self.offset
        for f, amp, ph in self.drift:
            c += amp * math.sin(TAU * f * v + ph)
        return c

    def half_width(self, v: float) -> float:
        s = 1.0
        for f, amp, ph in self.swell:
            s += amp * math.sin(TAU * f * v + ph)
        return self.width * max(0.15, s)


def wisp_channel(u: float, v: float, subs: list[SubWisp], taper: float) -> float:
    """Brightest sub-wisp wins, then the whole streak is tapered at both ends.

    Max rather than sum for the same reason as everywhere else in this effect:
    summing fills the gaps between the curls and flattens the streak into one
    solid slab, which is exactly the look being avoided.
    """
    # Head/tail taper — the trail's own silhouette, baked into the sheet. This
    # is what makes the sheet a TRAIL and not a material, and why it must be
    # sampled once across the whole length instead of tiled.
    ends = smoothstep(0.0, taper, v) * smoothstep(0.0, taper, 1.0 - v)
    if ends <= 0.0:
        return 0.0

    best = 0.0
    for sw in subs:
        c = sw.centre(v)
        hw = sw.half_width(v)
        d = abs(u - c) / hw
        if d > 3.0:
            continue
        val = math.exp(-d * d) * sw.gain
        if val > best:
            best = val

    # Run out of wisp before the sheet edge, so a wave crest that pushes the
    # sample sideways finds nothing rather than a hard cut.
    sides = smoothstep(0.0, 0.10, u) * smoothstep(0.0, 0.10, 1.0 - u)
    return best * ends * sides


def main() -> None:
    rng = random.Random(0x5A0CE7)

    # R: the readable streak — few, broad curls.
    coarse = [SubWisp(rng, 0.085, 0.10) for _ in range(4)]
    # G: a DIFFERENT streak, narrower and busier. Sampled at its own wave offset
    # by the shader, so it must not be a copy of R or the two lock together.
    fine = [SubWisp(rng, 0.055, 0.14) for _ in range(6)]

    flow = make_modes(rng, [1, 2, 4], 6)
    dissolve = make_modes(rng, [2, 4, 8], 5)

    pixels = bytearray(SIZE * SIZE * 4)
    for y in range(SIZE):
        v = y / SIZE
        coarse_row = [(sw.centre(v), sw.half_width(v), sw.gain) for sw in coarse]
        fine_row = [(sw.centre(v), sw.half_width(v), sw.gain) for sw in fine]
        ends_c = smoothstep(0.0, 0.30, v) * smoothstep(0.0, 0.30, 1.0 - v)
        ends_f = smoothstep(0.0, 0.22, v) * smoothstep(0.0, 0.22, 1.0 - v)
        for x in range(SIZE):
            u = x / SIZE
            sides = smoothstep(0.0, 0.10, u) * smoothstep(0.0, 0.10, 1.0 - u)

            best = 0.0
            for c, hw, gain in coarse_row:
                d = abs(u - c) / hw
                if d > 3.0:
                    continue
                val = math.exp(-d * d) * gain
                if val > best:
                    best = val
            r = best * ends_c * sides

            best = 0.0
            for c, hw, gain in fine_row:
                d = abs(u - c) / hw
                if d > 3.0:
                    continue
                val = math.exp(-d * d) * gain
                if val > best:
                    best = val
            g = best * ends_f * sides

            b = 0.5 + 0.5 * field(u, v, flow)
            a = smoothstep(-0.85, 0.85, field(u, v, dissolve))

            i = (y * SIZE + x) * 4
            pixels[i:i + 4] = bytes((
                round(max(0.0, min(1.0, r)) * 255.0),
                round(max(0.0, min(1.0, g)) * 255.0),
                round(max(0.0, min(1.0, b)) * 255.0),
                round(max(0.0, min(1.0, a)) * 255.0)))

    raw = bytearray(SIZE * (1 + SIZE * 4))
    for y in range(SIZE):
        dst = y * (1 + SIZE * 4)
        src = y * SIZE * 4
        raw[dst + 1:dst + 1 + SIZE * 4] = pixels[src:src + SIZE * 4]

    def chunk(kind: bytes, payload: bytes) -> bytes:
        joined = kind + payload
        return (struct.pack(">I", len(payload)) + joined +
                struct.pack(">I", zlib.crc32(joined) & 0xffffffff))

    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(png)
    print(f"{OUT}: {SIZE}x{SIZE} RGBA8 "
          f"(R wisp/{len(coarse)} curls, G wisp/{len(fine)} curls, B flow, A dissolve)")


if __name__ == "__main__":
    main()
