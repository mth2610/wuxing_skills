#!/usr/bin/env python3
"""
Generate strictly tileable 512x512 PBR alpine snow, permafrost tundra & glacial ice
textures for Mẫu Sơn (Hệ Thủy — Hàn Đàm Tuyết Lĩnh):
  - assets/maps/mau_son/textures/alpine_snow_diffuse.png
  - assets/maps/mau_son/textures/alpine_snow_material.png     (normal RGB + roughness A)
  - assets/maps/mau_son/textures/frozen_tundra_diffuse.png
  - assets/maps/mau_son/textures/frozen_tundra_material.png   (normal RGB + roughness A)
  - assets/maps/mau_son/textures/glacial_ice_plates.png       (R: floe dome, G: fracture net, B: bubbles FBM, A: snow crust)

Uses pure NumPy + toroidal periodic synthesis (zero edge seams, zero PIL dependency).
"""

import os
import sys
import numpy as np

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(os.path.dirname(SCRIPT_DIR))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from dem_pipeline import write_png
from generate_volcanic_textures import (
    height_to_normal,
    periodic_fbm,
    periodic_value_noise,
    periodic_voronoi,
)


def generate_textures(out_dir=None, size=512):
    if out_dir is None:
        out_dir = os.path.join(ROOT_DIR, "assets/maps/mau_son/textures")
    os.makedirs(out_dir, exist_ok=True)
    rng = np.random.default_rng(20261122)

    yy, xx = np.mgrid[0:size, 0:size].astype(np.float64) / size
    micro_grain = periodic_fbm(size, [64, 128, 256], [0.45, 0.35, 0.20], rng)

    # ── 1. Wind-Packed Alpine Snowpack (Sastrugi + Crystalline Grain) ─────────
    macro_drift = periodic_fbm(size, [4, 8, 16, 32], [0.46, 0.28, 0.16, 0.10], rng)
    wind_warp = periodic_fbm(size, [4, 8, 16], [0.55, 0.30, 0.15], rng)
    sastrugi_phase = (xx * 5.0 + yy * 2.5) * 2.0 * np.pi + (wind_warp - 0.5) * 4.8
    sastrugi_wave = np.power(0.5 + 0.5 * np.sin(sastrugi_phase), 0.78)

    snow_height = np.clip(
        macro_drift * 0.44 + sastrugi_wave * 0.38 + micro_grain * 0.18,
        0.0, 1.0,
    )
    # High-albedo pure crisp alpine snow (zero mint/green bias; clean ivory crests, subtle cool troughs)
    snow_luma = 0.82 + 0.15 * snow_height
    snow_r = np.clip(snow_luma * (0.985 + 0.020 * sastrugi_wave), 0.78, 0.98)
    snow_g = np.clip(snow_luma * (0.990 + 0.012 * sastrugi_wave), 0.79, 0.98)
    snow_b = np.clip(snow_luma * 1.015, 0.82, 0.995)
    snow_diffuse = np.stack((snow_r, snow_g, snow_b), axis=-1)

    snow_normal = height_to_normal(snow_height, strength=3.6)
    snow_rough = np.clip(0.68 - sastrugi_wave * 0.28 - micro_grain * 0.12, 0.28, 0.72)
    snow_material = np.concatenate((snow_normal, snow_rough[..., None]), axis=-1)

    write_png(os.path.join(out_dir, "alpine_snow_diffuse.png"), np.rint(snow_diffuse * 255.0), 2)
    write_png(os.path.join(out_dir, "alpine_snow_material.png"), np.rint(snow_material * 255.0), 6)

    # ── 2. Frost-Shattered Slate Scree & Permafrost Tundra ────────────────────
    tundra_fbm = periodic_fbm(size, [4, 8, 16, 32], [0.40, 0.30, 0.20, 0.10], rng)
    f1_slate, f2_slate, slate_id = periodic_voronoi(
        size, 32, rng,
        warp_x=(tundra_fbm - 0.5) * 0.035,
        warp_y=(periodic_value_noise(size, 10, rng) - 0.5) * 0.035,
    )
    slate_chips = np.clip(1.0 - f1_slate / 0.58, 0.0, 1.0) ** 1.35
    hoarfrost_veins = np.clip(1.0 - (f2_slate - f1_slate) / 0.12, 0.0, 1.0) * np.clip(tundra_fbm * 1.4 - 0.3, 0.0, 1.0)

    tundra_height = np.clip(
        tundra_fbm * 0.36 + slate_chips * 0.44 + micro_grain * 0.20,
        0.0, 1.0,
    )
    slate_luma = 0.20 + 0.18 * tundra_height + 0.08 * (slate_id - 0.5) * slate_chips
    tun_r = np.clip(slate_luma * 0.90 + hoarfrost_veins * 0.32, 0.12, 0.78)
    tun_g = np.clip(slate_luma * 0.96 + hoarfrost_veins * 0.36, 0.14, 0.82)
    tun_b = np.clip(slate_luma * 1.08 + hoarfrost_veins * 0.42, 0.16, 0.88)
    tundra_diffuse = np.stack((tun_r, tun_g, tun_b), axis=-1)

    tundra_normal = height_to_normal(tundra_height, strength=3.8)
    # Wet/icy frost crevices are smoother (0.24), dry slate scree is matte (0.86)
    tundra_rough = np.clip(0.84 - hoarfrost_veins * 0.56 - micro_grain * 0.08, 0.22, 0.92)
    tundra_material = np.concatenate((tundra_normal, tundra_rough[..., None]), axis=-1)

    write_png(os.path.join(out_dir, "frozen_tundra_diffuse.png"), np.rint(tundra_diffuse * 255.0), 2)
    write_png(os.path.join(out_dir, "frozen_tundra_material.png"), np.rint(tundra_material * 255.0), 6)

    # ── 3. Multi-Scale Fractured Glacial Ice Sheet & Internal Cleavage Network ──
    # Broad interlocking pressure-cracked shelves (no isolated round cobblestone stepping stones!)
    wx = (periodic_fbm(size, [4, 8, 16], [0.55, 0.30, 0.15], rng) - 0.5) * 0.16
    wy = (periodic_fbm(size, [4, 8, 16], [0.55, 0.30, 0.15], rng) - 0.5) * 0.16
    f1_sheet, f2_sheet, sheet_id = periodic_voronoi(size, 3, rng, warp_x=wx, warp_y=wy)
    f1_crack, f2_crack, _ = periodic_voronoi(size, 9, rng, warp_x=wx * 0.75, warp_y=wy * 0.75)
    f1_hair, f2_hair, _ = periodic_voronoi(size, 24, rng, warp_x=wx * 0.45, warp_y=wy * 0.45)

    edge_sheet = np.maximum(0.0, f2_sheet - f1_sheet)
    edge_crack = np.maximum(0.0, f2_crack - f1_crack)
    edge_hair = np.maximum(0.0, f2_hair - f1_hair)

    # R: Broad continuous ice shelf thickness (narrow meltwater leads only along major seams)
    lead_cut = np.clip(edge_sheet / 0.11, 0.0, 1.0)
    lead_cut = lead_cut * lead_cut * (3.0 - 2.0 * lead_cut)
    shelf_fbm = periodic_fbm(size, [4, 8, 16], [0.50, 0.32, 0.18], rng)
    floe_height = np.clip(
        lead_cut * (0.74 + 0.18 * sheet_id + 0.12 * shelf_fbm)
        - 0.06 * (1.0 - np.clip(edge_crack / 0.06, 0.0, 1.0)),
        0.0, 1.0,
    )

    # G: Crisp multi-scale internal glacial fracture network (pressure ridges + spiderweb hairline fissures)
    c_pri = np.clip(1.0 - edge_sheet / 0.075, 0.0, 1.0) ** 1.4
    c_sec = np.clip(1.0 - edge_crack / 0.048, 0.0, 1.0) ** 1.6 * 0.78
    c_hair = np.clip(1.0 - edge_hair / 0.032, 0.0, 1.0) ** 1.8 * 0.45
    fracture_net = np.clip(np.maximum(np.maximum(c_pri, c_sec), c_hair), 0.0, 1.0)

    # B: Deep trapped air bubbles & milky glacial crystal turbidity clouds
    bubble_fbm = periodic_fbm(size, [8, 16, 32, 64], [0.35, 0.30, 0.22, 0.13], rng)

    # A: Wind-drifted snow patches & frosted pressure-ridge rime hugging cracks and shelf interiors
    drift_mask = periodic_fbm(size, [4, 8, 16], [0.55, 0.30, 0.15], rng)
    snow_crust = np.clip(
        lead_cut * np.clip((drift_mask - 0.32) / 0.48, 0.0, 1.0) * 0.85
        + c_pri * 0.55,
        0.0, 1.0,
    )

    ice_plates = np.stack((floe_height, fracture_net, bubble_fbm, snow_crust), axis=-1)
    write_png(os.path.join(out_dir, "glacial_ice_plates.png"), np.rint(ice_plates * 255.0), 6)

    print(f"[MauSon Textures] Generated 5 seamless {size}x{size} alpine/ice PBR textures -> {out_dir}", flush=True)


if __name__ == "__main__":
    generate_textures()
