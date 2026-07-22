"""Generate a smoke/dust particle sprite with a LOBED silhouette.

Đợt E / F2. The engine's stock particle texture is a plain radial gradient, and
that is the ceiling under every soft VFX in the project: a radial gradient has no
outline. Lighting a featureless blob yields a lit featureless blob, and stacking
28 of them averages back to a uniform smear — which is exactly what the F1
verification round ran into once the shading maths was proven correct.

ELDEN_VFX_SPEC.md §0.1b cause 3: real smoke's beauty lives in its SILHOUETTE —
rounded, cauliflower-like billow lobes. FBM thresholding cannot produce that; it
gives a statistically-even fuzzy edge. This generates the shape explicitly:

  1. Scatter N overlapping metaball discs of varying radius inside a disc.
  2. Sum their smooth falloffs into a density field and threshold it — the union
     of circles is what makes the cauliflower outline, not noise.
  3. Warp the whole field with low-frequency FFT noise so the lobes are irregular
     rather than obviously circular. LOW frequency on purpose: high-frequency
     warp shreds the outline back into fuzz, undoing the entire point.
  4. Erode the alpha inward from the silhouette so the sprite reads as a solid
     puff with a soft rim, not as a gradient blob.

Output is white RGB with the shape in ALPHA — the particle system tints per
particle, and particle_lit.fs reads alpha for the derivative fallback path.

Usage: python3 scripts/generate_smoke_sprite.py [out_path] [size] [seed] [lobes]
"""
import sys
import numpy as np
from PIL import Image

out_path = sys.argv[1] if len(sys.argv) > 1 else "assets/textures/smoke_puff_soft.png"
size     = int(sys.argv[2]) if len(sys.argv) > 2 else 256
seed     = int(sys.argv[3]) if len(sys.argv) > 3 else 7
lobes    = int(sys.argv[4]) if len(sys.argv) > 4 else 14

rng = np.random.default_rng(seed)

yy, xx = np.mgrid[0:size, 0:size].astype(np.float32)
u = (xx + 0.5) / size * 2.0 - 1.0
v = (yy + 0.5) / size * 2.0 - 1.0

# ── 1-2. Metaball union — the source of the lobed outline ────────────────────
density = np.zeros((size, size), dtype=np.float32)
for i in range(lobes):
    # Bias lobe centres toward the middle so the puff stays compact; the few
    # that land near the edge are what break the circular silhouette. Centres
    # kept fairly tight and radii fairly large on purpose: heavy OVERLAP is what
    # reads as cauliflower. Scattered lobes with little overlap produce a hooked,
    # amorphous shape that reads as a splat rather than as smoke.
    ang = rng.uniform(0.0, 2.0 * np.pi)
    rad = np.sqrt(rng.uniform(0.0, 1.0)) * 0.26
    cx, cy = np.cos(ang) * rad, np.sin(ang) * rad
    r = rng.uniform(0.30, 0.44)
    d2 = (u - cx) ** 2 + (v - cy) ** 2
    # Smooth polynomial falloff (finite support) rather than a Gaussian: it
    # reaches exactly zero, so lobes have a real edge to union together.
    t = np.clip(1.0 - d2 / (r * r), 0.0, 1.0)
    density += t * t * t

# ── 3. Low-frequency warp so lobes are irregular, not obviously circular ─────
def fft_noise(n, beta, rs):
    w = rs.normal(size=(n, n))
    f = np.fft.fftshift(np.fft.fft2(w))
    cy_, cx_ = n // 2, n // 2
    ry, rx = np.mgrid[0:n, 0:n]
    radius = np.sqrt((ry - cy_) ** 2 + (rx - cx_) ** 2)
    radius[cy_, cx_] = 1.0
    f /= radius ** beta
    out = np.real(np.fft.ifft2(np.fft.ifftshift(f)))
    out -= out.min()
    return out / (out.max() + 1e-9)

warp_x = fft_noise(size, 3.0, np.random.default_rng(seed + 1)) - 0.5
warp_y = fft_noise(size, 3.0, np.random.default_rng(seed + 2)) - 0.5
amp = 0.09 * size
sx = np.clip(xx + warp_x * amp, 0, size - 1).astype(np.int32)
sy = np.clip(yy + warp_y * amp, 0, size - 1).astype(np.int32)
density = density[sy, sx]

# ── 4. Threshold to a silhouette, then erode inward for a soft rim ───────────
# The threshold IS the silhouette decision — above it is puff, below is nothing.
thresh = np.percentile(density, 52.0)
sil = np.clip((density - thresh) / (density.max() - thresh + 1e-9), 0.0, 1.0)

# Soft rim: alpha ramps up over the outer band of the silhouette instead of
# stepping. sqrt keeps the interior opaque so the puff reads as solid.
alpha = np.sqrt(np.clip(sil * 2.2, 0.0, 1.0))

# Vignette to guarantee the sprite reaches zero at the quad edge — a sprite that
# clips at its own border shows a hard square seam when it overlaps anything.
edge = np.clip(1.0 - np.sqrt(u * u + v * v), 0.0, 1.0)
alpha *= np.clip(edge * 3.0, 0.0, 1.0) ** 1.5

rgb = np.full((size, size, 3), 255, dtype=np.uint8)
a8 = (np.clip(alpha, 0.0, 1.0) * 255.0).astype(np.uint8)
img = Image.fromarray(np.dstack([rgb, a8]), mode="RGBA")
img.save(out_path)

cover = float((a8 > 8).mean())
print(f"wrote {out_path}  {size}x{size}  lobes={lobes} seed={seed}  coverage={cover:.2%}")
if cover < 0.10:
    print("  WARNING: very sparse — lower the threshold percentile or raise lobe radii")
if cover > 0.75:
    print("  WARNING: nearly full-frame — the silhouette will read as a disc again")
