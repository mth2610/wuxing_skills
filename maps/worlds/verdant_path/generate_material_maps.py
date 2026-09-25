"""Regenerate the meadow substrate normal/roughness textures from their albedo.

Uses the local texture contrast as a restrained height cue. These are authored
detail maps, not measured material scans; the world-space terrain shape still
comes from the heightmap.
"""

from pathlib import Path
import argparse
import struct
import subprocess
import zlib


ROOT = Path(__file__).resolve().parents[3]
TEXTURES = ROOT / "assets" / "textures"
SIZE = 512


def read_rgb(path):
    return subprocess.check_output([
        "ffmpeg", "-v", "error", "-i", str(path), "-vf", f"scale={SIZE}:{SIZE}", "-f", "rawvideo",
        "-pix_fmt", "rgb24", "pipe:1",
    ])


def png_chunk(kind, payload):
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(
        ">I", zlib.crc32(kind + payload) & 0xffffffff
    )


def write_rgba(path, pixels):
    rows = bytearray()
    for y in range(SIZE):
        rows.append(0)
        rows.extend(pixels[y * SIZE * 4:(y + 1) * SIZE * 4])
    header = struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", header)
        + png_chunk(b"IDAT", zlib.compress(rows, 9)) + png_chunk(b"IEND", b"")
    )


def make_material(source, destination, grass):
    rgb = read_rgb(TEXTURES / source)
    assert len(rgb) == SIZE * SIZE * 3
    heights = [0.0] * (SIZE * SIZE)
    for i in range(SIZE * SIZE):
        r, g, b = rgb[i * 3:i * 3 + 3]
        heights[i] = ((g * 0.65 + r * 0.25 + b * 0.10) if grass
                      else (r * 0.32 + g * 0.48 + b * 0.20)) / 255.0

    out = bytearray(SIZE * SIZE * 4)
    for y in range(SIZE):
        ym = ((y - 1) % SIZE) * SIZE
        yp = ((y + 1) % SIZE) * SIZE
        for x in range(SIZE):
            xm = (x - 1) % SIZE
            xp = (x + 1) % SIZE
            i = y * SIZE + x
            dx = (heights[y * SIZE + xp] - heights[y * SIZE + xm]) * 2.2
            dy = (heights[yp + x] - heights[ym + x]) * 2.2
            inv = (1.0 + dx * dx + dy * dy) ** -0.5
            rough = (0.87 if grass else 0.76) + (0.5 - heights[i]) * 0.15
            out[i * 4:i * 4 + 4] = bytes((
                max(0, min(255, round((1.0 - dx * inv) * 127.5))),
                max(0, min(255, round((1.0 - dy * inv) * 127.5))),
                max(0, min(255, round(inv * 255))),
                max(0, min(255, round(rough * 255))),
            ))
    write_rgba(TEXTURES / destination, out)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--grass-source", default="grass_ground_diffuse.png")
    parser.add_argument("--grass-output", default="grass_ground_material.png")
    args = parser.parse_args()
    make_material(args.grass_source, args.grass_output, True)
    make_material("dirt_diffuse.png", "dirt_material.png", False)
