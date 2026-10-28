#!/usr/bin/env python3
"""
Stage 1: Terrain Derivatives & Topographic Metric Extraction for Rock Placement.
Computes multi-scale TPI, profile/planform curvatures, Stream Power Index (SPI),
Vector Ruggedness Measure (VRM), fall-line vector fields, and boundary distance maps.
Zero heavy dependencies: pure NumPy + Python.
"""

import os
import sys
import json
import numpy as np

# Add parent directory for dem_pipeline helpers
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PIPELINE_DIR = os.path.dirname(SCRIPT_DIR)
if PIPELINE_DIR not in sys.path:
    sys.path.insert(0, PIPELINE_DIR)

from dem_pipeline import write_png, gaussian_filter_2d


def compute_distance_field(mask, cell_size_x, cell_size_z):
    """
    Computes Euclidean distance in meters to the nearest True pixel in mask.
    Using two-pass 8-neighbor raster Distance Transform.
    """
    h, w = mask.shape
    INF = 1e8
    dist = np.full((h, w), INF, dtype=np.float32)
    dist[mask] = 0.0

    diag_dist = np.hypot(cell_size_x, cell_size_z)

    # Forward pass: top-left to bottom-right
    for r in range(h):
        for c in range(w):
            d0 = dist[r, c]
            if r > 0:
                d0 = min(d0, dist[r - 1, c] + cell_size_z)
            if c > 0:
                d0 = min(d0, dist[r, c - 1] + cell_size_x)
            if r > 0 and c > 0:
                d0 = min(d0, dist[r - 1, c - 1] + diag_dist)
            if r > 0 and c < w - 1:
                d0 = min(d0, dist[r - 1, c + 1] + diag_dist)
            dist[r, c] = d0

    # Backward pass: bottom-right to top-left
    for r in range(h - 1, -1, -1):
        for c in range(w - 1, -1, -1):
            d0 = dist[r, c]
            if r < h - 1:
                d0 = min(d0, dist[r + 1, c] + cell_size_z)
            if c < w - 1:
                d0 = min(d0, dist[r, c + 1] + cell_size_x)
            if r < h - 1 and c < w - 1:
                d0 = min(d0, dist[r + 1, c + 1] + diag_dist)
            if r < h - 1 and c > 0:
                d0 = min(d0, dist[r + 1, c - 1] + diag_dist)
            dist[r, c] = d0

    return dist


def compute_terrain_fields(analysis_path, theme_path, out_dir):
    """
    Loads analysis data and theme, computes geomorphological derivative fields.
    """
    os.makedirs(out_dir, exist_ok=True)
    debug_dir = os.path.join(out_dir, "debug")
    os.makedirs(debug_dir, exist_ok=True)

    data = np.load(analysis_path)
    ground = data["ground_y_m"].astype(np.float32)
    water_depth = data["water_depth_m"].astype(np.float32)
    slope_deg = data["slope_deg"].astype(np.float32)
    aspect_deg = data["aspect_deg"].astype(np.float32)
    valid = data["valid"].astype(bool)
    accum = data["accumulation_cells"].astype(np.float32)
    h, w = ground.shape

    # Cell metric scale in world meters
    with open(theme_path, "r") as f:
        theme = json.load(f)

    # Standard Song Quao dimensions
    map_w = 188.2149
    map_d = 300.0
    sx = map_w / (w - 1)
    sz = map_d / (h - 1)

    # 1. Multi-scale Topographic Position Index (TPI)
    # TPI = ground - gaussian_filtered(ground)
    # Fine (clump scale ~ 4m), Medium (knoll scale ~ 16m), Coarse (massif scale ~ 48m)
    sigma_fine = max(1.2, 4.0 / max(sx, sz))
    sigma_med = max(2.5, 16.0 / max(sx, sz))
    sigma_coarse = max(5.0, 48.0 / max(sx, sz))

    ground_fine = gaussian_filter_2d(ground, sigma=sigma_fine)
    ground_med = gaussian_filter_2d(ground, sigma=sigma_med)
    ground_coarse = gaussian_filter_2d(ground, sigma=sigma_coarse)

    tpi_fine = ground - ground_fine
    tpi_med = ground - ground_med
    tpi_coarse = ground - ground_coarse

    # 2. Curvatures via finite differences (Zevenbergen & Thorne formulation)
    dz_dy, dz_dx = np.gradient(ground, sz, sx)
    d2z_dy2 = np.gradient(dz_dy, sz, axis=0)
    d2z_dx2 = np.gradient(dz_dx, sx, axis=1)
    d2z_dxdy = np.gradient(dz_dx, sz, axis=0)

    p = dz_dx
    q = dz_dy
    p2 = p * p
    q2 = q * q
    pq = p * q
    denom_prof = ((p2 + q2) * np.sqrt((1.0 + p2 + q2) ** 3))
    denom_prof = np.maximum(denom_prof, 1e-6)
    # Profile curvature: rate of change of slope along the gradient direction (convex > 0 shoulder, concave < 0 hollow)
    profile_curvature = -(p2 * d2z_dx2 + 2.0 * pq * d2z_dxdy + q2 * d2z_dy2) / denom_prof

    denom_plan = ((p2 + q2) ** 1.5)
    denom_plan = np.maximum(denom_plan, 1e-6)
    # Planform curvature: rate of change of aspect along contour (divergent > 0 ridge, convergent < 0 ravine)
    planform_curvature = -(q2 * d2z_dx2 - 2.0 * pq * d2z_dxdy + p2 * d2z_dy2) / denom_plan

    # 3. Fall-line downhill unit vector field (-grad ground)
    grad_norm = np.sqrt(p2 + q2)
    grad_norm_safe = np.maximum(grad_norm, 1e-6)
    fall_line_x = -p / grad_norm_safe
    fall_line_z = -q / grad_norm_safe
    # Flat areas default to downward Z flow
    flat_mask = grad_norm < 0.015
    fall_line_x[flat_mask] = 0.0
    fall_line_z[flat_mask] = 1.0

    # 4. Stream Power Index (SPI = A * tan(beta))
    slope_rad = np.radians(slope_deg)
    tan_slope = np.tan(slope_rad)
    cell_area = sx * sz
    contrib_area_m2 = accum * cell_area
    spi = np.log10(np.maximum(1.0, contrib_area_m2 * tan_slope))

    # 5. Vector Ruggedness Measure (VRM) in 3x3 window
    # Normal vectors: N = (-p, 1, -q) / sqrt(p^2 + 1 + q^2)
    n_len = np.sqrt(p2 + 1.0 + q2)
    nx = -p / n_len
    ny = 1.0 / n_len
    nz = -q / n_len

    # Local 3x3 sum
    nx_sum = gaussian_filter_2d(nx, sigma=1.0) * 9.0
    ny_sum = gaussian_filter_2d(ny, sigma=1.0) * 9.0
    nz_sum = gaussian_filter_2d(nz, sigma=1.0) * 9.0
    r_len = np.sqrt(nx_sum**2 + ny_sum**2 + nz_sum**2)
    vrm = 1.0 - (r_len / 9.0)
    vrm = np.clip(vrm * 10.0, 0.0, 1.0) # Normalized ruggedness metric

    # 6. Distance Transforms: Water Distance & Island Rim Distance
    water_mask = (water_depth > 0.01)
    dist_water_m = compute_distance_field(water_mask, sx, sz)

    invalid_mask = (~valid) | (ground < -2.40)
    dist_rim_m = compute_distance_field(invalid_mask, sx, sz)

    # 7. Gameplay Clearing Mask
    gp_center = theme.get("gameplay_center", [94.1, 150.0])
    gp_radius = theme.get("gameplay_clear_radius_m", 14.0)
    yy, xx = np.indices((h, w))
    world_x = xx * sx
    world_z = yy * sz
    dist_to_gp = np.hypot(world_x - gp_center[0], world_z - gp_center[1])
    gameplay_mask = np.clip((gp_radius - dist_to_gp) / 4.0 + 0.5, 0.0, 1.0)

    # Package output dictionary
    out_fields = {
        "tpi_fine": tpi_fine.astype(np.float32),
        "tpi_med": tpi_med.astype(np.float32),
        "tpi_coarse": tpi_coarse.astype(np.float32),
        "profile_curvature": profile_curvature.astype(np.float32),
        "planform_curvature": planform_curvature.astype(np.float32),
        "fall_line_x": fall_line_x.astype(np.float32),
        "fall_line_z": fall_line_z.astype(np.float32),
        "spi": spi.astype(np.float32),
        "vrm": vrm.astype(np.float32),
        "dist_water_m": dist_water_m.astype(np.float32),
        "dist_rim_m": dist_rim_m.astype(np.float32),
        "gameplay_mask": gameplay_mask.astype(np.float32),
        "valid": valid,
        "ground_y_m": ground,
        "water_depth_m": water_depth,
        "slope_deg": slope_deg,
        "aspect_deg": aspect_deg,
    }

    out_npz_path = os.path.join(out_dir, "rock_terrain_fields.npz")
    np.savez_compressed(out_npz_path, **out_fields)
    print(f"[Stage 1] Topographic fields saved to: {out_npz_path}")

    # Write debug PNG visualizations
    def norm_png(arr, vmin, vmax):
        norm = np.clip((arr - vmin) / max(1e-5, vmax - vmin), 0.0, 1.0)
        norm[~valid] = 0.0
        return (norm * 255.0).astype(np.uint8)

    write_png(os.path.join(debug_dir, "01_tpi_med.png"), norm_png(tpi_med, -1.2, 1.8), color_type=0)
    write_png(os.path.join(debug_dir, "02_profile_curv.png"), norm_png(profile_curvature, -0.05, 0.05), color_type=0)
    write_png(os.path.join(debug_dir, "03_vrm_ruggedness.png"), norm_png(vrm, 0.0, 0.5), color_type=0)
    write_png(os.path.join(debug_dir, "04_spi_stream_power.png"), norm_png(spi, 0.0, 4.5), color_type=0)
    write_png(os.path.join(debug_dir, "05_dist_water.png"), norm_png(dist_water_m, 0.0, 30.0), color_type=0)
    write_png(os.path.join(debug_dir, "06_dist_rim.png"), norm_png(dist_rim_m, 0.0, 25.0), color_type=0)

    # Combined RGB inspection map: R=VRM, G=TPI_med, B=dist_water
    rgb_composite = np.zeros((h, w, 3), dtype=np.uint8)
    rgb_composite[..., 0] = norm_png(vrm, 0.0, 0.45)
    rgb_composite[..., 1] = norm_png(tpi_med, -0.5, 1.5)
    rgb_composite[..., 2] = norm_png(np.clip(20.0 - dist_water_m, 0.0, 20.0), 0.0, 20.0)
    write_png(os.path.join(debug_dir, "07_composite_geomorph.png"), rgb_composite, color_type=2)
    print(f"[Stage 1] Visual debug images written to: {debug_dir}/")

    return out_fields


if __name__ == "__main__":
    analysis_file = "assets/maps/song_quao/song_quao_analysis.npz"
    theme_file = os.path.join(SCRIPT_DIR, "themes", "song_quao.json")
    out_directory = "assets/maps/song_quao/rock_pipeline"
    compute_terrain_fields(analysis_file, theme_file, out_directory)
