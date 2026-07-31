#!/usr/bin/env python3
"""Generate seamless RGBA8 flow maps for volume and ribbon trails.

R and G encode a signed unit flow vector (128 is neutral), B is zero and A is
opaque.  The fields are analytic, periodic curl noise: they tile exactly on
both axes and contain no sources/sinks, so they read as coherent circulation
instead of independent noisy UV offsets.

The names intentionally include ``_volume_``.  ``energy_flow.png`` is a
separate, authored *display* sheet used by swept trails and must not be
overwritten by data textures.

Only the Python standard library is required.
"""

from __future__ import annotations

import argparse
import math
import random
import struct
import zlib
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[1]
DEFAULT_OUT_DIR = ROOT_DIR / "assets" / "textures"
TAU = math.tau


class PeriodicCurlField:
    """Divergence-free, exactly tileable 2D curl field from Fourier modes."""

    def __init__(self, seed: int, base_frequency: int, octaves: int,
                 modes_per_octave: int, persistence: float) -> None:
        rng = random.Random(seed)
        self.modes: list[tuple[int, int, float, float]] = []
        for octave in range(octaves):
            frequency = base_frequency << octave
            amplitude = persistence ** octave
            for _ in range(modes_per_octave):
                # Integer frequencies make sin(2*pi*(m*u + n*v)) periodic.
                m = rng.randint(1, frequency)
                n = rng.randint(1, frequency)
                if rng.random() < 0.5:
                    m = -m
                if rng.random() < 0.5:
                    n = -n
                phase = rng.random() * TAU
                # Divide by wavelength so fine octaves add detail, not spikes.
                weight = amplitude / math.sqrt(float(m * m + n * n))
                self.modes.append((m, n, weight, phase))

    def curl(self, u: float, v: float) -> tuple[float, float]:
        """Return curl(stream_function): divergence-free and seamless."""
        dpsi_du = 0.0
        dpsi_dv = 0.0
        for m, n, weight, phase in self.modes:
            c = math.cos(TAU * (m * u + n * v) + phase)
            dpsi_du += weight * TAU * m * c
            dpsi_dv += weight * TAU * n * c
        return dpsi_dv, -dpsi_du


def make_flow_field(size: int, *, seed: int, base_frequency: int,
                    octaves: int, modes_per_octave: int, persistence: float,
                    bias: tuple[float, float], curl_strength: float) -> list[tuple[float, float]]:
    """Return a normalized, periodic RG flow vector for every texel."""
    field = PeriodicCurlField(seed, base_frequency, octaves,
                              modes_per_octave, persistence)
    out: list[tuple[float, float]] = []
    for y in range(size):
        v = y / size
        for x in range(size):
            u = x / size
            cx, cy = field.curl(u, v)
            dx = cx * curl_strength + bias[0]
            dy = cy * curl_strength + bias[1]
            length = math.hypot(dx, dy)
            if length < 1.0e-8:
                out.append((0.0, 0.0))
            else:
                out.append((dx / length, dy / length))
    return out


def encode_rgba(field: list[tuple[float, float]]) -> bytes:
    pixels = bytearray(len(field) * 4)
    for i, (dx, dy) in enumerate(field):
        base = i * 4
        pixels[base] = max(0, min(255, round((dx * 0.5 + 0.5) * 255.0)))
        pixels[base + 1] = max(0, min(255, round((dy * 0.5 + 0.5) * 255.0)))
        pixels[base + 2] = 0
        pixels[base + 3] = 255
    return bytes(pixels)


def write_png(path: Path, pixels: bytes, width: int, height: int) -> None:
    """Write an unfiltered RGBA8 PNG without external dependencies."""
    raw = bytearray(height * (1 + width * 4))
    for y in range(height):
        dst = y * (1 + width * 4)
        src = y * width * 4
        raw[dst + 1:dst + 1 + width * 4] = pixels[src:src + width * 4]

    def chunk(kind: bytes, data: bytes) -> bytes:
        payload = kind + data
        return (struct.pack(">I", len(data)) + payload +
                struct.pack(">I", zlib.crc32(payload) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(bytes(raw), level=9)) +
           chunk(b"IEND", b""))
    path.write_bytes(png)


def presets(size: int) -> dict[str, list[tuple[float, float]]]:
    """Element-specific circulation: distinct scale, direction and turbulence."""
    return {
        # High-frequency charged eddies; no imposed direction keeps it energetic.
        "energy_volume_flow.png": make_flow_field(
            size, seed=0xE91A, base_frequency=3, octaves=3, modes_per_octave=5,
            persistence=0.52, bias=(0.0, 0.0), curl_strength=1.0),
        # Large, lazy rising rolls; low frequencies prevent a watery appearance.
        "smoke_volume_flow.png": make_flow_field(
            size, seed=0x5A0C, base_frequency=1, octaves=3, modes_per_octave=4,
            persistence=0.58, bias=(0.0, -0.35), curl_strength=0.82),
        # Ribbon smoke needs long, lengthwise billows.  A gentle V bias moves
        # the puffs through the band while the large curl keeps the motion from
        # becoming a uniform scroll.
        "smoke_ribbon_flow.png": make_flow_field(
            size, seed=0x5A0D, base_frequency=1, octaves=2, modes_per_octave=3,
            persistence=0.64, bias=(0.0, -0.52), curl_strength=0.55),
        # Smaller turbulent tongues with a strong upward UV bias.
        "fire_volume_flow.png": make_flow_field(
            size, seed=0xF1CE, base_frequency=2, octaves=4, modes_per_octave=4,
            persistence=0.50, bias=(0.0, -0.72), curl_strength=0.70),
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--size", type=int, default=256,
                        help="square output resolution (default: 256)")
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR,
                        help="output directory (default: project assets/textures)")
    args = parser.parse_args()
    if args.size < 16:
        parser.error("--size must be at least 16")
    return args


def main() -> None:
    args = parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    for name, field in presets(args.size).items():
        path = args.out_dir / name
        write_png(path, encode_rgba(field), args.size, args.size)
        print(f"{path}: {args.size}x{args.size} RGBA8 ({path.stat().st_size / 1024:.1f} KiB)")


if __name__ == "__main__":
    main()
