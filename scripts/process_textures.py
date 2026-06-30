#!/usr/bin/env python3
"""
General-purpose post-processor for raw AI-generated textures.

Not tied to any one texture category (decals, sprites, masks, ...) — pass
--mode, --prefix, --dst per invocation to target whatever batch you're
currently processing. Designed to be reused across the project any time a
new batch of AI-generated source art needs to become engine-ready assets.

Modes:
  luma-alpha   White/gray art on black background -> RGBA with the art's
               luminosity converted to the alpha channel and RGB forced to
               pure white, so the engine can tint it via Color at runtime
               (e.g. DecalSystem_Add, SpawnGroundDecal in core/decal_system.h).
               Typical for decals, masks, glow sprites.
  resize-only  Just resize/re-export, no alpha manipulation. Typical for
               opaque textures (skyboxes, tileable surface diffuse maps).
  passthrough  Copy + resize without touching color/alpha at all (e.g. already
               has a clean alpha channel from the source).

Pipeline per file:
  1. Load raw image from --src.
  2. Apply --mode transform.
  3. Resize to a square --size (skipped if already that size).
  4. Save as "<prefix><name>.png" into --dst, creating it if missing.

Usage:
  python3 scripts/process_textures.py --mode luma-alpha \
      --src assets/raw_textures --dst assets/textures/decals --prefix decal_

  python3 scripts/process_textures.py --mode luma-alpha --size 1024 \
      --only splash_ring,frost_ring

  python3 scripts/process_textures.py --mode luma-alpha --threshold 8   # cut faint background noise

  python3 scripts/process_textures.py --mode resize-only \
      --src assets/raw_textures/skybox --dst assets/textures --prefix sky_

Re-run safely any time new raw textures are dropped into --src; existing
output files are simply overwritten.
"""

import argparse
import shutil
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("Missing dependency: pip install Pillow")

DEFAULT_SRC = "assets/raw_textures"
DEFAULT_DST = "assets/textures"
DEFAULT_SIZE = 512
DEFAULT_PREFIX = ""


def transform_luma_alpha(img: Image.Image, size: int, threshold: int) -> Image.Image:
    img = img.convert("RGB")

    # Luminosity -> alpha. Pillow's "L" convert uses standard luma weights.
    alpha = img.convert("L")
    if threshold > 0:
        alpha = alpha.point(lambda p: 0 if p < threshold else p)

    if img.size != (size, size):
        alpha = alpha.resize((size, size), Image.LANCZOS)

    white_rgb = Image.new("RGB", (size, size), (255, 255, 255))
    return Image.merge("RGBA", (*white_rgb.split(), alpha))


def transform_resize_only(img: Image.Image, size: int, threshold: int) -> Image.Image:
    img = img.convert("RGB")
    if img.size != (size, size):
        img = img.resize((size, size), Image.LANCZOS)
    return img


def transform_passthrough(img: Image.Image, size: int, threshold: int) -> Image.Image:
    mode = "RGBA" if img.mode in ("RGBA", "LA") or "transparency" in img.info else "RGB"
    img = img.convert(mode)
    if img.size != (size, size):
        img = img.resize((size, size), Image.LANCZOS)
    return img


MODES = {
    "luma-alpha": transform_luma_alpha,
    "resize-only": transform_resize_only,
    "passthrough": transform_passthrough,
}


def process_one(src_path: Path, dst_path: Path, mode: str, size: int, threshold: int) -> None:
    img = Image.open(src_path)
    out = MODES[mode](img, size, threshold)
    dst_path.parent.mkdir(parents=True, exist_ok=True)
    out.save(dst_path, "PNG", optimize=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--mode", choices=MODES.keys(), required=True, help="Transform to apply (see module docstring)")
    parser.add_argument("--src", default=DEFAULT_SRC, help=f"Raw texture source dir (default: {DEFAULT_SRC})")
    parser.add_argument("--dst", default=DEFAULT_DST, help=f"Processed output dir (default: {DEFAULT_DST})")
    parser.add_argument("--size", type=int, default=DEFAULT_SIZE, help=f"Output square size (default: {DEFAULT_SIZE})")
    parser.add_argument("--prefix", default=DEFAULT_PREFIX, help="Output filename prefix, e.g. 'decal_' (default: none)")
    parser.add_argument("--threshold", type=int, default=0, help="luma-alpha only: alpha cutoff [0-255] to strip faint background noise")
    parser.add_argument("--only", default=None, help="Comma-separated list of basenames (no extension) to process, skip the rest")
    args = parser.parse_args()

    src_dir = Path(args.src)
    dst_dir = Path(args.dst)

    if not src_dir.is_dir():
        sys.exit(f"Source dir not found: {src_dir}")

    only = {n.strip() for n in args.only.split(",")} if args.only else None

    sources = sorted(p for p in src_dir.glob("*.png") if p.is_file())
    if not sources:
        sys.exit(f"No .png files found in {src_dir}")

    processed = 0
    for src_path in sources:
        stem = src_path.stem
        if only and stem not in only:
            continue

        out_name = stem if stem.startswith(args.prefix) else f"{args.prefix}{stem}"
        dst_path = dst_dir / f"{out_name}.png"

        process_one(src_path, dst_path, args.mode, args.size, args.threshold)
        print(f"  {src_path.name:<28} -> {dst_path}")
        processed += 1

    print(f"\nDone: {processed} texture(s) processed [{args.mode}] -> {dst_dir}/")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
