#!/usr/bin/env python3
import math
import os
import sys
import time
import numpy as np
from PIL import Image
import taichi as ti

ti.init(arch=ti.gpu, default_fp=ti.f32, random_seed=1234)

CELL = 256
VRES = 128
N_PARTICLES = 300000

p_pos = ti.Vector.field(3, dtype=ti.f32, shape=N_PARTICLES)
p_life = ti.field(dtype=ti.f32, shape=N_PARTICLES)
p_max_life = ti.field(dtype=ti.f32, shape=N_PARTICLES)

grid_dens = ti.field(dtype=ti.f32, shape=(VRES, VRES, VRES))
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
    val += ti.sin(p.x * 2.1 + t * 0.95) * ti.cos(p.y * 1.9 - t * 0.85) * ti.sin(p.z * 2.3 + t * 1.05)
    val += 0.50 * ti.sin(p.y * 4.4 + t * 1.45) * ti.cos(p.z * 4.1 - t * 1.30) * ti.sin(p.x * 4.7 + t * 1.55)
    val += 0.25 * ti.sin(p.z * 9.2 - t * 2.20) * ti.cos(p.x * 8.6 + t * 1.95) * ti.sin(p.y * 9.8 - t * 2.40)
    val += 0.12 * ti.sin(p.x * 19.5 + t * 3.80) * ti.cos(p.y * 18.7 - t * 3.50) * ti.sin(p.z * 20.8 + t * 4.10)
    return val

@ti.func
def vector_potential(p: ti.types.vector(3, ti.f32), t: ti.f32) -> ti.types.vector(3, ti.f32):
    ax = snoise(p + ti.Vector([12.3, 45.6, 78.9]), t)
    ay = snoise(p + ti.Vector([98.7, 65.4, 32.1]), t + 1.7)
    az = snoise(p + ti.Vector([43.2, 87.6, 19.5]), t + 3.4)
    return ti.Vector([ax, ay, az])

@ti.func
def curl_noise(p: ti.types.vector(3, ti.f32), t: ti.f32) -> ti.types.vector(3, ti.f32):
    eps = 0.06
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
def init_particles(seed: ti.f32):
    for i in p_pos:
        rnd = hash31(float(i) + seed * 1000.0)
        theta = rnd.x * 2.0 * math.pi
        phi = ti.acos(2.0 * rnd.y - 1.0)
        r = 0.28 * ti.pow(rnd.z, 0.5)

        x = r * ti.sin(phi) * ti.cos(theta)
        y = r * ti.sin(phi) * ti.sin(theta)
        z = r * ti.cos(phi)

        p_pos[i] = ti.Vector([x, y, z])
        p_life[i] = rnd.x * 1.5
        p_max_life[i] = 1.2 + rnd.y * 1.0

@ti.kernel
def step_particles(dt: ti.f32, time_val: ti.f32):
    for i in p_pos:
        p_life[i] += dt
        if p_life[i] >= p_max_life[i]:
            rnd = hash31(float(i) + time_val * 513.7)
            theta = rnd.x * 2.0 * math.pi
            phi = ti.acos(2.0 * rnd.y - 1.0)
            r = 0.20 * ti.pow(rnd.z, 0.5)

            p_pos[i] = ti.Vector([
                r * ti.sin(phi) * ti.cos(theta),
                r * ti.sin(phi) * ti.sin(theta),
                r * ti.cos(phi) - 0.05
            ])
            p_life[i] = 0.0
            p_max_life[i] = 1.2 + rnd.z * 1.0

        pos = p_pos[i]
        r_xy = ti.Vector([pos.x, pos.y]).norm()

        # 1. Convective Toroidal Roll (Hill's spherical vortex)
        # Smoke rises in the center (+Z), curls outward at top, sinks at sides
        v_rise = ti.Vector([0.0, 0.0, 0.45 * ti.exp(-r_xy * r_xy * 12.0)])
        roll_dir = ti.Vector([pos.x, pos.y, 0.0]) / ti.max(r_xy, 1e-4)
        v_roll = roll_dir * (0.35 * ti.max(0.0, pos.z)) - roll_dir * (0.45 * ti.max(0.0, -pos.z))
        v_sink = ti.Vector([0.0, 0.0, -0.30 * ti.max(0.0, r_xy - 0.18)])

        # 2. 4D Curl Noise for churning cauliflower eddies
        v_curl = curl_noise(pos * 2.5, time_val * 1.6) * 0.75

        v_total = v_rise + v_roll + v_sink + v_curl

        # Smooth spherical boundary containment
        dist = pos.norm()
        if dist > 0.38:
            v_total -= (pos / dist) * (dist - 0.38) * 4.0

        p_pos[i] += v_total * dt

@ti.kernel
def clear_grid():
    for i, j, k in grid_dens:
        grid_dens[i, j, k] = 0.0

@ti.kernel
def splat_to_grid(splat_weight: ti.f32):
    for i in p_pos:
        pos = p_pos[i]
        dist = pos.norm()
        uvw = (pos * 1.15 + 0.5) * float(VRES)
        if 2.0 <= uvw.x < VRES - 3.0 and 2.0 <= uvw.y < VRES - 3.0 and 2.0 <= uvw.z < VRES - 3.0:
            base = ti.cast(ti.floor(uvw), ti.i32)
            f = uvw - ti.cast(base, ti.f32)

            progress = p_life[i] / p_max_life[i]
            life_fade = ti.sin(progress * math.pi)
            radial_fade = smoothstep_f(0.42, 0.30, dist)
            w = life_fade * radial_fade * splat_weight

            for dx, dy, dz in ti.static(ti.ndrange(2, 2, 2)):
                c_weight = ((1.0 - f.x) if dx == 0 else f.x) * \
                           ((1.0 - f.y) if dy == 0 else f.y) * \
                           ((1.0 - f.z) if dz == 0 else f.z)
                ti.atomic_add(grid_dens[base.x + dx, base.y + dy, base.z + dz], w * c_weight)

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
    for px, py in frame_color:
        u = ((float(px) + 0.5) / float(CELL) - 0.5) / zoom + 0.5
        v = ((float(py) + 0.5) / float(CELL) - 0.5) / zoom + 0.5

        trans = 1.0
        smoke_accum = 0.0
        shadow_accum = 0.0
        accum_norm = ti.Vector([0.0, 0.0, 0.0])

        steps = 96
        dstep = 1.0 / float(steps)

        inside = (0.0 <= u <= 1.0) and (0.0 <= v <= 1.0)
        fade = smoothstep_f(0.0, 0.05, u) * smoothstep_f(1.0, 0.95, u) * \
               smoothstep_f(0.0, 0.05, v) * smoothstep_f(1.0, 0.95, v)

        if inside:
            for s in range(steps):
                y = float(s) * dstep
                x = u
                z = 1.0 - v

                d_val = sample_trilinear(grid_dens, x, y, z) * d_scale
                ext = d_val * dstep * 18.0 * fade
                step_alpha = 1.0 - ti.exp(-ext)

                smoke_accum += d_val * trans * dstep * 16.0 * fade
                shadow_accum += d_val * (0.35 + 0.65 * trans) * dstep * 16.0 * fade

                delta = 1.0 / float(VRES)
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
                if trans < 0.004:
                    break

        coverage = smoothstep_f(0.015, 0.12, 1.0 - trans) * fade
        g_val = ti.math.clamp(smoke_accum * 0.90, 0.0, 1.0)
        b_val = ti.math.clamp(shadow_accum * 0.70, 0.0, 1.0)

        # R is strictly 0.0 (PURE COLD SMOKE!)
        frame_color[px, py] = ti.Vector([0.0, g_val, b_val, coverage])

        an_len = accum_norm.norm()
        n_unit = accum_norm / an_len if an_len > 1e-4 else ti.Vector([0.0, 0.0, 1.0])
        edge_blend = smoothstep_f(0.01, 0.35, coverage)
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
    print("Testing continuous smoke roil...")
    init_particles(42.0)
    # Warm up convective roll
    for _ in range(30):
        step_particles(0.04, 0.0)

    # March 4 test frames: 0, 8, 16, 24
    frames = []
    t0 = time.time()
    for f in range(32):
        sim_time = float(f) * 0.04
        for _ in range(2):
            step_particles(0.02, sim_time)

        if f in [0, 8, 16, 24]:
            clear_grid()
            splat_to_grid(0.85)
            raymarch_smoke(1.10, 1.20)

            c_arr = frame_color.to_numpy().transpose(1, 0, 2)
            c_u8 = (np.clip(c_arr, 0.0, 1.0) * 255.0).astype(np.uint8)
            Image.fromarray(c_u8, "RGBA").save(f"scratch/smoke_roil_f{f:02d}.png")
            frames.append(c_u8)
            print(f"Saved scratch/smoke_roil_f{f:02d}.png in {time.time() - t0:.2f}s")

    diff_0_8 = np.abs(frames[1].astype(float) - frames[0].astype(float)).mean()
    diff_8_16 = np.abs(frames[2].astype(float) - frames[1].astype(float)).mean()
    print(f"Motion difference 0 vs 8: {diff_0_8:.2f}, 8 vs 16: {diff_8_16:.2f}")


if __name__ == "__main__":
    main()
