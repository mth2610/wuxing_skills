#!/usr/bin/env python3
"""
Bake Phú Quốc (Metal / Hệ Kim — Kim Khoáng Bảo Đảo) mineral island map assets
from real projected UTM 48N DEM (assets/maps/phu_quoc/DEM_PhuQuoc_UTM48N.tif):
  1. Carves an Open-Pit Metal Quarry Amphitheater (Khai Trường Lộ Thiên) with
     5 crisp stepped mining benches into the flank of Núi Chúa, keeping the
     western Kim Tinh Đài ore-sorting yard flat & walkable.
  2. Routes real west-flowing D8 rivers (Sông Dương Đông / Rạch Cửa Cạn) from
     the eastern mountain spine across the coastal plain into the Gulf of Thailand
     (NO circular volcanic crater lakes).
  3. Exports 16-bit heightfields (MOBA 1x & Open World 2x), conformal river mesh
     (phu_quoc_water.obj), hydrothermal gold/silver vein debris_map, relief, & LUT.
  4. Places 2 Mine Adit Portals (Cổng Hầm Mỏ), granite/slate boulders (NO
     volcanic obsidian spires), 3D Mineral Ore Crystal Clusters (PHU_QUOC_ORE_ITEMS),
     and 3D gold/silver nugget debris (PHU_QUOC_DEBRIS_ITEMS).
"""

import os
import sys
from collections import deque
import numpy as np

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(os.path.dirname(SCRIPT_DIR))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from dem_pipeline import (
    compute_d8_flow,
    distance_inside,
    gaussian_filter_2d,
    normalized_smooth,
    resample_grid,
    write_png,
    write_water_mesh,
)
from georaster import read_dem


def bake_phu_quoc():
    dem_path = os.path.join(ROOT_DIR, "assets/maps/phu_quoc/DEM_PhuQuoc_UTM48N.tif")
    if not os.path.exists(dem_path):
        from fetch_phu_quoc_dem import main as fetch_main
        fetch_main()

    raw, valid_raw, geo = read_dem(dem_path)
    yy, xx = np.where(valid_raw)
    crop = (slice(yy.min(), yy.max() + 1), slice(xx.min(), xx.max() + 1))
    source = raw[crop]
    valid_source = valid_raw[crop]
    px, py = geo["pixel_size_m"]

    max_dim = 300.0
    height_range = 28.0
    cliff_depth = 8.0
    total_range = height_range + cliff_depth

    physical_w = (source.shape[1] - 1) * px
    physical_h = (source.shape[0] - 1) * py
    scale = max_dim / max(physical_w, physical_h)
    width = physical_w * scale
    depth = physical_h * scale
    res_scale = 1.0
    while True:
        w = max(8, round(width * res_scale) + 1)
        h = max(8, round(depth * res_scale) + 1)
        if w * h <= 65535:
            break
        res_scale *= 0.98
    sx, sz = width / (w - 1), depth / (h - 1)

    clean = normalized_smooth(source, valid_source, 0.7)
    lo = float(source[valid_source].min())
    hi = float(source[valid_source].max())

    sample = lambda a: resample_grid(a, h, w)
    valid = sample(valid_source.astype(float)) > 0.72
    valid[:2] = False
    valid[-2:] = False
    valid[:, :2] = False
    valid[:, -2:] = False

    dem = sample(clean)
    rolling = normalized_smooth(dem, valid, max(0.8, 2.0 / min(sx, sz)))
    normalized = np.clip((rolling - lo) / max(hi - lo, 1e-9), 0.0, 1.0)
    terrain = height_range * np.power(normalized, 0.92)

    X, Z = np.meshgrid(np.arange(w) * sx, np.arange(h) * sz)

    # Central-West Kim Tinh Đài ore-sorting combat yard on the Dương Đông plain
    arena_x, arena_z = float(width * 0.44), float(depth * 0.48)
    arena_dist = np.hypot(X - arena_x, Z - arena_z)

    # ── 1. Open-Pit Metal Quarry Amphitheater (Khai Trường Lộ Thiên) ──
    # Carved into the eastern mountain flank facing the central ore-sorting yard
    quarry_cx, quarry_cz = float(width * 0.62), float(depth * 0.42)
    q_dx = (X - quarry_cx) / 48.0
    q_dz = (Z - quarry_cz) / 56.0
    q_rad = np.hypot(q_dx, q_dz)
    quarry_mask = np.clip(1.0 - np.clip((q_rad - 0.35) / 0.65, 0.0, 1.0), 0.0, 1.0)
    quarry_mask = quarry_mask * quarry_mask * (3.0 - 2.0 * quarry_mask) * valid.astype(np.float64)

    # 5 crisp stepped mining benches (step_h = 2.4 m)
    step_h = 2.40
    tau = terrain / step_h
    frac = tau - np.floor(tau)
    smooth_frac = np.clip((frac - 0.24) / (0.76 - 0.24), 0.0, 1.0)
    smooth_frac = smooth_frac * smooth_frac * (3.0 - 2.0 * smooth_frac)
    stepped_quarry = (np.floor(tau) + smooth_frac) * step_h

    arena_guard = np.clip((arena_dist - 15.0) / 8.0, 0.0, 1.0)
    quarry_w = quarry_mask * arena_guard * 0.85
    terrain = np.where(valid, terrain * (1.0 - quarry_w) + stepped_quarry * quarry_w, -cliff_depth)

    # Gentle quarry haul bench smoothing on the central Kim Tinh Đài yard
    arena_flat_w = (1.0 - np.clip((arena_dist - 12.0) / 7.5, 0.0, 1.0)) * valid.astype(np.float64)
    arena_base_y = float(np.median(terrain[valid & (arena_dist < 15.0)]))
    terrain = np.where(valid, terrain * (1.0 - arena_flat_w * 0.72) + arena_base_y * (arena_flat_w * 0.72), -cliff_depth)

    # ── 2. Natural West-Flowing Rivers (Sông Dương Đông & Rạch Cửa Cạn) ──
    perimeter = distance_inside(valid, sx, sz)
    directions, accum, conditioned = compute_d8_flow(terrain, valid, sx, sz)
    threshold = max(14.0, float(valid.sum()) * 0.011)
    streams = valid & (accum >= threshold) & (perimeter > 4.5) & (arena_dist > 15.5)

    dd = [(0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1), (1, 0), (1, 1)]
    corridor = np.zeros_like(terrain)
    stream_surf_num = np.zeros_like(terrain)
    stream_surf_den = np.zeros_like(terrain)

    for r, c in zip(*np.where(streams)):
        d = int(directions[r, c])
        if d < 0:
            continue
        dr, dc = dd[d]
        nr, nc = r + dr, c + dc
        if not valid[nr, nc] or perimeter[nr, nc] <= 3.5:
            continue
        ax, az = c * sx, r * sz
        bx, bz = nc * sx, nr * sz
        seg_dx, seg_dz = bx - ax, bz - az

        half = float(np.clip(0.85 + 0.025 * np.sqrt(accum[r, c]), 0.90, 2.35))
        radius = half + max(sx, sz) * 2.35
        x0 = max(0, int((min(ax, bx) - radius * 1.4) / sx))
        x1 = min(w, int((max(ax, bx) + radius * 1.4) / sx) + 2)
        z0 = max(0, int((min(az, bz) - radius * 1.4) / sz))
        z1 = min(h, int((max(az, bz) + radius * 1.4) / sz) + 2)
        sl = np.s_[z0:z1, x0:x1]
        t = np.clip(((X[sl] - ax) * seg_dx + (Z[sl] - az) * seg_dz) / max(seg_dx ** 2 + seg_dz ** 2, 1e-9), 0.0, 1.0)
        dist = np.hypot(X[sl] - (ax + t * seg_dx), Z[sl] - (az + t * seg_dz))
        w_raw = 0.5 + 0.5 * np.cos(np.pi * np.clip(dist / radius, 0.0, 1.0))
        weight = w_raw * w_raw * (3.0 - 2.0 * w_raw)
        corridor[sl] = np.maximum(corridor[sl], weight)

        level = conditioned[r, c] * (1.0 - t) + conditioned[nr, nc] * t
        bed = np.minimum(terrain[sl], level - 1.05 * weight)
        terrain[sl] = terrain[sl] * (1.0 - weight) + bed * weight

        surf_r = half + max(sx, sz) * 0.65
        w_surf = np.clip((surf_r - dist) / max(surf_r, 1e-6), 0.0, 1.0)
        w_surf = w_surf * w_surf * (3.0 - 2.0 * w_surf)
        stream_surf_num[sl] += w_surf * (level - 0.14)
        stream_surf_den[sl] += w_surf

    has_stream_surf = stream_surf_den > 1e-4
    avg_stream_surf = np.where(has_stream_surf, stream_surf_num / np.maximum(stream_surf_den, 1e-4), terrain - 0.25)

    bed_smooth = normalized_smooth(terrain, valid, 2.0)
    smooth_mask = np.clip(corridor * 0.88, 0.0, 0.92)
    terrain = terrain * (1.0 - smooth_mask) + bed_smooth * smooth_mask
    terrain[~valid] = -cliff_depth

    water_plane = np.where(has_stream_surf, avg_stream_surf, terrain - 0.25)
    rim_taper = np.clip((perimeter - 2.5) / 4.5, 0.0, 1.0)
    water_plane = np.where(valid, water_plane * rim_taper + (terrain - 0.35) * (1.0 - rim_taper), terrain - 0.35)

    raw_water_cand = valid & ((water_plane - terrain) > 0.006)
    visited = np.zeros_like(raw_water_cand, dtype=bool)
    kept_water = np.zeros_like(raw_water_cand, dtype=bool)
    for sr, sc in zip(*np.where(raw_water_cand)):
        if visited[sr, sc]:
            continue
        comp = []
        q = deque([(sr, sc)])
        visited[sr, sc] = True
        while q:
            cr, cc = q.popleft()
            comp.append((cr, cc))
            for dr in (-1, 0, 1):
                for dc in (-1, 0, 1):
                    if dr == 0 and dc == 0:
                        continue
                    nr, nc = cr + dr, cc + dc
                    if 0 <= nr < h and 0 <= nc < w and raw_water_cand[nr, nc] and not visited[nr, nc]:
                        visited[nr, nc] = True
                        q.append((nr, nc))
        if len(comp) >= 24:
            for cr, cc in comp:
                kept_water[cr, cc] = True

    water_plane = np.where(kept_water, water_plane, terrain - 0.35)
    raw_water = valid & ((water_plane - terrain) > 0.006)
    near_water = gaussian_filter_2d(raw_water.astype(float), 2.0) > 0.01
    smooth_ext = normalized_smooth(water_plane, raw_water, 1.6)
    water_plane = np.where(raw_water, water_plane, np.where(near_water, np.minimum(smooth_ext, terrain - 0.004), terrain - 0.30))

    # Pick primary river placer midpoint for metadata/zone
    w_ys, w_xs = np.where(raw_water)
    if w_xs.size > 0:
        mid_i = len(w_xs) // 2
        lake_cx, lake_cz = float(w_xs[mid_i] * sx), float(w_ys[mid_i] * sz)
    else:
        lake_cx, lake_cz = float(width * 0.32), float(depth * 0.56)
    lake_rx, lake_rz = 18.0, 18.0
    tarn_cx, tarn_cz = quarry_cx, quarry_cz

    height_code = np.rint(np.clip((terrain + cliff_depth) / total_range, 0.0, 1.0) * 65535.0).astype(np.uint16)
    ground = height_code.astype(np.float64) / 65535.0 * total_range - cliff_depth
    encoded = np.rint(height_code.astype(np.float64) / 257.0).astype(np.uint8)

    water_code = np.rint(np.clip((water_plane + cliff_depth) / total_range, 0.0, 1.0) * 65535.0).astype(np.uint16)
    water_depth = np.where(valid & ((water_plane - ground) > 0.004), np.maximum(0.0, water_plane - ground), 0.0)
    wet_water = water_depth > 0.004

    # ── 3. Metalliferous Splat, Ore Vein & Debris Baking ──
    gy, gx = np.gradient(ground, sz, sx)
    slope_deg = np.degrees(np.arctan(np.hypot(gx, gy)))
    slope_s = gaussian_filter_2d(slope_deg, 1.0)
    tpi_m = ground - normalized_smooth(ground, valid, 5.0)

    cliff = np.clip(perimeter / 1.0, 0.0, 1.0)
    cliff = gaussian_filter_2d(cliff * cliff * (3.0 - 2.0 * cliff), 1.4)

    rock_slope = np.clip((slope_s - 10.5) / 11.5, 0.0, 1.0)
    rock_ridge = np.clip((tpi_m - 1.4) / 7.2, 0.0, 1.0) * np.clip((ground - 2.0) / 5.5, 0.0, 1.0)
    rock = np.maximum(np.maximum(rock_slope, rock_ridge), 1.0 - cliff)
    rock = np.maximum(rock, quarry_mask * 0.45)
    rock[wet_water] = 0.0

    # 2x Super-resolved maps
    h2, w2 = h * 2 - 1, w * 2 - 1
    sx2, sz2 = width / (w2 - 1), depth / (h2 - 1)
    up2 = lambda a: resample_grid(a.astype(np.float64), h2, w2)
    valid2 = up2(valid.astype(float)) > 0.5
    ground2 = gaussian_filter_2d(up2(ground), 0.85)
    water_plane2 = gaussian_filter_2d(up2(water_plane), 1.15)
    elev_diff2 = ground2 - water_plane2
    wet2 = valid2 & (elev_diff2 < -0.002)
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
    quarry2 = up2(quarry_mask)

    coverage2 = np.clip((1.0 - rock2 * 0.90) * (1.0 - wet_smooth2) * (1.0 - quarry2 * 0.75) * (0.34 + 0.48 * np.clip(1.0 - slope2 / 18.0, 0.0, 1.0)), 0.0, 1.0)
    coverage2[~valid2] = 0.0
    soil2 = np.clip((1.0 - coverage2) * (1.0 - rock2) * (1.0 - wet_smooth2) * 1.15, 0.0, 1.0)
    wet_placer2 = np.clip(np.maximum(shore2 * 0.84, wet_smooth2), 0.0, 1.0)
    habitat_val2 = np.clip(0.50 + 0.35 * np.clip((tpi2 + 4.0) / 12.0, 0.0, 1.0), 0.0, 1.0)

    # Hydrothermal Gold Veins (R) & Silver Lodes (A) + Tailings Scree (G)
    X2, Z2 = np.meshgrid(np.arange(w2) * sx2, np.arange(h2) * sz2)
    wx2 = X2 * 0.38 + np.sin(Z2 * 0.24 + X2 * 0.14) * 2.4
    wz2 = Z2 * 0.36 + np.cos(X2 * 0.22 - Z2 * 0.17) * 2.4
    vein_pat_g = 1.0 - np.clip(np.abs(np.sin(wx2 * 0.65 + wz2 * 0.32)) * 4.2, 0.0, 1.0)
    vein_pat_s = 1.0 - np.clip(np.abs(np.cos(wx2 * 0.42 - wz2 * 0.78)) * 4.2, 0.0, 1.0)

    bench_zone2 = np.maximum(quarry2 * 0.95, np.clip((ground2 - 2.5) / 4.5, 0.0, 1.0) * np.clip(slope2 / 8.0, 0.0, 1.0))
    placer_bar2 = shore2 * (1.0 - wet_smooth2 * 0.45)
    gold_vein2 = np.clip(vein_pat_g * np.maximum(bench_zone2 * 0.85, placer_bar2 * 0.92), 0.0, 1.0)
    silver_lode2 = np.clip(vein_pat_s * np.clip((ground2 - 3.5) / 6.5, 0.0, 1.0) * (0.35 + 0.65 * rock2), 0.0, 1.0) * (1.0 - wet_smooth2)
    scree_gravel2 = np.clip((slope2 - 7.5) / 12.0, 0.0, 1.0) * (0.42 + 0.58 * rock2)

    splat2 = np.stack((coverage2, rock2, np.clip(coverage2 * 0.24, 0.0, 1.0), shore2), axis=-1)
    splat2[~valid2] = (0, 1, 0, 0)
    habitat2 = np.stack((coverage2, coverage2, shore2, habitat_val2), axis=-1)
    habitat2[~valid2] = 0.0
    ground_material2 = np.stack((coverage2, soil2, wet_placer2, rock2), axis=-1)
    ground_material2[~valid2] = (0, 0, 0, 1)

    out_assets_dir = os.path.join(ROOT_DIR, "assets/maps/phu_quoc")
    out_world_dir = os.path.join(ROOT_DIR, "maps/worlds/phu_quoc")
    os.makedirs(out_assets_dir, exist_ok=True)
    os.makedirs(out_world_dir, exist_ok=True)
    p = lambda s: os.path.join(out_assets_dir, "phu_quoc_" + s)

    write_png(p("island_heightmap.png"), encoded)
    height_rgba = np.stack((height_code >> 8, height_code & 255, np.zeros_like(height_code), np.full_like(height_code, 255)), axis=-1)
    write_png(p("heightfield.png"), height_rgba, 6)

    chisel_ow = np.sin(X2 * 0.28 + Z2 * 0.22 + np.sin(Z2 * 0.10) * 1.4) * 0.08
    ow_flat = np.clip(1.0 - slope2 / 16.0, 0.0, 1.0) * (1.0 - wet_smooth2) * valid2.astype(np.float64)
    terrain_ow = np.where(valid2, ground2 + chisel_ow * ow_flat, -cliff_depth)
    height_code_ow = np.rint(np.clip((terrain_ow + cliff_depth) / total_range, 0.0, 1.0) * 65535.0).astype(np.uint16)
    height_rgba_ow = np.stack((height_code_ow >> 8, height_code_ow & 255, np.zeros_like(height_code_ow), np.full_like(height_code_ow, 255)), axis=-1)
    write_png(p("heightfield_ow.png"), height_rgba_ow, 6)

    write_png(p("ecology_splat.png"), np.rint(splat2 * 255), 6)
    write_png(p("ground_material.png"), np.rint(ground_material2 * 255), 6)
    write_png(p("habitat.png"), np.rint(habitat2 * 255), 6)

    riparian_b = np.rint(np.clip(gaussian_filter_2d(wet_water.astype(float), 2.8) * 3.5, 0.0, 1.0) * 255).astype(np.uint8)
    water_rgba = np.stack((water_code >> 8, water_code & 255, riparian_b, wet_water * 255), axis=-1).astype(np.uint8)
    write_png(p("water_height.png"), water_rgba, 6)

    shore_code2 = np.rint((np.clip(signed_shore2 / 16.0, -1.0, 1.0) * 0.5 + 0.5) * 65535).astype(np.uint16)
    distance2 = np.stack((np.full_like(shore_code2, 255), np.full_like(shore_code2, 255), shore_code2 >> 8, shore_code2 & 255), axis=-1)
    write_png(p("habitat_distance.png"), distance2, 6)

    triangles = write_water_mesh(p("water.obj"), ground, water_plane, sx, sz)

    # ── 4. Quarry Landmarks, Slate/Granite Boulders (NO Obsidian), & 3D Ore Clusters ──
    rng = np.random.default_rng(20261124)
    placements = []
    ore_placements = []
    occupied = []

    def sample_cell(x, z):
        r = int(np.clip(z / sz, 0, h - 1))
        c = int(np.clip(x / sx, 0, w - 1))
        return float(ground[r, c]), float(slope_s[r, c]), float(tpi_m[r, c]), bool(valid[r, c]), bool(wet_water[r, c])

    def can_place(x, z, r_scale, min_sep=0.62):
        if x < 16.0 or x > width - 16.0 or z < 16.0 or z > depth - 16.0:
            return False
        if np.hypot(x - arena_x, z - arena_z) < 16.0 + r_scale:
            return False
        for ox, oz, orad in occupied:
            if np.hypot(x - ox, z - oz) < (r_scale + orad) * min_sep:
                return False
        return True

    def add_rock(x, z, r_scale, h_scale, rot, arch, allow_stack=False):
        gy, sl, _, val, is_wet = sample_cell(x, z)
        if not val or is_wet or gy < -0.10 or sl > 36.0:
            return False
        if not allow_stack and not can_place(x, z, r_scale):
            return False
        placements.append((x, gy, z, r_scale, h_scale, rot % 360.0, arch))
        if not allow_stack:
            occupied.append((x, z, r_scale))
        return True

    def add_ore(x, z, r_scale, h_scale, rot, ore_type):
        gy, sl, _, val, is_wet = sample_cell(x, z)
        if not val or is_wet or gy < 0.05 or sl > 36.0:
            return False
        if not can_place(x, z, r_scale, min_sep=0.55):
            return False
        ore_placements.append((x, gy, z, r_scale, h_scale, rot % 360.0, ore_type))
        occupied.append((x, z, r_scale))
        return True

    # 4A. Cổng Hầm Mỏ Kim Quang (Mine Adit Portal anchored into Quarry Wall)
    portal_mask = valid & (~wet_water) & (quarry_mask > 0.35) & (ground > 3.0) & (ground < 11.0)
    pr_r, pr_c = np.where(portal_mask)
    portal_x, portal_z = quarry_cx, quarry_cz
    if len(pr_r) > 0:
        pick = len(pr_r) // 2
        portal_x, portal_z = float(pr_c[pick] * sx), float(pr_r[pick] * sz)
        gy_p, _, _, _, _ = sample_cell(portal_x, portal_z)
        placements.append((portal_x - 3.0, gy_p, portal_z - 1.1, 1.80, 3.60, 20.0, "MAP_ROCK_SLATE"))
        placements.append((portal_x + 3.0, gy_p, portal_z + 1.1, 1.80, 3.60, 200.0, "MAP_ROCK_SLATE"))
        placements.append((portal_x - 1.2, gy_p + 3.1, portal_z - 0.4, 2.30, 1.10, 18.0, "MAP_ROCK_GRANITE"))
        placements.append((portal_x + 1.2, gy_p + 3.1, portal_z + 0.4, 2.30, 1.10, 198.0, "MAP_ROCK_GRANITE"))
        occupied.append((portal_x, portal_z, 5.5))
        ore_placements.append((portal_x - 4.6, gy_p, portal_z - 1.6, 1.35, 1.65, 35.0, "MAP_ORE_GOLD_VEIN"))
        ore_placements.append((portal_x + 4.6, gy_p, portal_z + 1.6, 1.35, 1.65, 215.0, "MAP_ORE_SILVER_LODE"))

    # 4B. 3D Mineral Ore Crystal Clusters along Quarry Benches & River Placer Bars
    ore_candidates = np.where(valid & (~wet_water) & ((quarry_mask > 0.15) | (corridor > 0.15) | (ground > 2.5)) & (slope_s < 32.0))
    if len(ore_candidates[0]) > 0:
        o_order = rng.permutation(len(ore_candidates[0]))
        for idx in o_order:
            if len(ore_placements) >= 104:
                break
            or_r, or_c = int(ore_candidates[0][idx]), int(ore_candidates[1][idx])
            ox, oz = float(or_c * sx), float(or_r * sz)
            gy_o, _, _, _, _ = sample_cell(ox, oz)
            if ox > width * 0.56 and gy_o > 8.0:
                ore_type = "MAP_ORE_SILVER_LODE" if rng.random() < 0.70 else "MAP_ORE_CELESTIAL_METEORITE"
            elif corridor[or_r, or_c] > 0.15 or gy_o < 4.5:
                ore_type = "MAP_ORE_GOLD_VEIN" if rng.random() < 0.78 else "MAP_ORE_COPPER_PYRITE"
            else:
                roll = rng.random()
                if roll < 0.44:
                    ore_type = "MAP_ORE_GOLD_VEIN"
                elif roll < 0.74:
                    ore_type = "MAP_ORE_SILVER_LODE"
                elif roll < 0.90:
                    ore_type = "MAP_ORE_COPPER_PYRITE"
                else:
                    ore_type = "MAP_ORE_CELESTIAL_METEORITE"
            ro = float(rng.uniform(0.90, 1.65))
            ho = ro * float(rng.uniform(1.05, 1.65))
            add_ore(ox, oz, ro, ho, float(rng.uniform(0, 360)), ore_type)

    # 4C. Quarry Bench Crags & Granite/Slate Outcrops (NO circular monolith ring, NO obsidian)
    rim_mask = valid & (~wet_water) & (ground > 2.5) & (slope_s > 9.5) & (slope_s < 34.0)
    r_rows, r_cols = np.where(rim_mask)
    if len(r_rows) > 0:
        order = rng.permutation(len(r_rows))
        crag_clusters = 0
        for idx in order:
            if crag_clusters >= 24:
                break
            bx, bz = float(r_cols[idx] * sx), float(r_rows[idx] * sz)
            rb = float(rng.uniform(1.60, 2.75))
            if not can_place(bx, bz, rb, min_sep=1.35):
                continue
            hb = rb * float(rng.uniform(1.10, 1.65))
            if add_rock(bx, bz, rb, hb, float(rng.uniform(0, 360)), "MAP_ROCK_SLATE"):
                crag_clusters += 1
                for k in range(int(rng.integers(3, 5))):
                    ang = k * (2.0 * np.pi / 4.0) + float(rng.uniform(-0.25, 0.25))
                    dist = rb * float(rng.uniform(0.55, 0.95))
                    cr = rb * float(rng.uniform(0.42, 0.68))
                    add_rock(bx + np.cos(ang) * dist, bz + np.sin(ang) * dist, cr, cr * float(rng.uniform(1.05, 1.55)),
                             float(rng.uniform(0, 360)), "MAP_ROCK_GRANITE", allow_stack=True)

    # 4D. Scattered Granite/Slate Boulders & Scree Tailings
    for _ in range(1200):
        if len(placements) >= 320:
            break
        x = float(rng.uniform(18.0, width - 18.0))
        z = float(rng.uniform(18.0, depth - 18.0))
        gy, sl, _, val, is_wet = sample_cell(x, z)
        if not val or is_wet or gy < 0.20 or sl > 32.0:
            continue
        r_s = float(rng.uniform(0.68, 1.60))
        arch = "MAP_ROCK_SLATE" if rng.random() < 0.52 else "MAP_ROCK_GRANITE"
        h_s = r_s * float(rng.uniform(0.65, 1.12))
        if add_rock(x, z, r_s, h_s, float(rng.uniform(0, 360)), arch):
            for _ in range(int(rng.integers(1, 3))):
                a = rng.uniform(0.0, 2.0 * np.pi)
                d = r_s + float(rng.uniform(0.35, 1.25))
                sr = float(rng.uniform(0.22, 0.48))
                add_rock(x + np.cos(a) * d, z + np.sin(a) * d, sr, sr * 0.48, float(rng.uniform(0, 360)), "MAP_ROCK_SCREE")

    # Bake rock + ore contact AO & crushed quartz tailings into debris_map.png
    rock_apron2 = np.zeros_like(rock2)
    rock_contact2 = np.zeros_like(rock2)
    all_outcrops = [(rx, ry, rz, rr) for rx, ry, rz, rr, _, _, rarch in placements if rarch != "MAP_ROCK_SCREE"]
    all_outcrops += [(ox, oy, oz, orr) for ox, oy, oz, orr, _, _, _ in ore_placements]
    for rx, ry, rz, rr in all_outcrops:
        reach = rr + 3.4
        c0 = max(0, int((rx - reach) / sx2))
        c1 = min(w2, int((rx + reach) / sx2) + 1)
        r0 = max(0, int((rz - reach) / sz2))
        r1 = min(h2, int((rz + reach) / sz2) + 1)
        if c1 <= c0 or r1 <= r0:
            continue
        xs = np.arange(c0, c1, dtype=np.float64) * sx2 - rx
        zs = np.arange(r0, r1, dtype=np.float64) * sz2 - rz
        dx_g, dz_g = np.meshgrid(xs, zs)
        dist_edge = np.hypot(dx_g, dz_g) - rr * 0.85
        apron = np.exp(-np.maximum(dist_edge, 0.0) / 1.8)
        contact = np.exp(-np.maximum(dist_edge + 0.12, 0.0) / 0.45) * 0.78
        rock_apron2[r0:r1, c0:c1] = np.maximum(rock_apron2[r0:r1, c0:c1], apron)
        rock_contact2[r0:r1, c0:c1] = np.maximum(rock_contact2[r0:r1, c0:c1], contact)

    grus2 = np.clip(np.maximum(scree_gravel2, rock_apron2 * 0.82), 0.0, 1.0)
    debris_map2 = np.stack((gold_vein2, grus2, rock_contact2, silver_lode2), axis=-1)
    debris_map2[~valid2] = 0.0
    write_png(p("debris_map.png"), np.rint(debris_map2 * 255), 6)

    tint_r = 0.56 + 0.14 * gold_vein2 - 0.10 * rock2
    tint_g = 0.55 + 0.10 * gold_vein2 - 0.08 * rock2
    tint_b = 0.58 - 0.05 * gold_vein2 + 0.08 * silver_lode2
    tint_map2 = np.stack((np.clip(tint_r, 0.0, 1.0), np.clip(tint_g, 0.0, 1.0), np.clip(tint_b, 0.0, 1.0)), axis=-1)
    tint_map2[~valid2] = (0.34, 0.36, 0.40)
    write_png(p("ground_tint.png"), np.rint(tint_map2 * 255), 2)

    rel_sz = 256
    ry, rx_i = np.mgrid[0:rel_sz, 0:rel_sz].astype(np.float64) / rel_sz
    quartz_h = 0.5 + 0.20 * np.sin(rx_i * 2.0 * np.pi * 7.0 + np.sin(ry * 2.0 * np.pi * 3.0)) + 0.12 * np.cos(ry * 2.0 * np.pi * 11.0)
    slate_h = 0.48 + 0.24 * np.sin(rx_i * 2.0 * np.pi * 15.0) * np.cos(ry * 2.0 * np.pi * 15.0)
    rel_rgba = np.stack((
        np.clip(quartz_h, 0.0, 1.0),
        np.clip(slate_h, 0.0, 1.0),
        np.clip(0.35 + 0.35 * np.sin((rx_i + ry) * 2.0 * np.pi * 5.0), 0.0, 1.0),
        np.clip(0.35 + 0.35 * np.cos((rx_i - ry) * 2.0 * np.pi * 6.0), 0.0, 1.0),
    ), axis=-1)
    write_png(p("relief.png"), np.rint(rel_rgba * 255), 6)

    meta_h = os.path.join(out_assets_dir, "phu_quoc_generated_meta.h")
    with open(meta_h, "w") as f:
        f.write("// Generated by maps/pipeline/bake_phu_quoc.py\n")
        f.write("#ifndef PHU_QUOC_GENERATED_META_H\n#define PHU_QUOC_GENERATED_META_H\n")
        f.write(f"#define PHU_QUOC_MAP_WIDTH {width:.6f}f\n")
        f.write(f"#define PHU_QUOC_MAP_DEPTH {depth:.6f}f\n")
        f.write(f"#define PHU_QUOC_HEIGHT_RANGE {height_range:.6f}f\n")
        f.write(f"#define PHU_QUOC_CLIFF_DEPTH {cliff_depth:.6f}f\n")
        f.write(f"#define PHU_QUOC_ARENA_X {arena_x:.6f}f\n")
        f.write(f"#define PHU_QUOC_ARENA_Z {arena_z:.6f}f\n")
        f.write(f"#define PHU_QUOC_LAKE_X {lake_cx:.6f}f\n")
        f.write(f"#define PHU_QUOC_LAKE_Z {lake_cz:.6f}f\n")
        f.write(f"#define PHU_QUOC_LAKE_RX {lake_rx:.6f}f\n")
        f.write(f"#define PHU_QUOC_LAKE_RZ {lake_rz:.6f}f\n")
        f.write(f"#define PHU_QUOC_TARN_X {tarn_cx:.6f}f\n")
        f.write(f"#define PHU_QUOC_TARN_Z {tarn_cz:.6f}f\n")
        f.write(f"#define PHU_QUOC_PORTAL_X {portal_x:.6f}f\n")
        f.write(f"#define PHU_QUOC_PORTAL_Z {portal_z:.6f}f\n")
        f.write("#endif // PHU_QUOC_GENERATED_META_H\n")

    # ── 5. 3D Native Gold/Silver Nuggets & Metallic Tailings ──
    debris_items = []
    for ox, oy, oz, orr, oh, orot, otype in ore_placements:
        for _ in range(6):
            if len(debris_items) >= 1550:
                break
            a = float(rng.uniform(0.0, 2.0 * np.pi))
            d = orr * float(rng.uniform(0.80, 2.25))
            dx_p = ox + np.cos(a) * d
            dz_p = oz + np.sin(a) * d
            gy_p, sl_p, _, val_p, is_wet_p = sample_cell(dx_p, dz_p)
            if not val_p or is_wet_p or gy_p < 0.10 or sl_p > 34.0:
                continue
            sc = float(rng.uniform(1.6, 3.8))
            rot_d = float(rng.uniform(0.0, 360.0))
            if otype == "MAP_ORE_GOLD_VEIN":
                col = (242, 192, 68)
            elif otype == "MAP_ORE_SILVER_LODE":
                col = (214, 224, 236)
            elif otype == "MAP_ORE_COPPER_PYRITE":
                col = (196, 152, 74)
            else:
                col = (94, 102, 118)
            debris_items.append((dx_p, gy_p, dz_p, rot_d, sc, col, "MAP_DEBRIS_PEBBLE"))

    for _ in range(1400):
        if len(debris_items) >= 1550:
            break
        dx_p = float(rng.uniform(18.0, width - 18.0))
        dz_p = float(rng.uniform(18.0, depth - 18.0))
        gy_p, sl_p, _, val_p, is_wet_p = sample_cell(dx_p, dz_p)
        if not val_p or is_wet_p or gy_p < 0.15 or sl_p > 26.0:
            continue
        sc = float(rng.uniform(1.2, 2.8))
        rot_d = float(rng.uniform(0.0, 360.0))
        roll = rng.random()
        col = (232, 184, 68) if roll < 0.25 else ((198, 208, 220) if roll < 0.50 else (84, 90, 100))
        debris_items.append((dx_p, gy_p, dz_p, rot_d, sc, col, "MAP_DEBRIS_PEBBLE"))

    rock_header = os.path.join(out_world_dir, "phu_quoc_rocks_generated.h")
    with open(rock_header, "w") as f:
        f.write("// Generated by maps/pipeline/bake_phu_quoc.py\n")
        f.write("// Phú Quốc Mineral Island: Open-Pit Quarry, Mine Adit Portal, 3D Ore Clusters & Nuggets\n")
        f.write("#ifndef PHU_QUOC_ROCKS_GENERATED_H\n#define PHU_QUOC_ROCKS_GENERATED_H\n\n")
        f.write('#include "maps/toolkit/map_props.h"\n\n')
        f.write("static const MapRockItem PHU_QUOC_ROCK_ITEMS[] = {\n")
        for x, y, z, r_s, h_s, rot, arch in placements:
            f.write(f"    {{{{{x:.2f}f, {y:.2f}f, {z:.2f}f}}, {r_s:.2f}f, {h_s:.2f}f, {rot:.1f}f, {arch}}},\n")
        f.write("};\n\n")
        f.write(f"#define PHU_QUOC_ROCK_COUNT {len(placements)}\n\n")
        f.write("static const MapOrePlacement PHU_QUOC_ORE_ITEMS[] = {\n")
        for x, y, z, r_s, h_s, rot, ore_t in ore_placements:
            f.write(f"    {{{{{x:.2f}f, {y:.2f}f, {z:.2f}f}}, {r_s:.2f}f, {h_s:.2f}f, {rot:.1f}f, {ore_t}}},\n")
        f.write("};\n\n")
        f.write(f"#define PHU_QUOC_ORE_COUNT {len(ore_placements)}\n\n")
        f.write("static const MapDebrisPlacement PHU_QUOC_DEBRIS_ITEMS[] = {\n")
        for dx_p, dy_p, dz_p, drot, dsc, (cr, cg, cb), dtype in debris_items:
            f.write(f"    {{{{{dx_p:.2f}f, {dy_p:.2f}f, {dz_p:.2f}f}}, {drot:.1f}f, {dsc:.2f}f, {{{cr}, {cg}, {cb}, 255}}, {dtype}}},\n")
        f.write("};\n\n")
        f.write(f"#define PHU_QUOC_DEBRIS_COUNT {len(debris_items)}\n\n")
        f.write("#endif // PHU_QUOC_ROCKS_GENERATED_H\n")

    print(
        f"[PhuQuoc] Baked {w}x{h} real-DEM quarry terrain, {triangles} water tris, "
        f"{len(placements)} rocks, {len(ore_placements)} 3D ore clusters, {len(debris_items)} nuggets -> {rock_header}",
        flush=True,
    )


if __name__ == "__main__":
    bake_phu_quoc()
