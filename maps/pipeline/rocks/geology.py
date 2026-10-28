#!/usr/bin/env python3
"""
Stage 2 & 3: Geological & Lithological Inference for Rock Placement.
Computes Hillslope Elements (Ruhe/Pennock model), Regolith Depth, Outcrop Probability,
Regional Joint Strike Field, and Fluvial Clast Grain Size.
Enforces the Wuxing Mộc (Wood) Granitic Pluton + Fluvial Suite.
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


def sigmoid(x):
    return 1.0 / (1.0 + np.exp(-np.clip(x, -15.0, 15.0)))


def compute_geology_fields(fields_npz_path, theme_path, out_dir):
    """
    Infers geological substrate and bedrock exposure from topographic derivatives.
    """
    os.makedirs(out_dir, exist_ok=True)
    debug_dir = os.path.join(out_dir, "debug")
    os.makedirs(debug_dir, exist_ok=True)

    fields = np.load(fields_npz_path)
    ground = fields["ground_y_m"]
    water_depth = fields["water_depth_m"]
    slope = fields["slope_deg"]
    tpi_fine = fields["tpi_fine"]
    tpi_med = fields["tpi_med"]
    tpi_coarse = fields["tpi_coarse"]
    profile_curv = fields["profile_curvature"]
    plan_curv = fields["planform_curvature"]
    vrm = fields["vrm"]
    spi = fields["spi"]
    dist_water = fields["dist_water_m"]
    dist_rim = fields["dist_rim_m"]
    gameplay_mask = fields["gameplay_mask"]
    valid = fields["valid"]
    h, w = ground.shape

    with open(theme_path, "r") as f:
        theme = json.load(f)

    # -------------------------------------------------------------
    # 1. Hillslope Element Classification (Pennock / Ruhe Geomorphology)
    # -------------------------------------------------------------
    # 0: Invalid / Abyss
    # 1: Summit / Crest (high TPI, slope < 12, profile_curv >= 0)
    # 2: Shoulder (convex profile_curv > 0.015, slope 8-22)
    # 3: Backslope (maximum linear slope, 15-35 deg)
    # 4: Footslope / Colluvial base (concave profile_curv < -0.012)
    # 5: Valley Bottom / Plain / Bar (low TPI, low slope, near water)
    # 6: Channel Thalweg (water_depth > 0.02)
    # 7: Rim Ledge (dist_rim < 8.0, ground > -1.0)
    hillslope = np.zeros((h, w), dtype=np.uint8)

    inland_valid = valid & (ground >= -2.0)
    is_water = water_depth > 0.02

    hillslope[inland_valid & (slope <= 12.0) & (tpi_med > 0.45)] = 1 # Summit
    hillslope[inland_valid & (profile_curv > 0.012) & (slope > 8.0) & (slope <= 24.0)] = 2 # Shoulder
    hillslope[inland_valid & (slope > 18.0) & (profile_curv >= -0.015) & (profile_curv <= 0.02)] = 3 # Backslope
    hillslope[inland_valid & (profile_curv < -0.015) & (slope <= 20.0)] = 4 # Footslope
    hillslope[inland_valid & (slope < 8.0) & (tpi_med <= 0.2) & (dist_water <= 16.0)] = 5 # Plain / Bar
    hillslope[inland_valid & (dist_rim < 8.0) & (dist_rim > 1.2) & (ground > 0.0)] = 7 # Rim Ledge
    hillslope[is_water & inland_valid] = 6 # Channel

    # -------------------------------------------------------------
    # 2. Regolith Thickness Model & Outcrop Probability (P_out)
    # -------------------------------------------------------------
    # Soil is thin on convex ridges, steep slopes, and high VRM.
    # Soil accumulates in hollows and valley bottoms.
    z0 = 1.40 # nominal regolith depth in meters
    slope_factor = np.clip(1.0 - (slope / 38.0), 0.05, 1.0)
    convexity = np.clip(profile_curv * 18.0 + plan_curv * 12.0, -1.0, 2.5)
    curvature_factor = np.exp(-0.75 * np.maximum(0.0, convexity))

    regolith_depth_m = z0 * slope_factor * curvature_factor
    regolith_depth_m = np.maximum(0.05, regolith_depth_m - vrm * 0.9)
    regolith_depth_m[~valid] = 0.0

    # Bedrock Outcrop Potential P_out in [0, 1]
    # High score = granite corestone tor emergence
    score_outcrop = (
        0.55 * (0.85 - regolith_depth_m)
        + 0.35 * tpi_med
        + 0.40 * (vrm - 0.15)
        + 0.15 * (profile_curv * 20.0)
    )
    # Suppress in water thalweg and deep valley bottoms
    score_outcrop[is_water] -= 1.8
    score_outcrop[dist_water < 2.0] -= 0.8
    # Suppress in sheer abyss
    score_outcrop[ground < 0.5] -= 2.0
    # Modulate by gameplay clear mask
    score_outcrop -= gameplay_mask * 2.5

    p_outcrop = sigmoid(score_outcrop * 3.2)
    p_outcrop[~valid] = 0.0

    # -------------------------------------------------------------
    # 3. Geological Joint Strike Vector Field (Coherent Grain)
    # -------------------------------------------------------------
    # Base tectonic strike from theme (e.g. 32 degrees NNE)
    base_strike_deg = theme.get("joint_strike_deg", 32.0)
    base_strike_rad = np.radians(base_strike_deg)

    # Slight organic refraction around major topographic ridges (+/- 8 deg)
    ridge_refract = np.clip(tpi_med * 0.12 - slope * 0.003, -0.15, 0.15)
    local_strike_rad = base_strike_rad + ridge_refract

    joint_dir_x = np.cos(local_strike_rad).astype(np.float32)
    joint_dir_z = np.sin(local_strike_rad).astype(np.float32)

    # -------------------------------------------------------------
    # 4. Fluvial Clast Grain Size & Deposition Zoning
    # -------------------------------------------------------------
    # In channels and riparian margins: grain size d50 follows stream power SPI
    # High SPI on steep cascade rapids = large anchor boulders (0.8m - 1.6m)
    # Low SPI on wide shallows / inner bends = rounded cobbles and gravels (0.15m - 0.45m)
    fluvial_d50_m = np.zeros((h, w), dtype=np.float32)
    riparian_band = (dist_water <= 3.5) | is_water
    fluvial_d50_m[riparian_band] = np.clip(0.18 + 0.32 * (spi[riparian_band] - 1.2), 0.12, 1.45)

    # Save geology fields
    out_fields = {
        "hillslope": hillslope,
        "regolith_depth_m": regolith_depth_m.astype(np.float32),
        "p_outcrop": p_outcrop.astype(np.float32),
        "joint_dir_x": joint_dir_x,
        "joint_dir_z": joint_dir_z,
        "fluvial_d50_m": fluvial_d50_m,
    }

    out_npz_path = os.path.join(out_dir, "rock_geology_fields.npz")
    np.savez_compressed(out_npz_path, **out_fields)
    print(f"[Stage 2 & 3] Geology fields saved to: {out_npz_path}")

    # Visual debug outputs
    def norm_png(arr, vmin, vmax):
        norm = np.clip((arr - vmin) / max(1e-5, vmax - vmin), 0.0, 1.0)
        norm[~valid] = 0.0
        return (norm * 255.0).astype(np.uint8)

    # Palette for hillslope elements:
    # 0: black, 1: yellow (summit), 2: orange (shoulder), 3: red-brown (backslope),
    # 4: cyan (footslope), 5: green (plain), 6: blue (channel), 7: purple (rim)
    hillslope_palette = np.array([
        [0, 0, 0],
        [245, 230, 90],
        [240, 160, 50],
        [180, 80, 45],
        [70, 200, 210],
        [100, 190, 70],
        [40, 90, 240],
        [190, 90, 230],
    ], dtype=np.uint8)
    hillslope_rgb = hillslope_palette[np.clip(hillslope, 0, 7)]
    hillslope_rgb[~valid] = 0
    write_png(os.path.join(debug_dir, "08_hillslope_elements.png"), hillslope_rgb, color_type=2)

    write_png(os.path.join(debug_dir, "09_regolith_depth.png"), norm_png(regolith_depth_m, 0.0, 1.5), color_type=0)
    write_png(os.path.join(debug_dir, "10_outcrop_probability.png"), norm_png(p_outcrop, 0.0, 0.85), color_type=0)
    write_png(os.path.join(debug_dir, "11_fluvial_grain_size.png"), norm_png(fluvial_d50_m, 0.0, 1.2), color_type=0)

    print(f"[Stage 2 & 3] Geological debug images written to: {debug_dir}/")
    return out_fields


if __name__ == "__main__":
    fields_file = "assets/maps/song_quao/rock_pipeline/rock_terrain_fields.npz"
    theme_file = os.path.join(SCRIPT_DIR, "themes", "song_quao.json")
    out_directory = "assets/maps/song_quao/rock_pipeline"
    compute_geology_fields(fields_file, theme_file, out_directory)
