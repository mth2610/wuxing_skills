#!/usr/bin/env python3
"""Build the shipping dust parcel through the smoke/fire Taichi pipeline.

The former Blender script made a white volume independently of the accepted
smoke/fire channel contract.  Dust is now the ``dust_puff`` preset in
scripts/flipbook/ti_sim.py: simulate the full 64-frame event, ray-march it,
then subsample into a 4x4 atlas.  Do not simulate only 16 frames: frame count
is physics time, while --stride is presentation sampling.

    python3 scripts/gen_dust_flipbook.py
    python3 scripts/gen_dust_flipbook.py --arch cpu  # fallback when Metal is unavailable
"""
import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FLIP = os.path.join(ROOT, "scripts", "flipbook")
CACHE = os.path.join(ROOT, "build_cache", "dust_puff_4x4_source")

def call(args, run):
    print("+", " ".join(args))
    if run:
        subprocess.run(args, cwd=ROOT, check=True)

def pipeline_python():
    """Use an interpreter with the offline solver dependencies installed."""
    candidates = [os.environ.get("WUXING_FLIPBOOK_PYTHON"), sys.executable,
                  "/usr/bin/python3"]
    seen = set()
    for exe in candidates:
        if not exe or exe in seen or not os.path.isfile(exe):
            continue
        seen.add(exe)
        if subprocess.run([exe, "-c", "import numpy, taichi"],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0:
            return exe
    raise SystemExit(
        "No Python with numpy + taichi found. Set WUXING_FLIPBOOK_PYTHON to one, "
        "or install both packages into the Python that runs this script.")

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--dry-run", action="store_true", help="print the reproducible pipeline without executing it")
    p.add_argument("--res", type=int, default=112)
    p.add_argument("--frames", type=int, default=64)
    p.add_argument("--grid", type=int, default=4)
    p.add_argument("--cell", type=int, default=256)
    p.add_argument("--seed", type=int, default=31)
    p.add_argument("--arch", choices=("gpu", "cpu"), default="gpu",
                   help="GPU is the normal fast path; use cpu only when Taichi/Metal is unavailable")
    p.add_argument("--out", default="dust_puff_4x4.png")
    a = p.parse_args()
    if a.frames != 64 or a.grid != 4:
        p.error("shipping dust is a 64-frame simulation subsampled to 4x4")
    py = pipeline_python()
    print("flipbook interpreter:", py)

    call([py, os.path.join(FLIP, "ti_sim.py"), "dust_puff",
          "--name", os.path.basename(CACHE), "--res", str(a.res),
          "--frames", str(a.frames), "--seed", str(a.seed), "--arch", a.arch], not a.dry_run)
    call([py, os.path.join(FLIP, "render.py"), CACHE,
          "--cell", str(a.cell), "--supersample", "2", "--density-scale", "2.2",
          # Dust's eroded alpha is much tighter than the raw volume used by
          # render.py --zoom auto. Frame the drawable parcel, not its invisible
          # simulation haze; 3.0 leaves margin while avoiding a tiny card.
          "--profile", "dust", "--light", "0", "--ambient", "1", "--zoom", "3.0", "--arch", a.arch], not a.dry_run)
    call([py, os.path.join(FLIP, "pack.py"), os.path.join(CACHE, "frames"),
          "--grid", str(a.grid), "--stride", "4", "--alpha-from-luma", "0",
          "--offset", "2",
          "--split", "--shape", "puff", "--out", a.out], not a.dry_run)

if __name__ == "__main__":
    main()
