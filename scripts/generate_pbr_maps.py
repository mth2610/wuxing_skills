#!/usr/bin/env python3
"""
Derive normal + roughness maps for a ground/prop texture that only has a
diffuse (and optionally a grayscale height/bump image) — used to complete
the PBR-lite set (diffuse+normal+roughness) the prop_lit material expects,
without needing a second AI-gen pass per map.

Normal map generation: seamless (wrap-around) Sobel-style gradient of a
grayscale height source, encoded as a standard tangent-space normal map
(flat area -> RGB ~= (128,128,255)). Use --height if you already have a
dedicated height/bump image (e.g. an AI-gen result that came out as a height
map instead of a real normal map); otherwise --diffuse's luminance is used
as a pseudo-height source (works reasonably when the diffuse has baked-in
fake AO/highlights from crevices, common in AI-gen stone/rock textures).

Roughness generation: diffuse luminance remapped to a [--rough-min,
--rough-max] range (darker crevices = rougher by default; use
--invert-roughness to flip).

Usage:
  # Stone path: existing "normal" file is actually a height map -> convert
  # it properly, and derive roughness from the diffuse.
  python3 scripts/generate_pbr_maps.py \\
      --height assets/textures/stone_path_normal.png \\
      --diffuse assets/textures/stone_path_diffuse.png \\
      --out-prefix assets/textures/stone_path

  # Grass/rock: no height source at all -> pseudo-height from diffuse luma.
  python3 scripts/generate_pbr_maps.py \\
      --diffuse assets/textures/grass_ground_diffuse.png \\
      --out-prefix assets/textures/grass_ground --rough-min 0.5 --rough-max 0.95

  python3 scripts/generate_pbr_maps.py \\
      --diffuse assets/textures/rock_diffuse.png \\
      --out-prefix assets/textures/rock --strength 6.0

Re-run safely; output files are overwritten each time.
"""

import argparse
import sys

try:
    import numpy as np
    from PIL import Image
except ImportError:
    sys.exit("Missing dependency: pip install Pillow numpy")


def load_gray(path: str) -> np.ndarray:
    img = Image.open(path).convert("L")
    return np.asarray(img, dtype=np.float64) / 255.0


def height_to_normal(height: np.ndarray, strength: float) -> np.ndarray:
    # Wrap-around (seamless) central-difference gradient so tiled textures
    # don't show a normal-map seam at the edges.
    h_right = np.roll(height, -1, axis=1)
    h_left = np.roll(height, 1, axis=1)
    dx = (h_right - h_left) * 0.5 * strength

    h_down = np.roll(height, -1, axis=0)
    h_up = np.roll(height, 1, axis=0)
    dy = (h_down - h_up) * 0.5 * strength

    nx = -dx
    ny = -dy
    nz = np.ones_like(height)

    length = np.sqrt(nx * nx + ny * ny + nz * nz)
    nx /= length
    ny /= length
    nz /= length

    r = ((nx + 1.0) * 0.5 * 255.0).clip(0, 255).astype(np.uint8)
    g = ((ny + 1.0) * 0.5 * 255.0).clip(0, 255).astype(np.uint8)
    b = ((nz + 1.0) * 0.5 * 255.0).clip(0, 255).astype(np.uint8)
    return np.stack([r, g, b], axis=-1)


def luminance_to_roughness(gray: np.ndarray, rough_min: float, rough_max: float, invert: bool) -> np.ndarray:
    v = 1.0 - gray if invert else gray
    rough = rough_min + v * (rough_max - rough_min)
    return (rough * 255.0).clip(0, 255).astype(np.uint8)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--height", default=None, help="Grayscale height/bump source for the normal map (optional)")
    parser.add_argument("--diffuse", default=None, help="Diffuse texture; used as roughness source, and as normal-map pseudo-height if --height is omitted")
    parser.add_argument("--out-prefix", required=True, help="Writes <prefix>_normal.png and <prefix>_roughness.png")
    parser.add_argument("--strength", type=float, default=4.0, help="Normal map bump strength (default: 4.0)")
    parser.add_argument("--rough-min", type=float, default=0.35, help="Roughness value [0-1] for the brightest areas (default: 0.35)")
    parser.add_argument("--rough-max", type=float, default=0.9, help="Roughness value [0-1] for the darkest areas (default: 0.9)")
    parser.add_argument("--invert-roughness", action="store_true", help="Flip which areas read as rough vs smooth")
    parser.add_argument("--no-normal", action="store_true", help="Skip normal map generation")
    parser.add_argument("--no-roughness", action="store_true", help="Skip roughness map generation")
    args = parser.parse_args()

    if not args.height and not args.diffuse:
        sys.exit("Provide at least one of --height or --diffuse")

    if not args.no_normal:
        height_src = args.height or args.diffuse
        height = load_gray(height_src)
        normal_rgb = height_to_normal(height, args.strength)
        normal_path = f"{args.out_prefix}_normal.png"
        Image.fromarray(normal_rgb).save(normal_path, "PNG", optimize=True)
        print(f"  normal    <- {height_src:<45} -> {normal_path}")

    if not args.no_roughness:
        if not args.diffuse:
            sys.exit("--diffuse is required for roughness generation (or pass --no-roughness)")
        gray = load_gray(args.diffuse)
        rough = luminance_to_roughness(gray, args.rough_min, args.rough_max, args.invert_roughness)
        rough_path = f"{args.out_prefix}_roughness.png"
        Image.fromarray(rough).save(rough_path, "PNG", optimize=True)
        print(f"  roughness <- {args.diffuse:<45} -> {rough_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
