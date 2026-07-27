#!/usr/bin/env python3
"""Pack rendered frames into a flipbook atlas, and AUDIT it before it ships.

    python3 scripts/pack_flipbook.py build_cache_manta/frames --grid 8 \
        --out fire_atlas_manta_8x8.png

Separate from the Blender script on purpose: packing and measuring need PIL and
numpy, which live in the system Python, while the bake needs Blender's bundled
interpreter. Splitting them also means a re-pack (different normalisation, a
different grid) costs seconds instead of a re-bake.

THE AUDIT IS THE POINT. E4's fire sheet reached the engine and only then was
measured at 4.1% coverage and height/width 1.00 — a spherical puff. Those two
numbers are checked here, against the smoke sheet that works (19.6%) and against
the 1.3 ratio below which a flame is not a flame.
"""

import argparse
import glob
import os
import sys

import numpy as np
from PIL import Image

OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "..", "assets", "textures")


def audit(sheet, grid, cell):
    arr = np.asarray(sheet).astype(np.float32) / 255.0
    a = arr[..., 3]
    covs, ratios = [], []
    for f in range(grid * grid):
        r, c = divmod(f, grid)
        sub = a[r * cell:(r + 1) * cell, c * cell:(c + 1) * cell]
        m = sub > 0.06
        if m.sum() < 32:
            continue
        covs.append(m.mean())
        ys, xs = np.nonzero(m)
        ratios.append((ys.max() - ys.min() + 1) / max(1, (xs.max() - xs.min() + 1)))
    # Channel coverage, reported separately. On a multi-channel sheet (R = flame,
    # G = smoke) the alpha figure is the union of both populations, so comparing
    # it against the old single-channel 19.6% target is meaningless — that number
    # was measured on a sheet where alpha WAS the smoke.
    flame_cov = float((arr[..., 0] > 0.25).mean())
    smoke_cov = float((arr[..., 1] > 0.25).mean())
    return (float(np.mean(covs)) if covs else 0.0,
            float(np.mean(ratios)) if ratios else 0.0,
            len(covs), flame_cov, smoke_cov)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("frames_dir")
    ap.add_argument("--grid", type=int, default=8)
    ap.add_argument("--out", default="fire_atlas_manta_8x8.png")
    ap.add_argument("--cell", type=int, default=None)
    ap.add_argument("--alpha-from-luma", type=float, default=1.0,
                    help="fold RGB luminance into alpha (1 = on). Emission-only "
                         "volumes render bright but nearly TRANSPARENT: Eevee's "
                         "alpha comes from extinction, not from emission, so a "
                         "flame measures rgb 255 / alpha 10. An additive flipbook "
                         "uses alpha as its MASK, so luminance is the right "
                         "source for it.")
    args = ap.parse_args()

    files = sorted(glob.glob(os.path.join(args.frames_dir, "*.png")))
    want = args.grid * args.grid
    if len(files) < want:
        print("only %d frames in %s, need %d" % (len(files), args.frames_dir, want))
        return 1
    files = files[:want]

    first = Image.open(files[0]).convert("RGBA")
    cell = args.cell or first.size[0]
    sheet = Image.new("RGBA", (args.grid * cell, args.grid * cell), (0, 0, 0, 0))
    for i, p in enumerate(files):
        im = Image.open(p).convert("RGBA")
        if im.size != (cell, cell):
            im = im.resize((cell, cell), Image.LANCZOS)
        if args.alpha_from_luma > 0.0:
            arr = np.asarray(im).astype(np.float32)
            luma = (0.299 * arr[..., 0] + 0.587 * arr[..., 1] + 0.114 * arr[..., 2])
            arr[..., 3] = np.clip(np.maximum(arr[..., 3], luma * args.alpha_from_luma), 0, 255)
            im = Image.fromarray(arr.astype(np.uint8), "RGBA")
        r, c = divmod(i, args.grid)
        sheet.paste(im, (c * cell, r * cell))

    path = os.path.join(OUT_DIR, args.out)
    sheet.save(path)
    cov, ratio, live, fcov, scov = audit(sheet, args.grid, cell)
    print("wrote %s  %dx%d  (%d non-empty frames)" % (path, sheet.size[0], sheet.size[1], live))
    print("  cell coverage %.1f%%   (smoke sheet, which works: 19.6%%)" % (cov * 100))
    print("  height/width  %.2f    (must exceed 1.30 — flame, not puff)" % ratio)
    if fcov > 0.001 or scov > 0.001:
        print("  channels: flame %.1f%% of sheet, smoke %.1f%% "
              "(R and G are separate populations — see ti_render.py)"
              % (fcov * 100, scov * 100))
    if ratio < 1.3:
        print("  WARNING: still puff-shaped — raise --buoyancy or make the domain taller.")
    if live < want * 0.8:
        print("  WARNING: %d/%d frames are empty — bake or camera framing is off."
              % (want - live, want))
    return 0


if __name__ == "__main__":
    sys.exit(main())
