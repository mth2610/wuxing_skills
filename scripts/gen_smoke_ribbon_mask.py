#!/usr/bin/env python3
"""Generate a seamless scalar erosion mask for the smoke-ribbon shader.

The main sheet owns the visible smoke shape; this map only decides where that
shape thins or tears.  Low-frequency cells keep puffs readable and smaller
periodic detail prevents the flow map from looking like a repeated warp.
Only the Python standard library is required.
"""

from __future__ import annotations

import math
import random
import struct
import zlib
from pathlib import Path


SIZE = 256
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "assets" / "textures" / "smoke_ribbon_mask.png"
TAU = math.tau


def smoothstep(edge0: float, edge1: float, value: float) -> float:
    t = max(0.0, min(1.0, (value - edge0) / (edge1 - edge0)))
    return t * t * (3.0 - 2.0 * t)


def make_modes() -> list[tuple[int, int, float, float]]:
    rng = random.Random(0x5A0C0)
    modes: list[tuple[int, int, float, float]] = []
    for frequency, count, amplitude in ((1, 4, 1.0), (2, 5, 0.54), (4, 6, 0.22)):
        for _ in range(count):
            u_freq = rng.randint(1, frequency)
            v_freq = rng.randint(1, frequency)
            if rng.random() < 0.5:
                u_freq = -u_freq
            if rng.random() < 0.5:
                v_freq = -v_freq
            modes.append((u_freq, v_freq, amplitude, rng.random() * TAU))
    return modes


def mask_value(u: float, v: float, modes: list[tuple[int, int, float, float]]) -> float:
    total = 0.0
    weight = 0.0
    for u_freq, v_freq, amplitude, phase in modes:
        total += amplitude * math.sin(TAU * (u_freq * u + v_freq * v) + phase)
        weight += amplitude
    base = 0.5 + 0.5 * total / weight
    # Bias toward broad retained regions with soft, eroded boundaries.
    return smoothstep(0.20, 0.82, base)


def write_png(path: Path, pixels: bytes) -> None:
    raw = bytearray(SIZE * (1 + SIZE * 4))
    for y in range(SIZE):
        dst = y * (1 + SIZE * 4)
        src = y * SIZE * 4
        raw[dst + 1:dst + 1 + SIZE * 4] = pixels[src:src + SIZE * 4]

    def chunk(kind: bytes, payload: bytes) -> bytes:
        joined = kind + payload
        return (struct.pack(">I", len(payload)) + kind + payload +
                struct.pack(">I", zlib.crc32(joined) & 0xffffffff))

    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(bytes(raw), 9)) +
           chunk(b"IEND", b""))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def main() -> None:
    modes = make_modes()
    pixels = bytearray(SIZE * SIZE * 4)
    for y in range(SIZE):
        v = y / SIZE
        for x in range(SIZE):
            value = round(mask_value(x / SIZE, v, modes) * 255.0)
            i = (y * SIZE + x) * 4
            pixels[i:i + 4] = bytes((value, value, value, 255))
    write_png(OUT, bytes(pixels))
    print(f"{OUT}: {SIZE}x{SIZE} RGBA8 scalar mask")


if __name__ == "__main__":
    main()
