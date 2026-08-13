#!/usr/bin/env python3
"""Build the thin-smoke mask used once around an impact shockwave's polar U.

The source is intentionally a single non-tileable horizontal smoke strip.  Its
left/right ends are allowed to break: the effect is a torn pressure front, not
a rope of repeated noise.  The output is RGBA: neutral RGB preserves runtime
element tint while alpha carries the actual coverage.

Uses only the standard library so the committed runtime sheet is reproducible.
"""

from __future__ import annotations

import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets" / "sources" / "impact_shockwave_smoke_reference.png"
OUT = ROOT / "assets" / "textures" / "impact_shockwave_smoke.png"
WIDTH = 512
HEIGHT = 256


def paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else b if pb <= pc else c


def read_rgb_png(path: Path) -> tuple[int, int, bytearray]:
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path} is not a PNG")
    pos = 8
    width = height = color_type = bit_depth = None
    chunks = bytearray()
    while pos < len(data):
        size = struct.unpack(">I", data[pos:pos + 4])[0]
        kind = data[pos + 4:pos + 8]
        payload = data[pos + 8:pos + 8 + size]
        pos += 12 + size
        if kind == b"IHDR":
            width, height, bit_depth, color_type, comp, filt, interlace = struct.unpack(
                ">IIBBBBB", payload)
            if bit_depth != 8 or color_type != 2 or comp or filt or interlace:
                raise ValueError("source must be non-interlaced RGB8 PNG")
        elif kind == b"IDAT":
            chunks.extend(payload)
        elif kind == b"IEND":
            break
    if width is None or height is None:
        raise ValueError("source PNG has no header")
    stride = width * 3
    packed = zlib.decompress(chunks)
    rows = bytearray(width * height * 3)
    offset = 0
    prev = bytearray(stride)
    for y in range(height):
        kind = packed[offset]
        offset += 1
        row = bytearray(packed[offset:offset + stride])
        offset += stride
        for x in range(stride):
            left = row[x - 3] if x >= 3 else 0
            up = prev[x]
            up_left = prev[x - 3] if x >= 3 else 0
            if kind == 1:
                row[x] = (row[x] + left) & 255
            elif kind == 2:
                row[x] = (row[x] + up) & 255
            elif kind == 3:
                row[x] = (row[x] + ((left + up) >> 1)) & 255
            elif kind == 4:
                row[x] = (row[x] + paeth(left, up, up_left)) & 255
            elif kind != 0:
                raise ValueError(f"unsupported PNG filter {kind}")
        rows[y * stride:(y + 1) * stride] = row
        prev = row
    return width, height, rows


def smoke_alpha(value: float) -> float:
    # Suppress the true black surround without clipping the weak outer hairs.
    t = max(0.0, min(1.0, (value - 0.016) / 0.54))
    return t * t * (3.0 - 2.0 * t)


def chunk(kind: bytes, payload: bytes) -> bytes:
    joined = kind + payload
    return struct.pack(">I", len(payload)) + joined + struct.pack(">I", zlib.crc32(joined) & 0xffffffff)


def main() -> None:
    source_w, source_h, source = read_rgb_png(SOURCE)
    pixels = bytearray(WIDTH * HEIGHT * 4)
    for y in range(HEIGHT):
        sy = min(source_h - 1, int((y + 0.5) * source_h / HEIGHT))
        for x in range(WIDTH):
            sx = min(source_w - 1, int((x + 0.5) * source_w / WIDTH))
            i = (sy * source_w + sx) * 3
            luminosity = max(source[i], source[i + 1], source[i + 2]) / 255.0
            coverage = smoke_alpha(luminosity)
            # Neutral but non-constant RGB, with alpha as the formal coverage.
            grey = round(min(1.0, luminosity * 1.15) * 255.0)
            dst = (y * WIDTH + x) * 4
            pixels[dst:dst + 4] = bytes((grey, grey, grey, round(coverage * 255.0)))
    raw = bytearray(HEIGHT * (1 + WIDTH * 4))
    for y in range(HEIGHT):
        dst = y * (1 + WIDTH * 4)
        src = y * WIDTH * 4
        raw[dst + 1:dst + 1 + WIDTH * 4] = pixels[src:src + WIDTH * 4]
    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 6, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))
    OUT.write_bytes(png)
    print(f"{OUT}: {WIDTH}x{HEIGHT} RGBA8 from {SOURCE.name}")


if __name__ == "__main__":
    main()
