#!/usr/bin/env python3
"""
Generate strictly tileable 512x512 PBR volcanic textures for Lý Sơn (Hệ Hỏa):
  - assets/maps/ly_son/textures/volcanic_ash_diffuse.png
  - assets/maps/ly_son/textures/volcanic_ash_material.png     (normal RGB + roughness A)
  - assets/maps/ly_son/textures/pahoehoe_crust_diffuse.png
  - assets/maps/ly_son/textures/pahoehoe_crust_material.png  (normal RGB + roughness A)
  - assets/maps/ly_son/textures/lava_voronoi_plates.png      (R: plate dome, G: fissure, B: FBM, A: slag)

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


def periodic_value_noise(n, freq, rng):
    """Bilinearly interpolated toroidal periodic noise on an n x n grid."""
    grid = rng.random((freq, freq), dtype=np.float64)
    coords = (np.arange(n, dtype=np.float64) / n) * freq
    i0 = np.floor(coords).astype(np.int32) % freq
    i1 = (i0 + 1) % freq
    f = coords - np.floor(coords)
    s = f * f * (3.0 - 2.0 * f)

    g00 = grid[np.ix_(i0, i0)]
    g10 = grid[np.ix_(i0, i1)]
    g01 = grid[np.ix_(i1, i0)]
    g11 = grid[np.ix_(i1, i1)]

    sx = s[None, :]
    sy = s[:, None]
    nx0 = g00 * (1.0 - sx) + g10 * sx
    nx1 = g01 * (1.0 - sx) + g11 * sx
    return nx0 * (1.0 - sy) + nx1 * sy


def periodic_fbm(n, freqs, weights, rng):
    out = np.zeros((n, n), dtype=np.float64)
    wsum = 0.0
    for f, w in zip(freqs, weights):
        out += periodic_value_noise(n, f, rng) * w
        wsum += w
    return out / max(wsum, 1e-6)


def periodic_voronoi(n, cells, rng, warp_x=None, warp_y=None):
    """
    Compute periodic Worley/Voronoi F1 and F2 distances on [0, 1)^2.
    Returns (f1, f2, cell_id_hash) normalized to cell size.
    """
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float64) / n
    if warp_x is not None:
        xx = (xx + warp_x) % 1.0
    if warp_y is not None:
        yy = (yy + warp_y) % 1.0

    px = xx * cells
    py = yy * cells
    ix = np.floor(px).astype(np.int32)
    iy = np.floor(py).astype(np.int32)

    pts_x = rng.uniform(0.14, 0.86, size=(cells, cells))
    pts_y = rng.uniform(0.14, 0.86, size=(cells, cells))
    cell_val = rng.random((cells, cells))

    f1 = np.full((n, n), 1e9, dtype=np.float64)
    f2 = np.full((n, n), 1e9, dtype=np.float64)
    cid = np.zeros((n, n), dtype=np.float64)

    for dy in (-1, 0, 1):
        ny = (iy + dy) % cells
        for dx in (-1, 0, 1):
            nx = (ix + dx) % cells
            cx = ix + dx + pts_x[ny, nx]
            cy = iy + dy + pts_y[ny, nx]
            dist = np.hypot(px - cx, py - cy)

            mask1 = dist < f1
            f2 = np.where(mask1, f1, np.minimum(f2, dist))
            f1 = np.where(mask1, dist, f1)
            cid = np.where(mask1, cell_val[ny, nx], cid)

    return f1, f2, cid


def height_to_normal(h, strength=2.2):
    """Periodic central-difference tangent-space normal map in [0, 1] RGB."""
    dx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * 0.5 * strength
    dy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * 0.5 * strength
    nx = -dx
    ny = -dy
    nz = np.ones_like(h)
    inv_len = 1.0 / np.sqrt(nx * nx + ny * ny + nz * nz + 1e-8)
    nx = nx * inv_len * 0.5 + 0.5
    ny = ny * inv_len * 0.5 + 0.5
    nz = nz * inv_len * 0.5 + 0.5
    return np.stack((nx, ny, nz), axis=-1)


def generate_textures(out_dir=None, size=512):
    if out_dir is None:
        out_dir = os.path.join(ROOT_DIR, "assets/maps/ly_son/textures")
    os.makedirs(out_dir, exist_ok=True)
    rng = np.random.default_rng(20261121)

    yy, xx = np.mgrid[0:size, 0:size].astype(np.float64) / size

    # ── 1. Volcanic Tephra Ash & Angular Scoria Grit ─────────────────────────
    macro_ash = periodic_fbm(size, [4, 8, 16, 32], [0.42, 0.30, 0.18, 0.10], rng)
    wind_u = (xx * 2.0 + yy * 1.0) * 2.0 * np.pi
    streak = 0.5 + 0.5 * np.sin(wind_u * 3.0 + macro_ash * 4.5)
    f1_grit, f2_grit, grit_id = periodic_voronoi(
        size, 42, rng,
        warp_x=(macro_ash - 0.5) * 0.025,
        warp_y=(periodic_value_noise(size, 12, rng) - 0.5) * 0.025,
    )
    scoria_pebbles = np.clip(1.0 - f1_grit / 0.55, 0.0, 1.0) ** 1.4
    micro_grain = periodic_fbm(size, [64, 128, 256], [0.45, 0.35, 0.20], rng)

    ash_height = np.clip(
        macro_ash * 0.38 + streak * 0.12 + scoria_pebbles * 0.32 + micro_grain * 0.18,
        0.0, 1.0,
    )
    # Dark charcoal volcanic tephra with warm oxidized scoria chips
    base_luma = 0.16 + 0.18 * ash_height + 0.06 * (grit_id - 0.5) * scoria_pebbles
    ash_r = np.clip(base_luma * (1.04 + 0.10 * scoria_pebbles), 0.06, 0.48)
    ash_g = np.clip(base_luma * 0.96, 0.06, 0.44)
    ash_b = np.clip(base_luma * (0.94 - 0.04 * scoria_pebbles), 0.06, 0.43)
    ash_diffuse = np.stack((ash_r, ash_g, ash_b), axis=-1)

    ash_normal = height_to_normal(ash_height, strength=3.4)
    ash_rough = np.clip(0.88 - scoria_pebbles * 0.16 - micro_grain * 0.06, 0.68, 0.95)
    ash_material = np.concatenate((ash_normal, ash_rough[..., None]), axis=-1)

    write_png(os.path.join(out_dir, "volcanic_ash_diffuse.png"), np.rint(ash_diffuse * 255.0), 2)
    write_png(os.path.join(out_dir, "volcanic_ash_material.png"), np.rint(ash_material * 255.0), 6)

    # ── 2. Ropy Pahoehoe Cooled Basalt Crust ─────────────────────────────────
    warp_a = periodic_fbm(size, [4, 8, 16], [0.55, 0.30, 0.15], rng)
    warp_b = periodic_fbm(size, [6, 12, 24], [0.50, 0.32, 0.18], rng)
    rope_phase = (xx * 7.0 + yy * 3.0) * 2.0 * np.pi + (warp_a - 0.5) * 6.8 + (warp_b - 0.5) * 2.6
    # Asymmetric ropy folds (sharp convex crests, tight ash-filled troughs)
    rope_wave = 0.5 + 0.5 * np.sin(rope_phase)
    rope_folds = np.power(rope_wave, 0.68)
    f1_ves, _, _ = periodic_voronoi(size, 56, rng)
    vesicle_pits = np.clip((0.28 - f1_ves) / 0.28, 0.0, 1.0) * 0.16

    pahoehoe_height = np.clip(rope_folds * 0.66 + warp_a * 0.22 + micro_grain * 0.12 - vesicle_pits, 0.0, 1.0)
    crest_mask = np.clip((rope_folds - 0.35) / 0.65, 0.0, 1.0)
    trough_ash = np.clip(1.0 - rope_folds * 1.45, 0.0, 1.0)

    pah_luma = 0.12 + 0.17 * pahoehoe_height + 0.05 * crest_mask
    pah_r = np.clip(pah_luma * (0.98 + 0.06 * trough_ash), 0.06, 0.42)
    pah_g = np.clip(pah_luma * 0.97, 0.06, 0.40)
    pah_b = np.clip(pah_luma * (1.03 - 0.05 * trough_ash), 0.06, 0.43)
    pah_diffuse = np.stack((pah_r, pah_g, pah_b), axis=-1)

    pah_normal = height_to_normal(pahoehoe_height, strength=4.2)
    # Glossy vitreous basalt on crests (0.34), matte ash in crevices (0.90)
    pah_rough = np.clip(0.34 + trough_ash * 0.54 + vesicle_pits * 0.45, 0.30, 0.94)
    pah_material = np.concatenate((pah_normal, pah_rough[..., None]), axis=-1)

    write_png(os.path.join(out_dir, "pahoehoe_crust_diffuse.png"), np.rint(pah_diffuse * 255.0), 2)
    write_png(os.path.join(out_dir, "pahoehoe_crust_material.png"), np.rint(pah_material * 255.0), 6)

    # ── 3. Lava Voronoi Cooling Crust Plates + Fissure Network ───────────────
    wx = (periodic_fbm(size, [4, 8, 16], [0.55, 0.30, 0.15], rng) - 0.5) * 0.11
    wy = (periodic_fbm(size, [4, 8, 16], [0.55, 0.30, 0.15], rng) - 0.5) * 0.11
    f1_p, f2_p, plate_id = periodic_voronoi(size, 6, rng, warp_x=wx, warp_y=wy)
    f1_sub, f2_sub, _ = periodic_voronoi(size, 14, rng, warp_x=wx * 0.7, warp_y=wy * 0.7)

    edge_dist = np.maximum(0.0, f2_p - f1_p)
    sub_edge = np.maximum(0.0, f2_sub - f1_sub)

    # R: Smoothly domed basalt plate height (0 at fissure tear, 1 at plate center)
    dome = np.clip(edge_dist / 0.32, 0.0, 1.0)
    dome = dome * dome * (3.0 - 2.0 * dome)
    plate_height = np.clip(dome * (0.72 + 0.28 * plate_id) - 0.10 * (1.0 - np.clip(sub_edge / 0.15, 0.0, 1.0)), 0.0, 1.0)

    # G: Crisp glowing fissure mask (primary plate tears + fine secondary cracks)
    t_pri = np.clip(1.0 - edge_dist / 0.16, 0.0, 1.0)
    t_pri = t_pri * t_pri * (3.0 - 2.0 * t_pri)
    t_sec = np.clip(1.0 - sub_edge / 0.08, 0.0, 1.0) * 0.45
    fissure_mask = np.clip(np.maximum(t_pri, t_sec), 0.0, 1.0)

    # B: Domain-warped FBM magma turbulence
    fbm_turb = periodic_fbm(size, [4, 8, 16, 32], [0.40, 0.30, 0.20, 0.10], rng)

    # A: Vesicular basalt slag micro-roughness
    slag_micro = np.clip(0.45 + 0.35 * micro_grain + 0.20 * (1.0 - dome), 0.0, 1.0)

    lava_plates = np.stack((plate_height, fissure_mask, fbm_turb, slag_micro), axis=-1)
    write_png(os.path.join(out_dir, "lava_voronoi_plates.png"), np.rint(lava_plates * 255.0), 6)

    print(f"[LySon Textures] Generated 5 seamless {size}x{size} volcanic PBR textures -> {out_dir}", flush=True)


if __name__ == "__main__":
    generate_textures()
