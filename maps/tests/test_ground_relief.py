"""Derived normal integration, source-channel invariants and desktop/GLES shaders."""
from pathlib import Path
import importlib.util
import shutil
import subprocess
import tempfile
import re
import hashlib
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('ground_baker',ROOT/'maps/toolkit/tools/bake_ground_relief.py')
baker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(baker)
size = 128
y,x = np.mgrid[:size,:size]
k = 2*np.pi/size
height = 4*np.sin(k*x)*np.cos(k*y)+.7*np.cos(3*k*x)
gx = 4*k*np.cos(k*x)*np.cos(k*y)-2.1*k*np.sin(3*k*x)
gy = -4*k*np.sin(k*x)*np.sin(k*y)
normal = np.stack((-gx,-gy,np.ones_like(gx)),axis=-1)
normal /= np.linalg.norm(normal,axis=-1,keepdims=True)
encoded = np.rint((normal+1)*127.5).astype(np.uint8)
recovered = baker.integrate_normals(encoded)
assert abs(float(recovered.mean()))<1e-12
assert np.corrcoef(height.ravel(),recovered.ravel())[0,1]>.999
assert np.sqrt(np.mean((height-recovered)**2))<.03
flat = np.full((size,size,3),128,dtype=np.uint8)
flat[...,2] = 255
assert np.all(baker.normalize_height(baker.integrate_normals(flat))==.5)
paths = [baker.TEXTURES/'verdant_meadow_substrate_material.png',baker.TEXTURES/'dirt_material.png']
before = [hashlib.sha256(p.read_bytes()).digest() for p in paths]
result = baker.build()
assert result.shape==(512,512,4)
assert np.array_equal(result,np.asarray(Image.open(baker.TEXTURES/'verdant_terrain_relief.png')))
assert all(hashlib.sha256(p.read_bytes()).digest()==h for p,h in zip(paths,before))
assert all(np.std(result[...,i])>5 for i in range(4))
print('ground relief: signed gradient reconstruction, flat fallback, deterministic companion, source RGBA unchanged passed')

validator = shutil.which('glslangValidator')
if not validator:
    raise RuntimeError('glslangValidator is required for desktop/GLES shader validation')
def expand(path):
    text = path.read_text()
    return re.sub(r'^\s*#include "([^"]+)"\s*$',lambda m:expand(ROOT/m.group(1)),text,flags=re.M)
with tempfile.TemporaryDirectory(prefix='wuxing-ground-shader-') as directory:
    work = Path(directory)
    for version in ('330','300 es'):
        for extension in ('vs','fs'):
            text = expand(ROOT/f'maps/toolkit/shaders/ground_splat.{extension}')
            text = re.sub(r'^#version[^\n]+', '#version '+version, text, count=1)
            if version.endswith('es'):
                text = text.replace('#version 300 es','#version 300 es\nprecision highp float;\nprecision highp int;',1)
            shader = work/f'ground_{version[:3]}.{extension}'
            shader.write_text(text)
            subprocess.run([validator,'-S','vert' if extension=='vs' else 'frag',str(shader)],check=True)
print('ground relief: production vertex/fragment shaders compile GLSL330 and GLES300')
