#!/usr/bin/env python3
"""
Công cụ trích xuất Texture và Flipbook từ file .uasset của Unreal Engine (UE5 / UE4) sang định dạng PNG.
Hỗ trợ giải nén Oodle (pyooz), mã hóa vi sai UE DELTA, và các định dạng HDR RGBA16F, RGBA16, BGRA8, G8.

Cách dùng:
    python3 tools/extract_ue_textures.py <đường_dẫn_file_hoặc_thư_mục_uasset> [thư_mục_xuất_png]
"""

import os
import sys
import struct
import numpy as np
from PIL import Image
from io import BytesIO

from uasset.package import Package
from uasset.properties import (
    _read_property_type_name,
    UE5_PROPERTY_TAG_EXTENSION,
    TAG_HasArrayIndex, TAG_HasPropertyGuid, TAG_HasPropertyExtensions,
)
from uasset.texture import (
    TSF_BPP,
    _extract_source_struct, extract_trailer_payload,
    decompress_compressed_buffer, undo_ue_delta
)

def extract_texture_to_png(uasset_path: str, output_png_path: str) -> bool:
    try:
        pkg = Package(uasset_path)
    except Exception as e:
        return False

    tex_indices = pkg.find_exports_by_class("Texture2D")
    if not tex_indices:
        return False

    data_reader = pkg.get_export_data(tex_indices[0])
    if data_reader is None:
        return False

    data_reader.read_uint8()

    use_ext = pkg.file_version_ue5 >= UE5_PROPERTY_TAG_EXTENSION
    source_data = {}

    while True:
        if not data_reader.can_read(8):
            break
        name_idx = data_reader.read_int32()
        _name_num = data_reader.read_int32()
        name = pkg.name_map[name_idx] if 0 <= name_idx < len(pkg.name_map) else f"#{name_idx}"
        if name == "None":
            break

        type_name = _read_property_type_name(data_reader, pkg.name_map)
        size = data_reader.read_int32()
        flags = data_reader.read_uint8()

        if flags & TAG_HasArrayIndex:
            data_reader.skip(4)

        if name == "Source" and "StructProperty" in type_name:
            source_data = _extract_source_struct(
                data_reader, pkg.name_map, pkg.file_version_ue5, size)
        else:
            data_reader.skip(size)

        if flags & TAG_HasPropertyGuid:
            data_reader.skip(16)
        if use_ext and (flags & TAG_HasPropertyExtensions):
            ext = data_reader.read_uint8()
            if ext & 0x01:
                data_reader.skip(1 + 4)

    width = source_data.get('SizeX', 0)
    height = source_data.get('SizeY', 0)
    fmt = source_data.get('Format', -1)
    fmt_str = source_data.get('FormatStr', '?')
    compression_fmt = source_data.get('CompressionFormat', -1)
    bpp = TSF_BPP.get(fmt, 0)

    if width <= 0 or height <= 0 or fmt < 0 or bpp == 0:
        return False

    compressed_buffer = extract_trailer_payload(pkg.reader.data)
    if compressed_buffer is None:
        return False

    raw_data = decompress_compressed_buffer(compressed_buffer)
    if raw_data is None:
        return False

    needed = width * height * bpp
    if len(raw_data) < needed:
        return False

    bpng = source_data.get('bPNGCompressed', False)
    if bpng and raw_data[:4] == b'\x89PNG':
        try:
            img = Image.open(BytesIO(raw_data))
            os.makedirs(os.path.dirname(output_png_path), exist_ok=True)
            img.save(output_png_path)
            print(f"[OK] {os.path.basename(uasset_path)} -> {output_png_path} (PNG, {img.width}x{img.height})")
            return True
        except Exception:
            return False

    if compression_fmt == 4:  # TSCF_UEDELTA
        elem_size = 2 if fmt in (3, 4, 5) else 1
        raw_data = undo_ue_delta(raw_data, width, height, bpp, element_size=elem_size)

    pixels = None
    if fmt == 1:  # TSF_BGRA8
        img_data = np.frombuffer(raw_data[:needed], dtype=np.uint8)
        img = img_data.reshape(height, width, 4).copy()
        img[:, :, [0, 2]] = img[:, :, [2, 0]]
        pixels = img
    elif fmt == 3:  # TSF_RGBA16
        img_data = np.frombuffer(raw_data[:needed], dtype=np.uint16)
        img = img_data.reshape(height, width, 4).copy()
        pixels = (img >> 8).astype(np.uint8)
    elif fmt == 4:  # TSF_RGBA16F
        f16 = np.frombuffer(raw_data[:needed], dtype=np.float16).reshape(height, width, 4)
        f16_clean = np.nan_to_num(f16, nan=0.0, posinf=1.0, neginf=0.0)
        pixels = (np.clip(f16_clean, 0.0, 1.0) * 255.0).astype(np.uint8)
    elif fmt == 0:  # TSF_G8
        img_data = np.frombuffer(raw_data[:needed], dtype=np.uint8)
        gray = img_data.reshape(height, width)
        pixels = np.stack([gray, gray, gray, np.full_like(gray, 255)], axis=2)
    elif fmt == 5:  # TSF_G16
        img_data = np.frombuffer(raw_data[:needed], dtype=np.uint16)
        gray = (img_data.reshape(height, width) / 256).astype(np.uint8)
        pixels = np.stack([gray, gray, gray, np.full_like(gray, 255)], axis=2)
    else:
        return False

    if pixels is not None:
        os.makedirs(os.path.dirname(output_png_path), exist_ok=True)
        img = Image.fromarray(pixels, mode='RGBA')
        img.save(output_png_path)
        print(f"[OK] {os.path.basename(uasset_path)} -> {output_png_path} ({fmt_str}, {width}x{height})")
        return True

    return False

def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    target_path = sys.argv[1]
    out_dir = sys.argv[2] if len(sys.argv) > 2 else "extracted_textures"

    if os.path.isfile(target_path):
        filename = os.path.splitext(os.path.basename(target_path))[0] + ".png"
        out_file = os.path.join(out_dir, filename)
        extract_texture_to_png(target_path, out_file)
    else:
        count = 0
        for root, _, files in os.walk(target_path):
            for f in files:
                if f.endswith(".uasset"):
                    uasset_file = os.path.join(root, f)
                    rel = os.path.relpath(root, target_path)
                    filename = os.path.splitext(f)[0] + ".png"
                    out_file = os.path.join(out_dir, rel, filename)
                    if extract_texture_to_png(uasset_file, out_file):
                        count += 1
        print(f"\nĐã xuất thành công {count} textures vào: {out_dir}")

if __name__ == '__main__':
    main()
