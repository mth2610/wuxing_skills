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
VRES = 128

grid_dens = ti.field(dtype=ti.f32, shape=(VRES, VRES, VRES))
frame_color = ti.Vector.field(4, dtype=ti.f32, shape=(CELL, CELL))
frame_norm = ti.Vector.field(4, dtype=ti.f32, shape=(CELL, CELL))

NUM_LOBES = 12
lobe_pos = ti.Vector.field(3, dtype=ti.f32, shape=NUM_LOBES)
lobe_rad = ti.field(dtype=ti.f32, shape=NUM_LOBES)
lobe_den = ti.field(dtype=ti.f32, shape=NUM_LOBES)

@ti.func
def smoothstep_f(e0: ti.f32, e1: ti.f32, x: ti.f32) -> ti.f32:
    t = ti.math.clamp((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)

@ti.func
def rot_coord(p: ti.types.vector(3, ti.f32)) -> ti.types.vector(3, ti.f32):
    return ti.Vector([
        0.00 * p.x + 0.80 * p.y + 0.60 * p.z,
       -0.80 * p.x + 0.36 * p.y - 0.48 * p.z,
       -0.60 * p.x - 0.48 * p.y + 0.64 * p.z
    ])

@ti.func
def snoise(p: ti.types.vector(3, ti.f32), t: ti.f32) -> ti.f32:
    p0 = p
    v0 = ti.sin(p0.x * 2.17 + t * 0.82) * ti.cos(p0.y * 1.93 - t * 0.74) * ti.sin(p0.z * 2.31 + t * 0.91)
    
    p1 = rot_coord(p0 * 2.03 + ti.Vector([1.73, 3.19, 5.41]))
    v1 = 0.50 * ti.sin(p1.x * 2.21 + t * 1.34) * ti.cos(p1.y * 1.97 - t * 1.18) * ti.sin(p1.x * 2.29 + t * 1.42)
    
    p2 = rot_coord(p1 * 2.05 + ti.Vector([4.31, 2.77, 8.13]))
    v2 = 0.25 * ti.sin(p2.x * 2.19 - t * 2.05) * ti.cos(p2.y * 2.03 + t * 1.83) * ti.sin(p2.z * 2.25 - t * 2.21)
    
    p3 = rot_coord(p2 * 2.07 + ti.Vector([2.89, 7.33, 1.45]))
    v3 = 0.12 * ti.sin(p3.x * 2.23 + t * 3.15) * ti.cos(p3.y * 1.95 - t * 2.89) * ti.sin(p3.z * 2.27 + t * 3.47)
    return (v0 + v1 + v2 + v3) / 1.87

@ti.func
def billow_noise(p: ti.types.vector(3, ti.f32), t: ti.f32) -> ti.f32:
    n = snoise(p, t)
    return 1.0 - ti.abs(n)

@ti.kernel
def update_lobes(t: ti.f32):
    # Lobe 0: Core convective center
    lobe_pos[0] = ti.Vector([
        0.02 * ti.sin(t * 0.8),
        0.02 * ti.cos(t * 0.9),
        -0.02 + 0.03 * ti.sin(t * 1.1)
    ])
    lobe_rad[0] = 0.19 + 0.02 * ti.cos(t * 0.9)
    lobe_den[0] = 1.0

    # 11 surrounding billowing lobes (primary + secondary)
    for k in range(1, NUM_LOBES):
        phase = float(k - 1) * (2.0 * math.pi / float(NUM_LOBES - 1))
        # Staggered turnover cycle
        speed = 0.25 if k <= 6 else 0.38
        cycle = ti.math.fract(t * speed + float(k) * 0.1618)
        
        # Primary lobes vs secondary smaller turbulence lobes
        is_primary = (k <= 6)
        r_base = 0.15 if is_primary else 0.22
        r_orbit = r_base + (0.10 if is_primary else 0.06) * ti.sin(cycle * math.pi)
        
        angle = phase + t * 0.18 + (0.4 if is_primary else -0.5) * ti.sin(cycle * 3.14)
        z_pos = (-0.16 if is_primary else -0.10) + 0.34 * cycle - 0.10 * ti.pow(cycle, 2.0)
        
        rad = (0.13 if is_primary else 0.08) + (0.06 if is_primary else 0.04) * cycle
        den = ti.sin(cycle * math.pi) * (1.0 if is_primary else 0.75)
        
        lobe_pos[k] = ti.Vector([
            r_orbit * ti.cos(angle),
            r_orbit * ti.sin(angle),
            z_pos
        ])
        lobe_rad[k] = rad
        lobe_den[k] = den

@ti.func
def domain_warp(p: ti.types.vector(3, ti.f32), t: ti.f32) -> ti.types.vector(3, ti.f32):
    wx = snoise(p * 2.4 + ti.Vector([1.2, 3.4, 5.6]), t * 0.8)
    wy = snoise(p * 2.4 + ti.Vector([7.8, 9.0, 1.2]), t * 0.8 + 1.4)
    wz = snoise(p * 2.4 + ti.Vector([3.4, 5.6, 7.8]), t * 0.8 + 2.8)
    return ti.Vector([wx, wy, wz]) * 0.070

@ti.kernel
def compute_volume(t: ti.f32):
    for i, j, k in grid_dens:
        # Map grid [0, VRES] to world [-0.45, 0.45]
        wx = (float(i) / float(VRES) - 0.5) * 0.90
        wy = (float(j) / float(VRES) - 0.5) * 0.90
        wz = (float(k) / float(VRES) - 0.5) * 0.90
        p_raw = ti.Vector([wx, wy, wz])

        # Multi-octave 4D domain warp
        warp1 = domain_warp(p_raw, t)
        p1 = p_raw + warp1
        warp2 = domain_warp(p1 * 2.4, t * 1.3) * 0.40
        p = p1 + warp2

        total_d = 0.0
        for l in range(NUM_LOBES):
            l_pos = lobe_pos[l]
            l_rad = lobe_rad[l]
            l_den = lobe_den[l]
            
            d_lobe = (p - l_pos).norm()
            if d_lobe < l_rad * 1.30:
                nd = d_lobe / l_rad
                
                # Fractal billow texture on lobe surface
                oct0 = billow_noise(p * 4.8 + ti.Vector([float(l)*1.7, 0.0, 0.0]), t * 1.0)
                oct1 = billow_noise(p * 10.5 + ti.Vector([0.0, float(l)*2.1, 0.0]), t * 1.6)
                oct2 = billow_noise(p * 22.0, t * 2.4)
                b_noise = oct0 * 0.52 + oct1 * 0.33 + oct2 * 0.15
                
                # Cauliflower billow curve
                shape = smoothstep_f(1.20, 0.20, nd)
                cauliflower = shape * (0.62 + 0.38 * b_noise) - 0.20 * (1.0 - b_noise) * (1.0 - shape)
                val = ti.max(0.0, cauliflower) * l_den
                total_d = ti.max(total_d, val)

        # Micro-wisp veil attached strictly to billow surface
        if total_d > 0.01:
            wisp = snoise(p * 9.0 + ti.Vector([5.1, 2.3, 8.4]), t * 1.8) * 0.65 + \
                   snoise(p * 18.0 + ti.Vector([1.2, 9.4, 3.1]), t * 2.6) * 0.35
            total_d += 0.20 * wisp * total_d

        dist_orig = p_raw.norm()
        wisp_bound = snoise(p_raw * 6.0 + ti.Vector([3.1, 7.4, 1.2]), t * 1.2)
        edge_feather = smoothstep_f(0.44, 0.28, dist_orig + 0.06 * wisp_bound)

        grid_dens[i, j, k] = ti.max(0.0, total_d * edge_feather)

@ti.func
def sample_trilinear(fld: ti.template(), u: ti.f32, v: ti.f32, w: ti.f32) -> ti.f32:
    gx = ti.math.clamp(u * float(VRES - 1), 0.0, float(VRES - 1.001))
    gy = ti.math.clamp(v * float(VRES - 1), 0.0, float(VRES - 1.001))
    gz = ti.math.clamp(w * float(VRES - 1), 0.0, float(VRES - 1.001))

    ix = ti.cast(ti.floor(gx), ti.i32)
    iy = ti.cast(ti.floor(gy), ti.i32)
    iz = ti.cast(ti.floor(gz), ti.i32)

    fx = gx - float(ix)
    fy = gy - float(iy)
    fz = gz - float(iz)

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
def raymarch_smoke(zoom: ti.f32, d_scale: ti.f32):
    light_dir = ti.Vector([0.45, 0.65, 0.60]).normalized()

    for px, py in frame_color:
        u = ((float(px) + 0.5) / float(CELL) - 0.5) / zoom + 0.5
        v = ((float(py) + 0.5) / float(CELL) - 0.5) / zoom + 0.5

        trans = 1.0
        light_detail_accum = 0.0
        occlusion_accum = 0.0
        accum_norm = ti.Vector([0.0, 0.0, 0.0])

        steps = 96
        dstep = 1.0 / float(steps)

        inside = (0.0 <= u <= 1.0) and (0.0 <= v <= 1.0)
        fade = smoothstep_f(0.0, 0.06, u) * smoothstep_f(1.0, 0.94, u) * \
               smoothstep_f(0.0, 0.06, v) * smoothstep_f(1.0, 0.94, v)

        if inside:
            for s in range(steps):
                y = float(s) * dstep
                x = u
                z = 1.0 - v

                d_val = sample_trilinear(grid_dens, x, y, z) * d_scale
                ext = d_val * dstep * 24.0 * fade
                step_alpha = 1.0 - ti.exp(-ext)

                # Shadow ray for internal forward scattering
                s_shadow = 0.0
                for ss in range(1, 5):
                    s_pos = ti.Vector([x, y, z]) + light_dir * (float(ss) * 0.045)
                    s_val = sample_trilinear(grid_dens, s_pos.x, s_pos.y, s_pos.z) * d_scale
                    s_shadow += s_val * 0.045 * 18.0
                light_trans = ti.exp(-s_shadow)

                light_detail_accum += d_val * trans * light_trans * dstep * 14.0 * fade
                occlusion_accum += d_val * trans * dstep * 16.0 * fade

                # Sample smooth gradient with wider delta for noise-free surface normals
                delta = 2.5 / float(VRES)
                dx = sample_trilinear(grid_dens, x + delta, y, z) - sample_trilinear(grid_dens, x - delta, y, z)
                dy = sample_trilinear(grid_dens, x, y + delta, z) - sample_trilinear(grid_dens, x, y - delta, z)
                dz = sample_trilinear(grid_dens, x, y, z + delta) - sample_trilinear(grid_dens, x, y, z - delta)

                local_n = ti.Vector([-dx, -dz, dy])
                n_len = local_n.norm()
                if n_len > 1e-4:
                    local_n /= n_len
                else:
                    local_n = ti.Vector([0.0, 0.0, 1.0])

                accum_norm += local_n * trans * step_alpha
                trans *= ti.exp(-ext)
                if trans < 0.003:
                    break

        coverage = smoothstep_f(0.015, 0.12, 1.0 - trans) * fade

        # In Niagara EOO smoke format:
        # G = Transmittance: 1.0 at outer edges, dips to 0.38 - 0.50 in deep dense core
        g_val = ti.math.clamp(1.0 - occlusion_accum * 0.40, 0.38, 1.0)
        if coverage < 0.01:
            g_val = 1.0

        # R = Internal forward light detail (rim and crease scattering, peaks around 0.15 - 0.45)
        r_val = ti.math.clamp(light_detail_accum * 0.65, 0.0, 0.50) * coverage

        # B = 0.0 (strictly unused in EOO smoke)
        frame_color[px, py] = ti.Vector([r_val, g_val, 0.0, coverage])

        an_len = accum_norm.norm()
        n_unit = accum_norm / an_len if an_len > 1e-4 else ti.Vector([0.0, 0.0, 1.0])
        edge_blend = smoothstep_f(0.01, 0.25, coverage)
        n_smooth = n_unit * edge_blend + ti.Vector([0.0, 0.0, 1.0]) * (1.0 - edge_blend)
        ns_len = n_smooth.norm()
        n_final = n_smooth / ns_len if ns_len > 1e-4 else ti.Vector([0.0, 0.0, 1.0])

        # Normal map in BC5 tangent format
        frame_norm[px, py] = ti.Vector([
            0.5 + 0.5 * n_final.x,
            0.5 + 0.5 * n_final.y,
            0.0,
            1.0
        ])


def main():
    print("Testing procedural cauliflower billow volume...")
    os.makedirs("scratch/cauliflower_test", exist_ok=True)
    t0 = time.time()
    
    for f in [0, 8, 16, 24]:
        sim_t = float(f) * 0.08
        update_lobes(sim_t)
        compute_volume(sim_t)
        raymarch_smoke(1.10, 1.30)
        
        c_arr = frame_color.to_numpy().transpose(1, 0, 2)
        n_arr = frame_norm.to_numpy().transpose(1, 0, 2)
        c_u8 = (np.clip(c_arr, 0.0, 1.0) * 255.0).astype(np.uint8)
        n_u8 = (np.clip(n_arr, 0.0, 1.0) * 255.0).astype(np.uint8)
        
        Image.fromarray(c_u8, "RGBA").save(f"scratch/cauliflower_test/f{f:02d}.png")
        Image.fromarray(n_u8, "RGBA").save(f"scratch/cauliflower_test/n{f:02d}.png")
        print(f"Generated frame {f} in {time.time() - t0:.2f}s")

if __name__ == "__main__":
    main()
