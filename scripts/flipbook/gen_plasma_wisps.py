#!/usr/bin/env python3
"""gen_plasma_wisps.py — Generate AAA Plasma Wisp & Ethereal Energy VFX Flipbooks.

Uses GPU-accelerated particle advection (500k-1M particles) under 4D multi-octave
divergence-free Curl Noise in Taichi, accumulated into a 3D volume grid and raymarched
to produce both:
  1. High-dynamic-range Albedo/Emission flipbook (4096x4096 or 2048x2048 8x8)
  2. 3D Volumetric Tangent Space Normal Map companion

Usage:
  .venv/bin/python scripts/flipbook/gen_plasma_wisps.py --frames 64 --cell 256 --particles 500000 --out plasma_wisps
"""

import argparse
import math
import os
import sys
import time

import numpy as np
from PIL import Image
import taichi as ti

OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "..", "assets", "textures")
CACHE_DIR = os.path.join(os.path.dirname(__file__), "..", "..", "build_cache", "plasma_wisps")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frames", type=int, default=64, help="number of flipbook frames")
    parser.add_argument("--cell", type=int, default=256, help="pixel resolution per cell")
    parser.add_argument("--grid", type=int, default=8, help="flipbook grid dimension (8 for 8x8)")
    parser.add_argument("--particles", type=int, default=524288, help="active particle count")
    parser.add_argument("--vol-res", type=int, default=160, help="3D accumulation grid resolution")
    parser.add_argument("--speed", type=float, default=1.8, help="curl noise flow speed")
    parser.add_argument("--curl-scale", type=float, default=2.2, help="curl noise spatial frequency")
    parser.add_argument("--density-scale", type=float, default=18.0, help="volumetric optical depth multiplier")
    parser.add_argument("--out", default="plasma_wisps", help="base name for output pngs")
    parser.add_argument("--arch", default="gpu", choices=["gpu", "cpu"])
    args = parser.parse_args()

    ti.init(arch=ti.gpu if args.arch == "gpu" else ti.cpu, default_fp=ti.f32)

    N_PARTICLES = args.particles
    VRES = args.vol_res
    CELL = args.cell
    FRAMES = args.frames
    GRID = args.grid

    # Particle state: pos (vec3), vel (vec3), life (0..1), color_weight
    p_pos = ti.Vector.field(3, dtype=ti.f32, shape=N_PARTICLES)
    p_vel = ti.Vector.field(3, dtype=ti.f32, shape=N_PARTICLES)
    p_life = ti.field(dtype=ti.f32, shape=N_PARTICLES)
    p_max_life = ti.field(dtype=ti.f32, shape=N_PARTICLES)

    # 3D Density accumulation grid
    grid_dens = ti.field(dtype=ti.f32, shape=(VRES, VRES, VRES))

    # Output 2D buffers per frame
    frame_color = ti.Vector.field(4, dtype=ti.f32, shape=(CELL, CELL))
    frame_norm = ti.Vector.field(4, dtype=ti.f32, shape=(CELL, CELL))

    # --- Noise & Curl functions in Taichi ---
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
        # Fast 4D analytical multi-sine turbulence
        val = 0.0
        val += ti.sin(p.x * 1.5 + t * 0.8) * ti.cos(p.y * 1.3 - t * 0.6) * ti.sin(p.z * 1.7 + t * 0.9)
        val += 0.5 * ti.sin(p.y * 3.1 + t * 1.2) * ti.cos(p.z * 2.8 - t * 1.1) * ti.sin(p.x * 3.4 + t * 1.3)
        val += 0.25 * ti.sin(p.z * 6.2 - t * 2.1) * ti.cos(p.x * 5.9 + t * 1.8) * ti.sin(p.y * 6.7 - t * 2.3)
        return val

    @ti.func
    def vector_potential(p: ti.types.vector(3, ti.f32), t: ti.f32) -> ti.types.vector(3, ti.f32):
        ax = snoise(p + ti.Vector([12.3, 45.6, 78.9]), t)
        ay = snoise(p + ti.Vector([98.7, 65.4, 32.1]), t + 1.7)
        az = snoise(p + ti.Vector([43.2, 87.6, 19.5]), t + 3.4)
        return ti.Vector([ax, ay, az])

    @ti.func
    def curl_noise(p: ti.types.vector(3, ti.f32), t: ti.f32) -> ti.types.vector(3, ti.f32):
        eps = 0.08
        p_px = vector_potential(p + ti.Vector([eps, 0.0, 0.0]), t)
        p_nx = vector_potential(p - ti.Vector([eps, 0.0, 0.0]), t)
        p_py = vector_potential(p + ti.Vector([0.0, eps, 0.0]), t)
        p_ny = vector_potential(p - ti.Vector([0.0, eps, 0.0]), t)
        p_pz = vector_potential(p + ti.Vector([0.0, 0.0, eps]), t)
        p_nz = vector_potential(p - ti.Vector([0.0, 0.0, eps]), t)

        dAx_dy = (p_py.x - p_ny.x) / (2.0 * eps)
        dAx_dz = (p_pz.x - p_nz.x) / (2.0 * eps)
        dAy_dx = (p_px.y - p_nx.y) / (2.0 * eps)
        dAy_dz = (p_pz.y - p_nz.y) / (2.0 * eps)
        dAz_dx = (p_px.z - p_nx.z) / (2.0 * eps)
        dAz_dy = (p_py.z - p_ny.z) / (2.0 * eps)

        # curl = (dAz/dy - dAy/dz, dAx/dz - dAz/dx, dAy/dx - dAx/dy)
        return ti.Vector([dAz_dy - dAy_dz, dAx_dz - dAz_dx, dAy_dx - dAx_dy])

    @ti.kernel
    def init_particles(seed: ti.f32):
        for i in p_pos:
            rnd = hash31(float(i) + seed * 1000.0)
            theta = rnd.x * 2.0 * math.pi
            phi = ti.acos(2.0 * rnd.y - 1.0)
            r = 0.12 * ti.pow(rnd.z, 0.333)

            x = r * ti.sin(phi) * ti.cos(theta)
            y = r * ti.sin(phi) * ti.sin(theta)
            z = r * ti.cos(phi)

            p_pos[i] = ti.Vector([x, y, z])
            p_vel[i] = ti.Vector([0.0, 0.0, 0.0])
            p_life[i] = rnd.x * 0.8
            p_max_life[i] = 1.2 + rnd.y * 0.8

    @ti.kernel
    def step_particles(dt: ti.f32, time_val: ti.f32, flow_speed: ti.f32, scale: ti.f32):
        for i in p_pos:
            p_life[i] += dt
            if p_life[i] >= p_max_life[i]:
                # Respawn at center with organic elliptical variation
                rnd = hash31(float(i) + time_val * 313.7)
                theta = rnd.x * 2.0 * math.pi
                phi = ti.acos(2.0 * rnd.y - 1.0)
                # Form a rotating core disc
                spin_angle = time_val * 1.5
                c_cos = ti.cos(spin_angle)
                c_sin = ti.sin(spin_angle)
                r = 0.08 * ti.pow(rnd.z, 0.5)

                x0 = r * ti.sin(phi) * ti.cos(theta)
                y0 = r * ti.sin(phi) * ti.sin(theta) * 0.6
                z0 = r * ti.cos(phi)

                p_pos[i] = ti.Vector([
                    x0 * c_cos - z0 * c_sin,
                    y0,
                    x0 * c_sin + z0 * c_cos
                ])
                p_life[i] = 0.0
                p_max_life[i] = 1.0 + rnd.z * 1.0

            # Advect via 4D curl field
            pos = p_pos[i]
            # Center-seeking subtle attraction + outward wisp ejection
            dist = pos.norm()
            outward = (pos / ti.max(dist, 1e-4)) * 0.35

            v_curl = curl_noise(pos * scale, time_val * flow_speed)
            v_total = v_curl * 0.85 + outward * (0.2 + 0.4 * (p_life[i] / p_max_life[i]))
            
            # Smooth containment: restore particles toward center if they wander too far
            if dist > 0.42:
                v_total -= (pos / dist) * (dist - 0.42) * 3.5
            
            p_pos[i] += v_total * dt
            if dist > 0.62:
                p_pos[i] *= 0.95

    @ti.kernel
    def clear_grid():
        for i, j, k in grid_dens:
            grid_dens[i, j, k] = 0.0

    @ti.kernel
    def splat_particles_to_grid(splat_weight: ti.f32):
        for i in p_pos:
            pos = p_pos[i]
            dist = pos.norm()
            # Map [-1.0, 1.0] -> [0, VRES - 1]
            uvw = (pos * 0.65 + 0.5) * float(VRES)
            if 2.0 <= uvw.x < VRES - 3.0 and 2.0 <= uvw.y < VRES - 3.0 and 2.0 <= uvw.z < VRES - 3.0:
                base = ti.cast(ti.floor(uvw), ti.i32)
                f = uvw - ti.cast(base, ti.f32)
                # Fade at start and end of particle life + smooth radial boundary falloff
                progress = p_life[i] / p_max_life[i]
                life_fade = ti.sin(progress * math.pi)
                radial_fade = smoothstep_f(0.60, 0.42, dist)
                w = life_fade * radial_fade * splat_weight

                # Trilinear splatting
                for dx, dy, dz in ti.static(ti.ndrange(2, 2, 2)):
                    c_weight = ((1.0 - f.x) if dx == 0 else f.x) * \
                               ((1.0 - f.y) if dy == 0 else f.y) * \
                               ((1.0 - f.z) if dz == 0 else f.z)
                    ti.atomic_add(grid_dens[base.x + dx, base.y + dy, base.z + dz], w * c_weight)

    @ti.func
    def sample_grid(p: ti.types.vector(3, ti.f32)) -> ti.f32:
        p_c = ti.math.clamp(p, 1.0, VRES - 2.001)
        base = ti.cast(ti.floor(p_c), ti.i32)
        f = p_c - ti.cast(base, ti.f32)
        c00 = grid_dens[base.x, base.y, base.z] * (1.0 - f.x) + grid_dens[base.x + 1, base.y, base.z] * f.x
        c10 = grid_dens[base.x, base.y + 1, base.z] * (1.0 - f.x) + grid_dens[base.x + 1, base.y + 1, base.z] * f.x
        c01 = grid_dens[base.x, base.y, base.z + 1] * (1.0 - f.x) + grid_dens[base.x + 1, base.y, base.z + 1] * f.x
        c11 = grid_dens[base.x, base.y + 1, base.z + 1] * (1.0 - f.x) + grid_dens[base.x + 1, base.y + 1, base.z + 1] * f.x
        c0 = c00 * (1.0 - f.y) + c10 * f.y
        c1 = c01 * (1.0 - f.y) + c11 * f.y
        return c0 * (1.0 - f.z) + c1 * f.z

    @ti.kernel
    def raymarch_plasma(dens_scale: ti.f32):
        for px, py in frame_color:
            u_raw = (float(px) + 0.5) / float(CELL)
            v_raw = (float(py) + 0.5) / float(CELL)

            # Framing with 12% safe padding margin to guarantee 0 at edges
            margin = 0.12
            u = (u_raw - margin) / (1.0 - 2.0 * margin)
            v = (v_raw - margin) / (1.0 - 2.0 * margin)

            trans = 1.0
            accum_rad = ti.Vector([0.0, 0.0, 0.0])
            accum_norm = ti.Vector([0.0, 0.0, 0.0])

            if 0.0 <= u <= 1.0 and 0.0 <= v <= 1.0:
                gx = u * float(VRES - 1)
                gz = (1.0 - v) * float(VRES - 1)

                steps = VRES
                dstep = float(VRES - 1) / float(steps)

                for s in range(steps):
                    gy = float(s) * dstep
                    pos = ti.Vector([gx, gy, gz])
                    d = sample_grid(pos)

                    if d > 0.001:
                        ext = d * dens_scale * (dstep / float(VRES))
                        step_alpha = 1.0 - ti.exp(-ext)
                        w = trans * step_alpha

                        # Central differences 3D gradient for normal
                        dx = sample_grid(pos + ti.Vector([1.0, 0.0, 0.0])) - sample_grid(pos - ti.Vector([1.0, 0.0, 0.0]))
                        dy = sample_grid(pos + ti.Vector([0.0, 1.0, 0.0])) - sample_grid(pos - ti.Vector([0.0, 1.0, 0.0]))
                        dz = sample_grid(pos + ti.Vector([0.0, 0.0, 1.0])) - sample_grid(pos - ti.Vector([0.0, 0.0, 1.0]))

                        # Tangent space normal: [-dx, -dz, +dy]
                        local_n = ti.Vector([-0.5 * dx, -0.5 * dz, 0.5 * dy]).normalized(1e-4)
                        accum_norm += local_n * w

                        # Plasma radiance: celestial cyan core transitioning to ethereal deep violet/blue
                        # Core density d: hot core is white/cyan, wisp tendrils are saturated blue
                        core = ti.min(1.0, d * 0.45)
                        r_col = 0.45 * core * core + 0.15 * d
                        g_col = 0.75 * core + 0.45 * d
                        b_col = 1.00 + 0.35 * d

                        accum_rad += ti.Vector([r_col, g_col, b_col]) * w

                        trans *= (1.0 - step_alpha)
                        if trans < 0.004:
                            break

            total_alpha = 1.0 - trans
            if total_alpha > 0.005:
                # Normalize accumulated tangent normal
                n_len = accum_norm.norm()
                n_unit = accum_norm / n_len if n_len > 1e-4 else ti.Vector([0.0, 0.0, 1.0])
                n_unit.z = ti.max(0.0, n_unit.z)
                n_unit = n_unit.normalized(1e-4)

                frame_norm[px, py] = ti.Vector([
                    0.5 + 0.5 * n_unit.x,
                    0.5 + 0.5 * n_unit.y,
                    0.5 + 0.5 * n_unit.z,
                    total_alpha
                ])
                frame_color[px, py] = ti.Vector([
                    accum_rad.x, accum_rad.y, accum_rad.z, total_alpha
                ])
            else:
                frame_norm[px, py] = ti.Vector([0.5, 0.5, 1.0, 0.0])
                frame_color[px, py] = ti.Vector([0.0, 0.0, 0.0, 0.0])

    print("GEN_PLASMA: Initializing %d particles and 3D grid %dx%dx%d..." % (N_PARTICLES, VRES, VRES, VRES))
    init_particles(2026.0)

    # Warmup solver 15 steps so first frame is already a rich, swirling energy vortex
    print("GEN_PLASMA: Warming up curl turbulence...")
    for _ in range(16):
        step_particles(0.04, 0.5, args.speed, args.curl_scale)

    os.makedirs(CACHE_DIR, exist_ok=True)
    frames_dir = os.path.join(CACHE_DIR, "frames")
    normals_dir = os.path.join(CACHE_DIR, "normals")
    os.makedirs(frames_dir, exist_ok=True)
    os.makedirs(normals_dir, exist_ok=True)

    beauty_atlas = np.zeros((GRID * CELL, GRID * CELL, 4), dtype=np.uint8)
    normal_atlas = np.zeros((GRID * CELL, GRID * CELL, 4), dtype=np.uint8)

    t0 = time.time()
    dt = 0.035

    print("GEN_PLASMA: Generating %d frames..." % FRAMES)
    for f in range(FRAMES):
        sim_time = float(f) * dt

        # Substep particles
        for _ in range(3):
            step_particles(dt / 3.0, sim_time, args.speed, args.curl_scale)

        clear_grid()
        splat_particles_to_grid(0.12)
        raymarch_plasma(args.density_scale)

        # Extract frames (transposed to row-major)
        img_color = frame_color.to_numpy().transpose(1, 0, 2)
        img_norm = frame_norm.to_numpy().transpose(1, 0, 2)

        # Normalize and save single frames
        col_uint8 = np.clip(img_color * 255.0, 0, 255).astype(np.uint8)
        norm_uint8 = np.clip(img_norm * 255.0, 0, 255).astype(np.uint8)

        Image.fromarray(col_uint8, "RGBA").save(os.path.join(frames_dir, "f%03d.png" % (f + 1)))
        Image.fromarray(norm_uint8, "RGBA").save(os.path.join(normals_dir, "f%03d.png" % (f + 1)))

        # Paste into flipbook atlas
        r, c = divmod(f, GRID)
        beauty_atlas[r * CELL:(r + 1) * CELL, c * CELL:(c + 1) * CELL] = col_uint8
        normal_atlas[r * CELL:(r + 1) * CELL, c * CELL:(c + 1) * CELL] = norm_uint8

        if (f + 1) % 8 == 0:
            print("  Frame %d/%d (%.1fs elapsed)" % (f + 1, FRAMES, time.time() - t0))

    # Save final flipbooks to assets/textures/
    beauty_out_path = os.path.join(OUT_DIR, f"{args.out}_8x8.png")
    normal_out_path = os.path.join(OUT_DIR, f"{args.out}_normals_8x8.png")

    Image.fromarray(beauty_atlas, "RGBA").save(beauty_out_path)
    Image.fromarray(normal_atlas, "RGBA").save(normal_out_path)

    print("\nSUCCESS!")
    print("Beauty Flipbook: %s (%dx%d)" % (beauty_out_path, GRID * CELL, GRID * CELL))
    print("Normal Flipbook: %s (%dx%d)" % (normal_out_path, GRID * CELL, GRID * CELL))
    return 0


if __name__ == "__main__":
    sys.exit(main())
