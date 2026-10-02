#!/usr/bin/env python3
"""Derived terrain companion, never authored displacement or albedo luminance.

R/G: periodic least-squares height reconstructed from substrate/soil normal RGB.
B/A: straw/root and moss class probabilities inferred from substrate chroma.
Normal RGB and roughness A of the source materials remain untouched.
Run with /usr/bin/python3 maps/toolkit/tools/bake_ground_relief.py.
"""
from pathlib import Path
import argparse
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[3]
TEXTURES = ROOT/'assets/textures'
SIZE = 512

def integrate_normals(normal_rgb):
    """Solve periodic div(grad(h))=div(-nx/nz,-ny/nz), zero DC.

    Material normal Y points toward increasing UV Y in the stored source.
    The ground bitangent cross(tangent, geometricNormal) maps this to +Z.
    Integration stays in texture coordinates; do not flip source green again.
    Absolute elevation and non-integrable gradients cannot be recovered.
    """
    n = np.asarray(normal_rgb, dtype=np.float64) / 127.5 - 1.0
    nz = np.maximum(n[..., 2], .15)
    gx, gy = -n[..., 0]/nz, -n[..., 1]/nz
    height, width = gx.shape
    kx = (2*np.pi*np.fft.fftfreq(width))[None, :]
    ky = (2*np.pi*np.fft.fftfreq(height))[:, None]
    denom = kx*kx+ky*ky
    denom[0, 0] = 1.0
    spectrum = (-1j*kx*np.fft.fft2(gx)-1j*ky*np.fft.fft2(gy))/denom
    spectrum[0, 0] = 0
    return np.fft.ifft2(spectrum).real

def normalize_height(height):
    lo, hi = np.quantile(height, [.01, .99])
    if hi-lo < 1e-8:
        return np.full(height.shape, .5)
    # Leave headroom and keep broad integrations from overpowering coverage.
    return np.clip(.15+.70*(height-lo)/(hi-lo), .05, .95)

def build():
    fields = []
    for name in ('verdant_meadow_substrate_material.png', 'dirt_material.png'):
        material = np.asarray(Image.open(TEXTURES/name).convert('RGBA'))
        if material.shape != (SIZE, SIZE, 4):
            raise ValueError(f'{name}: expected {SIZE}x{SIZE} RGBA, got {material.shape}')
        fields.append(normalize_height(integrate_normals(material[..., :3])))
    substrate = np.asarray(Image.open(TEXTURES/'verdant_meadow_substrate_diffuse.png')
                           .convert('RGB').resize((SIZE, SIZE), Image.Resampling.LANCZOS), dtype=float)/255
    ratio = substrate[..., 0]/np.maximum(substrate[..., 1], .02)
    straw = np.clip((ratio-.83)/.22, 0, 1)
    moss = np.clip((.88-ratio)/.25, 0, 1)
    return np.rint(np.stack((*fields, straw, moss), axis=-1)*255).astype(np.uint8)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=TEXTURES/'verdant_terrain_relief.png')
    args = parser.parse_args()
    image = build()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(image).save(args.out, optimize=False)
    print(f'{args.out}: {SIZE}x{SIZE}, derived normal-integrated height R/G; litter B, moss A')
