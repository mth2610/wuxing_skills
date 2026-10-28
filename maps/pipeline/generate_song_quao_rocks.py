#!/usr/bin/env python3
"""
Geomorphological, Hydrodynamic & Japanese Stone Arrangement Generator for Song Quao Map.
Strict Geological Suite: Granitic Pluton + Fluvial Alluvial Erosion (Wuxing: Thủy & Mộc w/ Thổ).

Aesthetic & Arrangement Principles:
  1. Japanese Garden & Aquascaping Art (Sanzon Ishigumi 三尊石組 & Takashi Amano Stream Aquascaping):
     - Meadow Outcrops: Guaranteed triadic asymmetry (Oyaishi 親石 master boulder + Fukuishi 副石 companion + Soeishi 添石 accent + Suteishi 捨て石 scree).
     - NEVER place lone boulders standing alone on bare grass; stones in a cluster touch/nestle at their bases.
     - River-Island Triad (Kojima Ishigumi 小島石組) at split river forks (Z=90m) with upstream Mizuwake-ishi (水分石 river-splitter boulder).
  2. Fluvial Hydrodynamics & Riparian Clusters:
     - Curvature-based meanders: outer cut-bank gets a 3-stone shoreline anchor pod; inner point-bar gets a shingle of gentle rounded cobbles.
     - Central stream thalweg is kept clean and free-flowing (NO isolated flat discs in the channel!).
     - Sawatari-ishi (沢渡り石): natural staggered stepping stone crossing at the shallow ford (Z=150m).
  3. Lithology & Scale (Meter-Scale: 1 unit = 1 meter):
     - MAP_ROCK_GRANITE: Weathered polyhedral corestone boulders (Đá To 2.8m - 3.8m, Đá Trung 1.7m - 2.4m, Đá Nhỏ 1.0m - 1.5m).
     - MAP_ROCK_RIVER_STONE: Water-worn smooth 3D cobbles and boulders (0.3m - 1.1m).
     - MAP_ROCK_SCREE: Angular companion talus chips (0.4m - 0.7m).
"""

import os
import json
import numpy as np

def generate_rocks():
    analysis_path = "assets/maps/song_quao/song_quao_analysis.npz"
    if not os.path.exists(analysis_path):
        print(f"Error: {analysis_path} not found.")
        return

    data = np.load(analysis_path)
    ground = data["ground_y_m"]
    water_depth = data["water_depth_m"]
    slope = data["slope_deg"]
    aspect = data["aspect_deg"]
    tpi = data["tpi_m"]
    valid = data["valid"]
    h, w = ground.shape
    sx = 188.2149 / (w - 1)
    sz = 300.0 / (h - 1)
    yy, xx = np.indices((h, w))
    wx = xx * sx
    wz = yy * sz

    ARCH_GRANITE = 3     # Weathered rounded granite tor & corestone boulder
    ARCH_RIVER_STONE = 2 # Water-worn smooth 3D river cobble & boulder
    ARCH_SCREE = 9       # Small granite gravel & companion talus chip

    rock_items = []
    occupied = []

    def get_terrain(x, z):
        r = int(np.clip(z / sz, 0, h - 1))
        c = int(np.clip(x / sx, 0, w - 1))
        return ground[r, c], slope[r, c], water_depth[r, c], valid[r, c]

    def is_free(x, z, r, min_overlap=0.55):
        for ox, oz, orad in occupied:
            if np.hypot(x - ox, z - oz) < (r + orad) * min_overlap:
                return False
        return True

    def add_rock(x, z, rScale, hScale, rot, arch, max_slope=16.0, min_overlap=0.55):
        gy, sl, wd, val = get_terrain(x, z)
        if not val or sl > max_slope:
            return False
        # Universal boundary guard: well within island plateau safe margin
        if x < 80.0 or x > 146.0 or z < 18.0 or z > 255.0 or gy < 1.0:
            return False
        # Land rocks must be dry; scree companion chips can sit in shallow margins <= 0.08m
        if arch not in (ARCH_RIVER_STONE, ARCH_SCREE) and wd >= 0.01:
            return False
        if arch == ARCH_SCREE and wd > 0.08:
            return False
        if not is_free(x, z, rScale, min_overlap=min_overlap):
            return False

        # River stones embed naturally into the alluvial sand/gravel bed
        sink = hScale * 0.28 if arch == ARCH_RIVER_STONE else 0.0
        rock_items.append(dict(x=float(x), y=float(gy - sink), z=float(z),
                               rScale=float(rScale), hScale=float(hScale),
                               rot=float(rot), archetype=int(arch)))
        occupied.append((x, z, rScale))
        return True

    # -------------------------------------------------------------
    # 1. SUMMIT MASSIF (Highest mountain peak at X ~ 121, Z ~ 20)
    # -------------------------------------------------------------
    peak_r, peak_c = np.unravel_index(np.argmax(np.where(valid, ground, -999.0)), ground.shape)
    peak_x, peak_z, peak_y = float(peak_c * sx), float(peak_r * sz), float(ground[peak_r, peak_c])

    # Grand anchor tors crowning the summit ridge
    add_rock(peak_x - 0.5, peak_z - 0.5, 4.4, 3.5, 45.0, ARCH_GRANITE, max_slope=18.0)
    add_rock(peak_x + 3.2, peak_z + 1.6, 3.4, 2.7, 120.0, ARCH_GRANITE, max_slope=18.0)
    add_rock(peak_x - 3.2, peak_z + 2.3, 3.0, 2.4, 215.0, ARCH_GRANITE, max_slope=18.0)
    add_rock(peak_x + 0.8, peak_z - 3.2, 2.6, 2.1, 310.0, ARCH_GRANITE, max_slope=18.0)

    # Scree companion chips around summit tors
    for ang, dist in [(65, 4.6), (180, 5.0), (290, 5.2), (15, 4.0), (140, 4.4)]:
        rad = np.radians(ang)
        cx, cz = peak_x + np.cos(rad) * dist, peak_z + np.sin(rad) * dist
        add_rock(cx, cz, 0.65, 0.35, ang + 30, ARCH_SCREE, max_slope=18.0)

    # -------------------------------------------------------------
    # 2. SANZON ISHIGUMI (三尊石組) MEADOW KNOLL CLUSTERS
    # -------------------------------------------------------------
    # Dilate river mask by 8m (8 grid cells) to ensure meadow knolls are safely inland
    river_mask = (water_depth > 0.01)
    river_dilated = np.zeros_like(river_mask)
    for dr in range(-8, 9):
        for dc in range(-8, 9):
            if dr*dr + dc*dc <= 64:
                river_dilated |= np.roll(np.roll(river_mask, dr, axis=0), dc, axis=1)

    score = np.where(valid & (~river_dilated) & (ground >= 2.0) & (ground <= 9.0) &
                     (slope <= 10.0) & (tpi > 0.05) &
                     (wx >= 86.0) & (wx <= 144.0) & (wz >= 45.0) & (wz <= 245.0),
                     tpi * 2.0 - slope * 0.05, 0.0)

    # Candidate knoll centers (spacing >= 22m)
    knolls = []
    for idx in np.argsort(-score.ravel()):
        if score.ravel()[idx] <= 0: break
        r, c = divmod(int(idx), w)
        x, z = float(c * sx), float(r * sz)
        # Avoid river-island area (handled separately in Domain 3)
        if 96.0 <= x <= 106.0 and 80.0 <= z <= 98.0:
            continue
        if all(np.hypot(x - cx, z - cz) >= 22.0 for cx, cz in knolls):
            knolls.append((x, z))
            if len(knolls) >= 16: break

    placed_clusters = 0
    for ki, (kx, kz) in enumerate(knolls):
        r_oya = 3.0 + 0.4 * (ki % 3)
        h_oya = r_oya * 0.78
        rot_oya = float((ki * 73) % 360)

        # Radial search for optimal Fukuishi (65% scale) and Soeishi (42% scale)
        best_triad = None
        min_cost = 999.0
        r_fuku = r_oya * 0.65
        h_fuku = r_fuku * 0.76
        d_fuku = (r_oya + r_fuku) * 0.62

        r_soe = r_oya * 0.42
        h_soe = r_soe * 0.74
        d_soe = (r_oya + r_soe) * 0.66

        for a1 in range(0, 360, 15):
            rad1 = np.radians(a1)
            f_x, f_z = kx + np.cos(rad1) * d_fuku, kz + np.sin(rad1) * d_fuku
            gy1, sl1, wd1, val1 = get_terrain(f_x, f_z)
            if not val1 or wd1 >= 0.01 or sl1 > 14.0 or gy1 < 1.2:
                continue

            for a2 in range(a1 + 80, a1 + 160, 15):
                a2_norm = a2 % 360
                rad2 = np.radians(a2_norm)
                s_x, s_z = kx + np.cos(rad2) * d_soe, kz + np.sin(rad2) * d_soe
                gy2, sl2, wd2, val2 = get_terrain(s_x, s_z)
                if not val2 or wd2 >= 0.01 or sl2 > 14.0 or gy2 < 1.2:
                    continue

                cost = sl1 + sl2
                if cost < min_cost:
                    min_cost = cost
                    best_triad = (a1, a2_norm, f_x, f_z, s_x, s_z)

        # STRICT GUARANTEE: Never place Oyaishi alone if companions cannot fit!
        if not best_triad:
            continue

        # Commit Triad
        add_rock(kx, kz, r_oya, h_oya, rot_oya, ARCH_GRANITE, max_slope=14.0, min_overlap=0.50)
        a1, a2, f_x, f_z, s_x, s_z = best_triad
        add_rock(f_x, f_z, r_fuku, h_fuku, (rot_oya + 35.0) % 360, ARCH_GRANITE, max_slope=14.0, min_overlap=0.50)
        add_rock(s_x, s_z, r_soe, h_soe, (rot_oya - 50.0) % 360, ARCH_GRANITE, max_slope=14.0, min_overlap=0.50)

        # Suteishi (grounding scree chips around base perimeter)
        for si, sang_deg in enumerate([a1 - 40, a2 + 40, (a1 + a2) // 2 + 180]):
            sang = np.radians((sang_deg) % 360)
            sdist = r_oya + 0.6 + 0.3 * si
            sx_pos, sz_pos = kx + np.cos(sang) * sdist, kz + np.sin(sang) * sdist
            r_s = 0.50 + 0.15 * ((ki + si) % 3)
            add_rock(sx_pos, sz_pos, r_s, r_s * 0.55, (rot_oya + si * 80) % 360, ARCH_SCREE, max_slope=16.0, min_overlap=0.45)

        placed_clusters += 1
        if placed_clusters >= 8:
            break

    # -------------------------------------------------------------
    # 3. RIVER-ISLAND SANZON TRIAD (Kojima Ishigumi 小島石組) at Z=90
    # -------------------------------------------------------------
    # Specifically scaled for the 6-8m river island at (100.2, 90.0)
    # Master boulder
    add_rock(100.20, 90.00, 1.95, 1.55, 65.0, ARCH_GRANITE, max_slope=32.0, min_overlap=0.45)
    # Supporting daughter boulder along island ridge
    add_rock(100.80, 88.00, 1.25, 1.00, 105.0, ARCH_GRANITE, max_slope=32.0, min_overlap=0.45)
    # Accent stone
    add_rock(101.40, 91.20, 0.80, 0.65, 20.0, ARCH_GRANITE, max_slope=32.0, min_overlap=0.45)
    # Talus grounding chips
    add_rock(99.40, 89.40, 0.48, 0.28, 140.0, ARCH_SCREE, max_slope=34.0, min_overlap=0.40)
    add_rock(101.20, 89.10, 0.42, 0.25, 220.0, ARCH_SCREE, max_slope=34.0, min_overlap=0.40)

    # Mizuwake-ishi (水分石 - River-splitter anchor stone at upstream prow, half-sunken in sand)
    add_rock(103.50, 84.50, 0.44, 0.24, 45.0, ARCH_RIVER_STONE, max_slope=24.0, min_overlap=0.45)
    add_rock(102.80, 85.20, 0.28, 0.16, 120.0, ARCH_RIVER_STONE, max_slope=24.0, min_overlap=0.45)

    # Island shoreline waterline cobblestones (hugging waterline, resting on sand bar)
    add_rock(96.50, 90.00, 0.32, 0.18, 70.0, ARCH_RIVER_STONE, max_slope=34.0, min_overlap=0.45)
    add_rock(96.20, 90.80, 0.20, 0.12, 150.0, ARCH_RIVER_STONE, max_slope=34.0, min_overlap=0.45)
    add_rock(104.60, 91.00, 0.30, 0.16, 110.0, ARCH_RIVER_STONE, max_slope=34.0, min_overlap=0.45)
    add_rock(104.90, 91.70, 0.18, 0.10, 200.0, ARCH_RIVER_STONE, max_slope=34.0, min_overlap=0.45)

    # -------------------------------------------------------------
    # 4. STREAM RIPARIAN CLUSTERS & FLUID GEOMORPHOLOGY
    # -------------------------------------------------------------
    # Track stream channel cross sections
    z_list = []
    x_mid_list = []
    x_left_list = []
    x_right_list = []

    for r in range(35, 235):
        z_m = r * sz
        cols = np.where((water_depth[r, :] > 0.02) & (water_depth[r, :] < 0.45) & (ground[r, :] > 1.2))[0]
        if len(cols) >= 2:
            w_m = (cols[-1] - cols[0]) * sx
            if w_m < 25.0: # stream only
                z_list.append(z_m)
                x_mid_list.append(np.mean(cols) * sx)
                x_left_list.append(cols[0] * sx)
                x_right_list.append(cols[-1] * sx)

    z_arr = np.array(z_list)
    x_arr = np.array(x_mid_list)
    dx = np.gradient(x_arr, z_arr)
    d2x = np.gradient(dx, z_arr)

    # Meander bend curvature peaks (spacing >= 16m)
    bend_indices = []
    for i in range(2, len(d2x)-2):
        if abs(d2x[i]) > 0.015:
            if (d2x[i] > 0 and d2x[i] >= d2x[i-1] and d2x[i] >= d2x[i+1]) or \
               (d2x[i] < 0 and d2x[i] <= d2x[i-1] and d2x[i] <= d2x[i+1]):
                if not bend_indices or (z_arr[i] - z_arr[bend_indices[-1]]) >= 16.0:
                    bend_indices.append(i)

    # 4.1 Meander Riparian Pods:
    # Outer cut-bank gets an anchor boulder + downstream cobble + pebble at the water margin.
    # Inner point-bar gets a shingle of 2 smooth cobbles in the shallows.
    # Thalweg (channel center) is kept 100% CLEAR!
    for bi in bend_indices:
        curv = d2x[bi]
        z_val = z_arr[bi]
        # Skip island region (already custom composed in Domain 3)
        if 80.0 <= z_val <= 98.0:
            continue

        xl = x_left_list[bi]
        xr = x_right_list[bi]

        if curv > 0:
            # Curves East: outer cut-bank is RIGHT bank, inner point-bar is LEFT bank
            # Right bank anchor pod (hugging waterline on sand beach)
            add_rock(xr + 0.25, z_val, 0.44, 0.24, 45.0, ARCH_RIVER_STONE, max_slope=36.0, min_overlap=0.48)
            add_rock(xr + 0.65, z_val + 0.75, 0.28, 0.16, 120.0, ARCH_RIVER_STONE, max_slope=36.0, min_overlap=0.48)
            add_rock(xr + 0.10, z_val + 1.25, 0.18, 0.11, 190.0, ARCH_RIVER_STONE, max_slope=36.0, min_overlap=0.48)
            # Left bank inner point-bar shingle
            add_rock(xl - 0.25, z_val - 0.40, 0.24, 0.14, 80.0, ARCH_RIVER_STONE, max_slope=36.0, min_overlap=0.48)
            add_rock(xl - 0.10, z_val + 0.45, 0.16, 0.10, 160.0, ARCH_RIVER_STONE, max_slope=36.0, min_overlap=0.48)
        else:
            # Curves West: outer cut-bank is LEFT bank, inner point-bar is RIGHT bank
            # Left bank anchor pod (hugging waterline on sand beach)
            add_rock(xl - 0.25, z_val, 0.44, 0.24, 225.0, ARCH_RIVER_STONE, max_slope=36.0, min_overlap=0.48)
            add_rock(xl - 0.65, z_val + 0.75, 0.28, 0.16, 300.0, ARCH_RIVER_STONE, max_slope=36.0, min_overlap=0.48)
            add_rock(xl - 0.10, z_val + 1.25, 0.18, 0.11, 350.0, ARCH_RIVER_STONE, max_slope=36.0, min_overlap=0.48)
            # Right bank inner point-bar shingle
            add_rock(xr + 0.25, z_val - 0.40, 0.24, 0.14, 110.0, ARCH_RIVER_STONE, max_slope=36.0, min_overlap=0.48)
            add_rock(xr + 0.10, z_val + 0.45, 0.16, 0.10, 20.0, ARCH_RIVER_STONE, max_slope=36.0, min_overlap=0.48)

    # 4.2 Sawatari-ishi (沢渡り石): Transverse Stepping Stone Crossing at shallow ford (Z=150.0m)
    # Staggered in natural human stride rhythm, flat tops emerging 12-16cm above water
    stepping_coords = [
        (101.8, 149.8, 0.48, 0.32, 45.0),
        (102.9, 150.3, 0.46, 0.34, 115.0),
        (104.1, 149.7, 0.50, 0.35, 210.0),
        (105.3, 150.2, 0.46, 0.33, 310.0),
        (106.5, 149.9, 0.48, 0.32, 80.0),
    ]
    for sx_c, sz_c, sr, sh, srot in stepping_coords:
        add_rock(sx_c, sz_c, sr, sh, srot, ARCH_RIVER_STONE, max_slope=36.0, min_overlap=0.45)

    # 4.3 Cascade Choke Boulders (Bậc ghềnh thác) at steep narrow drops
    # Near Z=52:
    add_rock(103.50, 52.00, 0.48, 0.30, 60.0, ARCH_RIVER_STONE, max_slope=36.0, min_overlap=0.48)
    add_rock(100.80, 52.40, 0.42, 0.26, 240.0, ARCH_RIVER_STONE, max_slope=36.0, min_overlap=0.48)
    # Near Z=138:
    add_rock(108.20, 138.00, 0.46, 0.28, 80.0, ARCH_RIVER_STONE, max_slope=36.0, min_overlap=0.48)
    add_rock(105.40, 138.30, 0.40, 0.25, 270.0, ARCH_RIVER_STONE, max_slope=36.0, min_overlap=0.48)

    # 4.4 Continuous Riparian Shingle & Bank Cobble Clusters
    # Populate natural river cobbles and small pebbles along both banks (hugging sand shoreline)
    for i in range(0, len(z_arr), 3):
        z_val = z_arr[i]
        # Skip island region (already custom composed in Domain 3)
        if 80.0 <= z_val <= 98.0:
            continue

        xl = x_left_list[i]
        xr = x_right_list[i]

        # Deterministic pseudo-random seed based on index
        phase = (i * 37) % 100

        # --- Left Bank Pod (Hugging shoreline beach) ---
        r_left = 0.22 + 0.14 * ((phase * 7) % 10) / 10.0
        h_left = r_left * 0.55
        rot_l = float((phase * 43) % 360)
        offset_l = -0.25 - 0.20 * ((phase * 13) % 10) / 10.0
        if add_rock(xl + offset_l, z_val, r_left, h_left, rot_l, ARCH_RIVER_STONE, max_slope=36.0, min_overlap=0.45):
            # Companion pebble / scree chip nestled beside it
            c_ang = np.radians((rot_l + 70.0) % 360)
            c_dist = (r_left + 0.16) * 0.65
            c_x = xl + offset_l + np.cos(c_ang) * c_dist
            c_z = z_val + np.sin(c_ang) * c_dist
            c_arch = ARCH_SCREE if (phase % 2 == 0) else ARCH_RIVER_STONE
            add_rock(c_x, c_z, 0.14, 0.08, rot_l + 30.0, c_arch, max_slope=36.0, min_overlap=0.40)

        # --- Right Bank Pod (Hugging shoreline beach) ---
        r_right = 0.20 + 0.12 * ((phase * 17) % 10) / 10.0
        h_right = r_right * 0.55
        rot_r = float((phase * 67) % 360)
        offset_r = 0.25 + 0.20 * ((phase * 23) % 10) / 10.0
        if add_rock(xr + offset_r, z_val + 0.6, r_right, h_right, rot_r, ARCH_RIVER_STONE, max_slope=36.0, min_overlap=0.45):
            # Companion pebble / scree chip nestled beside it
            c_ang = np.radians((rot_r - 65.0) % 360)
            c_dist = (r_right + 0.16) * 0.65
            c_x = xr + offset_r + np.cos(c_ang) * c_dist
            c_z = z_val + 0.6 + np.sin(c_ang) * c_dist
            c_arch = ARCH_SCREE if (phase % 3 != 0) else ARCH_RIVER_STONE
            add_rock(c_x, c_z, 0.13, 0.08, rot_r - 25.0, c_arch, max_slope=36.0, min_overlap=0.40)

    # Sort rocks deterministically
    rock_items.sort(key=lambda r: (r['z'], r['x']))

    # Write output C header
    header_path = "assets/maps/song_quao/song_quao_rock_data.h"
    with open(header_path, "w") as f:
        f.write("// Geomorphological, Hydrodynamic & Japanese Stone Arrangement Placements for Song Quao Map\n")
        f.write("// Harmonious Wuxing Geological Suite: Granite + River Stone + Scree\n")
        f.write("#ifndef SONG_QUAO_ROCK_DATA_H\n")
        f.write("#define SONG_QUAO_ROCK_DATA_H\n\n")
        f.write('#include "maps/toolkit/map_props.h"\n\n')
        f.write("static const MapRockItem SONG_QUAO_ROCK_ITEMS[] = {\n")

        arch_names = {
            ARCH_GRANITE: "MAP_ROCK_GRANITE",
            ARCH_RIVER_STONE: "MAP_ROCK_RIVER_STONE",
            ARCH_SCREE: "MAP_ROCK_SCREE",
        }

        for item in rock_items:
            arch_str = arch_names.get(item['archetype'], "MAP_ROCK_GRANITE")
            f.write(f"    {{{{{item['x']:.2f}f, {item['y']:.2f}f, {item['z']:.2f}f}}, "
                    f"{item['rScale']:.2f}f, {item['hScale']:.2f}f, {item['rot']:.1f}f, {arch_str}}},\n")

        f.write("};\n")
        f.write(f"#define SONG_QUAO_ROCK_ITEM_COUNT (int)(sizeof(SONG_QUAO_ROCK_ITEMS) / sizeof(SONG_QUAO_ROCK_ITEMS[0]))\n\n")
        f.write("#endif // SONG_QUAO_ROCK_DATA_H\n")

    json_path = "assets/maps/song_quao/song_quao_rock_placements.json"
    with open(json_path, "w") as f:
        json.dump(rock_items, f, indent=2)

    granite = [r for r in rock_items if r['archetype'] == ARCH_GRANITE]
    large_g = [r for r in granite if r['rScale'] >= 2.8]
    med_g = [r for r in granite if 1.6 <= r['rScale'] < 2.8]
    small_g = [r for r in granite if r['rScale'] < 1.6]
    river = [r for r in rock_items if r['archetype'] == ARCH_RIVER_STONE]
    scree = [r for r in rock_items if r['archetype'] == ARCH_SCREE]
    print(f"Total rocks generated: {len(rock_items)}")
    print(f"  GRANITE: {len(granite)} items ({len(large_g)} Large Đá To >= 2.8m, {len(med_g)} Medium Đá Trung, {len(small_g)} Small Đá Nhỏ)")
    print(f"  RIVER_STONE: {len(river)} items (Riparian Anchor Pods, Shingles, Crossing, Chokes)")
    print(f"  SCREE: {len(scree)} items (Natural talus companion chips)")
    print(f"Written: {header_path} and {json_path}")

if __name__ == "__main__":
    generate_rocks()
