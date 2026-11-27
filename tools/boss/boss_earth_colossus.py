#!/usr/bin/env python3
"""Earth Colossus Boss ("Son Nhac Cu Khoi") — Procedural AAA 20-Bone Mixamo GLB Generator.

RUN:
    python boss_earth_colossus.py
    python boss_earth_colossus.py --height 3.1 --detail high --preview
    python boss_earth_colossus.py --ref-rig human_body.glb --game-out assets/characters/boss_earth.glb

DEPS:
    python -m pip install numpy
OPTIONAL (for --preview):
    python -m pip install vtk pillow

Standalone procedural pipeline — zero external mesh/rig data required.
Outputs a watertight, outward-normal, 20-bone Mixamo T-Pose GLB 2.0 ready for
direct ingestion into C99/Raylib/Vulkan engines matching male_body_v6.py.
"""
import argparse
import json
import struct
from pathlib import Path
import numpy as np


# =============================================================================
# 1. MATH, QUATERNIONS & 3D PROCEDURAL ROCK / VORONOI NOISE (PURE NUMPY)
# =============================================================================

CT = {5120: 'i1', 5121: 'u1', 5122: '<i2', 5123: '<u2', 5125: '<u4', 5126: '<f4'}
DIM = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}


def _smoothstep(a, b, x):
    t = np.clip((x - a) / max(b - a, 1e-12), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def _quat_to_mat(q):
    x, y, z, w = q
    n = (x*x + y*y + z*z + w*w) ** 0.5
    if n > 1e-12:
        x, y, z, w = x / n, y / n, z / n, w / n
    return np.array([
        [1 - 2*(y*y + z*z), 2*(x*y - z*w),     2*(x*z + y*w)],
        [2*(x*y + z*w),     1 - 2*(x*x + z*z), 2*(y*z - x*w)],
        [2*(x*z - y*w),     2*(y*z + x*w),     1 - 2*(x*x + y*y)],
    ], dtype=float)


def _rot_axis_angle(axis, rad):
    ax = np.asarray(axis, dtype=float)
    ax = ax / max(np.linalg.norm(ax), 1e-12)
    c, s = np.cos(rad), np.sin(rad)
    K = np.array([[0.0, -ax[2], ax[1]], [ax[2], 0.0, -ax[0]], [-ax[1], ax[0], 0.0]])
    return np.eye(3) + s * K + (1.0 - c) * (K @ K)


def _rot_euler_xyz(rx_deg=0.0, ry_deg=0.0, rz_deg=0.0):
    Rx = _rot_axis_angle([1, 0, 0], np.radians(rx_deg))
    Ry = _rot_axis_angle([0, 1, 0], np.radians(ry_deg))
    Rz = _rot_axis_angle([0, 0, 1], np.radians(rz_deg))
    return Rz @ Ry @ Rx


def _frame_from_up(up_dir, hint=None):
    """Build right-handed basis [right, fwd, up] where right x fwd = up."""
    u = np.asarray(up_dir, dtype=float)
    u = u / max(np.linalg.norm(u), 1e-12)
    if hint is None:
        h = np.array([0.0, 0.0, 1.0]) if abs(u[2]) < 0.9 else np.array([1.0, 0.0, 0.0])
    else:
        h = np.asarray(hint, dtype=float)
        if abs(np.dot(h / max(np.linalg.norm(h), 1e-12), u)) > 0.98:
            h = np.array([1.0, 0.0, 0.0]) if abs(u[0]) < 0.9 else np.array([0.0, 1.0, 0.0])
    fwd = h - np.dot(h, u) * u
    fwd = fwd / max(np.linalg.norm(fwd), 1e-12)
    right = np.cross(fwd, u)
    right = right / max(np.linalg.norm(right), 1e-12)
    fwd = np.cross(u, right)
    return right, fwd, u


def _hash3_u32(ix, iy, iz, seed=0):
    with np.errstate(over='ignore'):
        h = (
            np.asarray(ix, dtype=np.uint32) * np.uint32(374761393)
            + np.asarray(iy, dtype=np.uint32) * np.uint32(668265263)
            + np.asarray(iz, dtype=np.uint32) * np.uint32(2147483647)
            + np.uint32(seed) * np.uint32(1597334677)
        )
        h = (h ^ (h >> np.uint32(13))) * np.uint32(1274126177)
        h = h ^ (h >> np.uint32(16))
    return h


def _hash3_f(ix, iy, iz, seed=0):
    return _hash3_u32(ix, iy, iz, seed).astype(np.float64) * (1.0 / 4294967295.0)


def _value_noise3(pts, seed=0):
    p = np.asarray(pts, dtype=np.float64)
    i0 = np.floor(p).astype(np.int64)
    f = p - i0
    # Quintic C2 Hermite curve
    u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0)
    ix, iy, iz = i0[:, 0], i0[:, 1], i0[:, 2]
    c000 = _hash3_f(ix,     iy,     iz,     seed)
    c100 = _hash3_f(ix + 1, iy,     iz,     seed)
    c010 = _hash3_f(ix,     iy + 1, iz,     seed)
    c110 = _hash3_f(ix + 1, iy + 1, iz,     seed)
    c001 = _hash3_f(ix,     iy,     iz + 1, seed)
    c101 = _hash3_f(ix + 1, iy,     iz + 1, seed)
    c011 = _hash3_f(ix,     iy + 1, iz + 1, seed)
    c111 = _hash3_f(ix + 1, iy + 1, iz + 1, seed)
    ux, uy, uz = u[:, 0], u[:, 1], u[:, 2]
    x00 = c000 + (c100 - c000) * ux
    x10 = c010 + (c110 - c010) * ux
    x01 = c001 + (c101 - c001) * ux
    x11 = c011 + (c111 - c011) * ux
    y0 = x00 + (x10 - x00) * uy
    y1 = x01 + (x11 - x01) * uy
    return y0 + (y1 - y0) * uz


def _fbm3(pts, seed=0, octaves=4, lacunarity=2.07, gain=0.5):
    total = np.zeros(len(pts), dtype=np.float64)
    amp = 1.0
    freq = 1.0
    norm = 0.0
    for o in range(octaves):
        total += amp * (_value_noise3(pts * freq, seed + o * 101) * 2.0 - 1.0)
        norm += amp
        freq *= lacunarity
        amp *= gain
    return total / max(norm, 1e-12)


def _ridge_fbm3(pts, seed=0, octaves=3, lacunarity=2.12, gain=0.52):
    total = np.zeros(len(pts), dtype=np.float64)
    amp = 1.0
    freq = 1.0
    norm = 0.0
    for o in range(octaves):
        n = _value_noise3(pts * freq, seed + o * 137) * 2.0 - 1.0
        r = 1.0 - np.abs(n)
        total += amp * (r * r)
        norm += amp
        freq *= lacunarity
        amp *= gain
    return total / max(norm, 1e-12)


def _voronoi_rock(pts, cell_size=0.22, seed=0):
    """Vectorized 3D Voronoi returning (f1, edge_dist=f2-f1, cell_id_float)."""
    scaled = np.asarray(pts, dtype=np.float64) / max(cell_size, 1e-6)
    cell = np.floor(scaled).astype(np.int64)
    n = len(pts)
    f1 = np.full(n, 1e9, dtype=np.float64)
    f2 = np.full(n, 1e9, dtype=np.float64)
    cid = np.zeros(n, dtype=np.float64)
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            for dz in (-1, 0, 1):
                nx = cell[:, 0] + dx
                ny = cell[:, 1] + dy
                nz = cell[:, 2] + dz
                jx = 0.14 + 0.72 * _hash3_f(nx, ny, nz, seed + 11)
                jy = 0.14 + 0.72 * _hash3_f(nx, ny, nz, seed + 37)
                jz = 0.14 + 0.72 * _hash3_f(nx, ny, nz, seed + 73)
                fx = nx + jx - scaled[:, 0]
                fy = ny + jy - scaled[:, 1]
                fz = nz + jz - scaled[:, 2]
                d = np.sqrt(fx * fx + fy * fy + fz * fz)
                m1 = d < f1
                m2 = (~m1) & (d < f2)
                f2 = np.where(m1, f1, np.where(m2, d, f2))
                f1 = np.where(m1, d, f1)
                if np.any(m1):
                    cid = np.where(m1, _hash3_f(nx, ny, nz, seed + 199), cid)
    return f1, np.maximum(f2 - f1, 0.0), cid


# =============================================================================
# 2. WATERTIGHT GEOMETRY & NORMAL / UV / SKINNING KERNELS
# =============================================================================

def _ensure_outward_winding(v, f):
    """Flip face winding if closed mesh signed volume is negative."""
    c = v.mean(axis=0, keepdims=True)
    v0 = v[f[:, 0]] - c
    v1 = v[f[:, 1]] - c
    v2 = v[f[:, 2]] - c
    vol = float(np.sum(np.einsum('ij,ij->i', v0, np.cross(v1, v2)))) / 6.0
    if vol < 0.0:
        return f[:, [0, 2, 1]].copy()
    return f


def _compute_smooth_normals(v, f):
    f0, f1, f2 = f[:, 0], f[:, 1], f[:, 2]
    cross = np.cross(v[f1] - v[f0], v[f2] - v[f0])
    n = np.zeros_like(v, dtype=np.float64)
    np.add.at(n, f0, cross)
    np.add.at(n, f1, cross)
    np.add.at(n, f2, cross)
    lens = np.linalg.norm(n, axis=1, keepdims=True)
    bad = (lens[:, 0] < 1e-12)
    if np.any(bad):
        n[bad] = np.array([0.0, 1.0, 0.0])
        lens = np.linalg.norm(n, axis=1, keepdims=True)
    return n / np.maximum(lens, 1e-12)


def _split_flat_normals(v, f, uv, w20, col=None):
    """Unshare triangles for crisp faceted rock/crystal/metal edges."""
    idx = f.ravel()
    v_s = v[idx].copy()
    uv_s = uv[idx].copy()
    w_s = w20[idx].copy()
    col_s = col[idx].copy() if col is not None else None
    f_s = np.arange(len(idx), dtype=np.int32).reshape(-1, 3)
    v0, v1, v2 = v_s[0::3], v_s[1::3], v_s[2::3]
    fn = np.cross(v1 - v0, v2 - v0)
    fn /= np.maximum(np.linalg.norm(fn, axis=1, keepdims=True), 1e-12)
    n_s = np.repeat(fn, 3, axis=0)
    return v_s, n_s, f_s, uv_s, w_s, col_s


def _triplanar_uv(v, n, scale=0.65):
    an = np.abs(n)
    uv = np.zeros((len(v), 2), dtype=np.float64)
    mx = (an[:, 0] >= an[:, 1]) & (an[:, 0] >= an[:, 2])
    my = (an[:, 1] > an[:, 0]) & (an[:, 1] >= an[:, 2])
    mz = ~(mx | my)
    uv[mx] = v[mx][:, [2, 1]] * scale
    uv[my] = v[my][:, [0, 2]] * scale
    uv[mz] = v[mz][:, [0, 1]] * scale
    return uv


def build_lofted_shell(rings, cap_start=True, cap_end=True):
    """Build a watertight lofted mesh from (M, N, 3) cross-section rings."""
    rings = np.asarray(rings, dtype=np.float64)
    M, N, _ = rings.shape
    ring_list = list(rings)

    if cap_start:
        c0 = ring_list[0].mean(axis=0)
        t0 = ring_list[1].mean(axis=0) - c0
        t0 = t0 / max(np.linalg.norm(t0), 1e-12)
        rad0 = float(np.mean(np.linalg.norm(ring_list[0] - c0, axis=1)))
        r_mid = c0 + (ring_list[0] - c0) * 0.62 - t0 * (rad0 * 0.22)
        r_in = c0 + (ring_list[0] - c0) * 0.24 - t0 * (rad0 * 0.34)
        ring_list = [r_in, r_mid] + ring_list

    if cap_end:
        c1 = ring_list[-1].mean(axis=0)
        t1 = c1 - ring_list[-2].mean(axis=0)
        t1 = t1 / max(np.linalg.norm(t1), 1e-12)
        rad1 = float(np.mean(np.linalg.norm(ring_list[-1] - c1, axis=1)))
        r_mid = c1 + (ring_list[-1] - c1) * 0.62 + t1 * (rad1 * 0.22)
        r_in = c1 + (ring_list[-1] - c1) * 0.24 + t1 * (rad1 * 0.34)
        ring_list = ring_list + [r_mid, r_in]

    all_r = np.asarray(ring_list, dtype=np.float64)
    Mr = len(all_r)
    verts = [all_r.reshape(-1, 3)]
    faces = []
    for i in range(Mr - 1):
        base0 = i * N
        base1 = (i + 1) * N
        for j in range(N):
            jn = (j + 1) % N
            a, b = base0 + j, base0 + jn
            c, d = base1 + jn, base1 + j
            faces.append((a, b, c))
            faces.append((a, c, d))

    v_arr = verts[0]
    if cap_start:
        p_start = all_r[0].mean(axis=0)
        t_s = all_r[1].mean(axis=0) - p_start
        p_start = p_start - t_s * 0.35
        idx_s = len(v_arr)
        v_arr = np.vstack([v_arr, p_start[None, :]])
        for j in range(N):
            faces.append((idx_s, (j + 1) % N, j))

    if cap_end:
        p_end = all_r[-1].mean(axis=0)
        t_e = p_end - all_r[-2].mean(axis=0)
        p_end = p_end + t_e * 0.35
        idx_e = len(v_arr)
        v_arr = np.vstack([v_arr, p_end[None, :]])
        b_last = (Mr - 1) * N
        for j in range(N):
            faces.append((idx_e, b_last + j, b_last + (j + 1) % N))

    f_arr = _ensure_outward_winding(v_arr, np.asarray(faces, dtype=np.int32))
    return v_arr, f_arr


def build_beveled_slab(
    size_xyz,
    bevel=0.22,
    sub_div=6,
    bend_x=0.0,
    bend_y=0.0,
    taper_y=(1.0, 1.0),
    taper_z=(1.0, 1.0),
    rock_amp=0.0,
    rock_cell=0.18,
    seed=0,
):
    """Watertight, pole-free subdivided beveled armor/rock slab in local space."""
    n = max(int(sub_div), 2)
    lin = np.linspace(-1.0, 1.0, n + 1)
    uu, vv = np.meshgrid(lin, lin, indexing='ij')
    uu, vv = uu.ravel(), vv.ravel()

    # 6 cube faces with verified right-handed outward winding (u x v == outward normal)
    face_specs = [
        (np.array([ 1.0, 0.0, 0.0]), np.array([0.0, 1.0, 0.0]), np.array([0.0, 0.0, 1.0])),
        (np.array([-1.0, 0.0, 0.0]), np.array([0.0, 0.0, 1.0]), np.array([0.0, 1.0, 0.0])),
        (np.array([ 0.0, 1.0, 0.0]), np.array([0.0, 0.0, 1.0]), np.array([1.0, 0.0, 0.0])),
        (np.array([ 0.0,-1.0, 0.0]), np.array([1.0, 0.0, 0.0]), np.array([0.0, 0.0, 1.0])),
        (np.array([ 0.0, 0.0, 1.0]), np.array([1.0, 0.0, 0.0]), np.array([0.0, 1.0, 0.0])),
        (np.array([ 0.0, 0.0,-1.0]), np.array([0.0, 1.0, 0.0]), np.array([1.0, 0.0, 0.0])),
    ]

    all_v = []
    all_f = []
    stride = n + 1
    quad_f = []
    for i in range(n):
        for j in range(n):
            a = i * stride + j
            b = (i + 1) * stride + j
            c = (i + 1) * stride + (j + 1)
            d = i * stride + (j + 1)
            quad_f.append((a, b, c))
            quad_f.append((a, c, d))
    quad_f = np.asarray(quad_f, dtype=np.int32)

    for k, (origin, ax_u, ax_v) in enumerate(face_specs):
        pts = origin[None, :] + uu[:, None] * ax_u[None, :] + vv[:, None] * ax_v[None, :]
        all_v.append(pts)
        all_f.append(quad_f + k * (stride * stride))

    v_raw = np.vstack(all_v)
    f_raw = np.vstack(all_f)

    # Weld shared cube edges/corners cleanly
    _, uniq_idx, inv = np.unique(np.round(v_raw, 6), axis=0, return_index=True, return_inverse=True)
    v = v_raw[uniq_idx].copy()
    f = inv[f_raw].astype(np.int32)

    # Exact mathematical chamfer/bevel projection on [-1, 1]^3
    b = float(np.clip(bevel, 0.02, 0.95))
    inner = np.clip(v, -(1.0 - b), 1.0 - b)
    d = v - inner
    dlen = np.linalg.norm(d, axis=1, keepdims=True)
    v = np.where(dlen > 1e-8, inner + b * (d / np.maximum(dlen, 1e-8)), v)

    # Scale to requested dimensions
    sx, sy, sz = size_xyz
    v[:, 0] *= 0.5 * sx
    v[:, 1] *= 0.5 * sy
    v[:, 2] *= 0.5 * sz

    # Taper along Y (bottom -> top)
    ty = 0.5 * (v[:, 1] / max(0.5 * sy, 1e-6) + 1.0)
    tx_scale = taper_y[0] + (taper_y[1] - taper_y[0]) * ty
    tz_scale = taper_z[0] + (taper_z[1] - taper_z[0]) * ty
    v[:, 0] *= tx_scale
    v[:, 2] *= tz_scale

    # Curvature bends so plates wrap organically around torso / limbs
    if abs(bend_x) > 1e-6:
        nx = v[:, 0] / max(0.5 * sx, 1e-6)
        v[:, 2] -= bend_x * (nx * nx)
    if abs(bend_y) > 1e-6:
        ny = v[:, 1] / max(0.5 * sy, 1e-6)
        v[:, 2] -= bend_y * (ny * ny)

    # Procedural chiseled stone fracture & cleavage planes
    if rock_amp > 1e-6:
        n_est = _compute_smooth_normals(v, f)
        f1, edge, _ = _voronoi_rock(v, cell_size=rock_cell, seed=seed)
        ridge = _ridge_fbm3(v * 5.2, seed=seed + 55, octaves=3)
        crack = 1.0 - _smoothstep(0.0, 0.14, edge)
        disp = rock_amp * ((0.55 - f1) * 0.70 + (ridge - 0.45) * 0.65 - 0.85 * crack)
        v = v + n_est * disp[:, None]

    f = _ensure_outward_winding(v, f)
    return v, f


def build_basalt_column(p0, p1, r0, r1, sides=6, stacks=4, tilt_tip=0.22, seed=0):
    """Volcanic hexagonal/polygonal basalt column with fractured angled crown."""
    p0 = np.asarray(p0, dtype=np.float64)
    p1 = np.asarray(p1, dtype=np.float64)
    axis = p1 - p0
    length = max(float(np.linalg.norm(axis)), 1e-6)
    right, fwd, up = _frame_from_up(axis)

    rng = np.random.default_rng(seed)
    base_ang = rng.uniform(0.0, 2.0 * np.pi)
    angs = base_ang + np.linspace(0.0, 2.0 * np.pi, sides, endpoint=False)
    # Slight natural irregular polygon asymmetry
    rad_jitter = rng.uniform(0.86, 1.14, size=sides)

    rings = []
    t_vals = np.linspace(0.0, 0.90, stacks + 1)
    tilt_dir = np.cos(base_ang + 0.7) * right + np.sin(base_ang + 0.7) * fwd

    for s_idx, t in enumerate(t_vals):
        rad = r0 + (r1 - r0) * t
        # Subtle volcanic step/fluting along column shaft
        step = 1.0 + 0.06 * np.sin(t * 7.0 + seed) * (1.0 - t)
        center = p0 + up * (t * length)
        ring = []
        for j in range(sides):
            rj = rad * rad_jitter[j] * step
            pt = center + rj * (np.cos(angs[j]) * right + np.sin(angs[j]) * fwd)
            ring.append(pt)
        rings.append(ring)

    # Chamfered fractured top crown ring
    top_center = p1
    crown_ring = []
    for j in range(sides):
        rj = r1 * rad_jitter[j] * 0.74
        radial = np.cos(angs[j]) * right + np.sin(angs[j]) * fwd
        slope = float(np.dot(radial, tilt_dir)) * tilt_tip * r1
        crown_ring.append(top_center + rj * radial + up * slope)
    rings.append(crown_ring)

    rings = np.asarray(rings, dtype=np.float64)
    Mr, N, _ = rings.shape
    v_arr = rings.reshape(-1, 3)
    faces = []
    for i in range(Mr - 1):
        b0, b1 = i * N, (i + 1) * N
        for j in range(N):
            jn = (j + 1) % N
            faces.append((b0 + j, b0 + jn, b1 + jn))
            faces.append((b0 + j, b1 + jn, b1 + j))

    # Flat fractured bottom & top caps
    c_bot = p0.copy()
    c_top = crown_ring_mean = rings[-1].mean(axis=0) + up * (0.08 * r1)
    idx_b = len(v_arr)
    idx_t = idx_b + 1
    v_arr = np.vstack([v_arr, c_bot[None, :], c_top[None, :]])
    b_top = (Mr - 1) * N
    for j in range(N):
        jn = (j + 1) % N
        faces.append((idx_b, jn, j))
        faces.append((idx_t, b_top + j, b_top + jn))

    f_arr = _ensure_outward_winding(v_arr, np.asarray(faces, dtype=np.int32))
    return v_arr, f_arr


def build_crystal_prism(origin, direction, length, radius, sides=6, tip_ratio=0.32, seed=0):
    """Sharp hexagonal smoky-quartz / amber mineral crystal prism."""
    o = np.asarray(origin, dtype=np.float64)
    right, fwd, up = _frame_from_up(direction)
    rng = np.random.default_rng(seed)
    ang0 = rng.uniform(0.0, 2.0 * np.pi)
    angs = ang0 + np.linspace(0.0, 2.0 * np.pi, sides, endpoint=False)
    r_jit = rng.uniform(0.88, 1.12, size=sides)

    shaft_len = length * (1.0 - tip_ratio)
    r0 = radius * 0.85
    r1 = radius * 1.05
    ring0 = np.array([o + r0 * r_jit[j] * (np.cos(angs[j]) * right + np.sin(angs[j]) * fwd) for j in range(sides)])
    ring1 = np.array([
        o + up * shaft_len + r1 * r_jit[j] * (np.cos(angs[j]) * right + np.sin(angs[j]) * fwd)
        for j in range(sides)
    ])
    apex = o + up * length + (rng.uniform(-0.14, 0.14) * radius) * right + (rng.uniform(-0.14, 0.14) * radius) * fwd
    bot = o - up * (radius * 0.25)

    v = np.vstack([ring0, ring1, bot[None, :], apex[None, :]])
    idx_b = 2 * sides
    idx_t = 2 * sides + 1
    faces = []
    for j in range(sides):
        jn = (j + 1) % sides
        faces.append((j, jn, sides + jn))
        faces.append((j, sides + jn, sides + j))
        faces.append((idx_b, jn, j))
        faces.append((idx_t, sides + j, sides + jn))
    f = _ensure_outward_winding(v, np.asarray(faces, dtype=np.int32))
    return v, f


def build_torus_ring(center, axis, major_r, minor_r, seg_u=28, seg_v=10, squish_z=1.0):
    """Watertight carved bronze ring / Earth-Seal frame."""
    c = np.asarray(center, dtype=np.float64)
    right, fwd, up = _frame_from_up(axis)
    u = np.linspace(0.0, 2.0 * np.pi, seg_u, endpoint=False)
    v = np.linspace(0.0, 2.0 * np.pi, seg_v, endpoint=False)
    verts = []
    for ui in u:
        rad_dir = np.cos(ui) * right + np.sin(ui) * fwd
        ring_c = c + major_r * rad_dir
        for vi in v:
            pt = ring_c + minor_r * (np.cos(vi) * rad_dir + squish_z * np.sin(vi) * up)
            verts.append(pt)
    verts = np.asarray(verts, dtype=np.float64)
    faces = []
    for i in range(seg_u):
        inext = (i + 1) % seg_u
        for j in range(seg_v):
            jnext = (j + 1) % seg_v
            a = i * seg_v + j
            b = inext * seg_v + j
            cc = inext * seg_v + jnext
            d = i * seg_v + jnext
            faces.append((a, b, cc))
            faces.append((a, cc, d))
    f = _ensure_outward_winding(verts, np.asarray(faces, dtype=np.int32))
    return verts, f


def build_ellipsoid(center, radii, rot_mat=None, seg_u=20, seg_v=14):
    """Watertight UV ellipsoid with clean polar convergence."""
    rings = []
    phi = np.linspace(0.12 * np.pi, 0.88 * np.pi, seg_v)
    theta = np.linspace(0.0, 2.0 * np.pi, seg_u, endpoint=False)
    rx, ry, rz = radii
    for p in phi:
        sp, cp = np.sin(p), np.cos(p)
        ring = np.column_stack([rx * sp * np.cos(theta), ry * cp * np.ones(seg_u), rz * sp * np.sin(theta)])
        rings.append(ring)
    v, f = build_lofted_shell(np.asarray(rings), cap_start=True, cap_end=True)
    if rot_mat is not None:
        v = v @ rot_mat.T
    v = v + np.asarray(center, dtype=np.float64)[None, :]
    return v, f


# =============================================================================
# 3. 20-BONE MIXAMO T-POSE SKELETON & SKINNING SYSTEM
# =============================================================================

# Exact 20-bone Mixamo hierarchy matching male_body_v6.py / humanoid_anims.glb.
# Canonical boss proportions (~2.95m tall), +Y up, +Z forward, +X = Left.
BOSS_SKELETON_SPEC = [
    # idx, name,                     parent, world_pos (meters)
    ( 0, 'mixamorig:Hips',           -1, [ 0.00, 1.46,  0.00]),
    ( 1, 'mixamorig:Spine',           0, [ 0.00, 1.66,  0.00]),
    ( 2, 'mixamorig:Spine1',          1, [ 0.00, 1.92,  0.01]),
    ( 3, 'mixamorig:Spine2',          2, [ 0.00, 2.22,  0.02]),
    ( 4, 'mixamorig:Neck',            3, [ 0.00, 2.49,  0.03]),
    ( 5, 'mixamorig:Head',            4, [ 0.00, 2.66,  0.04]),

    ( 6, 'mixamorig:LeftShoulder',    3, [ 0.18, 2.34,  0.01]),
    ( 7, 'mixamorig:LeftArm',         6, [ 0.46, 2.32,  0.00]),
    ( 8, 'mixamorig:LeftForeArm',     7, [ 0.92, 2.30, -0.02]),
    ( 9, 'mixamorig:LeftHand',        8, [ 1.38, 2.30,  0.00]),

    (10, 'mixamorig:RightShoulder',   3, [-0.18, 2.34,  0.01]),
    (11, 'mixamorig:RightArm',       10, [-0.46, 2.32,  0.00]),
    (12, 'mixamorig:RightForeArm',   11, [-0.92, 2.30, -0.02]),
    (13, 'mixamorig:RightHand',      12, [-1.38, 2.30,  0.00]),

    (14, 'mixamorig:LeftUpLeg',       0, [ 0.21, 1.40,  0.00]),
    (15, 'mixamorig:LeftLeg',        14, [ 0.23, 0.78,  0.03]),
    (16, 'mixamorig:LeftFoot',       15, [ 0.24, 0.16,  0.00]),

    (17, 'mixamorig:RightUpLeg',      0, [-0.21, 1.40,  0.00]),
    (18, 'mixamorig:RightLeg',       17, [-0.23, 0.78,  0.03]),
    (19, 'mixamorig:RightFoot',      18, [-0.24, 0.16,  0.00]),
]

BONE_INDEX = {name.split(':')[-1]: idx for idx, name, _, _ in BOSS_SKELETON_SPEC}


def _skin_rigid(n_verts, primary_bone, secondary_bone=None, secondary_weight=0.0):
    w = np.zeros((n_verts, 20), dtype=np.float64)
    if secondary_bone is not None and secondary_weight > 1e-6:
        w[:, primary_bone] = 1.0 - secondary_weight
        w[:, secondary_bone] = secondary_weight
    else:
        w[:, primary_bone] = 1.0
    return w


def _skin_chain_smooth(verts, segments, sharpness=9.5):
    """Smooth capsule-distance skinning over a list of (bone_idx, p_start, p_end, weight_bias)."""
    w = np.zeros((len(verts), 20), dtype=np.float64)
    for b_idx, p0, p1, bias in segments:
        a = np.asarray(p0, dtype=np.float64)
        b = np.asarray(p1, dtype=np.float64)
        ab = b - a
        ab2 = max(float(np.dot(ab, ab)), 1e-12)
        t = np.clip(((verts - a) @ ab) / ab2, 0.0, 1.0)
        closest = a[None, :] + t[:, None] * ab[None, :]
        dist = np.linalg.norm(verts - closest, axis=1)
        influence = bias * np.exp(-sharpness * dist)
        w[:, b_idx] += influence
    s = w.sum(axis=1, keepdims=True)
    zero = (s[:, 0] < 1e-12)
    if np.any(zero):
        w[zero, segments[0][0]] = 1.0
        s = w.sum(axis=1, keepdims=True)
    return w / s


# =============================================================================
# 4. PROCEDURAL VERTEX COLOR SHADING (BAKED AO, STRATA, DUST, PATINA)
# =============================================================================

def _bake_stone_colors(v, n, seed=0, tint=(1.0, 1.0, 1.0), crack_glow=0.0, cell_size=0.22):
    """Bake rich AAA weathered basalt/sandstone surface modulation into RGBA vertex color."""
    f1, edge, cid = _voronoi_rock(v, cell_size=cell_size, seed=seed)
    fbm = _fbm3(v * 3.8, seed=seed + 21, octaves=3)
    ridge = _ridge_fbm3(v * 6.5, seed=seed + 83, octaves=2)

    # Horizontal warped sedimentary rock strata
    strata_y = v[:, 1] * 14.0 + fbm * 1.4
    strata = 0.5 + 0.5 * np.sin(strata_y)

    # Crevice occlusion from Voronoi cracks + ground contact + deep overhangs
    crack = 1.0 - _smoothstep(0.0, 0.13, edge)
    ground_ao = _smoothstep(0.0, 0.42, v[:, 1])
    up_facing = np.clip(n[:, 1], 0.0, 1.0) ** 1.8
    under_ao = 0.72 + 0.28 * np.clip(n[:, 1] * 0.5 + 0.5, 0.0, 1.0)

    # Cell-to-cell subtle stone hue variation
    tone = (
        0.74
        + 0.16 * (cid - 0.5)
        + 0.10 * (strata - 0.5)
        + 0.12 * (ridge - 0.45)
        - 0.34 * crack
    )
    tone *= (0.68 + 0.32 * ground_ao) * under_ao

    rgb = np.ones((len(v), 3), dtype=np.float64) * tone[:, None]
    rgb *= np.asarray(tint, dtype=np.float64)[None, :]

    # Highland loess/sandstone dust settled on upward-facing ledges
    dust = up_facing * (0.20 + 0.14 * np.clip(fbm + 0.5, 0.0, 1.0)) * _smoothstep(0.35, 1.1, v[:, 1])
    dust_col = np.array([1.15, 0.98, 0.76], dtype=np.float64)
    rgb = rgb * (1.0 - dust[:, None]) + (rgb * dust_col[None, :]) * dust[:, None]

    # Subtle warm earthen-amber bleed right inside deep tectonic torso/arm fissures
    if crack_glow > 1e-4:
        g_mask = (crack ** 2.2) * crack_glow
        ember = np.array([1.35, 0.65, 0.18], dtype=np.float64)
        rgb = rgb * (1.0 - g_mask[:, None]) + ember[None, :] * g_mask[:, None]

    rgb = np.clip(rgb, 0.08, 1.0)
    return np.column_stack([rgb, np.ones(len(v), dtype=np.float64)])


def _bake_bronze_colors(v, n, seed=0):
    """Antique oxidized bronze with burnished gold-ochre edges & dark verdigris recesses."""
    fbm = _fbm3(v * 7.5, seed=seed + 31, octaves=3)
    ridge = _ridge_fbm3(v * 9.0, seed=seed + 67, octaves=2)
    up = np.clip(n[:, 1] * 0.5 + 0.5, 0.0, 1.0)
    patina = np.clip(0.45 - 0.55 * ridge + 0.35 * fbm, 0.0, 1.0) * (1.0 - 0.35 * up)

    base = np.tile(np.array([0.92, 0.84, 0.70], dtype=np.float64), (len(v), 1))
    # Subtle oxidized teal-umber patina in recesses (grounded, never loud neon green)
    pat_col = np.array([0.44, 0.54, 0.48], dtype=np.float64)
    rgb = base * (1.0 - 0.55 * patina[:, None]) + pat_col[None, :] * (0.55 * patina[:, None])
    rgb *= (0.72 + 0.28 * up)[:, None]
    rgb = np.clip(rgb, 0.12, 1.0)
    return np.column_stack([rgb, np.ones(len(v), dtype=np.float64)])


def _bake_emissive_colors(v, n, seed=0):
    """Warm layered earthen-amber magma / Earth-Qi mineral gradient."""
    fbm = _fbm3(v * 6.5, seed=seed + 19, octaves=3)
    heat = np.clip(0.65 + 0.35 * fbm, 0.25, 1.0)
    r = np.clip(0.92 + 0.08 * heat, 0.0, 1.0)
    g = np.clip(0.55 + 0.38 * heat, 0.0, 1.0)
    b = np.clip(0.18 + 0.25 * (heat ** 2), 0.0, 1.0)
    return np.column_stack([r, g, b, np.ones(len(v), dtype=np.float64)])


def _bake_crystal_colors(v, origin, direction, length):
    """Base-dark smoky quartz transitioning to warm translucent amber tip."""
    d = np.asarray(direction, dtype=np.float64)
    d = d / max(np.linalg.norm(d), 1e-12)
    t = np.clip(((v - np.asarray(origin)) @ d) / max(length, 1e-6), 0.0, 1.0)
    r = 0.38 + 0.60 * t
    g = 0.26 + 0.52 * (t ** 1.2)
    b = 0.16 + 0.26 * (t ** 1.5)
    return np.column_stack([np.clip(r, 0, 1), np.clip(g, 0, 1), np.clip(b, 0, 1), np.ones(len(v))])


# =============================================================================
# 5. BOSS COMPONENT BUILDERS ("SON NHAC CU KHOI")
# =============================================================================

# Material indices (6 compact PBR draw calls)
MAT_BASALT = 0      # Dark volcanic basalt core & monolith spires
MAT_SANDSTONE = 1   # Weathered carved sandstone/terracotta armor plates
MAT_BRONZE = 2      # Ancient oxidized bronze trim & Earth Seal
MAT_AMBER_CORE = 3  # Glowing warm earthen-amber Earth-Qi fissures & eyes
MAT_CRYSTAL = 4     # Hexagonal smoky-amber mineral crystal clusters
MAT_DARK_IRON = 5   # Forged dark iron harness bands & joint sockets


class BossAssembler:
    def __init__(self, detail='high', seed=2610):
        self.detail = detail
        self.seed = seed
        self.parts = {i: [] for i in range(6)}
        self.J = {name.split(':')[-1]: np.array(pos, dtype=np.float64) for _, name, _, pos in BOSS_SKELETON_SPEC}
        # Resolution presets
        self.cfg = {
            'medium': dict(torso_u=28, torso_v=18, limb_u=16, limb_v=12, slab=4, finger_u=8),
            'high':   dict(torso_u=38, torso_v=24, limb_u=22, limb_v=16, slab=6, finger_u=10),
            'ultra':  dict(torso_u=48, torso_v=30, limb_u=28, limb_v=20, slab=8, finger_u=12),
        }[detail]

    def add_part(self, v, f, mat_idx, w20, col_rgba=None, faceted=False, uv_scale=0.65):
        f = _ensure_outward_winding(v, f)
        n = _compute_smooth_normals(v, f)
        uv = _triplanar_uv(v, n, scale=uv_scale)
        if col_rgba is None:
            col_rgba = np.ones((len(v), 4), dtype=np.float64)
        if faceted:
            v, n, f, uv, w20, col_rgba = _split_flat_normals(v, f, uv, w20, col_rgba)
        self.parts[mat_idx].append(dict(v=v, n=n, f=f, uv=uv, w=w20, col=col_rgba))

    def _transform(self, v, pos, rot_mat=None):
        out = v @ rot_mat.T if rot_mat is not None else v.copy()
        return out + np.asarray(pos, dtype=np.float64)[None, :]

    # -------------------------------------------------------------------------
    # A. MASSIVE ROCK-MUSCLE TORSO + INNER EARTH-QI MAGMA CORE
    # -------------------------------------------------------------------------
    def build_torso_and_core(self):
        nu, nv = self.cfg['torso_u'], self.cfg['torso_v']
        theta = np.linspace(0.0, 2.0 * np.pi, nu, endpoint=False)
        ct, st = np.cos(theta), np.sin(theta)

        # Vertical profile from pelvis (y=1.26) to upper trap/neck base (y=2.50)
        profile = [
            # (y,    rx,    rz_f,  rz_b,  z_off, squareness)
            (1.26, 0.25, 0.18, 0.20,  0.00, 0.12),
            (1.38, 0.32, 0.23, 0.24,  0.00, 0.16),
            (1.52, 0.30, 0.21, 0.22,  0.00, 0.18),
            (1.68, 0.28, 0.20, 0.21,  0.00, 0.20),
            (1.86, 0.34, 0.24, 0.24,  0.01, 0.22),
            (2.04, 0.42, 0.29, 0.27,  0.02, 0.24),
            (2.22, 0.48, 0.31, 0.29,  0.02, 0.24),
            (2.36, 0.42, 0.26, 0.26,  0.02, 0.20),
            (2.47, 0.22, 0.18, 0.19,  0.03, 0.12),
        ]
        yp = np.array([p[0] for p in profile])
        t_eval = np.linspace(yp[0], yp[-1], nv)
        rx_i = np.interp(t_eval, yp, [p[1] for p in profile])
        rzf_i = np.interp(t_eval, yp, [p[2] for p in profile])
        rzb_i = np.interp(t_eval, yp, [p[3] for p in profile])
        zoff_i = np.interp(t_eval, yp, [p[4] for p in profile])
        sq_i = np.interp(t_eval, yp, [p[5] for p in profile])

        rings_outer = []
        rings_core = []
        for k, y in enumerate(t_eval):
            rz = np.where(st >= 0.0, rzf_i[k], rzb_i[k])
            # Chiseled superellipse cross-section for heroic rock torso planes
            sq = sq_i[k]
            sx = np.sign(ct) * (np.abs(ct) ** (1.0 - sq))
            sz = np.sign(st) * (np.abs(st) ** (1.0 - sq))
            x = rx_i[k] * sx
            z = zoff_i[k] + rz * sz
            # Sculpted pectoral shelf & spinal furrow
            pec = np.exp(-0.5 * ((y - 2.16) / 0.13) ** 2) * np.clip(st, 0.0, 1.0) ** 1.5
            sternum_cleft = np.exp(-0.5 * (x / 0.045) ** 2) * pec
            z += 0.055 * pec - 0.042 * sternum_cleft
            spine_furrow = np.exp(-0.5 * (x / 0.055) ** 2) * np.clip(-st, 0.0, 1.0) ** 2
            z += 0.038 * spine_furrow
            ring = np.column_stack([x, np.full(nu, y), z])
            rings_outer.append(ring)
            # Slightly recessed inner Earth-Qi magma core exposed through deep tectonic cracks
            core_scale = 0.915 if (1.62 < y < 2.34) else 0.84
            ring_c = np.column_stack([x * core_scale, np.full(nu, y), zoff_i[k] + (z - zoff_i[k]) * core_scale])
            rings_core.append(ring_c)

        v_torso, f_torso = build_lofted_shell(np.asarray(rings_outer), cap_start=True, cap_end=True)
        n_est = _compute_smooth_normals(v_torso, f_torso)

        # Carve dramatic Voronoi rock plates + tectonic fissures on outer basalt body
        f1, edge, _ = _voronoi_rock(v_torso, cell_size=0.25, seed=self.seed + 10)
        ridge = _ridge_fbm3(v_torso * 3.8, seed=self.seed + 22, octaves=3)
        crack = 1.0 - _smoothstep(0.0, 0.11, edge)
        # Deepen fissures over chest & obliques so the glowing core peeks through organically
        chest_zone = _smoothstep(1.62, 1.85, v_torso[:, 1]) * (1.0 - _smoothstep(2.30, 2.46, v_torso[:, 1]))
        disp = 0.034 * (0.45 - f1) + 0.022 * (ridge - 0.45) - (0.048 + 0.038 * chest_zone) * crack
        v_torso = v_torso + n_est * disp[:, None]

        spine_segs = [
            (BONE_INDEX['Hips'],   [0, 1.24, 0.00], self.J['Spine'],  1.0),
            (BONE_INDEX['Spine'],  self.J['Spine'], self.J['Spine1'], 1.0),
            (BONE_INDEX['Spine1'], self.J['Spine1'], self.J['Spine2'], 1.05),
            (BONE_INDEX['Spine2'], self.J['Spine2'], self.J['Neck'],  1.15),
            (BONE_INDEX['Neck'],   self.J['Neck'],   self.J['Head'],  0.85),
        ]
        w_torso = _skin_chain_smooth(v_torso, spine_segs, sharpness=6.5)
        n_torso = _compute_smooth_normals(v_torso, f_torso)
        col_torso = _bake_stone_colors(
            v_torso, n_torso, seed=self.seed + 10,
            tint=(0.94, 0.91, 0.87), crack_glow=0.62, cell_size=0.25,
        )
        self.add_part(v_torso, f_torso, MAT_BASALT, w_torso, col_torso)

        # Inner glowing Earth-Qi core
        v_core, f_core = build_lofted_shell(np.asarray(rings_core), cap_start=True, cap_end=True)
        w_core = _skin_chain_smooth(v_core, spine_segs, sharpness=6.5)
        col_core = _bake_emissive_colors(v_core, _compute_smooth_normals(v_core, f_core), seed=self.seed + 77)
        self.add_part(v_core, f_core, MAT_AMBER_CORE, w_core, col_core)

        # Thick muscular rock neck column + collar ring
        neck_rings = []
        for k, t in enumerate(np.linspace(0.0, 1.0, 7)):
            p = self.J['Spine2'] * (1.0 - t) + self.J['Head'] * t
            r_x = 0.21 - 0.07 * t
            r_z = 0.19 - 0.05 * t
            ring = np.column_stack([r_x * ct, np.full(nu, p[1]), p[2] + r_z * st])
            neck_rings.append(ring)
        v_nk, f_nk = build_lofted_shell(np.asarray(neck_rings), cap_start=True, cap_end=True)
        n_nk = _compute_smooth_normals(v_nk, f_nk)
        v_nk += n_nk * (0.014 * _fbm3(v_nk * 6.0, seed=self.seed + 44))[:, None]
        w_nk = _skin_chain_smooth(v_nk, [
            (BONE_INDEX['Spine2'], self.J['Spine2'], self.J['Neck'], 1.0),
            (BONE_INDEX['Neck'],   self.J['Neck'],   self.J['Head'], 1.3),
        ], sharpness=10.0)
        col_nk = _bake_stone_colors(v_nk, _compute_smooth_normals(v_nk, f_nk), seed=self.seed + 44, tint=(0.88, 0.84, 0.80))
        self.add_part(v_nk, f_nk, MAT_BASALT, w_nk, col_nk)

    # -------------------------------------------------------------------------
    # B. CHEST CUIRASS, ANCIENT EARTH SEAL (DIA AN) & DORSAL BASALT SPINES
    # -------------------------------------------------------------------------
    def build_torso_armor_and_spines(self):
        sd = self.cfg['slab']

        # 1. Heavy beveled pectoral sandstone slabs (Left & Right) + bronze trim collars
        for s in (1.0, -1.0):
            v_pec, f_pec = build_beveled_slab(
                (0.36, 0.30, 0.12), bevel=0.22, sub_div=sd,
                bend_x=0.045, bend_y=0.025, taper_y=(0.86, 1.06),
                rock_amp=0.016, rock_cell=0.14, seed=self.seed + (101 if s > 0 else 102),
            )
            R = _rot_euler_xyz(rx_deg=-8.0, ry_deg=-s * 18.0, rz_deg=s * 7.0)
            v_pec = self._transform(v_pec, [s * 0.215, 2.18, 0.29], R)
            w_pec = _skin_rigid(len(v_pec), BONE_INDEX['Spine2'])
            col_pec = _bake_stone_colors(
                v_pec, _compute_smooth_normals(v_pec, f_pec),
                seed=self.seed + 105, tint=(1.04, 0.97, 0.86), cell_size=0.16,
            )
            self.add_part(v_pec, f_pec, MAT_SANDSTONE, w_pec, col_pec)

            # Bronze clavicle/chest border plate above each pectoral slab
            v_tr, f_tr = build_beveled_slab(
                (0.34, 0.055, 0.075), bevel=0.28, sub_div=max(sd - 1, 3),
                bend_x=0.04, rock_amp=0.003, seed=self.seed + 111,
            )
            R_tr = _rot_euler_xyz(rx_deg=-12.0, ry_deg=-s * 20.0, rz_deg=s * 14.0)
            v_tr = self._transform(v_tr, [s * 0.22, 2.34, 0.24], R_tr)
            w_tr = _skin_rigid(len(v_tr), BONE_INDEX['Spine2'])
            self.add_part(v_tr, f_tr, MAT_BRONZE, w_tr, _bake_bronze_colors(v_tr, _compute_smooth_normals(v_tr, f_tr), self.seed + 111))

            # Forged dark iron diagonal harness strap wrapping chest -> back
            v_st, f_st = build_beveled_slab(
                (0.085, 0.44, 0.52), bevel=0.24, sub_div=max(sd - 2, 3),
                taper_y=(0.90, 1.05), rock_amp=0.004, seed=self.seed + 119,
            )
            # Keep only outer shell feel by placing solid iron harness yoke over collarbone
            v_yoke, f_yoke = build_beveled_slab(
                (0.11, 0.055, 0.46), bevel=0.28, sub_div=max(sd - 1, 3),
                bend_y=0.05, rock_amp=0.003, seed=self.seed + 123,
            )
            R_yk = _rot_euler_xyz(rx_deg=6.0, ry_deg=-s * 10.0, rz_deg=-s * 12.0)
            v_yoke = self._transform(v_yoke, [s * 0.24, 2.41, 0.03], R_yk)
            self.add_part(
                v_yoke, f_yoke, MAT_DARK_IRON,
                _skin_rigid(len(v_yoke), BONE_INDEX['Spine2']),
                _bake_stone_colors(v_yoke, _compute_smooth_normals(v_yoke, f_yoke), self.seed + 123, tint=(0.82, 0.80, 0.78)),
            )

        # 2. Central Ancient Bronze Earth Seal ("Ho Tam Dia An") on sternum
        seal_pos = np.array([0.0, 2.13, 0.345])
        seal_axis = np.array([0.0, 0.14, 1.0])
        for maj_r, min_r, z_push in [(0.145, 0.024, 0.0), (0.095, 0.017, 0.012), (0.048, 0.013, 0.018)]:
            v_rg, f_rg = build_torus_ring(seal_pos + np.array([0, 0, z_push]), seal_axis, maj_r, min_r, seg_u=28, seg_v=10, squish_z=0.75)
            w_rg = _skin_rigid(len(v_rg), BONE_INDEX['Spine2'])
            self.add_part(v_rg, f_rg, MAT_BRONZE, w_rg, _bake_bronze_colors(v_rg, _compute_smooth_normals(v_rg, f_rg), self.seed + 131))

        # 4 cardinal bronze spokes on the Earth Seal (ancient Dong Son / Bagua compass motif)
        for ang_deg in (0.0, 45.0, 90.0, 135.0):
            v_sp, f_sp = build_beveled_slab((0.26, 0.022, 0.026), bevel=0.30, sub_div=3)
            R_sp = _rot_euler_xyz(rx_deg=-8.0, rz_deg=ang_deg)
            v_sp = self._transform(v_sp, seal_pos + np.array([0.0, 0.0, 0.008]), R_sp)
            self.add_part(v_sp, f_sp, MAT_BRONZE, _skin_rigid(len(v_sp), BONE_INDEX['Spine2']),
                          _bake_bronze_colors(v_sp, _compute_smooth_normals(v_sp, f_sp), self.seed + 137))

        # Recessed glowing amber Earth-Qi gem behind the bronze seal
        v_gem, f_gem = build_ellipsoid(seal_pos + np.array([0.0, 0.0, -0.012]), (0.125, 0.125, 0.032), seg_u=22, seg_v=12)
        self.add_part(v_gem, f_gem, MAT_AMBER_CORE, _skin_rigid(len(v_gem), BONE_INDEX['Spine2']),
                      _bake_emissive_colors(v_gem, _compute_smooth_normals(v_gem, f_gem), self.seed + 141))

        # 3. Layered Abdominal Stone Plastron (2 heavy interlocking plates)
        for idx_ab, (y_ab, w_ab, h_ab, z_ab, b_name) in enumerate([
            (1.90, 0.36, 0.16, 0.275, 'Spine1'),
            (1.73, 0.32, 0.15, 0.250, 'Spine'),
        ]):
            v_ab, f_ab = build_beveled_slab(
                (w_ab, h_ab, 0.095), bevel=0.24, sub_div=max(sd - 1, 4),
                bend_x=0.055, taper_y=(0.90, 1.05), rock_amp=0.012, rock_cell=0.14, seed=self.seed + 150 + idx_ab,
            )
            v_ab = self._transform(v_ab, [0.0, y_ab, z_ab], _rot_euler_xyz(rx_deg=4.0))
            self.add_part(
                v_ab, f_ab, MAT_SANDSTONE, _skin_rigid(len(v_ab), BONE_INDEX[b_name]),
                _bake_stone_colors(v_ab, _compute_smooth_normals(v_ab, f_ab), self.seed + 150 + idx_ab, tint=(0.98, 0.92, 0.82)),
            )

        # 4. Dorsal Basalt Column Ridge Crest along the back (signature isometric silhouette!)
        spine_crest = [
            # (p0,                    p1,                    r0,    r1,    bone)
            ([ 0.00, 2.34, -0.22], [ 0.00, 2.62, -0.44], 0.085, 0.052, 'Spine2'),
            ([ 0.11, 2.28, -0.23], [ 0.20, 2.50, -0.42], 0.068, 0.040, 'Spine2'),
            ([-0.11, 2.28, -0.23], [-0.20, 2.50, -0.42], 0.068, 0.040, 'Spine2'),
            ([ 0.00, 2.12, -0.24], [ 0.00, 2.34, -0.46], 0.078, 0.046, 'Spine2'),
            ([ 0.09, 2.02, -0.22], [ 0.15, 2.18, -0.40], 0.058, 0.034, 'Spine1'),
            ([-0.09, 2.02, -0.22], [-0.15, 2.18, -0.40], 0.058, 0.034, 'Spine1'),
            ([ 0.00, 1.88, -0.22], [ 0.00, 2.02, -0.41], 0.065, 0.038, 'Spine1'),
            ([ 0.00, 1.68, -0.20], [ 0.00, 1.78, -0.34], 0.054, 0.030, 'Spine'),
        ]
        for k, (p0, p1, r0, r1, bname) in enumerate(spine_crest):
            v_c, f_c = build_basalt_column(p0, p1, r0, r1, sides=6, stacks=3, tilt_tip=0.28, seed=self.seed + 200 + k)
            col_c = _bake_stone_colors(v_c, _compute_smooth_normals(v_c, f_c), self.seed + 200 + k, tint=(0.86, 0.84, 0.80), cell_size=0.18)
            self.add_part(v_c, f_c, MAT_BASALT, _skin_rigid(len(v_c), BONE_INDEX[bname]), col_c, faceted=True)

        # Embedded smoky-amber Earth-Qi crystals sprouting from upper back fissures
        back_crystals = [
            ([ 0.06, 2.32, -0.27], [ 0.35,  0.75, -0.60], 0.24, 0.032, 'Spine2'),
            ([-0.06, 2.30, -0.27], [-0.35,  0.72, -0.62], 0.22, 0.030, 'Spine2'),
            ([ 0.00, 2.20, -0.29], [ 0.08,  0.55, -0.82], 0.19, 0.026, 'Spine2'),
            ([ 0.05, 1.98, -0.26], [ 0.22,  0.42, -0.88], 0.15, 0.022, 'Spine1'),
            ([-0.05, 1.96, -0.26], [-0.24,  0.40, -0.88], 0.15, 0.022, 'Spine1'),
        ]
        for k, (orig, dvec, clen, crad, bname) in enumerate(back_crystals):
            v_cr, f_cr = build_crystal_prism(orig, dvec, clen, crad, sides=6, seed=self.seed + 240 + k)
            col_cr = _bake_crystal_colors(v_cr, orig, dvec, clen)
            self.add_part(v_cr, f_cr, MAT_CRYSTAL, _skin_rigid(len(v_cr), BONE_INDEX[bname]), col_cr, faceted=True)

    # -------------------------------------------------------------------------
    # C. WAR-MASK STONE HEAD & MOUNTAIN-PEAK CROWN ("SON TU QUAN")
    # -------------------------------------------------------------------------
    def build_head_and_crown(self):
        hp = self.J['Head']
        b_head = BONE_INDEX['Head']
        sd = self.cfg['slab']

        # 1. Sculpted chiseled basalt cranium & jaw core
        v_hd, f_hd = build_beveled_slab(
            (0.28, 0.34, 0.32), bevel=0.42, sub_div=sd,
            taper_y=(0.76, 1.06), taper_z=(0.82, 1.04),
            rock_amp=0.012, rock_cell=0.11, seed=self.seed + 301,
        )
        v_hd = self._transform(v_hd, hp + np.array([0.0, 0.07, 0.01]))
        # Carve deep recessed orbital sockets so glowing eyes sit inside dark shadow
        eye_l = hp + np.array([ 0.068, 0.085, 0.155])
        eye_r = hp + np.array([-0.068, 0.085, 0.155])
        for ep in (eye_l, eye_r):
            d2 = np.sum(((v_hd - ep) / np.array([0.044, 0.026, 0.055])) ** 2, axis=1)
            recess = np.exp(-0.5 * d2)
            v_hd[:, 2] -= 0.052 * recess
        col_hd = _bake_stone_colors(v_hd, _compute_smooth_normals(v_hd, f_hd), self.seed + 301, tint=(0.85, 0.82, 0.78), cell_size=0.12)
        # Darken orbital cavity AO around eyes
        for ep in (eye_l, eye_r):
            d2 = np.sum(((v_hd - ep) / np.array([0.055, 0.034, 0.065])) ** 2, axis=1)
            col_hd[:, :3] *= (1.0 - 0.65 * np.exp(-0.5 * d2))[:, None]
        self.add_part(v_hd, f_hd, MAT_BASALT, _skin_rigid(len(v_hd), b_head), col_hd)

        # 2. Fierce Angled Stone War-Mask Brow, Cheek Plates & Mandible Guard
        v_brow, f_brow = build_beveled_slab(
            (0.30, 0.085, 0.13), bevel=0.22, sub_div=max(sd - 1, 4),
            bend_x=0.045, rock_amp=0.008, rock_cell=0.10, seed=self.seed + 310,
        )
        # Scowling V-dip at glabella
        v_brow[:, 1] -= 0.024 * np.exp(-0.5 * (v_brow[:, 0] / 0.055) ** 2)
        v_brow = self._transform(v_brow, hp + np.array([0.0, 0.145, 0.145]), _rot_euler_xyz(rx_deg=12.0))
        self.add_part(
            v_brow, f_brow, MAT_SANDSTONE, _skin_rigid(len(v_brow), b_head),
            _bake_stone_colors(v_brow, _compute_smooth_normals(v_brow, f_brow), self.seed + 310, tint=(1.02, 0.94, 0.84)),
        )

        # Cheek-guard blades & stone mandible jaw
        for s in (1.0, -1.0):
            v_ck, f_ck = build_beveled_slab(
                (0.11, 0.21, 0.14), bevel=0.22, sub_div=max(sd - 1, 4),
                taper_y=(0.75, 1.05), rock_amp=0.007, seed=self.seed + (314 if s > 0 else 315),
            )
            R_ck = _rot_euler_xyz(rx_deg=-8.0, ry_deg=-s * 34.0, rz_deg=s * 10.0)
            v_ck = self._transform(v_ck, hp + np.array([s * 0.135, 0.01, 0.095]), R_ck)
            self.add_part(
                v_ck, f_ck, MAT_SANDSTONE, _skin_rigid(len(v_ck), b_head),
                _bake_stone_colors(v_ck, _compute_smooth_normals(v_ck, f_ck), self.seed + 315, tint=(0.98, 0.91, 0.80)),
            )

        v_jaw, f_jaw = build_beveled_slab(
            (0.19, 0.11, 0.13), bevel=0.24, sub_div=max(sd - 1, 4),
            bend_x=0.035, taper_y=(0.78, 1.05), rock_amp=0.007, seed=self.seed + 320,
        )
        v_jaw = self._transform(v_jaw, hp + np.array([0.0, -0.055, 0.135]), _rot_euler_xyz(rx_deg=-10.0))
        self.add_part(
            v_jaw, f_jaw, MAT_SANDSTONE, _skin_rigid(len(v_jaw), b_head),
            _bake_stone_colors(v_jaw, _compute_smooth_normals(v_jaw, f_jaw), self.seed + 320, tint=(0.95, 0.88, 0.78)),
        )

        # 3. Recessed Glowing Amber Mineral Eyes
        for s in (1.0, -1.0):
            v_eye, f_eye = build_ellipsoid(
                hp + np.array([s * 0.068, 0.082, 0.140]),
                (0.032, 0.015, 0.022),
                rot_mat=_rot_euler_xyz(ry_deg=-s * 16.0, rz_deg=-s * 12.0),
                seg_u=14, seg_v=10,
            )
            self.add_part(v_eye, f_eye, MAT_AMBER_CORE, _skin_rigid(len(v_eye), b_head),
                          _bake_emissive_colors(v_eye, _compute_smooth_normals(v_eye, f_eye), self.seed + 330))

        # 4. Ancient Bronze Brow Fillet & 3-Peak Mountain Crown ("Son Tu Quan")
        v_fil, f_fil = build_torus_ring(hp + np.array([0.0, 0.185, 0.01]), [0.0, 1.0, 0.0], 0.165, 0.024, seg_u=28, seg_v=8, squish_z=1.35)
        self.add_part(v_fil, f_fil, MAT_BRONZE, _skin_rigid(len(v_fil), b_head),
                      _bake_bronze_colors(v_fil, _compute_smooth_normals(v_fil, f_fil), self.seed + 340))

        crown_peaks = [
            (hp + np.array([ 0.00, 0.18, 0.11]), hp + np.array([ 0.00, 0.37, 0.06]), 0.046, 0.014, MAT_BRONZE),
            (hp + np.array([ 0.10, 0.18, 0.06]), hp + np.array([ 0.16, 0.31, 0.01]), 0.036, 0.012, MAT_BASALT),
            (hp + np.array([-0.10, 0.18, 0.06]), hp + np.array([-0.16, 0.31, 0.01]), 0.036, 0.012, MAT_BASALT),
            (hp + np.array([ 0.07, 0.19,-0.07]), hp + np.array([ 0.10, 0.30,-0.15]), 0.040, 0.016, MAT_BASALT),
            (hp + np.array([-0.07, 0.19,-0.07]), hp + np.array([-0.10, 0.30,-0.15]), 0.040, 0.016, MAT_BASALT),
        ]
        for k, (p0, p1, r0, r1, mat_id) in enumerate(crown_peaks):
            v_pk, f_pk = build_basalt_column(p0, p1, r0, r1, sides=5, stacks=2, tilt_tip=0.25, seed=self.seed + 350 + k)
            col_pk = (
                _bake_bronze_colors(v_pk, _compute_smooth_normals(v_pk, f_pk), self.seed + 350 + k)
                if mat_id == MAT_BRONZE else
                _bake_stone_colors(v_pk, _compute_smooth_normals(v_pk, f_pk), self.seed + 350 + k, tint=(0.86, 0.83, 0.78))
            )
            self.add_part(v_pk, f_pk, mat_id, _skin_rigid(len(v_pk), b_head), col_pk, faceted=True)

    # -------------------------------------------------------------------------
    # D. ASYMMETRIC AAA PAULDRONS (MONOLITH SPIRE LEFT / SIEGE BOULDER RIGHT)
    # -------------------------------------------------------------------------
    def build_pauldrons(self):
        sd = self.cfg['slab']

        # --- LEFT SHOULDER (+X): Towering Tiered Basalt Monolith & Amber Quartz Cluster ---
        sh_l = self.J['LeftArm']
        b_arm_l = BONE_INDEX['LeftArm']
        b_shd_l = BONE_INDEX['LeftShoulder']
        w_l_blend = lambda nv: _skin_rigid(nv, b_arm_l, b_shd_l, 0.30)

        # Base heavy carved stone pauldron dome wrapping deltoid
        v_pl, f_pl = build_beveled_slab(
            (0.42, 0.24, 0.40), bevel=0.28, sub_div=sd,
            bend_x=0.055, bend_y=0.055, taper_y=(1.08, 0.82),
            rock_amp=0.022, rock_cell=0.16, seed=self.seed + 401,
        )
        v_pl = self._transform(v_pl, sh_l + np.array([0.08, 0.14, 0.00]), _rot_euler_xyz(rz_deg=-18.0))
        self.add_part(
            v_pl, f_pl, MAT_SANDSTONE, w_l_blend(len(v_pl)),
            _bake_stone_colors(v_pl, _compute_smooth_normals(v_pl, f_pl), self.seed + 401, tint=(1.00, 0.93, 0.83)),
        )

        # Towering volcanic basalt spires erupting upward/outward from Left Pauldron
        left_spires = [
            (sh_l + [0.02, 0.14,  0.00], sh_l + [0.16, 0.54, -0.04], 0.115, 0.072),
            (sh_l + [0.14, 0.11,  0.06], sh_l + [0.34, 0.40,  0.08], 0.088, 0.050),
            (sh_l + [0.12, 0.10, -0.09], sh_l + [0.28, 0.36, -0.18], 0.085, 0.048),
            (sh_l + [-0.04, 0.16, -0.06], sh_l + [-0.02, 0.42, -0.16], 0.076, 0.042),
        ]
        for k, (p0, p1, r0, r1) in enumerate(left_spires):
            v_sp, f_sp = build_basalt_column(p0, p1, r0, r1, sides=6, stacks=3, tilt_tip=0.30, seed=self.seed + 410 + k)
            col_sp = _bake_stone_colors(v_sp, _compute_smooth_normals(v_sp, f_sp), self.seed + 410 + k, tint=(0.86, 0.83, 0.78))
            self.add_part(v_sp, f_sp, MAT_BASALT, w_l_blend(len(v_sp)), col_sp, faceted=True)

        # Smoky-amber Earth-Qi crystals wedged between the left basalt spires
        left_crystals = [
            (sh_l + [0.08, 0.20,  0.08], [0.45, 0.85,  0.30], 0.28, 0.036),
            (sh_l + [0.14, 0.18,  0.11], [0.68, 0.65,  0.40], 0.20, 0.026),
            (sh_l + [0.06, 0.22, -0.10], [0.32, 0.80, -0.50], 0.24, 0.030),
        ]
        for k, (orig, dvec, clen, crad) in enumerate(left_crystals):
            v_cr, f_cr = build_crystal_prism(orig, dvec, clen, crad, sides=6, seed=self.seed + 430 + k)
            self.add_part(v_cr, f_cr, MAT_CRYSTAL, w_l_blend(len(v_cr)), _bake_crystal_colors(v_cr, orig, dvec, clen), faceted=True)

        # Bronze banding ring hugging lower rim of left pauldron
        v_bl, f_bl = build_beveled_slab((0.38, 0.055, 0.42), bevel=0.28, sub_div=max(sd - 1, 3), bend_x=0.05)
        v_bl = self._transform(v_bl, sh_l + np.array([0.11, 0.03, 0.00]), _rot_euler_xyz(rz_deg=-22.0))
        self.add_part(v_bl, f_bl, MAT_BRONZE, w_l_blend(len(v_bl)),
                      _bake_bronze_colors(v_bl, _compute_smooth_normals(v_bl, f_bl), self.seed + 440))

        # --- RIGHT SHOULDER (-X): Layered Siege-Ram Stone Pauldron & Bronze Rim ---
        sh_r = self.J['RightArm']
        b_arm_r = BONE_INDEX['RightArm']
        b_shd_r = BONE_INDEX['RightShoulder']
        w_r_blend = lambda nv: _skin_rigid(nv, b_arm_r, b_shd_r, 0.30)

        # Upper domed siege boulder slab
        v_pr1, f_pr1 = build_beveled_slab(
            (0.46, 0.26, 0.44), bevel=0.30, sub_div=sd,
            bend_x=0.065, bend_y=0.060, taper_y=(1.08, 0.78),
            rock_amp=0.024, rock_cell=0.16, seed=self.seed + 450,
        )
        v_pr1 = self._transform(v_pr1, sh_r + np.array([-0.08, 0.14, 0.00]), _rot_euler_xyz(rz_deg=20.0))
        self.add_part(
            v_pr1, f_pr1, MAT_SANDSTONE, w_r_blend(len(v_pr1)),
            _bake_stone_colors(v_pr1, _compute_smooth_normals(v_pr1, f_pr1), self.seed + 450, tint=(1.02, 0.94, 0.84)),
        )

        # Lower overlapping lamellar stone shoulder skirt
        v_pr2, f_pr2 = build_beveled_slab(
            (0.32, 0.22, 0.36), bevel=0.24, sub_div=max(sd - 1, 4),
            bend_y=0.045, taper_y=(0.86, 1.06), rock_amp=0.014, seed=self.seed + 455,
        )
        v_pr2 = self._transform(v_pr2, sh_r + np.array([-0.19, -0.02, 0.00]), _rot_euler_xyz(rz_deg=34.0))
        self.add_part(
            v_pr2, f_pr2, MAT_BASALT, _skin_rigid(len(v_pr2), b_arm_r),
            _bake_stone_colors(v_pr2, _compute_smooth_normals(v_pr2, f_pr2), self.seed + 455, tint=(0.90, 0.87, 0.82)),
        )

        # Heavy bronze siege ridge crest running front-to-back over right pauldron
        v_br, f_br = build_beveled_slab((0.085, 0.14, 0.46), bevel=0.25, sub_div=max(sd - 1, 4), bend_y=0.065, taper_y=(1.1, 0.65))
        v_br = self._transform(v_br, sh_r + np.array([-0.06, 0.25, 0.00]), _rot_euler_xyz(rz_deg=18.0))
        self.add_part(v_br, f_br, MAT_BRONZE, w_r_blend(len(v_br)),
                      _bake_bronze_colors(v_br, _compute_smooth_normals(v_br, f_br), self.seed + 460))

        # 2 short angled basalt spurs + 1 amber crystal accent on right shoulder
        v_rs, f_rs = build_basalt_column(sh_r + [-0.08, 0.20, -0.06], sh_r + [-0.26, 0.38, -0.14], 0.075, 0.042, sides=6, stacks=2, seed=self.seed + 465)
        self.add_part(v_rs, f_rs, MAT_BASALT, w_r_blend(len(v_rs)),
                      _bake_stone_colors(v_rs, _compute_smooth_normals(v_rs, f_rs), self.seed + 465, tint=(0.86, 0.83, 0.78)), faceted=True)
        v_rc, f_rc = build_crystal_prism(sh_r + [-0.14, 0.16, 0.10], [-0.65, 0.65, 0.35], 0.19, 0.025, sides=6, seed=self.seed + 468)
        self.add_part(v_rc, f_rc, MAT_CRYSTAL, w_r_blend(len(v_rc)), _bake_crystal_colors(v_rc, sh_r + [-0.14, 0.16, 0.10], [-0.65, 0.65, 0.35], 0.19), faceted=True)

    # -------------------------------------------------------------------------
    # E. COLOSSAL ARMS, MONOLITH FOREARM BRACERS & ARTICULATED STONE GAUNTLETS
    # -------------------------------------------------------------------------
    def build_arms_and_gauntlets(self):
        nu, nv = self.cfg['limb_u'], self.cfg['limb_v']
        sd = self.cfg['slab']
        theta = np.linspace(0.0, 2.0 * np.pi, nu, endpoint=False)
        ct, st = np.cos(theta), np.sin(theta)

        for side, s in (('Left', 1.0), ('Right', -1.0)):
            p_sh = self.J[f'{side}Arm']
            p_el = self.J[f'{side}ForeArm']
            p_wr = self.J[f'{side}Hand']
            b_sh = BONE_INDEX[f'{side}Shoulder']
            b_ua = BONE_INDEX[f'{side}Arm']
            b_fa = BONE_INDEX[f'{side}ForeArm']
            b_ha = BONE_INDEX[f'{side}Hand']

            # 1. Sculpted Basalt Arm Core (Deltoid -> Bicep/Tricep -> Heavy Forearm)
            rings_arm = []
            rings_core = []
            for k in range(nv):
                u = k / (nv - 1)
                if u <= 0.5:
                    t = u * 2.0
                    pos = p_sh * (1.0 - t) + p_el * t
                    # Deltoid bulge -> thick bicep -> muscular elbow joint
                    rad_y = 0.165 * (1.0 - t) + 0.125 * t + 0.028 * np.sin(np.pi * t)
                    rad_z = 0.155 * (1.0 - t) + 0.118 * t + 0.022 * np.sin(np.pi * t)
                else:
                    t = (u - 0.5) * 2.0
                    pos = p_el * (1.0 - t) + p_wr * t
                    # Colossal thick golem forearm flaring slightly toward monolith bracer
                    rad_y = 0.132 + 0.036 * np.sin(np.pi * t) - 0.018 * t
                    rad_z = 0.126 + 0.032 * np.sin(np.pi * t) - 0.016 * t
                sx = np.sign(ct) * (np.abs(ct) ** 0.85)
                sz = np.sign(st) * (np.abs(st) ** 0.85)
                ring = np.column_stack([
                    np.full(nu, pos[0]),
                    pos[1] + rad_y * sx,
                    pos[2] + rad_z * sz,
                ])
                rings_arm.append(ring)
                rings_core.append(np.column_stack([
                    np.full(nu, pos[0]),
                    pos[1] + rad_y * 0.88 * sx,
                    pos[2] + rad_z * 0.88 * sz,
                ]))

            v_arm, f_arm = build_lofted_shell(np.asarray(rings_arm), cap_start=True, cap_end=True)
            n_est = _compute_smooth_normals(v_arm, f_arm)
            f1, edge, _ = _voronoi_rock(v_arm, cell_size=0.17, seed=self.seed + (500 if s > 0 else 501))
            crack = 1.0 - _smoothstep(0.0, 0.11, edge)
            v_arm += n_est * (0.020 * (0.48 - f1) - 0.028 * crack)[:, None]

            arm_segs = [
                (b_sh, self.J[f'{side}Shoulder'], p_sh, 0.7),
                (b_ua, p_sh, p_el, 1.15),
                (b_fa, p_el, p_wr, 1.25),
                (b_ha, p_wr, p_wr + np.array([s * 0.24, 0.0, 0.0]), 0.85),
            ]
            w_arm = _skin_chain_smooth(v_arm, arm_segs, sharpness=8.5)
            col_arm = _bake_stone_colors(
                v_arm, _compute_smooth_normals(v_arm, f_arm),
                self.seed + (500 if s > 0 else 501), tint=(0.90, 0.87, 0.83), crack_glow=0.42, cell_size=0.17,
            )
            self.add_part(v_arm, f_arm, MAT_BASALT, w_arm, col_arm)

            # Inner glowing arm Earth-Qi veins peeking through cracks
            v_ac, f_ac = build_lofted_shell(np.asarray(rings_core), cap_start=True, cap_end=True)
            self.add_part(v_ac, f_ac, MAT_AMBER_CORE, _skin_chain_smooth(v_ac, arm_segs, sharpness=8.5),
                          _bake_emissive_colors(v_ac, _compute_smooth_normals(v_ac, f_ac), self.seed + 510))

            # 2. Heavy Upper-Arm Iron Band & Elbow Guard
            mid_ua = 0.55 * p_sh + 0.45 * p_el
            v_bd, f_bd = build_torus_ring(mid_ua, [s, 0.0, 0.0], 0.155, 0.026, seg_u=22, seg_v=8, squish_z=1.35)
            self.add_part(v_bd, f_bd, MAT_DARK_IRON, _skin_rigid(len(v_bd), b_ua),
                          _bake_stone_colors(v_bd, _compute_smooth_normals(v_bd, f_bd), self.seed + 515, tint=(0.78, 0.76, 0.74)))

            # Elbow basalt spur
            v_elb, f_elb = build_basalt_column(
                p_el + np.array([-s * 0.03, 0.00, -0.09]),
                p_el + np.array([-s * 0.10, -0.03, -0.25]),
                0.068, 0.036, sides=6, stacks=2, seed=self.seed + (520 if s > 0 else 521),
            )
            self.add_part(v_elb, f_elb, MAT_BASALT, _skin_rigid(len(v_elb), b_fa, b_ua, 0.25),
                          _bake_stone_colors(v_elb, _compute_smooth_normals(v_elb, f_elb), self.seed + 520, tint=(0.86, 0.83, 0.79)), faceted=True)

            # 3. Massive Monolith Forearm Bracers ("Ho Uyen Cu Thach")
            mid_fa = 0.44 * p_el + 0.56 * p_wr
            # Outer dorsal stone slab
            v_br1, f_br1 = build_beveled_slab(
                (0.37, 0.25, 0.26), bevel=0.22, sub_div=sd,
                bend_y=0.02, taper_y=(0.94, 1.06), rock_amp=0.018, rock_cell=0.14,
                seed=self.seed + (530 if s > 0 else 531),
            )
            v_br1 = self._transform(v_br1, mid_fa + np.array([0.0, 0.055, -0.025]), _rot_euler_xyz(rx_deg=14.0))
            self.add_part(
                v_br1, f_br1, MAT_SANDSTONE, _skin_rigid(len(v_br1), b_fa),
                _bake_stone_colors(v_br1, _compute_smooth_normals(v_br1, f_br1), self.seed + 530, tint=(1.02, 0.94, 0.83)),
            )

            # Forearm bronze cuff rings (proximal + wrist)
            for t_ring, rad_r in ((0.26, 0.168), (0.82, 0.150)):
                p_rg = p_el * (1.0 - t_ring) + p_wr * t_ring
                v_rg, f_rg = build_torus_ring(p_rg, [s, 0.0, 0.0], rad_r, 0.026, seg_u=24, seg_v=8, squish_z=1.4)
                self.add_part(v_rg, f_rg, MAT_BRONZE, _skin_rigid(len(v_rg), b_fa),
                              _bake_bronze_colors(v_rg, _compute_smooth_normals(v_rg, f_rg), self.seed + 540))

            # Subtle mineral crystal erupting from outer forearm bracer
            c_orig = mid_fa + np.array([-s * 0.04, 0.13, -0.08])
            c_dir = np.array([-s * 0.45, 0.72, -0.52])
            v_fc, f_fc = build_crystal_prism(c_orig, c_dir, 0.17, 0.024, sides=6, seed=self.seed + (545 if s > 0 else 546))
            self.add_part(v_fc, f_fc, MAT_CRYSTAL, _skin_rigid(len(v_fc), b_fa), _bake_crystal_colors(v_fc, c_orig, c_dir, 0.17), faceted=True)

            # 4. Colossal Martial-Arts Stone Gauntlet Hand (Palm-Down, Thumb-Forward, Natural Finger Curl)
            self._build_stone_gauntlet(p_wr, s, b_ha, b_fa)

    def _build_stone_gauntlet(self, wrist_pos, s, b_hand, b_forearm):
        """Massive armored stone fist/hand aligned to T-Pose (+X Left, -X Right, palm -Y, thumb +Z)."""
        w_ha = lambda nv: _skin_rigid(nv, b_hand)
        # Palm & metacarpal monolith block
        v_pm, f_pm = build_beveled_slab(
            (0.18, 0.115, 0.185), bevel=0.26, sub_div=max(self.cfg['slab'] - 1, 4),
            rock_amp=0.009, rock_cell=0.10, seed=self.seed + (560 if s > 0 else 561),
        )
        v_pm = self._transform(v_pm, wrist_pos + np.array([s * 0.105, -0.005, 0.005]))
        self.add_part(
            v_pm, f_pm, MAT_BASALT, _skin_rigid(len(v_pm), b_hand, b_forearm, 0.10),
            _bake_stone_colors(v_pm, _compute_smooth_normals(v_pm, f_pm), self.seed + 560, tint=(0.88, 0.85, 0.80)),
        )

        # Raised bronze & sandstone knuckle-ridge plate across back of hand
        v_kn, f_kn = build_beveled_slab((0.14, 0.055, 0.185), bevel=0.24, sub_div=4, rock_amp=0.006, seed=self.seed + 565)
        v_kn = self._transform(v_kn, wrist_pos + np.array([s * 0.125, 0.055, 0.005]), _rot_euler_xyz(rz_deg=-s * 8.0))
        self.add_part(v_kn, f_kn, MAT_BRONZE, w_ha(len(v_kn)),
                      _bake_bronze_colors(v_kn, _compute_smooth_normals(v_kn, f_kn), self.seed + 565))

        # 4 articulated heavy stone fingers with natural martial-arts semi-clenched cascade curl
        finger_specs = [
            # (z_off,  len1,  len2,  r_base, curl1_deg, curl2_deg)
            ( 0.064, 0.088, 0.072, 0.025, 22.0, 48.0),  # Index
            ( 0.022, 0.095, 0.078, 0.026, 26.0, 54.0),  # Middle
            (-0.022, 0.090, 0.074, 0.025, 32.0, 62.0),  # Ring
            (-0.064, 0.076, 0.062, 0.022, 38.0, 68.0),  # Pinky
        ]
        for k, (z_off, l1, l2, rad, c1, c2) in enumerate(finger_specs):
            mcp = wrist_pos + np.array([s * 0.185, 0.008, z_off])
            a1 = np.radians(c1)
            d1 = np.array([s * np.cos(a1), -np.sin(a1), 0.0])
            pip = mcp + d1 * l1
            a2 = np.radians(c1 + c2)
            d2 = np.array([s * np.cos(a2), -np.sin(a2), 0.0])
            tip = pip + d2 * l2

            v_f1, f_f1 = build_basalt_column(mcp, pip, rad, rad * 0.88, sides=6, stacks=2, tilt_tip=0.10, seed=self.seed + 570 + k)
            v_f2, f_f2 = build_basalt_column(pip - d1 * 0.01, tip, rad * 0.86, rad * 0.68, sides=6, stacks=2, tilt_tip=0.15, seed=self.seed + 580 + k)
            for vf, ff in ((v_f1, f_f1), (v_f2, f_f2)):
                self.add_part(
                    vf, ff, MAT_SANDSTONE, w_ha(len(vf)),
                    _bake_stone_colors(vf, _compute_smooth_normals(vf, ff), self.seed + 575 + k, tint=(0.95, 0.89, 0.80)),
                    faceted=True,
                )

        # Heavy opposing stone thumb (+Z side, tucked slightly inward/downward)
        th0 = wrist_pos + np.array([s * 0.085, -0.020, 0.092])
        th1 = th0 + np.array([s * 0.075, -0.048, 0.068])
        th2 = th1 + np.array([s * 0.055, -0.062, 0.025])
        for p_a, p_b, ra, rb, sd_t in ((th0, th1, 0.031, 0.027, 590), (th1, th2, 0.026, 0.021, 591)):
            v_th, f_th = build_basalt_column(p_a, p_b, ra, rb, sides=6, stacks=2, tilt_tip=0.12, seed=self.seed + sd_t)
            self.add_part(
                v_th, f_th, MAT_SANDSTONE, w_ha(len(v_th)),
                _bake_stone_colors(v_th, _compute_smooth_normals(v_th, f_th), self.seed + sd_t, tint=(0.95, 0.89, 0.80)),
                faceted=True,
            )

    # -------------------------------------------------------------------------
    # F. WAIST BELT, SEGMENTED STONE TASSETS & PILLAR LEGS / SABATONS
    # -------------------------------------------------------------------------
    def build_waist_tassets(self):
        sd = self.cfg['slab']
        b_hips = BONE_INDEX['Hips']
        b_lul = BONE_INDEX['LeftUpLeg']
        b_rul = BONE_INDEX['RightUpLeg']

        # Heavy bronze & dark-iron waist girdle ring
        v_blt, f_blt = build_torus_ring([0.0, 1.54, 0.00], [0.0, 1.0, 0.0], 0.34, 0.062, seg_u=32, seg_v=10, squish_z=1.15)
        # Flatten front/back slightly to hug torso oval
        v_blt[:, 2] *= 0.82
        self.add_part(v_blt, f_blt, MAT_DARK_IRON, _skin_rigid(len(v_blt), b_hips),
                      _bake_stone_colors(v_blt, _compute_smooth_normals(v_blt, f_blt), self.seed + 600, tint=(0.80, 0.78, 0.75)))

        # Central bronze belt buckle (carved Earth-Mountain crest)
        v_bk, f_bk = build_beveled_slab((0.20, 0.17, 0.085), bevel=0.22, sub_div=4, bend_x=0.025, taper_y=(0.82, 1.04))
        v_bk = self._transform(v_bk, [0.0, 1.53, 0.31])
        self.add_part(v_bk, f_bk, MAT_BRONZE, _skin_rigid(len(v_bk), b_hips),
                      _bake_bronze_colors(v_bk, _compute_smooth_normals(v_bk, f_bk), self.seed + 605))

        # Front & Back central armored stone loincloth / banner slabs
        for z_dir, s_id in ((1.0, 610), (-1.0, 611)):
            v_lc, f_lc = build_beveled_slab(
                (0.24, 0.56, 0.068), bevel=0.22, sub_div=max(sd - 1, 4),
                bend_x=0.02, taper_y=(0.76, 1.04), rock_amp=0.011, seed=self.seed + s_id,
            )
            v_lc = self._transform(v_lc, [0.0, 1.24, z_dir * 0.285], _rot_euler_xyz(rx_deg=-z_dir * 7.0))
            self.add_part(
                v_lc, f_lc, MAT_SANDSTONE, _skin_rigid(len(v_lc), b_hips),
                _bake_stone_colors(v_lc, _compute_smooth_normals(v_lc, f_lc), self.seed + s_id, tint=(0.98, 0.90, 0.80)),
            )
            # Bronze lower rim bar
            v_rm, f_rm = build_beveled_slab((0.19, 0.045, 0.076), bevel=0.25, sub_div=3)
            v_rm = self._transform(v_rm, [0.0, 0.98, z_dir * 0.315], _rot_euler_xyz(rx_deg=-z_dir * 7.0))
            self.add_part(v_rm, f_rm, MAT_BRONZE, _skin_rigid(len(v_rm), b_hips),
                          _bake_bronze_colors(v_rm, _compute_smooth_normals(v_rm, f_rm), self.seed + s_id + 5))

        # 6 Lateral Segmented Stone Tassets hanging around thighs (cleanly weighted to UpLeg/Hips)
        for s, b_leg in ((1.0, b_lul), (-1.0, b_rul)):
            tasset_angles = [
                # (yaw_deg, x_off, z_off, width, height)
                ( 28.0, 0.24,  0.22, 0.24, 0.44),
                ( 86.0, 0.37,  0.01, 0.28, 0.48),
                (144.0, 0.24, -0.22, 0.24, 0.44),
            ]
            for k, (yaw, xo, zo, tw, th) in enumerate(tasset_angles):
                v_ts, f_ts = build_beveled_slab(
                    (tw, th, 0.068), bevel=0.22, sub_div=max(sd - 1, 4),
                    bend_x=0.025, taper_y=(1.08, 0.86), rock_amp=0.012, seed=self.seed + 620 + k + (10 if s < 0 else 0),
                )
                R_ts = _rot_euler_xyz(rx_deg=-10.0) @ _rot_euler_xyz(ry_deg=s * yaw)
                # Correct order: flare outward after yawing around pelvis
                R_ts = _rot_euler_xyz(ry_deg=s * yaw) @ _rot_euler_xyz(rx_deg=-11.0)
                v_ts = self._transform(v_ts, [s * xo, 1.30, zo], R_ts)
                # Upper part tracks Hips, lower part tracks UpLeg smoothly so thighs never clip through!
                t_blend = _smoothstep(1.48, 1.12, v_ts[:, 1]) * 0.72
                w_ts = np.zeros((len(v_ts), 20), dtype=np.float64)
                w_ts[:, b_hips] = 1.0 - t_blend
                w_ts[:, b_leg] = t_blend
                self.add_part(
                    v_ts, f_ts, MAT_SANDSTONE if k == 1 else MAT_BASALT, w_ts,
                    _bake_stone_colors(
                        v_ts, _compute_smooth_normals(v_ts, f_ts),
                        self.seed + 620 + k, tint=(1.00, 0.92, 0.82) if k == 1 else (0.90, 0.87, 0.82),
                    ),
                )

    def build_legs_and_sabatons(self):
        nu, nv = self.cfg['limb_u'], self.cfg['limb_v']
        sd = self.cfg['slab']
        theta = np.linspace(0.0, 2.0 * np.pi, nu, endpoint=False)
        ct, st = np.cos(theta), np.sin(theta)

        for side, s in (('Left', 1.0), ('Right', -1.0)):
            p_hip = self.J[f'{side}UpLeg']
            p_kn = self.J[f'{side}Leg']
            p_an = self.J[f'{side}Foot']
            b_hips = BONE_INDEX['Hips']
            b_ul = BONE_INDEX[f'{side}UpLeg']
            b_lg = BONE_INDEX[f'{side}Leg']
            b_ft = BONE_INDEX[f'{side}Foot']

            # 1. Massive Striated Basalt Pillar Legs (Thigh -> Knee -> Thick Calf)
            rings_leg = []
            for k in range(nv):
                u = k / (nv - 1)
                if u <= 0.5:
                    t = u * 2.0
                    pos = p_hip * (1.0 - t) + p_kn * t
                    rx = 0.185 * (1.0 - t) + 0.138 * t + 0.022 * np.sin(np.pi * t)
                    rz = 0.195 * (1.0 - t) + 0.145 * t + 0.024 * np.sin(np.pi * t)
                else:
                    t = (u - 0.5) * 2.0
                    pos = p_kn * (1.0 - t) + p_an * t
                    # Broad heroic calf tapering into armored ankle
                    rx = 0.140 + 0.036 * np.sin(np.pi * (t ** 0.8)) - 0.014 * t
                    rz = 0.148 + 0.044 * np.sin(np.pi * (t ** 0.8)) - 0.014 * t
                sx = np.sign(ct) * (np.abs(ct) ** 0.84)
                sz = np.sign(st) * (np.abs(st) ** 0.84)
                rings_leg.append(np.column_stack([pos[0] + rx * sx, np.full(nu, pos[1]), pos[2] + rz * sz]))

            v_lg, f_lg = build_lofted_shell(np.asarray(rings_leg), cap_start=True, cap_end=True)
            n_est = _compute_smooth_normals(v_lg, f_lg)
            f1, edge, _ = _voronoi_rock(v_lg, cell_size=0.19, seed=self.seed + (700 if s > 0 else 701))
            crack = 1.0 - _smoothstep(0.0, 0.11, edge)
            v_lg += n_est * (0.022 * (0.48 - f1) - 0.024 * crack)[:, None]

            leg_segs = [
                (b_hips, self.J['Hips'], p_hip, 0.65),
                (b_ul, p_hip, p_kn, 1.25),
                (b_lg, p_kn, p_an, 1.30),
                (b_ft, p_an, p_an + np.array([0.0, -0.14, 0.12]), 0.75),
            ]
            w_lg = _skin_chain_smooth(v_lg, leg_segs, sharpness=8.0)
            col_lg = _bake_stone_colors(
                v_lg, _compute_smooth_normals(v_lg, f_lg),
                self.seed + (700 if s > 0 else 701), tint=(0.88, 0.85, 0.81), crack_glow=0.22, cell_size=0.19,
            )
            self.add_part(v_lg, f_lg, MAT_BASALT, w_lg, col_lg)

            # 2. Carved Mountain-Ridge Knee Guard ("Ho Goi Thach Tuong")
            v_kn, f_kn = build_beveled_slab(
                (0.24, 0.25, 0.14), bevel=0.24, sub_div=sd,
                bend_x=0.055, taper_y=(0.76, 1.08), rock_amp=0.014, seed=self.seed + (710 if s > 0 else 711),
            )
            # Prominent central stone ridge crest
            v_kn[:, 2] += 0.032 * np.exp(-0.5 * (v_kn[:, 0] / 0.045) ** 2)
            v_kn = self._transform(v_kn, p_kn + np.array([0.0, 0.02, 0.155]), _rot_euler_xyz(rx_deg=-8.0, ry_deg=s * 6.0))
            self.add_part(
                v_kn, f_kn, MAT_SANDSTONE, _skin_rigid(len(v_kn), b_lg, b_ul, 0.25),
                _bake_stone_colors(v_kn, _compute_smooth_normals(v_kn, f_kn), self.seed + 710, tint=(1.02, 0.94, 0.83)),
            )

            # Bronze trim crest on knee cap
            v_kbr, f_kbr = build_beveled_slab((0.065, 0.21, 0.055), bevel=0.26, sub_div=3, taper_y=(0.70, 1.05))
            v_kbr = self._transform(v_kbr, p_kn + np.array([0.0, 0.03, 0.225]), _rot_euler_xyz(rx_deg=-8.0, ry_deg=s * 6.0))
            self.add_part(v_kbr, f_kbr, MAT_BRONZE, _skin_rigid(len(v_kbr), b_lg, b_ul, 0.25),
                          _bake_bronze_colors(v_kbr, _compute_smooth_normals(v_kbr, f_kbr), self.seed + 715))

            # 3. Armored Shin Greaves (Front + Outer Lateral Basalt Plates)
            mid_shin = 0.50 * p_kn + 0.50 * p_an
            v_sh, f_sh = build_beveled_slab(
                (0.24, 0.44, 0.13), bevel=0.24, sub_div=sd,
                bend_x=0.055, taper_y=(0.82, 1.06), rock_amp=0.015, seed=self.seed + (720 if s > 0 else 721),
            )
            v_sh = self._transform(v_sh, mid_shin + np.array([s * 0.015, -0.02, 0.145]), _rot_euler_xyz(rx_deg=4.0, ry_deg=s * 8.0))
            self.add_part(
                v_sh, f_sh, MAT_SANDSTONE, _skin_rigid(len(v_sh), b_lg),
                _bake_stone_colors(v_sh, _compute_smooth_normals(v_sh, f_sh), self.seed + 720, tint=(0.98, 0.90, 0.80)),
            )

            # Iron greave strap rings around calf
            for t_s, rad_s in ((0.30, 0.175), (0.76, 0.155)):
                p_st = p_kn * (1.0 - t_s) + p_an * t_s
                v_rg, f_rg = build_torus_ring(p_st, [0.0, 1.0, 0.0], rad_s, 0.022, seg_u=22, seg_v=8, squish_z=1.35)
                self.add_part(v_rg, f_rg, MAT_DARK_IRON, _skin_rigid(len(v_rg), b_lg),
                              _bake_stone_colors(v_rg, _compute_smooth_normals(v_rg, f_rg), self.seed + 728, tint=(0.78, 0.76, 0.74)))

            # 4. Broad Grounded Stone Sabaton & 3 Armored Toe-Plates (planted flat at Y = 0.0)
            v_ft, f_ft = build_beveled_slab(
                (0.29, 0.16, 0.42), bevel=0.22, sub_div=max(sd - 1, 4),
                taper_y=(1.12, 0.80), taper_z=(1.08, 0.82), rock_amp=0.010, seed=self.seed + (740 if s > 0 else 741),
            )
            v_ft = self._transform(v_ft, [p_an[0], 0.082, p_an[2] + 0.075])
            # Flatten bottom sole cleanly at Y = 0.0
            v_ft[:, 1] = np.maximum(v_ft[:, 1], 0.0)
            self.add_part(
                v_ft, f_ft, MAT_BASALT, _skin_rigid(len(v_ft), b_ft),
                _bake_stone_colors(v_ft, _compute_smooth_normals(v_ft, f_ft), self.seed + 740, tint=(0.82, 0.79, 0.75)),
            )

            # 3 heavy stone toe-claws gripping the earth
            for k_t, (x_t, z_ext, w_t) in enumerate([(-0.088, 0.25, 0.078), (0.0, 0.29, 0.086), (0.088, 0.25, 0.078)]):
                v_toe, f_toe = build_beveled_slab(
                    (w_t, 0.095, 0.145), bevel=0.24, sub_div=3,
                    taper_y=(1.12, 0.72), rock_amp=0.006, seed=self.seed + 750 + k_t,
                )
                v_toe = self._transform(v_toe, [p_an[0] + s * x_t, 0.048, p_an[2] + z_ext], _rot_euler_xyz(rx_deg=10.0, ry_deg=s * x_t * 95.0))
                v_toe[:, 1] = np.maximum(v_toe[:, 1], 0.0)
                self.add_part(
                    v_toe, f_toe, MAT_SANDSTONE, _skin_rigid(len(v_toe), b_ft),
                    _bake_stone_colors(v_toe, _compute_smooth_normals(v_toe, f_toe), self.seed + 750 + k_t, tint=(0.94, 0.87, 0.78)),
                )

            # Bronze instep armor arch
            v_ins, f_ins = build_beveled_slab((0.20, 0.08, 0.18), bevel=0.28, sub_div=3, bend_x=0.03)
            v_ins = self._transform(v_ins, [p_an[0], 0.145, p_an[2] + 0.12], _rot_euler_xyz(rx_deg=24.0))
            self.add_part(v_ins, f_ins, MAT_BRONZE, _skin_rigid(len(v_ins), b_ft),
                          _bake_bronze_colors(v_ins, _compute_smooth_normals(v_ins, f_ins), self.seed + 760))

    # -------------------------------------------------------------------------
    # G. BUILD ALL & ASSEMBLE
    # -------------------------------------------------------------------------
    def build_all(self):
        self.build_torso_and_core()
        self.build_torso_armor_and_spines()
        self.build_head_and_crown()
        self.build_pauldrons()
        self.build_arms_and_gauntlets()
        self.build_waist_tassets()
        self.build_legs_and_sabatons()
        return self.parts


# =============================================================================
# 6. GLB 2.0 EXPORTER, RIG VALIDATION & OPTIONAL REF-RIG ROTATION ALIGNMENT
# =============================================================================

def _read_ref_rig_rotations(ref_glb_path):
    """Optionally read bind-pose joint rotations from an existing engine GLB (e.g. human_body.glb)."""
    b = Path(ref_glb_path).read_bytes()
    magic, ver, _ = struct.unpack_from('<4sII', b)
    if magic != b'glTF' or ver != 2:
        raise ValueError(f'Not a valid GLB 2.0: {ref_glb_path}')
    p = 12
    doc = {}
    while p < len(b):
        n, t = struct.unpack_from('<I4s', b, p)
        p += 8
        if t == b'JSON':
            doc = json.loads(b[p:p + n])
            break
        p += n
    rot_map = {}
    for nd in doc.get('nodes', []):
        nm = nd.get('name', '')
        if nm:
            rot_map[nm] = nd.get('rotation', [0.0, 0.0, 0.0, 1.0])
    return rot_map


def export_boss_glb(parts_by_mat, dst_path, target_height=2.95, ref_rig_path=None):
    # Merge primitives per material
    merged = []
    min_y = 1e9
    max_y = -1e9
    for mat_idx in sorted(parts_by_mat.keys()):
        plist = parts_by_mat[mat_idx]
        if not plist:
            continue
        vs, ns, uvs, ws, cols, fs = [], [], [], [], [], []
        v_off = 0
        for p in plist:
            vs.append(p['v'])
            ns.append(p['n'])
            uvs.append(p['uv'])
            ws.append(p['w'])
            cols.append(p['col'])
            fs.append(p['f'] + v_off)
            v_off += len(p['v'])
        v = np.vstack(vs)
        min_y = min(min_y, float(v[:, 1].min()))
        max_y = max(max_y, float(v[:, 1].max()))
        merged.append(dict(
            mat=mat_idx,
            v=v,
            n=np.vstack(ns),
            uv=np.vstack(uvs),
            w20=np.vstack(ws),
            col=np.vstack(cols),
            f=np.vstack(fs),
        ))

    # Uniform scale to exact target_height + strict ground alignment (min_y == 0.0)
    raw_h = max(max_y - min_y, 1e-6)
    scale = float(target_height) / raw_h
    for m in merged:
        m['v'][:, 1] -= min_y
        m['v'] *= scale

    # Build 20-bone Mixamo T-Pose hierarchy scaled identically to the mesh
    ref_rots = _read_ref_rig_rotations(ref_rig_path) if ref_rig_path else {}
    world_pos = np.array([pos for _, _, _, pos in BOSS_SKELETON_SPEC], dtype=np.float64)
    world_pos[:, 1] -= min_y
    world_pos *= scale

    children_map = {i: [] for i in range(20)}
    for i, _, par, _ in BOSS_SKELETON_SPEC:
        if par >= 0:
            children_map[par].append(i)

    M_world = np.zeros((20, 4, 4), dtype=np.float64)
    nodes = []
    for i, name, par, _ in BOSS_SKELETON_SPEC:
        rot = [float(x) for x in ref_rots.get(name, [0.0, 0.0, 0.0, 1.0])]
        R_loc = _quat_to_mat(rot)
        if par < 0:
            tr_loc = world_pos[i]
            M_loc = np.eye(4)
            M_loc[:3, :3] = R_loc
            M_loc[:3, 3] = tr_loc
            M_world[i] = M_loc
        else:
            # Compute local translation in parent's rotated frame so world_pos[i] is exact
            R_par = M_world[par, :3, :3]
            tr_loc = R_par.T @ (world_pos[i] - M_world[par, :3, 3])
            M_loc = np.eye(4)
            M_loc[:3, :3] = R_loc
            M_loc[:3, 3] = tr_loc
            M_world[i] = M_world[par] @ M_loc

        nd = {'name': name, 'translation': [float(x) for x in tr_loc]}
        if rot != [0.0, 0.0, 0.0, 1.0]:
            nd['rotation'] = rot
        if children_map[i]:
            nd['children'] = children_map[i]
        nodes.append(nd)

    nodes.append({'name': 'Boss_EarthColossus_Mesh', 'mesh': 0, 'skin': 0})
    nodes.append({'name': 'Boss_EarthColossus_Armature', 'children': [20, 0]})

    ibm = np.linalg.inv(M_world).transpose(0, 2, 1).reshape(-1, 16).astype('<f4')

    # AAA Grounded PBR Materials (all watertight meshes use doubleSided=False)
    def _pbr(name, rgba, metallic, roughness, emissive=None):
        mat = {
            'name': name,
            'doubleSided': False,
            'pbrMetallicRoughness': {
                'baseColorFactor': list(rgba),
                'metallicFactor': float(metallic),
                'roughnessFactor': float(roughness),
            },
        }
        if emissive is not None:
            mat['emissiveFactor'] = list(emissive)
        return mat

    materials = [
        _pbr('Basalt_DarkStone',     [0.34, 0.31, 0.28, 1.0], 0.06, 0.88),
        _pbr('Sandstone_CarvedSlab', [0.58, 0.48, 0.36, 1.0], 0.04, 0.84),
        _pbr('AncientBronze_Patina', [0.56, 0.44, 0.24, 1.0], 0.84, 0.40),
        _pbr('EarthQi_AmberCore',    [0.95, 0.56, 0.14, 1.0], 0.00, 0.32, emissive=[0.82, 0.38, 0.06]),
        _pbr('SmokyQuartz_Crystal',  [0.78, 0.54, 0.22, 1.0], 0.14, 0.20, emissive=[0.24, 0.11, 0.02]),
        _pbr('IronStraps_DarkMetal', [0.22, 0.21, 0.20, 1.0], 0.80, 0.50),
    ]

    data = bytearray()
    buffer_views = []
    accessors = []

    def add(arr, typ, ct, target=None, bounds=False, normalized=False):
        a = np.ascontiguousarray(arr, dtype=CT[ct])
        data.extend(b'\0' * ((-len(data)) % 4))
        off = len(data)
        data.extend(a.tobytes())
        bv = {'buffer': 0, 'byteOffset': off, 'byteLength': a.nbytes}
        if target:
            bv['target'] = target
        buffer_views.append(bv)
        acc = {'bufferView': len(buffer_views) - 1, 'componentType': ct, 'count': len(a), 'type': typ}
        if bounds:
            acc.update(min=a.min(0).tolist(), max=a.max(0).tolist())
        if normalized:
            acc['normalized'] = True
        accessors.append(acc)
        return len(accessors) - 1

    ibm_acc = add(ibm, 'MAT4', 5126)
    prims_out = []
    total_v = 0
    total_t = 0

    for m in merged:
        v = m['v'].astype(np.float32)
        n = m['n'] / np.maximum(np.linalg.norm(m['n'], axis=1, keepdims=True), 1e-12)
        n = n.astype(np.float32)
        uv = m['uv'].astype(np.float32)
        w20 = m['w20']

        # Top-4 deduplicated joint weights, normalized to sum == 1.0
        j4 = np.argsort(-w20, axis=1)[:, :4]
        w4 = np.take_along_axis(w20, j4, axis=1)
        w4[w4 < 1e-7] = 0.0
        w4 /= np.maximum(w4.sum(axis=1, keepdims=True), 1e-12)
        j4[w4 == 0.0] = 0

        col_u8 = np.clip(np.round(m['col'] * 255.0), 0, 255).astype(np.uint8)
        idx = m['f'].ravel()
        idx_ct = 5123 if len(v) < 65536 else 5125

        # Strict integrity assertions matching engine contract
        assert np.isfinite(v).all() and np.isfinite(n).all() and np.isfinite(w4).all()
        assert (j4 < 20).all() and idx.max() < len(v)
        assert float(np.max(np.abs(w4.sum(axis=1) - 1.0))) < 1e-5

        attrs = {
            'POSITION':   add(v, 'VEC3', 5126, 34962, bounds=True),
            'NORMAL':     add(n, 'VEC3', 5126, 34962),
            'TEXCOORD_0': add(uv, 'VEC2', 5126, 34962),
            'JOINTS_0':   add(j4.astype(np.uint8), 'VEC4', 5121, 34962),
            'WEIGHTS_0':  add(w4.astype(np.float32), 'VEC4', 5126, 34962),
            'COLOR_0':    add(col_u8, 'VEC4', 5121, 34962, normalized=True),
        }
        prims_out.append({
            'attributes': attrs,
            'indices': add(idx, 'SCALAR', idx_ct, 34963),
            'material': m['mat'],
            'mode': 4,
        })
        total_v += len(v)
        total_t += len(idx) // 3

    out_doc = {
        'asset': {
            'version': '2.0',
            'generator': 'Earth Colossus Boss — AAA 20-Bone Mixamo T-Pose Pipeline',
        },
        'scene': 0,
        'scenes': [{'name': 'Scene', 'nodes': [21]}],
        'nodes': nodes,
        'skins': [{'name': 'Boss_EarthColossus_Armature', 'joints': list(range(20)), 'inverseBindMatrices': ibm_acc}],
        'materials': materials,
        'meshes': [{'name': 'Boss_EarthColossus_Game20', 'primitives': prims_out}],
        'accessors': accessors,
        'bufferViews': buffer_views,
        'buffers': [{'byteLength': len(data)}],
    }

    js = json.dumps(out_doc, separators=(',', ':')).encode('utf-8')
    js += b' ' * ((-len(js)) % 4)
    data.extend(b'\0' * ((-len(data)) % 4))
    dst_path = Path(dst_path)
    dst_path.parent.mkdir(parents=True, exist_ok=True)
    dst_path.write_bytes(
        struct.pack('<4sII', b'glTF', 2, 28 + len(js) + len(data))
        + struct.pack('<I4s', len(js), b'JSON') + js
        + struct.pack('<I4s', len(data), b'BIN\0') + data
    )

    # Verify inverse bind matrices & topological order on written file
    ibm_chk = ibm.reshape(-1, 4, 4).transpose(0, 2, 1)
    for j in range(20):
        err = float(np.max(np.abs(ibm_chk[j] @ M_world[j] - np.eye(4))))
        assert err < 1e-5, f'IBM mismatch on bone {j}: {err}'
    all_v = np.vstack([m['v'] for m in merged])
    size_kb = dst_path.stat().st_size / 1024.0
    print(
        f'Earth Boss GLB: {dst_path} | {size_kb:.1f} KB | verts={total_v} tris={total_t} '
        f'| draw_calls={len(prims_out)} | bounds={np.round(all_v.min(0), 3).tolist()}..{np.round(all_v.max(0), 3).tolist()}'
    )


# =============================================================================
# 7. OPTIONAL 4-VIEW VTK PREVIEW RENDERER (MATCHING male_body_v6.py)
# =============================================================================

def render_preview(glb_path, out_png_path):
    import vtk
    from vtk.util.numpy_support import numpy_to_vtk, numpy_to_vtkIdTypeArray
    from PIL import Image, ImageDraw, ImageFont

    b = Path(glb_path).read_bytes()
    p = 12
    d, bin_buf = {}, b''
    while p < len(b):
        n, t = struct.unpack_from('<I4s', b, p)
        p += 8
        if t == b'JSON':
            d = json.loads(b[p:p + n])
        elif t == b'BIN\0':
            bin_buf = b[p:p + n]
        p += n

    def read_acc(i, normalize=False):
        a = d['accessors'][i]
        bv = d['bufferViews'][a['bufferView']]
        dt = np.dtype(CT[a['componentType']])
        k = DIM[a['type']]
        arr = np.ndarray(
            (a['count'], k), dtype=dt, buffer=bin_buf,
            offset=bv.get('byteOffset', 0) + a.get('byteOffset', 0),
            strides=(bv.get('byteStride', k * dt.itemsize), dt.itemsize),
        ).copy()
        if normalize and a.get('normalized') and dt.kind in 'iu':
            arr = arr.astype(float) / np.iinfo(dt).max
        return arr

    r = vtk.vtkRenderer()
    r.SetBackground(0.09, 0.11, 0.14)
    win = vtk.vtkRenderWindow()
    win.SetOffScreenRendering(1)
    win.AddRenderer(r)
    win.SetSize(640, 920)
    win.SetMultiSamples(4)

    all_v = []
    for prim in d['meshes'][0]['primitives']:
        at = prim['attributes']
        v = read_acc(at['POSITION'])
        n = read_acc(at['NORMAL'])
        f = read_acc(prim['indices']).reshape(-1, 3)
        all_v.append(v)

        pts = vtk.vtkPoints()
        pts.SetData(numpy_to_vtk(v, deep=True))
        cells = vtk.vtkCellArray()
        cells.SetCells(len(f), numpy_to_vtkIdTypeArray(np.c_[np.full(len(f), 3), f].astype(np.int64).ravel(), deep=True))
        poly = vtk.vtkPolyData()
        poly.SetPoints(pts)
        poly.SetPolys(cells)
        poly.GetPointData().SetNormals(numpy_to_vtk(n, deep=True))
        mapper = vtk.vtkPolyDataMapper()
        mapper.SetInputData(poly)
        act = vtk.vtkActor()
        act.SetMapper(mapper)

        mat = d['materials'][prim['material']]
        pbr = mat['pbrMetallicRoughness']
        col = np.array(pbr['baseColorFactor'][:3], dtype=float)
        em = np.array(mat.get('emissiveFactor', [0.0, 0.0, 0.0]), dtype=float)
        rough = pbr.get('roughnessFactor', 0.7)
        metal = pbr.get('metallicFactor', 0.0)

        act.GetProperty().SetSpecular(0.15 + 0.55 * metal + 0.25 * (1.0 - rough))
        act.GetProperty().SetSpecularPower(12.0 + 70.0 * (1.0 - rough))
        if float(np.max(em)) > 0.05:
            act.GetProperty().SetAmbient(0.85)
            act.GetProperty().SetDiffuse(0.35)
        else:
            act.GetProperty().SetAmbient(0.12)
            act.GetProperty().SetDiffuse(0.88)

        if 'COLOR_0' in at:
            c_arr = read_acc(at['COLOR_0'], True)[:, :3]
            lin = np.clip(c_arr * col[None, :] + 0.65 * em[None, :], 0.0, 1.0)
            rgb = np.clip(np.power(lin, 1.0 / 2.2) * 255.0, 0, 255).astype(np.uint8)
            poly.GetPointData().SetScalars(numpy_to_vtk(rgb, deep=True))
            mapper.SetColorModeToDirectScalars()
            mapper.ScalarVisibilityOn()
            act.GetProperty().SetColor(1, 1, 1)
        else:
            act.GetProperty().SetColor(*np.power(np.clip(col + em, 0, 1), 1.0 / 2.2))
        r.AddActor(act)

    bv = np.vstack(all_v)
    cy = 0.5 * (bv[:, 1].min() + bv[:, 1].max())
    height = bv[:, 1].max() - bv[:, 1].min()
    sc_full = 0.58 * height
    bust_y = bv[:, 1].min() + 0.74 * height

    for xyz, intensity, rgb_l in [
        ((3.8, 4.8, 5.5), 1.05, (1.00, 0.94, 0.86)),
        ((-4.5, 2.2, 3.0), 0.50, (0.72, 0.82, 0.96)),
        ((0.0, 3.4, -5.2), 0.85, (1.00, 0.84, 0.64)),
    ]:
        l = vtk.vtkLight()
        l.SetLightTypeToSceneLight()
        l.SetPosition(*xyz)
        l.SetFocalPoint(0, cy, 0)
        l.SetIntensity(intensity)
        l.SetColor(*rgb_l)
        r.AddLight(l)

    cam = r.GetActiveCamera()
    cam.ParallelProjectionOn()
    views = [
        ('FRONT',        (0.0, cy, 6.5),        (0.0, cy, 0.0),       sc_full),
        ('3/4 ISO VIEW', (4.4, cy + 0.75, 5.2), (0.0, cy, 0.0),       sc_full),
        ('BACK',         (0.0, cy, -6.5),       (0.0, cy, 0.0),       sc_full),
        ('HEAD & CHEST', (0.85, bust_y + 0.1, 4.2), (0.0, bust_y, 0.05), 0.26 * height),
    ]
    tmp_png = Path(out_png_path).with_name('_boss_tmp_view.png')
    frames = []
    for title, eye, look, sc in views:
        cam.SetPosition(*eye)
        cam.SetFocalPoint(*look)
        cam.SetViewUp(0, 1, 0)
        cam.SetParallelScale(sc)
        r.ResetCameraClippingRange()
        win.Render()
        w2i = vtk.vtkWindowToImageFilter()
        w2i.SetInput(win)
        w2i.Update()
        wr = vtk.vtkPNGWriter()
        wr.SetFileName(str(tmp_png))
        wr.SetInputConnection(w2i.GetOutputPort())
        wr.Write()
        frames.append((title, Image.open(tmp_png).copy()))
    if tmp_png.exists():
        tmp_png.unlink()

    sheet = Image.new('RGB', (640 * len(frames), 980), (22, 27, 35))
    draw = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.truetype('DejaVuSans.ttf', 24)
    except OSError:
        font = ImageFont.load_default()
    for i, (title, img) in enumerate(frames):
        sheet.paste(img, (640 * i, 52))
        draw.text((640 * i + 24, 14), title, font=font, fill=(235, 225, 210))
    sheet.save(out_png_path)
    print('Preview saved:', out_png_path)


# =============================================================================
# 8. CLI ENTRY POINT
# =============================================================================

def main():
    root = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description='Generate AAA Earth Colossus Boss (20-bone Mixamo T-Pose GLB).')
    parser.add_argument('--out-dir', type=Path, default=root, help='Output directory')
    parser.add_argument('--output', type=str, default='boss_earth_colossus.glb', help='Output GLB filename')
    parser.add_argument('--height', type=float, default=2.95, help='Target boss height in meters (default: 2.95)')
    parser.add_argument('--detail', choices=['medium', 'high', 'ultra'], default='high', help='Mesh tessellation preset')
    parser.add_argument('--seed', type=int, default=2610, help='Deterministic procedural rock-fracture seed')
    parser.add_argument('--ref-rig', type=Path, default=None, help='Optional GLB (e.g. human_body.glb) to copy exact bind joint rotations')
    parser.add_argument('--game-out', type=Path, default=None, help='Optional extra copy destination in game assets')
    parser.add_argument('--preview', action='store_true', help='Render 4-view PNG preview (requires vtk, Pillow)')
    args = parser.parse_args()

    out_dir = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    dst_glb = out_dir / args.output

    assembler = BossAssembler(detail=args.detail, seed=args.seed)
    parts = assembler.build_all()
    export_boss_glb(parts, dst_glb, target_height=args.height, ref_rig_path=args.ref_rig)

    if args.game_out is not None:
        args.game_out.parent.mkdir(parents=True, exist_ok=True)
        args.game_out.write_bytes(dst_glb.read_bytes())
        print('Copied game-ready Boss GLB ->', args.game_out)

    if args.preview:
        render_preview(dst_glb, out_dir / (Path(args.output).stem + '_preview.png'))


if __name__ == '__main__':
    main()