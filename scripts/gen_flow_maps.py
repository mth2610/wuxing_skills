#!/usr/bin/env python3
"""Generate RG flow maps for volume trail textures.

Each output is a 256x256 RGBA PNG where:
  R = direction X  (0..255 maps to -1..1)
  G = direction Y  (0..255 maps to -1..1)
  B = 0
  A = 255

Uses only stdlib (struct + zlib), no PIL/numpy dependency.
"""

import math
import struct
import zlib
import os
import random

OUT_DIR = "assets/textures"

# ---------------------------------------------------------------------------
#  value-noise with smoothstep (cheap, tileable by construction)
# ---------------------------------------------------------------------------

class TileableNoise:
    """2D value noise on a wrap-around grid."""

    def __init__(self, grid_w: int, grid_h: int, seed: int = 0):
        self.gw = grid_w
        self.gh = grid_h
        rng = random.Random(seed)
        self.cells = [rng.random() * 2.0 - 1.0 for _ in range(grid_w * grid_h)]

    def _cell(self, x: int, y: int) -> float:
        return self.cells[(y % self.gh) * self.gw + (x % self.gw)]

    def sample(self, u: float, v: float) -> float:
        u = u % 1.0
        v = v % 1.0
        fx = u * self.gw
        fy = v * self.gh
        ix = int(fx) % self.gw
        iy = int(fy) % self.gh
        sx = fx - int(fx)
        sy = fy - int(fy)
        sx = sx * sx * (3.0 - 2.0 * sx)
        sy = sy * sy * (3.0 - 2.0 * sy)
        n00 = self._cell(ix, iy)
        n10 = self._cell(ix + 1, iy)
        n01 = self._cell(ix, iy + 1)
        n11 = self._cell(ix + 1, iy + 1)
        top = n00 + (n10 - n00) * sx
        bot = n01 + (n10 - n01) * sx  # not a bug — mixes along x so the
                                      # vertical blend reads the same cross
                                      # section at every row.
        return top + (bot - top) * sy


# ---------------------------------------------------------------------------
#  flow field builder
# ---------------------------------------------------------------------------

def make_flow_field(
    size: int,
    pot_grid_w: int,
    pot_grid_h: int,
    seed_x: int,
    seed_y: int,
    octaves: int = 3,
    persistence: float = 0.5,
    bias_u: float = 0.0,
    bias_v: float = 0.0,
) -> list[tuple[float, float]]:
    """Return a list of (dx,dy) for every pixel, tileable."""
    base = TileableNoise(pot_grid_w, pot_grid_h, seed_x)
    base2 = TileableNoise(pot_grid_w, pot_grid_h, seed_y)

    def fbm(nz, u, v):
        val = 0.0
        amp = 1.0
        freq = 1.0
        total = 0.0
        for _ in range(octaves):
            val += amp * nz.sample(u * freq, v * freq)
            total += amp
            amp *= persistence
            freq *= 2.0
        return val / total

    out = []
    for py in range(size):
        for px in range(size):
            u = px / size
            v = py / size
            dx = fbm(base, u, v) * 0.7 + fbm(base2, u + 0.3, v + 0.7) * 0.3 + bias_u
            dy = fbm(base2, u, v) * 0.7 + fbm(base, u + 0.7, v + 0.3) * 0.3 + bias_v
            length = math.sqrt(dx * dx + dy * dy)
            if length > 1e-8:
                dx /= length
                dy /= length
            out.append((dx, dy))
    return out


# ---------------------------------------------------------------------------
#  PNG writer (std only — no PIL)
# ---------------------------------------------------------------------------

def write_png(path: str, pixels: bytes, w: int, h: int) -> None:
    """Write an RGBA8 PNG."""
    raw = b""
    for y in range(h):
        raw += b"\x00"  # filter byte = None
        row_start = y * w * 4
        raw += pixels[row_start: row_start + w * 4]
    compressed = zlib.compress(raw)

    def chunk(ctype: bytes, data: bytes) -> bytes:
        c = ctype + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)  # 8-bit RGBA = color_type 6
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", compressed))
        f.write(chunk(b"IEND", b""))


# ---------------------------------------------------------------------------
#  presets per gas kind
# ---------------------------------------------------------------------------

def gen_energy_flow(size: int = 256) -> bytes:
    """Tight swirls — quick, sharp direction changes."""
    field = make_flow_field(size, 16, 16, 73, 191, octaves=3, persistence=0.5)
    pix = bytearray()
    for dx, dy in field:
        r = int((dx * 0.5 + 0.5) * 255 + 0.5)
        g = int((dy * 0.5 + 0.5) * 255 + 0.5)
        if r > 255: r = 255
        if g > 255: g = 255
        pix.extend([r, g, 0, 255])
    return bytes(pix)


def gen_smoke_flow(size: int = 256) -> bytes:
    """Large slow eddies — lazy churn."""
    field = make_flow_field(size, 8, 8, 421, 577, octaves=4, persistence=0.6)
    pix = bytearray()
    for dx, dy in field:
        r = int((dx * 0.5 + 0.5) * 255 + 0.5)
        g = int((dy * 0.5 + 0.5) * 255 + 0.5)
        if r > 255: r = 255
        if g > 255: g = 255
        pix.extend([r, g, 0, 255])
    return bytes(pix)


def gen_fire_flow(size: int = 256) -> bytes:
    """Strong upward bias with flicker."""
    field = make_flow_field(size, 12, 12, 911, 313, octaves=3, persistence=0.5,
                            bias_u=0.0, bias_v=0.35)  # upward pull
    pix = bytearray()
    for dx, dy in field:
        r = int((dx * 0.5 + 0.5) * 255 + 0.5)
        g = int((dy * 0.5 + 0.5) * 255 + 0.5)
        if r > 255: r = 255
        if g > 255: g = 255
        pix.extend([r, g, 0, 255])
    return bytes(pix)


def gen_water_flow(size: int = 256) -> bytes:
    """Horizontal layers with gentle wave."""
    field = make_flow_field(size, 20, 4, 557, 223, octaves=3, persistence=0.5,
                            bias_u=0.5, bias_v=0.0)  # horizontal sweep
    pix = bytearray()
    for dx, dy in field:
        r = int((dx * 0.5 + 0.5) * 255 + 0.5)
        g = int((dy * 0.5 + 0.5) * 255 + 0.5)
        if r > 255: r = 255
        if g > 255: g = 255
        pix.extend([r, g, 0, 255])
    return bytes(pix)


# ---------------------------------------------------------------------------
#  main
# ---------------------------------------------------------------------------

def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    generators = {
        "energy_flow.png": gen_energy_flow,
        "smoke_flow.png":  gen_smoke_flow,
        "fire_flow.png":   gen_fire_flow,
        "water_flow.png":  gen_water_flow,
    }
    for name, gen in generators.items():
        path = os.path.join(OUT_DIR, name)
        data = gen(256)
        write_png(path, data, 256, 256)
        size_kb = os.path.getsize(path) / 1024
        print(f"  {name:25s}  {size_kb:.1f} KiB  (256x256 RGBA)")


if __name__ == "__main__":
    main()
