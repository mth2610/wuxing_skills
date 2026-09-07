#!/usr/bin/env python3
"""
Packs the 4 isolated botanical components into maps/toolkit/textures/foliage_atlas_v1.png:
- Quadrant 0 (Top-Left, 0..512, 0..512): Single Daisy Petal (elongated, notched tip)
- Quadrant 1 (Top-Right, 512..1024, 0..512): Single Poppy Petal (broad fan, ruffled)
- Quadrant 2 (Bottom-Left, 0..512, 512..1024): Compact Reed Plume (búp cờ lau gọn gàng, không cành)
- Quadrant 3 (Bottom-Right, 512..1024, 512..1024): Flower Center Seed Disc (circular stamen core)

All elements:
- Neutral grayscale in RGB (NO pre-baked colors, so game engine can tint via vertex color)
- Alpha channel derived from clean thresholding with anti-aliasing
- Boundary bleeding prevention (unpremultiplied edges)
"""

import os
import struct
import subprocess
import zlib

DAISY_JPG = "/Users/mth2610/.gemini/antigravity/brain/79f2118e-6c2f-4c73-ae69-daf4893c7ff8/single_daisy_petal_1788767523340.jpg"
POPPY_JPG = "/Users/mth2610/.gemini/antigravity/brain/79f2118e-6c2f-4c73-ae69-daf4893c7ff8/single_poppy_petal_1788767540343.jpg"
PLUME_JPG = "/Users/mth2610/.gemini/antigravity/brain/79f2118e-6c2f-4c73-ae69-daf4893c7ff8/reed_plume_compact_1788767695497.jpg"
CENTER_JPG = "/Users/mth2610/.gemini/antigravity/brain/79f2118e-6c2f-4c73-ae69-daf4893c7ff8/single_flower_center_1788767588455.jpg"

TMP_DIR = "/tmp/foliage_pack"
os.makedirs(TMP_DIR, exist_ok=True)

def convert_to_raw(jpg_path, out_raw, size=512):
    # Use sips to resize and convert to png, then decode
    tmp_png = os.path.join(TMP_DIR, os.path.basename(jpg_path) + f"_{size}.png")
    cmd = ["sips", "-z", str(size), str(size), "-s", "format", "png", jpg_path, "--out", tmp_png]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
    return read_png_rgb(tmp_png)

def read_png_rgb(png_path):
    with open(png_path, "rb") as f:
        data = f.read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    offset = 8
    idat = bytearray()
    w, h = 0, 0
    while offset < len(data):
        length, tag = struct.unpack(">I4s", data[offset:offset+8])
        offset += 8
        chunk = data[offset:offset+length]
        offset += length + 4
        if tag == b"IHDR":
            w, h = struct.unpack(">II", chunk[:8])
        elif tag == b"IDAT":
            idat.extend(chunk)
    raw = zlib.decompress(bytes(idat))
    stride = 1 + w * 3
    pixels = bytearray(w * h * 3)
    prev_row = bytearray(w * 3)
    for y in range(h):
        filter_type = raw[y * stride]
        curr_row = bytearray(raw[y * stride + 1 : (y + 1) * stride])
        if filter_type == 1:
            for x in range(3, w * 3):
                curr_row[x] = (curr_row[x] + curr_row[x - 3]) & 0xFF
        elif filter_type == 2:
            for x in range(w * 3):
                curr_row[x] = (curr_row[x] + prev_row[x]) & 0xFF
        elif filter_type == 3:
            for x in range(w * 3):
                left = curr_row[x - 3] if x >= 3 else 0
                up = prev_row[x]
                curr_row[x] = (curr_row[x] + ((left + up) >> 1)) & 0xFF
        elif filter_type == 4:
            for x in range(w * 3):
                a = curr_row[x - 3] if x >= 3 else 0
                b = prev_row[x]
                c = prev_row[x - 3] if x >= 3 else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                curr_row[x] = (curr_row[x] + pr) & 0xFF
        pixels[y * w * 3 : (y + 1) * w * 3] = curr_row
        prev_row = curr_row
    return w, h, pixels

def write_png_rgba(filepath, width, height, raw_rgba):
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xffffffff)

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    scanlines = bytearray()
    row_stride = width * 4
    for y in range(height):
        scanlines.append(0)
        scanlines.extend(raw_rgba[y * row_stride:(y + 1) * row_stride])
    compressed = zlib.compress(bytes(scanlines), 9)

    with open(filepath, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", compressed))
        f.write(chunk(b"IEND", b""))

def process_and_pack():
    print("Reading and resizing 4 botanical components...")
    w0, h0, daisy_rgb = convert_to_raw(DAISY_JPG, "daisy", 512)
    w1, h1, poppy_rgb = convert_to_raw(POPPY_JPG, "poppy", 512)
    w2, h2, plume_rgb = convert_to_raw(PLUME_JPG, "plume", 512)
    w3, h3, center_rgb = convert_to_raw(CENTER_JPG, "center", 512)

    atlas_w = 1024
    atlas_h = 1024
    atlas = bytearray(atlas_w * atlas_h * 4)

    quads = [
        (daisy_rgb, 0, 0, 14.0, 36.0),      # Q0: Daisy petal
        (poppy_rgb, 512, 0, 14.0, 36.0),    # Q1: Poppy petal
        (plume_rgb, 0, 512, 10.0, 28.0),    # Q2: Compact reed plume
        (center_rgb, 512, 512, 14.0, 38.0), # Q3: Flower center disc
    ]

    for rgb_data, ox, oy, t_low, t_high in quads:
        for y in range(512):
            for x in range(512):
                src_idx = (y * 512 + x) * 3
                r = rgb_data[src_idx + 0]
                g = rgb_data[src_idx + 1]
                b = rgb_data[src_idx + 2]
                luma = 0.2126 * r + 0.7152 * g + 0.0722 * b

                # Compute clean antialiased alpha
                if luma <= t_low:
                    alpha = 0.0
                elif luma >= t_high:
                    alpha = 1.0
                else:
                    alpha = (luma - t_low) / (t_high - t_low)
                    alpha = alpha * alpha * (3.0 - 2.0 * alpha) # smoothstep

                # Make pure neutral grayscale in RGB and boost contrast slightly
                # Normalize so background darkness doesn't darken the petal edge
                val = int(min(255, max(0, luma)))
                if alpha > 0.01:
                    # Unpremultiply / color clamping at edge
                    edge_boost = min(1.0, alpha * 2.0)
                    val = int(min(255, max(0, (luma / max(0.25, alpha)) * edge_boost)))
                    # Gentle contrast stretch for crisp petal veins
                    val = int(min(255, max(0, (val - 20) * (255.0 / 235.0))))

                dst_x = ox + x
                dst_y = oy + y
                dst_idx = (dst_y * atlas_w + dst_x) * 4

                atlas[dst_idx + 0] = val
                atlas[dst_idx + 1] = val
                atlas[dst_idx + 2] = val
                atlas[dst_idx + 3] = int(alpha * 255.0)

    out_path = "maps/toolkit/textures/foliage_atlas_v1.png"
    print(f"Writing packed atlas to {out_path}...")
    write_png_rgba(out_path, atlas_w, atlas_h, atlas)
    print("Done!")

if __name__ == "__main__":
    process_and_pack()
