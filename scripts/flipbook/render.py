#!/usr/bin/env python3
"""Step 2 of 3 — ray-march the dumped Mantaflow grids on the GPU (Taichi).

    python3 scripts/ti_render.py build_cache/fire --cell 128
    python3 scripts/ti_render.py build_cache/fire --cell 256 --supersample 2

Reads build_cache/<name>/f###.npz (from scripts/manta_bake.py) and writes
build_cache/<name>/frames/f###.png, ready for scripts/pack_flipbook.py.

WHY A HAND-WRITTEN RAY-MARCHER RATHER THAN EEVEE
    Eevee's alpha comes from EXTINCTION, so an emissive flame renders bright and
    almost transparent — measured on the previous pipeline at rgb 255 / alpha 10,
    which forced alpha to be faked from luminance and collapsed the sheet to one
    channel of information. Marching the grid ourselves decides exactly what
    lands in each channel, so "thick but cool" (smoke) and "hot" stay separable.

CHANNEL LAYOUT (what the engine gets)
    R = emission / flame        → multiply by the black-body ramp at the call
                                  site (F3); this is the additive population.
    G = smoke density           → the alpha-blended, LIT population (F1b).
    B = 0                       → reserved (motion vectors, rim, 6-way lighting).
    A = 1 - transmittance       → a real opacity, not a luminance guess.

    One draw of this sheet can therefore feed both populations the blend law
    requires, instead of one greyscale mask doing duty for both.
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
    ap.add_argument("cache_dir")
    ap.add_argument("--cell", type=int, default=128, help="output pixels per frame")
    ap.add_argument("--supersample", type=int, default=2,
                    help="render at N x cell then box-filter down; 1 = off")
    ap.add_argument("--density-scale", type=float, default=28.0,
                    help="extinction per unit of Mantaflow density. Its grid "
                         "peaks near 0.1 for smoke, so the raw values integrate "
                         "to almost nothing over a short ray.")
    ap.add_argument("--flame-scale", type=float, default=3.0)
    ap.add_argument("--flame-extinction", type=float, default=6.0,
                    help="how much the flame itself blocks light; 0 makes fire "
                         "purely additive and it stops occluding its own smoke")
    ap.add_argument("--arch", default="gpu", choices=["gpu", "cpu"])
    args = ap.parse_args()

    files = sorted(glob.glob(os.path.join(args.cache_dir, "f*.npz")))
    if not files:
        print("no f###.npz in %s — run scripts/manta_bake.py first" % args.cache_dir)
        return 1

    ti.init(arch=ti.gpu if args.arch == "gpu" else ti.cpu)

    first = np.load(files[0])
    rz, ry, rx = first["density"].shape
    S = args.cell * max(1, args.supersample)

    # The cell is SQUARE but the domain is not. Fit the grid inside the cell at
    # its true aspect and leave the rest transparent, instead of stretching each
    # axis to the full cell — which is what the first version did: a 34x34x96
    # grid came out smeared 2.8x horizontally, which is why the flame looked
    # wrong, why the smoke reached the cell edges and got clipped, and why the
    # height/width audit read 0.60 for a plume that is actually tall.
    aspect = rx / rz                      # width / height of the domain
    fit_w = min(1.0, aspect)              # fraction of the cell used, per axis
    fit_h = min(1.0, 1.0 / aspect)

    dens = ti.field(ti.f32, shape=(rz, ry, rx))
    flame = ti.field(ti.f32, shape=(rz, ry, rx))
    out = ti.Vector.field(3, ti.f32, shape=(S, S))   # emission, smoke, opacity

    @ti.func
    def sample(fld, gx, gy, gz):
        # Trilinear, clamped. Clamping (rather than wrapping) matters: the plume
        # touches the domain wall and a wrap would smear it to the far side.
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
    def march(ks: ti.f32, kf: ti.f32, kfe: ti.f32, fw: ti.f32, fh: ti.f32):
        for px, py in out:
            # Orthographic side view: image X is the grid's X, image Y is the
            # grid's Z (up in Blender), and the ray runs along Y.
            # Normalised cell coords, then remapped into the fitted rectangle.
            # Outside it there is no domain at all, so the ray contributes
            # nothing and the pixel stays transparent.
            u = ((px + 0.5) / S - 0.5) / fw + 0.5
            v = ((py + 0.5) / S - 0.5) / fh + 0.5
            gx = u * (rx - 1)
            gz = (1.0 - v) * (rz - 1)

            trans = 1.0
            emis = 0.0
            smoke = 0.0
            inside = (u >= 0.0) and (u <= 1.0) and (v >= 0.0) and (v <= 1.0)
            steps = ry * 2 if inside else 0
            dstep = ti.cast(ry - 1, ti.f32) / steps
            for s in range(steps):
                gy = s * dstep
                d = sample(dens, gx, gy, gz)
                f = sample(flame, gx, gy, gz)
                ext = (d * ks + f * kfe) * dstep
                emis += f * kf * trans * dstep
                smoke += d * ks * trans * dstep
                trans *= ti.exp(-ext)
                if trans < 0.004:      # the rest cannot contribute a visible level
                    break
            out[px, py] = ti.Vector([emis, smoke, 1.0 - trans])

    frames = []
    t0 = time.time()
    for i, p in enumerate(files):
        z = np.load(p)
        dens.from_numpy(np.ascontiguousarray(z["density"], np.float32))
        flame.from_numpy(np.ascontiguousarray(z["flame"], np.float32))
        march(args.density_scale, args.flame_scale, args.flame_extinction,
              fit_w, fit_h)
        # transpose: the Taichi field is indexed [px, py] (x first), but numpy
        # and PIL read axis 0 as the ROW. Without this the sheet comes out
        # rotated 90 degrees — the flame rises along the image's X axis, which
        # measured as a row-centroid that never moved (128 in every frame) while
        # the column-centroid drifted 236 -> 198.
        img = out.to_numpy().transpose(1, 0, 2)
        if args.supersample > 1:
            k = args.supersample
            img = img.reshape(args.cell, k, args.cell, k, 3).mean(axis=(1, 3))
        frames.append(img)
        if i % 8 == 0:
            print("RENDER: %d/%d  %.1fs" % (i, len(files), time.time() - t0), flush=True)

    stack = np.stack(frames)
    # One scale for the WHOLE sheet, from a high percentile. Per-frame
    # normalisation would rescale a dying flame to look as bright as a roaring
    # one, which deletes the intensity arc the flipbook exists to carry; the
    # percentile keeps a few hot voxels from crushing everything else.
    e_max = max(1e-5, float(np.percentile(stack[..., 0], 99.5)))
    s_max = max(1e-5, float(np.percentile(stack[..., 1], 99.5)))
    print("RENDER: normalising emission/%.4f smoke/%.4f" % (e_max, s_max))

    out_dir = os.path.join(args.cache_dir, "frames")
    os.makedirs(out_dir, exist_ok=True)
    for i, img in enumerate(frames):
        rgba = np.zeros((args.cell, args.cell, 4), np.float32)
        rgba[..., 0] = np.clip(img[..., 0] / e_max, 0, 1)     # emission
        rgba[..., 1] = np.clip(img[..., 1] / s_max, 0, 1)     # smoke
        rgba[..., 2] = 0.0                                     # reserved
        rgba[..., 3] = np.clip(img[..., 2], 0, 1)              # true opacity
        # Rows: image Y already runs down from the grid's top, so no flip here.
        Image.fromarray((rgba * 255).astype(np.uint8), "RGBA").save(
            os.path.join(out_dir, "f%03d.png" % (i + 1)))

    print("RENDER: %d frames -> %s  (%.1fs)" % (len(frames), out_dir, time.time() - t0))
    print("RENDER: next  python3 scripts/flipbook/pack.py %s --grid 8 "
          "--alpha-from-luma 0 --out <name>.png" % out_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
