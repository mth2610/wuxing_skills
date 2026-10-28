#!/usr/bin/env python3
"""
Stage 4: Hero Boulder (Đá Chủ / Oyaishi) Placement Generator.
Selects 7-10 major monumental granite tor anchor points across Song Quao.
Driven by outcrop probability P_out, hillslope landform elements, camera salience,
joint strike alignment, and Japanese Sakuteiki principles.
"""

import os
import sys
import json
import numpy as np

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PIPELINE_DIR = os.path.dirname(SCRIPT_DIR)
if PIPELINE_DIR not in sys.path:
    sys.path.insert(0, PIPELINE_DIR)

from dem_pipeline import write_png


def place_hero_boulders(terrain_npz_path, geology_npz_path, theme_path, out_dir):
    """
    Selects optimal hero boulder anchors with Poisson disk exclusion.
    """
    os.makedirs(out_dir, exist_ok=True)
    debug_dir = os.path.join(out_dir, "debug")
    os.makedirs(debug_dir, exist_ok=True)

    terrain = np.load(terrain_npz_path)
    geology = np.load(geology_npz_path)

    ground = terrain["ground_y_m"]
    water_depth = terrain["water_depth_m"]
    slope = terrain["slope_deg"]
    tpi_med = terrain["tpi_med"]
    dist_water = terrain["dist_water_m"]
    dist_rim = terrain["dist_rim_m"]
    gameplay_mask = terrain["gameplay_mask"]
    valid = terrain["valid"]

    hillslope = geology["hillslope"]
    p_outcrop = geology["p_outcrop"]
    joint_dir_x = geology["joint_dir_x"]
    joint_dir_z = geology["joint_dir_z"]

    h, w = ground.shape
    map_w = 188.2149
    map_d = 300.0
    sx = map_w / (w - 1)
    sz = map_d / (h - 1)

    with open(theme_path, "r") as f:
        theme = json.load(f)

    target_count_min, target_count_max = theme.get("hero_count_range", [7, 10])

    # -------------------------------------------------------------
    # 1. Scoring Surface for Hero Stone Anchors
    # -------------------------------------------------------------
    # Landform element weight multipliers:
    # 1: Summit (1.2), 2: Shoulder (1.1), 7: Rim (0.85), 3: Backslope (0.4), 5: Plain (0.1), 6: Water (0.0)
    landform_weights = np.zeros_like(hillslope, dtype=np.float32)
    landform_weights[hillslope == 1] = 1.30 # Summit knolls
    landform_weights[hillslope == 2] = 1.15 # Prominent shoulders
    landform_weights[hillslope == 7] = 0.85 # Rim scenic ledges
    landform_weights[hillslope == 3] = 0.45 # Moderate backslope
    landform_weights[hillslope == 5] = 0.12 # Open plain
    landform_weights[hillslope == 6] = 0.00 # Water thalweg (handled in riparian phase)

    # Outer boundary safety (stay away from sheer drop-off cliff)
    safe_margin = np.clip((dist_rim - 8.0) / 6.0, 0.0, 1.0)
    dry_land = (water_depth < 0.01) & (ground > 1.2) & (slope < 22.0)

    score = (
        p_outcrop * 1.5
        + landform_weights * 0.8
        + np.clip(tpi_med * 0.6, 0.0, 1.2)
        - slope * 0.02
    )
    score *= safe_margin * (1.0 - gameplay_mask * 0.95)
    score[~dry_land | ~valid] = 0.0

    # Guarantee key geographical focal points:
    # A. Summit Massif (Highest Mountain Peak)
    peak_r, peak_c = np.unravel_index(np.argmax(np.where(valid, ground, -999.0)), ground.shape)
    peak_x, peak_z = float(peak_c * sx), float(peak_r * sz)

    # B. River-Island Knolls (around Z=90)
    # Exclude river island from standard land hero pass (island has Kojima triad in riparian)
    yy, xx = np.indices((h, w))
    world_x = xx * sx
    world_z = yy * sz
    island_zone = (world_x >= 96.0) & (world_x <= 106.0) & (world_z >= 80.0) & (world_z <= 98.0)
    score[island_zone] = 0.0

    # -------------------------------------------------------------
    # 2. Non-Maximum Suppression with Variable Poisson Spacing
    # -------------------------------------------------------------
    min_hero_spacing = 22.0 # Minimum 22 meters between major hero outcrops
    heroes = []

    # Priority 1: Add the Grand Summit Massif Tor
    heroes.append({
        "id": "hero_summit",
        "x": peak_x - 0.4,
        "z": peak_z - 0.2,
        "y": float(ground[peak_r, peak_c]),
        "radius": 3.60,
        "height": 2.85,
        "rot_deg": 38.0,
        "role": "oyaishi_summit",
        "archetype": 3, # MAP_ROCK_GRANITE
    })

    # Sort remaining candidate coordinates by score
    flat_indices = np.argsort(-score.ravel())
    for idx in flat_indices:
        if score.ravel()[idx] <= 0.25:
            break
        r, c = divmod(int(idx), w)
        cand_x = float(c * sx)
        cand_z = float(r * sz)

        # Enforce minimum distance from existing hero stones
        too_close = False
        for h_prev in heroes:
            dist = np.hypot(cand_x - h_prev["x"], cand_z - h_prev["z"])
            if dist < min_hero_spacing:
                too_close = True
                break
        if too_close:
            continue

        # Get local geological joint strike
        j_angle = np.degrees(np.arctan2(joint_dir_z[r, c], joint_dir_x[r, c]))
        rot_deg = float((j_angle + np.random.uniform(-10.0, 10.0)) % 360.0)

        # Scale based on local outcrop prominence
        p_val = float(p_outcrop[r, c])
        radius = float(2.40 + 0.85 * p_val)
        height = float(radius * (0.74 + 0.12 * np.random.uniform(-1.0, 1.0)))

        heroes.append({
            "id": f"hero_{len(heroes):02d}",
            "x": cand_x,
            "z": cand_z,
            "y": float(ground[r, c]),
            "radius": round(radius, 2),
            "height": round(height, 2),
            "rot_deg": round(rot_deg, 1),
            "role": "oyaishi_meadow",
            "archetype": 3, # MAP_ROCK_GRANITE
        })

        if len(heroes) >= target_count_max:
            break

    print(f"[Stage 4] Placed {len(heroes)} Hero Boulders (Đá Chủ):")
    for h_item in heroes:
        print(f"  - {h_item['id']}: pos=({h_item['x']:.1f}, {h_item['z']:.1f}, {h_item['y']:.1f}), r={h_item['radius']}m, h={h_item['height']}m, rot={h_item['rot_deg']}°")

    # Save to JSON
    out_json_path = os.path.join(out_dir, "rock_heroes.json")
    with open(out_json_path, "w") as f:
        json.dump(heroes, f, indent=2)

    # Visual debug
    debug_map = np.zeros((h, w, 3), dtype=np.uint8)
    norm_score = np.clip(score / (np.max(score) + 1e-5), 0.0, 1.0)
    debug_map[..., 1] = (norm_score * 200).astype(np.uint8)
    debug_map[water_depth > 0.01] = [30, 70, 190]
    debug_map[~valid] = [0, 0, 0]

    for h_item in heroes:
        rc = int(h_item["x"] / sx)
        rr = int(h_item["z"] / sz)
        rad_cells = max(2, int(h_item["radius"] / min(sx, sz)))
        for dr in range(-rad_cells, rad_cells + 1):
            for dc in range(-rad_cells, rad_cells + 1):
                if 0 <= rr + dr < h and 0 <= rc + dc < w:
                    if dr*dr + dc*dc <= rad_cells * rad_cells:
                        debug_map[rr + dr, rc + dc] = [255, 230, 40] # Bright gold hero markers

    write_png(os.path.join(debug_dir, "12_hero_boulders.png"), debug_map, color_type=2)
    print(f"[Stage 4] Hero boulder debug map written to: {debug_dir}/12_hero_boulders.png")

    return heroes


if __name__ == "__main__":
    terrain_file = "assets/maps/song_quao/rock_pipeline/rock_terrain_fields.npz"
    geology_file = "assets/maps/song_quao/rock_pipeline/rock_geology_fields.npz"
    theme_file = os.path.join(SCRIPT_DIR, "themes", "song_quao.json")
    out_directory = "assets/maps/song_quao/rock_pipeline"
    place_hero_boulders(terrain_file, geology_file, theme_file, out_directory)
