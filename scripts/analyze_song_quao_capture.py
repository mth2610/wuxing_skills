#!/usr/bin/env python3
"""Measure Song Quao captures on identical frames.

Pure NumPy + zlib PNG reader (no Pillow dependency). Prints one row per image:
luma percentiles, crushed-black ratios in the lower two thirds, and per-band
saturation. The water-edge and horizon-fill metrics need the Phase 2 masks and
are intentionally not faked here.

Usage:
    python3 scripts/analyze_song_quao_capture.py /tmp/song_quao_layers
    python3 scripts/analyze_song_quao_capture.py a.png b.png
"""

import os
import struct
import sys
import zlib

import numpy as np


def read_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos = 8
    idat = bytearray()
    width = height = bit_depth = color_type = None
    while pos < len(data):
        length = struct.unpack(">I", data[pos : pos + 4])[0]
        kind = data[pos + 4 : pos + 8]
        chunk = data[pos + 8 : pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, bit_depth, color_type, _, _, interlace = struct.unpack(
                ">IIBBBBB", chunk
            )
            if interlace:
                raise ValueError(f"{path}: interlaced PNGs are not supported")
        elif kind == b"IDAT":
            idat.extend(chunk)
        elif kind == b"IEND":
            break
    if bit_depth != 8:
        raise ValueError(f"{path}: only 8-bit PNGs are supported")
    channels = {0: 1, 2: 3, 4: 2, 6: 4}[color_type]
    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    out = np.empty((height, width, channels), np.uint8)
    prev = np.zeros(stride, np.uint8)
    cursor = 0
    for y in range(height):
        filt = raw[cursor]
        cursor += 1
        line = np.frombuffer(raw[cursor : cursor + stride], np.uint8).copy()
        cursor += stride
        if filt == 1:
            for x in range(channels, stride):
                line[x] = (int(line[x]) + int(line[x - channels])) & 0xFF
        elif filt == 2:
            line = (line.astype(np.uint16) + prev) & 0xFF
            line = line.astype(np.uint8)
        elif filt == 3:
            for x in range(stride):
                left = int(line[x - channels]) if x >= channels else 0
                line[x] = (int(line[x]) + ((left + int(prev[x])) >> 1)) & 0xFF
        elif filt == 4:
            for x in range(stride):
                left = int(line[x - channels]) if x >= channels else 0
                up = int(prev[x])
                up_left = int(prev[x - channels]) if x >= channels else 0
                p = left + up - up_left
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - up_left)
                pred = left if (pa <= pb and pa <= pc) else (up if pb <= pc else up_left)
                line[x] = (int(line[x]) + pred) & 0xFF
        elif filt != 0:
            raise ValueError(f"{path}: unsupported PNG filter {filt}")
        out[y] = line.reshape(width, channels)
        prev = line
    return out


def measure(path):
    img = read_png(path)[..., :3].astype(np.float32) / 255.0
    h, w, _ = img.shape
    luma = img @ np.array([0.2126, 0.7152, 0.0722], np.float32)
    mx = img.max(axis=2)
    mn = img.min(axis=2)
    sat = np.where(mx > 1e-5, (mx - mn) / np.maximum(mx, 1e-5), 0.0)

    lower = luma[h // 3 :]
    top = luma[: h // 4]
    bottom = luma[3 * h // 4 :]
    return {
        "path": path,
        "size": f"{w}x{h}",
        "median": float(np.median(luma)),
        "p10": float(np.percentile(luma, 10)),
        "p90": float(np.percentile(luma, 90)),
        "low_p10": float(np.percentile(lower, 10)),
        "low_median": float(np.median(lower)),
        "pct_005": float((lower < 0.05).mean() * 100.0),
        "pct_010": float((lower < 0.10).mean() * 100.0),
        "sat_median": float(np.median(sat)),
        "top_luma": float(np.median(top)),
        "bottom_luma": float(np.median(bottom)),
        "bottom_sat": float(np.median(sat[3 * h // 4 :])),
    }


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    paths = []
    for arg in argv[1:]:
        if os.path.isdir(arg):
            paths.extend(
                os.path.join(arg, name)
                for name in sorted(os.listdir(arg))
                if name.lower().endswith(".png")
            )
        else:
            paths.append(arg)
    if not paths:
        print("No PNG files found.", file=sys.stderr)
        return 2

    header = (
        f"{'frame':24s} {'size':9s} {'med':>5s} {'p10':>5s} {'p90':>5s} "
        f"{'low10':>5s} {'lowmed':>6s} {'<.05%':>6s} {'<.10%':>6s} "
        f"{'sat':>5s} {'topL':>5s} {'botL':>5s} {'botS':>5s}"
    )
    print(header)
    print("-" * len(header))
    for path in paths:
        m = measure(path)
        print(
            f"{os.path.basename(path):24s} {m['size']:9s} "
            f"{m['median']:5.3f} {m['p10']:5.3f} {m['p90']:5.3f} "
            f"{m['low_p10']:5.3f} {m['low_median']:6.3f} "
            f"{m['pct_005']:6.2f} {m['pct_010']:6.2f} "
            f"{m['sat_median']:5.3f} {m['top_luma']:5.3f} "
            f"{m['bottom_luma']:5.3f} {m['bottom_sat']:5.3f}"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
