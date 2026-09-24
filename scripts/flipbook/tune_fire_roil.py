#!/usr/bin/env python3
"""tune_fire_roil.py — Rapid iterative tuning of Niagara EOO Fire Roil with rich microstructure."""

import math
import os
import sys
import time

import numpy as np
from PIL import Image
import taichi as ti

ti.init(arch=ti.gpu, default_fp=ti.f32, random_seed=42)

N_PARTICLES = 300000
VRES = 160
CELL = 256

p_pos = ti.Vector.field(3, dtype=ti.f32, shape=N_PARTICLES)
p_life = ti.field(dtype=ti.f32, shape=N_PARTICLES)
p_max_life = ti.field(dtype=ti.f32, shape=N_PARTICLES)
p_heat = ti.field(dtype=ti.f32, shape=N_PARTICLES)

grid_core = ti.field(dtype=ti.f32, shape=(VRES, VRES, VRES))
grid_envelope = ti.field(dtype=ti.f32, shape=(VRES, VRES, VRES))

frame_color = ti.Vector.field(4, dtype=ti.f32, shape=(CELL, CELL))
frame_norm = ti.Vector.field(4, dtype=ti.f32, shape=(CELL, CELL))

@ti.func
def hash31(p: ti.f32) -> ti.types.vector(3, ti.f32):
    p3 = ti.math.fract(ti.Vector([p * 0.1031, p * 0.1030, p * 0.0973]))
    p3 += p3.dot(ti.Vector([p3.y + 33.33, p3.z + 33.33, p3.x + 33.33]))
    return ti.math.fract(ti.Vector([(p3.x + p3.y) * p3.z, (p3.x + p3.z) * p3.y, (p3.y + p3.z) * p3.x]))

@ti.func
def smoothstep_f(e0: ti.f32, e1: ti.f32, x: ti.f32) -> ti.f32:
    t = ti.math.clamp((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)

@ti.func
def snoise(p: ti.types.vector(3, ti.f32), t: ti.f32) -> ti.f32:
    val = 0.0
    # 4 distinct octaves for deep cauliflower billows and fine licking tendrils
    val += ti.sin(p.x * 2.2 + t * 0.9) * ti.cos(p.y * 2.0 - t * 0.8) * ti.sin(p.z * 2.4 + t * 1.0)
    val += 0.55 * ti.sin(p.y * 4.8 + t * 1.4) * ti.cos(p.z * 4.4 - t * 1.3) * ti.sin(p.x * 5.1 + t * 1.5)
    val += 0.28 * ti.sin(p.z * 9.8 - t * 2.3) * ti.cos(p.x * 9.2 + t * 2.1) * ti.sin(p.y * 10.4 - t * 2.5)
    val += 0.14 * ti.sin(p.x * 20.5 + t * 3.9) * ti.cos(p.y * 19.8 - t * 3.7) * ti.sin(p.z * 21.6 + t * 4.2)
    return val

@ti.func
def vector_potential(p: ti.types.vector(3, ti.f32), t: ti.f32) -> ti.types.vector(3, ti.f32):
    ax = snoise(p + ti.Vector([12.3, 45.6, 78.9]), t)
    ay = snoise(p + ti.Vector([98.7, 65.4, 32.1]), t + 1.7)
    az = snoise(p + ti.Vector([43.2, 87.6, 19.5]), t + 3.4)
    return ti.Vector([ax, ay, az])

@ti.func
def curl_noise(p: ti.types.vector(3, ti.f32), t: ti.f32) -> ti.types.vector(3, ti.f32):
    eps = 0.045
    p_px = vector_potential(p + ti.Vector([eps, 0.0, 0.0]), t)
    p_nx = vector_potential(p - ti.Vector([eps, 0.0, 0.0]), t)
    p_py = vector_potential(p + ti.Vector([0.0, eps, 0.0]), t)
    p_ny = vector_potential(p - ti.Vector([0.0, eps, 0.0]), t)
    p_pz = vector_potential(p + ti.Vector([0.0, 0.0, eps]), t)
    p_nz = vector_potential(p - ti.Vector([0.0, 0.0, eps]), t)

    dAz_dy = (p_py.z - p_ny.z) / (2.0 * eps)
    dAy_dz = (p_pz.y - p_nz.y) / (2.0 * eps)
    dAx_dz = (p_pz.x - p_nz.x) / (2.0 * eps)
    dAz_dx = (p_px.z - p_nx.z) / (2.0 * eps)
    dAy_dx = (p_px.y - p_nx.y) / (2.0 * eps)
    dAx_dy = (p_py.x - p_ny.x) / (2.0 * eps)

    return ti.Vector([dAz_dy - dAy_dz, dAx_dz - dAz_dx, dAy_dx - dAx_dy])

@ti.kernel
def init_particles():
    for i in p_pos:
        rnd = hash31(float(i))
        theta = rnd.x * 2.0 * math.pi
        phi = ti.acos(2.0 * rnd.y - 1.0)
        # Tight ball in lower-center
        r = 0.18 * ti.pow(rnd.z, 0.45)
        p_pos[i] = ti.Vector([
            r * ti.sin(phi) * ti.cos(theta),
            r * ti.sin(phi) * ti.sin(theta),
            r * ti.cos(phi) - 0.06
        ])
        p_life[i] = rnd.x * 0.8
        p_max_life[i] = 1.0 + rnd.y * 0.6
        p_heat[i] = 1.0 - (r / 0.18) * 0.35

@ti.kernel
def step_sim(dt: ti.f32, sim_time: ti.f32):
    for i in p_pos:
        p_life[i] += dt
        if p_life[i] >= p_max_life[i]:
            rnd = hash31(float(i) + sim_time * 513.7)
            theta = rnd.x * 2.0 * math.pi
            r = 0.15 * ti.sqrt(rnd.y)
            p_pos[i] = ti.Vector([
                r * ti.cos(theta),
                r * ti.sin(theta) * 0.85,
                -0.18 + rnd.z * 0.06
            ])
            p_life[i] = 0.0
            p_max_life[i] = 1.0 + rnd.z * 0.6
            p_heat[i] = 1.0

        pos = p_pos[i]
        r_xy = ti.Vector([pos.x, pos.y]).norm()

        # Multi-octave curl turbulence
        v_curl = curl_noise(pos * 2.6, sim_time * 1.6)

        # Buoyant rise + toroidal expansion
        v_buoy = ti.Vector([0.0, 0.0, 0.75 * p_heat[i]])
        # Toroidal swirl: expand outward at upper half
        v_roll = ti.Vector([pos.x, pos.y, 0.0]) / ti.max(r_xy, 1e-4) * (0.35 * ti.max(0.0, pos.z + 0.12))
        # Entrainment suction at bottom
        v_entrain = -ti.Vector([pos.x, pos.y, 0.0]) * (0.50 * ti.max(0.0, -pos.z))

        v_total = v_curl * 0.95 + v_buoy + v_roll + v_entrain

        # Smooth containment to keep in bounds
        if r_xy > 0.35:
            v_total.x -= (pos.x / r_xy) * (r_xy - 0.35) * 5.0
            v_total.y -= (pos.y / r_xy) * (r_xy - 0.35) * 5.0
        if pos.z > 0.35:
            v_total.z -= (pos.z - 0.35) * 6.0
        if pos.z < -0.32:
            v_total.z += (-0.32 - pos.z) * 6.0

        p_pos[i] += v_total * dt
        p_heat[i] = ti.max(0.0, p_heat[i] - dt * (0.75 + 0.45 * ti.max(0.0, pos.z)))

@ti.kernel
def clear_grids():
    for i, j, k in grid_core:
        grid_core[i, j, k] = 0.0
        grid_envelope[i, j, k] = 0.0

@ti.kernel
def splat_particles():
    for i in p_pos:
        pos = p_pos[i]
        # Map [-0.42, 0.42] -> [0, VRES - 1]
        uvw = (pos * 1.18 + 0.5) * float(VRES)
        if 2.0 <= uvw.x < VRES - 3.0 and 2.0 <= uvw.y < VRES - 3.0 and 2.0 <= uvw.z < VRES - 3.0:
            base = ti.cast(ti.floor(uvw), ti.i32)
            f = uvw - ti.cast(base, ti.f32)

            prog = p_life[i] / p_max_life[i]
            life_fade = ti.sin(prog * math.pi)
            heat = p_heat[i]

            # Crisp incandescent core filaments
            w_core = life_fade * ti.pow(heat, 2.5) * 1.6
            # Billowing outer envelope
            w_env = life_fade * (0.25 + 0.75 * heat) * 0.9

            for dx, dy, dz in ti.static(ti.ndrange(2, 2, 2)):
                c_w = ((1.0 - f.x) if dx == 0 else f.x) * \
                      ((1.0 - f.y) if dy == 0 else f.y) * \
                      ((1.0 - f.z) if dz == 0 else f.z)
                ti.atomic_add(grid_core[base.x + dx, base.y + dy, base.z + dz], w_core * c_w)
                ti.atomic_add(grid_envelope[base.x + dx, base.y + dy, base.z + dz], w_env * c_w)

@ti.func
def sample_field(fld: ti.template(), u: ti.f32, v: ti.f32, w: ti.f32) -> ti.f32:
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
def raymarch_fire(e_norm: ti.f32):
    for px, py in frame_color:
        u_coord = (float(px) + 0.5) / float(CELL)
        v_coord = (float(py) + 0.5) / float(CELL)

        trans = 1.0
        emis_accum = 0.0
        env_accum = 0.0
        accum_norm = ti.Vector([0.0, 0.0, 0.0])

        steps = 112
        ds = 1.0 / float(steps)

        for s in range(steps):
            y_c = float(s) * ds
            x_c = u_coord
            z_c = 1.0 - v_coord

            c_val = sample_field(grid_core, x_c, y_c, z_c)
            e_val = sample_field(grid_envelope, x_c, y_c, z_c)

            # Extinction: envelope provides the dense gas body
            ext = (e_val * 4.2 + c_val * 6.5) * ds
            step_alpha = 1.0 - ti.exp(-ext)

            emis_accum += c_val * trans * ds
            env_accum += e_val * trans * ds

            delta = 1.0 / float(VRES)
            dx = sample_field(grid_envelope, x_c + delta, y_c, z_c) - sample_field(grid_envelope, x_c - delta, y_c, z_c)
            dy = sample_field(grid_envelope, x_c, y_c + delta, z_c) - sample_field(grid_envelope, x_c, y_c - delta, z_c)
            dz = sample_field(grid_envelope, x_c, y_c, z_c + delta) - sample_field(grid_envelope, x_c, y_c, z_c - delta)

            local_n = ti.Vector([-dx, -dz, dy])
            n_len = local_n.norm()
            if n_len > 1e-4:
                local_n /= n_len
            else:
                local_n = ti.Vector([0.0, 0.0, 1.0])

            accum_norm += local_n * trans * step_alpha
            trans *= ti.exp(-ext)
            if trans < 0.005:
                break

        # Exact Niagara EOO Mapping
        r_raw = emis_accum / ti.max(e_norm, 1e-5)
        # Power curve to keep core intense and boundary sharp
        r_val = ti.pow(ti.math.clamp(r_raw, 0.0, 1.0), 1.35)

        cov_raw = 1.0 - trans
        coverage = smoothstep_f(0.02, 0.14, cov_raw)

        # G: Transmittance (1.0 - 0.68*R - 0.22*env)
        env_norm = ti.math.clamp(env_accum * 1.8, 0.0, 1.0)
        g_val = ti.math.clamp(1.0 - 0.68 * r_val - 0.22 * env_norm, 0.32, 1.0)

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
    init_particles()
    # Warm up physical convection
    for w in range(30):
        step_sim(0.04, float(w) * 0.04)

    # Step to frame 16
    for f in range(16):
        sim_t = (30 + f) * 0.04
        for _ in range(3):
            step_sim(0.04 / 3.0, sim_t)

    clear_grids()
    splat_particles()

    # Probe max emission for normalization
    raymarch_fire(1.0)
    arr_probe = frame_color.to_numpy()
    e_max = float(np.percentile(arr_probe[..., 0], 99.5))
    if e_max < 1e-4: e_max = 1.0
    print(f"e_max probe: {e_max:.4f}")

    # Final normalized render
    raymarch_fire(e_max)
    c_arr = frame_color.to_numpy().transpose(1, 0, 2)
    n_arr = frame_norm.to_numpy().transpose(1, 0, 2)

    c_u8 = (np.clip(c_arr, 0.0, 1.0) * 255.0).astype(np.uint8)
    n_u8 = (np.clip(n_arr, 0.0, 1.0) * 255.0).astype(np.uint8)

    Image.fromarray(c_u8, "RGBA").save("scratch/tuned_fire_roil.png")
    Image.fromarray(n_u8, "RGBA").save("scratch/tuned_fire_roil_norm.png")
    print("Exported scratch/tuned_fire_roil.png and scratch/tuned_fire_roil_norm.png")

if __name__ == "__main__":
    main()
