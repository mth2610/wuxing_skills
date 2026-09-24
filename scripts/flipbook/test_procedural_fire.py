#!/usr/bin/env python3
"""test_procedural_fire.py — High-speed analytical multi-octave 4D raymarch with Niagara EOO packing."""

import math
import os
import sys
import time

import numpy as np
from PIL import Image
import taichi as ti

ti.init(arch=ti.gpu, default_fp=ti.f32, random_seed=42)

CELL = 256
N = 64

dens = ti.field(dtype=ti.f32, shape=(N, N, N))
flame = ti.field(dtype=ti.f32, shape=(N, N, N))

frame_color = ti.Vector.field(4, dtype=ti.f32, shape=(CELL, CELL))
frame_norm = ti.Vector.field(4, dtype=ti.f32, shape=(CELL, CELL))

@ti.func
def smoothstep_f(e0: ti.f32, e1: ti.f32, x: ti.f32) -> ti.f32:
    t = ti.math.clamp((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)

@ti.func
def snoise3(p: ti.types.vector(3, ti.f32), t: ti.f32) -> ti.f32:
    val = 0.0
    val += ti.sin(p.x * 2.8 + t * 1.1) * ti.cos(p.y * 2.5 - t * 0.9) * ti.sin(p.z * 3.1 + t * 1.2)
    val += 0.55 * ti.sin(p.y * 6.2 + t * 1.7) * ti.cos(p.z * 5.8 - t * 1.5) * ti.sin(p.x * 6.5 + t * 1.8)
    val += 0.32 * ti.sin(p.z * 13.5 - t * 2.9) * ti.cos(p.x * 12.8 + t * 2.6) * ti.sin(p.y * 14.2 - t * 3.1)
    val += 0.18 * ti.sin(p.x * 28.4 + t * 4.5) * ti.cos(p.y * 27.1 - t * 4.2) * ti.sin(p.z * 29.7 + t * 4.8)
    val += 0.09 * ti.sin(p.y * 58.2 + t * 6.8) * ti.cos(p.z * 55.4 - t * 6.1) * ti.sin(p.x * 61.3 + t * 7.2)
    return val

@ti.func
def sample_field(fld: ti.template(), gx: ti.f32, gy: ti.f32, gz: ti.f32) -> ti.f32:
    cx = ti.math.clamp(gx, 0.0, float(N - 1.001))
    cy = ti.math.clamp(gy, 0.0, float(N - 1.001))
    cz = ti.math.clamp(gz, 0.0, float(N - 1.001))
    ix = ti.cast(ti.floor(cx), ti.i32)
    iy = ti.cast(ti.floor(cy), ti.i32)
    iz = ti.cast(ti.floor(cz), ti.i32)
    fx = cx - float(ix)
    fy = gy - float(iy)
    fz = cz - float(iz)
    c000 = fld[ix, iy, iz]
    c100 = fld[ix + 1, iy, iz]
    c010 = fld[ix, iy + 1, iz]
    c110 = fld[ix + 1, iy + 1, iz]
    c001 = fld[ix, iy, iz + 1]
    c101 = fld[ix + 1, iy, iz + 1]
    c011 = fld[ix, iy + 1, iz + 1]
    c111 = fld[ix + 1, iy + 1, iz + 1]
    c00 = c000 * (1.0 - fx) + c100 * fx
    c10 = c010 * (1.0 - fx) + c110 * fx
    c01 = c001 * (1.0 - fx) + c101 * fx
    c11 = c011 * (1.0 - fx) + c111 * fx
    c0 = c00 * (1.0 - fy) + c10 * fy
    c1 = c01 * (1.0 - fy) + c11 * fy
    return c0 * (1.0 - fz) + c1 * fz

@ti.kernel
def raymarch_procedural(zoom: ti.f32, flame_scale: ti.f32, kfe: ti.f32, e_norm: ti.f32, time_val: ti.f32):
    for px, py in frame_color:
        u = ((float(px) + 0.5) / float(CELL) - 0.5) / zoom + 0.5
        v = ((float(py) + 0.5) / float(CELL) - 0.5) / zoom + 0.5

        gx_base = u * float(N - 1)
        gz_base = (1.0 - v) * float(N - 1)

        trans = 1.0
        emis_accum = 0.0
        body_accum = 0.0
        shadow_accum = 0.0
        accum_norm = ti.Vector([0.0, 0.0, 0.0])

        steps = 112
        dstep = float(N - 1) / float(steps)

        inside = (0.0 <= u <= 1.0) and (0.0 <= v <= 1.0)

        if inside:
            for s in range(steps):
                gy = float(s) * dstep

                p_norm = ti.Vector([gx_base, gy, gz_base]) / float(N)
                w_x = snoise3(p_norm * 4.2 + ti.Vector([12.3, 0, 0]), time_val * 1.5)
                w_y = snoise3(p_norm * 4.2 + ti.Vector([0, 45.6, 0]), time_val * 1.5)
                w_z = snoise3(p_norm * 4.2 + ti.Vector([0, 0, 78.9]), time_val * 1.5)

                gx = gx_base + w_x * 3.2
                gz = gz_base + w_z * 3.2
                gy_w = gy + w_y * 3.2

                f_raw = sample_field(flame, gx, gy_w, gz)
                if f_raw > 0.002:
                    noise_coarse = snoise3(p_norm * 5.5, time_val * 1.6)
                    noise_fine = snoise3(p_norm * 14.5, time_val * 2.4)
                    noise_micro = snoise3(p_norm * 32.0, time_val * 3.8)

                    filament_noise = 0.55 * noise_coarse + 0.32 * noise_fine + 0.18 * noise_micro
                    f_mod = f_raw * ti.max(0.0, 1.0 + 1.20 * filament_noise)

                    core_heat = ti.pow(ti.max(0.0, f_mod - 0.09) * 1.8, 1.6)
                    core_heat *= (0.68 + 0.65 * ti.max(0.0, noise_fine + 0.4))

                    ext = (f_mod * kfe) * (dstep / float(N)) * 22.0
                    step_alpha = 1.0 - ti.exp(-ext)

                    emis_accum += core_heat * flame_scale * trans * (dstep / float(N)) * 18.0
                    body_accum += f_mod * trans * (dstep / float(N)) * 14.0
                    shadow_accum += f_mod * (1.0 - trans) * (dstep / float(N)) * 12.0

                    delta = 1.0
                    dx = sample_field(flame, gx + delta, gy_w, gz) - sample_field(flame, gx - delta, gy_w, gz)
                    dy = sample_field(flame, gx, gy_w + delta, gz) - sample_field(flame, gx, gy_w - delta, gz)
                    dz = sample_field(flame, gx, gy_w, gz + delta) - sample_field(flame, gx, gy_w, gz - delta)

                    n_noise_x = snoise3(p_norm * 16.0, time_val)
                    n_noise_y = snoise3(p_norm * 16.0 + ti.Vector([7, 7, 7]), time_val)

                    local_n = ti.Vector([-0.5 * dx + 0.35 * n_noise_x, -0.5 * dz + 0.35 * n_noise_y, 0.5 * dy])
                    n_len = local_n.norm()
                    if n_len > 1e-4:
                        local_n /= n_len
                    else:
                        local_n = ti.Vector([0.0, 0.0, 1.0])

                    accum_norm += local_n * trans * step_alpha
                    trans *= ti.exp(-ext)
                    if trans < 0.003:
                        break

        # ── EXACT NIAGARA EOO FORMAT ──
        r_raw = emis_accum / ti.max(e_norm, 1e-4)
        r_val = ti.pow(ti.math.clamp(r_raw, 0.0, 1.0), 1.10)

        cov_raw = 1.0 - trans
        edge_wisp = snoise3(ti.Vector([u * 16.0, v * 16.0, 0.5]), time_val * 2.0)
        coverage = smoothstep_f(0.012 + 0.022 * edge_wisp, 0.11, cov_raw)

        body_norm = ti.math.clamp(body_accum * 0.85, 0.0, 1.0)
        crevice_shadow = ti.math.clamp(shadow_accum * 1.3, 0.0, 0.42)
        g_val = ti.math.clamp(1.0 - 0.62 * r_val - 0.18 * body_norm - crevice_shadow, 0.32, 1.0)

        if coverage < 0.005:
            r_val = 0.0
            g_val = 0.0
            coverage = 0.0

        frame_color[px, py] = ti.Vector([r_val, g_val, 0.0, coverage])

        an_len = accum_norm.norm()
        n_unit = accum_norm / an_len if an_len > 1e-4 else ti.Vector([0.0, 0.0, 1.0])
        frame_norm[px, py] = ti.Vector([
            0.5 + 0.5 * n_unit.x,
            0.5 + 0.5 * n_unit.y,
            0.5 + 0.5 * ti.max(0.0, n_unit.z),
            coverage
        ])


def main():
    p = "build_cache/fire_tongue_01/f016.npz"
    z = np.load(p)
    flame.from_numpy(np.ascontiguousarray(z["flame"], np.float32))

    zoom = 2.15
    t0 = time.time()
    raymarch_procedural(zoom, 3.2, 5.0, 1.0, 1.6)
    arr_probe = frame_color.to_numpy()
    e_max = float(np.percentile(arr_probe[..., 0], 99.5))
    if e_max < 1e-4: e_max = 1.0
    print(f"e_max probe: {e_max:.4f}")

    raymarch_procedural(zoom, 3.2, 5.0, e_max, 1.6)

    c_arr = frame_color.to_numpy().transpose(1, 0, 2)
    n_arr = frame_norm.to_numpy().transpose(1, 0, 2)

    c_u8 = (np.clip(c_arr, 0.0, 1.0) * 255.0).astype(np.uint8)
    n_u8 = (np.clip(n_arr, 0.0, 1.0) * 255.0).astype(np.uint8)

    Image.fromarray(c_u8, "RGBA").save("scratch/test_procedural_fire.png")
    Image.fromarray(n_u8, "RGBA").save("scratch/test_procedural_fire_norm.png")
    print(f"Rendered in {time.time() - t0:.2f}s -> scratch/test_procedural_fire.png")


if __name__ == "__main__":
    main()
