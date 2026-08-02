#!/usr/bin/env python3
"""Generate the 4-channel packed wisp sheet used by GPU deform trails.

trail_deform.fs reinterprets ONE RGBA texture as four independent roles:
    R = coarse wisp body    (low-frequency shape)
    G = fine wisp detail    (high-frequency texture, mixed in by u_wispMix)
    B = dissolve noise      (panning field the dissolve smoothstep erodes on)
    A = turbulence          (zero-mean jitter on the coarse<->fine mix)

Why this file exists: the trail system's smoke sheet (smoke_ribbon.png) is
single-channel — R=G=B, edge fade carried only in alpha — so the packed
material read three identical greys, had no internal structure to flow, and
the hard band lost its soft U edges. A packed sheet must bake the edge fade
into EVERY channel, because the packed shader never reads the texture alpha.

Conventions (must match trail_deform.fs):
  * V (texture .y) is the seam direction — analytic periodic noise, seamless.
  * U (texture .x) is the cross-strip direction — edges reach 0 at U=0/1 so
    the wrapped band fades out at its physical edges, never a hard cut.
  * Each channel is a DIFFERENT noise instance (decorrelated R vs G vs B).
  * B is centred on 0.5 with a wide spread; the FS does
    smoothstep(u_dissolve, u_dissolve + edge, B) — chunks erode, not a haze.
  * A is centred on 0.5 with a NARROW spread; the FS does
    (A - 0.5) * 2 * u_turbStrength — a jitter, not a full-range flicker.
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
OUT = ROOT / "assets" / "textures" / "energy_wisp.png"
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
    """Periodic on V (the seam), quasi-random on U; zero-centred, -1..1."""
    value = 0.0
    weight = 0.0
    for m, n, amp, phase in modes:
        value += amp * math.sin(TAU * (m * u + n * v) + phase)
        weight += amp
    return value / weight


def main() -> None:
    rng = random.Random(0xE6E7C0)

    # Decorrelated instances per role: coarse (low), fine (high), mid (erosion),
    # mid-high (turbulence). Same octave ladder each time but different seeds
    # means the four channels read as independent layers, which is the point.
    coarse = make_modes(rng, [1, 2, 4], 6)
    fine = make_modes(rng, [4, 8, 16], 6)
    mid = make_modes(rng, [2, 4, 8], 5)
    turb = make_modes(rng, [4, 8], 5)

    pixels = bytearray(SIZE * SIZE * 4)
    for y in range(SIZE):
        v = y / SIZE
        for x in range(SIZE):
            u = x / SIZE
            # Edge fade baked into EVERY channel: at U=0/1 the band must be
            # transparent in the packed path, which never reads texture alpha.
            edge = math.sin(math.pi * u)
            edge = max(0.0, edge) ** 0.72

            # R — coarse wisp: big billows, shaped for a soft body. Brightened
            # so the additive glow is clearly visible (R rides the alpha as
            # alpha = wisp * mask * vColor.a — a dim sheet makes a dim trail).
            c = 0.5 + 0.5 * field(u, v, coarse)
            r = edge * (0.32 + 0.68 * smoothstep(0.20, 0.85, c))

            # G — fine wisp: high-frequency detail, mixed in by u_wispMix.
            f = 0.5 + 0.5 * field(u, v, fine)
            g = edge * (0.28 + 0.72 * smoothstep(0.25, 0.88, f))

            # B — dissolve noise: centred near 0.5 with a WIDE spread, so with
            # a modest threshold most of the sheet survives and only low B
            # regions erode (a B mean of ~0.35 made the whole band vanish).
            m = 0.5 + 0.5 * field(u, v, mid)
            b = edge * (0.5 + 0.5 * smoothstep(0.10, 0.90, m))

            # A — turbulence: moderate spread around 0.5, so (A-0.5)*2*strength
            # ripples the coarse<->fine mix without slamming the clamp.
            t = 0.5 + 0.35 * field(u, v, turb)
            a = edge * max(0.0, min(1.0, t))

            i = (y * SIZE + x) * 4
            pixels[i:i + 4] = bytes((round(r * 255.0), round(g * 255.0),
                                     round(b * 255.0), 255))

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
    print(f"{OUT}: {SIZE}x{SIZE} RGBA8 (R coarse, G fine, B dissolve, A turbulence)")


if __name__ == "__main__":
    main()
