#!/usr/bin/env python3
"""Experimental Taichi gas bakes with separate smoke and fire material outputs.

This generator reads no source texture. Taichi advects density and heat through
an evolving divergence-free curl field, then shades the simulated density with
coherent multiscale detail. The directional maps are 2.5D approximations of
light transmission through that field for SMOKE only, not a hidden 3D volume
reconstruction. FIRE exports emission/coverage plus a normal companion only.
Outputs go to build_cache because the current 2D visuals are below the art bar.

    /usr/bin/python3 scripts/gen_taichi_gas_prototypes.py --kind smoke --arch cpu
    /usr/bin/python3 scripts/gen_taichi_gas_prototypes.py --kind fire --arch cpu
"""

import argparse
from pathlib import Path

import numpy as np
from PIL import Image
import taichi as ti


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build_cache/taichi_vfx_prototypes"
N = 256
GRID = 8
FRAMES = 64
# One 256 px cell spans 2 m, played at 30 fps. Artist-facing targets below
# have physical units; framewise solver coefficients are derived from them.
CELL_METERS = 2.0
FPS = 30.0
PX_PER_METER = N / CELL_METERS
SMOKE_R0_M = 0.11
SMOKE_RMAX_M = 0.57
SMOKE_EXPANSION_TAU_S = 0.48
SMOKE_TURBULENCE_MPS = 0.16
SMOKE_RISE_MPS = 0.065
SMOKE_DISSIPATION_TAU_S = 3.2
FIRE_SOURCE_RADIUS_M = 0.085
FIRE_RISE_MPS = 0.52
FIRE_TURBULENCE_MPS = 0.35
FIRE_HEAT_TAU_S = 1.25
FIRE_DENSITY_TAU_S = 1.30
FIRE_FLARE_DEGREES = 12.0
FIRE_SOURCE_OPTICAL_DEPTH = 1.2
FIRE_SOURCE_HEAT = 2.0


def write_png(array, path):
    pixels = np.clip(array * 255.0 + 0.5, 0, 255).astype(np.uint8)
    Image.fromarray(pixels).save(path)
    print(path.relative_to(ROOT), flush=True)


def audit(kind, art_atlas):
    """Measure visible geometry against the dimensional design targets."""
    print(f"{kind} geometry audit (alpha > 0.10):", flush=True)
    for frame in (0, 8, 24, 40, 56):
        row, col = divmod(frame, GRID)
        alpha = art_atlas[row * N:(row + 1) * N,
                          col * N:(col + 1) * N, 3]
        ys, xs = np.nonzero(alpha > 0.10)
        if kind == "smoke":
            rr = np.hypot(xs - N * 0.5, ys - N * 0.56)
            measured = float(np.percentile(rr, 90)) if len(rr) else 0.0
            target = PX_PER_METER * (SMOKE_R0_M +
                (SMOKE_RMAX_M - SMOKE_R0_M) *
                (1.0 - np.exp(-(frame + 1) / (SMOKE_EXPANSION_TAU_S * FPS))))
            print(f"  frame {frame:02}: R90={measured:.1f}px, model R={target:.1f}px, "
                  f"ratio={measured / max(target, 1):.2f}", flush=True)
        else:
            top = int(ys.min()) if len(ys) else N
            expected = N * 0.84 - FIRE_RISE_MPS * PX_PER_METER / FPS * (frame + 1)
            print(f"  frame {frame:02}: visible top={top}px, ballistic front={expected:.0f}px",
                  flush=True)
        if len(xs) and (xs.min() < 4 or xs.max() > N - 5 or
                        ys.min() < 4 or ys.max() > N - 5):
            print(f"  WARNING: frame {frame} reaches cell edge", flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--arch", choices=("cpu", "gpu"), default="cpu")
    ap.add_argument("--kind", choices=("smoke", "fire"), required=True)
    ap.add_argument("--seed", type=int, default=20260923)
    args = ap.parse_args()
    cache = ROOT / "build_cache/taichi_gas_kernel_cache"
    cache.mkdir(parents=True, exist_ok=True)
    OUT.mkdir(parents=True, exist_ok=True)
    ti.init(arch=ti.cpu if args.arch == "cpu" else ti.gpu,
            default_fp=ti.f32, offline_cache=False,
            offline_cache_file_path=str(cache))

    density = ti.field(ti.f32, shape=(N, N))
    density_next = ti.field(ti.f32, shape=(N, N))
    heat = ti.field(ti.f32, shape=(N, N))
    heat_next = ti.field(ti.f32, shape=(N, N))
    shaped = ti.field(ti.f32, shape=(N, N))
    art = ti.Vector.field(4, ti.f32, shape=(N, N))
    normal = ti.Vector.field(4, ti.f32, shape=(N, N))
    map_a = ti.Vector.field(4, ti.f32, shape=(N, N))
    map_b = ti.Vector.field(4, ti.f32, shape=(N, N))

    @ti.func
    def sample(field: ti.template(), xf: ti.f32, yf: ti.f32):
        x = ti.math.clamp(xf, 0.0, N - 1.001)
        y = ti.math.clamp(yf, 0.0, N - 1.001)
        x0, y0 = ti.cast(x, ti.i32), ti.cast(y, ti.i32)
        tx, ty = x - x0, y - y0
        a = field[y0, x0] * (1.0 - tx) + field[y0, x0 + 1] * tx
        b = field[y0 + 1, x0] * (1.0 - tx) + field[y0 + 1, x0 + 1] * tx
        return a * (1.0 - ty) + b * ty

    @ti.func
    def hash2(x: ti.i32, y: ti.i32, seed: ti.i32):
        h = (x * 73856093) ^ (y * 19349663) ^ (seed * 83492791)
        h = (h ^ (h >> 13)) * 1274126177
        return ti.cast(h & 65535, ti.f32) / 65535.0

    @ti.func
    def noise(x: ti.f32, y: ti.f32, seed: ti.i32):
        ix, iy = ti.cast(ti.floor(x), ti.i32), ti.cast(ti.floor(y), ti.i32)
        fx, fy = x - ix, y - iy
        fx = fx * fx * (3.0 - 2.0 * fx)
        fy = fy * fy * (3.0 - 2.0 * fy)
        lo = hash2(ix, iy, seed) * (1.0 - fx) + hash2(ix + 1, iy, seed) * fx
        hi = hash2(ix, iy + 1, seed) * (1.0 - fx) + hash2(ix + 1, iy + 1, seed) * fx
        return lo * (1.0 - fy) + hi * fy

    @ti.kernel
    def reset():
        for y, x in density:
            density[y, x] = 0.0
            density_next[y, x] = 0.0
            heat[y, x] = 0.0
            heat_next[y, x] = 0.0

    @ti.kernel
    def seed_smoke(phase: ti.f32):
        for y, x in density:
            cx, cy = N * 0.50, N * 0.56
            radius = SMOKE_R0_M * PX_PER_METER
            dx = (ti.cast(x, ti.f32) - cx) / radius
            dy = (ti.cast(y, ti.f32) - cy) / radius
            amount = 12.0 * ti.exp(-0.5 * (dx * dx + dy * dy))
            # Six overlapping parcels form one cloud with asymmetric lobes.
            # Their offsets and radii are fractions of the physical seed radius.
            for k in ti.static(range(6)):
                angle = ti.cast(k, ti.f32) * 1.04719755 + phase
                lx = dx - 0.62 * ti.cos(angle)
                ly = dy - 0.62 * ti.sin(angle)
                amount += 2.2 * ti.exp(-0.5 * (lx * lx + ly * ly) / (0.47 * 0.47))
            r = ti.sqrt(dx * dx + dy * dy)
            cutoff = ti.math.clamp((1.0 - r) / 0.24, 0.0, 1.0)
            cutoff = cutoff * cutoff * (3.0 - 2.0 * cutoff)
            density[y, x] = amount * cutoff
            heat[y, x] = 0.0

    @ti.kernel
    def advance(frame: ti.i32, kind: ti.i32, phase: ti.f32):
        for y, x in density:
            fx = (ti.cast(x, ti.f32) + 0.5) / N
            fy = (ti.cast(y, ti.f32) + 0.5) / N
            t = ti.cast(frame, ti.f32)
            # Analytic curl of two stream functions: no framewise random
            # motion, so billows roll coherently instead of flickering.
            a = 7.3 * fx + 0.057 * t + phase
            b = 6.1 * fy - 0.049 * t
            c = 15.7 * fx - 0.083 * t + phase * 0.53
            d = 13.4 * fy + 0.065 * t
            eddy_x = 0.65 * ti.sin(a) * ti.cos(b) + 0.35 * ti.sin(c) * ti.cos(d)
            eddy_y = -0.65 * ti.cos(a) * ti.sin(b) - 0.35 * ti.cos(c) * ti.sin(d)
            sx, sy = N * 0.50, N * 0.56
            turb = SMOKE_TURBULENCE_MPS * PX_PER_METER / FPS
            vx, vy = eddy_x * turb, eddy_y * turb
            dilution = 1.0
            den_decay = ti.exp(-1.0 / (SMOKE_DISSIPATION_TAU_S * FPS))
            hot_decay = 0.0
            if kind == 0:
                tau = SMOKE_EXPANSION_TAU_S * FPS
                r0 = SMOKE_R0_M * PX_PER_METER
                dr = (SMOKE_RMAX_M - SMOKE_R0_M) * PX_PER_METER
                r_prev = r0 + dr * (1.0 - ti.exp(-t / tau))
                r_next = r0 + dr * (1.0 - ti.exp(-(t + 1.0) / tau))
                growth = r_next / r_prev - 1.0
                vx += (ti.cast(x, ti.f32) - sx) * growth
                vy += (ti.cast(y, ti.f32) - sy) * growth
                vy -= SMOKE_RISE_MPS * PX_PER_METER / FPS
                # Projected optical depth of fixed 3D mass scales as R^-2.
                dilution = (r_prev / r_next) * (r_prev / r_next)
            else:
                sx, sy = N * 0.50, N * 0.84
                turb = FIRE_TURBULENCE_MPS * PX_PER_METER / FPS
                vx, vy = eddy_x * turb, eddy_y * turb
                rise_px = FIRE_RISE_MPS * PX_PER_METER / FPS
                vy -= rise_px
                # A 12 degree plume opens laterally as it rises.
                vx += (ti.math.clamp((ti.cast(x, ti.f32) - sx) /
                                     (FIRE_SOURCE_RADIUS_M * PX_PER_METER), -1.0, 1.0)
                       * rise_px * ti.tan(FIRE_FLARE_DEGREES * 0.0174532925))
                den_decay = ti.exp(-1.0 / (FIRE_DENSITY_TAU_S * FPS))
                hot_decay = ti.exp(-1.0 / (FIRE_HEAT_TAU_S * FPS))
            px = ti.cast(x, ti.f32) - vx
            py = ti.cast(y, ti.f32) - vy
            den = sample(density, px, py) * den_decay * dilution
            hot = sample(heat, px, py) * hot_decay

            # Smoke: one expanding puff. Fire: a pulsing, three-lobed fuel
            # source that stays connected but sends new tongues upward.
            if kind == 1:
                radius = FIRE_SOURCE_RADIUS_M * PX_PER_METER
                residence_frames = radius / (FIRE_RISE_MPS * PX_PER_METER / FPS)
                # Three overlapping nozzles contribute about two effective
                # sources at their centre; injection follows desired source
                # optical depth / residence time rather than an arbitrary
                # per-frame amount.
                q_density = FIRE_SOURCE_OPTICAL_DEPTH / (2.0 * residence_frames)
                q_heat = FIRE_SOURCE_HEAT / (2.0 * residence_frames)
                for k in ti.static(range(3)):
                    off = ti.cast(k - 1, ti.f32)
                    cx = sx + off * radius * 0.66 + radius * 0.24 * ti.sin(t * 0.19 + phase)
                    cy = sy + off * radius * 0.10
                    qx = (ti.cast(x, ti.f32) - cx) / radius
                    qy = (ti.cast(y, ti.f32) - cy) / radius
                    g = ti.exp(-0.5 * (qx * qx + qy * qy))
                    pulse = 0.60 + 0.40 * ti.sin(t * 0.65 + off * 1.7 + phase)
                    den += g * pulse * q_density
                    hot += g * pulse * q_heat
            # Absorbing edges prevent advection clamp from making a box.
            margin = ti.min(ti.min(x, N - 1 - x), ti.min(y, N - 1 - y))
            edge = ti.math.clamp((ti.cast(margin, ti.f32) - 2.0) / 13.0, 0.0, 1.0)
            density_next[y, x] = ti.math.clamp(den * edge, 0.0, 32.0)
            heat_next[y, x] = ti.math.clamp(hot * edge, 0.0, 8.0)

    @ti.kernel
    def commit():
        for y, x in density:
            density[y, x] = density_next[y, x]
            heat[y, x] = heat_next[y, x]

    @ti.kernel
    def shade(frame: ti.i32, kind: ti.i32, seed: ti.i32):
        for y, x in density:
            xf, yf = ti.cast(x, ti.f32), ti.cast(y, ti.f32)
            t = ti.cast(frame, ti.f32)
            # Three coherent scales shape internal filaments, while simulation
            # density owns the silhouette and motion.
            n0 = noise(xf / 37.0 + t * 0.018, yf / 37.0 - t * 0.014, seed)
            n1 = noise(xf / 15.0 - t * 0.031, yf / 15.0 - t * 0.019, seed + 11)
            n2 = noise(xf / 6.0 + t * 0.047, yf / 6.0 - t * 0.032, seed + 29)
            grain = 0.48 * n0 + 0.34 * n1 + 0.18 * n2
            den = density[y, x]
            shaped[y, x] = den * ti.math.clamp(0.35 + 1.25 * grain, 0.0, 1.5)
            cov = 1.0 - ti.exp(-shaped[y, x] * (1.2 if kind == 0 else 1.0))
            # Fine porous gaps reveal flame tongues and smoke billows.
            porous = ti.math.clamp((grain - 0.20) * 1.65, 0.0, 1.0)
            if kind == 0:
                cov *= 0.72 + 0.28 * porous
            else:
                cov *= 0.28 + 0.72 * ti.math.clamp((grain - 0.33) / 0.32, 0.0, 1.0)
            lit = ti.math.clamp((0.18 + 0.85 * grain) * (1.0 - 0.30 * cov), 0.0, 1.0)
            emis = 1.0 - ti.exp(-heat[y, x] * (0.40 + 0.40 * grain))
            if kind == 0:
                art[y, x] = ti.Vector([lit * cov, 1.0 - cov * 0.53, 0.0, cov])
            else:
                art[y, x] = ti.Vector([emis, 1.0 - cov * 0.72, 0.0, cov])

    @ti.func
    def thick(x: ti.i32, y: ti.i32, sx: ti.i32, sy: ti.i32):
        accum = 0.0
        for k in ti.static(range(1, 7)):
            xx = ti.math.clamp(x + sx * k * 4, 0, N - 1)
            yy = ti.math.clamp(y + sy * k * 4, 0, N - 1)
            accum += shaped[yy, xx] * 0.16
        return accum

    @ti.kernel
    def bake_normal():
        for y, x in art:
            xm, xp = ti.max(0, x - 2), ti.min(N - 1, x + 2)
            ym, yp = ti.max(0, y - 2), ti.min(N - 1, y + 2)
            gx = (shaped[y, xp] - shaped[y, xm]) * 1.4
            gy = (shaped[ym, x] - shaped[yp, x]) * 1.4
            inv = ti.rsqrt(gx * gx + gy * gy + 1.0)
            nx, ny, nz = -gx * inv, -gy * inv, inv
            cov = art[y, x].w
            normal[y, x] = ti.Vector([0.5 + 0.5 * nx, 0.5 + 0.5 * ny, 0.5 + 0.5 * nz, cov])

    @ti.kernel
    def bake_smoke_sixway():
        for y, x in art:
            nx = normal[y, x].x * 2.0 - 1.0
            ny = normal[y, x].y * 2.0 - 1.0
            nz = normal[y, x].z * 2.0 - 1.0
            cov = art[y, x].w
            # Directional extinction tracks neighbouring material. The normal
            # adds face lighting, while R's simulated grain survives in each
            # directional map as internal detail.
            detail = ti.math.clamp(0.42 + art[y, x].x * 0.85, 0.25, 1.0)
            px = ti.exp(-1.8 * thick(x, y, 1, 0))
            mx = ti.exp(-1.8 * thick(x, y, -1, 0))
            py = ti.exp(-1.8 * thick(x, y, 0, -1))
            my = ti.exp(-1.8 * thick(x, y, 0, 1))
            lx = ti.math.clamp((0.28 + 0.72 * ti.max(nx, 0.0)) * px * detail, 0.0, 1.0)
            rx = ti.math.clamp((0.28 + 0.72 * ti.max(-nx, 0.0)) * mx * detail, 0.0, 1.0)
            ly = ti.math.clamp((0.28 + 0.72 * ti.max(ny, 0.0)) * py * detail, 0.0, 1.0)
            ry = ti.math.clamp((0.28 + 0.72 * ti.max(-ny, 0.0)) * my * detail, 0.0, 1.0)
            back = ti.math.clamp((0.28 + 0.72 * (1.0 - nz * 0.55)) * ti.exp(-1.0 * shaped[y, x]) * detail, 0.0, 1.0)
            front = ti.math.clamp((0.28 + 0.72 * nz) * ti.exp(-0.40 * shaped[y, x]) * detail, 0.0, 1.0)
            ao = ti.math.clamp(0.94 - 0.55 * shaped[y, x], 0.08, 1.0)
            map_a[y, x] = ti.Vector([lx, ly, back, cov])
            map_b[y, x] = ti.Vector([rx, ry, front, ao])

    for kind_name in (args.kind,):
        kind = int(kind_name == "fire")
        reset()
        if kind == 0:
            seed_smoke((args.seed % 997) * 0.0063)
        fields = (art, normal, map_a, map_b) if kind == 0 else (art, normal)
        suffixes = ("eoo", "normals", "light_a", "light_b") if kind == 0 else ("eoo", "normals")
        atlases = [np.zeros((GRID * N, GRID * N, 4), np.float32) for _ in fields]
        for frame in range(FRAMES):
            advance(frame, kind, (args.seed % 997) * 0.0063 + kind * 1.7)
            commit()
            shade(frame, kind, args.seed + kind * 101)
            bake_normal()
            if kind == 0:
                bake_smoke_sixway()
            row, col = divmod(frame, GRID)
            area = np.s_[row * N:(row + 1) * N, col * N:(col + 1) * N]
            for atlas, field in zip(atlases, fields):
                atlas[area] = field.to_numpy()
            if frame % 16 == 15:
                print(f"{kind_name}: {frame + 1}/{FRAMES} frames", flush=True)
        audit(kind_name, atlases[0])
        for suffix, atlas in zip(suffixes, atlases):
            write_png(atlas, OUT / f"taichi_{kind_name}_proto_8x8_{suffix}.png")


if __name__ == "__main__":
    main()
