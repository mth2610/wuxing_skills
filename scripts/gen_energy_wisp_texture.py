#!/usr/bin/env python3
"""Generate the 4-channel packed STRAND sheet used by GPU deform trails.

Channel layout follows the RzFX "Sin Wave Trail" breakdown
(ryanzengvfx.blogspot.com, 2019-04), which trail_deform.fs mode 2 implements:

    R = trail pattern 1   (coarse filaments — the readable strands)
    G = trail pattern 2   (finer, denser filaments — detail layer)
    B = distortion noise  (smooth flow field; two offset samples warp the UV)
    A = dissolve noise    (mid-frequency; erodes hardest toward the tail)

WHY FILAMENTS, NOT CLOUDS. The previous sheet was isotropic cloud noise in
every channel. Cloud noise can only ever modulate the BRIGHTNESS of a band —
it cannot split one band into many, so the trail rendered as a smooth glowing
ribbon with a clean edge and a clean tail, which is exactly the "texture chưa
đúng, biên và đuôi liền mạch" complaint. The strand structure has to exist in
the asset: a hair is a narrow ridge in U that persists for a long stretch of V,
and no amount of shader work conjures that out of blobs.

SHAPE LIVES IN THE SHEET, ON PURPOSE. R and G carry a soft density falloff
toward U=0 and U=1, so the bundle thins into separate hairs at its edges
instead of ending at a drawn boundary. Mode 2 therefore has NO analytic band —
it samples this sheet three times at three wave offsets and lets the overlap be
the silhouette. (Mode 1, the older packed-wisp path, wanted the opposite: an
edge fade baked into every channel because it never reads texture alpha. The
two conventions cannot share a sheet; mode 1 has no consumers left.)

SEAMLESS IN BOTH AXES. V is panned and wrapped every frame, and U wraps when a
wave crest pushes a sample past the strip edge, so every field here is built
from terms periodic in both — hair centres wander on sums of sines in V, and
their distance to a texel is measured on the CIRCLE in U.

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
    """Periodic in BOTH axes; zero-centred, -1..1."""
    value = 0.0
    weight = 0.0
    for m, n, amp, phase in modes:
        value += amp * math.sin(TAU * (m * u + n * v) + phase)
        weight += amp
    return value / weight


class Hair:
    """One filament: a narrow ridge in U whose centre wanders slowly along V.

    `wander` is a sum of sines with INTEGER frequencies in v, so the hair
    returns to where it started at v=1 and the sheet tiles along the path. The
    lengthwise `breaks` term thins the hair in places, so a bundle reads as
    overlapping broken fibres rather than as ruled lines.
    """

    __slots__ = ("u0", "width", "gain", "wander", "breaks")

    def __init__(self, rng: random.Random, width: float, wander_amp: float):
        self.u0 = rng.random()
        self.width = width * rng.uniform(0.55, 1.7)
        self.gain = rng.uniform(0.55, 1.0)
        self.wander = [
            (rng.randint(1, 2), wander_amp * rng.uniform(0.6, 1.0), rng.random() * TAU),
            (rng.randint(2, 5), wander_amp * rng.uniform(0.2, 0.5), rng.random() * TAU),
        ]
        self.breaks = [
            (rng.randint(1, 3), rng.random() * TAU),
            (rng.randint(4, 9), rng.random() * TAU),
        ]

    def centre(self, v: float) -> float:
        c = self.u0
        for n, amp, ph in self.wander:
            c += amp * math.sin(TAU * n * v + ph)
        return c

    def intensity(self, v: float) -> float:
        # 0..1, dipping to near zero a few times along the length so the hair
        # breaks into segments instead of running the whole sheet unbroken.
        a = math.sin(TAU * self.breaks[0][0] * v + self.breaks[0][1])
        b = math.sin(TAU * self.breaks[1][0] * v + self.breaks[1][1])
        return self.gain * smoothstep(-0.75, 0.55, 0.65 * a + 0.35 * b)


def hair_field(u: float, v: float, hairs: list[Hair]) -> float:
    """Brightest hair wins — hairs OVERLAP, they do not sum into a solid band.

    Summing was the first attempt and it filled the gaps between neighbours
    back in, which is the whole failure being fixed here: the gaps ARE the
    effect. `max` keeps a bright hair bright and leaves the space beside it
    empty.
    """
    best = 0.0
    for h in hairs:
        c = h.centre(v)
        # Distance on the circle: a hair whose centre has wandered past U=1
        # must still light texels near U=0, or the wrap shows as a seam.
        d = abs(u - c)
        d = min(d, 1.0 - d) if d < 1.0 else d % 1.0
        d = min(d, 1.0 - d)
        if d > h.width * 3.0:
            continue
        val = math.exp(-(d / h.width) ** 2) * h.intensity(v)
        if val > best:
            best = val
    return best


def main() -> None:
    rng = random.Random(0xE6E7C0)

    # R: readable strands — few, wide, lazy wander. This is the layer the eye
    # actually resolves as "the trail is made of threads".
    coarse_hairs = [Hair(rng, 0.016, 0.075) for _ in range(34)]
    # G: detail — many, thin, livelier wander. Mixed in at partial weight.
    fine_hairs = [Hair(rng, 0.0065, 0.11) for _ in range(96)]

    flow = make_modes(rng, [1, 2, 4], 6)   # B — smooth, low frequency
    dissolve = make_modes(rng, [2, 4, 8], 5)  # A — mid frequency

    pixels = bytearray(SIZE * SIZE * 4)
    for y in range(SIZE):
        v = y / SIZE
        coarse_row = [(h.centre(v), h.intensity(v), h.width) for h in coarse_hairs]
        fine_row = [(h.centre(v), h.intensity(v), h.width) for h in fine_hairs]
        for x in range(SIZE):
            u = x / SIZE

            # Bundle density: the hair field is modulated so the middle is
            # dense and the two edges thin out into stragglers. This is the
            # silhouette — mode 2 draws no band of its own.
            edge = math.sin(math.pi * u)
            density = max(0.0, edge) ** 1.35

            best_c = 0.0
            for c, inten, w in coarse_row:
                d = abs(u - c) % 1.0
                d = min(d, 1.0 - d)
                if d > w * 3.0:
                    continue
                val = math.exp(-(d / w) ** 2) * inten
                if val > best_c:
                    best_c = val

            best_f = 0.0
            for c, inten, w in fine_row:
                d = abs(u - c) % 1.0
                d = min(d, 1.0 - d)
                if d > w * 3.0:
                    continue
                val = math.exp(-(d / w) ** 2) * inten
                if val > best_f:
                    best_f = val

            r = best_c * density
            g = best_f * density

            # B — flow/distortion noise. Smooth and zero-centred: the shader
            # reads (B - 0.5) twice at different pan offsets and adds the pair
            # to the across coordinate, so a biased mean would push the whole
            # trail sideways instead of wobbling it.
            b = 0.5 + 0.5 * field(u, v, flow)

            # A — dissolve noise. Must span the FULL 0..1 range: the shader
            # sweeps a threshold across it, so a channel confined to the top
            # half (the `0.5 + 0.5 * smoothstep(...)` this replaced) can only
            # ever be eroded by a threshold above 0.5 and reads as "the
            # dissolve does nothing" for every reasonable setting.
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
          f"(R strands, G fine strands, B flow, A dissolve)")


if __name__ == "__main__":
    main()
