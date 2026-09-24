import sys
import os
import time
import numpy as np

# Modify sys.argv to run ti_sim with debug prints
sys.path.insert(0, "scripts/flipbook")
import taichi as ti
import ti_sim

# Let's inspect where dens might zero out:
# Let's test running smoke_puff at res 48 for 64 frames and print frame summaries:
sys.argv = [
    "ti_sim.py",
    "smoke_puff",
    "--res", "48",
    "--frames", "64",
    "--name", "test_48_64",
    "--bfecc", "0"
]

ti_sim.main()
