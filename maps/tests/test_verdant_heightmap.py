#!/usr/bin/env python3
"""Verify physical relief, lake depth and cloud-rim topology of the baked island."""
from collections import deque
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'maps/worlds/verdant_path/verdant_path.c').read_text()
cliff = float(re.search(r'#define CLIFF_DEPTH ([\d.]+)f', source)[1])
with tempfile.TemporaryDirectory(prefix='verdant-heightmap-') as directory:
    output = Path(directory) / 'terrain.png'
    subprocess.run([sys.executable, str(ROOT / 'scripts/generate_island_heightmap.py'),
                    str(output), '80', '1337'], check=True)
    heights = np.asarray(Image.open(output))
    assert np.array_equal(heights, np.asarray(Image.open(
        ROOT / 'assets/heightmaps/verdant_path_island.png'))), 'asset is stale'
    x, z = round(63 / 100 * 79), round(25.5 / 75 * 79)
    decoded_depth = (1 - float(heights[z, x]) / 255) * cliff
    assert 1.05 < decoded_depth < 1.18, ('lake depth in metres', decoded_depth)
    assert heights[0, 0] < 40, 'cliff border was lost'
    ground = (heights.astype(float) / 255 - 1) * cliff
    zz, xx = np.mgrid[:80, :80]
    wx, wz = xx / 79 * 100, zz / 79 * 75
    # Exclude cliff falloff and the shoreline transition; test walkable meadow.
    meadow = ((wx > 10) & (wx < 90) & (wz > 9) & (wz < 66) &
              (((wx - 63) / 16)**2 + ((wz - 25.5) / 12)**2 > 1))
    assert -.85 < ground[meadow].min() < -.65, 'broad swales lost or too deep'
    assert ground.max() <= 0, 'terrain protrudes through the fixed Y=0 shoreline'
    slope_z, slope_x = np.gradient(ground, 75 / 79, 100 / 79)
    max_slope = np.hypot(slope_x, slope_z)[meadow].max()
    assert max_slope < .12, ('meadow has steep/aliased height steps', max_slope)
    # The cloud-sea contour must contain only the perimeter: neither swales nor
    # the lake may create a detached below-threshold region or interior mist rim.
    threshold = float(re.search(
        r'MapProp_SetCloudSeaGroundBoundary\(&s_cloudSea, &s_ground, (-[\d.]+)f', source)[1])
    below = ground < threshold
    seen = np.zeros_like(below)
    pending = deque((z, x) for z in range(80) for x in range(80)
                    if below[z, x] and (z in (0, 79) or x in (0, 79)))
    while pending:
        z, x = pending.popleft()
        if seen[z, x]:
            continue
        seen[z, x] = True
        for nz, nx in ((z-1, x), (z+1, x), (z, x-1), (z, x+1)):
            if 0 <= nz < 80 and 0 <= nx < 80 and below[nz, nx] and not seen[nz, nx]:
                pending.append((nz, nx))
    assert below.any() and np.array_equal(below, seen), 'cloud rim enters interior relief/lake'
print(f'PASS: deterministic 80x80 asset; lake {decoded_depth:.3f} m; '
      f'meadow slope {max_slope:.3f}; cloud contour remains a perimeter')
