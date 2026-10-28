#!/usr/bin/env python3
"""
Stage 6: Gravel & Grounding Fields (Đá vụn & Vòng đất gốc đá).
Generates:
  1. rock_mask.png (Footprint, Soil contact ring, Moss gradient, Grass suppression).
  2. rock_gravel_density.png (Micro-gravel talus aprons for debris integration).
  3. song_quao_rock_data.h (Production C header for engine compile).
"""

import os
import sys
import json
import numpy as np

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PIPELINE_DIR = os.path.dirname(SCRIPT_DIR)
if PIPELINE_DIR not in sys.path:
    sys.path.insert(0, PIPELINE_DIR)

from dem_pipeline import write_png, gaussian_filter_2d


def generate_gravel_and_grounding(terrain_npz_path, placements_json_path, theme_path, out_dir, c_header_path):
    """
    Bakes rock footprint mask, soil grus ring, and exports C header.
    """
    os.makedirs(out_dir, exist_ok=True)
    debug_dir = os.path.join(out_dir, "debug")
    os.makedirs(debug_dir, exist_ok=True)

    terrain = np.load(terrain_npz_path)
    ground = terrain["ground_y_m"]
    valid = terrain["valid"]
    water_depth = terrain["water_depth_m"]
    fall_line_x = terrain["fall_line_x"]
    fall_line_z = terrain["fall_line_z"]
    h, w = ground.shape

    map_w = 188.2149
    map_d = 300.0
    sx = map_w / (w - 1)
    sz = map_d / (h - 1)

    with open(placements_json_path, "r") as f:
        placements = json.load(f)

    with open(theme_path, "r") as f:
        theme = json.load(f)

    # -------------------------------------------------------------
    # 1. Rasterize Rock Footprint, Grus Ring & Gravel Apron
    # -------------------------------------------------------------
    # Channel R: Rock Core Footprint
    # Channel G: Soil Grus Contact Ring (0.1m - 0.6m perimeter)
    # Channel B: Talus Gravel Apron (downslope fall-line skew)
    # Channel A: Grass Suppression Mask
    rock_mask_rgba = np.zeros((h, w, 4), dtype=np.float32)

    yy, xx = np.indices((h, w))
    world_x = xx * sx
    world_z = yy * sz

    for p in placements:
        px = p["x"]
        pz = p["z"]
        r = p["rScale"]
        arch = p["archetype"]

        # Local bounding box optimization
        pad_m = r * 2.5
        min_c = max(0, int((px - pad_m) / sx))
        max_c = min(w - 1, int((px + pad_m) / sx))
        min_r = max(0, int((pz - pad_m) / sz))
        max_r = min(h - 1, int((pz + pad_m) / sz))

        sub_x = world_x[min_r:max_r+1, min_c:max_c+1]
        sub_z = world_z[min_r:max_r+1, min_c:max_c+1]
        dist = np.hypot(sub_x - px, sub_z - pz)

        # R: Core Footprint
        core = np.clip(1.0 - (dist / (r * 0.90)), 0.0, 1.0)
        rock_mask_rgba[min_r:max_r+1, min_c:max_c+1, 0] = np.maximum(
            rock_mask_rgba[min_r:max_r+1, min_c:max_c+1, 0], core
        )

        # G: Soil Contact Ring (0 - 0.6m beyond rock perimeter)
        ring_inner = r * 0.75
        ring_outer = r + 0.60
        ring = np.clip(1.0 - np.abs(dist - (ring_inner + ring_outer) * 0.5) / ((ring_outer - ring_inner) * 0.5), 0.0, 1.0)
        rock_mask_rgba[min_r:max_r+1, min_c:max_c+1, 1] = np.maximum(
            rock_mask_rgba[min_r:max_r+1, min_c:max_c+1, 1], ring
        )

        # B: Gravel Talus Apron (extended in downslope direction)
        pr_idx = int(np.clip(pz / sz, 0, h - 1))
        pc_idx = int(np.clip(px / sx, 0, w - 1))
        flx = float(fall_line_x[pr_idx, pc_idx])
        flz = float(fall_line_z[pr_idx, pc_idx])

        # Directional projection along fall-line
        dx = sub_x - px
        dz = sub_z - pz
        downslope_proj = dx * flx + dz * flz
        apron_dist = dist - downslope_proj * 0.45 # Elongate downslope
        apron = np.clip(1.0 - (apron_dist / (r * 2.2)), 0.0, 1.0)
        rock_mask_rgba[min_r:max_r+1, min_c:max_c+1, 2] = np.maximum(
            rock_mask_rgba[min_r:max_r+1, min_c:max_c+1, 2], apron * 0.85
        )

        # A: Grass Suppression (0 inside rock footprint, smooth fade out)
        suppress = np.clip((dist - r * 0.82) / 0.45, 0.0, 1.0)
        if np.all(rock_mask_rgba[min_r:max_r+1, min_c:max_c+1, 3] == 0.0):
            rock_mask_rgba[min_r:max_r+1, min_c:max_c+1, 3] = 1.0
        rock_mask_rgba[min_r:max_r+1, min_c:max_c+1, 3] = np.minimum(
            rock_mask_rgba[min_r:max_r+1, min_c:max_c+1, 3], suppress
        )

    # Convert to 8-bit image
    rock_mask_u8 = (np.clip(rock_mask_rgba, 0.0, 1.0) * 255.0).astype(np.uint8)
    rock_mask_u8[~valid] = 0

    mask_out_path = os.path.join(out_dir, "song_quao_rock_mask.png")
    write_png(mask_out_path, rock_mask_u8, color_type=6) # RGBA
    print(f"[Stage 6] Baked rock grounding mask to: {mask_out_path}")

    # -------------------------------------------------------------
    # 2. Export Production C Header
    # -------------------------------------------------------------
    arch_names = {
        0: "MAP_ROCK_BOULDER",
        1: "MAP_ROCK_CRAG",
        2: "MAP_ROCK_RIVER_STONE",
        3: "MAP_ROCK_GRANITE",
        4: "MAP_ROCK_BASALT",
        5: "MAP_ROCK_SANDSTONE",
        6: "MAP_ROCK_LIMESTONE",
        7: "MAP_ROCK_OBSIDIAN",
        8: "MAP_ROCK_SLATE",
        9: "MAP_ROCK_SCREE",
    }

    with open(c_header_path, "w") as f:
        f.write("// ==========================================================================\n")
        f.write("// Song Quao Map — Production Rock Placements\n")
        f.write("// Generated by maps/pipeline/rocks pipeline (Geomorphology & Japanese Ishigumi)\n")
        f.write("// ==========================================================================\n")
        f.write("#ifndef SONG_QUAO_ROCK_DATA_H\n")
        f.write("#define SONG_QUAO_ROCK_DATA_H\n\n")
        f.write('#include "maps/toolkit/map_props.h"\n\n')
        f.write("static const MapRockItem SONG_QUAO_ROCK_ITEMS[] = {\n")

        for item in placements:
            arch_str = arch_names.get(item["archetype"], "MAP_ROCK_GRANITE")
            f.write(f"    {{{{{item['x']:.2f}f, {item['y']:.2f}f, {item['z']:.2f}f}}, "
                    f"{item['rScale']:.2f}f, {item['hScale']:.2f}f, {item['rot']:.1f}f, {arch_str}}},\n")

        f.write("};\n")
        f.write(f"#define SONG_QUAO_ROCK_ITEM_COUNT (int)(sizeof(SONG_QUAO_ROCK_ITEMS) / sizeof(SONG_QUAO_ROCK_ITEMS[0]))\n\n")
        f.write("#endif // SONG_QUAO_ROCK_DATA_H\n")

    print(f"[Stage 6] Exported production C header: {c_header_path} ({len(placements)} items)")

    # Also sync assets/maps/song_quao/song_quao_rock_placements.json
    json_path = os.path.join(os.path.dirname(c_header_path), "song_quao_rock_placements.json")
    with open(json_path, "w") as f:
        json.dump(placements, f, indent=2)

    return mask_out_path, c_header_path


if __name__ == "__main__":
    terrain_file = "assets/maps/song_quao/rock_pipeline/rock_terrain_fields.npz"
    placements_file = "assets/maps/song_quao/rock_pipeline/rock_ishigumi_placements.json"
    theme_file = os.path.join(SCRIPT_DIR, "themes", "song_quao.json")
    out_directory = "assets/maps/song_quao/rock_pipeline"
    header_file = "assets/maps/song_quao/song_quao_rock_data.h"
    generate_gravel_and_grounding(terrain_file, placements_file, theme_file, out_directory, header_file)
