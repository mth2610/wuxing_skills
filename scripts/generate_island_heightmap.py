"""Generate a grayscale heightmap for the "floating island" map motif
(MAP_API.md): white = flat walkable plateau, black = cliff edge sinking
down. Simple rectangle, flat interior — the plateau covers almost the
whole map, with only a thin, mildly-jagged cliff strip right at the
border (not a big rounded/blobby falloff zone). Consumed by
MapProp_CreateGroundHeightmap (maps/toolkit/).

Usage: python3 scripts/generate_island_heightmap.py [out_path] [size] [seed]
Defaults match verdant_path's current island: 128x128, seed 1337.
"""
import sys
import numpy as np
from PIL import Image, ImageFilter

out_path = sys.argv[1] if len(sys.argv) > 1 else "assets/heightmaps/verdant_path_island.png"
size = int(sys.argv[2]) if len(sys.argv) > 2 else 128
seed = int(sys.argv[3]) if len(sys.argv) > 3 else 1337

yy, xx = np.mgrid[0:size, 0:size]
nx = (xx / (size - 1)) * 2.0 - 1.0
ny = (yy / (size - 1)) * 2.0 - 1.0

# Mostly-rectangular distance (Chebyshev) with only a touch of rounding so
# corners aren't perfectly sharp mitred edges.
dist_rect = np.maximum(np.abs(nx), np.abs(ny))
dist_round = np.sqrt(nx ** 2 + ny ** 2) / np.sqrt(2.0)
dist = dist_rect * 0.9 + dist_round * 0.1

# Smoothed noise perturbs WHERE the cliff edge falls, for a mildly jagged
# (not perfectly straight, not wildly lumpy) border.
rng = np.random.default_rng(seed)
noise = rng.random((size, size)).astype(np.float32)
noise_img = Image.fromarray((noise * 255).astype(np.uint8))
noise_img = noise_img.filter(ImageFilter.GaussianBlur(radius=size / 40.0))
noise = np.asarray(noise_img).astype(np.float32) / 255.0
edge_jitter = (noise - 0.5) * 0.06

# Flat interior out to 90% of half-width/half-depth; only the outer 10%
# band slopes down to the cliff bottom.
plateau_edge = 0.90
falloff_width = 0.10
local_edge = plateau_edge + edge_jitter

height = 1.0 - np.clip((dist - local_edge) / falloff_width, 0.0, 1.0)
height = np.clip(height, 0.0, 1.0)

img = (height * 255).astype(np.uint8)
Image.fromarray(img, mode="L").save(out_path)
print(f"Wrote {out_path} ({size}x{size})")
