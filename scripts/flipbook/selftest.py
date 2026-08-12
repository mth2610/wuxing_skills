#!/usr/bin/env python3
"""Renderer self-test — no Blender, no bake, ~5 seconds.

    python3 scripts/flipbook/selftest.py

Builds a SYNTHETIC grid whose shape is known exactly (a tapering column, 34 wide
and 96 tall — the same aspect a real fire bake produces), renders it, and asserts
the sheet comes back with that shape.

Why this exists: the renderer shipped two orientation bugs that the audit could
not catch, because the audit measures the sheet and the sheet was wrong in the
same direction as the measurement.
  1. frames written transposed (Taichi indexes [x, y], PIL reads axis 0 as rows)
     — the audit reported height/width 2.13 for a flame that was WIDER than tall;
  2. each axis stretched to the full square cell regardless of the domain's
     aspect — a 34x34x96 grid came out smeared 2.8x horizontally, which is what
     made the fire "not look like fire", pushed the smoke into the cell edges,
     and clipped it there.
Both are invisible on a real bake (nobody knows what the flame SHOULD look like)
and obvious on a synthetic one (we authored the shape).
"""

import os
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
PROBE = os.path.join(ROOT, "build_cache", "_selftest")

RZ, RY, RX = 96, 34, 34          # tall domain, exactly like the fire preset
FRAMES = 16


def write_grids():
    os.makedirs(PROBE, exist_ok=True)
    zz, yy, xx = np.meshgrid(np.arange(RZ), np.arange(RY), np.arange(RX), indexing="ij")
    for f in range(FRAMES):
        top = (0.3 + 0.65 * f / max(1, FRAMES - 1)) * RZ
        r = (1.0 - 0.6 * (zz / max(top, 1))) * 6.0
        d = np.sqrt((xx - RX / 2) ** 2 + (yy - RY / 2) ** 2)
        col = np.clip(1.0 - d / np.maximum(r, 1e-3), 0, 1) * (zz < top)
        np.savez_compressed(
            os.path.join(PROBE, "f%03d.npz" % (f + 1)),
            density=(col * 0.5 * np.clip(zz / max(top, 1), 0, 1)).astype(np.float16),
            flame=(col * np.clip(1.0 - zz / max(top, 1) * 1.4, 0, 1)).astype(np.float16),
            temperature=col.astype(np.float16),
            res=np.array([RX, RY, RZ], np.int32))


def check(sheet_path, grid, cell):
    from PIL import Image
    a = np.asarray(Image.open(sheet_path)).astype(np.float32) / 255.0
    ok = True
    # Take the last frame: the column is at its tallest there.
    r, c = divmod(FRAMES - 1, grid)
    sub = a[r * cell:(r + 1) * cell, c * cell:(c + 1) * cell]
    m = sub[..., 3] > 0.06
    ys, xs = np.nonzero(m)
    h = ys.max() - ys.min() + 1
    w = xs.max() - xs.min() + 1
    ratio = h / max(1, w)
    print("  tall column renders at height/width %.2f (want > 2.0)" % ratio)
    ok &= ratio > 2.0

    # It must not touch the left/right edges: the domain is narrower than the
    # square cell, so a correct fit leaves margins.
    margin = min(xs.min(), cell - 1 - xs.max())
    print("  horizontal margin %d px (want > 4 — no clipping at the cell edge)" % margin)
    ok &= margin > 4

    # The column rises: the flame centroid must move UP the rows across frames.
    def centroid(fi):
        rr, cc = divmod(fi, grid)
        s = a[rr * cell:(rr + 1) * cell, cc * cell:(cc + 1) * cell]
        mm = s[..., 0] > 0.1
        return np.nonzero(mm)[0].mean() if mm.sum() > 20 else None
    c0, c1 = centroid(1), centroid(FRAMES - 2)
    print("  flame row-centroid %.0f -> %.0f (must DECREASE: 0 is the top)" % (c0, c1))
    ok &= c1 < c0 - 2
    return ok


def main():
    write_grids()
    cell = 128
    for cmd, label in (
        ([sys.executable, os.path.join(HERE, "render.py"), PROBE,
          "--cell", str(cell), "--supersample", "2", "--arch", "cpu"], "render"),
        ([sys.executable, os.path.join(HERE, "pack.py"),
          os.path.join(PROBE, "frames"), "--grid", "4",
          "--alpha-from-luma", "0", "--out", "_selftest.png"], "pack"),
    ):
        if subprocess.run(cmd, cwd=ROOT).returncode != 0:
            print("SELFTEST: %s stage failed" % label)
            return 1

    sheet = os.path.join(ROOT, "assets", "textures", "_selftest.png")
    ok = check(sheet, 4, cell)
    os.remove(sheet)                      # a test must not leave assets behind
    print("SELFTEST: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
