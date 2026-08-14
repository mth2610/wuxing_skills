#!/usr/bin/env python3
"""Generate the neutral RGBA sprite used by the particle-system fallback."""

from pathlib import Path
import math
import struct
import zlib

SIZE = 256
OUT = Path(__file__).resolve().parents[1] / "assets/textures/particles/default_particle_sprite.png"


def clamp01(value: float) -> float:
    return max(0.0, min(1.0, value))


def smoothstep(edge0: float, edge1: float, value: float) -> float:
    t = clamp01((value - edge0) / (edge1 - edge0))
    return t * t * (3.0 - 2.0 * t)


def chunk(kind: bytes, payload: bytes) -> bytes:
    return (struct.pack(">I", len(payload)) + kind + payload +
            struct.pack(">I", zlib.crc32(kind + payload) & 0xffffffff))


def main() -> None:
    pixels = bytearray()
    half = SIZE * 0.5
    for y in range(SIZE):
        pixels.append(0)  # PNG filter type: None
        py = ((y + 0.5) - half) / half
        for x in range(SIZE):
            px = ((x + 0.5) - half) / half
            radius = math.sqrt(px * px + py * py)
            # One neutral soft spark: compact opaque centre, with only a faint
            # low-alpha halo.  The edge has no baked dark RGB, so tint decides
            # the element colour instead of producing a dirty outline.
            edge = 1.0 - smoothstep(0.66, 0.94, radius)
            core = math.exp(-(radius * radius) / 0.034)
            halo = math.exp(-(radius * radius) / 0.20)
            coverage = edge * (0.92 * core + 0.08 * halo)
            alpha = int(round(255.0 * clamp01(coverage)))
            # Keep RGB white through the entire falloff.  Particle colour then
            # controls the edge; baking a dark rim here makes alpha fire turn
            # into a dirty brown/black ring on coloured backgrounds.
            pixels.extend((255, 255, 255, alpha))

    OUT.parent.mkdir(parents=True, exist_ok=True)
    ihdr = struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0)
    OUT.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
                    chunk(b"IDAT", zlib.compress(bytes(pixels), 9)) +
                    chunk(b"IEND", b""))
    print(OUT)


if __name__ == "__main__":
    main()
