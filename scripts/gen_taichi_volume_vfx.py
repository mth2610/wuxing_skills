#!/usr/bin/env python3
"""Bake distinct smoke and fire flipbook prototypes from a 3D Taichi volume.

The density is a physically scaled set of advected Gaussian parcels. This is
an inexpensive volumetric model, not a Navier–Stokes solver. It integrates
optical depth through the volume; only smoke receives directional light maps.
Source atlas patches provide small-scale material detail, never silhouettes.

    /usr/bin/python3 scripts/gen_taichi_volume_vfx.py --kind smoke
    /usr/bin/python3 scripts/gen_taichi_volume_vfx.py --kind fire
"""

import argparse
from pathlib import Path

import numpy as np
from PIL import Image
import taichi as ti

from gen_taichi_gas_prototypes import synthesize_detail


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build_cache/taichi_vfx_prototypes"
N, V, GRID, FRAMES = 256, 80, 8, 64
CELL_M = 2.0
FPS = 30.0
DZ_M = CELL_M / V


def write_atlas(name, frames):
    atlas = np.zeros((N * GRID, N * GRID, 4), dtype=np.uint8)
    for i, frame in enumerate(frames):
        y, x = divmod(i, GRID)
        atlas[y * N:(y + 1) * N, x * N:(x + 1) * N] = np.uint8(
            np.clip(frame * 255.0 + 0.5, 0, 255))
    path = OUT / name
    Image.fromarray(atlas).save(path)
    print(path.relative_to(ROOT), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kind", choices=("smoke", "fire"), required=True)
    parser.add_argument("--frames", type=int, default=FRAMES)
    parser.add_argument("--seed", type=int, default=20260923)
    args = parser.parse_args()
    if not 1 <= args.frames <= FRAMES:
        parser.error("--frames must be in [1, 64]")
    OUT.mkdir(parents=True, exist_ok=True)
    ti.init(arch=ti.cpu, default_fp=ti.f32, offline_cache=False,
            offline_cache_file_path=str(ROOT / "build_cache/taichi_gas_kernel_cache"))

    density = ti.field(ti.f32, shape=(V, V, V))
    heat = ti.field(ti.f32, shape=(V, V, V))
    detail = ti.field(ti.f32, shape=(N, N))
    art = ti.Vector.field(4, ti.f32, shape=(N, N))
    normal = ti.Vector.field(4, ti.f32, shape=(N, N))
    light_a = ti.Vector.field(4, ti.f32, shape=(N, N))
    light_b = ti.Vector.field(4, ti.f32, shape=(N, N))
    detail.from_numpy(synthesize_detail(args.kind, args.seed))

    @ti.func
    def gaussian(dx: ti.f32, dy: ti.f32, dz: ti.f32, radius: ti.f32):
        return ti.exp(-0.5 * (dx * dx + dy * dy + dz * dz) / (radius * radius))

    @ti.kernel
    def build_volume(frame: ti.i32, kind: ti.i32, phase: ti.f32):
        for z, y, x in density:
            px = ((ti.cast(x, ti.f32) + 0.5) / V - 0.5) * CELL_M
            py = ((ti.cast(y, ti.f32) + 0.5) / V - 0.5) * CELL_M
            pz = ((ti.cast(z, ti.f32) + 0.5) / V - 0.5) * CELL_M
            t = ti.cast(frame, ti.f32) / FPS
            rho = 0.0
            hot = 0.0
            if kind == 0:
                # Expanding finite parcel cloud. A 0.36 m initial radius grows
                # toward 0.70 m in 0.55 s, with 0.07 m/s thermal rise.
                radius = 0.36 + 0.34 * (1.0 - ti.exp(-t / 0.55))
                lobe_radius = 0.12 + 0.07 * (1.0 - ti.exp(-t / 0.55))
                for k in ti.static(range(11)):
                    a = ti.cast(k, ti.f32) * 2.399963 + phase
                    h = (ti.cast(k, ti.f32) + 0.5) / 11.0 * 2.0 - 1.0
                    shell = 0.78 * radius * ti.sqrt(1.0 - h * h)
                    cx = shell * ti.cos(a) + 0.035 * ti.sin(2.1 * t + a)
                    cy = 0.08 + 0.07 * t + 0.78 * radius * h
                    cz = shell * ti.sin(a) + 0.035 * ti.cos(1.7 * t + a)
                    rho += gaussian(px - cx, py - cy, pz - cz,
                                    lobe_radius * (0.8 + 0.25 * ti.sin(a * 1.7) ** 2))
                rho *= 2.1 * ti.exp(-t / 2.7)
            else:
                # A continuously injected combustion plume. Parcel ages are
                # staggered, so the first output frame already contains fire.
                for k in ti.static(range(15)):
                    age = (ti.cast(k, ti.f32) / 15.0 * 1.8 + t) % 1.8
                    a = ti.cast(k, ti.f32) * 2.399963 + phase
                    cx = 0.095 * ti.sin(a + age * 3.3) * (0.45 + age * 0.45)
                    cy = -0.58 + 0.58 * age
                    cz = 0.075 * ti.cos(a + age * 2.7)
                    radius = 0.075 + 0.07 * age
                    parcel = gaussian(px - cx, py - cy, pz - cz, radius)
                    rho += parcel * ti.exp(-age / 1.5)
                    hot += parcel * ti.exp(-age / 0.72)
                rho *= 1.3
                hot *= 1.8
            # Three-dimensional curl-like warp makes the parcel boundary roll.
            # Its 0.08 m scale is small compared with the physical plume.
            warp = ti.sin(31.0 * px + 7.0 * py + 2.2 * t) * ti.sin(27.0 * pz - 9.0 * py - 1.7 * t)
            warp += 0.5 * ti.sin(68.0 * px - 42.0 * pz + 3.7 * t + phase)
            rho *= ti.math.clamp(0.65 + 0.48 * warp, 0.04, 1.3)
            hot *= ti.math.clamp(0.65 + 0.48 * warp, 0.04, 1.3)
            density[z, y, x] = ti.max(0.0, rho - (0.22 if kind == 0 else 0.16))
            heat[z, y, x] = hot

    @ti.func
    def volume_xy(field: ti.template(), z: ti.i32, xf: ti.f32, yf: ti.f32):
        ix = ti.math.clamp(ti.cast(xf, ti.i32), 0, V - 2)
        iy = ti.math.clamp(ti.cast(yf, ti.i32), 0, V - 2)
        tx, ty = xf - ix, yf - iy
        a = field[z, iy, ix] * (1.0 - tx) + field[z, iy, ix + 1] * tx
        b = field[z, iy + 1, ix] * (1.0 - tx) + field[z, iy + 1, ix + 1] * tx
        return a * (1.0 - ty) + b * ty

    @ti.kernel
    def render(frame: ti.i32, kind: ti.i32):
        for y, x in art:
            xf = (ti.cast(x, ti.f32) + 0.5) / N * V - 0.5
            yf = (ti.cast(y, ti.f32) + 0.5) / N * V - 0.5
            yf = ti.math.clamp(yf, 0.0, V - 1.001)
            xf = ti.math.clamp(xf, 0.0, V - 1.001)
            trans = 1.0
            light = 0.0
            glow = 0.0
            side_pos, side_neg, up, down, rear, front = 0.0, 0.0, 0.0, 0.0, 0.0, 0.0
            # Beer–Lambert optical depth through 2 m of actual 3D density.
            for zi in range(V):
                z = V - 1 - zi
                rho = volume_xy(density, z, xf, yf)
                tau = rho * DZ_M * (3.2 if kind == 0 else 2.6)
                absorbed = trans * (1.0 - ti.exp(-tau))
                trans *= ti.exp(-tau)
                if kind == 0:
                    dx = density[z, ti.cast(yf, ti.i32), ti.min(V - 1, ti.cast(xf, ti.i32) + 2)] - density[z, ti.cast(yf, ti.i32), ti.max(0, ti.cast(xf, ti.i32) - 2)]
                    dy = density[z, ti.min(V - 1, ti.cast(yf, ti.i32) + 2), ti.cast(xf, ti.i32)] - density[z, ti.max(0, ti.cast(yf, ti.i32) - 2), ti.cast(xf, ti.i32)]
                    dz = density[ti.min(V - 1, z + 2), ti.cast(yf, ti.i32), ti.cast(xf, ti.i32)] - density[ti.max(0, z - 2), ti.cast(yf, ti.i32), ti.cast(xf, ti.i32)]
                    light += absorbed * (0.45 + 0.20 * ti.math.clamp(-dy, -1.0, 1.0))
                    side_pos += absorbed * (0.25 + 0.55 * ti.math.clamp(dx, 0.0, 1.0))
                    side_neg += absorbed * (0.25 + 0.55 * ti.math.clamp(-dx, 0.0, 1.0))
                    up += absorbed * (0.25 + 0.55 * ti.math.clamp(-dy, 0.0, 1.0))
                    down += absorbed * (0.25 + 0.55 * ti.math.clamp(dy, 0.0, 1.0))
                    rear += absorbed * (0.25 + 0.55 * ti.math.clamp(dz, 0.0, 1.0))
                    front += absorbed * (0.25 + 0.55 * ti.math.clamp(-dz, 0.0, 1.0))
                else:
                    hot = volume_xy(heat, z, xf, yf)
                    glow += absorbed * (1.0 - ti.exp(-hot * 1.3))
            alpha = 1.0 - trans
            motif = detail[y, x]
            # Fine frequencies affect shading and porous coverage. The 3D
            # volume alone controls the macro silhouette.
            alpha *= ti.math.clamp(0.44 + 1.05 * motif, 0.0, 1.0)
            if kind == 0:
                art[y, x] = ti.Vector([ti.math.clamp(0.28 + 0.58 * motif * light / ti.max(0.02, 1.0 - trans), 0.0, 1.0), 1.0 - alpha * 0.55, 0.0, alpha])
                scale = 1.0 / ti.max(0.02, 1.0 - trans)
                light_a[y, x] = ti.Vector([ti.math.clamp(side_pos * scale, 0.0, 1.0), ti.math.clamp(up * scale, 0.0, 1.0), ti.math.clamp(rear * scale, 0.0, 1.0), alpha])
                light_b[y, x] = ti.Vector([ti.math.clamp(side_neg * scale, 0.0, 1.0), ti.math.clamp(down * scale, 0.0, 1.0), ti.math.clamp(front * scale, 0.0, 1.0), 1.0 - alpha * 0.35])
            else:
                art[y, x] = ti.Vector([ti.math.clamp(glow * (0.55 + 0.9 * motif), 0.0, 1.0), 1.0 - alpha * 0.6, 0.0, alpha])

    @ti.kernel
    def bake_normal():
        for y, x in normal:
            xm, xp = ti.max(0, x - 2), ti.min(N - 1, x + 2)
            ym, yp = ti.max(0, y - 2), ti.min(N - 1, y + 2)
            gx = (art[y, xp].w - art[y, xm].w) * 2.0
            gy = (art[ym, x].w - art[yp, x].w) * 2.0
            inv = ti.rsqrt(1.0 + gx * gx + gy * gy)
            normal[y, x] = ti.Vector([0.5 - 0.5 * gx * inv, 0.5 - 0.5 * gy * inv, 0.5 + 0.5 * inv, art[y, x].w])

    fields = {"eoo": [], "normals": []}
    if args.kind == "smoke":
        fields.update(light_a=[], light_b=[])
    for frame in range(args.frames):
        build_volume(frame, int(args.kind == "fire"), (args.seed % 997) * 0.0063)
        render(frame, int(args.kind == "fire"))
        bake_normal()
        fields["eoo"].append(art.to_numpy())
        fields["normals"].append(normal.to_numpy())
        if args.kind == "smoke":
            fields["light_a"].append(light_a.to_numpy())
            fields["light_b"].append(light_b.to_numpy())
        if (frame + 1) % 8 == 0:
            print(f"{args.kind}: {frame + 1}/{args.frames}", flush=True)
    for channel, frames in fields.items():
        # Short preview runs intentionally leave remaining atlas cells empty.
        suffix = "" if args.frames == FRAMES else f"_preview{args.frames}"
        write_atlas(f"taichi_{args.kind}_volume_8x8_{channel}{suffix}.png", frames)


if __name__ == "__main__":
    main()
