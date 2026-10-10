#!/usr/bin/env python3
"""
scripts/process_dem_basin.py

Universal DEM Watershed-to-Floating-Island Pipeline for Wuxing Skills.
Reads any DEM GeoTIFF (tiled or strip, float/int):
1. Auto-fits aspect ratio within a max bounding box (e.g. max 300x300m).
2. Extracts watershed boundary / cliff falloff for floating island abyss.
3. Computes D8 Flow Direction & Flow Accumulation (with tributary pruning threshold).
4. Computes Terrain Slope and Topographic Wetness Index (TWI).
5. Synthesizes Ecology & Surface Splat Maps (River, Lakeshore, Meadow, Ridge Rock).
6. Exports lightweight 8/16-bit PNGs and C metadata header for immediate map plugin usage.

Usage:
    python3 scripts/process_dem_basin.py <dem_path> [options]
Example:
    python3 scripts/process_dem_basin.py /Users/mth2610/Desktop/dem/DEM_SongQuao.tif --max-dim 300.0 --height-range 18.0 --out-dir assets/maps/song_quao
"""

import sys
import os
import struct
import zlib
import argparse
import numpy as np

# --- Pure Python + Zlib PNG Writer (No external dependencies) ---
def write_png(filename, img_array, color_type=0, bit_depth=8):
    """
    img_array: 2D (grayscale) or 3D (RGB/RGBA) uint8 or uint16 numpy array.
    color_type: 0 for grayscale, 2 for RGB, 6 for RGBA.
    bit_depth: 8 or 16.
    """
    h, w = img_array.shape[:2]
    
    def png_chunk(chunk_type, data):
        length = len(data)
        crc = zlib.crc32(chunk_type + data) & 0xffffffff
        return struct.pack(">I", length) + chunk_type + data + struct.pack(">I", crc)

    ihdr_data = struct.pack(">IIBBBBB", w, h, bit_depth, color_type, 0, 0, 0)
    ihdr = png_chunk(b"IHDR", ihdr_data)

    raw_data = bytearray()
    if bit_depth == 8:
        if color_type == 0: # Gray
            for row in range(h):
                raw_data.append(0) # filter type 0 (None)
                raw_data.extend(img_array[row].astype(np.uint8).tobytes())
        elif color_type == 6: # RGBA
            for row in range(h):
                raw_data.append(0)
                raw_data.extend(img_array[row].astype(np.uint8).tobytes())
        elif color_type == 2: # RGB
            for row in range(h):
                raw_data.append(0)
                raw_data.extend(img_array[row].astype(np.uint8).tobytes())
    elif bit_depth == 16:
        # PNG requires big-endian 16-bit
        arr_be = img_array.astype(">u2")
        for row in range(h):
            raw_data.append(0)
            raw_data.extend(arr_be[row].tobytes())

    compressed = zlib.compress(bytes(raw_data), level=6)
    idat = png_chunk(b"IDAT", compressed)
    iend = png_chunk(b"IEND", b"")

    with open(filename, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(ihdr)
        f.write(idat)
        f.write(iend)

# --- Universal GeoTIFF Reader (Handles Tiled & Stripped) ---
def read_geotiff(dem_path):
    with open(dem_path, "rb") as f:
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
            if typ in (3, 4): # SHORT or LONG
                is_short = (typ == 3)
                fmt = f"{endian}H" if is_short else f"{endian}I"
                b_size = 2 if is_short else 4
                if count == 1:
                    return struct.unpack(fmt, raw[:b_size])[0]
                else:
                    off = struct.unpack(f"{endian}I", raw)[0]
                    cur = f.tell()
                    f.seek(off)
                    vals = struct.unpack(f"{endian}{count}" + ("H" if is_short else "I"), f.read(count * b_size))
                    f.seek(cur)
                    return vals
            elif typ == 2: # ASCII
                off = struct.unpack(f"{endian}I", raw)[0]
                cur = f.tell()
                f.seek(off)
                txt = f.read(count).decode("ascii", errors="ignore")
                f.seek(cur)
                return txt
            return raw

        width = read_tag_val(tags[256])
        height = read_tag_val(tags[257])
        bits_per_sample = read_tag_val(tags.get(258, (3, 1, b"\x10\x00\x00\x00")))
        sample_format = read_tag_val(tags.get(339, (3, 1, b"\x01\x00\x00\x00"))) # 1=uint, 2=int, 3=float
        
        dtype = np.float32
        if sample_format == 2 or sample_format == 1:
            dtype = np.int16 if bits_per_sample == 16 else (np.int32 if bits_per_sample == 32 else np.uint8)
        elif sample_format == 3:
            dtype = np.float32 if bits_per_sample == 32 else np.float64

        dem = np.full((height, width), -32768.0, dtype=np.float32)

        # Check Tiled vs Stripped
        if 322 in tags: # Tiled
            tile_w = read_tag_val(tags[322])
            tile_h = read_tag_val(tags[323])
            tile_offsets = read_tag_val(tags[324])
            tile_byte_counts = read_tag_val(tags[325])
            tiles_across = (width + tile_w - 1) // tile_w
            tiles_down = (height + tile_h - 1) // tile_h

            for td in range(tiles_down):
                for ta in range(tiles_across):
                    t_idx = td * tiles_across + ta
                    off = tile_offsets[t_idx] if isinstance(tile_offsets, tuple) else tile_offsets
                    b_cnt = tile_byte_counts[t_idx] if isinstance(tile_byte_counts, tuple) else tile_byte_counts
                    f.seek(off)
                    tile_raw = f.read(b_cnt)
                    tile_arr = np.frombuffer(tile_raw, dtype=dtype).reshape((tile_h, tile_w)).astype(np.float32)
                    y0, y1 = td * tile_h, min((td + 1) * tile_h, height)
                    x0, x1 = ta * tile_w, min((ta + 1) * tile_w, width)
                    dem[y0:y1, x0:x1] = tile_arr[0:(y1-y0), 0:(x1-x0)]
        else: # Stripped
            strip_offsets = read_tag_val(tags[273])
            rows_per_strip = read_tag_val(tags.get(278, (4, 1, struct.pack(f"{endian}I", height))))
            strip_byte_counts = read_tag_val(tags[279])
            num_strips = len(strip_offsets) if isinstance(strip_offsets, tuple) else 1

            for s_idx in range(num_strips):
                off = strip_offsets[s_idx] if isinstance(strip_offsets, tuple) else strip_offsets
                b_cnt = strip_byte_counts[s_idx] if isinstance(strip_byte_counts, tuple) else strip_byte_counts
                f.seek(off)
                s_raw = f.read(b_cnt)
                y0 = s_idx * rows_per_strip
                y1 = min(y0 + rows_per_strip, height)
                s_arr = np.frombuffer(s_raw, dtype=dtype).reshape((y1 - y0, width)).astype(np.float32)
                dem[y0:y1, 0:width] = s_arr

        return dem

# --- Bilinear Resampling ---
def resample_grid(src, dst_h, dst_w):
    src_h, src_w = src.shape
    y_coords = np.linspace(0, src_h - 1, dst_h)
    x_coords = np.linspace(0, src_w - 1, dst_w)
    
    y0 = np.floor(y_coords).astype(int)
    y1 = np.clip(y0 + 1, 0, src_h - 1)
    x0 = np.floor(x_coords).astype(int)
    x1 = np.clip(x0 + 1, 0, src_w - 1)
    
    wy = (y_coords - y0).reshape(-1, 1)
    wx = (x_coords - x0).reshape(1, -1)
    
    dst = (src[y0, :][:, x0] * (1 - wx) * (1 - wy) +
           src[y0, :][:, x1] * wx * (1 - wy) +
           src[y1, :][:, x0] * (1 - wx) * wy +
           src[y1, :][:, x1] * wx * wy)
    return dst

# --- Hydrological Sink / Depression Filling (Priority-Flood / Wang-Liu concept) ---
def fill_depressions(elevation, valid_mask, max_iter=150):
    """
    Fills local pits/sinks in the DEM so that flow paths can drain continuously
    to the watershed outlet without getting trapped in local micro-depressions.
    """
    filled = elevation.copy()
    h, w = elevation.shape
    dy = [-1, -1, -1, 0, 0, 1, 1, 1]
    dx = [-1, 0, 1, -1, 1, -1, 0, 1]
    
    for it in range(max_iter):
        padded = np.pad(filled, 1, mode='edge')
        min_neighbor = np.full_like(filled, 1e9)
        for i in range(8):
            n = padded[1+dy[i]:1+dy[i]+h, 1+dx[i]:1+dx[i]+w]
            min_neighbor = np.minimum(min_neighbor, n)
            
        sinks = (filled < min_neighbor) & valid_mask
        if not np.any(sinks):
            break
        filled[sinks] = min_neighbor[sinks] + 0.005
    return filled

# --- D8 Flow Direction and Accumulation ---
def compute_d8_flow(elevation, valid_mask):
    """
    Standard D8 hydrological flow algorithm with automatic sink filling.
    Elevation: 2D numpy array.
    valid_mask: boolean 2D array.
    Returns: flow_direction (0..7), flow_accumulation (cells)
    """
    # 1. Fill pits so flow reaches the main river network
    dem_flow = fill_depressions(elevation, valid_mask)
    h, w = dem_flow.shape
    # 8 neighbors: E, NE, N, NW, W, SW, S, SE
    dy = np.array([0, -1, -1, -1,  0,  1, 1, 1])
    dx = np.array([1,  1,  0, -1, -1, -1, 0, 1])
    dist = np.array([1.0, 1.414, 1.0, 1.414, 1.0, 1.414, 1.0, 1.414])
    
    # Calculate steepest descent for every cell
    flow_dir = np.full((h, w), -1, dtype=np.int8)
    elev_copy = dem_flow.copy()
    elev_copy[~valid_mask] = 1e9 # High barrier outside

    # Pad for boundary
    elev_padded = np.pad(elev_copy, 1, mode='edge')
    
    max_slope = np.zeros((h, w), dtype=np.float32)
    center = elev_padded[1:-1, 1:-1]
    
    for i in range(8):
        neighbor = elev_padded[1+dy[i]:1+dy[i]+h, 1+dx[i]:1+dx[i]+w]
        drop = center - neighbor
        slope = drop / dist[i]
        steeper = (slope > max_slope) & (drop > 0)
        max_slope[steeper] = slope[steeper]
        flow_dir[steeper] = i

    # Flow accumulation by sorting elevations descending (topological order)
    flat_indices = np.argsort(-dem_flow.ravel())
    accum = np.ones((h, w), dtype=np.float32)
    accum[~valid_mask] = 0.0

    for idx in flat_indices:
        r, c = divmod(idx, w)
        if not valid_mask[r, c]:
            continue
        d = flow_dir[r, c]
        if d >= 0:
            nr = r + dy[d]
            nc = c + dx[d]
            if 0 <= nr < h and 0 <= nc < w and valid_mask[nr, nc]:
                accum[nr, nc] += accum[r, c]

    return flow_dir, accum

# --- Main Pipeline ---
def process_dem_to_floating_island(args):
    print(f"[*] Reading DEM: {args.dem_path}")
    dem_raw = read_geotiff(args.dem_path)
    h_orig, w_orig = dem_raw.shape
    
    # Identify valid basin mask
    valid_mask_orig = (dem_raw > -1000.0) & (dem_raw < 10000.0)
    ys, xs = np.where(valid_mask_orig)
    if len(xs) == 0:
        raise ValueError("No valid elevation data found in DEM!")

    # Crop to tight watershed bounding box
    min_x, max_x = xs.min(), xs.max()
    min_y, max_y = ys.min(), ys.max()
    crop_w = max_x - min_x + 1
    crop_h = max_y - min_y + 1
    print(f"[*] Watershed crop: {crop_w}x{crop_h} (from {w_orig}x{h_orig})")

    dem_cropped = dem_raw[min_y:max_y+1, min_x:max_x+1]
    valid_mask_cropped = valid_mask_orig[min_y:max_y+1, min_x:max_x+1]

    # Calculate real-world game dimensions (constrained to max_dim)
    aspect = crop_w / crop_h
    if aspect >= 1.0:
        game_width = args.max_dim
        game_depth = args.max_dim / aspect
    else:
        game_depth = args.max_dim
        game_width = args.max_dim * aspect

    print(f"[+] Dynamic Map Dimensions: {game_width:.1f}m (W) x {game_depth:.1f}m (D) [Aspect: {aspect:.3f}]")

    # Target raster resolution for game terrain
    # 1 texel / cell ~ 1.0 - 1.5 meter for optimal rendering & mesh budget
    res_w = int(round(game_width * args.resolution_scale))
    res_h = int(round(game_depth * args.resolution_scale))
    # Make even for GPU texture alignment
    res_w = (res_w // 2) * 2
    res_h = (res_h // 2) * 2
    print(f"[+] Target Texture/Grid Resolution: {res_w} x {res_h}")

    # Replace NoData with nearest or minimum valid value BEFORE resampling 
    # so bilinear interpolation does not leak -32768 into border pixels
    e_raw_min = dem_cropped[valid_mask_cropped].min()
    e_raw_max = dem_cropped[valid_mask_cropped].max()
    dem_clean = dem_cropped.copy()
    dem_clean[~valid_mask_cropped] = e_raw_min

    # Resample DEM and valid mask
    dem = resample_grid(dem_clean, res_h, res_w)
    valid_float = resample_grid(valid_mask_cropped.astype(np.float32), res_h, res_w)
    valid_mask = valid_float > 0.45

    # Elevation normalization to Game height range
    elev_valid = dem[valid_mask]
    e_min, e_max = e_raw_min, e_raw_max
    e_min, e_max = elev_valid.min(), elev_valid.max()
    print(f"[*] Elevation raw: {e_min:.1f}m to {e_max:.1f}m (Range: {e_max - e_min:.1f}m)")

    # Normalized height in [0.0, 1.0] across gameplay vertical range
    norm_height = np.zeros_like(dem)
    norm_height[valid_mask] = (dem[valid_mask] - e_min) / max(e_max - e_min, 1e-4)

    # Cliff drop-off calculation for Floating Island abyss
    # Distance transform from watershed boundary
    print("[*] Computing watershed perimeter cliff falloff...")
    # Pure numpy distance transform approximation for perimeter
    inside_dist = np.zeros((res_h, res_w), dtype=np.float32)
    # Simple multi-pass fast marching / erosion
    cur_mask = valid_mask.copy()
    step_m = (game_width / res_w + game_depth / res_h) * 0.5
    for dist_step in range(1, 15):
        eroded = cur_mask.copy()
        eroded[1:-1, 1:-1] = (cur_mask[1:-1, 1:-1] & cur_mask[:-2, 1:-1] & cur_mask[2:, 1:-1] &
                              cur_mask[1:-1, :-2] & cur_mask[1:-1, 2:])
        diff = cur_mask & ~eroded
        inside_dist[diff] = dist_step * step_m
        cur_mask = eroded

    inside_dist[cur_mask] = 15.0 * step_m # deep interior

    # Cliff drop: outer 4.5m slopes steeply into the abyss
    cliff_margin_m = args.cliff_margin
    cliff_drop = np.clip(inside_dist / max(cliff_margin_m, 0.1), 0.0, 1.0)
    # Smoothstep cliff contour
    cliff_drop = cliff_drop * cliff_drop * (3.0 - 2.0 * cliff_drop)

    # Final heightmap: 1.0 at highest mountain, 0.0 at lake basin water level, 
    # cliff drops below 0 into -cliff_depth
    cliff_depth = args.cliff_depth
    game_height_m = norm_height * args.height_range
    # Edge vertices drop towards -cliff_depth
    final_ground_y = game_height_m * cliff_drop - (1.0 - cliff_drop) * cliff_depth
    final_ground_y[~valid_mask] = -cliff_depth

    # Convert to standard 8-bit heightmap (White=top, Black=-cliff_depth)
    # Range: [-cliff_depth, args.height_range]
    total_range = args.height_range + cliff_depth
    h_encoded = np.clip((final_ground_y + cliff_depth) / total_range, 0.0, 1.0)
    heightmap_8bit = (h_encoded * 255.0 + 0.5).astype(np.uint8)

    # 3. Hydrology: Flow accumulation and Stream carving
    print("[*] Computing D8 Drainage Network...")
    flow_dir, accum = compute_d8_flow(dem, valid_mask)
    
    # Filter tributaries: threshold accumulator to prune small branches
    # Stream threshold based on total watershed area
    total_valid_cells = np.count_nonzero(valid_mask)
    stream_threshold = max(15.0, total_valid_cells * args.stream_threshold_ratio)
    print(f"[*] Stream Extraction Threshold: {stream_threshold:.1f} cells (prunes micro-rills)")

    stream_mask = (accum >= stream_threshold) & valid_mask
    print(f"[*] Total Stream Cells extracted: {np.count_nonzero(stream_mask)}")

    # Detect Reservoir / Lake basin:
    # Song Quao reservoir is the prominent flat low-lying water catchment in the lower basin.
    # We look for the most frequent flat plateau in the lower 30% elevation range.
    flat_search_mask = (dem <= e_min + (e_max - e_min) * 0.30) & valid_mask
    if np.count_nonzero(flat_search_mask) > 0:
        unique_vals, val_counts = np.unique(np.round(dem[flat_search_mask], decimals=0), return_counts=True)
        # Find elevation with greatest flat frequency
        lake_flat_elev = unique_vals[np.argmax(val_counts)]
        lake_candidates = (np.abs(dem - lake_flat_elev) <= 2.5) & valid_mask & (np.arange(res_w) > res_w * 0.60)[None, :]
        if np.count_nonzero(lake_candidates) < 20:
            lake_candidates = (dem <= e_min + (e_max - e_min) * 0.05) & valid_mask
    else:
        lake_candidates = np.zeros_like(valid_mask)

    if np.count_nonzero(lake_candidates) > 0:
        lake_ys, lake_xs = np.where(lake_candidates)
        lake_cx = float(lake_xs.mean() / res_w * game_width)
        lake_cz = float(lake_ys.mean() / res_h * game_depth)
        lake_rx = float((lake_xs.max() - lake_xs.min() + 1) * 0.5 * (game_width / res_w))
        lake_rz = float((lake_ys.max() - lake_ys.min() + 1) * 0.5 * (game_depth / res_h))
        lake_norm_top = float(heightmap_8bit[lake_candidates].mean()) / 255.0
        lake_y_level = lake_norm_top * args.height_range - args.cliff_depth + 0.05
        lake_bed_min_y = (float(heightmap_8bit[lake_candidates].min()) / 255.0) * args.height_range - args.cliff_depth
    else:
        lake_cx, lake_cz, lake_rx, lake_rz = game_width * 0.85, game_depth * 0.75, 20.0, 18.0
        lake_y_level = -2.48
        lake_bed_min_y = -3.20

    print(f"[*] Reservoir extracted: Center=({lake_cx:.2f}, {lake_cz:.2f}), Rx={lake_rx:.2f}, Rz={lake_rz:.2f}, WaterY={lake_y_level:.2f}m")

    # 4. Slope and TWI
    print("[*] Computing Geomorphic Slopes & Topographic Wetness Index...")
    dy, dx = np.gradient(game_height_m, game_depth / res_h, game_width / res_w)
    slope_rad = np.arctan(np.sqrt(dx*dx + dy*dy))
    slope_deg = np.degrees(slope_rad)

    # TWI = ln(accum / (tan(slope) + 0.05))
    twi = np.log((accum + 1.0) / (np.tan(slope_rad) + 0.05))
    twi[~valid_mask] = 0.0
    twi_norm = np.clip((twi - 2.0) / 8.0, 0.0, 1.0)

    # 5. Ecology Splat Map Generation:
    # Channel R: Dense Meadow Grass (Gentle slopes, moderate moisture)
    # Channel G: Exposed Rock / Scree (Steep slopes > 28 deg, cliff perimeter)
    # Channel B: Wildflowers (Sunny knolls, dry-moderate, low slope)
    # Channel A: River / Lake Wetland / Reeds (High TWI, stream mask, lake bed)
    print("[*] Synthesizing Ecology & Surface Splat Maps...")
    splat = np.zeros((res_h, res_w, 4), dtype=np.uint8)

    # Rock on steep slopes or cliff edges
    rock_prob = np.clip((slope_deg - 22.0) / 12.0, 0.0, 1.0)
    rock_prob = np.maximum(rock_prob, 1.0 - cliff_drop)
    rock_prob[~valid_mask] = 1.0

    # Wetland / Reeds / Water
    wetland_prob = np.clip((twi_norm - 0.45) / 0.35, 0.0, 1.0)
    wetland_prob = np.maximum(wetland_prob, stream_mask.astype(np.float32))
    if np.count_nonzero(lake_candidates) > 0:
        wetland_prob = np.maximum(wetland_prob, lake_candidates.astype(np.float32))

    # Meadow grass
    grass_prob = (1.0 - rock_prob) * (1.0 - wetland_prob * 0.7)
    grass_prob[~valid_mask] = 0.0

    # Wildflower clusters on gentle dry ridges
    flower_prob = grass_prob * np.clip(1.0 - wetland_prob * 1.5, 0.0, 1.0) * np.clip(norm_height * 1.5, 0.0, 1.0)

    splat[:, :, 0] = (np.clip(grass_prob, 0, 1) * 255).astype(np.uint8)      # R: Grass
    splat[:, :, 1] = (np.clip(rock_prob, 0, 1) * 255).astype(np.uint8)       # G: Rock
    splat[:, :, 2] = (np.clip(flower_prob, 0, 1) * 255).astype(np.uint8)     # B: Flower
    splat[:, :, 3] = (np.clip(wetland_prob, 0, 1) * 255).astype(np.uint8)    # A: Water/Reed

    # Output directory
    os.makedirs(args.out_dir, exist_ok=True)
    heightmap_png = os.path.join(args.out_dir, f"{args.prefix}_island_heightmap.png")
    ecology_splat_png = os.path.join(args.out_dir, f"{args.prefix}_ecology_splat.png")
    stream_mask_png = os.path.join(args.out_dir, f"{args.prefix}_stream_mask.png")

    write_png(heightmap_png, heightmap_8bit, color_type=0, bit_depth=8)
    write_png(ecology_splat_png, splat, color_type=6, bit_depth=8)
    write_png(stream_mask_png, (stream_mask.astype(np.uint8) * 255), color_type=0, bit_depth=8)

    print(f"[+] Exported: {heightmap_png}")
    print(f"[+] Exported: {ecology_splat_png}")
    print(f"[+] Exported: {stream_mask_png}")

    # Generate C Header Constants for immediate game map consumption
    header_path = os.path.join(args.out_dir, f"{args.prefix}_generated_meta.h")
    # Find river outlet (lowest valid point in stream)
    stream_elev = np.where(stream_mask, dem, 1e9)
    min_outlet_y, min_outlet_x = np.where(stream_elev == stream_elev.min())
    outlet_world_x = (min_outlet_x[0] / res_w) * game_width
    outlet_world_z = (min_outlet_y[0] / res_h) * game_depth

    # Find highest peak
    peak_y, peak_x = np.where(dem == dem[valid_mask].max())
    peak_world_x = (peak_x[0] / res_w) * game_width
    peak_world_z = (peak_y[0] / res_h) * game_depth

    with open(header_path, "w") as f:
        f.write(f"// Generated by scripts/process_dem_basin.py for {args.prefix}\n")
        f.write(f"#ifndef {args.prefix.upper()}_GENERATED_META_H\n")
        f.write(f"#define {args.prefix.upper()}_GENERATED_META_H\n\n")
        f.write(f"#define {args.prefix.upper()}_MAP_WIDTH      {game_width:.2f}f\n")
        f.write(f"#define {args.prefix.upper()}_MAP_DEPTH      {game_depth:.2f}f\n")
        f.write(f"#define {args.prefix.upper()}_HEIGHT_RANGE   {args.height_range:.2f}f\n")
        f.write(f"#define {args.prefix.upper()}_CLIFF_DEPTH    {args.cliff_depth:.2f}f\n")
        f.write(f"#define {args.prefix.upper()}_OUTLET_X       {outlet_world_x:.2f}f\n")
        f.write(f"#define {args.prefix.upper()}_OUTLET_Z       {outlet_world_z:.2f}f\n")
        f.write(f"#define {args.prefix.upper()}_PEAK_X         {peak_world_x:.2f}f\n")
        f.write(f"#define {args.prefix.upper()}_PEAK_Z         {peak_world_z:.2f}f\n")
        f.write(f"#define {args.prefix.upper()}_LAKE_CENTER_X  {lake_cx:.2f}f\n")
        f.write(f"#define {args.prefix.upper()}_LAKE_CENTER_Z  {lake_cz:.2f}f\n")
        f.write(f"#define {args.prefix.upper()}_LAKE_RADIUS_X  {lake_rx:.2f}f\n")
        f.write(f"#define {args.prefix.upper()}_LAKE_RADIUS_Z  {lake_rz:.2f}f\n")
        f.write(f"#define {args.prefix.upper()}_LAKE_WATER_Y   {lake_y_level:.2f}f\n")
        f.write(f"#define {args.prefix.upper()}_LAKE_BED_MIN_Y {lake_bed_min_y:.2f}f\n")
        f.write(f"\n#endif // {args.prefix.upper()}_GENERATED_META_H\n")

    print(f"[+] Exported Map Meta Header: {header_path}")
    print("[*] Processing Complete! Basin island is ready for map instantiation.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Universal DEM Watershed-to-Floating-Island Pipeline")
    parser.add_argument("dem_path", help="Path to input DEM GeoTIFF")
    parser.add_argument("--max-dim", type=float, default=260.0, help="Maximum boundary dimension (meters, default 260.0)")
    parser.add_argument("--height-range", type=float, default=16.0, help="Playable mountain elevation range (meters, default 16.0)")
    parser.add_argument("--cliff-depth", type=float, default=8.0, help="Island abyss cliff drop-off depth (meters, default 8.0)")
    parser.add_argument("--cliff-margin", type=float, default=5.0, help="Perimeter cliff edge transition width (meters, default 5.0)")
    parser.add_argument("--resolution-scale", type=float, default=0.75, help="Grid texels per meter (default 0.75)")
    parser.add_argument("--stream-threshold-ratio", type=float, default=0.003, help="Drainage accumulation ratio to form stream (default 0.003)")
    parser.add_argument("--prefix", default="song_quao", help="Asset and symbol prefix name")
    parser.add_argument("--out-dir", default="assets/maps/song_quao", help="Output directory for generated assets")

    args = parser.parse_args()
    process_dem_to_floating_island(args)
