"""Build the neutral global particle sprite.

RGB is always white. Alpha alone defines a solid inner core and two narrow
shoulders, so every visible hue comes from the particle material at runtime.

Usage: python3 scripts/generate_default_particle_sprite.py [output.png]
"""
import sys
import math
import struct
import zlib

out_path = sys.argv[1] if len(sys.argv) > 1 else "assets/textures/particle_default.png"
size = 256

def smooth01(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3.0 - 2.0 * t)

def png_chunk(kind, data):
    return (struct.pack(">I", len(data)) + kind + data +
            struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff))

pixels = bytearray()
half = size * 0.5
for y in range(size):
    pixels.append(0)  # PNG filter: None
    for x in range(size):
        px = (x + 0.5 - half) / half
        py = (y + 0.5 - half) / half
        r = math.sqrt(px * px + py * py)
        if r <= 0.55:
            alpha = 1.0
        elif r <= 0.70:
            alpha = 1.0 - 0.58 * smooth01((r - 0.55) / 0.15)
        elif r < 0.95:
            fade = 1.0 - smooth01((r - 0.70) / 0.25)
            alpha = 0.42 * fade * fade
        else:
            alpha = 0.0
        pixels.extend((255, 255, 255, int(max(0.0, min(1.0, alpha)) * 255.0)))

png = (b"\x89PNG\r\n\x1a\n" +
       png_chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)) +
       png_chunk(b"IDAT", zlib.compress(bytes(pixels), 9)) +
       png_chunk(b"IEND", b""))
with open(out_path, "wb") as out_file:
    out_file.write(png)
print(f"wrote {out_path}: {size}x{size}, RGB=white, alpha-only core profile")
