#!/usr/bin/env python3
"""
Enhanced Geomorphological & Geological Rock Distribution Generator for Song Quao Map.
Generates:
  - assets/maps/song_quao/song_quao_rock_data.h
  - assets/maps/song_quao/song_quao_rock_placements.json

Hierarchy & Lithology:
  1. ĐÁ TO (Mega / Large Boulders & Spires):
     - Summit Granite Massif & Sacred Obsidian Spire
     - High Ridge Karst Limestone Pinnacles
     - Western & Outer Cliff Precipice (Columnar Basalt & Karst Crags)
  2. ĐÁ TRUNG (Medium Boulders & Outcrops):
     - Meadow rolling knoll Granite boulders
     - Hillside Sandstone strata ledges
     - Talus crest Slate & Basalt blocks
     - In-stream cascade breaker boulders
  3. ĐÁ NHỎ (Small Rocks & Companions):
     - Orbiting companion stones
     - Trailside and meadow fringe stones
     - Pond & river shoreline stepping stones
  4. ĐÁ VỤN (Scree / Talus & Gravel):
     - Talus aprons below steep cliffs (Slate & Scree)
     - Shoreline gravel shingle
  5. ĐÁ Ở SÔNG SUỐI (River Stones & Fluvial Cobbles):
     - In-stream rapids cobbles (oriented with stream flow)
     - Shallow crossing stepping stones
     - Waterfront gravel & wet alluvial stones
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
    slope = data["slope_deg"]
    aspect = data["aspect_deg"]
    tpi = data["tpi_m"]
    water_depth = data["water_depth_m"]
    valid = data["valid"]
    h, w = ground.shape
    sx = 188.2149 / (w - 1)
    sz = 300.0 / (h - 1)

    wet = (water_depth > 0.005)
    streams = (water_depth > 0.02) & (water_depth < 0.40) & (ground > -2.0)

    # Lake center & radius from metadata
    lake_cx, lake_cz = 166.11, 226.98
    lake_rx, lake_rz = 8.51, 11.50
    yy, xx = np.indices((h, w))
    world_x = xx * sx
    world_z = yy * sz
    dist_lake = np.hypot((world_x - lake_cx) / lake_rx, (world_z - lake_cz) / lake_rz)
    lake = (dist_lake <= 1.0) & wet

    rock_items = []
    occupied = []

    def is_free(x, z, min_dist):
        return all(np.hypot(x - ox, z - oz) >= (min_dist + orad * 0.35) for ox, oz, orad in occupied)

    def add_rock(x, y, z, rScale, hScale, rot, archetype):
        rock_items.append(dict(x=float(x), y=float(y), z=float(z),
                               rScale=float(rScale), hScale=float(hScale),
                               rot=float(rot), archetype=int(archetype)))
        occupied.append((x, z, rScale))

    ARCH_BOULDER = 0     # Granite Tor
    ARCH_CRAG = 1        # Karst Spire
    ARCH_RIVER_STONE = 2 # River Cobble
    ARCH_GRANITE = 3     # Granite Block / Cleavage
    ARCH_BASALT = 4      # Hexagonal Columnar Basalt
    ARCH_SANDSTONE = 5   # Tabular Sedimentary Strata
    ARCH_LIMESTONE = 6   # Karst Pinnacle
    ARCH_OBSIDIAN = 7    # Conchoidal Glass Shard
    ARCH_SLATE = 8       # Foliated Cleavage Slabs
    ARCH_SCREE = 9       # Shattered Gravel

    # -------------------------------------------------------------
    # 1. ĐÁ TO (Mega / Large Boulders & Spires)
    # -------------------------------------------------------------
    # 1.1 Summit Massif (Granite & Obsidian)
    peak_r, peak_c = np.unravel_index(np.argmax(np.where(valid, ground, -999.0)), ground.shape)
    peak_x, peak_z, peak_y = float(peak_c * sx), float(peak_r * sz), float(ground[peak_r, peak_c])
    add_rock(peak_x - 1.2, peak_y, peak_z - 0.8, 3.8, 4.5, 45.0, ARCH_GRANITE)
    add_rock(peak_x + 1.5, peak_y, peak_z + 1.2, 1.9, 5.2, 115.0, ARCH_OBSIDIAN)
    add_rock(peak_x - 3.5, peak_y - 0.4, peak_z + 2.8, 2.8, 3.4, 210.0, ARCH_GRANITE)

    # 1.2 Western Island Precipice (Prominent cliff face in camera view: X in [62, 78], Z in [125, 185])
    # Place majestic Columnar Basalt pillars and Karst spires cascading down to the cloud sea!
    west_precipice_mask = valid & ~wet & (world_x >= 62.0) & (world_x <= 78.0) & (world_z >= 125.0) & (world_z <= 185.0) & (slope >= 18.0)
    for idx in np.argsort(-slope.ravel()):
        r, c = divmod(int(idx), w)
        if not west_precipice_mask[r, c]: continue
        x, z = float(c * sx), float(r * sz)
        if is_free(x, z, 4.8):
            rScale = 2.4 + 1.2 * ((len(rock_items) * 17) % 10) / 10.0
            hScale = rScale * (1.6 + 0.7 * ((len(rock_items) * 29) % 10) / 10.0)
            rot = float((len(rock_items) * 73) % 360)
            # Columnar basalt and karst pinnacles dominating the cliff face
            arch = ARCH_BASALT if (len(rock_items) % 2 == 0) else ARCH_LIMESTONE
            add_rock(x, ground[r, c], z, rScale, hScale, rot, arch)
            if len([r for r in rock_items if 62.0 <= r['x'] <= 78.0 and 125.0 <= r['z'] <= 185.0 and r['rScale'] >= 2.0]) >= 20:
                break

    # 1.3 High Ridge Karst Spires (Limestone & Granite) across the rest of the island
    ridge_score = np.where(valid & ~wet & (ground > 4.2) & (tpi > 2.0) & (slope > 12.0),
                           tpi * 0.6 + (ground / 13.0) * 0.4, 0.0)
    for idx in np.argsort(-ridge_score.ravel()):
        r, c = divmod(int(idx), w)
        if ridge_score[r, c] <= 0.12: break
        x, z = float(c * sx), float(r * sz)
        if is_free(x, z, 14.0):
            rScale = 2.4 + 1.1 * ((len(rock_items) * 19) % 10) / 10.0
            hScale = rScale * (1.7 + 0.6 * ((len(rock_items) * 23) % 10) / 10.0)
            rot = float((len(rock_items) * 67) % 360)
            arch = ARCH_LIMESTONE if (len(rock_items) % 2 == 0) else ARCH_GRANITE
            add_rock(x, ground[r, c], z, rScale, hScale, rot, arch)
            if len([r for r in rock_items if r['rScale'] >= 2.2]) >= 26: break

    # 1.4 Outer Perimeter Rim Drop-Offs (facing cloud sea)
    rim_cliff_score = np.where(valid & ~wet & (ground >= -1.4) & (ground <= 8.5) & (slope >= 20.0),
                               slope * 0.7 + (8.5 - np.abs(ground - 3.5)) * 0.3, 0.0)
    for idx in np.argsort(-rim_cliff_score.ravel()):
        r, c = divmod(int(idx), w)
        if rim_cliff_score[r, c] <= 5.0: break
        x, z = float(c * sx), float(r * sz)
        if is_free(x, z, 9.0):
            rScale = 2.2 + 1.1 * ((len(rock_items) * 31) % 10) / 10.0
            hScale = rScale * (1.5 + 0.6 * ((len(rock_items) * 37) % 10) / 10.0)
            rot = float((len(rock_items) * 83) % 360)
            m = len(rock_items) % 3
            arch = ARCH_BASALT if m == 0 else (ARCH_SANDSTONE if m == 1 else ARCH_LIMESTONE)
            add_rock(x, ground[r, c], z, rScale, hScale, rot, arch)
            if len([r for r in rock_items if r['rScale'] >= 2.0]) >= 48: break

    # -------------------------------------------------------------
    # 2. ĐÁ TRUNG (Medium Boulders & Outcrops)
    # -------------------------------------------------------------
    # 2.1 Meadow Knolls & Rolling Slopes (Granite & Sandstone)
    meadow_score = np.where(valid & ~wet & (ground > 0.0) & (ground < 8.5) &
                            (slope > 5.5) & (slope < 20.0) & (tpi > 0.1),
                            slope * 0.5 + tpi * 0.5, 0.0)
    for idx in np.argsort(-meadow_score.ravel()):
        r, c = divmod(int(idx), w)
        if meadow_score[r, c] <= 0.06: break
        x, z = float(c * sx), float(r * sz)
        if is_free(x, z, 7.0):
            rScale = 1.25 + 0.65 * ((len(rock_items) * 13) % 10) / 10.0
            hScale = rScale * (0.65 + 0.35 * ((len(rock_items) * 29) % 10) / 10.0)
            rot = float((len(rock_items) * 53) % 360)
            arch = ARCH_GRANITE if (len(rock_items) % 2 == 0) else ARCH_SANDSTONE
            add_rock(x, ground[r, c], z, rScale, hScale, rot, arch)
            if len([r for r in rock_items if 1.1 <= r['rScale'] < 2.0]) >= 45: break

    # 2.2 Talus Crests & Mid-Slopes (Slate, Basalt & Sandstone)
    talus_crest_score = np.where(valid & ~wet & (slope > 14.0) & (slope < 28.0) & (ground > -1.2),
                                 slope, 0.0)
    for idx in np.argsort(-talus_crest_score.ravel()):
        r, c = divmod(int(idx), w)
        if talus_crest_score[r, c] <= 4.0: break
        x, z = float(c * sx), float(r * sz)
        if is_free(x, z, 5.8):
            rScale = 1.15 + 0.55 * ((len(rock_items) * 11) % 10) / 10.0
            hScale = rScale * (0.60 + 0.45 * ((len(rock_items) * 17) % 10) / 10.0)
            rot = float((len(rock_items) * 71) % 360)
            m = len(rock_items) % 3
            arch = ARCH_SLATE if m == 0 else (ARCH_BASALT if m == 1 else ARCH_SANDSTONE)
            add_rock(x, ground[r, c], z, rScale, hScale, rot, arch)
            if len([r for r in rock_items if 1.1 <= r['rScale'] < 2.0]) >= 75: break

    # 2.3 Cascade Breakers (Large river boulders dividing water current)
    cascade_score = np.where(streams & (slope > 2.8), slope * water_depth, 0.0)
    for idx in np.argsort(-cascade_score.ravel()):
        r, c = divmod(int(idx), w)
        if cascade_score[r, c] <= 0.04: break
        x, z = float(c * sx), float(r * sz)
        if is_free(x, z, 5.0):
            rScale = 1.05 + 0.45 * ((len(rock_items) * 23) % 10) / 10.0
            hScale = rScale * 0.65
            rot = float(aspect[r, c] + 90.0)
            add_rock(x, ground[r, c], z, rScale, hScale, rot, ARCH_GRANITE)
            if len([r for r in rock_items if r['archetype'] == ARCH_GRANITE and r['rScale'] <= 1.6 and r['rScale'] >= 1.0]) >= 18: break

    # -------------------------------------------------------------
    # 3. ĐÁ NHỎ & VỆ TINH (Small Rocks & Companion Stones)
    # -------------------------------------------------------------
    # 3.1 Companions orbiting large & medium anchors
    anchors = [r for r in rock_items if r['rScale'] >= 1.15]
    for i, anc in enumerate(anchors):
        comp_count = 2 if anc['rScale'] < 2.0 else 3
        for c_idx in range(comp_count):
            ang = float((i * 110 + c_idx * 145) % 360) * np.pi / 180.0
            dist = anc['rScale'] * (1.1 + 0.6 * c_idx)
            cx_pos = anc['x'] + np.cos(ang) * dist
            cz_pos = anc['z'] + np.sin(ang) * dist
            r_grid = int(np.clip(cz_pos / sz, 0, h - 1))
            c_grid = int(np.clip(cx_pos / sx, 0, w - 1))
            if valid[r_grid, c_grid] and not wet[r_grid, c_grid]:
                c_scale = anc['rScale'] * (0.35 + 0.15 * c_idx)
                c_h = c_scale * (0.55 + 0.20 * c_idx)
                c_rot = anc['rot'] + 40.0 * (c_idx + 1)
                c_arch = anc['archetype'] if (anc['archetype'] != ARCH_OBSIDIAN) else ARCH_GRANITE
                add_rock(cx_pos, ground[r_grid, c_grid], cz_pos, c_scale, c_h, c_rot, c_arch)

    # 3.2 Scattered Trailside & Margin Stones
    margin_score = np.where(valid & ~wet & (ground > -1.0) & (slope > 6.0) & (slope < 24.0),
                            1.0, 0.0)
    for idx in np.argsort(-margin_score.ravel())[::5]:
        r, c = divmod(int(idx), w)
        x, z = float(c * sx), float(r * sz)
        if is_free(x, z, 4.0):
            rScale = 0.50 + 0.38 * ((len(rock_items) * 27) % 10) / 10.0
            hScale = rScale * (0.50 + 0.25 * ((len(rock_items) * 19) % 10) / 10.0)
            rot = float((len(rock_items) * 41) % 360)
            arch = ARCH_SANDSTONE if (len(rock_items) % 2 == 0) else ARCH_GRANITE
            add_rock(x, ground[r, c], z, rScale, hScale, rot, arch)
            if len([r for r in rock_items if 0.45 <= r['rScale'] < 1.0]) >= 135: break

    # -------------------------------------------------------------
    # 4. ĐÁ VỤN (Scree / Talus Aprons & Gravel)
    # -------------------------------------------------------------
    scree_score = np.where(valid & ~wet & (ground > -2.2) & (slope > 16.0) & (slope < 38.0),
                           slope, 0.0)
    for idx in np.argsort(-scree_score.ravel())[::2]:
        r, c = divmod(int(idx), w)
        x, z = float(c * sx), float(r * sz)
        if is_free(x, z, 1.8):
            rScale = 0.20 + 0.18 * ((len(rock_items) * 37) % 10) / 10.0
            hScale = rScale * (0.45 + 0.25 * ((len(rock_items) * 23) % 10) / 10.0)
            rot = float((len(rock_items) * 59) % 360)
            arch = ARCH_SLATE if (len(rock_items) % 3 == 0) else ARCH_SCREE
            add_rock(x, ground[r, c], z, rScale, hScale, rot, arch)
            if len([r for r in rock_items if r['archetype'] in (ARCH_SCREE, ARCH_SLATE) and r['rScale'] < 0.45]) >= 150: break

    # -------------------------------------------------------------
    # 5. ĐÁ Ở SÔNG SUỐI (River Stones & Fluvial Cobbles)
    # -------------------------------------------------------------
    # 5.1 In-stream channel rapids (oriented with water current)
    stream_candidates = np.where(streams & ~lake)
    if len(stream_candidates[0]) > 0:
        step = max(1, len(stream_candidates[0]) // 110)
        for i in range(0, len(stream_candidates[0]), step):
            r = int(stream_candidates[0][i])
            c = int(stream_candidates[1][i])
            x, z = float(c * sx), float(r * sz)
            if is_free(x, z, 1.8):
                rScale = 0.42 + 0.38 * ((i * 19) % 10) / 10.0
                hScale = rScale * (0.32 + 0.18 * ((i * 31) % 10) / 10.0) # flattened hydro profile
                rot = float(aspect[r, c]) # aligned with hydrodynamic stream flow
                add_rock(x, ground[r, c], z, rScale, hScale, rot, ARCH_RIVER_STONE)
                if len([r for r in rock_items if r['archetype'] == ARCH_RIVER_STONE]) >= 85: break

    # 5.2 Stepping stone crossings across the river in front of camera
    # At Z in [148, 154], X in [98, 105], place a natural diagonal chain of stepping stones
    for step_i in range(5):
        st_x = 98.5 + step_i * 1.5
        st_z = 153.0 - step_i * 0.9
        r_grid = int(np.clip(st_z / sz, 0, h - 1))
        c_grid = int(np.clip(st_x / sx, 0, w - 1))
        st_r = 0.55 + 0.15 * ((step_i * 7) % 5) / 5.0
        st_h = 0.28
        add_rock(st_x, ground[r_grid, c_grid], st_z, st_r, st_h, 35.0 * step_i, ARCH_RIVER_STONE)

    # 5.3 Waterfront Lake Shoreline & Shallows
    shore_mask = valid & (dist_lake >= 0.82) & (dist_lake <= 1.45) & (ground > -0.6)
    shore_candidates = np.where(shore_mask)
    if len(shore_candidates[0]) > 0:
        step = max(1, len(shore_candidates[0]) // 50)
        for i in range(0, len(shore_candidates[0]), step):
            r = int(shore_candidates[0][i])
            c = int(shore_candidates[1][i])
            x, z = float(c * sx), float(r * sz)
            if is_free(x, z, 2.0):
                rScale = 0.36 + 0.32 * ((i * 23) % 10) / 10.0
                hScale = rScale * 0.42
                rot = float((i * 47) % 360)
                add_rock(x, ground[r, c], z, rScale, hScale, rot, ARCH_RIVER_STONE)
                if len([r for r in rock_items if r['archetype'] == ARCH_RIVER_STONE]) >= 110: break

    # 5.4 Foreground Pond Edge & Shallows (west hollow at x in [80, 93], z in [154, 174])
    front_pond_mask = valid & (world_x >= 80.0) & (world_x <= 93.0) & (world_z >= 154.0) & (world_z <= 174.0) & (ground < 1.4)
    fp_candidates = np.where(front_pond_mask)
    if len(fp_candidates[0]) > 0:
        for i in range(len(fp_candidates[0])):
            r = int(fp_candidates[0][i])
            c = int(fp_candidates[1][i])
            x, z = float(c * sx), float(r * sz)
            if is_free(x, z, 1.4):
                rScale = 0.34 + 0.28 * ((i * 17) % 10) / 10.0
                hScale = rScale * 0.38
                rot = float((i * 53) % 360)
                add_rock(x, ground[r, c], z, rScale, hScale, rot, ARCH_RIVER_STONE)
                if len([r for r in rock_items if r['archetype'] == ARCH_RIVER_STONE]) >= 130: break

    print(f"Total rocks generated: {len(rock_items)}")
    arch_names = {
        0: "BOULDER", 1: "CRAG", 2: "RIVER_STONE", 3: "GRANITE",
        4: "BASALT", 5: "SANDSTONE", 6: "LIMESTONE", 7: "OBSIDIAN",
        8: "SLATE", 9: "SCREE"
    }
    counts = {}
    for r in rock_items:
        a = r['archetype']
        counts[a] = counts.get(a, 0) + 1
    for a, cnt in sorted(counts.items()):
        print(f"  {arch_names.get(a, str(a))}: {cnt} items")

    # Write C header
    header_path = "assets/maps/song_quao/song_quao_rock_data.h"
    with open(header_path, "w") as f:
        f.write("// Geomorphological & Geological Rock Placements for Song Quao Map\n")
        f.write("#ifndef SONG_QUAO_ROCK_DATA_H\n")
        f.write("#define SONG_QUAO_ROCK_DATA_H\n\n")
        f.write('#include "maps/toolkit/map_props.h"\n\n')
        f.write("static const MapRockItem SONG_QUAO_ROCK_ITEMS[] = {\n")
        for item in rock_items:
            arch_str = f"MAP_ROCK_{arch_names[item['archetype']]}"
            f.write(f'    {{{{{item["x"]:.2f}f, {item["y"]:.2f}f, {item["z"]:.2f}f}}, {item["rScale"]:.2f}f, {item["hScale"]:.2f}f, {item["rot"]:.1f}f, {arch_str}}},\n')
        f.write("};\n")
        f.write("#define SONG_QUAO_ROCK_ITEM_COUNT (int)(sizeof(SONG_QUAO_ROCK_ITEMS) / sizeof(SONG_QUAO_ROCK_ITEMS[0]))\n\n")
        f.write("#endif // SONG_QUAO_ROCK_DATA_H\n")

    # Write JSON
    json_path = "assets/maps/song_quao/song_quao_rock_placements.json"
    with open(json_path, "w") as f:
        json.dump(rock_items, f, indent=2)

    print(f"Written: {header_path} and {json_path}")

if __name__ == "__main__":
    generate_rocks()
