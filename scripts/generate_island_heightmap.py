"""Generate a grayscale heightmap for the "floating island" map motif
(MAP_API.md): white = walkable plateau crest, black = cliff edge sinking
down. Includes:
1. Natural gentle meadow undulation (rolling swales & knolls, ~0.20m depth)
2. Seamlessly sunken concave lake basin (depth 1.15m at center, matching Water_EdgeScale)
3. Smooth waterline fade around the lake rim so the water surface meets the shore at Y=0.0m
4. Mildly-jagged cliff edge strip around the outer 10% perimeter

Usage: python3 scripts/generate_island_heightmap.py [out_path] [size] [seed]
Defaults match verdant_path: 80x80, seed 1337.
"""
import sys
import numpy as np
from PIL import Image, ImageFilter

out_path = sys.argv[1] if len(sys.argv) > 1 else "assets/heightmaps/verdant_path_island.png"
size = int(sys.argv[2]) if len(sys.argv) > 2 else 80
seed = int(sys.argv[3]) if len(sys.argv) > 3 else 1337

MAP_WIDTH = 100.0
MAP_DEPTH = 75.0
CLIFF_DEPTH = 3.6
LAKE_CENTER_X = 63.0
LAKE_CENTER_Z = 25.5
LAKE_RADIUS_X = 10.5
LAKE_RADIUS_Z = 7.4
LAKE_MAX_DEPTH = 1.15
LAKE_SEED = 9173

yy, xx = np.mgrid[0:size, 0:size]
world_x = (xx / (size - 1)) * MAP_WIDTH
world_z = (yy / (size - 1)) * MAP_DEPTH

# 1. Island boundary & cliff falloff
nx = (xx / (size - 1)) * 2.0 - 1.0
ny = (yy / (size - 1)) * 2.0 - 1.0

# Mostly-rectangular distance (Chebyshev) with subtle rounding
dist_rect = np.maximum(np.abs(nx), np.abs(ny))
dist_round = np.sqrt(nx ** 2 + ny ** 2) / np.sqrt(2.0)
dist = dist_rect * 0.9 + dist_round * 0.1

# Smoothed noise for mildly jagged cliff border
rng = np.random.default_rng(seed)
noise = rng.random((size, size)).astype(np.float32)
noise_img = Image.fromarray((noise * 255).astype(np.uint8))
noise_img = noise_img.filter(ImageFilter.GaussianBlur(radius=size / 40.0))
noise = np.asarray(noise_img).astype(np.float32) / 255.0
edge_jitter = (noise - 0.5) * 0.06

plateau_edge = 0.90
falloff_width = 0.10
local_edge = plateau_edge + edge_jitter

cliff_drop = np.clip((dist - local_edge) / falloff_width, 0.0, 1.0)
cliff_mask = np.clip((local_edge - dist) / 0.12, 0.0, 1.0)

# 2. Lake depression matching Water_EdgeScale harmonic shoreline
dx = world_x - LAKE_CENTER_X
dz = world_z - LAKE_CENTER_Z
angle = np.arctan2(dz, dx)
seedPhase = (LAKE_SEED & 1023) * 0.0173
shorelineNoise = (np.sin(angle * 2.0 + seedPhase) * 0.095 +
                  np.cos(angle * 3.0 - seedPhase * 0.70) * 0.065 +
                  np.sin(angle * 5.0 + 1.2) * 0.038 +
                  np.sin(angle * 11.0 - seedPhase * 0.40) * 0.018)
edge = 1.0 + shorelineNoise
rx = LAKE_RADIUS_X * edge
rz = LAKE_RADIUS_Z * edge
r_lake = np.sqrt((dx / rx)**2 + (dz / rz)**2)
lake_depth = np.where(r_lake < 1.0, LAKE_MAX_DEPTH * (1.0 - np.clip(r_lake, 0.0, 1.0)**1.6), 0.0)

# 3. Meadow undulating swales (relative depth: 0.0 to 0.20m below crests)
h_macro = np.sin(world_x * 0.065 + world_z * 0.048) * 0.55 + np.sin(world_x * -0.048 + world_z * 0.082 + 1.2) * 0.45
h_med = np.sin(world_x * 0.14 - world_z * 0.11 + 0.8) * 0.55 + np.sin(world_x * 0.09 + world_z * 0.18 + 2.1) * 0.45
raw_undulation = h_macro * 0.7 + h_med * 0.3
swale_depth = (1.0 - (raw_undulation * 0.5 + 0.5)) * 0.20

# Fade meadow undulation at lake rim and cliff
lake_fade = np.clip((r_lake - 1.02) / 0.30, 0.0, 1.0)
lake_fade = lake_fade * lake_fade * (3.0 - 2.0 * lake_fade) # smoothstep
meadow_dip = swale_depth * lake_fade * cliff_mask

# Total depth below 0.0m
total_depth = lake_depth + meadow_dip

# Convert to normalized height [0.0, 1.0] where 1.0 = 0.0m, 0.0 = -CLIFF_DEPTH
plateau_height = 1.0 - (total_depth / CLIFF_DEPTH)
final_height = plateau_height * (1.0 - cliff_drop)
final_height = np.clip(final_height, 0.0, 1.0)

img = (final_height * 255.0 + 0.5).astype(np.uint8)
out_img = Image.fromarray(img, mode="L").filter(ImageFilter.GaussianBlur(radius=0.6))
out_img.save(out_path)
print(f"Wrote {out_path} ({size}x{size}) with lake basin and undulating terrain")


