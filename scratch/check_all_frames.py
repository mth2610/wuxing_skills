#!/usr/bin/env python3
import glob
import os
import sys
import numpy as np

files = sorted(glob.glob("build_cache/fire_tongue_01/f*.npz"))
print(f"Total frames: {len(files)}")

# Check extents of flame > 0.01 across all frames
min_x, max_x = 999, -999
min_y, max_y = 999, -999
min_z, max_z = 999, -999

for i, f in enumerate(files):
    z = np.load(f)
    fl = z["flame"]
    idx = np.where(fl > 0.01)
    if len(idx[0]) > 0:
        min_z = min(min_z, idx[0].min())
        max_z = max(max_z, idx[0].max())
        min_y = min(min_y, idx[1].min())
        max_y = max(max_y, idx[1].max())
        min_x = min(min_x, idx[2].min())
        max_x = max(max_x, idx[2].max())

print(f"Global bounding box for flame > 0.01 in 64x64x64 grid:")
print(f"X (width): [{min_x}..{max_x}] span = {max_x - min_x + 1} voxels (center = {(min_x + max_x)/2:.1f})")
print(f"Y (depth): [{min_y}..{max_y}] span = {max_y - min_y + 1} voxels (center = {(min_y + max_y)/2:.1f})")
print(f"Z (height): [{min_z}..{max_z}] span = {max_z - min_z + 1} voxels (center = {(min_z + max_z)/2:.1f})")
