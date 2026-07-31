#!/usr/bin/env python3
"""Generate the seamless-on-V, soft-edged smoke sheet used by ribbon trails.

Unlike a volume sheet, a ribbon sheet deliberately reaches alpha 0 at U=0/1;
those are the two physical edges of the band. V is analytic periodic noise, so
the texture can repeat and scroll down an arbitrarily long trail without a seam.
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
OUT = ROOT / "assets" / "textures" / "smoke_ribbon.png"
TAU = math.tau


def smoothstep(edge0: float, edge1: float, value: float) -> float:
    t = max(0.0, min(1.0, (value - edge0) / (edge1 - edge0)))
    return t * t * (3.0 - 2.0 * t)


def make_modes() -> list[tuple[int, int, float, float]]:
    rng = random.Random(0x5A0CE)
    modes: list[tuple[int, int, float, float]] = []
    # More variation across U than V produces stretched puffs, not marble noise.
    for octave, count in ((1, 5), (2, 6), (4, 5), (8, 4)):
        for _ in range(count):
            m = rng.randint(1, octave + 1)
            n = rng.randint(1, max(1, octave // 2))
            if rng.random() < 0.5:
                m = -m
            if rng.random() < 0.5:
                n = -n
            modes.append((m, n, 1.0 / octave, rng.random() * TAU))
    return modes


def density(u: float, v: float, modes: list[tuple[int, int, float, float]]) -> float:
    value = 0.0
    weight = 0.0
    for m, n, amp, phase in modes:
        value += amp * math.sin(TAU * (m * u + n * v) + phase)
        weight += amp
    # A second slow roll makes coherent packets along the trail direction.
    roll = 0.5 + 0.5 * math.sin(TAU * (3.0 * v + 0.45 * math.sin(TAU * u)))
    return 0.5 + 0.5 * (value / weight) * 0.72 + (roll - 0.5) * 0.28


def packet_mask(u: float, v: float) -> float:
    """Large, intermittent billows along V; periodic at the scroll seam."""
    phase = 2.2 * v + 0.18 * math.sin(TAU * (u * 1.5 + v))
    packet = 0.5 + 0.5 * math.sin(TAU * phase)
    return smoothstep(0.30, 0.73, packet)


def write_png(path: Path, pixels: bytes, size: int) -> None:
    raw = bytearray(size * (1 + size * 4))
    for y in range(size):
        dst = y * (1 + size * 4)
        src = y * size * 4
        raw[dst + 1:dst + 1 + size * 4] = pixels[src:src + size * 4]

    def chunk(kind: bytes, payload: bytes) -> bytes:
        joined = kind + payload
        return (struct.pack(">I", len(payload)) + joined +
                struct.pack(">I", zlib.crc32(joined) & 0xffffffff))

    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def main() -> None:
    modes = make_modes()
    pixels = bytearray(SIZE * SIZE * 4)
    for y in range(SIZE):
        v = y / SIZE
        for x in range(SIZE):
            u = x / SIZE
            edge = math.sin(math.pi * u)
            edge = max(0.0, edge) ** 0.72
            smoke = density(u, v, modes)
            wisps = smoothstep(0.43, 0.72, smoke)
            alpha = edge * wisps * packet_mask(u, v) * (0.42 + 0.58 * smoke)
            # Dark detail gives alpha-blended smoke a body without creating an
            # additive-looking glowing ribbon when it uses an elemental tint.
            grey = round(74.0 + 104.0 * smoke)
            i = (y * SIZE + x) * 4
            pixels[i:i + 4] = bytes((grey, grey, grey, round(alpha * 255.0)))
    write_png(OUT, bytes(pixels), SIZE)
    print(f"{OUT}: {SIZE}x{SIZE} RGBA8")


if __name__ == "__main__":
    main()
