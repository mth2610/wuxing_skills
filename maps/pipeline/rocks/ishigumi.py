#!/usr/bin/env python3
"""
Stage 5: Japanese Stone Arrangement Grammar (Ishigumi / Iwagumi / Sakuteiki).
Generates organic, harmonious multi-scale stone groupings:
  - Sanzon Ishigumi (三尊石組): Triadic asymmetry (Oyaishi + Fukuishi + Soeishi + Suteishi).
  - Fluvial Riparian pods (cut-bank anchors, point-bar shingles, Mizuwake-ishi river splitter).
  - Sawatari-ishi (沢渡り石): Staggered stepping stone crossing.
Enforces nestling (stones touch at the base, never lone isolated rocks).
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


def generate_ishigumi_arrangements(terrain_npz_path, geology_npz_path, heroes_json_path, theme_path, out_dir):
    """
    Spawns medium, small, and companion stones around hero boulders and along stream reaches.
    """
    os.makedirs(out_dir, exist_ok=True)
    debug_dir = os.path.join(out_dir, "debug")
    os.makedirs(debug_dir, exist_ok=True)

    terrain = np.load(terrain_npz_path)
    geology = np.load(geology_npz_path)

    ground = terrain["ground_y_m"]
    water_depth = terrain["water_depth_m"]
    slope = terrain["slope_deg"]
    dist_water = terrain["dist_water_m"]
    dist_rim = terrain["dist_rim_m"]
    fall_line_x = terrain["fall_line_x"]
    fall_line_z = terrain["fall_line_z"]
    valid = terrain["valid"]

    joint_dir_x = geology["joint_dir_x"]
    joint_dir_z = geology["joint_dir_z"]

    h, w = ground.shape
    map_w = 188.2149
    map_d = 300.0
    sx = map_w / (w - 1)
    sz = map_d / (h - 1)

    with open(heroes_json_path, "r") as f:
        heroes = json.load(f)

    with open(theme_path, "r") as f:
        theme = json.load(f)

    ARCH_GRANITE = 3
    ARCH_RIVER_STONE = 2
    ARCH_SCREE = 9

    rock_placements = []
    occupied = []

    def get_terrain(x, z):
        r = int(np.clip(z / sz, 0, h - 1))
        c = int(np.clip(x / sx, 0, w - 1))
        return ground[r, c], slope[r, c], water_depth[r, c], valid[r, c], r, c

    def is_overlap_ok(x, z, r, min_nestle=0.52):
        for ox, oz, orad in occupied:
            d = np.hypot(x - ox, z - oz)
            if d < (r + orad) * min_nestle:
                return False
        return True

    def add_rock(x, z, rScale, hScale, rotDeg, archetype, role="stone"):
        gy, sl, wd, val, r, c = get_terrain(x, z)
        if not val or sl > 36.0 or gy < 0.2:
            return False
        # Universal boundary guard: well inside island plateau safe margin
        if x < 25.0 or x > 165.0 or z < 15.0 or z > 285.0:
            return False
        # Land rocks must be dry
        if archetype != ARCH_RIVER_STONE and wd >= 0.01:
            return False
        if not is_overlap_ok(x, z, rScale):
            return False

        rock_placements.append({
            "x": round(float(x), 2),
            "y": round(float(gy), 2),
            "z": round(float(z), 2),
            "rScale": round(float(rScale), 2),
            "hScale": round(float(hScale), 2),
            "rot": round(float(rotDeg % 360.0), 1),
            "archetype": int(archetype),
            "role": role,
        })
        occupied.append((x, z, rScale))
        return True

    # -------------------------------------------------------------
    # 1. Commit Hero Boulders & Build Sanzon Ishigumi Triads
    # -------------------------------------------------------------
    for hi, hero in enumerate(heroes):
        hx = hero["x"]
        hz = hero["z"]
        hr = hero["radius"]
        hh = hero["height"]
        hrot = hero["rot_deg"]

        # Add Master Boulder (Oyaishi)
        add_rock(hx, hz, hr, hh, hrot, ARCH_GRANITE, role="oyaishi")

        # Get local terrain slope & fall-line direction
        _, _, _, _, hr_idx, hc_idx = get_terrain(hx, hz)
        flx = float(fall_line_x[hr_idx, hc_idx])
        flz = float(fall_line_z[hr_idx, hc_idx])
        fl_angle = np.degrees(np.arctan2(flz, flx))

        # Fukuishi (supporting secondary boulder, ~64% scale)
        r_fuku = hr * 0.64
        h_fuku = r_fuku * 0.78
        d_fuku = (hr + r_fuku) * 0.64

        # Search for optimal Fukuishi angle: prefer downhill flank (+/- 45 deg from fall-line)
        best_fuku = None
        min_fuku_cost = 999.0
        for ang_offset in [-55, -35, -20, 20, 35, 55, 90, -90]:
            fang = np.radians(fl_angle + ang_offset)
            fx = hx + np.cos(fang) * d_fuku
            fz = hz + np.sin(fang) * d_fuku
            fgy, fsl, fwd, fval, _, _ = get_terrain(fx, fz)
            if not fval or fwd >= 0.01 or fsl > 20.0 or fgy < 0.5:
                continue
            cost = fsl
            if cost < min_fuku_cost:
                min_fuku_cost = cost
                best_fuku = (fx, fz, fl_angle + ang_offset)

        if best_fuku:
            fx, fz, f_ang_deg = best_fuku
            add_rock(fx, fz, r_fuku, h_fuku, hrot + 12.0, ARCH_GRANITE, role="fukuishi")

            # Soeishi (companion accent stone, ~42% scale)
            # Must form an asymmetric scalene triangle with Oyaishi and Fukuishi
            r_soe = hr * 0.42
            h_soe = r_soe * 0.75
            d_soe = (hr + r_soe) * 0.68

            best_soe = None
            min_soe_cost = 999.0
            # Place Soeishi on the opposite flank
            for sang_offset in [f_ang_deg + 110, f_ang_deg - 110, f_ang_deg + 140, f_ang_deg - 140]:
                sang = np.radians(sang_offset)
                sx_pos = hx + np.cos(sang) * d_soe
                sz_pos = hz + np.sin(sang) * d_soe
                sgy, ssl, swd, sval, _, _ = get_terrain(sx_pos, sz_pos)
                if not sval or swd >= 0.01 or ssl > 22.0 or sgy < 0.5:
                    continue
                cost = ssl
                if cost < min_soe_cost:
                    min_soe_cost = cost
                    best_soe = (sx_pos, sz_pos, sang_offset)

            if best_soe:
                sx_pos, sz_pos, s_ang_deg = best_soe
                add_rock(sx_pos, sz_pos, r_soe, h_soe, hrot - 18.0, ARCH_GRANITE, role="soeishi")

                # Suteishi (2-3 grounding talus chips around perimeter)
                for si, chip_ang_deg in enumerate([f_ang_deg - 30, s_ang_deg + 30, (f_ang_deg + s_ang_deg) * 0.5 + 180]):
                    c_rad = np.radians(chip_ang_deg % 360)
                    c_dist = hr + 0.65 + si * 0.35
                    cx = hx + np.cos(c_rad) * c_dist
                    cz = hz + np.sin(c_rad) * c_dist
                    r_chip = 0.50 + 0.12 * (si % 3)
                    add_rock(cx, cz, r_chip, r_chip * 0.52, hrot + si * 40.0, ARCH_SCREE, role="suteishi")

    # -------------------------------------------------------------
    # 2. Riparian & Stream Ishigumi (Fluvial Geomorphology)
    # -------------------------------------------------------------
    # 2.1 River-Island Triad (Kojima Ishigumi 小島石組) at Z=90
    add_rock(100.20, 90.00, 1.65, 1.25, 65.0, ARCH_GRANITE, role="kojima_oya")
    add_rock(100.80, 88.00, 1.15, 0.88, 105.0, ARCH_GRANITE, role="kojima_fuku")
    add_rock(101.40, 91.20, 0.75, 0.58, 20.0, ARCH_GRANITE, role="kojima_soe")
    add_rock(99.40, 89.40, 0.48, 0.28, 140.0, ARCH_SCREE, role="kojima_sute")
    add_rock(101.20, 89.10, 0.42, 0.25, 220.0, ARCH_SCREE, role="kojima_sute")

    # Mizuwake-ishi (水分石 - Stream-splitter boulder at upstream island prow)
    add_rock(103.50, 84.50, 0.95, 0.72, 45.0, ARCH_RIVER_STONE, role="mizuwake_ishi")
    add_rock(102.80, 85.20, 0.58, 0.38, 120.0, ARCH_RIVER_STONE, role="mizuwake_companion")

    # Island shoreline waterline cobblestones (hugging bank, not channel!)
    add_rock(96.50, 90.00, 0.55, 0.36, 70.0, ARCH_RIVER_STONE, role="waterline_stone")
    add_rock(96.20, 90.80, 0.38, 0.25, 150.0, ARCH_RIVER_STONE, role="waterline_stone")
    add_rock(104.60, 91.00, 0.52, 0.34, 110.0, ARCH_RIVER_STONE, role="waterline_stone")
    add_rock(104.90, 91.70, 0.36, 0.24, 200.0, ARCH_RIVER_STONE, role="waterline_stone")

    # 2.2 Sawatari-ishi (沢渡り石): Staggered stepping stone crossing at shallow ford (Z=150m)
    stepping_coords = [
        (99.5, 150.1, 0.56, 0.38, 35.0),
        (100.7, 149.6, 0.60, 0.42, 110.0),
        (101.9, 150.2, 0.54, 0.40, 205.0),
        (103.1, 149.7, 0.62, 0.44, 290.0),
        (104.3, 150.3, 0.55, 0.39, 40.0),
        (105.5, 149.8, 0.58, 0.41, 125.0),
        (106.7, 150.2, 0.52, 0.36, 215.0),
    ]
    for sx_c, sz_c, sr, sh, srot in stepping_coords:
        add_rock(sx_c, sz_c, sr, sh, srot, ARCH_RIVER_STONE, role="sawatari_ishi")

    # 2.3 Cascade Choke Boulders (Bậc ghềnh thác) at steep narrow drops
    add_rock(103.50, 52.00, 0.92, 0.68, 60.0, ARCH_RIVER_STONE, role="cascade_choke")
    add_rock(100.80, 52.40, 0.82, 0.60, 240.0, ARCH_RIVER_STONE, role="cascade_choke")
    add_rock(108.20, 138.00, 0.88, 0.64, 80.0, ARCH_RIVER_STONE, role="cascade_choke")
    add_rock(105.40, 138.30, 0.78, 0.56, 270.0, ARCH_RIVER_STONE, role="cascade_choke")

    # 2.4 Meander Riparian Pods along natural river bends
    z_list = []
    x_mid_list = []
    x_left_list = []
    x_right_list = []

    for r in range(35, 235):
        z_m = r * sz
        cols = np.where((water_depth[r, :] > 0.02) & (water_depth[r, :] < 0.45) & (ground[r, :] > 1.2))[0]
        if len(cols) >= 2:
            w_m = (cols[-1] - cols[0]) * sx
            if w_m < 24.0:
                z_list.append(z_m)
                x_mid_list.append(np.mean(cols) * sx)
                x_left_list.append(cols[0] * sx)
                x_right_list.append(cols[-1] * sx)

    z_arr = np.array(z_list)
    x_arr = np.array(x_mid_list)
    dx = np.gradient(x_arr, z_arr)
    d2x = np.gradient(dx, z_arr)

    bend_indices = []
    for i in range(2, len(d2x) - 2):
        if abs(d2x[i]) > 0.015:
            if (d2x[i] > 0 and d2x[i] >= d2x[i-1] and d2x[i] >= d2x[i+1]) or \
               (d2x[i] < 0 and d2x[i] <= d2x[i-1] and d2x[i] <= d2x[i+1]):
                if not bend_indices or (z_arr[i] - z_arr[bend_indices[-1]]) >= 18.0:
                    bend_indices.append(i)

    for bi in bend_indices:
        curv = d2x[bi]
        z_val = z_arr[bi]
        if 80.0 <= z_val <= 98.0:
            continue # Skip island area already composed
        xl = x_left_list[bi]
        xr = x_right_list[bi]

        if curv > 0: # Curves East: outer cut-bank is RIGHT bank
            add_rock(xr + 0.15, z_val, 0.88, 0.65, 45.0, ARCH_RIVER_STONE, role="cutbank_anchor")
            add_rock(xr + 0.68, z_val + 0.78, 0.54, 0.38, 120.0, ARCH_RIVER_STONE, role="cutbank_pod")
            add_rock(xr - 0.10, z_val + 1.28, 0.34, 0.24, 190.0, ARCH_RIVER_STONE, role="cutbank_pebble")
            # Left bank inner point-bar shingle
            add_rock(xl - 0.20, z_val - 0.40, 0.44, 0.30, 80.0, ARCH_RIVER_STONE, role="pointbar_shingle")
            add_rock(xl + 0.35, z_val + 0.45, 0.30, 0.20, 160.0, ARCH_RIVER_STONE, role="pointbar_shingle")
        else: # Curves West: outer cut-bank is LEFT bank
            add_rock(xl - 0.15, z_val, 0.88, 0.65, 225.0, ARCH_RIVER_STONE, role="cutbank_anchor")
            add_rock(xl - 0.68, z_val + 0.78, 0.54, 0.38, 300.0, ARCH_RIVER_STONE, role="cutbank_pod")
            add_rock(xl + 0.10, z_val + 1.28, 0.34, 0.24, 350.0, ARCH_RIVER_STONE, role="cutbank_pebble")
            # Right bank inner point-bar shingle
            add_rock(xr + 0.20, z_val - 0.40, 0.44, 0.30, 110.0, ARCH_RIVER_STONE, role="pointbar_shingle")
            add_rock(xr - 0.35, z_val + 0.45, 0.30, 0.20, 20.0, ARCH_RIVER_STONE, role="pointbar_shingle")

    # Sort deterministically
    rock_placements.sort(key=lambda r: (r['z'], r['x']))

    granite = [r for r in rock_placements if r['archetype'] == ARCH_GRANITE]
    river = [r for r in rock_placements if r['archetype'] == ARCH_RIVER_STONE]
    scree = [r for r in rock_placements if r['archetype'] == ARCH_SCREE]
    print(f"[Stage 5] Generated {len(rock_placements)} Total Stones via Ishigumi Arrangement:")
    print(f"  - GRANITE Boulders (Đá To / Trung / Nhỏ): {len(granite)}")
    print(f"  - RIVER STONES (Anchor Pods / Shingles / Sawatari-ishi): {len(river)}")
    print(f"  - SCREE Talus Chips (Đá vụn nền): {len(scree)}")

    # Save to JSON
    out_json_path = os.path.join(out_dir, "rock_ishigumi_placements.json")
    with open(out_json_path, "w") as f:
        json.dump(rock_placements, f, indent=2)

    # Visual debug output
    debug_map = np.zeros((h, w, 3), dtype=np.uint8)
    norm_g = np.clip((ground + 2.0) / 14.0, 0.0, 1.0)
    debug_map[..., 0] = (norm_g * 90).astype(np.uint8)
    debug_map[..., 1] = (norm_g * 130).astype(np.uint8)
    debug_map[..., 2] = (norm_g * 60).astype(np.uint8)
    debug_map[water_depth > 0.01] = [30, 70, 190]
    debug_map[~valid] = [0, 0, 0]

    for p in rock_placements:
        rc = int(p["x"] / sx)
        rr = int(p["z"] / sz)
        rad_cells = max(1, int(p["rScale"] / min(sx, sz)))
        color = [220, 180, 80] # Granite (Gold/Ochre)
        if p["archetype"] == ARCH_RIVER_STONE:
            color = [80, 210, 220] # River Stone (Cyan)
        elif p["archetype"] == ARCH_SCREE:
            color = [200, 100, 70] # Scree (Orange-Red)

        for dr in range(-rad_cells, rad_cells + 1):
            for dc in range(-rad_cells, rad_cells + 1):
                if 0 <= rr + dr < h and 0 <= rc + dc < w:
                    if dr*dr + dc*dc <= rad_cells * rad_cells:
                        debug_map[rr + dr, rc + dc] = color

    write_png(os.path.join(debug_dir, "13_ishigumi_arrangements.png"), debug_map, color_type=2)
    print(f"[Stage 5] Ishigumi debug map written to: {debug_dir}/13_ishigumi_arrangements.png")

    return rock_placements


if __name__ == "__main__":
    terrain_file = "assets/maps/song_quao/rock_pipeline/rock_terrain_fields.npz"
    geology_file = "assets/maps/song_quao/rock_pipeline/rock_geology_fields.npz"
    heroes_file = "assets/maps/song_quao/rock_pipeline/rock_heroes.json"
    theme_file = os.path.join(SCRIPT_DIR, "themes", "song_quao.json")
    out_directory = "assets/maps/song_quao/rock_pipeline"
    generate_ishigumi_arrangements(terrain_file, geology_file, heroes_file, theme_file, out_directory)
