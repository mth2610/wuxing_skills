#!/usr/bin/env python3
"""Stage 2c of flipbook pipeline — Render Optical Flow Motion Vectors.

Reads 3D simulated grids (build_cache/<name>/f###.npz) produced by ti_sim.py,
or pre-rendered 2D frames, and generates a seamless 2D optical flow motion vector
atlas for flipbook subframe warping (60-120 FPS fluid motion).

Layout in the output PNG:
  R: Horizontal displacement U (128 = neutral 0.0, >128 = +X/Right, <128 = -X/Left)
  G: Vertical displacement V   (128 = neutral 0.0, >128 = +Y/Up,    <128 = -Y/Down)
  B: Flow magnitude |V| / max_speed
  A: Motion confidence / density mask

Usage:
  python3 scripts/flipbook/render_motion.py build_cache/fire_volume_puff --grid 8 --cell 256 \
      --out assets/textures/fire_volume_motion_8x8.png
"""

from __future__ import annotations

import argparse
import glob
import math
import os
import struct
import sys
import time
import zlib
from pathlib import Path

import numpy as np


def resize_bilinear(img: np.ndarray, out_h: int, out_w: int) -> np.ndarray:
    """Fast bilinear resize in pure numpy."""
    h, w = img.shape
    if h == out_h and w == out_w:
        return img
    y = np.linspace(0, h - 1, out_h)
    x = np.linspace(0, w - 1, out_w)
    x0 = np.floor(x).astype(int)
    x1 = np.clip(x0 + 1, 0, w - 1)
    y0 = np.floor(y).astype(int)
    y1 = np.clip(y0 + 1, 0, h - 1)
    wx = (x - x0)[None, :]
    wy = (y - y0)[:, None]

    top = img[y0[:, None], x0] * (1.0 - wx) + img[y0[:, None], x1] * wx
    bot = img[y1[:, None], x0] * (1.0 - wx) + img[y1[:, None], x1] * wx
    return (top * (1.0 - wy) + bot * wy).astype(np.float32)


def write_png_unfiltered(path: str | Path, pixels: bytes, width: int, height: int) -> None:
    """Write an unfiltered RGBA8 PNG without external dependencies."""
    raw = bytearray(height * (1 + width * 4))
    for y in range(height):
        dst = y * (1 + width * 4)
        src = y * width * 4
        raw[dst + 1:dst + 1 + width * 4] = pixels[src:src + width * 4]

    def chunk(kind: bytes, data: bytes) -> bytes:
        payload = kind + data
        return (struct.pack(">I", len(data)) + payload +
                struct.pack(">I", zlib.crc32(payload) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(bytes(raw), level=9)) +
           chunk(b"IEND", b""))
    Path(path).write_bytes(png)


def horn_schunck_pyramid(im1: np.ndarray, im2: np.ndarray,
                         num_scales: int = 3, iterations: int = 35,
                         alpha_reg: float = 0.8) -> tuple[np.ndarray, np.ndarray]:
    """Coarse-to-fine optical flow calculation on 2D density/emission fields."""
    pyr1 = [im1]
    pyr2 = [im2]
    for _ in range(1, num_scales):
        h_s = pyr1[-1].shape[0] // 2
        w_s = pyr1[-1].shape[1] // 2
        if h_s < 16 or w_s < 16:
            break
        s1 = 0.25 * (pyr1[-1][0::2, 0::2] + pyr1[-1][1::2, 0::2] +
                     pyr1[-1][0::2, 1::2] + pyr1[-1][1::2, 1::2])
        s2 = 0.25 * (pyr2[-1][0::2, 0::2] + pyr2[-1][1::2, 0::2] +
                     pyr2[-1][0::2, 1::2] + pyr2[-1][1::2, 1::2])
        pyr1.append(s1)
        pyr2.append(s2)

    u, v = None, None
    for s in reversed(range(len(pyr1))):
        cur1 = pyr1[s]
        cur2 = pyr2[s]
        h_cur, w_cur = cur1.shape

        if u is not None:
            # 2x upsample
            u_up = np.repeat(np.repeat(u, 2, axis=0), 2, axis=1) * 2.0
            v_up = np.repeat(np.repeat(v, 2, axis=0), 2, axis=1) * 2.0
            u = u_up[:h_cur, :w_cur]
            v = v_up[:h_cur, :w_cur]
        else:
            u = np.zeros((h_cur, w_cur), dtype=np.float32)
            v = np.zeros((h_cur, w_cur), dtype=np.float32)

        # Spatial gradients
        iy, ix = np.gradient(cur1)
        it = cur2 - cur1

        alpha_sq = alpha_reg * alpha_reg
        denom = ix * ix + iy * iy + alpha_sq
        iters = max(15, iterations // (s + 1))

        for _ in range(iters):
            u_avg = 0.25 * (np.roll(u, 1, 0) + np.roll(u, -1, 0) +
                            np.roll(u, 1, 1) + np.roll(u, -1, 1))
            v_avg = 0.25 * (np.roll(v, 1, 0) + np.roll(v, -1, 0) +
                            np.roll(v, 1, 1) + np.roll(v, -1, 1))
            der = (ix * u_avg + iy * v_avg + it) / denom
            u = u_avg - ix * der
            v = v_avg - iy * der

    return u, v


def main() -> int:
    ap = argparse.ArgumentParser(description="Render optical flow motion vector atlas from Taichi sim grids.")
    ap.add_argument("cache_dir", help="directory with f###.npz grids from ti_sim.py")
    ap.add_argument("--grid", type=int, default=8, help="grid dimension (default: 8 for 8x8 = 64 frames)")
    ap.add_argument("--cell", type=int, default=256, help="cell resolution in pixels (default: 256)")
    ap.add_argument("--out", default=None, help="output motion vector atlas PNG path")
    ap.add_argument("--max-speed", type=float, default=12.0, help="maximum speed normalization clamp in pixels")
    ap.add_argument("--alpha-reg", type=float, default=0.75, help="Horn-Schunck smoothness regularization")
    args = ap.parse_args()

    source_path = os.path.abspath(args.cache_dir)
    want = args.grid * args.grid
    cell = args.cell
    atlas_size = args.grid * cell
    t0 = time.time()
    frames_proj = []
    frames_mask = []

    if os.path.isfile(source_path) and source_path.lower().endswith(".png"):
        from PIL import Image
        src_img = Image.open(source_path).convert("RGBA")
        src_w, src_h = src_img.size
        c_w = src_w // args.grid
        c_h = src_h // args.grid
        arr = np.array(src_img, dtype=np.float32) / 255.0

        print("[render_motion] Loading %d frames directly from atlas %s (%dx%d cells)..."
              % (want, source_path, c_w, c_h))

        for f in range(want):
            r, c = divmod(f, args.grid)
            cell_data = arr[r * c_h:(r + 1) * c_h, c * c_w:(c + 1) * c_w]
            # Channel 0 is flame emission, Channel 3 is opacity
            c_proj = resize_bilinear(cell_data[..., 0], cell, cell)
            c_mask = resize_bilinear(cell_data[..., 3], cell, cell)
            frames_proj.append(c_proj)
            frames_mask.append(c_mask)
    else:
        cache_dir = source_path
        files = sorted(glob.glob(os.path.join(cache_dir, "f*.npz")))
        if not files:
            print("no f###.npz in %s — run scripts/flipbook/ti_sim.py first" % cache_dir)
            return 1

        if len(files) < want:
            print("only %d frames in %s, need %d for %dx%d grid" % (len(files), cache_dir, want, args.grid, args.grid))
            return 1
        files = files[:want]

        print("[render_motion] Projecting %d frames from %s -> atlas %dx%d (%dx%d cells)..."
              % (want, cache_dir, atlas_size, atlas_size, cell, cell))

        for i, p in enumerate(files):
            z = np.load(p)
            d = z["density"].astype(np.float32)
            has_flame = "flame" in z
            fl = z["flame"].astype(np.float32) if has_flame else np.zeros_like(d)

            # Ray-marching along Y axis (depth), orthographic view (Z=up, X=right):
            # Taking peak envelope for flame & integrated density for smoke
            proj_flame = fl.max(axis=1) if has_flame else np.zeros((d.shape[0], d.shape[2]), dtype=np.float32)
            proj_dens = d.sum(axis=1) / d.shape[1]
            
            combined = proj_flame * 0.75 + proj_dens * 0.25
            # In Taichi grid, axis 0 is Z (height, 0=bottom, rz-1=top), axis 2 is X (width)
            # Flip vertically so row 0 is top (standard texture coordinate / screen layout)
            combined_screen = combined[::-1, :]
            mask = (proj_flame * 0.5 + proj_dens).clip(0.0, 1.0)[::-1, :]

            # Resize to cell dimension
            c_proj = resize_bilinear(combined_screen, cell, cell)
            c_mask = resize_bilinear(mask, cell, cell)
            frames_proj.append(c_proj)
            frames_mask.append(c_mask)

    out_path = args.out
    if not out_path:
        stem = os.path.splitext(os.path.basename(source_path))[0]
        out_path = os.path.join(os.path.dirname(__file__), "..", "..", "assets", "textures",
                                stem + "_motion_8x8.png")
    out_path = os.path.abspath(out_path)

    print("[render_motion] Calculating multi-scale optical flow...")
    atlas_rgba = np.zeros((atlas_size, atlas_size, 4), dtype=np.uint8)
    # Neutral default: R=128 (Vx=0), G=128 (Vy=0), B=0, A=255
    atlas_rgba[..., 0] = 128
    atlas_rgba[..., 1] = 128
    atlas_rgba[..., 2] = 0
    atlas_rgba[..., 3] = 255

    max_spd = args.max_speed

    for f in range(want):
        f_next = (f + 1) if (f + 1 < want) else f
        im1 = frames_proj[f]
        im2 = frames_proj[f_next]
        mask = np.maximum(frames_mask[f], frames_mask[f_next])

        if f == want - 1 or mask.max() < 0.01:
            u = np.zeros((cell, cell), dtype=np.float32)
            v = np.zeros((cell, cell), dtype=np.float32)
        else:
            u, v = horn_schunck_pyramid(im1, im2, num_scales=3, iterations=35, alpha_reg=args.alpha_reg)

        # In image coordinates:
        # u is horizontal (positive = right)
        # v is vertical (positive = down in array indices, so -v is upward motion)
        # Standard flow map convention:
        # R > 128 = right (+X), R < 128 = left (-X)
        # G > 128 = upward motion (+Y in world / -V in UV), G < 128 = downward motion
        u_norm = np.clip(u / max_spd, -1.0, 1.0)
        v_norm = np.clip(-v / max_spd, -1.0, 1.0) # Invert so positive means moving UP
        spd = np.sqrt(u * u + v * v)
        spd_norm = np.clip(spd / max_spd, 0.0, 1.0)

        gate = np.clip(mask * 2.5, 0.0, 1.0)
        r_byte = np.clip((u_norm * gate) * 127.0 + 128.0, 0, 255).astype(np.uint8)
        g_byte = np.clip((v_norm * gate) * 127.0 + 128.0, 0, 255).astype(np.uint8)
        b_byte = np.clip((spd_norm * gate) * 255.0, 0, 255).astype(np.uint8)
        a_byte = np.clip(mask * 255.0, 0, 255).astype(np.uint8)

        # Place into atlas (row r from top, col c from left)
        r, c = divmod(f, args.grid)
        atlas_rgba[r * cell:(r + 1) * cell, c * cell:(c + 1) * cell, 0] = r_byte
        atlas_rgba[r * cell:(r + 1) * cell, c * cell:(c + 1) * cell, 1] = g_byte
        atlas_rgba[r * cell:(r + 1) * cell, c * cell:(c + 1) * cell, 2] = b_byte
        atlas_rgba[r * cell:(r + 1) * cell, c * cell:(c + 1) * cell, 3] = a_byte

    print("[render_motion] Writing PNG to %s..." % out_path)
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    write_png_unfiltered(out_path, atlas_rgba.tobytes(), atlas_size, atlas_size)

    elapsed = time.time() - t0
    file_size_kb = os.path.getsize(out_path) / 1024.0
    print("[render_motion] DONE: %dx%d atlas written in %.2fs (%.1f KB) -> %s"
          % (atlas_size, atlas_size, elapsed, file_size_kb, out_path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
