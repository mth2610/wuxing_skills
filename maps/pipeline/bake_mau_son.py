#!/usr/bin/env python3
"""
Bake Mẫu Sơn (Water / Hệ Thủy — Hàn Đàm Tuyết Lĩnh) snowy mountain peak &
freezing glacial lake assets from projected UTM 48N DEM
(assets/maps/mau_son/DEM_MauSon_UTM48N.tif):
  1. Preserves endoreic alpine cirque & saddle glacial tarns (Thiên Trì Hàn Đàm
     & Băng Vân Hồ) with C^2 Gaussian-smoothed bathymetry (LANDMINES.md §248).
  2. Carves D8 cascading freezing mountain streams connecting the upper cirque
     tarn down into the central saddle lake and mountain spillways.
  3. Exports 16-bit heightfields (MOBA 1x & Open World 2x with sastrugi snow
     drifts), conformal water/ice mesh (mau_son_water.obj), ice-shelf flowmap
     (mau_son_ice_flow.png), snow/permafrost splat/debris/relief maps, and LUT.
  4. Places Cổng Băng Tuyết (NW frost-covered stone/ice arch), Hàn Băng Thạch
     Trận (monolithic slate & glacial ice pillars), frost-rimed slate crags
     (MAP_ROCK_CRAG / MAP_ROCK_SLATE), granite boulders, talus scree, and
     freezing lake-mist vents into mau_son_rocks_generated.h.
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
    largest_component,
    normalized_smooth,
    priority_flood,
    resample_grid,
    write_png,
    write_water_mesh,
)
from georaster import read_dem


def bake_mau_son():
    dem_path = os.path.join(ROOT_DIR, "assets/maps/mau_son/DEM_MauSon_UTM48N.tif")
    if not os.path.exists(dem_path):
        from fetch_mau_son_dem import synthesize_mau_son_dem, write_geotiff_utm48n
        dem_syn = synthesize_mau_son_dem()
        write_geotiff_utm48n(dem_path, dem_syn, origin_x=700000.0, origin_y=2418500.0, pixel_size_m=15.0)

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
    w = max(8, round(width) + 1)
    h = max(8, round(depth) + 1)
    if w * h > 65535:
        raise ValueError("Runtime heightfield supports at most 65535 nodes")
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
    terrain = height_range * np.power(normalized, 1.06)

    X, Z = np.meshgrid(np.arange(w) * sx, np.arange(h) * sz)

    # Central high-altitude saddle combat arena (walkable snow plateau beside Thiên Trì Hàn Đàm)
    arena_x, arena_z = float(width * 0.45), float(depth * 0.50)
    arena_dist = np.hypot(X - arena_x, Z - arena_z)

    # Subtle wind-sculpted snowdrift lobes outside the central arena
    drift_wave = (
        np.sin(X * 0.048 + np.cos(Z * 0.035) * 1.4) * 0.36
        + np.cos(Z * 0.056 - X * 0.038) * 0.24
    )
    arena_guard = np.clip((arena_dist - 14.5) / 10.0, 0.0, 1.0)
    terrain = np.where(valid, terrain + drift_wave * arena_guard * np.clip((terrain - 1.5) / 8.0, 0.0, 1.0), -cliff_depth)

    # ── 1. Glacial Cirque Tarn & Saddle Lake Detection ──
    perimeter = distance_inside(valid, sx, sz)
    filled_dem = priority_flood(terrain, valid)
    sink_depth = np.where(valid, np.maximum(0.0, filled_dem - terrain), 0.0)

    tarn_raw = valid & (sink_depth > 0.28) & (perimeter > 8.0) & (terrain > 1.8)
    tarn_blob = gaussian_filter_2d(tarn_raw.astype(np.float64), 2.3)
    tarn_zone = valid & (tarn_blob > 0.18) & (perimeter > 6.5)

    # Primary central saddle lake (Thiên Trì Hàn Đàm) & upper eastern cirque tarn (Băng Vân Hồ)
    saddle_lake = largest_component(tarn_zone & (X <= width * 0.62))
    if saddle_lake.sum() < 16:
        saddle_lake = valid & (np.hypot((X - (arena_x + 22.0)) * 0.90, (Z - (arena_z + 10.0)) * 1.15) < 16.0)
    upper_tarn = largest_component(tarn_zone & (X > width * 0.62))
    if upper_tarn.sum() < 12:
        upper_tarn = valid & (np.hypot(X - width * 0.72, Z - depth * 0.44) < 12.0)

    # Ensure central 14m combat arena stays dry and walkable
    saddle_lake = saddle_lake & (arena_dist > 14.0)
    upper_tarn = upper_tarn & (arena_dist > 14.0)
    lakes_all = saddle_lake | upper_tarn
    lakes_smooth = gaussian_filter_2d(lakes_all.astype(np.float64), 2.2)

    s_ys, s_xs = np.where(saddle_lake)
    lake_cx = float(s_xs.mean() * sx) if s_xs.size else float(arena_x + 22.0)
    lake_cz = float(s_ys.mean() * sz) if s_ys.size else float(arena_z + 10.0)
    lake_rx = float(max(10.0, (s_xs.max() - s_xs.min() + 1) * sx * 0.5)) if s_xs.size else 16.0
    lake_rz = float(max(10.0, (s_ys.max() - s_ys.min() + 1) * sz * 0.5)) if s_ys.size else 14.0

    u_ys, u_xs = np.where(upper_tarn)
    tarn_cx = float(u_xs.mean() * sx) if u_xs.size else float(width * 0.72)
    tarn_cz = float(u_ys.mean() * sz) if u_ys.size else float(depth * 0.44)

    water_plane = np.full_like(terrain, -cliff_depth - 1.0)
    if saddle_lake.any():
        # Smooth C2 basin carving + wide hillside shoulder grading (eliminates hard cliff ring!)
        rim_ring = (gaussian_filter_2d(saddle_lake.astype(np.float64), 2.5) > 0.05) & (~saddle_lake) & valid
        rim_low = float(np.percentile(terrain[rim_ring], 18)) if rim_ring.any() else float(np.percentile(terrain[saddle_lake], 25))
        s_water_y = max(1.8, rim_low - 0.16)
        s_w = gaussian_filter_2d(saddle_lake.astype(np.float64), 2.6)
        s_shoulder = gaussian_filter_2d(saddle_lake.astype(np.float64), 5.8)
        s_blend = np.clip(s_shoulder / 0.55, 0.0, 1.0)
        s_blend = s_blend * s_blend * (3.0 - 2.0 * s_blend)
        s_bed = s_water_y - 1.75 * np.clip(s_w * 1.35, 0.0, 1.0)
        # Gently grade uphill banks down toward lake rim so max shore slope stays <= 26 deg
        s_target = np.minimum(terrain, s_water_y + 0.45)
        terrain = np.where(valid, terrain * (1.0 - s_blend) + s_target * s_blend, terrain)
        s_in = np.clip(s_w / 0.42, 0.0, 1.0)
        s_in = s_in * s_in * (3.0 - 2.0 * s_in)
        terrain = np.where(valid, terrain * (1.0 - s_in) + np.minimum(terrain, s_bed) * s_in, terrain)
        water_plane = np.where(saddle_lake, np.maximum(water_plane, s_water_y), water_plane)

    if upper_tarn.any():
        rim_ring_u = (gaussian_filter_2d(upper_tarn.astype(np.float64), 2.5) > 0.05) & (~upper_tarn) & valid
        rim_low_u = float(np.percentile(terrain[rim_ring_u], 18)) if rim_ring_u.any() else float(np.percentile(terrain[upper_tarn], 25))
        u_water_y = max(2.8, rim_low_u - 0.16)
        u_w = gaussian_filter_2d(upper_tarn.astype(np.float64), 2.6)
        u_shoulder = gaussian_filter_2d(upper_tarn.astype(np.float64), 5.2)
        u_blend = np.clip(u_shoulder / 0.55, 0.0, 1.0)
        u_blend = u_blend * u_blend * (3.0 - 2.0 * u_blend)
        u_bed = u_water_y - 1.55 * np.clip(u_w * 1.35, 0.0, 1.0)
        u_target = np.minimum(terrain, u_water_y + 0.45)
        terrain = np.where(valid, terrain * (1.0 - u_blend) + u_target * u_blend, terrain)
        u_in = np.clip(u_w / 0.42, 0.0, 1.0)
        u_in = u_in * u_in * (3.0 - 2.0 * u_in)
        terrain = np.where(valid, terrain * (1.0 - u_in) + np.minimum(terrain, u_bed) * u_in, terrain)
        water_plane = np.where(upper_tarn, np.maximum(water_plane, u_water_y), water_plane)

    # ── 2. D8 Cascading Mountain Streams & Glacial Ice Flowmap ──
    directions, accum, conditioned = compute_d8_flow(terrain, valid, sx, sz)
    threshold = max(14.0, float(valid.sum()) * 0.011)
    streams = valid & (accum >= threshold) & (perimeter > 4.5) & (~lakes_all) & (arena_dist > 14.5)

    dd = [(0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1), (1, 0), (1, 1)]
    corridor = np.zeros_like(terrain)
    stream_surf_num = np.zeros_like(terrain)
    stream_surf_den = np.zeros_like(terrain)
    flow_vx = np.zeros_like(terrain)
    flow_vz = np.zeros_like(terrain)
    flow_speed_acc = np.zeros_like(terrain)

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
        seg_len = max(np.hypot(seg_dx, seg_dz), 1e-6)
        dir_x, dir_z = seg_dx / seg_len, seg_dz / seg_len

        half = float(np.clip(0.85 + 0.022 * np.sqrt(accum[r, c]), 0.90, 2.20))
        radius = half + max(sx, sz) * 2.25
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
        flow_vx[sl] += w_surf * dir_x
        flow_vz[sl] += w_surf * dir_z
        flow_speed_acc[sl] = np.maximum(flow_speed_acc[sl], w_surf * np.clip(0.35 + 0.65 * (1.0 - dist / max(surf_r, 1e-6)), 0.0, 1.0))

    has_stream_surf = stream_surf_den > 1e-4
    avg_stream_surf = np.where(has_stream_surf, stream_surf_num / np.maximum(stream_surf_den, 1e-4), terrain - 0.25)

    bed_smooth = normalized_smooth(terrain, valid, 2.8)
    smooth_mask = np.clip(corridor * 0.90 + lakes_smooth * 0.94, 0.0, 0.96)
    terrain = terrain * (1.0 - smooth_mask) + bed_smooth * smooth_mask
    terrain[~valid] = -cliff_depth

    stream_plane = np.where(has_stream_surf, avg_stream_surf, terrain - 0.25)
    water_plane = np.where(lakes_all, water_plane, stream_plane)
    # Smooth stream-to-lake elevation transition so cascades slope gently (<= 26 deg) instead of vertical cliffs
    water_active = lakes_all | has_stream_surf
    for _ in range(8):
        wp_smooth = normalized_smooth(water_plane, water_active, 2.8)
        water_plane = np.where(lakes_all, water_plane, np.where(has_stream_surf, water_plane * 0.30 + wp_smooth * 0.70, water_plane))
    # Soft C2 channel grading beneath smoothed stream cascade (zero hard boolean cliff edges!)
    stream_grade = gaussian_filter_2d((has_stream_surf & (~lakes_all)).astype(np.float64), 2.4)
    stream_grade = np.clip(stream_grade / 0.45, 0.0, 1.0)
    stream_grade = stream_grade * stream_grade * (3.0 - 2.0 * stream_grade)
    terrain = np.where(valid, terrain * (1.0 - stream_grade) + np.minimum(terrain, water_plane - 0.42) * stream_grade, terrain)
    # Final gentle shoreline bank relaxation around all water bodies
    bank_zone = gaussian_filter_2d(water_active.astype(np.float64), 3.2)
    bank_mask = np.clip(bank_zone * 1.4, 0.0, 0.85) * valid.astype(np.float64)
    for _ in range(2):
        t_sm = normalized_smooth(terrain, valid, 2.6)
        terrain = terrain * (1.0 - bank_mask) + t_sm * bank_mask
    terrain[~valid] = -cliff_depth
    rim_taper = np.clip((perimeter - 2.5) / 4.5, 0.0, 1.0)
    water_plane = np.where(valid, water_plane * rim_taper + (terrain - 0.35) * (1.0 - rim_taper), terrain - 0.35)

    # Keep only connected water bodies touching lakes or >= 26 cells
    raw_water_cand = valid & ((water_plane - terrain) > 0.006)
    visited = np.zeros_like(raw_water_cand, dtype=bool)
    kept_water = np.zeros_like(raw_water_cand, dtype=bool)
    for sr, sc in zip(*np.where(raw_water_cand)):
        if visited[sr, sc]:
            continue
        comp = []
        q = deque([(sr, sc)])
        visited[sr, sc] = True
        touches_lake = False
        while q:
            cr, cc = q.popleft()
            comp.append((cr, cc))
            if lakes_all[cr, cc]:
                touches_lake = True
            for dr in (-1, 0, 1):
                for dc in (-1, 0, 1):
                    if dr == 0 and dc == 0:
                        continue
                    nr, nc = cr + dr, cc + dc
                    if 0 <= nr < h and 0 <= nc < w and raw_water_cand[nr, nc] and not visited[nr, nc]:
                        visited[nr, nc] = True
                        q.append((nr, nc))
        if touches_lake or len(comp) >= 26:
            for cr, cc in comp:
                kept_water[cr, cc] = True

    water_plane = np.where(kept_water | (lakes_smooth > 0.05), water_plane, terrain - 0.35)

    raw_water = valid & ((water_plane - terrain) > 0.006)
    near_water = gaussian_filter_2d(raw_water.astype(float), 2.0) > 0.01
    smooth_ext = normalized_smooth(water_plane, raw_water | (lakes_smooth > 0.05), 1.6)
    water_plane = np.where(raw_water, water_plane, np.where(near_water, np.minimum(smooth_ext, terrain - 0.004), terrain - 0.30))

    # Ice flowmap: RG = D8 unit flow dir, B = freeze/ice-shelf potential, A = lake mask
    f_len = np.hypot(flow_vx, flow_vz)
    fx_u = np.where(f_len > 1e-4, flow_vx / np.maximum(f_len, 1e-4), 0.0)
    fz_u = np.where(f_len > 1e-4, flow_vz / np.maximum(f_len, 1e-4), 0.0)
    fx_s = gaussian_filter_2d(fx_u, 1.4)
    fz_s = gaussian_filter_2d(fz_u, 1.4)
    fl_s = np.hypot(fx_s, fz_s)
    fx_n = np.where(fl_s > 1e-4, fx_s / np.maximum(fl_s, 1e-4), 0.65)
    fz_n = np.where(fl_s > 1e-4, fz_s / np.maximum(fl_s, 1e-4), 0.55)
    lake_mask_f = np.clip(lakes_smooth * 1.4, 0.0, 1.0)
    speed_norm = np.clip(gaussian_filter_2d(flow_speed_acc, 1.2), 0.0, 1.0)
    freeze_potential = np.clip(lake_mask_f * 0.85 + (1.0 - speed_norm) * 0.35, 0.0, 1.0)
    ice_flow_rgba = np.stack((
        np.clip(fx_n * 0.5 + 0.5, 0.0, 1.0),
        np.clip(fz_n * 0.5 + 0.5, 0.0, 1.0),
        freeze_potential,
        lake_mask_f,
    ), axis=-1)

    height_code = np.rint(np.clip((terrain + cliff_depth) / total_range, 0.0, 1.0) * 65535.0).astype(np.uint16)
    ground = height_code.astype(np.float64) / 65535.0 * total_range - cliff_depth
    encoded = np.rint(height_code.astype(np.float64) / 257.0).astype(np.uint8)

    water_code = np.rint(np.clip((water_plane + cliff_depth) / total_range, 0.0, 1.0) * 65535.0).astype(np.uint16)
    water_depth = np.where(valid & ((water_plane - ground) > 0.004), np.maximum(0.0, water_plane - ground), 0.0)
    wet_water = water_depth > 0.004

    # ── 3. Alpine Snowpack, Permafrost & Cryosphere Splat Baking ──
    gy, gx = np.gradient(ground, sz, sx)
    slope_deg = np.degrees(np.arctan(np.hypot(gx, gy)))
    slope_s = gaussian_filter_2d(slope_deg, 1.0)
    tpi_m = ground - normalized_smooth(ground, valid, 5.0)

    cliff = np.clip(perimeter / 1.0, 0.0, 1.0)
    cliff = gaussian_filter_2d(cliff * cliff * (3.0 - 2.0 * cliff), 1.4)

    # Steep faces (> 14 deg) shed snow to expose dark frost-shattered slate crags
    rock_slope = np.clip((slope_s - 13.5) / 11.0, 0.0, 1.0)
    rock_ridge = np.clip((tpi_m - 1.6) / 7.0, 0.0, 1.0) * np.clip((ground - 2.0) / 5.5, 0.0, 1.0)
    rock = np.maximum(np.maximum(rock_slope, rock_ridge), 1.0 - cliff)
    rock = np.maximum(rock, corridor * 0.35)
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

    # Snowpack coverage: high on plateaus & sheltered hollows, suppressed on sheer slate cliffs & submerged beds
    snow_cover2 = np.clip((1.0 - rock2 * 0.90) * (1.0 - wet_smooth2) * (0.45 + 0.55 * np.clip(1.0 - slope2 / 22.0, 0.0, 1.0)), 0.0, 1.0)
    snow_cover2[~valid2] = 0.0
    permafrost2 = np.clip((1.0 - snow_cover2) * (1.0 - rock2) * (1.0 - wet_smooth2) * 1.15, 0.0, 1.0)
    wet_ice_bed2 = np.clip(np.maximum(shore2 * 0.85, wet_smooth2), 0.0, 1.0)
    habitat_val2 = np.clip(0.55 + 0.30 * np.clip((tpi2 + 4.0) / 12.0, 0.0, 1.0), 0.0, 1.0)

    X2, Z2 = np.meshgrid(np.arange(w2) * sx2, np.arange(h2) * sz2)
    wx2 = X2 * 0.55 + np.sin(Z2 * 0.42 + X2 * 0.19) * 1.4
    wz2 = Z2 * 0.52 + np.cos(X2 * 0.38 - Z2 * 0.25) * 1.4
    frost_pat = np.abs(np.sin(wx2 * 1.10) * np.cos(wz2 * 1.10))
    hoarfrost2 = np.clip((1.0 - wet_smooth2) * np.clip(1.0 - frost_pat * 1.8, 0.0, 1.0) * (0.35 + 0.65 * shore2), 0.0, 1.0)
    scree_gravel2 = np.clip((slope2 - 8.5) / 12.0, 0.0, 1.0) * (0.42 + 0.58 * rock2)
    glazed_ice2 = np.clip(shore2 * 0.90 * (1.0 - slope2 / 20.0), 0.0, 1.0) * (1.0 - wet_smooth2 * 0.6)

    splat2 = np.stack((snow_cover2, rock2, np.clip(snow_cover2 * 0.25, 0.0, 1.0), shore2), axis=-1)
    splat2[~valid2] = (0, 1, 0, 0)
    habitat2 = np.stack((snow_cover2, snow_cover2, shore2, habitat_val2), axis=-1)
    habitat2[~valid2] = 0.0
    ground_material2 = np.stack((snow_cover2, permafrost2, wet_ice_bed2, rock2), axis=-1)
    ground_material2[~valid2] = (0, 0, 0, 1)

    out_assets_dir = os.path.join(ROOT_DIR, "assets/maps/mau_son")
    out_world_dir = os.path.join(ROOT_DIR, "maps/worlds/mau_son")
    os.makedirs(out_assets_dir, exist_ok=True)
    os.makedirs(out_world_dir, exist_ok=True)
    p = lambda s: os.path.join(out_assets_dir, "mau_son_" + s)

    write_png(p("island_heightmap.png"), encoded)
    height_rgba = np.stack((height_code >> 8, height_code & 255, np.zeros_like(height_code), np.full_like(height_code, 255)), axis=-1)
    write_png(p("heightfield.png"), height_rgba, 6)

    # 2x Open World heightfield with wind-sculpted sastrugi snow waves
    sastrugi_ow = np.sin(X2 * 0.30 + Z2 * 0.18 + np.sin(Z2 * 0.09) * 1.6) * 0.09 + np.cos(X2 * 0.48 - Z2 * 0.36) * 0.04
    ow_flat = np.clip(1.0 - slope2 / 16.0, 0.0, 1.0) * (1.0 - wet_smooth2) * valid2.astype(np.float64)
    terrain_ow = np.where(valid2, ground2 + sastrugi_ow * ow_flat, -cliff_depth)
    height_code_ow = np.rint(np.clip((terrain_ow + cliff_depth) / total_range, 0.0, 1.0) * 65535.0).astype(np.uint16)
    height_rgba_ow = np.stack((height_code_ow >> 8, height_code_ow & 255, np.zeros_like(height_code_ow), np.full_like(height_code_ow, 255)), axis=-1)
    write_png(p("heightfield_ow.png"), height_rgba_ow, 6)

    write_png(p("ecology_splat.png"), np.rint(splat2 * 255), 6)
    write_png(p("ground_material.png"), np.rint(ground_material2 * 255), 6)
    write_png(p("habitat.png"), np.rint(habitat2 * 255), 6)
    write_png(p("ice_flow.png"), np.rint(ice_flow_rgba * 255), 6)

    riparian_b = np.rint(np.clip(gaussian_filter_2d(wet_water.astype(float), 2.8) * 3.5, 0.0, 1.0) * 255).astype(np.uint8)
    water_rgba = np.stack((water_code >> 8, water_code & 255, riparian_b, wet_water * 255), axis=-1).astype(np.uint8)
    write_png(p("water_height.png"), water_rgba, 6)

    shore_code2 = np.rint((np.clip(signed_shore2 / 16.0, -1.0, 1.0) * 0.5 + 0.5) * 65535).astype(np.uint16)
    distance2 = np.stack((np.full_like(shore_code2, 255), np.full_like(shore_code2, 255), shore_code2 >> 8, shore_code2 & 255), axis=-1)
    write_png(p("habitat_distance.png"), distance2, 6)

    triangles = write_water_mesh(p("water.obj"), ground, water_plane, sx, sz)

    # ── 4. Alpine Landmarks: Cổng Băng Tuyết Arch, Hàn Băng Thạch Trận & Slate Crags ──
    rng = np.random.default_rng(20261122)
    placements = []
    occupied = []

    # Precompute 2.2m dry-shore clearance mask so rocks never clip into water/cascades
    wet_buffer = gaussian_filter_2d(wet_water.astype(np.float64), 2.2) > 0.04

    def sample_cell(x, z):
        r = int(np.clip(z / sz, 0, h - 1))
        c = int(np.clip(x / sx, 0, w - 1))
        return float(ground[r, c]), float(slope_s[r, c]), float(tpi_m[r, c]), bool(valid[r, c]), bool(wet_water[r, c] or wet_buffer[r, c])

    def can_place(x, z, r_scale, min_sep=0.62):
        if x < 16.0 or x > width - 16.0 or z < 16.0 or z > depth - 16.0:
            return False
        if np.hypot(x - arena_x, z - arena_z) < 15.5 + r_scale:
            return False
        for ox, oz, orad in occupied:
            if np.hypot(x - ox, z - oz) < (r_scale + orad) * min_sep:
                return False
        return True

    def add_rock(x, z, r_scale, h_scale, rot, arch, allow_stack=False):
        gy, sl, _, val, is_wet = sample_cell(x, z)
        if not val or is_wet or gy < 0.25 or sl > 34.0:
            return False
        if not allow_stack and not can_place(x, z, r_scale):
            return False
        placements.append((x, gy, z, r_scale, h_scale, rot % 360.0, arch))
        if not allow_stack:
            occupied.append((x, z, r_scale))
        return True

    # 4A. Cổng Băng Tuyết (Frost-Covered Natural Granite/Slate Arch on NW Ridge Overlook)
    nw_mask = valid & (X < width * 0.36) & (Z < depth * 0.45) & (perimeter >= 3.0) & (perimeter <= 8.5) & (~wet_buffer)
    nw_r, nw_c = np.where(nw_mask)
    arch_x, arch_z = width * 0.22, depth * 0.32
    if len(nw_r) > 0:
        pick = len(nw_r) // 2
        arch_x, arch_z = float(nw_c[pick] * sx), float(nw_r[pick] * sz)
        gy_a, _, _, _, _ = sample_cell(arch_x, arch_z)
        placements.append((arch_x - 3.3, gy_a, arch_z - 1.2, 2.10, 1.95, 24.0, "MAP_ROCK_CRAG"))
        placements.append((arch_x + 3.3, gy_a, arch_z + 1.2, 2.10, 1.95, 204.0, "MAP_ROCK_CRAG"))
        placements.append((arch_x - 1.4, gy_a + 3.2, arch_z - 0.5, 2.50, 1.65, 20.0, "MAP_ROCK_SLATE"))
        placements.append((arch_x + 1.4, gy_a + 3.2, arch_z + 0.5, 2.50, 1.65, 200.0, "MAP_ROCK_SLATE"))
        occupied.append((arch_x, arch_z, 5.8))

    # 4B. Hàn Băng Thạch Trận (Frosted Alpine Granite & Slate Monoliths framing Arena & Lakes)
    # Note: MAP_ROCK_CRAG mesh already has 2.65x internal Y stretch -> keep h_scale = rp * 0.58..0.90!
    # MAP_ROCK_SLATE mesh has flat Y -> boost h_scale = rp * 1.45..2.05 for solid vertical outcrops.
    for i in range(14):
        ang = i * (2.0 * np.pi / 14.0) + rng.uniform(-0.12, 0.12)
        dist = float(rng.uniform(18.5, 28.0))
        px_i = arena_x + np.cos(ang) * dist
        pz_i = arena_z + np.sin(ang) * dist
        rp = float(rng.uniform(1.35, 2.25))
        if i % 2 == 0:
            arch = "MAP_ROCK_GRANITE"
            hp = rp * float(rng.uniform(0.85, 1.25))
        else:
            arch = "MAP_ROCK_CRAG"
            hp = rp * float(rng.uniform(0.60, 0.90))
        if add_rock(px_i, pz_i, rp, hp, float(np.degrees(ang)), arch):
            add_rock(px_i + np.cos(ang + 0.45) * rp * 0.85, pz_i + np.sin(ang + 0.45) * rp * 0.85,
                     rp * 0.65, rp * 1.45, float(np.degrees(ang) + 28.0), "MAP_ROCK_SLATE", allow_stack=True)

    # 4C. Frost-Rimed Alpine Granite Crags & Summit Outcrops
    rim_mask = valid & (~wet_buffer) & (ground > 3.0) & (slope_s > 9.0) & (slope_s < 32.0)
    r_rows, r_cols = np.where(rim_mask)
    if len(r_rows) > 0:
        order = rng.permutation(len(r_rows))
        crag_clusters = 0
        for idx in order:
            if crag_clusters >= 24:
                break
            bx, bz = float(r_cols[idx] * sx), float(r_rows[idx] * sz)
            rb = float(rng.uniform(1.85, 3.10))
            if not can_place(bx, bz, rb, min_sep=1.35):
                continue
            hb = rb * float(rng.uniform(0.58, 0.88))
            if add_rock(bx, bz, rb, hb, float(rng.uniform(0, 360)), "MAP_ROCK_CRAG"):
                crag_clusters += 1
                for k in range(int(rng.integers(3, 6))):
                    ang = k * (2.0 * np.pi / 5.0) + float(rng.uniform(-0.25, 0.25))
                    dist = rb * float(rng.uniform(0.58, 0.95))
                    cr = rb * float(rng.uniform(0.46, 0.72))
                    if k % 2 == 0:
                        sub_arch = "MAP_ROCK_GRANITE"
                        ch = cr * float(rng.uniform(0.75, 1.15))
                    else:
                        sub_arch = "MAP_ROCK_SLATE"
                        ch = cr * float(rng.uniform(1.45, 2.05))
                    add_rock(bx + np.cos(ang) * dist, bz + np.sin(ang) * dist, cr, ch,
                             float(rng.uniform(0, 360)), sub_arch, allow_stack=True)

    # 4D. Alpine Granite Boulders & Frost-Weathered Outcrops
    for _ in range(1300):
        if len(placements) >= 340:
            break
        x = float(rng.uniform(18.0, width - 18.0))
        z = float(rng.uniform(18.0, depth - 18.0))
        gy, sl, tpi_v, val, is_wet = sample_cell(x, z)
        if not val or is_wet or gy < 0.35 or sl > 30.0:
            continue
        r_s = float(rng.uniform(0.75, 1.80))
        if tpi_v > 1.8 and rng.random() < 0.30:
            arch = "MAP_ROCK_CRAG"
            h_s = r_s * float(rng.uniform(0.56, 0.82))
        elif rng.random() < 0.38:
            arch = "MAP_ROCK_SLATE"
            h_s = r_s * float(rng.uniform(1.45, 2.10))
        else:
            arch = "MAP_ROCK_GRANITE"
            h_s = r_s * float(rng.uniform(0.72, 1.12))
        if add_rock(x, z, r_s, h_s, float(rng.uniform(0, 360)), arch):
            for _ in range(int(rng.integers(1, 3))):
                a = rng.uniform(0.0, 2.0 * np.pi)
                d = r_s + float(rng.uniform(0.38, 1.35))
                sr = float(rng.uniform(0.24, 0.52))
                add_rock(x + np.cos(a) * d, z + np.sin(a) * d, sr, sr * 0.55, float(rng.uniform(0, 360)), "MAP_ROCK_SCREE")

    # Bake rock contact AO & frost-shattered slate talus aprons into debris_map.png
    rock_apron2 = np.zeros_like(rock2)
    rock_contact2 = np.zeros_like(rock2)
    for rx, ry, rz, rr, rh, rrot, rarch in placements:
        if rarch == "MAP_ROCK_SCREE":
            continue
        reach = rr + 3.6
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
    debris_map2 = np.stack((hoarfrost2, grus2, rock_contact2, glazed_ice2), axis=-1)
    debris_map2[~valid2] = 0.0
    write_png(p("debris_map.png"), np.rint(debris_map2 * 255), 6)

    # Clean neutral-ivory alpine snow -> crisp slate permafrost tint (zero cyan/green bias)
    tint_r = 0.94 - 0.14 * rock2 - 0.06 * shore2
    tint_g = 0.95 - 0.12 * rock2 - 0.04 * shore2
    tint_b = 0.98 - 0.08 * rock2 + 0.01 * shore2
    tint_map2 = np.stack((np.clip(tint_r, 0.0, 1.0), np.clip(tint_g, 0.0, 1.0), np.clip(tint_b, 0.0, 1.0)), axis=-1)
    tint_map2[~valid2] = (0.52, 0.56, 0.64)
    write_png(p("ground_tint.png"), np.rint(tint_map2 * 255), 2)

    # 256x256 Alpine snow-sastrugi & permafrost relief map
    rel_sz = 256
    ry, rx_i = np.mgrid[0:rel_sz, 0:rel_sz].astype(np.float64) / rel_sz
    snow_h = 0.5 + 0.20 * np.sin(rx_i * 2.0 * np.pi * 6.0 + np.sin(ry * 2.0 * np.pi * 2.0)) + 0.12 * np.cos(ry * 2.0 * np.pi * 10.0)
    slate_h = 0.46 + 0.24 * np.sin(rx_i * 2.0 * np.pi * 14.0) * np.cos(ry * 2.0 * np.pi * 14.0)
    rel_rgba = np.stack((
        np.clip(snow_h, 0.0, 1.0),
        np.clip(slate_h, 0.0, 1.0),
        np.clip(0.35 + 0.35 * np.sin((rx_i + ry) * 2.0 * np.pi * 5.0), 0.0, 1.0),
        np.clip(0.35 + 0.35 * np.cos((rx_i - ry) * 2.0 * np.pi * 6.0), 0.0, 1.0),
    ), axis=-1)
    write_png(p("relief.png"), np.rint(rel_rgba * 255), 6)

    # Write generated C metadata header
    meta_h = os.path.join(out_assets_dir, "mau_son_generated_meta.h")
    with open(meta_h, "w") as f:
        f.write("// Generated by maps/pipeline/bake_mau_son.py\n")
        f.write("#ifndef MAU_SON_GENERATED_META_H\n#define MAU_SON_GENERATED_META_H\n")
        f.write(f"#define MAU_SON_MAP_WIDTH {width:.6f}f\n")
        f.write(f"#define MAU_SON_MAP_DEPTH {depth:.6f}f\n")
        f.write(f"#define MAU_SON_HEIGHT_RANGE {height_range:.6f}f\n")
        f.write(f"#define MAU_SON_CLIFF_DEPTH {cliff_depth:.6f}f\n")
        f.write(f"#define MAU_SON_ARENA_X {arena_x:.6f}f\n")
        f.write(f"#define MAU_SON_ARENA_Z {arena_z:.6f}f\n")
        f.write(f"#define MAU_SON_LAKE_X {lake_cx:.6f}f\n")
        f.write(f"#define MAU_SON_LAKE_Z {lake_cz:.6f}f\n")
        f.write(f"#define MAU_SON_LAKE_RX {lake_rx:.6f}f\n")
        f.write(f"#define MAU_SON_LAKE_RZ {lake_rz:.6f}f\n")
        f.write(f"#define MAU_SON_TARN_X {tarn_cx:.6f}f\n")
        f.write(f"#define MAU_SON_TARN_Z {tarn_cz:.6f}f\n")
        f.write(f"#define MAU_SON_ARCH_X {arch_x:.6f}f\n")
        f.write(f"#define MAU_SON_ARCH_Z {arch_z:.6f}f\n")
        f.write("#endif // MAU_SON_GENERATED_META_H\n")

    # ── 5. 3D Frost-Shattered Slate Pebbles & Ice Shards (MapDebrisSystem) ──
    debris_items = []
    for rx, ry, rz, rr, rh, rrot, rarch in placements:
        if rarch == "MAP_ROCK_SCREE":
            continue
        n_chips = 5 if rr > 1.5 else 3
        for _ in range(n_chips):
            if len(debris_items) >= 1500:
                break
            a = float(rng.uniform(0.0, 2.0 * np.pi))
            d = rr * float(rng.uniform(0.85, 2.15))
            dx_p = rx + np.cos(a) * d
            dz_p = rz + np.sin(a) * d
            gy_p, sl_p, _, val_p, is_wet_p = sample_cell(dx_p, dz_p)
            if not val_p or is_wet_p or gy_p < 0.10 or sl_p > 34.0:
                continue
            sc = float(rng.uniform(1.5, 3.5))
            rot_d = float(rng.uniform(0.0, 360.0))
            tone = rng.random()
            if tone < 0.55:
                col = (92, 102, 114)    # frost-dusted slate pebble
            elif tone < 0.85:
                col = (165, 202, 224)   # fractured glacial ice chunk
            else:
                col = (68, 76, 88)      # dark metamorphic slate chip
            debris_items.append((dx_p, gy_p, dz_p, rot_d, sc, col, "MAP_DEBRIS_PEBBLE"))

    for _ in range(1400):
        if len(debris_items) >= 1500:
            break
        dx_p = float(rng.uniform(18.0, width - 18.0))
        dz_p = float(rng.uniform(18.0, depth - 18.0))
        gy_p, sl_p, _, val_p, is_wet_p = sample_cell(dx_p, dz_p)
        if not val_p or is_wet_p or gy_p < 0.15 or sl_p > 26.0:
            continue
        sc = float(rng.uniform(1.2, 2.6))
        rot_d = float(rng.uniform(0.0, 360.0))
        col = (104, 116, 128) if rng.random() < 0.65 else (175, 212, 232)
        debris_items.append((dx_p, gy_p, dz_p, rot_d, sc, col, "MAP_DEBRIS_PEBBLE"))

    rock_header = os.path.join(out_world_dir, "mau_son_rocks_generated.h")
    with open(rock_header, "w") as f:
        f.write("// Generated by maps/pipeline/bake_mau_son.py\n")
        f.write("// Mẫu Sơn Alpine Peak: Cổng Băng Tuyết Arch, Hàn Băng Thạch Trận, Slate Crags & Ice Debris\n")
        f.write("#ifndef MAU_SON_ROCKS_GENERATED_H\n#define MAU_SON_ROCKS_GENERATED_H\n\n")
        f.write('#include "maps/toolkit/map_props.h"\n\n')
        f.write("static const MapRockItem MAU_SON_ROCK_ITEMS[] = {\n")
        for x, y, z, r_s, h_s, rot, arch in placements:
            f.write(f"    {{{{{x:.2f}f, {y:.2f}f, {z:.2f}f}}, {r_s:.2f}f, {h_s:.2f}f, {rot:.1f}f, {arch}}},\n")
        f.write("};\n\n")
        f.write(f"#define MAU_SON_ROCK_COUNT {len(placements)}\n\n")
        f.write("static const MapDebrisPlacement MAU_SON_DEBRIS_ITEMS[] = {\n")
        for dx_p, dy_p, dz_p, drot, dsc, (cr, cg, cb), dtype in debris_items:
            f.write(f"    {{{{{dx_p:.2f}f, {dy_p:.2f}f, {dz_p:.2f}f}}, {drot:.1f}f, {dsc:.2f}f, {{{cr}, {cg}, {cb}, 255}}, {dtype}}},\n")
        f.write("};\n\n")
        f.write(f"#define MAU_SON_DEBRIS_COUNT {len(debris_items)}\n\n")
        f.write("#endif // MAU_SON_ROCKS_GENERATED_H\n")

    print(
        f"[MauSon] Baked {w}x{h} alpine terrain, {triangles} glacial water tris, "
        f"{len(placements)} rocks/ice spires, {len(debris_items)} frost debris -> {rock_header}",
        flush=True,
    )


if __name__ == "__main__":
    bake_mau_son()
