"""Bake a periodic soft contour strip: one texture sample, no runtime noise."""
from pathlib import Path
import numpy as np
from PIL import Image
size = 256
u, v = np.meshgrid(np.arange(size) / size, np.linspace(0, 1, size))
center = .42 + .09*np.sin(u*2*np.pi) + .06*np.cos(u*6*np.pi)
spread = .18 + .05*np.cos(u*4*np.pi+.4)
mask = np.exp(-((v-center)/spread)**2)
mask *= .58 + .24*np.cos(u*2*np.pi+.9) + .18*np.sin(u*8*np.pi)
fade = np.clip(np.minimum(v, 1-v)/.16, 0, 1)
mask = np.clip(mask*fade, 0, 1)
pixels = np.full((size,size,4),255,dtype=np.uint8)
pixels[:,:,3] = np.round(mask*255).astype(np.uint8)
path = Path(__file__).with_name('textures') / 'island_mist_strip.png'
Image.fromarray(pixels).save(path)
print(path)
