"""Generate a seamlessly-tileable grayscale cloud noise texture for
maps/toolkit/shaders/cloud_sea.fs — replaces per-pixel sin()-based FBM math
with a couple of texture fetches (much cheaper on GPU fill-rate for a plane
that often covers most of the screen). Generated via FFT-shaped noise (1/f^beta
spectral falloff), which is inherently periodic/tileable since FFT assumes
periodic boundary conditions — no manual seam-blending needed.

Usage: python3 scripts/generate_cloud_noise.py [out_path] [size] [seed] [beta]
"""
import sys
import numpy as np
from PIL import Image

out_path = sys.argv[1] if len(sys.argv) > 1 else "assets/textures/cloud_noise.png"
size = int(sys.argv[2]) if len(sys.argv) > 2 else 512
seed = int(sys.argv[3]) if len(sys.argv) > 3 else 2024
beta = float(sys.argv[4]) if len(sys.argv) > 4 else 2.2  # higher = smoother/puffier

rng = np.random.default_rng(seed)
noise = rng.normal(size=(size, size))

F = np.fft.fft2(noise)
fy = np.fft.fftfreq(size).reshape(-1, 1)
fx = np.fft.fftfreq(size).reshape(1, -1)
freq = np.sqrt(fx ** 2 + fy ** 2)
freq[0, 0] = 1e-6  # avoid divide-by-zero at DC component

amplitude = 1.0 / (freq ** (beta / 2.0))
result = np.fft.ifft2(F * amplitude).real

result -= result.min()
result /= result.max()

img = (result * 255).astype(np.uint8)
Image.fromarray(img, mode="L").save(out_path)
print(f"Wrote {out_path} ({size}x{size}, beta={beta})")
