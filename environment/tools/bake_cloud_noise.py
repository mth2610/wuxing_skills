#!/usr/bin/env python3
"""Bake the fixed periodic cloud field; standard library only, no runtime work."""
import math
from pathlib import Path
import struct
import zlib

SIZE = 64


def lattice(x, y, cells):
    seed = ((x % cells) * 0x9E3779B9) ^ ((y % cells) * 0x85EBCA6B) ^ 0x629A292A
    seed = ((seed ^ (seed >> 16)) * 0x7FEB352D) & 0xFFFFFFFF
    seed = ((seed ^ (seed >> 15)) * 0x846CA68B) & 0xFFFFFFFF
    return ((seed ^ (seed >> 16)) & 0xFFFFFF) / 16777216.0


def field(u, v, cells):
    x, y = u * cells, v * cells
    ix, iy = math.floor(x), math.floor(y)
    tx, ty = x - ix, y - iy
    tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
    a = lattice(ix, iy, cells) * (1 - tx) + lattice(ix + 1, iy, cells) * tx
    b = lattice(ix, iy + 1, cells) * (1 - tx) + lattice(ix + 1, iy + 1, cells) * tx
    return a * (1 - ty) + b * ty


def bake(path):
    rows = bytearray()
    for y in range(SIZE):
        rows.append(0)
        for x in range(SIZE):
            # Texel centers and periodic lattice make repeat-filtered seams continuous.
            u, v = (x + 0.5) / SIZE, (y + 0.5) / SIZE
            value = 0.8 * field(u, v, 4) + 0.2 * field(u, v, 8)
            byte = round(min(1, max(0, (value - 0.5) * 1.6 + 0.5)) * 255)
            rows.extend((byte, byte, byte, 255))

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))

    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', SIZE, SIZE, 8, 6, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b'')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


if __name__ == '__main__':
    bake(Path(__file__).resolve().parents[1] / 'textures/cloud_noise.png')
