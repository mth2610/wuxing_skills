#!/usr/bin/env python3
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
def billow_noise(p: ti.types.vector(3, ti.f32), t: ti.f32) -> ti.f32:
    b1 = 1.0 - 2.0 * ti.abs(ti.sin(p.x * 3.5 + t * 1.2) * ti.cos(p.y * 3.1 - t) * ti.sin(p.z * 3.8 + t * 1.1))
    b2 = 1.0 - 2.0 * ti.abs(ti.sin(p.y * 7.8 + t * 1.8) * ti.cos(p.z * 7.2 - t * 1.6) * ti.sin(p.x * 8.1 + t * 1.7))
    b3 = 1.0 - 2.0 * ti.abs(ti.sin(p.z * 17.5 - t * 3.0) * ti.cos(p.x * 16.2 + t * 2.8) * ti.sin(p.y * 18.1 - t * 3.2))
    b4 = 1.0 - 2.0 * ti.abs(ti.sin(p.x * 36.0 + t * 4.6) * ti.cos(p.y * 34.0 - t * 4.4) * ti.sin(p.z * 37.0 + t * 4.9))
    return 0.45 * b1 + 0.30 * b2 + 0.18 * b3 + 0.10 * b4

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
    c000 = fld[iz, iy, ix]
    c100 = fld[iz, iy, ix + 1]
    c010 = fld[iz, iy + 1, ix]
    c110 = fld[iz, iy + 1, ix + 1]
    c001 = fld[iz + 1, iy, ix]
    c101 = fld[iz + 1, iy, ix + 1]
    c011 = fld[iz + 1, iy + 1, ix]
    c111 = fld[iz + 1, iy + 1, ix + 1]
    c00 = c000 * (1.0 - fx) + c100 * fx
    c10 = c010 * (1.0 - fx) + c110 * fx
    c01 = c001 * (1.0 - fx) + c101 * fx
    c11 = c011 * (1.0 - fx) + c111 * fx
    c0 = c00 * (1.0 - fy) + c10 * fy
    c1 = c01 * (1.0 - fy) + c11 * fy
    return c0 * (1.0 - fz) + c1 * fz

@ti.kernel
def raymarch_cauliflower(zoom: ti.f32, flame_scale: ti.f32, kfe: ti.f32, e_norm: ti.f32, time_val: ti.f32):
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

        steps = 120
        dstep = float(N - 1) / float(steps)

        inside = (0.0 <= u <= 1.0) and (0.0 <= v <= 1.0)

        if inside:
            for s in range(steps):
                gy = float(s) * dstep

                p_norm = ti.Vector([gx_base, gy, gz_base]) / float(N)
                
                # Anisotropic flame stretch: tendrils stretch vertically along Y/Z
                p_stretch = ti.Vector([p_norm.x * 1.25, p_norm.y * 1.25, p_norm.z * 0.85])

                # 4D Domain warp
                w_x = snoise3(p_stretch * 5.0 + ti.Vector([12.3, 0, 0]), time_val * 1.4)
                w_y = snoise3(p_stretch * 5.0 + ti.Vector([0, 45.6, 0]), time_val * 1.4)
                w_z = snoise3(p_stretch * 5.0 + ti.Vector([0, 0, 78.9]), time_val * 1.4)

                gx = gx_base + w_x * 3.5
                gz = gz_base + w_z * 3.5
                gy_w = gy + w_y * 3.5

                f_raw = sample_field(flame, gx, gy_w, gz)
                if f_raw > 0.002:
                    # Cauliflower billow & micro-crevice carving
                    b_noise = billow_noise(p_stretch * 4.2, time_val * 1.6)
                    fine_wisp = snoise3(p_stretch * 18.0, time_val * 2.8)
                    micro_grain = snoise3(p_stretch * 40.0, time_val * 4.2)

                    # Compound modulation: sharp billow lobes + fine wisps
                    modulation = ti.max(0.0, 0.68 + 0.90 * b_noise + 0.32 * fine_wisp + 0.12 * micro_grain)
                    f_mod = f_raw * modulation

                    # Incandescent core heat: Planck filament veins
                    core_heat = ti.pow(ti.max(0.0, f_mod - 0.08) * 1.7, 1.5)
                    # Vein intensity variation
                    core_heat *= (0.60 + 0.80 * ti.max(0.0, fine_wisp + 0.3))

                    ext = (f_mod * kfe) * (dstep / float(N)) * 24.0
                    step_alpha = 1.0 - ti.exp(-ext)

                    emis_accum += core_heat * flame_scale * trans * (dstep / float(N)) * 20.0
                    body_accum += f_mod * trans * (dstep / float(N)) * 16.0
                    shadow_accum += f_mod * (1.0 - trans) * (dstep / float(N)) * 14.0

                    delta = 1.0
                    dx = sample_field(flame, gx + delta, gy_w, gz) - sample_field(flame, gx - delta, gy_w, gz)
                    dy = sample_field(flame, gx, gy_w + delta, gz) - sample_field(flame, gx, gy_w - delta, gz)
                    dz = sample_field(flame, gx, gy_w, gz + delta) - sample_field(flame, gx, gy_w, gz - delta)

                    n_noise_x = snoise3(p_stretch * 12.0, time_val)
                    n_noise_y = snoise3(p_stretch * 12.0 + ti.Vector([7, 7, 7]), time_val)

                    # Smooth normal volume with subtle billow perturbations
                    local_n = ti.Vector([-0.5 * dx + 0.12 * n_noise_x, -0.5 * dz + 0.12 * n_noise_y, 0.5 * dy])
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
        r_val = ti.pow(ti.math.clamp(r_raw, 0.0, 1.0), 1.12)

        cov_raw = 1.0 - trans
        edge_wisp = snoise3(ti.Vector([u * 22.0, v * 22.0, 0.5]), time_val * 2.2)
        coverage = smoothstep_f(0.012 + 0.025 * edge_wisp, 0.12, cov_raw)

        body_norm = ti.math.clamp(body_accum * 0.85, 0.0, 1.0)
        crevice_shadow = ti.math.clamp(shadow_accum * 1.4, 0.0, 0.45)
        # Transmittance G: exactly matches Niagara EOO [82, 255]
        g_val = ti.math.clamp(1.0 - 0.65 * r_val - 0.20 * body_norm - crevice_shadow, 0.32, 1.0)

        # In Niagara EOO, unmasked area (coverage == 0) has G = 1.0, R = 0, B = 0, A = 0
        if coverage < 0.005:
            r_val = 0.0
            g_val = 1.0
            coverage = 0.0

        frame_color[px, py] = ti.Vector([r_val, g_val, 0.0, coverage])

        an_len = accum_norm.norm()
        n_unit = accum_norm / an_len if an_len > 1e-4 else ti.Vector([0.0, 0.0, 1.0])
        edge_blend = smoothstep_f(0.005, 0.30, coverage)
        n_smooth = n_unit * edge_blend + ti.Vector([0.0, 0.0, 1.0]) * (1.0 - edge_blend)
        ns_len = n_smooth.norm()
        n_final = n_smooth / ns_len if ns_len > 1e-4 else ti.Vector([0.0, 0.0, 1.0])
        frame_norm[px, py] = ti.Vector([
            0.5 + 0.5 * n_final.x,
            0.5 + 0.5 * n_final.y,
            0.0,
            1.0
        ])


def main():
    p = "build_cache/fire_tongue_01/f016.npz"
    z = np.load(p)
    flame.from_numpy(np.ascontiguousarray(z["flame"], np.float32))

    zoom = 2.15
    t0 = time.time()
    raymarch_cauliflower(zoom, 3.2, 5.0, 1.0, 1.6)
    arr_probe = frame_color.to_numpy()
    e_max = float(np.percentile(arr_probe[..., 0], 99.5))
    if e_max < 1e-4: e_max = 1.0
    print(f"e_max probe: {e_max:.4f}")

    raymarch_cauliflower(zoom, 3.2, 5.0, e_max, 1.6)

    c_arr = frame_color.to_numpy().transpose(1, 0, 2)
    n_arr = frame_norm.to_numpy().transpose(1, 0, 2)

    c_u8 = (np.clip(c_arr, 0.0, 1.0) * 255.0).astype(np.uint8)
    n_u8 = (np.clip(n_arr, 0.0, 1.0) * 255.0).astype(np.uint8)

    Image.fromarray(c_u8, "RGBA").save("scratch/test_procedural_fire_v3.png")
    Image.fromarray(n_u8, "RGBA").save("scratch/test_procedural_fire_norm_v3.png")
    print(f"Rendered in {time.time() - t0:.2f}s -> scratch/test_procedural_fire_v3.png")


if __name__ == "__main__":
    main()
