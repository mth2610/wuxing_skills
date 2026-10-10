#!/usr/bin/env python3
import struct
import numpy as np

TIFF_PATH = "/Users/mth2610/Desktop/dem/DEM_SongQuao.tif"

with open(TIFF_PATH, "rb") as f:
    header = f.read(8)
    byte_order_mark, magic, ifd_offset = struct.unpack("<2sHI", header)
    endian = "<" if byte_order_mark == b"II" else ">"
    
    f.seek(ifd_offset)
    num_entries = struct.unpack(f"{endian}H", f.read(2))[0]
    
    tags = {}
    for _ in range(num_entries):
        entry = f.read(12)
        tag, typ, count, val_or_off = struct.unpack(f"{endian}HHI4s", entry)
        tags[tag] = (typ, count, val_or_off)
        
    def read_tag_val(tag_tuple):
        typ, count, raw = tag_tuple
        if typ == 3: # SHORT
            if count == 1:
                return struct.unpack(f"{endian}H", raw[:2])[0]
            else:
                off = struct.unpack(f"{endian}I", raw)[0]
                cur = f.tell()
                f.seek(off)
                vals = struct.unpack(f"{endian}{count}H", f.read(count * 2))
                f.seek(cur)
                return vals
        elif typ == 4: # LONG
            if count == 1:
                return struct.unpack(f"{endian}I", raw)[0]
            else:
                off = struct.unpack(f"{endian}I", raw)[0]
                cur = f.tell()
                f.seek(off)
                vals = struct.unpack(f"{endian}{count}I", f.read(count * 4))
                f.seek(cur)
                return vals
        return raw

    width = read_tag_val(tags[256])
    height = read_tag_val(tags[257])
    tile_w = read_tag_val(tags[322])
    tile_h = read_tag_val(tags[323])
    tile_offsets = read_tag_val(tags[324])
    tile_byte_counts = read_tag_val(tags[325])
    
    print(f"Dimensions: {width} x {height}")
    print(f"Tile size: {tile_w} x {tile_h}")
    tiles_across = (width + tile_w - 1) // tile_w
    tiles_down = (height + tile_h - 1) // tile_h
    
    dem = np.full((height, width), -32768, dtype=np.int16)
    for td in range(tiles_down):
        for ta in range(tiles_across):
            t_idx = td * tiles_across + ta
            off = tile_offsets[t_idx] if isinstance(tile_offsets, tuple) else tile_offsets
            b_cnt = tile_byte_counts[t_idx] if isinstance(tile_byte_counts, tuple) else tile_byte_counts
            f.seek(off)
            tile_raw = f.read(b_cnt)
            tile_arr = np.frombuffer(tile_raw, dtype=np.int16).reshape((tile_h, tile_w))
            
            y0 = td * tile_h
            y1 = min(y0 + tile_h, height)
            x0 = ta * tile_w
            x1 = min(x0 + tile_w, width)
            
            dem[y0:y1, x0:x1] = tile_arr[0:(y1-y0), 0:(x1-x0)]
            
    valid = dem != -32768
    valid_count = np.count_nonzero(valid)
    print(f"Valid pixels: {valid_count} / {width * height} ({valid_count/(width*height)*100:.1f}%)")
    valid_data = dem[valid]
    print(f"Elevations: min={valid_data.min()}m, max={valid_data.max()}m, mean={valid_data.mean():.1f}m")
