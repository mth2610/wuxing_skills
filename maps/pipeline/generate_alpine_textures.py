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
    # High-albedo crisp snow with subtle sky-blue ambient bounce in wind-scoured troughs
    snow_luma = 0.78 + 0.16 * snow_height
    snow_r = np.clip(snow_luma * (0.96 + 0.03 * sastrugi_wave), 0.72, 0.96)
    snow_g = np.clip(snow_luma * (0.98 + 0.02 * sastrugi_wave), 0.75, 0.97)
    snow_b = np.clip(snow_luma * 1.03, 0.79, 0.99)
    snow_diffuse = np.stack((snow_r, snow_g, snow_b), axis=-1)

    snow_normal = height_to_normal(snow_height, strength=2.6)
    snow_rough = np.clip(0.62 - sastrugi_wave * 0.18 - micro_grain * 0.08, 0.34, 0.76)
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

    # ── 3. Glacial Ice Floe Plates + Internal Fracture Network ────────────────
    wx = (periodic_fbm(size, [4, 8, 16], [0.55, 0.30, 0.15], rng) - 0.5) * 0.12
    wy = (periodic_fbm(size, [4, 8, 16], [0.55, 0.30, 0.15], rng) - 0.5) * 0.12
    f1_floe, f2_floe, floe_id = periodic_voronoi(size, 5, rng, warp_x=wx, warp_y=wy)
    f1_crack, f2_crack, _ = periodic_voronoi(size, 15, rng, warp_x=wx * 0.65, warp_y=wy * 0.65)

    edge_dist = np.maximum(0.0, f2_floe - f1_floe)
    sub_edge = np.maximum(0.0, f2_crack - f1_crack)

    # R: Ice floe plate dome height (0 at lead edge, 1 at thick plate core)
    dome = np.clip(edge_dist / 0.28, 0.0, 1.0)
    dome = dome * dome * (3.0 - 2.0 * dome)
    floe_height = np.clip(dome * (0.76 + 0.24 * floe_id) - 0.08 * (1.0 - np.clip(sub_edge / 0.12, 0.0, 1.0)), 0.0, 1.0)

    # G: Crisp internal glacial fracture network (primary floe pressure cracks + fine radial fractures)
    c_pri = np.clip(1.0 - edge_dist / 0.14, 0.0, 1.0)
    c_pri = c_pri * c_pri * (3.0 - 2.0 * c_pri)
    c_sec = np.clip(1.0 - sub_edge / 0.075, 0.0, 1.0) * 0.58
    fracture_net = np.clip(np.maximum(c_pri, c_sec), 0.0, 1.0)

    # B: Deep trapped air bubbles & sub-surface glacial crystal turbidity
    bubble_fbm = periodic_fbm(size, [8, 16, 32, 64], [0.35, 0.30, 0.22, 0.13], rng)

    # A: Wind-blown snow crust & hoarfrost rime resting on thick floe interiors
    snow_crust = np.clip((dome - 0.38) / 0.52, 0.0, 1.0) * np.clip(bubble_fbm * 1.25 - 0.15, 0.0, 1.0)

    ice_plates = np.stack((floe_height, fracture_net, bubble_fbm, snow_crust), axis=-1)
    write_png(os.path.join(out_dir, "glacial_ice_plates.png"), np.rint(ice_plates * 255.0), 6)

    print(f"[MauSon Textures] Generated 5 seamless {size}x{size} alpine/ice PBR textures -> {out_dir}", flush=True)


if __name__ == "__main__":
    generate_textures()
