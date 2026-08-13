#!/usr/bin/env python3
"""Build the thin-smoke strip that VFX_ComposeShockRing wraps around its ring.

WHY THIS IS A SIMULATION AND NOT A NOISE FUNCTION
-------------------------------------------------
An fbm is statistically homogeneous: every region of it looks like every other
region.  Threshold one and you get features of one size, evenly distributed,
which is exactly what made earlier versions of the shock ring read as a
necklace of similar beads.  No amount of octaves, clumping or domain warp
removes that, because it is a property of the field and not of the tuning.

What reference smoke has instead is long sweeping strokes next to fine detail
next to nothing at all.  That comes from ADVECTION: particles carried through a
swirling field leave streaks whose length, thickness and density depend on where
they started and how long they survived.  Two neighbouring regions genuinely
differ, because their histories differ.

So this is a particle sim.  It does not need a fluid solver and it does not need
taichi — a divergence-free curl-noise field plus streakline splatting is enough
to get the structure, and it runs in the standard library in about half a minute.

THE THREE SOURCES OF NON-UNIFORMITY, in the order they matter:

  1. CLUSTERED SEEDING.  Particles start in a handful of clumps rather than
     uniformly, so some arcs of the final ring are dense and others are nearly
     empty.  This is the one that does the most work, and it is deliberately NOT
     noise-driven — uniform seeding through a noisy field still gives uniform
     coverage, just noisier.
  2. PER-PARTICLE VARIATION.  Lifetime, speed, weight and radius all vary, so
     strokes differ in length and thickness rather than only in position.
  3. THE FIELD ITSELF.  Curl of an fbm potential: divergence-free, so it swirls
     and folds instead of pushing everything one way.

PERIODIC IN X.  The strip wraps around a closed ring, so the noise lattice wraps
in x and particles that leave one side re-enter the other.  Without this the
consumer has to dodge the strip's ends, which is what the shader used to do with
a 0.06..0.94 remap — a workaround for a texture that should simply have tiled.
Top and bottom rows are forced to zero, so it is tileable on both axes.

OUTPUT.  RGBA.  RGB stays neutral so VFX_Material supplies the element colour at
runtime; alpha is the coverage.  See assets/TEXTURE_PACKING.md.

Deterministic: same seed, same bytes.  Standard library only, so the committed
sheet is reproducible.
"""

from __future__ import annotations

import argparse
import math
import struct
import zlib
from array import array
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT = ROOT / "assets" / "textures" / "shock_ring_smoke.png"

WIDTH = 2048
HEIGHT = 512

# Base angular cells of the potential field. LOW: this sets the size of the
# sweeping structures, and the whole point is that they are large. Detail comes
# from the octaves and from the streaks, not from this.
FIELD_CELLS_X = 6
FIELD_CELLS_Y = 3
FIELD_OCTAVES = 3

CLUSTERS = 11          # dense arcs around the ring
PARTICLES_PER_CLUSTER = 340
STEPS = 110            # advection steps per particle
DT = 0.0016            # normalised units per step


# ── deterministic hashing ───────────────────────────────────────────────────

def _hash(ix: int, iy: int, seed: int) -> float:
    h = (ix * 374761393 + iy * 668265263 + seed * 2654435761) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((h ^ (h >> 16)) & 0xFFFFFF) / float(0xFFFFFF)


def _vnoise(x: float, y: float, period_x: int, seed: int) -> float:
    """Value noise, periodic in x with `period_x` cells."""
    ix = math.floor(x)
    iy = math.floor(y)
    fx = x - ix
    fy = y - iy
    ux = fx * fx * (3.0 - 2.0 * fx)
    uy = fy * fy * (3.0 - 2.0 * fy)
    x0 = int(ix) % period_x
    x1 = (int(ix) + 1) % period_x
    y0 = int(iy)
    y1 = y0 + 1
    a = _hash(x0, y0, seed)
    b = _hash(x1, y0, seed)
    c = _hash(x0, y1, seed)
    d = _hash(x1, y1, seed)
    top = a + (b - a) * ux
    bot = c + (d - c) * ux
    return top + (bot - top) * uy


def _fbm(x: float, y: float, cells_x: int, cells_y: int, seed: int) -> float:
    """Octaves double frequency AND period together, so every octave wraps."""
    total = 0.0
    amp = 0.5
    norm = 0.0
    px, py = float(cells_x), float(cells_y)
    period = cells_x
    for _ in range(FIELD_OCTAVES):
        total += amp * _vnoise(x * px, y * py, period, seed)
        norm += amp
        px *= 2.0
        py *= 2.0
        period *= 2
        amp *= 0.5
    return total / norm


# ── the velocity field ──────────────────────────────────────────────────────

def _curl(x: float, y: float, cx: int, cy: int, seed: int) -> tuple[float, float]:
    e = 0.004
    p_up = _fbm(x, y + e, cx, cy, seed)
    p_dn = _fbm(x, y - e, cx, cy, seed)
    p_rt = _fbm(x + e, y, cx, cy, seed)
    p_lf = _fbm(x - e, y, cx, cy, seed)
    return ((p_up - p_dn) / (2.0 * e), -(p_rt - p_lf) / (2.0 * e))


def _velocity(x: float, y: float, seed: int) -> tuple[float, float]:
    """Curl of a scalar potential: divergence-free, so it swirls and folds
    instead of sweeping everything in one direction. A plain gradient field
    would push every particle the same way and the streaks would come out
    parallel — the comb again, by yet another route.

    TWO SCALES. With one smooth field every particle in a neighbourhood follows
    almost the same streamline, and the sheet comes out as concentric contour
    bands — laminar, and readable as such. A weaker high-frequency curl on top
    separates neighbours without destroying the large structure."""
    bx, by = _curl(x, y, FIELD_CELLS_X, FIELD_CELLS_Y, seed)
    sx, sy = _curl(x, y, FIELD_CELLS_X * 5, FIELD_CELLS_Y * 4, seed + 4409)
    return (bx + sx * 0.38, by + sy * 0.38)


# ── splatting ───────────────────────────────────────────────────────────────

def _kernel(radius_px: int) -> list[tuple[int, int, float]]:
    """Soft round falloff, precomputed once per radius."""
    out = []
    r = float(radius_px)
    for dy in range(-radius_px, radius_px + 1):
        for dx in range(-radius_px, radius_px + 1):
            d = math.hypot(dx, dy) / (r + 0.5)
            if d >= 1.0:
                continue
            f = 1.0 - d * d
            out.append((dx, dy, f * f))
    return out


def simulate(seed: int) -> array:
    field = array("f", bytes(4 * WIDTH * HEIGHT))
    kernels = {r: _kernel(r) for r in range(1, 6)}

    rnd_i = 0

    def rnd() -> float:
        nonlocal rnd_i
        rnd_i += 1
        return _hash(rnd_i, rnd_i * 7 + 13, seed + 977)

    for c in range(CLUSTERS):
        # Cluster centres are spread around x with a jitter, so the dense arcs
        # are irregular without ever leaving a half of the strip empty.
        cx = (c + 0.5) / CLUSTERS + (rnd() - 0.5) * (0.55 / CLUSTERS)
        cy = 0.5 + (rnd() - 0.5) * 0.10
        spread_x = 0.020 + rnd() * 0.055
        spread_y = 0.020 + rnd() * 0.045
        # Whole clusters differ in weight, so some arcs are faint and some solid.
        cluster_gain = 0.35 + rnd() * 0.95
        count = int(PARTICLES_PER_CLUSTER * (0.45 + rnd() * 1.10))

        for _ in range(count):
            # Box-Muller-ish clustering without importing random.
            u1 = max(rnd(), 1e-6)
            u2 = rnd()
            g = math.sqrt(-2.0 * math.log(u1))
            px = cx + g * math.cos(2.0 * math.pi * u2) * spread_x
            py = cy + g * math.sin(2.0 * math.pi * u2) * spread_y

            life = int(STEPS * (0.25 + rnd() * 0.95))
            speed = 0.55 + rnd() * 1.30
            radius = 1 + int(rnd() * 4.0)
            weight = cluster_gain * (0.25 + rnd() * 0.85)
            # A gentle outward bias so the streaks lean across the strip rather
            # than only circulating; the ring reads as expanding because of it.
            bias = (rnd() - 0.5) * 0.35 + 0.16

            kern = kernels[radius]
            prev_x, prev_y = px, py
            for step in range(life):
                vx, vy = _velocity(px, py, seed)
                px += vx * DT * speed
                py += (vy + bias) * DT * speed
                px -= math.floor(px)          # wrap in x, the ring is closed
                if py < 0.02 or py > 0.98:
                    break

                # Fade in fast, out slowly: a streak is densest just behind its
                # head, which is what gives it direction.
                t = step / float(life)
                w = weight * (1.0 - t) * (1.0 - t) * min(1.0, t * 8.0 + 0.05)
                if w <= 0.002:
                    continue

                # SPLAT ALONG THE SEGMENT, not only at the endpoint. A particle
                # can travel further in one step than the kernel is wide, and the
                # streak then comes out as a string of beads — which is exactly
                # what it looked like before this. Sub-stepping by the distance
                # actually covered keeps the stroke continuous without paying for
                # a smaller DT everywhere.
                dxs = px - prev_x
                dxs -= round(dxs)          # shortest way round the wrap
                dys = py - prev_y
                dist_px = math.hypot(dxs * WIDTH, dys * HEIGHT)
                sub = int(dist_px / max(radius * 0.7, 0.5)) + 1
                if sub > 12:
                    sub = 12
                sw = w / float(sub)
                for k in range(sub):
                    t_sub = (k + 1) / float(sub)
                    sx_n = (prev_x + dxs * t_sub) % 1.0
                    sy_n = prev_y + dys * t_sub
                    ix = int(sx_n * WIDTH)
                    iy = int(sy_n * HEIGHT)
                    for dx, dy, f in kern:
                        yy = iy + dy
                        if yy < 0 or yy >= HEIGHT:
                            continue
                        xx = (ix + dx) % WIDTH
                        field[yy * WIDTH + xx] += sw * f
                prev_x, prev_y = px, py
    return field


# ── post ────────────────────────────────────────────────────────────────────

def to_alpha(field: array, seed: int) -> bytearray:
    # NORMALISE ON A PERCENTILE, NOT THE MAXIMUM. A handful of pixels where many
    # streaks crossed accumulate values several times higher than anything else;
    # dividing by that maximum pushes 88% of the sheet under alpha 0.1 and the
    # consumer's thresholds then find nothing at all. The strip renders as an
    # empty ring while every individual term still looks correct — which is
    # exactly how this failed the first time. Take the level that 99.2% of the
    # non-zero samples fall below and clamp the rest.
    BUCKETS = 4096
    hi = 0.0
    for v in field:
        if v > hi:
            hi = v
    if hi <= 0.0:
        hi = 1.0
    hist = [0] * (BUCKETS + 1)
    nonzero = 0
    for v in field:
        if v > 0.0:
            hist[int(v / hi * BUCKETS)] += 1
            nonzero += 1
    target = int(nonzero * 0.992)
    acc = 0
    ref = hi
    for b, c in enumerate(hist):
        acc += c
        if acc >= target:
            ref = (b + 1) / float(BUCKETS) * hi
            break
    inv = 1.0 / max(ref, 1e-9)

    out = bytearray(WIDTH * HEIGHT)
    for y in range(HEIGHT):
        # Force the top and bottom rows to nothing, which both keeps the strip
        # tileable on the short axis and gives the consumer somewhere safe to pan
        # the smoke off to.
        fy = y / float(HEIGHT - 1)
        edge = min(1.0, fy / 0.10) * min(1.0, (1.0 - fy) / 0.10)
        edge = edge * edge
        row = y * WIDTH
        for x in range(WIDTH):
            v = field[row + x] * inv
            if v <= 0.0:
                continue
            if v > 1.0:
                v = 1.0
            # Gamma opens up the thin tails; the sim concentrates far too much of
            # its range in the brightest few percent to use linearly.
            v = v ** 0.55
            # A light erosion so solid cores break rather than reading as paint.
            n = _vnoise(x / WIDTH * 24.0, y / HEIGHT * 9.0, 24, seed + 31)
            v *= 0.72 + 0.55 * n
            v *= edge
            if v > 1.0:
                v = 1.0
            out[row + x] = int(v * 255.0 + 0.5)
    return out


def write_png(path: Path, alpha: bytearray) -> None:
    raw = bytearray()
    for y in range(HEIGHT):
        raw.append(0)  # filter: none
        row = y * WIDTH
        for x in range(WIDTH):
            a = alpha[row + x]
            raw += b"\xff\xff\xff"
            raw.append(a)

    def chunk(kind: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + kind + payload +
                struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 6, 0, 0, 0)
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
           chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--seed", type=int, default=20260813)
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = ap.parse_args()

    field = simulate(args.seed)
    alpha = to_alpha(field, args.seed)
    write_png(args.out, alpha)

    covered = sum(1 for a in alpha if a > 8)
    print(f"{args.out.relative_to(ROOT)}  {WIDTH}x{HEIGHT}  "
          f"coverage {covered / float(WIDTH * HEIGHT) * 100.0:.1f}%")


if __name__ == "__main__":
    main()
