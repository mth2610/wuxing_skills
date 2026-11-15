#!/usr/bin/env python3
"""
Bake Ninh Thuan (Earth / He Tho) map assets from the song_quao DEM analysis:
  1. Applies GPU Gems 3 Ch. 1 Pre-Warped Bedrock Terracing to ground_y_m.
  2. Drains ~75% upstream channels into a dry boulder-strewn wadi, keeping
     a shallow emerald desert oasis pool in the lowest basin.
  3. Generates arid ecology/splat/debris/heightfield/water assets in assets/maps/ninh_thuan/.
  4. Generates dense Ninh Thuan Stone Park exfoliation tors, stacked balanced
     corestones, joint-controlled boulder alleys, dry wadi cobbles, and talus
     scree into maps/worlds/ninh_thuan/ninh_thuan_rocks_generated.h.
"""

import json
import os
import sys
import numpy as np

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(os.path.dirname(SCRIPT_DIR))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from dem_pipeline import (
    distance_inside,
    gaussian_filter_2d,
    resample_grid,
    write_png,
    write_water_mesh,
)


def bake_ninh_thuan():
    src_npz_path = os.path.join(ROOT_DIR, "assets/maps/song_quao/song_quao_analysis.npz")
    src_manifest_path = os.path.join(ROOT_DIR, "assets/maps/song_quao/song_quao_manifest.json")
    out_assets_dir = os.path.join(ROOT_DIR, "assets/maps/ninh_thuan")
    out_world_dir = os.path.join(ROOT_DIR, "maps/worlds/ninh_thuan")
    os.makedirs(out_assets_dir, exist_ok=True)
    os.makedirs(out_world_dir, exist_ok=True)

    with open(src_manifest_path, "r") as f:
        manifest = json.load(f)

    width = float(manifest["game"]["width_m"])
    depth = float(manifest["game"]["depth_m"])
    height_range = float(manifest["game"]["height_range_m"])
    cliff_depth = float(manifest["game"]["cliff_depth_m"])
    total_range = height_range + cliff_depth

    data = np.load(src_npz_path)
    valid = data["valid"].astype(bool)
    ground_src = data["ground_y_m"].astype(np.float64)
    water_y_src = data["water_y_m"].astype(np.float64)
    water_depth_src = data["water_depth_m"].astype(np.float64)
    slope_deg = data["slope_deg"].astype(np.float64)
    tpi_m = data["tpi_m"].astype(np.float64)
    twi = data["twi"].astype(np.float64)
    accum = data["accumulation_cells"].astype(np.float64)
    landform = data["landform"].astype(np.uint8)

    h, w = ground_src.shape
    sx, sz = width / (w - 1), depth / (h - 1)
    X, Z = np.meshgrid(np.arange(w) * sx, np.arange(h) * sz)

    # ── 1. GPU Gems 3 Ch. 1 Pre-Warped Bedrock Terracing ──
    # Carve stepped granite benches & exposed rock shelves onto arid hillsides
    # while keeping the central gameplay arena (94.1, 150.0, r=16m) smoothly playable.
    warp_x = np.sin(X * 0.045 + Z * 0.031) * 1.40 + np.cos(Z * 0.088 - X * 0.052) * 0.70
    step_h = 1.65
    tau = (ground_src + warp_x * 0.55) / step_h
    frac = tau - np.floor(tau)
    s_frac = np.clip((frac - 0.16) / (0.84 - 0.16), 0.0, 1.0)
    s_frac = s_frac * s_frac * (3.0 - 2.0 * s_frac)
    terraced = (np.floor(tau) + s_frac) * step_h - warp_x * 0.55

    dist_arena = np.hypot(X - 94.10, Z - 150.0)
    arena_guard = np.clip((dist_arena - 14.0) / 10.0, 0.0, 1.0)
    outcrop_w = np.clip((ground_src - 2.0) / 6.0, 0.0, 0.50) * arena_guard
    terrain = np.where(valid, ground_src * (1.0 - outcrop_w) + terraced * outcrop_w, -cliff_depth)

    # ── 2. Dry Wadi + Shallow Desert Oasis Pool ──
    # Retain surface water only in the lowest oasis basin (landform == 4)
    lake = valid & (landform == 4)
    oasis_zone = gaussian_filter_2d(lake.astype(np.float64), 2.5) > 0.12
    water_plane = np.where(oasis_zone, water_y_src, terrain - 0.25)
    water_depth = np.where(valid & oasis_zone, np.maximum(0.0, water_plane - terrain), 0.0)
    wet = valid & oasis_zone & (water_depth > 0.002)

    height_code = np.rint(np.clip((terrain + cliff_depth) / total_range, 0.0, 1.0) * 65535.0).astype(np.uint16)
    ground = height_code.astype(np.float64) / 65535.0 * total_range - cliff_depth
    encoded = np.rint(height_code.astype(np.float64) / 257.0).astype(np.uint8)

    water_code = np.rint(np.clip((water_plane + cliff_depth) / total_range, 0.0, 1.0) * 65535.0).astype(np.uint16)
    surface = water_code.astype(np.float64) / 65535.0 * total_range - cliff_depth

    # ── 3. Arid Earth-Biome Splat & Ecology Maps ──
    gy, gx = np.gradient(ground, sz, sx)
    slope_local = np.degrees(np.arctan(np.hypot(gx, gy)))
    slope_s = gaussian_filter_2d(slope_local, 1.1)
    perimeter = distance_inside(valid, sx, sz)
    cliff = np.clip(perimeter / 1.0, 0.0, 1.0)
    cliff = gaussian_filter_2d(cliff * cliff * (3.0 - 2.0 * cliff), 1.4)

    wadi_channel = (water_depth_src > 0.02) & (~oasis_zone)
    wadi_band = gaussian_filter_2d(wadi_channel.astype(np.float64), 1.8)

    # Exposed granite bedrock on ridges, slopes, and dry wadi banks
    rock_slope = np.clip((slope_s - 8.5) / 11.0, 0.0, 1.0)
    rock_ridge = np.clip((tpi_m - 1.5) / 8.0, 0.0, 1.0) * np.clip((ground - 1.5) / 5.5, 0.0, 1.0)
    rock = np.maximum(np.maximum(rock_slope, rock_ridge), 1.0 - cliff)
    rock = np.maximum(rock, wadi_band * 0.52)
    rock[water_depth > 0.01] = 0.0

    shore = gaussian_filter_2d(wet.astype(np.float64), max(1.1, 2.2 / min(sx, sz)))
    moisture = np.clip(shore * 0.90 + wadi_band * 0.18, 0.0, 1.0)

    # 2x Super-resolved maps matching song_quao layout
    h2, w2 = h * 2 - 1, w * 2 - 1
    sx2, sz2 = width / (w2 - 1), depth / (h2 - 1)
    up2 = lambda a: resample_grid(a.astype(np.float64), h2, w2)
    valid2 = up2(valid.astype(float)) > 0.5
    ground2 = gaussian_filter_2d(up2(ground), 0.85)
    water_plane2 = gaussian_filter_2d(up2(water_plane), 1.15)
    elev_diff2 = ground2 - water_plane2
    wet2 = valid2 & (elev_diff2 < -0.002) & (up2(oasis_zone.astype(float)) > 0.5)
    raw_dist2 = distance_inside(~wet2, sx2, sz2) - distance_inside(wet2, sx2, sz2)
    gy2, gx2 = np.gradient(elev_diff2, sz2, sx2)
    grad2 = np.maximum(np.hypot(gx2, gy2), 0.10)
    subpixel_zero2 = np.clip(elev_diff2 / grad2, -1.6, 1.6)
    near_zero_w2 = np.exp(-0.5 * (raw_dist2 / 1.15) ** 2)
    signed_shore2 = gaussian_filter_2d(raw_dist2 * (1.0 - near_zero_w2) + subpixel_zero2 * near_zero_w2, 1.65)

    shore2 = np.clip(1.0 - (signed_shore2 + 0.35) / 2.65, 0.0, 1.0)
    shore2 = shore2 * shore2 * (3.0 - 2.0 * shore2)
    wet_smooth2 = np.clip(1.0 - (signed_shore2 + 0.10) / 0.85, 0.0, 1.0)
    wet_smooth2 = wet_smooth2 * wet_smooth2 * (3.0 - 2.0 * wet_smooth2)

    rock2 = np.clip(up2(rock), 0.0, 1.0)
    slope2 = up2(slope_s)
    tpi2 = up2(tpi_m)
    moisture2 = up2(moisture)
    # Sparse xerophytic coverage: concentrated near oasis & shaded rock hollows
    coverage2 = np.clip((1.0 - rock2 * 0.90) * (1.0 - wet_smooth2) * (0.22 + 0.58 * moisture2), 0.0, 1.0)
    coverage2[~valid2] = 0.0
    soil2 = np.clip((1.0 - coverage2) * (1.0 - rock2) * (1.0 - wet_smooth2) * 1.15, 0.0, 1.0)
    wet_mud2 = np.clip(np.maximum(shore2 * 0.78, wet_smooth2), 0.0, 1.0)
    habitat_val2 = np.clip(0.55 + 0.30 * np.clip((tpi2 + 4.0) / 12.0, 0.0, 1.0) - 0.35 * moisture2, 0.0, 1.0)

    scree_gravel2 = np.clip((slope2 - 6.0) / 12.0, 0.0, 1.0) * (0.45 + 0.55 * rock2)
    scree_gravel2 = np.maximum(scree_gravel2, up2(wadi_band) * 0.75)
    leaf_litter2 = np.zeros_like(rock2)
    twigs2 = np.clip(coverage2 * 0.35, 0.0, 1.0)
    moss2 = np.zeros_like(rock2)
    flowers2 = np.clip(coverage2 * 0.25, 0.0, 1.0)

    splat2 = np.stack((coverage2, rock2, flowers2, shore2), axis=-1)
    splat2[~valid2] = (0, 1, 0, 0)
    habitat2 = np.stack((coverage2, coverage2, moisture2, habitat_val2), axis=-1)
    habitat2[~valid2] = 0.0
    debris_map2 = np.stack((leaf_litter2, scree_gravel2, twigs2, moss2), axis=-1)
    debris_map2[~valid2] = 0.0
    ground_material2 = np.stack((coverage2, soil2, wet_mud2, rock2), axis=-1)
    ground_material2[~valid2] = (0, 0, 0, 1)

    sunny2 = np.clip(tpi2 / 15.0 + 0.5, 0.0, 1.0)
    tint_map2 = np.stack((
        np.clip(0.58 + 0.22 * sunny2, 0.0, 1.0),
        np.clip(0.46 + 0.16 * sunny2, 0.0, 1.0),
        np.clip(0.32 + 0.12 * rock2, 0.0, 1.0),
    ), axis=-1)
    tint_map2[~valid2] = (0.45, 0.38, 0.30)

    p = lambda s: os.path.join(out_assets_dir, "ninh_thuan_" + s)
    write_png(p("island_heightmap.png"), encoded)
    height_rgba = np.stack((height_code >> 8, height_code & 255, np.zeros_like(height_code), np.full_like(height_code, 255)), axis=-1)
    write_png(p("heightfield.png"), height_rgba, 6)
    write_png(p("ecology_splat.png"), np.rint(splat2 * 255), 6)
    write_png(p("ground_material.png"), np.rint(ground_material2 * 255), 6)
    write_png(p("debris_map.png"), np.rint(debris_map2 * 255), 6)
    write_png(p("ground_tint.png"), np.rint(tint_map2 * 255), 2)
    write_png(p("habitat.png"), np.rint(habitat2 * 255), 6)

    riparian_b = np.rint(np.clip(gaussian_filter_2d(wet.astype(float), 2.8) * 3.5, 0.0, 1.0) * 255).astype(np.uint8)
    water_rgba = np.stack((water_code >> 8, water_code & 255, riparian_b, lake * 255), axis=-1).astype(np.uint8)
    write_png(p("water_height.png"), water_rgba, 6)

    shore_code2 = np.rint((np.clip(signed_shore2 / 16.0, -1.0, 1.0) * 0.5 + 0.5) * 65535).astype(np.uint16)
    distance2 = np.stack((np.full_like(shore_code2, 255), np.full_like(shore_code2, 255), shore_code2 >> 8, shore_code2 & 255), axis=-1)
    write_png(p("habitat_distance.png"), distance2, 6)

    triangles = write_water_mesh(p("water.obj"), ground, water_plane, sx, sz)

    lys, lxs = np.where(lake)
    cx = float(lxs.mean() * sx) if lxs.size else 170.68
    cz = float(lys.mean() * sz) if lys.size else 230.41
    rx = float((lxs.max() - lxs.min() + 1) * sx / 2) if lxs.size else 14.5
    rz = float((lys.max() - lys.min() + 1) * sz / 2) if lys.size else 12.5
    water_y_val = float(np.median(surface[lake])) if lake.any() else 0.196
    bed_min_y = float(ground[lake].min()) if lake.any() else -0.35

    meta_h = os.path.join(out_assets_dir, "ninh_thuan_generated_meta.h")
    with open(meta_h, "w") as f:
        f.write("// Generated by maps/pipeline/bake_ninh_thuan.py\n")
        f.write("#ifndef NINH_THUAN_GENERATED_META_H\n#define NINH_THUAN_GENERATED_META_H\n")
        f.write(f"#define NINH_THUAN_MAP_WIDTH {width:.6f}f\n")
        f.write(f"#define NINH_THUAN_MAP_DEPTH {depth:.6f}f\n")
        f.write(f"#define NINH_THUAN_HEIGHT_RANGE {height_range:.6f}f\n")
        f.write(f"#define NINH_THUAN_CLIFF_DEPTH {cliff_depth:.6f}f\n")
        f.write(f"#define NINH_THUAN_LAKE_CENTER_X {cx:.6f}f\n")
        f.write(f"#define NINH_THUAN_LAKE_CENTER_Z {cz:.6f}f\n")
        f.write(f"#define NINH_THUAN_LAKE_RADIUS_X {rx:.6f}f\n")
        f.write(f"#define NINH_THUAN_LAKE_RADIUS_Z {rz:.6f}f\n")
        f.write(f"#define NINH_THUAN_LAKE_WATER_Y {water_y_val:.6f}f\n")
        f.write(f"#define NINH_THUAN_LAKE_BED_MIN_Y {bed_min_y:.6f}f\n")
        f.write("#endif // NINH_THUAN_GENERATED_META_H\n")

    # ── 4. Ninh Thuan Stone Park Rock & Exfoliation Tor Placement ──
    rng = np.random.default_rng(20261115)
    placements = []
    occupied = []

    def sample_cell(x, z):
        r = int(np.clip(z / sz, 0, h - 1))
        c = int(np.clip(x / sx, 0, w - 1))
        return float(ground[r, c]), float(slope_s[r, c]), float(tpi_m[r, c]), bool(valid[r, c]), bool(wet[r, c]), bool(wadi_channel[r, c])

    def can_place(x, z, r_scale, min_sep=0.58):
        if x < 20.0 or x > width - 20.0 or z < 16.0 or z > depth - 16.0:
            return False
        # Keep central combat arena clear (radius 15m around 94.1, 150.0)
        if np.hypot(x - 94.10, z - 150.0) < 15.5 + r_scale:
            return False
        for ox, oz, orad in occupied:
            if np.hypot(x - ox, z - oz) < (r_scale + orad) * min_sep:
                return False
        return True

    def add_rock(x, z, r_scale, h_scale, rot, arch, allow_stack=False):
        gy, sl, tpi_v, val, is_wet, _ = sample_cell(x, z)
        if not val or is_wet or gy < 0.25 or sl > 34.0:
            return False
        if not allow_stack and not can_place(x, z, r_scale):
            return False
        placements.append((x, gy, z, r_scale, h_scale, rot % 360.0, arch))
        if not allow_stack:
            occupied.append((x, z, r_scale))
        return True

    # 4A. Monumental Stacked Exfoliation Tors (Nui Chua Corestones)
    tor_candidates = []
    for r in range(18, h - 18, 5):
        for c in range(18, w - 18, 5):
            if not valid[r, c] or wet[r, c] or ground[r, c] < 1.2 or slope_s[r, c] > 24.0:
                continue
            x, z = c * sx, r * sz
            if np.hypot(x - 94.10, z - 150.0) < 19.0:
                continue
            score = tpi_m[r, c] * 0.45 + rock[r, c] * 3.5 + rng.uniform(0.0, 1.5)
            tor_candidates.append((score, x, z))
    tor_candidates.sort(reverse=True)

    tors_placed = 0
    for _, tx, tz in tor_candidates:
        if tors_placed >= 26:
            break
        r_base = float(rng.uniform(2.1, 3.6))
        h_base = r_base * float(rng.uniform(0.72, 0.95))
        rot_base = float(rng.uniform(0.0, 360.0))
        if not can_place(tx, tz, r_base * 1.15, min_sep=1.35):
            continue
        if add_rock(tx, tz, r_base, h_base, rot_base, "MAP_ROCK_GRANITE"):
            tors_placed += 1
            # Perched balanced upper corestone (stacked tor!)
            gy_base = placements[-1][1]
            top_r = r_base * float(rng.uniform(0.56, 0.72))
            top_h = top_r * float(rng.uniform(0.82, 1.12))
            off_ang = np.radians(rot_base + rng.uniform(-35.0, 35.0))
            off_d = r_base * 0.18
            px = tx + np.cos(off_ang) * off_d
            pz = tz + np.sin(off_ang) * off_d
            py = gy_base + h_base * 0.62
            placements.append((px, py, pz, top_r, top_h, (rot_base + 48.0) % 360.0, "MAP_ROCK_GRANITE"))

            # Companion flanking boulders & scree apron around tor
            for k in range(3):
                ang = np.radians(rot_base + k * 115.0 + rng.uniform(-22.0, 22.0))
                dist = r_base + float(rng.uniform(0.6, 1.8))
                cr = float(rng.uniform(0.65, 1.45))
                ch = cr * float(rng.uniform(0.55, 0.85))
                arch = "MAP_ROCK_SANDSTONE" if (k == 1 and rng.random() < 0.40) else "MAP_ROCK_GRANITE"
                add_rock(tx + np.cos(ang) * dist, tz + np.sin(ang) * dist, cr, ch, float(rng.uniform(0, 360)), arch)
            for s in range(4):
                ang = rng.uniform(0.0, 2.0 * np.pi)
                dist = r_base + float(rng.uniform(1.0, 3.2))
                sr = float(rng.uniform(0.28, 0.62))
                add_rock(tx + np.cos(ang) * dist, tz + np.sin(ang) * dist, sr, sr * 0.52, float(rng.uniform(0, 360)), "MAP_ROCK_SCREE")

    # 4B. Joint-Controlled Granite Boulder Alleys & Tafoni Outcrops
    strike_rad = np.radians(48.0)
    for _ in range(1800):
        if len(placements) >= 620:
            break
        x = float(rng.uniform(22.0, width - 22.0))
        z = float(rng.uniform(18.0, depth - 18.0))
        gy, sl, tpi_v, val, is_wet, is_wadi = sample_cell(x, z)
        if not val or is_wet or gy < 0.35 or sl > 30.0:
            continue
        # Higher probability on rocky outcrops & wadi banks
        prob = 0.25 + 0.55 * float(rock[int(np.clip(z / sz, 0, h - 1)), int(np.clip(x / sx, 0, w - 1))])
        if rng.random() > prob:
            continue
        r_s = float(rng.uniform(0.75, 1.95))
        h_s = r_s * float(rng.uniform(0.55, 0.90))
        rot = float(np.degrees(strike_rad) + rng.uniform(-18.0, 18.0))
        arch = "MAP_ROCK_SANDSTONE" if rng.random() < 0.24 else ("MAP_ROCK_BOULDER" if rng.random() < 0.28 else "MAP_ROCK_GRANITE")
        if is_wadi and rng.random() < 0.55:
            arch = "MAP_ROCK_RIVER_STONE"
            h_s = r_s * 0.48
        if add_rock(x, z, r_s, h_s, rot, arch):
            # Add 1-2 scree chips nearby
            for _ in range(int(rng.integers(1, 3))):
                a = rng.uniform(0.0, 2.0 * np.pi)
                d = r_s + float(rng.uniform(0.4, 1.3))
                sr = float(rng.uniform(0.22, 0.52))
                add_rock(x + np.cos(a) * d, z + np.sin(a) * d, sr, sr * 0.48, float(rng.uniform(0, 360)), "MAP_ROCK_SCREE")

    rock_header = os.path.join(out_world_dir, "ninh_thuan_rocks_generated.h")
    with open(rock_header, "w") as f:
        f.write("// Generated by maps/pipeline/bake_ninh_thuan.py\n")
        f.write("// Ninh Thuan Stone Park: Exfoliated Granite Tors, Stacked Kopjes, Tafoni & Dry Wadi Talus\n")
        f.write("#ifndef NINH_THUAN_ROCKS_GENERATED_H\n#define NINH_THUAN_ROCKS_GENERATED_H\n\n")
        f.write('#include "maps/toolkit/map_props.h"\n\n')
        f.write("static const MapRockItem NINH_THUAN_ROCK_ITEMS[] = {\n")
        for x, y, z, r_s, h_s, rot, arch in placements:
            f.write(f"    {{{{{x:.2f}f, {y:.2f}f, {z:.2f}f}}, {r_s:.2f}f, {h_s:.2f}f, {rot:.1f}f, {arch}}},\n")
        f.write("};\n\n")
        f.write(f"#define NINH_THUAN_ROCK_COUNT {len(placements)}\n\n")
        f.write("#endif // NINH_THUAN_ROCKS_GENERATED_H\n")

    print(
        f"[NinhThuan] Baked {w}x{h} terrain, {triangles} oasis water tris, "
        f"{len(placements)} rocks ({tors_placed} stacked tors) -> {rock_header}",
        flush=True,
    )


if __name__ == "__main__":
    bake_ninh_thuan()
