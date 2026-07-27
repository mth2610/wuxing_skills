#!/usr/bin/env python3
"""One command for the whole flipbook pipeline: bake → render → pack → audit.

    python3 scripts/flipbook/make.py fire --quick     # ~15 s end to end
    python3 scripts/flipbook/make.py fire             # full sheet
    python3 scripts/flipbook/make.py explosion --quick --out explosion_8x8.png

    # Skip the slow half when only the LOOK changed — the grids are still there:
    python3 scripts/flipbook/make.py fire --no-bake --density-scale 6

The three stages stay usable on their own (bake.py / render.py / pack.py); this
is the front door. `--no-bake` is the one that matters in practice: baking is
the slow step and nothing about the sheet's appearance depends on it, so a look
iteration is seconds, not minutes.
"""

import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
CACHE = os.path.join(ROOT, "build_cache")

BLENDER_CANDIDATES = [
    "/Applications/Blender.app/Contents/MacOS/Blender",
    "blender",
]


def find_blender():
    for c in BLENDER_CANDIDATES:
        if os.path.exists(c):
            return c
    from shutil import which
    return which("blender")


def run(cmd, label):
    print("\n=== %s ===\n$ %s" % (label, " ".join(cmd)), flush=True)
    r = subprocess.run(cmd, cwd=ROOT)
    if r.returncode != 0:
        print("%s FAILED (exit %d)" % (label, r.returncode))
        sys.exit(r.returncode)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("preset")
    ap.add_argument("--name", default=None, help="cache dir name (default: preset)")
    ap.add_argument("--out", default=None, help="atlas filename in assets/textures")
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--frames", type=int, default=64)
    ap.add_argument("--grid", type=int, default=8)
    ap.add_argument("--res", type=int, default=None, help="sim resolution")
    ap.add_argument("--cell", type=int, default=None, help="pixels per atlas cell")
    ap.add_argument("--supersample", type=int, default=2)
    ap.add_argument("--density-scale", type=float, default=9.0)
    ap.add_argument("--flame-scale", type=float, default=3.0)
    ap.add_argument("--no-bake", action="store_true",
                    help="reuse the grids already in build_cache/<name>")
    args = ap.parse_args()

    name = args.name or args.preset
    out = args.out or ("%s_atlas_%dx%d.png" % (args.preset, args.grid, args.grid))
    cell = args.cell or (128 if args.quick else 256)
    cache_dir = os.path.join(CACHE, name)

    if not args.no_bake:
        blender = find_blender()
        if not blender:
            print("Blender not found. Edit BLENDER_CANDIDATES in make.py, or pass "
                  "--no-bake to reuse existing grids.")
            return 1
        cmd = [blender, "--background", "--python",
               os.path.join(HERE, "bake.py"), "--",
               "--preset", args.preset, "--name", name,
               "--frames", str(args.frames)]
        if args.quick:
            cmd.append("--quick")
        if args.res:
            cmd += ["--res", str(args.res)]
        run(cmd, "1/3 BAKE (Mantaflow)")
    elif not os.path.isdir(cache_dir):
        print("--no-bake, but %s has no grids" % cache_dir)
        return 1

    run([sys.executable, os.path.join(HERE, "render.py"), cache_dir,
         "--cell", str(cell), "--supersample", str(args.supersample),
         "--density-scale", str(args.density_scale),
         "--flame-scale", str(args.flame_scale)], "2/3 RENDER (Taichi)")

    run([sys.executable, os.path.join(HERE, "pack.py"),
         os.path.join(cache_dir, "frames"), "--grid", str(args.grid),
         "--alpha-from-luma", "0", "--out", out], "3/3 PACK + AUDIT")

    print("\nassets/textures/%s" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
