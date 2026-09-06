#!/usr/bin/env python3
"""Ray-march simulated 3D smoke grids in Taichi to produce 6-way volumetric lightmaps.

Produces two frame sets:
  frames_a:
    R = +X (Right)
    G = +Y (Top)
    B = +Z (Backlight / Forward-Scatter)
    A = Opacity (1 - transmittance)
  frames_b:
    R = -X (Left)
    G = -Y (Bottom)
    B = -Z (Front / Camera view)
    A = Ambient Occlusion (mean 6-way penetration)

Usage:
  /usr/bin/python3 scripts/flipbook/render_sixway.py build_cache/smoke_puff --cell 256 --supersample 2 --zoom auto
"""

import argparse
import glob
import os
import sys
import time

import numpy as np
from PIL import Image
import taichi as ti


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cache_dir", help="directory with f###.npz grids")
    ap.add_argument("--cell", type=int, default=256, help="pixels per cell")
    ap.add_argument("--supersample", type=int, default=2, help="supersample factor")
    ap.add_argument("--density-scale", type=float, default=2.8, help="extinction multiplier")
    ap.add_argument("--light-scale", type=float, default=1.6, help="shadow extinction multiplier")
    ap.add_argument("--ambient-floor", type=float, default=0.08, help="shadow floor")
    ap.add_argument("--zoom", default="auto", help="auto or float zoom")
    ap.add_argument("--arch", default="gpu", choices=["gpu", "cpu"])
    args = ap.parse_args()

    files = sorted(glob.glob(os.path.join(args.cache_dir, "f*.npz")))
    if not files:
        print("no f###.npz in %s — run scripts/flipbook/ti_sim.py first" % args.cache_dir)
        return 1

    ti.init(arch=ti.gpu if args.arch == "gpu" else ti.cpu)

    first = np.load(files[0])
    rz, ry, rx = first["density"].shape
    S = args.cell * max(1, args.supersample)

    aspect = rx / rz
    autofit = str(args.zoom).lower() == "auto"
    zoom = 1.0 if autofit else float(args.zoom)
    fit_w = min(1.0, aspect) * zoom
    fit_h = min(1.0, 1.0 / aspect) * zoom

    dens = ti.field(ti.f32, shape=(rz, ry, rx))

    # 6 directional transmittance shadow fields
    shad_posX = ti.field(ti.f32, shape=(rz, ry, rx))  # +X (Right)
    shad_negX = ti.field(ti.f32, shape=(rz, ry, rx))  # -X (Left)
    shad_posY = ti.field(ti.f32, shape=(rz, ry, rx))  # +Y (Top)
    shad_negY = ti.field(ti.f32, shape=(rz, ry, rx))  # -Y (Bottom)
    shad_posZ = ti.field(ti.f32, shape=(rz, ry, rx))  # +Z (Backlight)
    shad_negZ = ti.field(ti.f32, shape=(rz, ry, rx))  # -Z (Front)

    # Output fields: Map A and Map B
    out_a = ti.Vector.field(4, ti.f32, shape=(S, S))
    out_b = ti.Vector.field(4, ti.f32, shape=(S, S))

    @ti.func
    def sample(fld, gx, gy, gz):
        x = ti.math.clamp(gx, 0.0, rx - 1.001)
        y = ti.math.clamp(gy, 0.0, ry - 1.001)
        z = ti.math.clamp(gz, 0.0, rz - 1.001)
        i, j, k = int(x), int(y), int(z)
        fx, fy, fz = x - i, y - j, z - k
        c00 = fld[k, j, i] * (1 - fx) + fld[k, j, i + 1] * fx
        c10 = fld[k, j + 1, i] * (1 - fx) + fld[k, j + 1, i + 1] * fx
        c01 = fld[k + 1, j, i] * (1 - fx) + fld[k + 1, j, i + 1] * fx
        c11 = fld[k + 1, j + 1, i] * (1 - fx) + fld[k + 1, j + 1, i + 1] * fx
        c0 = c00 * (1 - fy) + c10 * fy
        c1 = c01 * (1 - fy) + c11 * fy
        return c0 * (1 - fz) + c1 * fz

    @ti.kernel
    def march_sixway(ks: ti.f32, fw: ti.f32, fh: ti.f32):
        for px, py in out_a:
            u = ((px + 0.5) / S - 0.5) / fw + 0.5
            v = ((py + 0.5) / S - 0.5) / fh + 0.5
            gx = u * (rx - 1)
            gz = (1.0 - v) * (rz - 1)

            trans = 1.0
            smoke = 0.0
            v_posX = 0.0
            v_negX = 0.0
            v_posY = 0.0
            v_negY = 0.0
            v_posZ = 0.0
            v_negZ = 0.0

            inside = (u >= 0.0) and (u <= 1.0) and (v >= 0.0) and (v <= 1.0)
            steps = ry * 2 if inside else 0
            dstep = ti.cast(ry - 1, ti.f32) / steps if steps > 0 else 0.0

            for s in range(steps):
                gy = s * dstep
                d = sample(dens, gx, gy, gz)
                ext = d * ks * dstep
                weight = d * ks * trans * dstep

                v_posX += weight * sample(shad_posX, gx, gy, gz)
                v_negX += weight * sample(shad_negX, gx, gy, gz)
                v_posY += weight * sample(shad_posY, gx, gy, gz)
                v_negY += weight * sample(shad_negY, gx, gy, gz)
                v_posZ += weight * sample(shad_posZ, gx, gy, gz)
                v_negZ += weight * sample(shad_negZ, gx, gy, gz)

                smoke += weight
                trans *= ti.exp(-ext)
                if trans < 0.003:
                    break

            norm = ti.max(smoke, 1e-4)
            opac = 1.0 - trans
            t_fade = ti.math.clamp((opac - 0.005) / 0.025, 0.0, 1.0)
            fade = t_fade * t_fade * (3.0 - 2.0 * t_fade)

            # Map A: R = +X (Right), G = +Y (Top), B = +Z (Backlight), A = Opacity
            out_a[px, py] = ti.Vector([
                (v_posX / norm) * fade,
                (v_posY / norm) * fade,
                (v_posZ / norm) * fade,
                opac
            ])

            # Map B: R = -X (Left), G = -Y (Bottom), B = -Z (Front), A = Opacity
            out_b[px, py] = ti.Vector([
                (v_negX / norm) * fade,
                (v_negY / norm) * fade,
                (v_negZ / norm) * fade,
                opac
            ])

    t0 = time.time()

    def render_all(fw, fh, quiet=False):
        frames_a = []
        frames_b = []
        for i, p in enumerate(files):
            z = np.load(p)
            d = np.ascontiguousarray(z["density"], np.float32)
            dens.from_numpy(d)

            # Cumulative optical depth along 6 cardinal directions:
            k_sh = args.density_scale * args.light_scale
            fl = args.ambient_floor

            # +X: light from right (x=rx-1) to left
            above_px = np.cumsum(d[:, :, ::-1], axis=2)[:, :, ::-1] - d
            shad_posX.from_numpy(np.ascontiguousarray(fl + (1.0 - fl) * np.exp(-k_sh * above_px), np.float32))

            # -X: light from left (x=0) to right
            above_nx = np.cumsum(d, axis=2) - d
            shad_negX.from_numpy(np.ascontiguousarray(fl + (1.0 - fl) * np.exp(-k_sh * above_nx), np.float32))

            # +Y: light from top (z=rz-1) to bottom
            above_py = np.cumsum(d[::-1, :, :], axis=0)[::-1, :, :] - d
            shad_posY.from_numpy(np.ascontiguousarray(fl + (1.0 - fl) * np.exp(-k_sh * above_py), np.float32))

            # -Y: light from bottom (z=0) to top
            above_ny = np.cumsum(d, axis=0) - d
            shad_negY.from_numpy(np.ascontiguousarray(fl + (1.0 - fl) * np.exp(-k_sh * above_ny), np.float32))

            # +Z: light from back (y=ry-1) to camera (forward scatter)
            above_pz = np.cumsum(d[:, ::-1, :], axis=1)[:, ::-1, :] - d
            shad_posZ.from_numpy(np.ascontiguousarray(fl + (1.0 - fl) * np.exp(-k_sh * above_pz), np.float32))

            # -Z: light from front (y=0) to back
            above_nz = np.cumsum(d, axis=1) - d
            shad_negZ.from_numpy(np.ascontiguousarray(fl + (1.0 - fl) * np.exp(-k_sh * above_nz), np.float32))

            march_sixway(args.density_scale, fw, fh)

            img_a = out_a.to_numpy().transpose(1, 0, 2)
            img_b = out_b.to_numpy().transpose(1, 0, 2)

            if args.supersample > 1:
                k = args.supersample
                img_a = img_a.reshape(args.cell, k, args.cell, k, 4).mean(axis=(1, 3))
                img_b = img_b.reshape(args.cell, k, args.cell, k, 4).mean(axis=(1, 3))

            frames_a.append(img_a)
            frames_b.append(img_b)

            if i % 8 == 0 and not quiet:
                print("6-WAY RENDER: %d/%d  %.1fs" % (i, len(files), time.time() - t0), flush=True)

        return frames_a, frames_b

    if autofit:
        probe_a, _ = render_all(fit_w, fit_h, quiet=True)
        probe = np.stack(probe_a)[..., 3]
        lit = probe > 0.01
        half = args.cell / 2.0
        ys, xs = np.nonzero(lit.any(axis=0))
        if len(ys):
            reach = max(abs(ys.max() + 0.5 - half), abs(ys.min() + 0.5 - half),
                        abs(xs.max() + 0.5 - half), abs(xs.min() + 0.5 - half)) / half
            zoom = 0.88 / max(reach, 1e-3)
            fit_w, fit_h = min(1.0, aspect) * zoom, min(1.0, 1.0 / aspect) * zoom
            print("6-WAY RENDER: autofit reach %.3f -> zoom %.2f" % (reach, zoom))

    frames_a, frames_b = render_all(fit_w, fit_h)

    dir_a = os.path.join(args.cache_dir, "frames_a")
    dir_b = os.path.join(args.cache_dir, "frames_b")
    os.makedirs(dir_a, exist_ok=True)
    os.makedirs(dir_b, exist_ok=True)

    for i in range(len(files)):
        fa = np.clip(frames_a[i], 0.0, 1.0)
        fb = np.clip(frames_b[i], 0.0, 1.0)
        Image.fromarray((fa * 255).astype(np.uint8), "RGBA").save(
            os.path.join(dir_a, "f%03d.png" % (i + 1)))
        Image.fromarray((fb * 255).astype(np.uint8), "RGBA").save(
            os.path.join(dir_b, "f%03d.png" % (i + 1)))

    print("6-WAY RENDER: %d frames -> %s and %s (%.1fs)" % (
        len(files), dir_a, dir_b, time.time() - t0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
