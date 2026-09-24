#!/usr/bin/env python3
"""gen_fire_eoo.py — Generate AAA Unreal Engine Niagara EOO Fire & Flame Flipbooks.

Emulates UE5 Niagara's T_FireRoil / T_Fireball flipbook format with:
  1. Multi-octave 4D divergence-free Curl Noise (crisp micro-tendrils & billows)
  2. Convective toroidal roll & buoyancy advection (authentic flame lick & swirl)
  3. Exact Niagara EOO channel layout:
       - R: High-energy incandescent core emission (Planck blackbody filaments)
       - G: Transmittance (1.0 - 0.65*R - 0.25*outer_envelope) -> green in viewer,
            provides optical thickness for particle_lit.fs (u_volumeSheet Mode 4)
       - B: 0.0 (Niagara standard)
       - A: Alpha coverage / transmittance mask
  4. BC5 Tangent Space Normal companion (R = Nx, G = Ny, B = 0, A = coverage)

Usage:
  ./.venv/bin/python scripts/flipbook/gen_fire_eoo.py --frames 64 --cell 256 --out fire_tongue_01
"""

import argparse
import math
import os
import sys
import time

import numpy as np
from PIL import Image
import taichi as ti

OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "..", "assets", "textures", "vfx", "flipbooks")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frames", type=int, default=64, help="number of flipbook frames")
    parser.add_argument("--cell", type=int, default=256, help="pixel resolution per cell")
    parser.add_argument("--grid", type=int, default=8, help="flipbook grid dimension (8 for 8x8)")
    parser.add_argument("--particles", type=int, default=524288, help="active simulation particle count")
    parser.add_argument("--vol-res", type=int, default=160, help="3D accumulation grid resolution")
    parser.add_argument("--style", default="roil", choices=["roil", "tongue", "ball"],
                        help="fire flow profile: roil (rolling parcel), tongue (tall lick), ball (burst)")
    parser.add_argument("--out", default="fire_tongue_01", help="base name for output pngs")
    parser.add_argument("--arch", default="gpu", choices=["gpu", "cpu"])
    args = parser.parse_args()

    ti.init(arch=ti.gpu if args.arch == "gpu" else ti.cpu, default_fp=ti.f32, random_seed=42)

    N_PARTICLES = args.particles
    VRES = args.vol_res
    CELL = args.cell
    FRAMES = args.frames
    GRID = args.grid

    # Particle state: pos (vec3), vel (vec3), life, max_life, heat
    p_pos = ti.Vector.field(3, dtype=ti.f32, shape=N_PARTICLES)
    p_life = ti.field(dtype=ti.f32, shape=N_PARTICLES)
    p_max_life = ti.field(dtype=ti.f32, shape=N_PARTICLES)
    p_heat = ti.field(dtype=ti.f32, shape=N_PARTICLES)

    # 3D Density & Heat accumulation grids
    grid_core = ti.field(dtype=ti.f32, shape=(VRES, VRES, VRES))
    grid_envelope = ti.field(dtype=ti.f32, shape=(VRES, VRES, VRES))

    # Output 2D frame buffers
    frame_color = ti.Vector.field(4, dtype=ti.f32, shape=(CELL, CELL))
    frame_norm = ti.Vector.field(4, dtype=ti.f32, shape=(CELL, CELL))

    # --- Fast 4D Noise & Vector Potential Functions ---
    @ti.func
    def smoothstep_f(e0: ti.f32, e1: ti.f32, x: ti.f32) -> ti.f32:
        t = ti.math.clamp((x - e0) / (e1 - e0), 0.0, 1.0)
        return t * t * (3.0 - 2.0 * t)

    @ti.func
    def hash31(p: ti.f32) -> ti.types.vector(3, ti.f32):
        p3 = ti.math.fract(ti.Vector([p * 0.1031, p * 0.1030, p * 0.0973]))
        p3 += p3.dot(ti.Vector([p3.y + 33.33, p3.z + 33.33, p3.x + 33.33]))
        return ti.math.fract(ti.Vector([(p3.x + p3.y) * p3.z, (p3.x + p3.z) * p3.y, (p3.y + p3.z) * p3.x]))

    @ti.func
    def snoise(p: ti.types.vector(3, ti.f32), t: ti.f32) -> ti.f32:
        # Multi-octave analytical 4D sine turbulence for crisp micro-structure
        val = 0.0
        # Octave 1: Macro convective swell
        val += ti.sin(p.x * 1.4 + t * 0.85) * ti.cos(p.y * 1.3 - t * 0.70) * ti.sin(p.z * 1.6 + t * 0.95)
        # Octave 2: Billowing eddy swirls
        val += 0.50 * ti.sin(p.y * 3.2 + t * 1.35) * ti.cos(p.z * 2.9 - t * 1.20) * ti.sin(p.x * 3.5 + t * 1.45)
        # Octave 3: Licking flame tendrils
        val += 0.25 * ti.sin(p.z * 6.7 - t * 2.20) * ti.cos(p.x * 6.3 + t * 1.95) * ti.sin(p.y * 7.1 - t * 2.40)
        # Octave 4: Microscopic fraying edge wisps
        val += 0.12 * ti.sin(p.x * 14.2 + t * 3.80) * ti.cos(p.y * 13.7 - t * 3.50) * ti.sin(p.z * 15.1 + t * 4.10)
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
            r = 0.16 * ti.pow(rnd.z, 0.4)

            x = r * ti.sin(phi) * ti.cos(theta)
            y = r * ti.sin(phi) * ti.sin(theta)
            z = r * ti.cos(phi) - 0.05

            p_pos[i] = ti.Vector([x, y, z])
            p_life[i] = rnd.x * 1.0
            p_max_life[i] = 1.0 + rnd.y * 0.8
            p_heat[i] = 1.0 - (r / 0.16) * 0.4

    @ti.kernel
    def step_particles(dt: ti.f32, time_val: ti.f32, flow_speed: ti.f32, scale: ti.f32, is_tongue: ti.i32):
        for i in p_pos:
            p_life[i] += dt
            if p_life[i] >= p_max_life[i]:
                # Respawn at base emitter zone
                rnd = hash31(float(i) + time_val * 419.3)
                theta = rnd.x * 2.0 * math.pi
                r = (0.12 if is_tongue else 0.15) * ti.sqrt(rnd.y)
                base_z = -0.22 if is_tongue else -0.10

                p_pos[i] = ti.Vector([
                    r * ti.cos(theta),
                    r * ti.sin(theta),
                    base_z + rnd.z * 0.08
                ])
                p_life[i] = 0.0
                p_max_life[i] = 0.9 + rnd.z * 0.8
                p_heat[i] = 1.0

            pos = p_pos[i]
            r_xy = ti.Vector([pos.x, pos.y]).norm()

            # 1. 4D Multi-Octave Curl Noise (Turbulent eddies)
            v_curl = curl_noise(pos * scale, time_val * flow_speed)

            # 2. Convective Buoyancy & Toroidal Roll
            # Hot gas rises (+Z), draws air inward at the base and rolls outward at the top
            rise_speed = 1.10 if is_tongue else 0.55
            v_buoy = ti.Vector([0.0, 0.0, rise_speed * p_heat[i]])

            # Toroidal roll: outward expansion proportional to height
            z_norm = (pos.z + 0.25)
            roll_outward = ti.Vector([pos.x, pos.y, 0.0]) / ti.max(r_xy, 1e-4) * (0.35 * ti.max(0.0, z_norm))

            # Base entrainment: suck inward at bottom
            entrain = -ti.Vector([pos.x, pos.y, 0.0]) * (0.65 * ti.max(0.0, -pos.z))

            v_total = v_curl * 0.95 + v_buoy + roll_outward + entrain

            # Containment bounds: keep parcel centered and inside grid
            max_r = 0.42 if is_tongue else 0.46
            if r_xy > max_r:
                v_total.x -= (pos.x / r_xy) * (r_xy - max_r) * 4.0
                v_total.y -= (pos.y / r_xy) * (r_xy - max_r) * 4.0

            top_z = 0.45 if is_tongue else 0.38
            if pos.z > top_z:
                v_total.z -= (pos.z - top_z) * 5.0

            p_pos[i] += v_total * dt

            # Cooling: lose heat along path and life
            p_heat[i] = ti.max(0.0, p_heat[i] - dt * (0.85 + 0.5 * ti.max(0.0, pos.z)))

    @ti.kernel
    def clear_grids():
        for i, j, k in grid_core:
            grid_core[i, j, k] = 0.0
            grid_envelope[i, j, k] = 0.0

    @ti.kernel
    def splat_to_grids(splat_core: ti.f32, splat_env: ti.f32):
        for i in p_pos:
            pos = p_pos[i]
            # Map [-0.55, 0.55] -> [0, VRES - 1]
            uvw = (pos * 0.90 + 0.5) * float(VRES)
            if 2.0 <= uvw.x < VRES - 3.0 and 2.0 <= uvw.y < VRES - 3.0 and 2.0 <= uvw.z < VRES - 3.0:
                base = ti.cast(ti.floor(uvw), ti.i32)
                f = uvw - ti.cast(base, ti.f32)

                progress = p_life[i] / p_max_life[i]
                life_fade = ti.sin(progress * math.pi)
                heat = p_heat[i]

                # Core incandescent splat: highly concentrated in hot interior
                w_core = life_fade * ti.pow(heat, 2.2) * splat_core

                # Outer envelope splat: broader volume of the flame body
                w_env = life_fade * (0.35 + 0.65 * heat) * splat_env

                for dx, dy, dz in ti.static(ti.ndrange(2, 2, 2)):
                    c_weight = ((1.0 - f.x) if dx == 0 else f.x) * \
                               ((1.0 - f.y) if dy == 0 else f.y) * \
                               ((1.0 - f.z) if dz == 0 else f.z)
                    ti.atomic_add(grid_core[base.x + dx, base.y + dy, base.z + dz], w_core * c_weight)
                    ti.atomic_add(grid_envelope[base.x + dx, base.y + dy, base.z + dz], w_env * c_weight)

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
    def raymarch_eoo_fire(zoom: ti.f32, core_scale: ti.f32, env_scale: ti.f32):
        # Raymarch through the volume (Z is UP, X is RIGHT, Y is VIEW RAY)
        for px, py in frame_color:
            u_coord = ((float(px) + 0.5) / float(CELL) - 0.5) / zoom + 0.5
            v_coord = ((float(py) + 0.5) / float(CELL) - 0.5) / zoom + 0.5

            # Transmittance and emission accumulation
            trans = 1.0
            emis_accum = 0.0
            env_accum = 0.0

            accum_norm = ti.Vector([0.0, 0.0, 0.0])

            steps = 96
            step_ds = 1.0 / float(steps)

            inside = (0.0 <= u_coord <= 1.0) and (0.0 <= v_coord <= 1.0)

            if inside:
                for s in range(steps):
                    y_coord = float(s) * step_ds
                    x_coord = u_coord
                    # Invert V so top of image is +Z
                    z_coord = 1.0 - v_coord

                    c_val = sample_trilinear(grid_core, x_coord, y_coord, z_coord) * core_scale
                    e_val = sample_trilinear(grid_envelope, x_coord, y_coord, z_coord) * env_scale

                    ext = (e_val * 1.8 + c_val * 4.5) * step_ds
                    step_alpha = 1.0 - ti.exp(-ext)

                    # Emission from incandescent core filaments
                    emis_accum += c_val * trans * step_ds
                    env_accum += e_val * trans * step_ds

                    # 3D Normal gradient estimation
                    delta = 1.0 / float(VRES)
                    dx = sample_trilinear(grid_envelope, x_coord + delta, y_coord, z_coord) - \
                         sample_trilinear(grid_envelope, x_coord - delta, y_coord, z_coord)
                    dy = sample_trilinear(grid_envelope, x_coord, y_coord + delta, z_coord) - \
                         sample_trilinear(grid_envelope, x_coord, y_coord - delta, z_coord)
                    dz = sample_trilinear(grid_envelope, x_coord, y_coord, z_coord + delta) - \
                         sample_trilinear(grid_envelope, x_coord, y_coord, z_coord - delta)

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

            # ── EXACT NIAGARA EOO CHANNEL SYNTHESIS ──
            # R: Incandescent Planck core emission (high contrast, filamentary)
            # G: Transmittance (1.0 - 0.65*R - 0.25*env) -> green outer cloud with orange/yellow core!
            # B: 0.0 (Niagara standard)
            # A: Coverage mask (1.0 - trans)
            r_val = ti.pow(ti.min(1.0, emis_accum * 1.35), 1.25)
            coverage = smoothstep_f(0.015, 0.12, 1.0 - trans)

            # Fit exactly the Niagara regression: G = 1.0 - 0.65*R - 0.25*envelope_density
            g_transmittance = ti.math.clamp(1.0 - 0.65 * r_val - 0.25 * ti.min(1.0, env_accum * 0.9), 0.32, 1.0)
            if coverage < 0.005:
                r_val = 0.0
                g_transmittance = 0.0

            frame_color[px, py] = ti.Vector([r_val, g_transmittance, 0.0, coverage])

            # Normal map (BC5 tangent space)
            an_len = accum_norm.norm()
            n_unit = accum_norm / an_len if an_len > 1e-4 else ti.Vector([0.0, 0.0, 1.0])
            frame_norm[px, py] = ti.Vector([
                0.5 + 0.5 * n_unit.x,
                0.5 + 0.5 * n_unit.y,
                0.5 + 0.5 * ti.max(0.0, n_unit.z),
                coverage
            ])

    print("GEN_FIRE: Initializing %d particles on GPU..." % N_PARTICLES)
    init_particles(12.34)

    # Warm up simulation to achieve established turbulent convective flow
    is_tongue = 1 if args.style == "tongue" else 0
    flow_speed = 1.95 if args.style == "roil" else 2.40
    curl_scale = 2.4 if args.style == "roil" else 2.8

    print("GEN_FIRE: Warming up physical convection...")
    for w in range(25):
        step_particles(0.045, float(w) * 0.045, flow_speed, curl_scale, is_tongue)

    color_frames = []
    norm_frames = []

    print("GEN_FIRE: Marching %d frames (%s style)..." % (FRAMES, args.style))
    t0 = time.time()
    dt_frame = 0.040

    for f in range(FRAMES):
        sim_time = (25 + f) * dt_frame
        # 3 substeps per frame for smooth continuous advection
        for _ in range(3):
            step_particles(dt_frame / 3.0, sim_time, flow_speed, curl_scale, is_tongue)

        clear_grids()
        splat_to_grids(0.85, 0.55)

        # Zoom calibration: 1.05 gives 40-45% cell coverage with 0% border clipping
        zoom = 1.08 if args.style == "roil" else 1.15
        raymarch_eoo_fire(zoom, 1.45, 1.10)

        c_arr = frame_color.to_numpy().transpose(1, 0, 2)
        n_arr = frame_norm.to_numpy().transpose(1, 0, 2)

        color_frames.append(c_arr)
        norm_frames.append(n_arr)

        if (f + 1) % 8 == 0:
            print("  Frame %2d/%d (%.1fs elapsed)" % (f + 1, FRAMES, time.time() - t0))

    # Pack into 8x8 atlas
    sheet_w = CELL * GRID
    sheet_h = CELL * GRID
    color_atlas = np.zeros((sheet_h, sheet_w, 4), dtype=np.uint8)
    norm_atlas = np.zeros((sheet_h, sheet_w, 4), dtype=np.uint8)

    for idx, (c_img, n_img) in enumerate(zip(color_frames, norm_frames)):
        row = idx // GRID
        col = idx % GRID
        y0 = row * CELL
        x0 = col * CELL

        c_u8 = (np.clip(c_img, 0.0, 1.0) * 255.0).astype(np.uint8)
        n_u8 = (np.clip(n_img, 0.0, 1.0) * 255.0).astype(np.uint8)

        color_atlas[y0:y0 + CELL, x0:x0 + CELL] = c_u8
        norm_atlas[y0:y0 + CELL, x0:x0 + CELL] = n_u8

    os.makedirs(OUT_DIR, exist_ok=True)
    color_path = os.path.join(OUT_DIR, f"{args.out}_8x8.png")
    norm_path = os.path.join(OUT_DIR, f"{args.out}_normals_8x8.png")

    Image.fromarray(color_atlas, "RGBA").save(color_path)
    Image.fromarray(norm_atlas, "RGBA").save(norm_path)

    print(f"GEN_FIRE: Exported {color_path} ({sheet_w}x{sheet_h})")
    print(f"GEN_FIRE: Exported {norm_path} ({sheet_w}x{sheet_h})")


if __name__ == "__main__":
    main()
