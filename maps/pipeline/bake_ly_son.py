#!/usr/bin/env python3
"""
Bake Lý Sơn (Fire / Hệ Hỏa — Xích Hỏa Sơn) volcanic island map assets from
projected UTM 49N DEM (assets/maps/ly_son/DEM_LySon_UTM49N.tif):
  1. Preserves endoreic volcanic caldera craters (Thới Lới & Giếng Tiền) via
     sink_depth = priority_flood(dem) - dem rather than flattening them.
  2. Carves breached caldera spillways and radial D8 molten lava rivers with
     C^2 Gaussian-smoothed beds and raised basaltic levee margins.
  3. Exports 16-bit heightfields (MOBA 1x & Open World 2x), conformal magma
     mesh (ly_son_water.obj), thermal splat/debris/relief maps, and LUT.
  4. Places Hang Câu columnar basalt palisades (MAP_ROCK_BASALT), Cổng Tò Vò
     natural basalt arch, obsidian monoliths (MAP_ROCK_OBSIDIAN), scoria talus,
     and geothermal fumarole vents into ly_son_rocks_generated.h.
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


def bake_ly_son():
    dem_path = os.path.join(ROOT_DIR, "assets/maps/ly_son/DEM_LySon_UTM49N.tif")
    if not os.path.exists(dem_path):
        from fetch_ly_son_dem import synthesize_ly_son_dem, write_geotiff_utm49n
        dem_syn = synthesize_ly_son_dem()
        write_geotiff_utm49n(dem_path, dem_syn, origin_x=294000.0, origin_y=1704500.0, pixel_size_m=15.0)

    raw, valid_raw, geo = read_dem(dem_path)
    yy, xx = np.where(valid_raw)
    crop = (slice(yy.min(), yy.max() + 1), slice(xx.min(), xx.max() + 1))
    source = raw[crop]
    valid_source = valid_raw[crop]
    px, py = geo["pixel_size_m"]

    max_dim = 300.0
    height_range = 28.0
    cliff_depth = 8.0
    cliff_margin = 1.0
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
    terrain = height_range * np.power(normalized, 1.08)

    X, Z = np.meshgrid(np.arange(w) * sx, np.arange(h) * sz)

    # Central volcanic saddle combat arena (walkable ash/basalt plain between Thới Lới & Giếng Tiền)
    cx_mid, cz_mid = width * 0.48, depth * 0.50
    arena_dist = np.hypot(X - cx_mid, Z - cz_mid)
    arena_x, arena_z = float(cx_mid), float(cz_mid)

    # Organic radial pahoehoe lava flow lobes (replaces periodic step_h=1.45m zebra terracing)
    ang_e = np.arctan2(Z - depth * 0.46, X - width * 0.72)
    ang_w = np.arctan2(Z - depth * 0.52, X - width * 0.30)
    lobe_e = np.sin(ang_e * 7.0 + np.sin(X * 0.045 + Z * 0.038) * 1.6) * 0.5 + 0.5
    lobe_w = np.sin(ang_w * 5.0 - np.cos(Z * 0.052 - X * 0.041) * 1.4) * 0.5 + 0.5
    radial_lobe = (lobe_e * 0.65 + lobe_w * 0.35) * 0.72
    arena_guard = np.clip((arena_dist - 15.0) / 10.0, 0.0, 1.0)
    terrain = np.where(valid, terrain + radial_lobe * arena_guard * np.clip((terrain - 2.0) / 10.0, 0.0, 1.0), -cliff_depth)

    # ── 1. Endoreic Caldera Crater Detection (Preserved instead of Priority-Flooded away) ──
    perimeter = distance_inside(valid, sx, sz)
    filled_dem = priority_flood(terrain, valid)
    sink_depth = np.where(valid, np.maximum(0.0, filled_dem - terrain), 0.0)

    # Identify primary caldera (eastern Thới Lới crater) and secondary crater (western Giếng Tiền)
    caldera_raw = valid & (sink_depth > 0.35) & (perimeter > 8.0) & (terrain > 3.0)
    caldera_blob = gaussian_filter_2d(caldera_raw.astype(np.float64), 2.2)
    caldera_zone = valid & (caldera_blob > 0.20) & (perimeter > 6.5)

    # Primary caldera (largest eastern component)
    east_caldera = largest_component(caldera_zone & (X > width * 0.50))
    if east_caldera.sum() < 12:
        # Fallback to eastern summit depression if DEM was very smooth
        peak_idx = np.unravel_index(np.argmax(np.where(valid & (X > width * 0.50), terrain, -np.inf)), terrain.shape)
        py_c, px_c = peak_idx[0] * sz, peak_idx[1] * sx
        east_caldera = valid & (np.hypot(X - px_c, Z - py_c) < 15.0)
    west_caldera = largest_component(caldera_zone & (X <= width * 0.50))

    c_ys, c_xs = np.where(east_caldera)
    caldera_cx = float(c_xs.mean() * sx)
    caldera_cz = float(c_ys.mean() * sz)
    caldera_rx = float(max(10.0, (c_xs.max() - c_xs.min() + 1) * sx * 0.5))
    caldera_rz = float(max(10.0, (c_ys.max() - c_ys.min() + 1) * sz * 0.5))

    # Carve smooth C^2 magma bowl in primary & secondary calderas
    caldera_all = east_caldera | west_caldera
    caldera_smooth = gaussian_filter_2d(caldera_all.astype(np.float64), 2.0)
    magma_plane = np.full_like(terrain, -cliff_depth - 1.0)

    if east_caldera.any():
        east_rim_y = float(np.percentile(terrain[east_caldera], 82))
        east_magma_y = max(3.5, east_rim_y - 0.45)
        east_w = gaussian_filter_2d(east_caldera.astype(np.float64), 1.8)
        east_bed = east_magma_y - 1.35 * np.clip(east_w * 1.30, 0.0, 1.0)
        terrain = np.where(valid & (east_w > 0.03), np.minimum(terrain, east_bed), terrain)
        magma_plane = np.where(east_w > 0.05, np.maximum(magma_plane, east_magma_y), magma_plane)

    if west_caldera.any():
        west_rim_y = float(np.percentile(terrain[west_caldera], 82))
        west_magma_y = max(2.2, west_rim_y - 0.35)
        west_w = gaussian_filter_2d(west_caldera.astype(np.float64), 1.8)
        west_bed = west_magma_y - 1.05 * np.clip(west_w * 1.30, 0.0, 1.0)
        terrain = np.where(valid & (west_w > 0.03), np.minimum(terrain, west_bed), terrain)
        magma_plane = np.where(west_w > 0.05, np.maximum(magma_plane, west_magma_y), magma_plane)

    # ── 2. Radial D8 Molten Lava Spillways from Breached Crater Rims ──
    directions, accum, conditioned = compute_d8_flow(terrain, valid, sx, sz)
    threshold = max(14.0, float(valid.sum()) * 0.012)
    lava_streams = valid & (accum >= threshold) & (perimeter > 4.5) & (~caldera_all)
    # Keep central combat arena (radius 14m) clear of deep lava rivers
    lava_streams = lava_streams & (arena_dist > 14.5)

    dd = [(0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1), (1, 0), (1, 1)]
    corridor = np.zeros_like(terrain)
    stream_surf_num = np.zeros_like(terrain)
    stream_surf_den = np.zeros_like(terrain)
    flow_vx = np.zeros_like(terrain)
    flow_vz = np.zeros_like(terrain)
    flow_speed_acc = np.zeros_like(terrain)

    for r, c in zip(*np.where(lava_streams)):
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

        half = float(np.clip(0.85 + 0.022 * np.sqrt(accum[r, c]), 0.90, 2.30))
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
        # Raised basaltic levee rim on lava channel shoulders + deeper incised U-shaped magma gorge
        levee = np.exp(-((dist - radius * 0.78) / (radius * 0.24)) ** 2) * 0.42
        bed = np.minimum(terrain[sl], level - 1.35 * weight) + levee
        terrain[sl] = terrain[sl] * (1.0 - weight) + bed * weight

        # Clamp stream surface strictly inside the raised levee core (eliminates detached outer blobs)
        surf_r = half + max(sx, sz) * 0.65
        w_surf = np.clip((surf_r - dist) / max(surf_r, 1e-6), 0.0, 1.0)
        w_surf = w_surf * w_surf * (3.0 - 2.0 * w_surf)
        stream_surf_num[sl] += w_surf * (level - 0.16)
        stream_surf_den[sl] += w_surf
        flow_vx[sl] += w_surf * dir_x
        flow_vz[sl] += w_surf * dir_z
        flow_speed_acc[sl] = np.maximum(flow_speed_acc[sl], w_surf * np.clip(0.35 + 0.65 * (1.0 - dist / max(surf_r, 1e-6)), 0.0, 1.0))

    has_stream_surf = stream_surf_den > 1e-4
    avg_stream_surf = np.where(has_stream_surf, stream_surf_num / np.maximum(stream_surf_den, 1e-4), terrain - 0.25)

    bed_smooth = normalized_smooth(terrain, valid, 1.65)
    smooth_mask = np.clip(corridor * 0.88 + caldera_smooth * 0.90, 0.0, 0.92)
    terrain = terrain * (1.0 - smooth_mask) + bed_smooth * smooth_mask
    terrain[~valid] = -cliff_depth

    stream_plane = np.where(has_stream_surf, avg_stream_surf, terrain - 0.25)
    lava_plane = np.where(caldera_smooth > 0.05, magma_plane, stream_plane)
    rim_taper = np.clip((perimeter - 2.5) / 4.5, 0.0, 1.0)
    lava_plane = np.where(valid, lava_plane * rim_taper + (terrain - 0.35) * (1.0 - rim_taper), terrain - 0.35)

    # Connected-component pruning: keep ONLY lava components connected to calderas (>= 25 cells)
    raw_lava_cand = valid & ((lava_plane - terrain) > 0.006)
    visited = np.zeros_like(raw_lava_cand, dtype=bool)
    kept_lava = np.zeros_like(raw_lava_cand, dtype=bool)
    from collections import deque
    for sr, sc in zip(*np.where(raw_lava_cand)):
        if visited[sr, sc]:
            continue
        comp = []
        q = deque([(sr, sc)])
        visited[sr, sc] = True
        touches_caldera = False
        while q:
            cr, cc = q.popleft()
            comp.append((cr, cc))
            if caldera_all[cr, cc]:
                touches_caldera = True
            for dr in (-1, 0, 1):
                for dc in (-1, 0, 1):
                    if dr == 0 and dc == 0:
                        continue
                    nr, nc = cr + dr, cc + dc
                    if 0 <= nr < h and 0 <= nc < w and raw_lava_cand[nr, nc] and not visited[nr, nc]:
                        visited[nr, nc] = True
                        q.append((nr, nc))
        if touches_caldera or len(comp) >= 28:
            for cr, cc in comp:
                kept_lava[cr, cc] = True

    # Suppress disconnected orphan puddles below terrain
    lava_plane = np.where(kept_lava | (caldera_smooth > 0.05), lava_plane, terrain - 0.35)

    # Diffuse lava_plane smoothly onto adjacent banks for clean shoreline clipping
    raw_lava = valid & ((lava_plane - terrain) > 0.006)
    near_lava = gaussian_filter_2d(raw_lava.astype(float), 2.0) > 0.01
    smooth_ext = normalized_smooth(lava_plane, raw_lava | (caldera_smooth > 0.05), 1.6)
    lava_plane = np.where(raw_lava, lava_plane, np.where(near_lava, np.minimum(smooth_ext, terrain - 0.004), terrain - 0.30))

    # Smooth D8 lava flowmap (RG = unit flow dir, B = speed/shear, A = caldera mask)
    f_len = np.hypot(flow_vx, flow_vz)
    fx_u = np.where(f_len > 1e-4, flow_vx / np.maximum(f_len, 1e-4), 0.0)
    fz_u = np.where(f_len > 1e-4, flow_vz / np.maximum(f_len, 1e-4), 0.0)
    fx_s = gaussian_filter_2d(fx_u, 1.4)
    fz_s = gaussian_filter_2d(fz_u, 1.4)
    fl_s = np.hypot(fx_s, fz_s)
    fx_n = np.where(fl_s > 1e-4, fx_s / np.maximum(fl_s, 1e-4), -0.65)
    fz_n = np.where(fl_s > 1e-4, fz_s / np.maximum(fl_s, 1e-4), 0.75)
    caldera_mask_f = np.clip(caldera_smooth * 1.4, 0.0, 1.0)
    flow_rgba = np.stack((
        np.clip(fx_n * 0.5 + 0.5, 0.0, 1.0),
        np.clip(fz_n * 0.5 + 0.5, 0.0, 1.0),
        np.clip(gaussian_filter_2d(flow_speed_acc, 1.0), 0.0, 1.0),
        caldera_mask_f,
    ), axis=-1)

    height_code = np.rint(np.clip((terrain + cliff_depth) / total_range, 0.0, 1.0) * 65535.0).astype(np.uint16)
    ground = height_code.astype(np.float64) / 65535.0 * total_range - cliff_depth
    encoded = np.rint(height_code.astype(np.float64) / 257.0).astype(np.uint8)

    lava_code = np.rint(np.clip((lava_plane + cliff_depth) / total_range, 0.0, 1.0) * 65535.0).astype(np.uint16)
    lava_depth = np.where(valid & ((lava_plane - ground) > 0.004), np.maximum(0.0, lava_plane - ground), 0.0)
    wet_lava = lava_depth > 0.004

    # ── 3. Volcanic Thermal Splat, Ecology & Debris Baking ──
    gy, gx = np.gradient(ground, sz, sx)
    slope_deg = np.degrees(np.arctan(np.hypot(gx, gy)))
    slope_s = gaussian_filter_2d(slope_deg, 1.0)
    tpi_m = ground - normalized_smooth(ground, valid, 5.0)

    cliff = np.clip(perimeter / 1.0, 0.0, 1.0)
    cliff = gaussian_filter_2d(cliff * cliff * (3.0 - 2.0 * cliff), 1.4)

    rock_slope = np.clip((slope_s - 9.5) / 12.0, 0.0, 1.0)
    rock_ridge = np.clip((tpi_m - 1.2) / 7.5, 0.0, 1.0) * np.clip((ground - 1.5) / 5.5, 0.0, 1.0)
    rock = np.maximum(np.maximum(rock_slope, rock_ridge), 1.0 - cliff)
    rock = np.maximum(rock, corridor * 0.45)
    rock[wet_lava] = 0.0

    shore_lava = gaussian_filter_2d(wet_lava.astype(np.float64), max(1.1, 2.2 / min(sx, sz)))

    # 2x Super-resolved maps
    h2, w2 = h * 2 - 1, w * 2 - 1
    sx2, sz2 = width / (w2 - 1), depth / (h2 - 1)
    up2 = lambda a: resample_grid(a.astype(np.float64), h2, w2)
    valid2 = up2(valid.astype(float)) > 0.5
    ground2 = gaussian_filter_2d(up2(ground), 0.85)
    lava_plane2 = gaussian_filter_2d(up2(lava_plane), 1.15)
    elev_diff2 = ground2 - lava_plane2
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

    # Sparse scorched grass survival pockets away from molten lava & steep basalt cliffs
    coverage2 = np.clip((1.0 - rock2 * 0.92) * (1.0 - shore2 * 0.95) * (0.28 + 0.45 * np.clip(1.0 - slope2 / 16.0, 0.0, 1.0)), 0.0, 1.0)
    coverage2[~valid2] = 0.0
    soil2 = np.clip((1.0 - coverage2) * (1.0 - rock2) * (1.0 - wet_smooth2) * 1.15, 0.0, 1.0)
    wet_mud2 = np.clip(np.maximum(shore2 * 0.82, wet_smooth2), 0.0, 1.0)
    habitat_val2 = np.clip(0.55 + 0.30 * np.clip((tpi2 + 4.0) / 12.0, 0.0, 1.0), 0.0, 1.0)

    # Volcanic debris map channels:
    # R: Sub-surface incandescent magma fissures (strictly on flat cooled shelves < 20 deg near lava)
    # G: Basalt scree & volcanic talus gravel (enhanced around placed basalt columns)
    # B: Rock contact AO (filled after rock placement)
    # A: Yellow-ochre fumarolic sulfur crust (around crater rims & hydrothermal vents)
    X2, Z2 = np.meshgrid(np.arange(w2) * sx2, np.arange(h2) * sz2)
    flat_shelf2 = np.clip(1.0 - (slope2 - 14.0) / 8.0, 0.0, 1.0)
    near_lava_shelf2 = np.clip(1.0 - (signed_shore2 + 0.15) / 1.55, 0.0, 1.0)
    near_lava_shelf2 = near_lava_shelf2 * near_lava_shelf2 * (3.0 - 2.0 * near_lava_shelf2)
    # Isotropic domain-warped hairline cracks (zero Cartesian wall triangles)
    wx2 = X2 * 0.62 + np.sin(Z2 * 0.45 + X2 * 0.21) * 1.4
    wz2 = Z2 * 0.58 + np.cos(X2 * 0.41 - Z2 * 0.27) * 1.4
    crack_pat = np.abs(np.sin(wx2 * 1.15) * np.cos(wz2 * 1.15))
    ember_fissure2 = np.clip(near_lava_shelf2 * flat_shelf2 * np.clip(1.0 - crack_pat * 2.1, 0.0, 1.0), 0.0, 1.0)
    scree_gravel2 = np.clip((slope2 - 7.5) / 12.0, 0.0, 1.0) * (0.40 + 0.60 * rock2)
    sulfur_crust2 = np.clip(up2(caldera_smooth) * 0.85 + shore2 * 0.35, 0.0, 1.0) * (1.0 - wet_smooth2)

    splat2 = np.stack((coverage2, rock2, np.clip(coverage2 * 0.20, 0.0, 1.0), shore2), axis=-1)
    splat2[~valid2] = (0, 1, 0, 0)
    habitat2 = np.stack((coverage2, coverage2, shore2, habitat_val2), axis=-1)
    habitat2[~valid2] = 0.0
    ground_material2 = np.stack((coverage2, soil2, wet_mud2, rock2), axis=-1)
    ground_material2[~valid2] = (0, 0, 0, 1)

    out_assets_dir = os.path.join(ROOT_DIR, "assets/maps/ly_son")
    out_world_dir = os.path.join(ROOT_DIR, "maps/worlds/ly_son")
    os.makedirs(out_assets_dir, exist_ok=True)
    os.makedirs(out_world_dir, exist_ok=True)
    p = lambda s: os.path.join(out_assets_dir, "ly_son_" + s)

    write_png(p("island_heightmap.png"), encoded)
    height_rgba = np.stack((height_code >> 8, height_code & 255, np.zeros_like(height_code), np.full_like(height_code, 255)), axis=-1)
    write_png(p("heightfield.png"), height_rgba, 6)

    # 2x Open World heightfield with pahoehoe rope lava flow micro-relief
    rope_wave = np.sin(X2 * 0.34 + Z2 * 0.21 + np.sin(Z2 * 0.11) * 1.8) * 0.10 + np.cos(X2 * 0.55 - Z2 * 0.41) * 0.05
    ow_flat = np.clip(1.0 - slope2 / 16.0, 0.0, 1.0) * (1.0 - wet_smooth2) * valid2.astype(np.float64)
    terrain_ow = np.where(valid2, ground2 + rope_wave * ow_flat, -cliff_depth)
    height_code_ow = np.rint(np.clip((terrain_ow + cliff_depth) / total_range, 0.0, 1.0) * 65535.0).astype(np.uint16)
    height_rgba_ow = np.stack((height_code_ow >> 8, height_code_ow & 255, np.zeros_like(height_code_ow), np.full_like(height_code_ow, 255)), axis=-1)
    write_png(p("heightfield_ow.png"), height_rgba_ow, 6)

    write_png(p("ecology_splat.png"), np.rint(splat2 * 255), 6)
    write_png(p("ground_material.png"), np.rint(ground_material2 * 255), 6)
    write_png(p("habitat.png"), np.rint(habitat2 * 255), 6)
    write_png(p("lava_flow.png"), np.rint(flow_rgba * 255), 6)

    riparian_b = np.rint(np.clip(gaussian_filter_2d(wet_lava.astype(float), 2.8) * 3.5, 0.0, 1.0) * 255).astype(np.uint8)
    water_rgba = np.stack((lava_code >> 8, lava_code & 255, riparian_b, wet_lava * 255), axis=-1).astype(np.uint8)
    write_png(p("water_height.png"), water_rgba, 6)

    shore_code2 = np.rint((np.clip(signed_shore2 / 16.0, -1.0, 1.0) * 0.5 + 0.5) * 65535).astype(np.uint16)
    distance2 = np.stack((np.full_like(shore_code2, 255), np.full_like(shore_code2, 255), shore_code2 >> 8, shore_code2 & 255), axis=-1)
    write_png(p("habitat_distance.png"), distance2, 6)

    triangles = write_water_mesh(p("water.obj"), ground, lava_plane, sx, sz)

    # ── 4. Volcanic Rock Landmarks: Columnar Basalt, Cổng Tò Vò Arch, Obsidian Monoliths ──
    rng = np.random.default_rng(20261121)
    placements = []
    occupied = []

    def sample_cell(x, z):
        r = int(np.clip(z / sz, 0, h - 1))
        c = int(np.clip(x / sx, 0, w - 1))
        return float(ground[r, c]), float(slope_s[r, c]), float(tpi_m[r, c]), bool(valid[r, c]), bool(wet_lava[r, c])

    def can_place(x, z, r_scale, min_sep=0.62):
        if x < 16.0 or x > width - 16.0 or z < 16.0 or z > depth - 16.0:
            return False
        if np.hypot(x - arena_x, z - arena_z) < 15.0 + r_scale:
            return False
        for ox, oz, orad in occupied:
            if np.hypot(x - ox, z - oz) < (r_scale + orad) * min_sep:
                return False
        return True

    def add_rock(x, z, r_scale, h_scale, rot, arch, allow_stack=False):
        gy, sl, _, val, is_lava = sample_cell(x, z)
        if not val or is_lava or gy < -0.10 or sl > 36.0:
            return False
        if not allow_stack and not can_place(x, z, r_scale):
            return False
        placements.append((x, gy, z, r_scale, h_scale, rot % 360.0, arch))
        if not allow_stack:
            occupied.append((x, z, r_scale))
        return True

    # 4A. Cổng Tò Vò Natural Basalt Arch (NW coastal lava bench)
    # Find valid NW coastal bench point
    nw_mask = valid & (X < width * 0.35) & (Z < depth * 0.45) & (perimeter >= 2.0) & (perimeter <= 7.0) & (~wet_lava)
    nw_r, nw_c = np.where(nw_mask)
    tovo_x, tovo_z = width * 0.20, depth * 0.30
    if len(nw_r) > 0:
        pick = len(nw_r) // 2
        tovo_x, tovo_z = float(nw_c[pick] * sx), float(nw_r[pick] * sz)
        gy_tv, _, _, _, _ = sample_cell(tovo_x, tovo_z)
        # Left & right basalt arch pillars + spanning capstones
        placements.append((tovo_x - 3.2, gy_tv, tovo_z - 1.2, 1.85, 3.60, 22.0, "MAP_ROCK_BASALT"))
        placements.append((tovo_x + 3.2, gy_tv, tovo_z + 1.2, 1.85, 3.60, 202.0, "MAP_ROCK_BASALT"))
        placements.append((tovo_x - 1.4, gy_tv + 3.2, tovo_z - 0.5, 2.35, 1.15, 18.0, "MAP_ROCK_BASALT"))
        placements.append((tovo_x + 1.4, gy_tv + 3.2, tovo_z + 0.5, 2.35, 1.15, 198.0, "MAP_ROCK_BASALT"))
        occupied.append((tovo_x, tovo_z, 5.5))

    # 4B. Hang Câu Columnar Basalt Organ-Pipe Palisades & Crater Rim Pillars (5-7 tightly packed hexagonal columns)
    rim_mask = valid & (~wet_lava) & (ground > 3.5) & (slope_s > 10.0) & (slope_s < 34.0)
    r_rows, r_cols = np.where(rim_mask)
    if len(r_rows) > 0:
        order = rng.permutation(len(r_rows))
        basalt_clusters = 0
        for idx in order:
            if basalt_clusters >= 22:
                break
            bx, bz = float(r_cols[idx] * sx), float(r_rows[idx] * sz)
            rb = float(rng.uniform(1.85, 3.10))
            if not can_place(bx, bz, rb, min_sep=1.35):
                continue
            hb = rb * float(rng.uniform(1.30, 1.95))
            if add_rock(bx, bz, rb, hb, float(rng.uniform(0, 360)), "MAP_ROCK_BASALT"):
                basalt_clusters += 1
                col_count = int(rng.integers(4, 7))
                for k in range(col_count):
                    ang = k * (2.0 * np.pi / col_count) + float(rng.uniform(-0.22, 0.22))
                    dist = rb * float(rng.uniform(0.55, 0.92))
                    cr = rb * float(rng.uniform(0.42, 0.68))
                    add_rock(bx + np.cos(ang) * dist, bz + np.sin(ang) * dist, cr, cr * float(rng.uniform(1.25, 1.85)),
                             float(rng.uniform(0, 360)), "MAP_ROCK_BASALT", allow_stack=True)

    # 4C. Jagged Basalt Lava-Gorge Embankments framing lava river banks (never blocking channel center)
    gorge_mask = valid & (~wet_lava) & (corridor > 0.12) & (corridor < 0.45) & (slope_s < 32.0)
    g_rows, g_cols = np.where(gorge_mask)
    if len(g_rows) > 0:
        g_order = rng.permutation(len(g_rows))
        gorge_count = 0
        for idx in g_order:
            if gorge_count >= 38:
                break
            gx_p, gz_p = float(g_cols[idx] * sx), float(g_rows[idx] * sz)
            rg = float(rng.uniform(0.95, 1.80))
            if add_rock(gx_p, gz_p, rg, rg * float(rng.uniform(0.85, 1.35)), float(rng.uniform(0, 360)), "MAP_ROCK_BASALT"):
                gorge_count += 1

    # 4D. Obsidian Shard Monoliths around Central Combat Arena Perimeter & Caldera Edges
    for i in range(12):
        ang = i * (2.0 * np.pi / 12.0) + rng.uniform(-0.14, 0.14)
        dist = float(rng.uniform(18.0, 26.0))
        ox = arena_x + np.cos(ang) * dist
        oz = arena_z + np.sin(ang) * dist
        ro = float(rng.uniform(1.2, 2.1))
        if add_rock(ox, oz, ro, ro * float(rng.uniform(1.45, 2.05)), float(np.degrees(ang)), "MAP_ROCK_OBSIDIAN"):
            # Twin smaller flanking obsidian shard
            add_rock(ox + np.cos(ang + 0.5) * ro * 0.85, oz + np.sin(ang + 0.5) * ro * 0.85,
                     ro * 0.55, ro * 0.95, float(np.degrees(ang) + 25.0), "MAP_ROCK_OBSIDIAN", allow_stack=True)

    # 4E. Cohesive Volcanic Basalt Outcrops & Scoria Talus Aprons (NO pale granite/limestone boulders)
    for _ in range(1400):
        if len(placements) >= 380:
            break
        x = float(rng.uniform(18.0, width - 18.0))
        z = float(rng.uniform(18.0, depth - 18.0))
        gy, sl, tpi_v, val, is_lava = sample_cell(x, z)
        if not val or is_lava or gy < 0.20 or sl > 32.0:
            continue
        r_s = float(rng.uniform(0.75, 1.85))
        arch = "MAP_ROCK_OBSIDIAN" if (tpi_v > 1.8 and rng.random() < 0.35) else "MAP_ROCK_BASALT"
        h_s = r_s * (1.28 if arch == "MAP_ROCK_OBSIDIAN" else float(rng.uniform(0.75, 1.25)))
        if add_rock(x, z, r_s, h_s, float(rng.uniform(0, 360)), arch):
            for _ in range(int(rng.integers(1, 4))):
                a = rng.uniform(0.0, 2.0 * np.pi)
                d = r_s + float(rng.uniform(0.35, 1.35))
                sr = float(rng.uniform(0.24, 0.52))
                add_rock(x + np.cos(a) * d, z + np.sin(a) * d, sr, sr * 0.50, float(rng.uniform(0, 360)), "MAP_ROCK_SCREE")

    # Bake rock contact AO & basalt scree aprons into debris_map.png
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
        contact = np.exp(-np.maximum(dist_edge + 0.12, 0.0) / 0.45) * 0.85
        rock_apron2[r0:r1, c0:c1] = np.maximum(rock_apron2[r0:r1, c0:c1], apron)
        rock_contact2[r0:r1, c0:c1] = np.maximum(rock_contact2[r0:r1, c0:c1], contact)

    grus2 = np.clip(np.maximum(scree_gravel2, rock_apron2 * 0.85), 0.0, 1.0)
    debris_map2 = np.stack((ember_fissure2, grus2, rock_contact2, sulfur_crust2), axis=-1)
    debris_map2[~valid2] = 0.0
    write_png(p("debris_map.png"), np.rint(debris_map2 * 255), 6)

    # Volcanic charcoal-basalt -> oxidized red scoria -> sulfur tint
    tint_r = 0.28 + 0.18 * shore2 + 0.14 * np.clip(tpi2 / 12.0 + 0.4, 0.0, 1.0) + 0.12 * sulfur_crust2
    tint_g = 0.22 + 0.07 * shore2 + 0.08 * sulfur_crust2
    tint_b = 0.21 + 0.03 * np.clip(1.0 - slope2 / 25.0, 0.0, 1.0)
    tint_map2 = np.stack((np.clip(tint_r, 0.0, 1.0), np.clip(tint_g, 0.0, 1.0), np.clip(tint_b, 0.0, 1.0)), axis=-1)
    tint_map2[~valid2] = (0.18, 0.16, 0.16)
    write_png(p("ground_tint.png"), np.rint(tint_map2 * 255), 2)

    # 256x256 Volcanic pahoehoe relief map
    rel_sz = 256
    ry, rx_i = np.mgrid[0:rel_sz, 0:rel_sz].astype(np.float64) / rel_sz
    rope_h = 0.5 + 0.22 * np.sin(rx_i * 2.0 * np.pi * 8.0 + np.sin(ry * 2.0 * np.pi * 3.0)) + 0.12 * np.cos(ry * 2.0 * np.pi * 12.0)
    ash_h = 0.48 + 0.24 * np.sin(rx_i * 2.0 * np.pi * 16.0) * np.cos(ry * 2.0 * np.pi * 16.0)
    rel_rgba = np.stack((
        np.clip(rope_h, 0.0, 1.0),
        np.clip(ash_h, 0.0, 1.0),
        np.clip(0.35 + 0.35 * np.sin((rx_i + ry) * 2.0 * np.pi * 5.0), 0.0, 1.0),
        np.clip(0.35 + 0.35 * np.cos((rx_i - ry) * 2.0 * np.pi * 6.0), 0.0, 1.0),
    ), axis=-1)
    write_png(p("relief.png"), np.rint(rel_rgba * 255), 6)

    # ── 5. Geothermal Fumarole & Magma Hotspot Vents ──
    vents = []
    vent_candidates = np.where(valid & (shore_lava > 0.25) & (ground > 0.5))
    if len(vent_candidates[0]) > 0:
        v_order = rng.permutation(len(vent_candidates[0]))
        for idx in v_order:
            if len(vents) >= 8:
                break
            vr, vc = int(vent_candidates[0][idx]), int(vent_candidates[1][idx])
            vx, vz = float(vc * sx), float(vr * sz)
            if all(np.hypot(vx - ex, vz - ez) >= 24.0 for ex, _, ez, *_ in vents):
                vents.append((vx, float(ground[vr, vc]), vz, float(rng.uniform(4.5, 7.5)), float(rng.uniform(0.8, 1.3)), float(rng.uniform(0.10, 0.18)), float(rng.uniform(0.0, 6.28))))
    if not vents:
        vents.append((caldera_cx, float(ground[int(caldera_cz / sz), int(caldera_cx / sx)]), caldera_cz, 6.0, 1.2, 0.15, 0.0))

    # Write generated C headers
    meta_h = os.path.join(out_assets_dir, "ly_son_generated_meta.h")
    with open(meta_h, "w") as f:
        f.write("// Generated by maps/pipeline/bake_ly_son.py\n")
        f.write("#ifndef LY_SON_GENERATED_META_H\n#define LY_SON_GENERATED_META_H\n")
        f.write(f"#define LY_SON_MAP_WIDTH {width:.6f}f\n")
        f.write(f"#define LY_SON_MAP_DEPTH {depth:.6f}f\n")
        f.write(f"#define LY_SON_HEIGHT_RANGE {height_range:.6f}f\n")
        f.write(f"#define LY_SON_CLIFF_DEPTH {cliff_depth:.6f}f\n")
        f.write(f"#define LY_SON_ARENA_X {arena_x:.6f}f\n")
        f.write(f"#define LY_SON_ARENA_Z {arena_z:.6f}f\n")
        f.write(f"#define LY_SON_CALDERA_X {caldera_cx:.6f}f\n")
        f.write(f"#define LY_SON_CALDERA_Z {caldera_cz:.6f}f\n")
        f.write(f"#define LY_SON_CALDERA_RX {caldera_rx:.6f}f\n")
        f.write(f"#define LY_SON_CALDERA_RZ {caldera_rz:.6f}f\n")
        f.write(f"#define LY_SON_TOVO_X {tovo_x:.6f}f\n")
        f.write(f"#define LY_SON_TOVO_Z {tovo_z:.6f}f\n")
        f.write("#endif // LY_SON_GENERATED_META_H\n")

    # ── 6. 3D Volcanic Scoria Pebbles & Obsidian Chips (MapDebrisSystem) ──
    debris_items = []
    # Cluster dense basalt scree & obsidian chips around placed hero rocks + arena margins
    for rx, ry, rz, rr, rh, rrot, rarch in placements:
        if rarch == "MAP_ROCK_SCREE":
            continue
        n_chips = 5 if rr > 1.5 else 3
        for _ in range(n_chips):
            if len(debris_items) >= 1600:
                break
            a = float(rng.uniform(0.0, 2.0 * np.pi))
            d = rr * float(rng.uniform(0.85, 2.15))
            dx_p = rx + np.cos(a) * d
            dz_p = rz + np.sin(a) * d
            gy_p, sl_p, _, val_p, is_lava_p = sample_cell(dx_p, dz_p)
            if not val_p or is_lava_p or gy_p < 0.10 or sl_p > 34.0:
                continue
            sc = float(rng.uniform(1.6, 3.8))
            rot_d = float(rng.uniform(0.0, 360.0))
            tone = rng.random()
            if tone < 0.55:
                col = (52, 48, 52)   # dark basalt pebble
            elif tone < 0.85:
                col = (94, 46, 34)   # oxidized red scoria chip
            else:
                col = (34, 34, 40)   # dark obsidian shard
            debris_items.append((dx_p, gy_p, dz_p, rot_d, sc, col, "MAP_DEBRIS_PEBBLE"))

    # Also scatter subtle foreground volcanic grit pebbles across the central combat saddle
    for _ in range(1800):
        if len(debris_items) >= 1600:
            break
        dx_p = float(rng.uniform(18.0, width - 18.0))
        dz_p = float(rng.uniform(18.0, depth - 18.0))
        gy_p, sl_p, _, val_p, is_lava_p = sample_cell(dx_p, dz_p)
        if not val_p or is_lava_p or gy_p < 0.15 or sl_p > 26.0:
            continue
        sc = float(rng.uniform(1.2, 2.8))
        rot_d = float(rng.uniform(0.0, 360.0))
        tone = rng.random()
        col = (54, 50, 54) if tone < 0.65 else (92, 48, 36)
        debris_items.append((dx_p, gy_p, dz_p, rot_d, sc, col, "MAP_DEBRIS_PEBBLE"))

    rock_header = os.path.join(out_world_dir, "ly_son_rocks_generated.h")
    with open(rock_header, "w") as f:
        f.write("// Generated by maps/pipeline/bake_ly_son.py\n")
        f.write("// Lý Sơn Volcanic Island: Columnar Basalt, Cổng Tò Vò Arch, Obsidian Monoliths, Scoria Debris & Vents\n")
        f.write("#ifndef LY_SON_ROCKS_GENERATED_H\n#define LY_SON_ROCKS_GENERATED_H\n\n")
        f.write('#include "maps/toolkit/map_props.h"\n\n')
        f.write("static const MapRockItem LY_SON_ROCK_ITEMS[] = {\n")
        for x, y, z, r_s, h_s, rot, arch in placements:
            f.write(f"    {{{{{x:.2f}f, {y:.2f}f, {z:.2f}f}}, {r_s:.2f}f, {h_s:.2f}f, {rot:.1f}f, {arch}}},\n")
        f.write("};\n\n")
        f.write(f"#define LY_SON_ROCK_COUNT {len(placements)}\n\n")
        f.write("static const MapDebrisPlacement LY_SON_DEBRIS_ITEMS[] = {\n")
        for dx_p, dy_p, dz_p, drot, dsc, (cr, cg, cb), dtype in debris_items:
            f.write(f"    {{{{{dx_p:.2f}f, {dy_p:.2f}f, {dz_p:.2f}f}}, {drot:.1f}f, {dsc:.2f}f, {{{cr}, {cg}, {cb}, 255}}, {dtype}}},\n")
        f.write("};\n\n")
        f.write(f"#define LY_SON_DEBRIS_COUNT {len(debris_items)}\n\n")
        f.write("static const MapVolcanoVent LY_SON_VOLCANO_VENTS[] = {\n")
        for vx, vy, vz, vrad, vheat, vsteam, vphase in vents:
            f.write(f"    {{{{{vx:.2f}f, {vy:.2f}f, {vz:.2f}f}}, {vrad:.2f}f, {vheat:.2f}f, {vsteam:.2f}f, {vphase:.2f}f}},\n")
        f.write("};\n\n")
        f.write(f"#define LY_SON_VENT_COUNT {len(vents)}\n\n")
        f.write("#endif // LY_SON_ROCKS_GENERATED_H\n")

    print(
        f"[LySon] Baked {w}x{h} volcanic terrain, {triangles} magma tris, "
        f"{len(placements)} volcanic rocks, {len(debris_items)} scoria debris, {len(vents)} vents -> {rock_header}",
        flush=True,
    )


if __name__ == "__main__":
    bake_ly_son()
