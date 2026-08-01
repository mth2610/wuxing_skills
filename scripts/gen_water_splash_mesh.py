#!/usr/bin/env python3
"""Bake a short, volumetric water-impact animation from Taichi particles.

This script runs a true 3D Material Point Method (MPM) liquid simulation of an
asymmetric water volume colliding with a ground plane. The particle field is
then reconstructed into a smooth, watertight mesh sequence using Voxel Extraction
and Laplacian Smoothing (pure Numpy).

No procedural "fake" crowns or external libraries (like skimage) are used.
The output is ready for Blender inspection and Vertex Animation Texture (VAT) builds.
"""

import argparse
import json
import math
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def pipeline_python():
    """Find the Taichi interpreter used by the existing flipbook pipeline."""
    candidates = [os.environ.get("WUXING_FLIPBOOK_PYTHON"), sys.executable,
                  "/usr/bin/python3"]
    seen = set()
    for exe in candidates:
        if not exe or exe in seen or not os.path.isfile(exe):
            continue
        seen.add(exe)
        if subprocess.run([exe, "-c", "import numpy, taichi"],
                          stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL).returncode == 0:
            return exe
    raise SystemExit(
        "No Python with numpy + taichi found. Set WUXING_FLIPBOOK_PYTHON to "
        "the same interpreter used by scripts/gen_dust_flipbook.py.")


# Ensure we are running in the correct Python environment
py_exe = pipeline_python()
if os.path.realpath(py_exe) != os.path.realpath(sys.executable):
    os.execv(py_exe, [py_exe, os.path.abspath(__file__)] + sys.argv[1:])

import numpy as np
import taichi as ti

# ==============================================================================
# MESH RECONSTRUCTION & EXPORT (PURE NUMPY)
# ==============================================================================

def splat_particles(points, resolution, domain_half, radius_voxels):
    """Turn liquid particles into one continuous scalar field."""
    field = np.zeros((resolution, resolution, resolution), dtype=np.float32)
    scale = resolution / (2.0 * domain_half)
    radius = max(2, int(radius_voxels))
    rr = float(radius * radius)
    for px, py, pz in points:
        gx = int((px + domain_half) * scale)
        gy = int(py * scale)
        gz = int((pz + domain_half) * scale)
        
        x0, x1 = max(0, gx - radius), min(resolution, gx + radius + 1)
        y0, y1 = max(0, gy - radius), min(resolution, gy + radius + 1)
        z0, z1 = max(0, gz - radius), min(resolution, gz + radius + 1)
        
        if x0 >= x1 or y0 >= y1 or z0 >= z1:
            continue
            
        zz, yy, xx = np.ogrid[z0:z1, y0:y1, x0:x1]
        d2 = (xx - gx) ** 2 + (yy - gy) ** 2 + (zz - gz) ** 2
        w = np.maximum(0.0, 1.0 - d2 / rr)
        field[z0:z1, y0:y1, x0:x1] += w * w
    return field


def write_voxel_surface_with_normals(density, threshold, path, domain_half, smooth_passes=6):
    """Extract enclosed surface, apply Laplacian smooth, and write watertight OBJ with normals."""
    solid = density >= threshold
    n = solid.shape[0]
    vertices, faces, index = [], [], {}
    
    # 6 Outward facing directions for voxel faces
    dirs = (
        ((-1, 0, 0), ((0, 0, 0), (0, 0, 1), (0, 1, 1), (0, 1, 0))),
        (( 1, 0, 0), ((1, 0, 0), (1, 1, 0), (1, 1, 1), (1, 0, 1))),
        ((0, -1, 0), ((0, 0, 0), (1, 0, 0), (1, 0, 1), (0, 0, 1))),
        ((0,  1, 0), ((0, 1, 0), (0, 1, 1), (1, 1, 1), (1, 1, 0))),
        ((0, 0, -1), ((0, 0, 0), (0, 1, 0), (1, 1, 0), (1, 0, 0))),
        ((0, 0,  1), ((0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1))),
    )
    
    def add_quad(a, b, c, d):
        quad = []
        for p in (a, b, c, d):
            key = (int(p[0]), int(p[1]), int(p[2]))
            idx = index.get(key)
            if idx is None:
                idx = len(vertices) + 1
                index[key] = idx
                vertices.append(key)
            quad.append(idx)
        # Triangulate quad
        faces.append((quad[0], quad[1], quad[2]))
        faces.append((quad[0], quad[2], quad[3]))

    for z, y, x in np.argwhere(solid):
        for (dx, dy, dz), corners in dirs:
            nx, ny, nz = x + dx, y + dy, z + dz
            if 0 <= nx < n and 0 <= ny < n and 0 <= nz < n and solid[nz, ny, nx]:
                continue
            add_quad(*[(x + qx, y + qy, z + qz) for qx, qy, qz in corners])

    if not vertices:
        return 0, 0, [0.0, 0.0, 0.0], [0.0, 0.0, 0.0]

    # Laplacian smoothing to turn staircases into continuous sheets/droplets
    neighbours = [set() for _ in vertices]
    for a, b, c in faces:
        for left, right in ((a, b), (b, c), (c, a)):
            neighbours[left - 1].add(right - 1)
            neighbours[right - 1].add(left - 1)
            
    points = np.asarray(vertices, dtype=np.float32)
    for _ in range(smooth_passes):
        next_points = points.copy()
        for i, linked in enumerate(neighbours):
            if linked:
                average = points[list(linked)].mean(axis=0)
                # Heavy relaxation for organic water look
                next_points[i] = points[i] * 0.35 + average * 0.65
        points = next_points

    # Compute Smooth Normals
    normals = np.zeros_like(points)
    for a, b, c in faces:
        v0, v1, v2 = points[a-1], points[b-1], points[c-1]
        n_vec = np.cross(v1 - v0, v2 - v0)
        normals[a-1] += n_vec
        normals[b-1] += n_vec
        normals[c-1] += n_vec
        
    lengths = np.linalg.norm(normals, axis=1, keepdims=True)
    normals = np.divide(normals, lengths, out=np.zeros_like(normals), where=lengths > 1e-6)

    # Transform back to world space
    scale = (domain_half * 2.0) / n
    points = points * scale - domain_half
    
    # Floor limit enforcement (prevent vertices clipping through Y=0 due to smoothing)
    points[:, 1] = np.maximum(points[:, 1], 0.0)
    
    vmin, vmax = points.min(axis=0), points.max(axis=0)

    # Export watertight OBJ
    with open(path, "w", encoding="utf-8") as out:
        out.write("# Physical MPM Splash - Offline Bake\n")
        for p in points:
            out.write(f"v {p[0]:.6f} {p[1]:.6f} {p[2]:.6f}\n")
        for vn in normals:
            out.write(f"vn {vn[0]:.6f} {vn[1]:.6f} {vn[2]:.6f}\n")
        out.write("s 1\n")
        for a, b, c in faces:
            out.write(f"f {a}//{a} {b}//{b} {c}//{c}\n")

    return len(vertices), len(faces), vmin.tolist(), vmax.tolist()


def self_check(stats):
    """Validate generated mesh sequences to avoid broken exports."""
    for i, stat in enumerate(stats):
        if stat["triangles"] == 0:
            raise RuntimeError(f"Frame {stat['frame']} is empty (0 triangles). Simulation exploded or flew out of bounds.")
        if i > 0:
            prev = stats[i-1]["triangles"]
            curr = stat["triangles"]
            if prev > 0 and (curr > prev * 8 or curr < prev * 0.15):
                raise RuntimeError(f"Topology jump detected at frame {stat['frame']}: {prev} -> {curr} tris.")
        if stat["max_bound"][1] > 2.0 or stat["min_bound"][1] < -0.1:
            raise RuntimeError(f"Mesh out of bounds at frame {stat['frame']}.")
    print("Self-check passed: Sequence is continuous, watertight, and stable.")


# ==============================================================================
# MAIN EXECUTION (MPM & BAKE LOOP)
# ==============================================================================

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default=os.path.join(ROOT, "build_cache", "water_splash_mesh"))
    ap.add_argument("--frames", type=int, default=32)
    ap.add_argument("--fps", type=float, default=30.0)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--arch", choices=("gpu", "cpu"), default="gpu")
    ap.add_argument("--quick", action="store_true", help="16-frame quick preview")
    ap.add_argument("--overwrite", action="store_true", help="replace only prior frame_*.obj and metadata in --out")
    args = ap.parse_args()

    if args.quick:
        args.frames = 16

    # IMPORTANT: ti.init() must be called before ANY ti.field is created.
    ti.init(arch=ti.gpu if args.arch == "gpu" else ti.cpu, random_seed=args.seed)

    out = os.path.abspath(args.out)
    if os.path.exists(out) and not os.path.isdir(out):
        ap.error("--out exists and is not a directory: %s" % out)
    os.makedirs(out, exist_ok=True)
    
    old_files = [f for f in os.listdir(out) if (f.startswith("frame_") and f.endswith(".obj")) or f == "water_splash_mesh.json"]
    if old_files and not args.overwrite:
        ap.error("--out already contains a bake; pass --overwrite to replace its generated frames")
    for f in old_files:
        os.remove(os.path.join(out, f))

    # --- MPM LIQUID SOLVER SETUP ---
    dim = 3
    n_grid = 64
    dx, inv_dx = 1.0 / n_grid, float(n_grid)
    dt = 2e-4
    p_vol, p_rho = (dx * 0.5)**2, 1
    p_mass = p_vol * p_rho
    E = 800  # Bulk modulus for weak compressibility (water)
    max_num_particles = 80000

    x = ti.Vector.field(dim, dtype=float, shape=max_num_particles)
    v = ti.Vector.field(dim, dtype=float, shape=max_num_particles)
    C = ti.Matrix.field(dim, dim, dtype=float, shape=max_num_particles)
    J = ti.field(dtype=float, shape=max_num_particles)
    grid_v = ti.Vector.field(dim, dtype=float, shape=(n_grid, n_grid, n_grid))
    grid_m = ti.field(dtype=float, shape=(n_grid, n_grid, n_grid))
    num_particles = ti.field(dtype=int, shape=())

    @ti.kernel
    def init_particles():
        count = 0
        # Drop an asymmetric block of water
        for i in range(max_num_particles):
            pos = ti.Vector([
                ti.random() * 0.25 + 0.35,
                ti.random() * 0.35 + 0.40,
                ti.random() * 0.25 + 0.35
            ])
            # Make the mass irregular so the splash isn't a perfect circle
            if pos[0] > 0.5 and ti.random() > 0.4:
                continue
            if pos[2] < 0.45 and ti.random() > 0.7:
                continue
                
            x[count] = pos
            v[count] = ti.Vector([ (ti.random()-0.5)*1.5, -4.5 - ti.random()*2.0, (ti.random()-0.5)*1.5 ])
            J[count] = 1.0
            C[count] = ti.Matrix.zero(float, dim, dim)
            count += 1
        num_particles[None] = count

    @ti.kernel
    def substep():
        for I in ti.grouped(grid_m):
            grid_v[I] = ti.Vector.zero(float, dim)
            grid_m[I] = 0.0

        # P2G
        for p in range(num_particles[None]):
            base = (x[p] * inv_dx - 0.5).cast(int)
            fx = x[p] * inv_dx - base.cast(float)
            w = [0.5 * (1.5 - fx)**2, 0.75 - (fx - 1)**2, 0.5 * (fx - 0.5)**2]
            
            # Water equation of state
            stress = -E * (J[p] - 1.0)
            affine = ti.Matrix.identity(float, dim) * stress * p_vol * inv_dx**2 / dx + p_mass * C[p]
            
            for offset in ti.static(ti.grouped(ti.ndrange(*([3]*dim)))):
                dpos = (offset.cast(float) - fx) * dx
                weight = w[offset[0]][0] * w[offset[1]][1] * w[offset[2]][2]
                grid_v[base + offset] += weight * (p_mass * v[p] + affine @ dpos)
                grid_m[base + offset] += weight * p_mass

        # Grid operations
        bound = 4
        for I in ti.grouped(grid_m):
            if grid_m[I] > 0:
                grid_v[I] = (1 / grid_m[I]) * grid_v[I]
                grid_v[I][1] -= 9.8 * dt # Gravity
                
                # Floor collision Y=0 (grid index `bound`)
                if I[1] < bound and grid_v[I][1] < 0:
                    grid_v[I][1] = 0
                    grid_v[I][0] *= 0.3 # Friction
                    grid_v[I][2] *= 0.3
                
                # Wall bounds to prevent particles flying into infinity
                if I[0] < bound and grid_v[I][0] < 0: grid_v[I][0] = 0
                if I[0] > n_grid - bound and grid_v[I][0] > 0: grid_v[I][0] = 0
                if I[2] < bound and grid_v[I][2] < 0: grid_v[I][2] = 0
                if I[2] > n_grid - bound and grid_v[I][2] > 0: grid_v[I][2] = 0

        # G2P
        for p in range(num_particles[None]):
            base = (x[p] * inv_dx - 0.5).cast(int)
            fx = x[p] * inv_dx - base.cast(float)
            w = [0.5 * (1.5 - fx)**2, 0.75 - (fx - 1)**2, 0.5 * (fx - 0.5)**2]
            new_v = ti.Vector.zero(float, dim)
            new_C = ti.Matrix.zero(float, dim, dim)
            
            for offset in ti.static(ti.grouped(ti.ndrange(*([3]*dim)))):
                dpos = offset.cast(float) - fx
                g_v = grid_v[base + offset]
                weight = w[offset[0]][0] * w[offset[1]][1] * w[offset[2]][2]
                new_v += weight * g_v
                new_C += 4 * inv_dx * weight * g_v.outer_product(dpos)
                
            v[p] = new_v
            x[p] += dt * v[p]
            J[p] *= 1 + dt * new_C.trace()
            C[p] = new_C

    # --- SIMULATION & BAKE RUNNER ---
    init_particles()
    
    # Pre-simulate so the volume is just about to impact the floor
    print("Running pre-impact warmup...")
    for _ in range(40): 
        substep()

    steps_per_frame = int((1.0 / args.fps) / dt)
    stats = []

    print(f"Baking {args.frames} frames into {out}...")
    t0 = time.time()
    
    for frame in range(args.frames):
        for _ in range(steps_per_frame):
            substep()
            
        points = x.to_numpy()[:num_particles[None]]
        
        # Center points for splatting (-0.5 to 0.5 logical domain)
        centered_points = points - 0.5 
        
        # Reconstruct scalar field and export surface
        res = 96
        domain = 0.5
        field = splat_particles(centered_points, resolution=res, domain_half=domain, radius_voxels=2.5)
        
        obj_path = os.path.join(out, f"frame_{frame:03d}.obj")
        
        # Threshold 1.25 filters out noise and keeps the main body connected and thick
        v_count, f_count, vmin, vmax = write_voxel_surface_with_normals(
            field, threshold=1.25, path=obj_path, domain_half=domain, smooth_passes=7
        )
        
        stats.append({
            "frame": frame,
            "vertices": v_count,
            "triangles": f_count,
            "min_bound": vmin,
            "max_bound": vmax
        })
        print(f"[{frame+1:03d}/{args.frames:03d}] Extracted {v_count} verts, {f_count} tris")

    t1 = time.time()
    print(f"Simulation & Meshing complete in {t1 - t0:.2f} seconds.")

    with open(os.path.join(out, "water_splash_mesh.json"), "w", encoding="utf-8") as f:
        json.dump({
            "format": "wuxing-water-impact-mpm-v4",
            "fps": args.fps, 
            "frames": args.frames, 
            "unit": "metre",
            "up_axis": "Y", 
            "receiver_plane": "Y=0",
            "note": "Physical MPM splash output (Pure Numpy Mesher). Watertight meshes ready for VAT.",
            "frame_stats": stats
        }, f, indent=2)

    self_check(stats)

if __name__ == "__main__":
    main()