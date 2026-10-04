#!/usr/bin/env python3
"""Verify the baked lake's physical depth against the runtime mesh range."""
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
print(f'PASS: deterministic asset; lake depth {decoded_depth:.3f} m; cliff {cliff:g} m')
