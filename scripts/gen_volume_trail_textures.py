#!/usr/bin/env python3
"""Generate the source textures a VOLUMETRIC TRAIL scrolls and distorts.

A swept tube samples its sheet with `u` wrapping AROUND the section and `v`
tiling ALONG the length, so every texture here must be seamless on BOTH axes.
That constraint is what makes this a script rather than a few hand-painted
files: seamlessness is achieved by construction — the noise lattice wraps — not
by cross-fading edges, which softens exactly the detail the sheet exists to
carry, and which for a direction field would be actively wrong (averaging two
opposing directions gives no flow at all).

What each texture is FOR, because they are not interchangeable:

  smoke_volume    soft, low contrast, large features. Smoke has no edges; its
                  shape comes from the silhouette and the light, not the sheet.
  fire_volume     features STRETCHED along v and higher contrast. Flame is
                  directional — it is drawn out along its travel — and it has a
                  bright core with a defined edge where smoke has neither.
  energy_volume   thin high-contrast filaments, mostly parallel. Energy reads as
                  strands, and strands are what the reference sheets all show.
  flow_trail      RG DIRECTION FIELD — the flow map itself. R/G encode a 2D
                  vector as v*0.5+0.5, and the shader displaces its lookup of
                  one of the sheets above along it. NOT a scrolling texture:
                  scrolling slides a fixed pattern, a flow map makes different
                  parts of the surface drift different ways. Three variants
                  because the swirl is the whole character.
  gradient_alpha  the third of the guide's three essentials — a 1D fade ramp,
                  stored as a column so it can be sampled with one coordinate.
  volume_noise    RGB, three independent octaved fields = a 3D offset per texel,
                  for VERTEX displacement. Not a mask: it is read as a vector,
                  so its channels must be uncorrelated or the mesh ripples along
                  one diagonal instead of billowing.

Usage:
    python3 scripts/gen_volume_trail_textures.py [--out assets/textures] [--size 256]

No third-party dependencies: this repo's toolchain has neither numpy nor PIL, so
the noise and the PNG encoder are both here.
"""

import argparse
import math
import os
import random
import struct
import zlib


# ── PNG ─────────────────────────────────────────────────────────────────────

def _chunk(tag, data):
    body = tag + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


def write_png(path, width, height, rows, colour_type):
    """rows: list of bytes objects, one per scanline, already in PNG byte order."""
    raw = b"".join(b"\x00" + r for r in rows)
    png = (b"\x89PNG\r\n\x1a\n"
           + _chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, colour_type, 0, 0, 0))
           + _chunk(b"IDAT", zlib.compress(raw, 9))
           + _chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


# ── Periodic value noise ────────────────────────────────────────────────────
#
# THE WHOLE REASON THIS IS NOT A LIBRARY CALL. Ordinary noise is not periodic, so
# a tile cut from it does not meet itself, and the usual fix — mirroring or
# cross-fading the edges — either doubles every feature or blurs the seam into a
# visible soft band. Here the integer lattice is indexed MODULO the period, so
# the field repeats exactly by construction and the seam is not a seam.

def _lattice(period, seed):
    rnd = random.Random(seed)
    return [[rnd.random() for _ in range(period)] for _ in range(period)]


def _smooth(t):
    # Quintic: zero first AND second derivative at the ends, so octaves do not
    # show the lattice as faint square edges the way a cubic fade does.
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0)


def value_noise(u, v, period, lat):
    x, y = u * period, v * period
    x0, y0 = int(math.floor(x)) % period, int(math.floor(y)) % period
    x1, y1 = (x0 + 1) % period, (y0 + 1) % period
    fx, fy = _smooth(x - math.floor(x)), _smooth(y - math.floor(y))
    a = lat[y0][x0] + (lat[y0][x1] - lat[y0][x0]) * fx
    b = lat[y1][x0] + (lat[y1][x1] - lat[y1][x0]) * fx
    return a + (b - a) * fy


def fbm(u, v, octaves, base_period, seed, gain=0.5, stretch_v=1.0):
    """Fractal sum. `stretch_v` < 1 squashes features along v, which is how a
    flame gets drawn out along its direction of travel while staying seamless:
    the PERIOD stays an integer, only the sampling rate changes."""
    total, amp, norm = 0.0, 1.0, 0.0
    period = base_period
    for o in range(octaves):
        lat = _lattice(period, seed + o * 7919)
        total += amp * value_noise(u, v * stretch_v % 1.0 if stretch_v != 1.0 else v,
                                   period, lat)
        norm += amp
        amp *= gain
        period *= 2
    return total / norm


# ── The sheets ──────────────────────────────────────────────────────────────

def gen_smoke(size, seed):
    """Soft, low contrast, large features. Smoke has no edges."""
    lat_cache = [(_lattice(p, seed + i * 7919), p)
                 for i, p in enumerate((4, 8, 16, 32))]
    rows = []
    for y in range(size):
        v = (y + 0.5) / size
        row = bytearray(size)
        for x in range(size):
            u = (x + 0.5) / size
            total = amp = norm = 0.0
            amp = 1.0
            for lat, p in lat_cache:
                total += amp * value_noise(u, v, p, lat)
                norm += amp
                amp *= 0.55
            n = total / norm
            # Gentle contrast only: pushing smoke toward black and white gives it
            # edges, and an edge is the one thing smoke must not have.
            n = 0.5 + (n - 0.5) * 1.25
            row[x] = max(0, min(255, int(n * 255)))
        rows.append(bytes(row))
    return rows


def gen_fire(size, seed):
    """Features stretched ALONG v, higher contrast, brighter core."""
    # Anisotropic by using a SHORTER period along v than across u: features are
    # then longer in v while both periods stay integers, so it still tiles.
    # STRONGER anisotropy and MORE octaves than the first attempt, which used
    # 8:3 and a steep remap and came out as leopard spots — round blobs with hard
    # edges. Flame is drawn OUT along its travel, so the across period has to be
    # several times the along period, and the fine octaves are what give the
    # leading edge its tatter. 6:1 here, against 2.7:1 before.
    layers = [(_lattice(12, seed + 11), 12, 2),
              (_lattice(24, seed + 23), 24, 4),
              (_lattice(48, seed + 37), 48, 8),
              (_lattice(64, seed + 51), 64, 16)]
    rows = []
    for y in range(size):
        v = (y + 0.5) / size
        row = bytearray(size)
        for x in range(size):
            u = (x + 0.5) / size
            total = norm = 0.0
            amp = 1.0
            for lat, pu, pv in layers:
                # Separate periods per axis: sample u at pu and v at pv.
                xx, yy = u * pu, v * pv
                x0, y0 = int(math.floor(xx)) % pu, int(math.floor(yy)) % pv
                x1, y1 = (x0 + 1) % pu, (y0 + 1) % pv
                fx, fy = _smooth(xx - math.floor(xx)), _smooth(yy - math.floor(yy))
                a = lat[y0][x0] + (lat[y0][x1] - lat[y0][x0]) * fx
                b = lat[y1][x0] + (lat[y1][x1] - lat[y1][x0]) * fx
                total += amp * (a + (b - a) * fy)
                norm += amp
                amp *= 0.58
            n = total / norm
            # A defined edge and a hot core, but NOT a threshold. The first
            # version remapped over a 0.55 window and then smoothstepped, which
            # is close enough to binary that everything mid-tone vanished — and
            # mid-tones are the whole difference between a tongue of flame and a
            # spot. Wider window, single smoothstep, floor lifted so the gaps
            # still carry a little heat.
            n = max(0.0, min(1.0, (n - 0.22) / 0.72))
            n = n * n * (3.0 - 2.0 * n)
            n = 0.08 + 0.92 * n
            row[x] = int(n * 255)
        rows.append(bytes(row))
    return rows


def gen_energy(size, seed):
    """Thin, high-contrast, mostly parallel filaments."""
    rnd = random.Random(seed)
    # Filaments are explicit rather than thresholded noise: thresholding gives
    # blobs with ragged edges, and what every reference sheet shows is STRANDS.
    # Each runs the full height (so it tiles along v by construction) and wanders
    # across u with integer-frequency wobble (so it tiles across u too).
    n_fil = 22
    fil = []
    for i in range(n_fil):
        fil.append(dict(u=rnd.random(),
                        wob=rnd.uniform(0.02, 0.10),
                        cyc=rnd.randint(1, 3),
                        ph=rnd.uniform(0.0, 2.0 * math.pi),
                        tight=rnd.uniform(0.004, 0.016),
                        amp=rnd.uniform(0.35, 1.0)))
    haze_lat = _lattice(8, seed + 5)
    rows = []
    for y in range(size):
        v = (y + 0.5) / size
        row = bytearray(size)
        for x in range(size):
            u = (x + 0.5) / size
            a = 0.10 * value_noise(u, v, 8, haze_lat)
            for f in fil:
                c = f["u"] + f["wob"] * math.sin(2.0 * math.pi * f["cyc"] * v + f["ph"])
                # Wrap the distance: a filament near u = 0 must light both edges,
                # or the seam is a dark line down the tube.
                d = abs(u - c)
                d = min(d, 1.0 - d)
                a += f["amp"] * math.exp(-(d / f["tight"]) ** 2)
            row[x] = max(0, min(255, int(a * 255)))
        rows.append(bytes(row))
    return rows


def gen_noise_rgb(size, seed):
    """Three UNCORRELATED octaved fields — a 3D offset per texel."""
    chans = []
    for c in range(3):
        cache = [(_lattice(p, seed + c * 104729 + i * 7919), p)
                 for i, p in enumerate((8, 16, 32))]
        field = []
        for y in range(size):
            v = (y + 0.5) / size
            r = []
            for x in range(size):
                u = (x + 0.5) / size
                total = norm = 0.0
                amp = 1.0
                for lat, p in cache:
                    total += amp * value_noise(u, v, p, lat)
                    norm += amp
                    amp *= 0.5
                r.append(total / norm)
            field.append(r)
        chans.append(field)
    rows = []
    for y in range(size):
        row = bytearray(size * 3)
        for x in range(size):
            for c in range(3):
                row[x * 3 + c] = max(0, min(255, int(chans[c][y][x] * 255)))
        rows.append(bytes(row))
    return rows


def flow_vector(u, v, swirl):
    """The SAME harmonics as FlowMap_CreateWithTrailTexture in core/flow_map.c.

    Kept identical on purpose: two generators for one field is two things to
    drift apart, and a flow map that disagrees with itself between the asset and
    the runtime fallback is a bug nobody would think to look for."""
    amp = (0.55, 0.30, 0.18)
    ku = (1, 2, 3)
    kv = (2, 1, 3)
    ph = (0.0, 1.9, 3.7)
    fu, fv = 0.0, -1.0
    for h in range(3):
        a = 2.0 * math.pi * (ku[h] * u + kv[h] * v) + ph[h]
        fu += swirl * amp[h] * math.sin(a)
        fv += 0.35 * amp[h] * math.cos(a)
    max_len = 1.0 + 0.35 * sum(amp) + swirl * sum(amp)
    return fu / max_len, fv / max_len


def gen_flow(size, seed, swirl):
    """RG direction field, tiling on both axes by construction.

    B is left at 0 and A at 255. A flow map is READ AS A VECTOR, so its unused
    channels must be inert — putting a mask in B is a standard trick and a
    standard way to get a field that quietly changes when someone 'optimises'
    the texture format."""
    rows = []
    for y in range(size):
        v = (y + 0.5) / size
        row = bytearray(size * 3)
        for x in range(size):
            u = (x + 0.5) / size
            fu, fv = flow_vector(u, v, swirl)
            row[x * 3 + 0] = max(0, min(255, int((fu * 0.5 + 0.5) * 255)))
            row[x * 3 + 1] = max(0, min(255, int((fv * 0.5 + 0.5) * 255)))
            row[x * 3 + 2] = 0
        rows.append(bytes(row))
    return rows


def gen_gradient(width, height):
    """A fade ramp, as a column. Held slightly then falling — a linear fade on
    an additive layer reads as a blink, because the eye is far more sensitive to
    the first half of a fade than the last."""
    rows = []
    for y in range(height):
        t = 1.0 - (y + 0.5) / height          # 1 at the top = the head
        a = t * t * (3.0 - 2.0 * t)           # smoothstep
        a = 0.15 * t + 0.85 * a               # a little linear, so it never flatlines
        rows.append(bytes(bytearray([max(0, min(255, int(a * 255)))]) * width))
    return rows


# ── Verification ────────────────────────────────────────────────────────────

def seam_error(rows, channels=1):
    """Mean absolute difference across the two wraps, 0..255. This is the whole
    acceptance test: a sheet that does not tile shows a line, and a line on a
    tube runs its entire length."""
    h, w = len(rows), len(rows[0]) // channels
    du = sum(abs(rows[y][0 * channels + c] - rows[y][(w - 1) * channels + c])
             for y in range(h) for c in range(channels)) / (h * channels)
    dv = sum(abs(rows[0][x * channels + c] - rows[h - 1][x * channels + c])
             for x in range(w) for c in range(channels)) / (w * channels)
    return du, dv


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="assets/textures")
    ap.add_argument("--size", type=int, default=256)
    ap.add_argument("--seed", type=int, default=20260730)
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    jobs = [
        ("smoke_volume.png", gen_smoke, 0),
        ("fire_volume.png", gen_fire, 0),
        ("energy_volume.png", gen_energy, 0),
        ("volume_noise.png", gen_noise_rgb, 2),  # colour type 2 = RGB
        ("flow_trail_calm.png", lambda s_, sd: gen_flow(s_, sd, 0.15), 2),
        ("flow_trail.png", lambda s_, sd: gen_flow(s_, sd, 0.40), 2),
        ("flow_trail_wild.png", lambda s_, sd: gen_flow(s_, sd, 0.95), 2),
    ]
    # The gradient is 1-D, so it is written directly rather than going through
    # the square-texture loop.
    grad = gen_gradient(8, args.size)
    write_png(os.path.join(args.out, "gradient_alpha.png"), 8, args.size, grad, 0)
    print(f"{'gradient_alpha.png':22s} 8x{args.size}  (1-D fade ramp)")

    for name, fn, ctype in jobs:
        rows = fn(args.size, args.seed)
        ch = 3 if ctype == 2 else 1
        path = os.path.join(args.out, name)
        write_png(path, args.size, args.size, rows, ctype)
        du, dv = seam_error(rows, ch)
        # The adjacent-row difference is the yardstick: a seam is only a seam if
        # it is larger than the texture's own local variation.
        local = sum(abs(rows[len(rows) // 2][i] - rows[len(rows) // 2][i + ch])
                    for i in range(0, (args.size - 1) * ch, ch)) / (args.size - 1)
        verdict = "seamless" if (du <= local * 1.5 and dv <= local * 1.5) else "SEAM!"
        print(f"{name:22s} {args.size}x{args.size}  wrap u {du:5.2f}  "
              f"wrap v {dv:5.2f}  local {local:5.2f}  -> {verdict}")


if __name__ == "__main__":
    main()
