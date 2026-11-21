#!/usr/bin/env python3
"""
Generate strictly tileable 512x512 PBR metalliferous textures for Phú Quốc (Hệ Kim):
  - assets/maps/phu_quoc/textures/auriferous_quartz_diffuse.png
  - assets/maps/phu_quoc/textures/auriferous_quartz_material.png  (normal RGB + roughness A)
  - assets/maps/phu_quoc/textures/argentite_slate_diffuse.png
  - assets/maps/phu_quoc/textures/argentite_slate_material.png    (normal RGB + roughness A)
  - assets/maps/phu_quoc/textures/mineral_ore_veins.png           (R: gold vein, G: silver lode, B: crystal facet, A: ore grade)

Uses stratified quartzite bedding + quarry chisel striations (NO polygonal Voronoi floor tiles).
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
    periodic_voronoi,
)


def generate_textures(out_dir=None, size=512):
    if out_dir is None:
        out_dir = os.path.join(ROOT_DIR, "assets/maps/phu_quoc/textures")
    os.makedirs(out_dir, exist_ok=True)
    rng = np.random.default_rng(20261124)

    yy, xx = np.mgrid[0:size, 0:size].astype(np.float64) / size
    micro_grain = periodic_fbm(size, [64, 128, 256], [0.45, 0.35, 0.20], rng)
    warp_a = periodic_fbm(size, [4, 8, 16], [0.55, 0.30, 0.15], rng)
    warp_b = periodic_fbm(size, [6, 12, 24], [0.50, 0.32, 0.18], rng)

    # ── 1. Stratified Auriferous Quartzite & Quarry Chisel Bedding (NO Voronoi tiles) ──
    bed_phase = (xx * 5.0 + yy * 2.0) * 2.0 * np.pi + (warp_a - 0.5) * 4.5
    strata_wave = 0.5 + 0.5 * np.sin(bed_phase)
    chisel_fine = 0.5 + 0.5 * np.sin((xx * 18.0 - yy * 6.0) * 2.0 * np.pi + (warp_b - 0.5) * 3.0)

    # Thin conjugate hydrothermal gold hairline veins (not cellular borders)
    vein_field_a = np.abs(np.sin((xx * 3.0 + yy * 2.0) * 2.0 * np.pi + (warp_a - 0.5) * 5.5))
    vein_field_b = np.abs(np.cos((xx * 2.0 - yy * 4.0) * 2.0 * np.pi + (warp_b - 0.5) * 4.8))
    gold_seam = np.clip(1.0 - np.minimum(vein_field_a, vein_field_b) / 0.085, 0.0, 1.0) ** 1.8

    quartz_height = np.clip(
        warp_a * 0.36 + strata_wave * 0.24 + chisel_fine * 0.14 + micro_grain * 0.18 + gold_seam * 0.08,
        0.0, 1.0,
    )
    q_luma = 0.42 + 0.22 * quartz_height + 0.05 * (warp_b - 0.5)
    q_r = np.clip(q_luma * 1.04 + gold_seam * 0.38, 0.18, 0.96)
    q_g = np.clip(q_luma * 1.01 + gold_seam * 0.22, 0.16, 0.88)
    q_b = np.clip(q_luma * 0.95 - gold_seam * 0.16, 0.12, 0.80)
    quartz_diffuse = np.stack((q_r, q_g, q_b), axis=-1)

    quartz_normal = height_to_normal(quartz_height, strength=3.2)
    quartz_rough = np.clip(0.72 - strata_wave * 0.12 - gold_seam * 0.50 + (micro_grain - 0.5) * 0.10, 0.16, 0.90)
    quartz_material = np.concatenate((quartz_normal, quartz_rough[..., None]), axis=-1)

    write_png(os.path.join(out_dir, "auriferous_quartz_diffuse.png"), np.rint(quartz_diffuse * 255.0), 2)
    write_png(os.path.join(out_dir, "auriferous_quartz_material.png"), np.rint(quartz_material * 255.0), 6)

    # ── 2. Dark Gunmetal Iron-Slate & Argentite (Ag) Host Rock ───────────────
    foliation = np.sin((xx * 6.0 + yy * 2.5) * 2.0 * np.pi + (warp_a - 0.5) * 5.2) * 0.5 + 0.5
    silver_wave = np.abs(np.sin((xx * 4.0 - yy * 3.0) * 2.0 * np.pi + (warp_b - 0.5) * 5.0))
    silver_vein = np.clip(1.0 - silver_wave / 0.075, 0.0, 1.0) ** 1.8

    slate_height = np.clip(
        foliation * 0.42 + warp_b * 0.26 + micro_grain * 0.22 + silver_vein * 0.10,
        0.0, 1.0,
    )
    s_luma = 0.22 + 0.16 * slate_height + 0.05 * (warp_a - 0.5)
    s_r = np.clip(s_luma * 0.96 + silver_vein * 0.36, 0.10, 0.86)
    s_g = np.clip(s_luma * 0.99 + silver_vein * 0.38, 0.11, 0.89)
    s_b = np.clip(s_luma * 1.06 + silver_vein * 0.42, 0.12, 0.94)
    slate_diffuse = np.stack((s_r, s_g, s_b), axis=-1)

    slate_normal = height_to_normal(slate_height, strength=3.6)
    slate_rough = np.clip(0.76 - foliation * 0.16 - silver_vein * 0.52 + (micro_grain - 0.5) * 0.08, 0.14, 0.92)
    slate_material = np.concatenate((slate_normal, slate_rough[..., None]), axis=-1)

    write_png(os.path.join(out_dir, "argentite_slate_diffuse.png"), np.rint(slate_diffuse * 255.0), 2)
    write_png(os.path.join(out_dir, "argentite_slate_material.png"), np.rint(slate_material * 255.0), 6)

    # ── 3. Hydrothermal Mineral Ore Vein Data Map (for 3D Ore Crystal Shader) ─
    wx_q = (warp_a - 0.5) * 0.08
    wy_q = (warp_b - 0.5) * 0.08
    f1_v1, f2_v1, facet_id = periodic_voronoi(size, 8, rng, warp_x=wx_q * 1.3, warp_y=wy_q * 1.3)
    f1_v2, f2_v2, _ = periodic_voronoi(size, 12, rng, warp_x=wy_q * 1.1, warp_y=wx_q * 1.1)
    vein_gold = np.clip(1.0 - np.maximum(0.0, f2_v1 - f1_v1) / 0.14, 0.0, 1.0) ** 1.5
    vein_silver = np.clip(1.0 - np.maximum(0.0, f2_v2 - f1_v2) / 0.12, 0.0, 1.0) ** 1.5
    ore_grade = np.clip(np.maximum(vein_gold, vein_silver) * 0.75 + warp_a * 0.25, 0.0, 1.0)

    ore_veins = np.stack((vein_gold, vein_silver, facet_id, ore_grade), axis=-1)
    write_png(os.path.join(out_dir, "mineral_ore_veins.png"), np.rint(ore_veins * 255.0), 6)

    print(f"[PhuQuoc Textures] Generated 5 seamless {size}x{size} metallic PBR textures -> {out_dir}", flush=True)


if __name__ == "__main__":
    generate_textures()
