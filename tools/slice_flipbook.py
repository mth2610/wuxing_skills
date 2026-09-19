#!/usr/bin/env python3
"""
Công cụ cắt tấm ảnh Atlas Flipbook (lưới NxM) thành từng khung hình đơn lẻ (PNG).
Cách dùng:
    python3 tools/slice_flipbook.py <đường_dẫn_ảnh_atlas.png> [cols] [rows] [output_dir]

Ví dụ:
    python3 tools/slice_flipbook.py extracted_flipbooks/Sprites/T_FireBall_EOO_Loop.png 8 8 frames/fireball
"""

import sys
import os
from PIL import Image

def slice_flipbook(atlas_path: str, cols: int = 8, rows: int = 8, output_dir: str = None):
    if not os.path.exists(atlas_path):
        print(f"Lỗi: Không tìm thấy file {atlas_path}")
        return

    if output_dir is None:
        base_name = os.path.splitext(os.path.basename(atlas_path))[0]
        output_dir = os.path.join(os.path.dirname(atlas_path), f"{base_name}_frames")

    os.makedirs(output_dir, exist_ok=True)
    img = Image.open(atlas_path)
    w, h = img.size
    frame_w = w // cols
    frame_h = h // rows

    print(f"Đang cắt {atlas_path} ({w}x{h}) thành lưới {cols}x{rows} (khung hình {frame_w}x{frame_h})...")

    frame_idx = 0
    for r in range(rows):
        for c in range(cols):
            box = (c * frame_w, r * frame_h, (c + 1) * frame_w, (r + 1) * frame_h)
            frame = img.crop(box)
            out_file = os.path.join(output_dir, f"frame_{frame_idx:03d}.png")
            frame.save(out_file)
            frame_idx += 1

    print(f"Đã xuất thành công {frame_idx} khung hình vào thư mục: {output_dir}")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    atlas = sys.argv[1]
    c = int(sys.argv[2]) if len(sys.argv) > 2 else 8
    r = int(sys.argv[3]) if len(sys.argv) > 3 else 8
    out = sys.argv[4] if len(sys.argv) > 4 else None

    slice_flipbook(atlas, c, r, out)
