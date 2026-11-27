#!/usr/bin/env python3
"""
generate_wuxia_grooming.py — Standalone Engine-Ready Wuxia Hero Grooming Generator
(Layered Wuxia Hair + Sword-Brows + Eyelashes + Refined Martial-Arts Beard in 1 GLB)

Pure Python + NumPy (NO Blender, NO Pillow, NO external dependencies).
Conformally fits a heroic, handsome Wuxia swordsman grooming set onto
assets/characters/bodies/human_body.glb:
  1. Hair (Tóc Kiếm Khách Cổ Trang): layered sculpted 3D hair clumps sweeping back
     from an off-center part (zero flat helmet/bowl-cut!), windswept asymmetric front
     bangs (Tóc Mái), temple framing locks, backward-tilted antique-gold & celadon-jade
     hair coronet (Phát Quán) + carved jade hairpin (Trâm Ngọc), and a lush cascading
     high ponytail + layered back mane driven by 8 engine-native cloth_hair_* spring bones.
  2. Eyebrows (Lông Mày Kiếm Mi): cohesive sculpted 3D sword-brows aligned directly over
     the brow ridge in natural dark espresso-charcoal.
  3. Eyelashes (Lông Mi): crisp masculine upper & lower eyelid lash fringes.
  4. Beard & Mustache (Râu Kiếm Khách): sleek, close-cropped, natural dark-charcoal
     mustache + contoured chin/jawline trim + temple sideburns (no cartoonish goatee).

RUN:
    python3 tools/generate_wuxia_grooming.py
    python3 tools/generate_wuxia_grooming.py --install
    python3 tools/generate_wuxia_grooming.py --no-beard --out custom.glb
"""

import argparse
import json
import math
import struct
import zlib
from pathlib import Path
import numpy as np


# ==============================================================================
# 1. EXACT GAME_MIXAMO_20 T-POSE SKELETON & MATERIAL IDS
# ==============================================================================

GAME_MIXAMO_20 = [
    ("mixamorig:Hips",          -1, [0.0,   0.95, 0.0], [0.0,  0.0,  0.0,  1.0]),
    ("mixamorig:Spine",          0, [0.0,   0.13, 0.0], [0.0,  0.0,  0.0,  1.0]),
    ("mixamorig:Spine1",         1, [0.0,   0.14, 0.0], [0.0,  0.0,  0.0,  1.0]),
    ("mixamorig:Spine2",         2, [0.0,   0.14, 0.0], [0.0,  0.0,  0.0,  1.0]),
    ("mixamorig:Neck",           3, [0.0,   0.16, 0.0], [0.0,  0.0,  0.0,  1.0]),
    ("mixamorig:Head",           4, [0.0,   0.14, 0.0], [0.0,  0.0,  0.0,  1.0]),
    ("mixamorig:LeftShoulder",   3, [0.08,  0.12, 0.0], [-0.5, -0.5, -0.5, 0.5]),
    ("mixamorig:LeftArm",        6, [0.0,   0.14, 0.0], [0.0,  0.0,  0.0,  1.0]),
    ("mixamorig:LeftForeArm",    7, [0.0,   0.26, 0.0], [0.0,  0.0,  0.0,  1.0]),
    ("mixamorig:LeftHand",       8, [0.0,   0.24, 0.0], [0.0,  0.0,  0.0,  1.0]),
    ("mixamorig:RightShoulder",  3, [-0.08, 0.12, 0.0], [-0.5,  0.5,  0.5, 0.5]),
    ("mixamorig:RightArm",      10, [0.0,   0.14, 0.0], [0.0,  0.0,  0.0,  1.0]),
    ("mixamorig:RightForeArm",  11, [0.0,   0.26, 0.0], [0.0,  0.0,  0.0,  1.0]),
    ("mixamorig:RightHand",     12, [0.0,   0.24, 0.0], [0.0,  0.0,  0.0,  1.0]),
    ("mixamorig:LeftUpLeg",      0, [0.10, -0.05, 0.0], [-1.0,  0.0,  0.0, 0.0]),
    ("mixamorig:LeftLeg",       14, [0.0,   0.42, 0.0], [0.0,  0.0,  0.0,  1.0]),
    ("mixamorig:LeftFoot",      15, [0.0,   0.40, 0.0], [0.0,  0.0,  0.0,  1.0]),
    ("mixamorig:RightUpLeg",     0, [-0.10, -0.05, 0.0], [-1.0, 0.0,  0.0, 0.0]),
    ("mixamorig:RightLeg",      17, [0.0,   0.42, 0.0], [0.0,  0.0,  0.0,  1.0]),
    ("mixamorig:RightFoot",     18, [0.0,   0.40, 0.0], [0.0,  0.0,  0.0,  1.0]),
]

MAT_HAIR = 0   # Deep ink-charcoal (Huyền Mặc) fine hair-strand silk
MAT_GOLD = 1   # Antique burnished imperial gold coronet & hairpin caps
MAT_JADE = 2   # Luminous celadon-jade hairpin shaft & crown inlay


# ==============================================================================
# 2. LINEAR ALGEBRA & CONFORMAL HEAD-SURFACE SAMPLER
# ==============================================================================

def _smooth(a: float, b: float, x):
    t = np.clip((x - a) / max(b - a, 1e-12), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def _quat_to_mat(q) -> np.ndarray:
    x, y, z, w = q
    n = math.sqrt(x*x + y*y + z*z + w*w)
    if n > 1e-12:
        x, y, z, w = x/n, y/n, z/n, w/n
    return np.array([
        [1 - 2*(y*y + z*z), 2*(x*y - z*w),     2*(x*z + y*w)],
        [2*(x*y + z*w),     1 - 2*(x*x + z*z), 2*(y*z - x*w)],
        [2*(x*z - y*w),     2*(y*z + x*w),     1 - 2*(x*x + y*y)],
    ], dtype=np.float64)


def _mat_to_quat(R: np.ndarray) -> list:
    tr = float(R[0, 0] + R[1, 1] + R[2, 2])
    if tr > 0.0:
        s = 2.0 * math.sqrt(tr + 1.0)
        w = 0.25 * s
        x = (R[2, 1] - R[1, 2]) / s
        y = (R[0, 2] - R[2, 0]) / s
        z = (R[1, 0] - R[0, 1]) / s
    elif R[0, 0] > R[1, 1] and R[0, 0] > R[2, 2]:
        s = 2.0 * math.sqrt(1.0 + R[0, 0] - R[1, 1] - R[2, 2])
        w = (R[2, 1] - R[1, 2]) / s
        x = 0.25 * s
        y = (R[0, 1] + R[1, 0]) / s
        z = (R[0, 2] + R[2, 0]) / s
    elif R[1, 1] > R[2, 2]:
        s = 2.0 * math.sqrt(1.0 + R[1, 1] - R[0, 0] - R[2, 2])
        w = (R[0, 2] - R[2, 0]) / s
        x = (R[0, 1] + R[1, 0]) / s
        y = 0.25 * s
        z = (R[1, 2] + R[2, 1]) / s
    else:
        s = 2.0 * math.sqrt(1.0 + R[2, 2] - R[0, 0] - R[1, 1])
        w = (R[1, 0] - R[0, 1]) / s
        x = (R[0, 2] + R[2, 0]) / s
        y = (R[1, 2] + R[2, 1]) / s
        z = 0.25 * s
    n = math.sqrt(x*x + y*y + z*z + w*w)
    if n > 1e-12:
        x, y, z, w = x/n, y/n, z/n, w/n
    return [float(x), float(y), float(z), float(w)]


def _make_bone_basis(y_dir, hint_z=(0.0, 0.0, 1.0)) -> np.ndarray:
    y = np.asarray(y_dir, dtype=np.float64)
    y /= max(float(np.linalg.norm(y)), 1e-12)
    h = np.asarray(hint_z, dtype=np.float64)
    if abs(float(np.dot(y, h))) > 0.96:
        h = np.array([1.0, 0.0, 0.0], dtype=np.float64)
    z = h - np.dot(h, y) * y
    z /= max(float(np.linalg.norm(z)), 1e-12)
    x = np.cross(y, z)
    x /= max(float(np.linalg.norm(x)), 1e-12)
    z = np.cross(x, y)
    return np.column_stack([x, y, z])


class HeadSurfaceSampler:
    """Sub-millimeter conformal surface sampler built directly from human_body.glb head skin."""

    def __init__(self, body_glb_path: Path):
        data = body_glb_path.read_bytes()
        jlen = struct.unpack_from("<I", data, 12)[0]
        gltf = json.loads(data[20:20 + jlen])
        blob = data[20 + jlen + 8:]

        def racc(idx):
            a = gltf["accessors"][idx]
            bv = gltf["bufferViews"][a["bufferView"]]
            off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
            dt = {5121: np.uint8, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}[a["componentType"]]
            cnt = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[a["type"]]
            arr = np.frombuffer(blob, dtype=dt, count=a["count"] * cnt, offset=off)
            return arr.reshape(-1, cnt).astype(np.float64) if cnt > 1 else arr

        p0 = gltf["meshes"][0]["primitives"][0]
        pos = racc(p0["attributes"]["POSITION"])
        nrm = racc(p0["attributes"]["NORMAL"])
        m = pos[:, 1] >= 1.535
        self.pos = pos[m]
        self.nrm = nrm[m]
        self.center = np.array([0.0, 1.675, 0.022], dtype=np.float64)

        mf = (self.pos[:, 2] > 0.040) & (self.nrm[:, 2] > -0.15)
        self.f_pos = self.pos[mf]
        self.f_nrm = self.nrm[mf]

        rel = self.pos - self.center
        self.rad_len = np.linalg.norm(rel, axis=1)
        self.rad_dir = rel / np.maximum(self.rad_len[:, None], 1e-12)

    def sample_face_xy(self, x: float, y: float, z_offset: float = 0.0, radius: float = 0.008):
        d2 = (self.f_pos[:, 0] - x) ** 2 + (self.f_pos[:, 1] - y) ** 2
        idx = np.argpartition(d2, min(14, len(d2) - 1))[:14]
        w = np.exp(-d2[idx] / max(radius * radius * 0.35, 1e-8))
        w /= max(float(w.sum()), 1e-12)
        z = float(np.dot(w, self.f_pos[idx, 2]))
        n = (w[:, None] * self.f_nrm[idx]).sum(axis=0)
        n /= max(float(np.linalg.norm(n)), 1e-12)
        return np.array([x, y, z], dtype=np.float64) + n * z_offset, n

    def sample_skull_dir(self, dir_vec: np.ndarray, offset: float = 0.0025):
        d = np.asarray(dir_vec, dtype=np.float64)
        d /= max(float(np.linalg.norm(d)), 1e-12)
        dots = self.rad_dir @ d
        idx = np.argpartition(-dots, min(18, len(dots) - 1))[:18]
        ang2 = np.maximum(0.0, 1.0 - dots[idx])
        w = np.exp(-ang2 / 0.0045)
        w /= max(float(w.sum()), 1e-12)
        r_mean = float(np.dot(w, self.rad_len[idx]))
        r_max = float(self.rad_len[idx[:6]].max())
        r = 0.45 * r_mean + 0.55 * r_max + offset
        n = (w[:, None] * self.nrm[idx]).sum(axis=0)
        n /= max(float(np.linalg.norm(n)), 1e-12)
        return self.center + d * r, n


# ==============================================================================
# 3. 28-BONE SKELETON (20 MIXAMO + 8 TOPOLOGICAL cloth_hair_* SPRING BONES)
# ==============================================================================

def build_skeleton():
    """Construct 28-bone skeleton (GAME_MIXAMO_20 + 8 cloth_hair_* spring bones parented to Head)."""
    bones = []
    name_to_idx = {}

    for name, par, tr, rot in GAME_MIXAMO_20:
        loc = np.eye(4, dtype=np.float64)
        loc[:3, :3] = _quat_to_mat(rot)
        loc[:3, 3] = tr
        world = bones[par]["world"] @ loc if par >= 0 else loc
        name_to_idx[name] = len(bones)
        bones.append(dict(name=name, parent=par, tr=list(tr), rot=list(rot), world=world))

    def add_cloth_bone(name: str, parent_name: str, head_world, tail_world, hint_z=(0.0, 0.0, 1.0)):
        par = name_to_idx[parent_name]
        head_w = np.asarray(head_world, dtype=np.float64)
        tail_w = np.asarray(tail_world, dtype=np.float64)
        R_w = _make_bone_basis(tail_w - head_w, hint_z=hint_z)
        M_w = np.eye(4, dtype=np.float64)
        M_w[:3, :3] = R_w
        M_w[:3, 3] = head_w
        M_loc = np.linalg.inv(bones[par]["world"]) @ M_w
        tr = M_loc[:3, 3].tolist()
        rot = _mat_to_quat(M_loc[:3, :3])
        name_to_idx[name] = len(bones)
        bones.append(dict(name=name, parent=par, tr=tr, rot=rot, world=M_w))

    # 3 flowing back-hair & high-ponytail chains (6 bones, parented to Head)
    for b1, b2, x0, z0, z1, z2 in [
        ("cloth_hair_c_01", "cloth_hair_c_02",  0.000, -0.092, -0.145, -0.168),
        ("cloth_hair_l_01", "cloth_hair_l_02",  0.058, -0.080, -0.128, -0.152),
        ("cloth_hair_r_01", "cloth_hair_r_02", -0.058, -0.080, -0.128, -0.152),
    ]:
        p0 = [x0, 1.660, z0]
        p1 = [x0 * 1.18, 1.495, z1]
        p2 = [x0 * 1.32, 1.310, z2]
        add_cloth_bone(b1, "mixamorig:Head", p0, p1)
        add_cloth_bone(b2, b1, p1, p2)

    # 2 front windswept temple locks (Tóc Mai, 2 bones, parented to Head)
    for tag, s in [("l", 1.0), ("r", -1.0)]:
        add_cloth_bone(
            f"cloth_hair_bang_{tag}", "mixamorig:Head",
            [s * 0.068, 1.695, 0.062],
            [s * 0.078, 1.560, 0.058],
        )

    return bones, name_to_idx


# ==============================================================================
# 4. PURE-NUMPY MESH BUILDER (2-SIDED SOLID SHELLS & SCULPTED HAIR CLUMPS)
# ==============================================================================

def _grid_normals(P: np.ndarray, wrap_u: bool = False, flip: bool = False) -> np.ndarray:
    du = np.zeros_like(P)
    dv = np.zeros_like(P)
    if wrap_u:
        du = np.roll(P, -1, axis=1) - np.roll(P, 1, axis=1)
    else:
        du[:, 1:-1] = P[:, 2:] - P[:, :-2]
        du[:, 0] = P[:, 1] - P[:, 0]
        du[:, -1] = P[:, -1] - P[:, -2]
    dv[1:-1, :] = P[2:, :] - P[:-2, :]
    dv[0, :] = P[1, :] - P[0, :]
    dv[-1, :] = P[-1, :] - P[-2, :]
    n = np.cross(du, dv)
    if flip:
        n = -n
    n /= np.maximum(np.linalg.norm(n, axis=2, keepdims=True), 1e-12)
    return n


class MeshBuilder:
    def __init__(self, num_bones: int):
        self.num_bones = num_bones
        self.pos, self.uv, self.col, self.w, self.tris, self.mat = [], [], [], [], [], []
        self._vcount = 0

    def add_grid(self, P, UV, COL, W, face_mats, wrap_u=False, flip=False):
        R, C, _ = P.shape
        base = self._vcount
        self.pos.append(P.reshape(-1, 3))
        self.uv.append(UV.reshape(-1, 2))
        self.col.append(COL.reshape(-1, 3))
        self.w.append(W.reshape(-1, self.num_bones))
        self._vcount += R * C

        c_steps = C if wrap_u else (C - 1)
        tris, mats = [], []
        for r in range(R - 1):
            r0 = base + r * C
            r1 = base + (r + 1) * C
            for c in range(c_steps):
                cn = (c + 1) % C
                v00, v01 = r0 + c, r0 + cn
                v11, v10 = r1 + cn, r1 + c
                m = int(face_mats[r, c])
                if not flip:
                    tris.append((v00, v01, v11))
                    tris.append((v00, v11, v10))
                else:
                    tris.append((v00, v11, v01))
                    tris.append((v00, v10, v11))
                mats.append(m)
                mats.append(m)
        self.tris.append(np.asarray(tris, dtype=np.int32))
        self.mat.append(np.asarray(mats, dtype=np.int32))

    def _add_rim_strip(self, Pa, Pb, UVa, UVb, COLa, COLb, Wa, Wb, mats_1d, wrap=False, flip=False):
        self.add_grid(
            np.stack([Pa, Pb], axis=0),
            np.stack([UVa, UVb], axis=0),
            np.stack([COLa, COLb], axis=0),
            np.stack([Wa, Wb], axis=0),
            np.asarray(mats_1d, dtype=np.int32)[None, :],
            wrap_u=wrap,
            flip=flip,
        )

    def add_solidified_grid(
        self, P, UV, COL_out, W, face_mats, thickness=0.0020,
        wrap_u=False, flip=False, inner_shade=0.78,
        close_top=True, close_bottom=True, close_sides=True,
    ):
        R, C, _ = P.shape
        N = _grid_normals(P, wrap_u=wrap_u, flip=flip)
        P_in = P - N * thickness
        COL_in = np.clip(COL_out * inner_shade, 0.0, 1.0)
        self.add_grid(P, UV, COL_out, W, face_mats, wrap_u=wrap_u, flip=flip)
        self.add_grid(P_in, UV, COL_in, W, face_mats, wrap_u=wrap_u, flip=(not flip))

        c_steps = C if wrap_u else (C - 1)
        if close_bottom:
            self._add_rim_strip(P_in[0], P[0], UV[0], UV[0], COL_in[0], COL_out[0], W[0], W[0], face_mats[0, :c_steps], wrap=wrap_u, flip=flip)
        if close_top:
            self._add_rim_strip(P[R - 1], P_in[R - 1], UV[R - 1], UV[R - 1], COL_out[R - 1], COL_in[R - 1], W[R - 1], W[R - 1], face_mats[R - 2, :c_steps], wrap=wrap_u, flip=flip)
        if close_sides and not wrap_u:
            self._add_rim_strip(P[:, 0], P_in[:, 0], UV[:, 0], UV[:, 0], COL_out[:, 0], COL_in[:, 0], W[:, 0], W[:, 0], face_mats[:, 0], wrap=False, flip=flip)
            self._add_rim_strip(P_in[:, C - 1], P[:, C - 1], UV[:, C - 1], UV[:, C - 1], COL_in[:, C - 1], COL_out[:, C - 1], W[:, C - 1], W[:, C - 1], face_mats[:, C - 2], wrap=False, flip=flip)

    def add_tapered_tube(self, spine: np.ndarray, rx: np.ndarray, rz: np.ndarray,
                         COL_rows: np.ndarray, W_rows: np.ndarray, mat_id: int,
                         segs: int = 8, uv_v_scale: float = 2.0,
                         cap_start: bool = True, cap_end: bool = True,
                         out_hint: np.ndarray = None):
        """Build a closed, outward-normal 3D tapered hair lock/ribbon along a 3D spine.

        When out_hint is supplied (N x 3 outward normal per spine point), rx lies strictly
        tangent across the lock width and rz lies along the outward hair thickness!
        """
        segs = max(segs, 8)
        spine = np.asarray(spine, dtype=np.float64).copy()
        R = len(spine)
        if R >= 5:
            for _ in range(2):
                sp_s = spine.copy()
                sp_s[1:-1] = 0.25 * spine[:-2] + 0.50 * spine[1:-1] + 0.25 * spine[2:]
                spine = sp_s

        T = np.zeros_like(spine)
        T[1:-1] = spine[2:] - spine[:-2]
        T[0] = spine[1] - spine[0]
        T[-1] = spine[-1] - spine[-2]
        T /= np.maximum(np.linalg.norm(T, axis=1, keepdims=True), 1e-12)

        U_ax = np.zeros_like(spine)
        V_ax = np.zeros_like(spine)
        if out_hint is not None:
            for k in range(R):
                v_k = out_hint[k] - np.dot(out_hint[k], T[k]) * T[k]
                vn = float(np.linalg.norm(v_k))
                if vn < 1e-6:
                    v_k = np.array([0.0, 1.0, 0.0]) if abs(T[k, 1]) < 0.9 else np.array([0.0, 0.0, 1.0])
                    v_k = v_k - np.dot(v_k, T[k]) * T[k]
                    vn = max(float(np.linalg.norm(v_k)), 1e-12)
                V_ax[k] = v_k / vn
                U_ax[k] = np.cross(V_ax[k], T[k])
                U_ax[k] /= max(float(np.linalg.norm(U_ax[k])), 1e-12)
                if k > 0 and float(np.dot(U_ax[k], U_ax[k - 1])) < 0.0:
                    U_ax[k] = -U_ax[k]
                V_ax[k] = np.cross(T[k], U_ax[k])
        else:
            hint = np.array([1.0, 0.0, 0.0], dtype=np.float64)
            if abs(float(np.dot(T[0], hint))) > 0.90:
                hint = np.array([0.0, 0.0, 1.0], dtype=np.float64)
            u0 = hint - np.dot(hint, T[0]) * T[0]
            u0 /= max(float(np.linalg.norm(u0)), 1e-12)
            U_ax[0] = u0
            V_ax[0] = np.cross(T[0], u0)
            for k in range(1, R):
                u_p = U_ax[k - 1] - np.dot(U_ax[k - 1], T[k]) * T[k]
                un = float(np.linalg.norm(u_p))
                if un < 1e-6:
                    u_p = np.cross(T[k], V_ax[k - 1])
                    un = max(float(np.linalg.norm(u_p)), 1e-12)
                U_ax[k] = u_p / un
                V_ax[k] = np.cross(T[k], U_ax[k])

        P = np.zeros((R, segs, 3), dtype=np.float64)
        UV = np.zeros((R, segs, 2), dtype=np.float64)
        COL = np.zeros((R, segs, 3), dtype=np.float64)
        W = np.zeros((R, segs, self.num_bones), dtype=np.float64)

        for k in range(R):
            v = k / max(R - 1, 1)
            for i in range(segs):
                th = (i / segs) * 2.0 * math.pi
                # Diamond/arched silk-lock cross-section catches soft layered highlights!
                ct, st = math.cos(th), math.sin(th)
                P[k, i] = spine[k] + U_ax[k] * (rx[k] * ct) + V_ax[k] * (rz[k] * st)
                UV[k, i] = (i / segs, v * uv_v_scale)
                COL[k, i] = COL_rows[k] * (0.90 + 0.10 * max(0.0, st))
                W[k, i] = W_rows[k]

        fm = np.full((R - 1, segs), mat_id, dtype=np.int32)
        self.add_grid(P, UV, COL, W, fm, wrap_u=True, flip=False)

        def add_cap(ring_p, ring_uv, ring_col, ring_w, apex_p, reverse_winding: bool):
            base_c = self._vcount
            self.pos.append(np.vstack([apex_p[None, :], ring_p]))
            self.uv.append(np.vstack([ring_uv[0:1], ring_uv]))
            self.col.append(np.vstack([ring_col[0:1], ring_col]))
            self.w.append(np.vstack([ring_w[0:1], ring_w]))
            self._vcount += 1 + segs
            tris_c = []
            for i in range(segs):
                v1 = base_c + 1 + i
                v2 = base_c + 1 + ((i + 1) % segs)
                tris_c.append((base_c, v2, v1) if reverse_winding else (base_c, v1, v2))
            self.tris.append(np.asarray(tris_c, dtype=np.int32))
            self.mat.append(np.full(segs, mat_id, dtype=np.int32))

        if cap_start:
            apex0 = spine[0] - T[0] * (0.35 * min(rx[0], rz[0]))
            add_cap(P[0], UV[0], COL[0], W[0], apex0, reverse_winding=True)
        if cap_end:
            apex1 = spine[-1] + T[-1] * (0.35 * min(rx[-1], rz[-1]))
            add_cap(P[-1], UV[-1], COL[-1], W[-1], apex1, reverse_winding=False)

    def finalize(self):
        V = np.concatenate(self.pos, axis=0)
        UV = np.concatenate(self.uv, axis=0)
        COL = np.concatenate(self.col, axis=0)
        W = np.concatenate(self.w, axis=0)
        F = np.concatenate(self.tris, axis=0)
        M = np.concatenate(self.mat, axis=0)

        f0, f1, f2 = F[:, 0], F[:, 1], F[:, 2]
        cross = np.cross(V[f1] - V[f0], V[f2] - V[f0])
        N = np.zeros_like(V, dtype=np.float64)
        np.add.at(N, f0, cross)
        np.add.at(N, f1, cross)
        np.add.at(N, f2, cross)
        N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-12)

        W = np.maximum(W, 0.0)
        ws = W.sum(axis=1, keepdims=True)
        zero_mask = (ws[:, 0] < 1e-9)
        if zero_mask.any():
            W[zero_mask, 5] = 1.0
            ws = W.sum(axis=1, keepdims=True)
        W /= ws
        J4 = np.argsort(-W, axis=1)[:, :4]
        W4 = np.take_along_axis(W, J4, axis=1)
        W4 /= np.maximum(W4.sum(axis=1, keepdims=True), 1e-12)
        J4[W4 <= 1e-6] = 0
        return V, N, UV, COL, J4.astype(np.uint8), W4.astype(np.float32), F, M


def _head_w(num_bones: int, bidx: dict, rows: int) -> np.ndarray:
    W = np.zeros((rows, num_bones), dtype=np.float64)
    W[:, bidx["mixamorig:Head"]] = 1.0
    return W


# ==============================================================================
# 5. PART 1 — HEROIC LAYERED WUXIA HAIR (VOLUME CLUMPS + WINDSWEPT BANGS + CROWN)
# ==============================================================================

def _add_hero_hair_crown_and_bangs(mb: MeshBuilder, head: HeadSurfaceSampler, bidx: dict):
    """Build cohesive, heroic layered Wuxia swordsman hair (lush sculpted crown + windswept bangs + coronet).

    Architecture:
      1. Continuous, lush sculpted hair-volume crown shell (64x20) with soft off-center parting,
         subtle brushed-back silk wave flutes (no bowling-ball hotspot AND no cornrow tubes!),
         and organic temple/ear hairline.
      2. 7 dynamic asymmetrical windswept front bangs (Tóc Mái Kiếm Khách) as sleek flat silk
         ribbons emerging seamlessly from the parted crown.
      3. Backward-tilted antique-gold & celadon-jade coronet (Phát Quán) + carved jade hairpin
         (Trâm Ngọc) with ZERO hair clipping through the gold crown.
    """
    nb = mb.num_bones
    b_head = bidx["mixamorig:Head"]
    crown_anchor = np.array([0.0, 1.778, -0.044], dtype=np.float64)

    # 1. Lush Continuous Sculpted Crown Hair Shell (full heroic volume + soft silk wave ridges, zero cornrows!)
    n_phi, n_v = 64, 20
    P = np.zeros((n_v, n_phi, 3), dtype=np.float64)
    UV = np.zeros((n_v, n_phi, 2), dtype=np.float64)
    COL = np.ones((n_v, n_phi, 3), dtype=np.float64)
    W = np.zeros((n_v, n_phi, nb), dtype=np.float64)
    W[:, :, b_head] = 1.0

    for iv in range(n_v):
        v = iv / (n_v - 1)
        for ip in range(n_phi):
            phi = (ip / n_phi) * 2.0 * math.pi
            # Wrap phi to [-pi, pi] for clean front/side symmetry
            phi_s = phi - 2.0 * math.pi if phi > math.pi else phi
            c, s = math.cos(phi_s), math.sin(phi_s)
            front_w, back_w = max(0.0, c), max(0.0, -c)
            side_w = abs(s)

            # Natural masculine hairline: soft widow's peak at center, gentle temple bays, crisp ear arch
            widow_peak = -0.045 * math.exp(-((phi_s / 0.22) ** 2))
            temple_bay = 0.115 * math.exp(-(((abs(phi_s) - 0.64) / 0.26) ** 2))
            ear_arch = 0.155 * math.exp(-(((abs(phi_s) - 1.40) / 0.26) ** 2))
            elev_bot = 0.57 * (front_w ** 1.15) - 0.62 * (back_w ** 0.85) + widow_peak + temple_bay + ear_arch
            elev = elev_bot * (1.0 - v) + 1.53 * v

            d_vec = np.array([math.cos(elev) * s, math.sin(elev), math.cos(elev) * c], dtype=np.float64)

            # Generous, natural hair-volume loft (+10..16 mm over top/sides, feathering cleanly at hairline)
            env_v = math.sin(min(1.0, v * 1.18) * math.pi * 0.86)
            base_loft = 0.0025 + (0.0115 + 0.0032 * front_w + 0.0028 * back_w + 0.0018 * side_w) * env_v

            # Soft off-center parting groove at phi_part = +0.20 (natural brushed-back flow, no plastic dome)
            part_dist = abs(phi_s - 0.20)
            part_groove = -0.0026 * math.exp(-((part_dist / 0.14) ** 2)) * front_w * math.sin(v * math.pi)

            # Gentle multi-harmonic brushed-back silk wave ripples (±1.5 mm -> breaks specular without cornrows!)
            wave_env = math.sin(v * math.pi) * (1.0 - 0.35 * (v ** 2))
            silk_wave = (
                0.0011 * math.sin(9.0 * phi_s + 2.2 * v)
                + 0.0006 * math.cos(15.0 * phi_s - 3.0 * v)
                + 0.0004 * math.sin(5.0 * phi_s + 1.4 * v)
            ) * wave_env

            vol = max(0.0018, base_loft + part_groove + silk_wave)
            pt, _ = head.sample_skull_dir(d_vec, offset=vol)
            P[iv, ip] = pt
            UV[iv, ip] = (ip / n_phi * 7.0, v * 2.8)

            # Subtle brushed-strand shading: deeper at hairline/parting, silky sheen across mid-crown
            strand_mod = 0.06 * math.sin(14.0 * phi_s + 1.8 * v) + 0.04 * math.cos(23.0 * phi_s)
            part_ao = -0.10 * math.exp(-((part_dist / 0.12) ** 2)) * front_w * math.sin(v * math.pi)
            shade = np.clip(0.76 + 0.18 * math.sin(v * math.pi * 0.85) + strand_mod + part_ao, 0.58, 0.98)
            COL[iv, ip] = shade

    mb.add_solidified_grid(
        P, UV, COL, W, np.full((n_v - 1, n_phi), MAT_HAIR, dtype=np.int32),
        thickness=0.0028, wrap_u=True, flip=False, inner_shade=0.68,
    )

    # 2. 7 Dynamic Windswept Front Bangs (Tóc Mái Kiếm Khách — sleek flat silk ribbons breaking up hairline!)
    #    Emerge seamlessly from the parted crown volume (+0.015, 1.772) down across forehead & cheekbones.
    bang_specs = [
        # (x_root, x_mid, x_tip, y_tip, z_lift, hw, sway_bone_or_None)
        ( 0.014,  0.046,  0.070, 1.662, 0.014, 0.017, "cloth_hair_bang_l"),  # Hero left-swept brow bang
        ( 0.010,  0.029,  0.048, 1.686, 0.016, 0.013, None),                 # Inner left forehead wisp
        ( 0.016, -0.014, -0.048, 1.674, 0.015, 0.018, None),                 # Hero right-swept diagonal bang
        ( 0.006, -0.038, -0.070, 1.656, 0.014, 0.017, "cloth_hair_bang_r"),  # Outer right temple sweep
        ( 0.022,  0.002, -0.016, 1.700, 0.016, 0.012, None),                 # Delicate center-right forehead wisp
        ( 0.058,  0.074,  0.072, 1.562, 0.012, 0.014, "cloth_hair_bang_l"),  # Left long cheekbone framing lock
        (-0.058, -0.074, -0.072, 1.562, 0.012, 0.014, "cloth_hair_bang_r"),  # Right long cheekbone framing lock
    ]
    n_b = 11
    for xr, xm, xt, yt, z_l, hw, sw_bone in bang_specs:
        spine = np.zeros((n_b, 3), dtype=np.float64)
        out_h = np.zeros((n_b, 3), dtype=np.float64)
        rx = np.zeros(n_b, dtype=np.float64)
        rz = np.zeros(n_b, dtype=np.float64)
        col_r = np.ones((n_b, 3), dtype=np.float64)
        w_r = np.zeros((n_b, nb), dtype=np.float64)

        yr = 1.772
        for k in range(n_b):
            t = k / (n_b - 1)
            x = (1 - t) ** 2 * xr + 2 * (1 - t) * t * xm + t ** 2 * xt
            y = yr * (1.0 - t ** 0.92) + yt * (t ** 0.92)
            if y >= 1.645:
                d_v = np.array([x, y, 0.125 - 0.55 * abs(x)], dtype=np.float64) - head.center
                # Smoothly rooted inside the crown shell at t=0, lifting gracefully across the forehead
                off = 0.0085 * (1.0 - t) + z_l * math.sin(t * math.pi * 0.85) + 0.0025
                pt, nrm = head.sample_skull_dir(d_v, offset=off)
            else:
                s_x = 1.0 if x >= 0 else -1.0
                pt = np.array([x + s_x * 0.006 * (t - 0.5), y, 0.068 - 0.012 * t], dtype=np.float64)
                nrm = np.array([s_x * 0.65, 0.0, 0.76], dtype=np.float64)
            spine[k] = pt
            out_h[k] = nrm
            taper = (0.48 + 0.52 * math.sin(t * math.pi * 0.78)) * (1.0 - 0.88 * (t ** 1.65))
            rx[k] = max(0.0014, hw * taper)
            rz[k] = max(0.0010, hw * 0.30 * taper)  # Sleek flat silk ribbon (not chunky tube)
            col_r[k] = 0.80 + 0.20 * math.sin(t * math.pi * 0.8)

            if sw_bone is not None:
                w_sw = float(_smooth(0.32, 0.80, t)) * 0.78
                w_r[k, b_head] = 1.0 - w_sw
                w_r[k, bidx[sw_bone]] = w_sw
            else:
                w_r[k, b_head] = 1.0

        mb.add_tapered_tube(spine, rx, rz, col_r, w_r, MAT_HAIR, segs=8, uv_v_scale=2.0, out_hint=out_h)

    # 4. Backward-Tilted Imperial Antique-Gold & Celadon-Jade Hair Coronet (Phát Quán) + Jade Pin
    #    Tilted back 22 deg on upper-rear crown (Y=1.774, Z=-0.044) -> heroic swordsman silhouette,
    #    ZERO hair poking through the gold walls!
    tilt = math.radians(-22.0)  # tilts local +Y backward toward -Z!
    ct, st = math.cos(tilt), math.sin(tilt)
    def crown_pt(lx, ly, lz):
        return crown_anchor + np.array([lx, ly * ct - lz * st, ly * st + lz * ct], dtype=np.float64)

    # Inner hair ponytail knot STRICTLY inside coronet (r_max=0.023 < r_coronet=0.032 -> ZERO poke-through!)
    n_bv, n_bu = 10, 20
    Pb = np.zeros((n_bv, n_bu, 3), dtype=np.float64)
    UVb = np.zeros((n_bv, n_bu, 2), dtype=np.float64)
    COLb = np.ones((n_bv, n_bu, 3), dtype=np.float64)
    Wb = np.zeros((n_bv, n_bu, nb), dtype=np.float64)
    Wb[:, :, b_head] = 1.0
    for r in range(n_bv):
        v = r / (n_bv - 1)
        rad = max(0.004, 0.022 * (1.0 - 0.12 * v) * (1.0 - 0.72 * (v ** 3.5)))
        ly = -0.004 + v * 0.044
        for i in range(n_bu):
            th = (i / n_bu) * 2.0 * math.pi
            Pb[r, i] = crown_pt(rad * math.sin(th), ly, rad * math.cos(th))
            UVb[r, i] = (i / n_bu * 3.0, v * 1.5)
            COLb[r, i] = 0.78 + 0.18 * v
    mb.add_grid(Pb, UVb, COLb, Wb, np.full((n_bv - 1, n_bu), MAT_HAIR, dtype=np.int32), wrap_u=True, flip=False)

    # Antique-gold coronet with inlaid celadon-jade front medallion
    n_cv, n_cu = 7, 28
    Pc = np.zeros((n_cv, n_cu, 3), dtype=np.float64)
    UVc = np.zeros((n_cv, n_cu, 2), dtype=np.float64)
    COLc = np.ones((n_cv, n_cu, 3), dtype=np.float64)
    Wc = np.zeros((n_cv, n_cu, nb), dtype=np.float64)
    Wc[:, :, b_head] = 1.0
    for r in range(n_cv):
        v = r / (n_cv - 1)
        ly = 0.000 + v * 0.028
        flare = 0.0028 * ((2.0 * v - 1.0) ** 2)
        for i in range(n_cu):
            th = (i / n_cu) * 2.0 * math.pi
            crest = 0.009 * max(0.0, math.cos(th)) ** 2.4 * v
            rr = 0.031 + flare + 0.0012 * math.cos(4.0 * th)
            Pc[r, i] = crown_pt(rr * math.sin(th), ly + crest, rr * math.cos(th))
            UVc[r, i] = (i / n_cu * 4.0, v)
            COLc[r, i] = 0.90 + 0.10 * math.sin(v * math.pi)
    fm_c = np.full((n_cv - 1, n_cu), MAT_GOLD, dtype=np.int32)
    for i in range(n_cu):
        th = ((i + 0.5) / n_cu) * 2.0 * math.pi
        if math.cos(th) > 0.88:
            fm_c[1:-1, i] = MAT_JADE
    mb.add_solidified_grid(Pc, UVc, COLc, Wc, fm_c, thickness=0.0024, wrap_u=True, flip=False, inner_shade=0.78)

    # Carved celadon-jade hairpin (Trâm Ngọc) piercing cleanly ABOVE coronet rim
    n_p = 12
    xs = np.linspace(-0.068, 0.068, n_p)
    sp_p = np.array([crown_pt(float(xx), 0.032, 0.0) for xx in xs])
    rad_p = np.array([0.0038, 0.0046, 0.0032] + [0.0027] * (n_p - 6) + [0.0032, 0.0046, 0.0038])
    mb.add_tapered_tube(sp_p, rad_p, rad_p, np.full((n_p, 3), 0.96), _head_w(nb, bidx, n_p), MAT_JADE, segs=8)
    for sgn in (-1.0, 1.0):
        ex = np.linspace(sgn * 0.054, sgn * 0.074, 5)
        if sgn < 0:
            ex = ex[::-1]
        sp_e = np.array([crown_pt(float(xx), 0.032, 0.0) for xx in ex])
        re = np.array([0.0034, 0.0054, 0.0060, 0.0046, 0.0020])
        mb.add_tapered_tube(sp_e, re, re, np.full((5, 3), 0.98), _head_w(nb, bidx, 5), MAT_GOLD, segs=8)


def _add_ponytail_and_back_mane(mb: MeshBuilder, bidx: dict):
    """Build cascading high ponytail + layered, S-curved flowing back mane (zero stiff triangular fin!)."""
    nb = mb.num_bones
    b_head = bidx["mixamorig:Head"]

    def make_back_weights(t: float, su: float) -> np.ndarray:
        w = np.zeros(nb, dtype=np.float64)
        w_h = 1.0 - float(_smooth(0.14, 0.34, t))
        w_c = 1.0 - w_h
        w2 = float(_smooth(0.40, 0.78, t))
        w_side = float(_smooth(0.18, 0.74, abs(su)))
        w_mid = 1.0 - w_side
        is_l = 1.0 if su >= 0.0 else 0.0
        w[b_head] = w_h
        w[bidx["cloth_hair_c_01"]] = w_c * w_mid * (1.0 - w2)
        w[bidx["cloth_hair_c_02"]] = w_c * w_mid * w2
        w[bidx["cloth_hair_l_01"]] = w_c * w_side * is_l * (1.0 - w2)
        w[bidx["cloth_hair_l_02"]] = w_c * w_side * is_l * w2
        w[bidx["cloth_hair_r_01"]] = w_c * w_side * (1.0 - is_l) * (1.0 - w2)
        w[bidx["cloth_hair_r_02"]] = w_c * w_side * (1.0 - is_l) * w2
        return w

    # 1. Heroic High Ponytail Cascading UP & BACK out of the Tilted Coronet (7 wide flat silk ribbons!)
    ponytail_specs = [
        # (su, y_bot, x_spread, z_arc, hw, phase)
        ( 0.00, 1.285,  0.000, -0.024, 0.030, 0.0),
        ( 0.30, 1.310,  0.038, -0.018, 0.026, 0.8),
        (-0.30, 1.310, -0.038, -0.018, 0.026, 1.6),
        ( 0.60, 1.350,  0.068, -0.010, 0.023, 2.4),
        (-0.60, 1.350, -0.068, -0.010, 0.023, 3.2),
        ( 0.16, 1.335,  0.020, -0.030, 0.022, 1.1),
        (-0.16, 1.335, -0.020, -0.030, 0.022, 2.1),
    ]
    n_pt = 15
    for su, y_bot, x_sp, z_arc, hw, ph in ponytail_specs:
        spine = np.zeros((n_pt, 3), dtype=np.float64)
        out_h = np.zeros((n_pt, 3), dtype=np.float64)
        rx = np.zeros(n_pt, dtype=np.float64)
        rz = np.zeros(n_pt, dtype=np.float64)
        col_r = np.ones((n_pt, 3), dtype=np.float64)
        w_r = np.zeros((n_pt, nb), dtype=np.float64)

        for k in range(n_pt):
            t = k / (n_pt - 1)
            y = 1.802 * (1.0 - t ** 1.15) + y_bot * (t ** 1.15) + 0.034 * math.sin(min(1.0, t * 2.2) * math.pi) * (1.0 - t)
            x = su * 0.012 * (1.0 - t) + x_sp * (t ** 0.72) + 0.007 * math.sin(4.2 * t + ph) * t
            z = -0.054 + z_arc * math.sin(t * math.pi * 0.85) - 0.118 * (t ** 0.62) + 0.006 * math.cos(3.6 * t + ph) * t
            spine[k] = [x, y, z]
            out_h[k] = [0.3 * su, 0.25 * (1.0 - t), -0.95]
            taper = (0.48 + 0.52 * math.sin(t * math.pi * 0.75)) * (1.0 - 0.88 * (t ** 1.8))
            rx[k] = max(0.0020, hw * taper)
            rz[k] = max(0.0014, hw * 0.28 * taper)  # Wide sleek silk ribbon
            col_r[k] = 0.80 + 0.20 * math.sin(t * math.pi * 0.8)
            w_r[k] = make_back_weights(t, su)

        mb.add_tapered_tube(spine, rx, rz, col_r, w_r, MAT_HAIR, segs=8, uv_v_scale=2.6, out_hint=out_h)

    # 2. Continuous Cohesive S-Curved Back-Mane Under-Sheet (eliminates gaps/tubular separation in back/side!)
    n_mr, n_mc = 15, 19
    Pm = np.zeros((n_mr, n_mc, 3), dtype=np.float64)
    UVm = np.zeros((n_mr, n_mc, 2), dtype=np.float64)
    COLm = np.ones((n_mr, n_mc, 3), dtype=np.float64)
    Wm = np.zeros((n_mr, n_mc, nb), dtype=np.float64)
    for ir in range(n_mr):
        t = ir / (n_mr - 1)  # 0 = nape -> 1 = mid-back scalloped hem
        for ic in range(n_mc):
            u = ic / (n_mc - 1)
            su = 2.0 * u - 1.0  # -1 = right, +1 = left
            # Layered V-scalloped hem (longer at center, gracefully shorter behind shoulders)
            y_top = 1.672 - 0.016 * (su ** 2)
            y_bot = 1.335 + 0.085 * (su ** 2) + 0.014 * math.sin(5.0 * math.pi * u)
            y = y_top * (1.0 - t) + y_bot * t
            half_w = 0.068 + 0.044 * math.sin(t * math.pi * 0.78) - 0.018 * (t ** 2)
            x = su * half_w
            # Gentle organic S-curve draping over back with soft vertical flutes (±2.2 mm)
            flute = 0.0022 * math.sin(7.0 * math.pi * u + 1.2 * t) * math.sin(t * math.pi)
            z = -0.069 - 0.084 * (t ** 0.74) + 0.032 * (su ** 2) * (1.0 - 0.45 * t) - flute
            Pm[ir, ic] = [x, y, z]
            UVm[ir, ic] = (u * 3.5, t * 2.8)
            COLm[ir, ic] = 0.70 + 0.18 * math.sin(t * math.pi * 0.85) + 0.04 * math.sin(7.0 * math.pi * u)
            Wm[ir, ic] = make_back_weights(t, su)
    mb.add_solidified_grid(
        Pm, UVm, COLm, Wm, np.full((n_mr - 1, n_mc - 1), MAT_HAIR, dtype=np.int32),
        thickness=0.0040, wrap_u=False, flip=True, inner_shade=0.65,
    )

    # 3. 5 Sleek Outer-Edge Framing Accent Locks (soft layered silhouette over the continuous back sheet)
    mane_specs = [
        ( 0.00, 1.665, 1.315, -0.005, 0.026, 0.2),
        ( 0.54, 1.660, 1.360,  0.000, 0.022, 1.8),
        (-0.54, 1.660, 1.360,  0.000, 0.022, 2.6),
        ( 0.90, 1.652, 1.425,  0.014, 0.018, 0.8),
        (-0.90, 1.652, 1.425,  0.014, 0.018, 2.0),
    ]
    n_m = 13
    for su, y_top, y_bot, z_ex, hw, ph in mane_specs:
        spine = np.zeros((n_m, 3), dtype=np.float64)
        out_h = np.zeros((n_m, 3), dtype=np.float64)
        rx = np.zeros(n_m, dtype=np.float64)
        rz = np.zeros(n_m, dtype=np.float64)
        col_r = np.ones((n_m, 3), dtype=np.float64)
        w_r = np.zeros((n_m, nb), dtype=np.float64)

        for k in range(n_m):
            t = k / (n_m - 1)
            y = y_top * (1.0 - t) + y_bot * t
            x = su * (0.066 + 0.044 * math.sin(t * math.pi * 0.80)) + 0.005 * math.sin(3.6 * t + ph) * t
            z = -0.071 + z_ex - 0.084 * (t ** 0.74) + 0.030 * (su ** 2) * (1.0 - 0.45 * t) + 0.005 * math.cos(3.2 * t + ph) * t
            spine[k] = [x, y, z]
            out_h[k] = [0.45 * su, 0.1, -0.88]
            taper = (0.65 + 0.35 * math.sin(t * math.pi * 0.8)) * (1.0 - 0.88 * (t ** 1.75))
            rx[k] = max(0.0018, hw * taper)
            rz[k] = max(0.0012, hw * 0.28 * taper)
            col_r[k] = 0.76 + 0.20 * math.sin(t * math.pi * 0.8)
            w_r[k] = make_back_weights(t, su)

        mb.add_tapered_tube(spine, rx, rz, col_r, w_r, MAT_HAIR, segs=8, uv_v_scale=2.4, out_hint=out_h)


# ==============================================================================
# 6. PART 2 — EYEBROWS (COHESIVE HEROIC KIẾM MI SWORD-BROWS)
# ==============================================================================

def _add_eyebrows(mb: MeshBuilder, head: HeadSurfaceSampler, bidx: dict):
    """Build cohesive, bold, natural dark-espresso sword-brows aligned directly over the brow ridge."""
    nb = mb.num_bones
    brow_tint = np.array([0.74, 0.66, 0.60], dtype=np.float64)  # Natural dark espresso-charcoal (not sharpie black!)

    for s in (1.0, -1.0):
        # 2 tightly overlapped sculpted brow ribbons sitting directly ON the brow crest (Y=1.698..1.705)
        for dy_off, scale_w, z_ex in [
            ( 0.0000, 1.00, 0.0036),  # Primary cohesive sword-brow body
            ( 0.0014, 0.72, 0.0034),  # Upper arch crest feather
        ]:
            n_pts = 12
            spine = np.zeros((n_pts, 3), dtype=np.float64)
            out_h = np.zeros((n_pts, 3), dtype=np.float64)
            rx = np.zeros(n_pts, dtype=np.float64)
            rz = np.zeros(n_pts, dtype=np.float64)
            col_r = np.zeros((n_pts, 3), dtype=np.float64)
            for k in range(n_pts):
                t = k / (n_pts - 1)  # 0 = inner glabella -> 1 = sharp sword-tip outer tail
                x = s * (0.0115 + t * 0.0395)
                # Confident rising Kiếm Mi arch sitting directly over human_body.glb's brow crest!
                arch = 0.0054 * (t ** 0.80) - 0.0032 * max(0.0, (t - 0.68) / 0.32) ** 1.6
                y = 1.6978 + arch + dy_off * (1.0 - 0.65 * t)
                pt, nrm = head.sample_face_xy(x, y, z_offset=z_ex, radius=0.0055)
                spine[k] = pt
                out_h[k] = nrm

                prof = math.sin(min(1.0, t * 1.25) * math.pi * 0.5) * (1.0 - 0.85 * (t ** 1.9))
                rx[k] = max(0.0008, 0.0044 * scale_w * (0.48 + 0.52 * prof))
                rz[k] = max(0.0006, 0.0018 * scale_w * (0.48 + 0.52 * prof))
                col_r[k] = brow_tint * (0.88 + 0.12 * (1.0 - math.sin(t * math.pi)))

            mb.add_tapered_tube(spine, rx, rz, col_r, _head_w(nb, bidx, n_pts), MAT_HAIR, segs=8, uv_v_scale=1.2, out_hint=out_h)


# ==============================================================================
# 7. PART 3 — EYELASHES (CRISP MASCULINE UPPER & LOWER FRINGES)
# ==============================================================================

def _add_eyelashes(mb: MeshBuilder, head: HeadSurfaceSampler, bidx: dict):
    """Build natural masculine upper lash line + outward-curling tufts + subtle lower lash rim."""
    nb = mb.num_bones
    for s in (1.0, -1.0):
        n_u = 11
        u_spine = np.zeros((n_u, 3), dtype=np.float64)
        u_rx = np.zeros(n_u, dtype=np.float64)
        u_rz = np.zeros(n_u, dtype=np.float64)
        for k in range(n_u):
            t = k / (n_u - 1)
            x = s * (0.0185 + t * 0.0255)
            y = 1.6834 + 0.0040 * math.sin(t * math.pi) + 0.0012 * t
            pt, _ = head.sample_face_xy(x, y, z_offset=0.0018, radius=0.004)
            u_spine[k] = pt
            thk = 0.00075 + 0.00060 * math.sin(t * math.pi)
            u_rx[k] = thk
            u_rz[k] = thk * 0.85
        mb.add_tapered_tube(u_spine, u_rx, u_rz, np.full((n_u, 3), 0.48), _head_w(nb, bidx, n_u), MAT_HAIR, segs=8)

        for j in range(10):
            t = 0.10 + 0.84 * (j / 9.0)
            x = s * (0.0185 + t * 0.0255)
            y = 1.6834 + 0.0040 * math.sin(t * math.pi) + 0.0012 * t
            p0, _ = head.sample_face_xy(x, y, z_offset=0.0017, radius=0.004)
            length = 0.0028 + 0.0015 * (t ** 1.1)
            dir_out = np.array([s * (0.18 + 0.38 * t), 0.28 + 0.22 * t, 0.90], dtype=np.float64)
            dir_out /= np.linalg.norm(dir_out)
            sp = np.stack([p0, p0 + dir_out * (length * 0.55), p0 + dir_out * length + np.array([0.0, 0.0007, 0.0])], axis=0)
            r_l = np.array([0.00060, 0.00040, 0.00014])
            mb.add_tapered_tube(sp, r_l, r_l, np.full((3, 3), 0.44), _head_w(nb, bidx, 3), MAT_HAIR, segs=8)

        n_lo = 9
        l_spine = np.zeros((n_lo, 3), dtype=np.float64)
        l_r = np.zeros(n_lo, dtype=np.float64)
        for k in range(n_lo):
            t = k / (n_lo - 1)
            x = s * (0.0200 + t * 0.0225)
            y = 1.6774 - 0.0020 * math.sin(t * math.pi) + 0.0010 * t
            pt, _ = head.sample_face_xy(x, y, z_offset=0.0013, radius=0.004)
            l_spine[k] = pt
            l_r[k] = 0.00040 + 0.00025 * math.sin(t * math.pi)
        mb.add_tapered_tube(l_spine, l_r, l_r, np.full((n_lo, 3), 0.56), _head_w(nb, bidx, n_lo), MAT_HAIR, segs=8)


# ==============================================================================
# 8. PART 4 — REFINED, HANDSOME MARTIAL-ARTS BEARD & MUSTACHE (SUBTLE & SLEEK)
# ==============================================================================

def _add_beard_and_mustache(mb: MeshBuilder, head: HeadSurfaceSampler, bidx: dict):
    """Build sleek, understated, ruggedly handsome Wuxia facial hair (no cartoon Fu Manchu!):
      - Close-cropped natural dark-espresso mustache hugging the upper lip cleanly
      - Sleek, close-contoured chin & lower-jaw trim (soft natural silhouette, no isolated dot!)
      - Natural tapered temple sideburns blending seamlessly from the hair clumps down past ears
    """
    nb = mb.num_bones
    b_head = bidx["mixamorig:Head"]
    beard_tint = np.array([0.82, 0.72, 0.66], dtype=np.float64)  # Natural dark charcoal-brown

    # 1. Sleek Close-Cropped Mustache (follows upper lip contour tightly, tapers cleanly at corners)
    for s in (1.0, -1.0):
        for dy_m, w_sc, z_ex, x_end in [
            ( 0.0000, 1.00, 0.0014, 0.031),
            ( 0.0016, 0.68, 0.0012, 0.027),
        ]:
            n_m = 11
            spine = np.zeros((n_m, 3), dtype=np.float64)
            out_h = np.zeros((n_m, 3), dtype=np.float64)
            rx = np.zeros(n_m, dtype=np.float64)
            rz = np.zeros(n_m, dtype=np.float64)
            col_r = np.zeros((n_m, 3), dtype=np.float64)
            for k in range(n_m):
                t = k / (n_m - 1)
                x = s * (0.0020 + t * (x_end - 0.0020))
                # Gentle natural arch closely hugging the upper lip (NO long drooping whiskers!)
                y = 1.6215 + 0.0012 * math.sin(t * math.pi * 0.65) - 0.0115 * (t ** 1.9) + dy_m * (1.0 - 0.55 * t)
                pt, nrm = head.sample_face_xy(x, y, z_offset=z_ex, radius=0.0055)
                spine[k] = pt
                out_h[k] = nrm
                prof = (0.45 + 0.55 * math.sin(t * math.pi * 0.85)) * (1.0 - 0.86 * (t ** 2.0))
                rx[k] = max(0.0007, 0.0030 * w_sc * prof)
                rz[k] = max(0.0005, 0.0012 * w_sc * prof)
                col_r[k] = beard_tint * (0.82 + 0.14 * (1.0 - t))
            mb.add_tapered_tube(spine, rx, rz, col_r, _head_w(nb, bidx, n_m), MAT_HAIR, segs=8, uv_v_scale=1.5, out_hint=out_h)

    # 2. Sleek Close-Cropped Contoured Chin Beard (subtle, refined chin framing — zero chunky cup!)
    n_r, n_c = 9, 15
    Pb = np.zeros((n_r, n_c, 3), dtype=np.float64)
    UVb = np.zeros((n_r, n_c, 2), dtype=np.float64)
    COLb = np.ones((n_r, n_c, 3), dtype=np.float64)
    Wb = np.zeros((n_r, n_c, nb), dtype=np.float64)
    Wb[:, :, b_head] = 1.0

    for i in range(n_c):
        u = i / (n_c - 1)
        su = 2.0 * u - 1.0
        env = max(0.0, 1.0 - su * su)
        # Sleek contoured chin-line rising softly at center toward lower lip depression
        y_top = 1.5775 + 0.0055 * (env ** 1.6)
        y_bot = 1.5640 - 0.0075 * (env ** 1.2)

        for r in range(n_r):
            v = r / (n_r - 1)
            t_down = 1.0 - v
            y = y_top * v + y_bot * t_down
            x = su * 0.032 * (1.0 - 0.22 * (t_down ** 1.3))
            flute = 0.0004 * math.sin(6.0 * math.pi * u) * env
            edge_taper = math.sin(u * math.pi) ** 0.65
            relief = (0.0008 + 0.0016 * math.sin(v * math.pi * 0.85)) * edge_taper + flute

            z_base = 0.1280 - 14.5 * (x * x) - 28.0 * max(0.0, 1.578 - y) ** 1.60 - 12.0 * max(0.0, y - 1.578) ** 2
            Pb[r, i] = [x, y, z_base + relief]
            UVb[r, i] = (u * 2.2, t_down * 1.4)
            COLb[r, i] = np.clip(beard_tint * (0.78 + 0.14 * v), 0.55, 0.96)

    mb.add_solidified_grid(
        Pb, UVb, COLb, Wb, np.full((n_r - 1, n_c - 1), MAT_HAIR, dtype=np.int32),
        thickness=0.0014, flip=False, inner_shade=0.72,
    )

    # 3. Natural Tapered Temple Sideburns blending seamlessly from side hair down past ears
    for s in (1.0, -1.0):
        n_sb = 7
        spine = np.zeros((n_sb, 3), dtype=np.float64)
        rx = np.zeros(n_sb, dtype=np.float64)
        rz = np.zeros(n_sb, dtype=np.float64)
        for k in range(n_sb):
            t = k / (n_sb - 1)
            y = 1.714 - t * 0.068
            x = s * (0.072 - 0.006 * t)
            z = 0.052 - 0.006 * t
            d_vec = np.array([x, y, z], dtype=np.float64) - head.center
            pt, _ = head.sample_skull_dir(d_vec, offset=0.0020)
            spine[k] = pt
            taper = 1.0 - 0.78 * (t ** 1.6)
            rx[k] = max(0.0010, 0.0065 * taper)
            rz[k] = max(0.0008, 0.0020 * taper)
        mb.add_tapered_tube(spine, rx, rz, np.tile(beard_tint * 0.85, (n_sb, 1)), _head_w(nb, bidx, n_sb), MAT_HAIR, segs=8)


# ==============================================================================
# 9. PROCEDURAL PNG TEXTURES (SILK HAIR STRANDS, ANTIQUE GOLD, CELADON JADE)
# ==============================================================================

def _encode_png_rgb(rgb_u8: np.ndarray) -> bytes:
    h, w, _ = rgb_u8.shape
    raw = b"".join(b"\x00" + rgb_u8[y].tobytes() for y in range(h))
    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I4s", len(data), tag) + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 6))
        + chunk(b"IEND", b"")
    )


def _make_hair_strand_png(size: int = 256) -> bytes:
    """Deep ink-charcoal (Huyền Mặc) fine silk hair-strand texture with warm-neutral sheen."""
    u = np.linspace(0.0, 2.0 * math.pi, size, endpoint=False)
    U, V = np.meshgrid(u, u)
    s1 = 0.5 + 0.5 * np.sin(U * 42.0 + 0.35 * np.sin(V * 3.0))
    s2 = 0.5 + 0.5 * np.sin(U * 88.0 - 0.25 * np.cos(V * 5.0))
    s3 = 0.5 + 0.5 * np.cos(U * 21.0 + 0.40 * np.sin(V * 2.0))
    strand = 0.45 * s1 + 0.35 * s2 + 0.20 * s3
    dark = np.array([0.042, 0.040, 0.046], dtype=np.float64)[None, None, :]
    sheen = np.array([0.125, 0.122, 0.138], dtype=np.float64)[None, None, :]
    rgb = dark * (1.0 - strand[:, :, None]) + sheen * strand[:, :, None]
    return _encode_png_rgb(np.clip(rgb * 255.0, 0, 255).astype(np.uint8))


def _make_gold_png(size: int = 128) -> bytes:
    y_i, x_i = np.mgrid[0:size, 0:size]
    cell = size // 8
    cx = (x_i % cell) / float(cell)
    cy = (y_i % cell) / float(cell)
    ring1 = ((cx < 0.14) | (cx > 0.86) | (cy < 0.14) | (cy > 0.86))
    ring2 = ((cx > 0.28) & (cx < 0.72) & (cy > 0.28) & (cy < 0.72) &
             ((cx < 0.40) | (cx > 0.60) | (cy < 0.40) | (cy > 0.60)))
    fret = (ring1 | ring2).astype(np.float64)
    dark = np.array([0.38, 0.29, 0.14], dtype=np.float64)[None, None, :]
    hi = np.array([0.74, 0.59, 0.31], dtype=np.float64)[None, None, :]
    rgb = dark * (1.0 - fret[:, :, None]) + hi * fret[:, :, None]
    return _encode_png_rgb(np.clip(rgb * 255.0, 0, 255).astype(np.uint8))


def _make_jade_png(size: int = 64) -> bytes:
    u = np.linspace(0.0, 2.0 * math.pi, size, endpoint=False)
    U, V = np.meshgrid(u, u)
    vein = 0.5 + 0.5 * np.sin(3.0 * U + 2.0 * V + 0.6 * np.sin(4.0 * U))
    base = np.array([0.24, 0.54, 0.48], dtype=np.float64)[None, None, :]
    hi = np.array([0.42, 0.74, 0.66], dtype=np.float64)[None, None, :]
    rgb = base * (1.0 - 0.55 * vein[:, :, None]) + hi * (0.55 * vein[:, :, None])
    return _encode_png_rgb(np.clip(rgb * 255.0, 0, 255).astype(np.uint8))


# ==============================================================================
# 10. BINARY GLB 2.0 EXPORTER & BUILT-IN SELF-VERIFICATION
# ==============================================================================

def export_glb(out_path: Path, bones: list, V, N, UV, COL, J4, W4, F, M):
    data = bytearray()
    buffer_views = []
    accessors = []

    def add_bytes(raw: bytes, target=None) -> int:
        data.extend(b"\0" * ((-len(data)) % 4))
        off = len(data)
        data.extend(raw)
        bv = {"buffer": 0, "byteOffset": off, "byteLength": len(raw)}
        if target is not None:
            bv["target"] = target
        buffer_views.append(bv)
        return len(buffer_views) - 1

    def add_acc(arr: np.ndarray, typ: str, ct: int, target=None, bounds=False, normalized=False) -> int:
        dt = {5121: np.uint8, 5123: "<u2", 5125: "<u4", 5126: "<f4"}[ct]
        a = np.ascontiguousarray(arr, dtype=dt)
        bv_idx = add_bytes(a.tobytes(), target=target)
        acc = {"bufferView": bv_idx, "componentType": ct, "count": len(a), "type": typ}
        if bounds:
            acc["min"] = a.min(axis=0).tolist()
            acc["max"] = a.max(axis=0).tolist()
        if normalized:
            acc["normalized"] = True
        accessors.append(acc)
        return len(accessors) - 1

    img_hair = add_bytes(_make_hair_strand_png(256))
    img_gold = add_bytes(_make_gold_png(128))
    img_jade = add_bytes(_make_jade_png(64))
    images = [
        {"bufferView": img_hair, "mimeType": "image/png", "name": "Wuxia_Ink_Hair"},
        {"bufferView": img_gold, "mimeType": "image/png", "name": "Antique_Gold_Crown"},
        {"bufferView": img_jade, "mimeType": "image/png", "name": "Celadon_Jade_Pin"},
    ]
    samplers = [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}]
    textures = [{"sampler": 0, "source": i} for i in range(3)]
    materials = [
        {
            "name": "MAT_Wuxia_Ink_Hair",
            "doubleSided": False,
            "pbrMetallicRoughness": {
                "baseColorTexture": {"index": 0},
                "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
                "metallicFactor": 0.0,
                "roughnessFactor": 0.54,
            },
        },
        {
            "name": "MAT_Gold_Hair_Crown",
            "doubleSided": False,
            "pbrMetallicRoughness": {
                "baseColorTexture": {"index": 1},
                "baseColorFactor": [1.0, 0.96, 0.86, 1.0],
                "metallicFactor": 0.65,
                "roughnessFactor": 0.28,
            },
        },
        {
            "name": "MAT_Jade_Hairpin",
            "doubleSided": False,
            "pbrMetallicRoughness": {
                "baseColorTexture": {"index": 2},
                "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
                "metallicFactor": 0.05,
                "roughnessFactor": 0.24,
            },
        },
    ]

    nb = len(bones)
    M_world = np.array([b["world"] for b in bones], dtype=np.float64)
    ibm = np.linalg.inv(M_world).transpose(0, 2, 1).reshape(nb, 16).astype("<f4")
    ibm_acc = add_acc(ibm, "MAT4", 5126)

    children_map = {i: [] for i in range(nb)}
    for i, b in enumerate(bones):
        if b["parent"] >= 0:
            children_map[b["parent"]].append(i)
    nodes = []
    for i, b in enumerate(bones):
        nd = {"name": b["name"], "translation": [float(v) for v in b["tr"]]}
        if any(abs(a - c) > 1e-7 for a, c in zip(b["rot"], [0.0, 0.0, 0.0, 1.0])):
            nd["rotation"] = [float(v) for v in b["rot"]]
        if children_map[i]:
            nd["children"] = children_map[i]
        nodes.append(nd)
    nodes.append({"name": "Wuxia_Grooming_Mesh", "mesh": 0, "skin": 0})
    nodes.append({"name": "GroomingArmature", "children": [nb, 0]})

    prims = []
    total_v = 0
    total_t = 0
    col_rgba = np.column_stack([
        np.round(np.clip(COL, 0.0, 1.0) * 255.0).astype(np.uint8),
        np.full((len(COL), 1), 255, dtype=np.uint8),
    ])

    for mi in range(len(materials)):
        faces = F[M == mi]
        if len(faces) == 0:
            continue
        ids, inv = np.unique(faces.ravel(), return_inverse=True)
        idx_ct = 5123 if len(ids) < 65536 else 5125
        attrs = {
            "POSITION":   add_acc(V[ids], "VEC3", 5126, 34962, bounds=True),
            "NORMAL":     add_acc(N[ids], "VEC3", 5126, 34962),
            "TEXCOORD_0": add_acc(UV[ids], "VEC2", 5126, 34962),
            "COLOR_0":    add_acc(col_rgba[ids], "VEC4", 5121, 34962, normalized=True),
            "JOINTS_0":   add_acc(J4[ids], "VEC4", 5121, 34962),
            "WEIGHTS_0":  add_acc(W4[ids], "VEC4", 5126, 34962),
        }
        prims.append({
            "attributes": attrs,
            "indices": add_acc(inv, "SCALAR", idx_ct, 34963),
            "material": mi,
            "mode": 4,
        })
        total_v += len(ids)
        total_t += len(faces)

    doc = {
        "asset": {
            "version": "2.0",
            "generator": "Standalone Wuxia Hero Grooming Pipeline (Layered Hair + Brows + Lashes + Beard)",
        },
        "scene": 0,
        "scenes": [{"name": "Scene", "nodes": [nb + 1]}],
        "nodes": nodes,
        "skins": [{"name": "GroomingArmature", "joints": list(range(nb)), "inverseBindMatrices": ibm_acc}],
        "materials": materials,
        "textures": textures,
        "images": images,
        "samplers": samplers,
        "meshes": [{"name": "WuxiaGroomingMesh", "primitives": prims}],
        "accessors": accessors,
        "bufferViews": buffer_views,
        "buffers": [{"byteLength": len(data)}],
    }

    js = json.dumps(doc, separators=(",", ":")).encode("utf-8")
    js += b" " * ((-len(js)) % 4)
    data.extend(b"\0" * ((-len(data)) % 4))
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(
        struct.pack("<4sII", b"glTF", 2, 28 + len(js) + len(data))
        + struct.pack("<I4s", len(js), b"JSON") + js
        + struct.pack("<I4s", len(data), b"BIN\0") + data
    )

    assert all(b["parent"] < i for i, b in enumerate(bones)), "Bones not topologically sorted!"
    rebuilt_world = np.zeros((nb, 4, 4), dtype=np.float64)
    for i, b in enumerate(bones):
        loc = np.eye(4, dtype=np.float64)
        loc[:3, :3] = _quat_to_mat(b["rot"])
        loc[:3, 3] = b["tr"]
        rebuilt_world[i] = rebuilt_world[b["parent"]] @ loc if b["parent"] >= 0 else loc
    ibm_m = ibm.reshape(nb, 4, 4).transpose(0, 2, 1)
    max_ibm_err = max(float(np.max(np.abs(ibm_m[i] @ rebuilt_world[i] - np.eye(4)))) for i in range(nb))
    assert max_ibm_err < 1e-5, f"IBM mismatch: {max_ibm_err}"

    f0, f1, f2 = F[:, 0], F[:, 1], F[:, 2]
    fn = np.cross(V[f1] - V[f0], V[f2] - V[f0])
    fn /= np.maximum(np.linalg.norm(fn, axis=1, keepdims=True), 1e-12)
    vn = N[f0] + N[f1] + N[f2]
    vn /= np.maximum(np.linalg.norm(vn, axis=1, keepdims=True), 1e-12)
    dot_min = float((fn * vn).sum(axis=1).min())
    assert dot_min > 0.0, f"Inverted normal detected: dot_min={dot_min}"

    print("==================================================================")
    print(f"Exported standalone Wuxia Grooming GLB -> {out_path}")
    print(f"  Bones     : {nb} (20 Mixamo T-Pose + {nb - 20} cloth_hair_* spring bones)")
    print(f"  Geometry  : {total_v} verts | {total_t} tris | {len(prims)} material prims")
    print(f"  Bounds    : {np.round(V.min(0), 4).tolist()} .. {np.round(V.max(0), 4).tolist()}")
    print(f"  Quality   : IBM_err={max_ibm_err:.2e} | dot(fn,vn)_min={dot_min:.3f} | size={out_path.stat().st_size:,} B")
    print("==================================================================")


def build_wuxia_grooming(out_path: Path, body_glb: Path, include_beard: bool = True):
    head = HeadSurfaceSampler(body_glb)
    bones, bidx = build_skeleton()
    mb = MeshBuilder(num_bones=len(bones))

    _add_hero_hair_crown_and_bangs(mb, head, bidx)
    _add_ponytail_and_back_mane(mb, bidx)
    _add_eyebrows(mb, head, bidx)
    _add_eyelashes(mb, head, bidx)
    if include_beard:
        _add_beard_and_mustache(mb, head, bidx)

    V, N, UV, COL, J4, W4, F, M = mb.finalize()
    export_glb(out_path, bones, V, N, UV, COL, J4, W4, F, M)


def main():
    here = Path(__file__).resolve().parent
    repo_root = here.parent
    parser = argparse.ArgumentParser(
        description="Standalone generator for unified Wuxia Hero Grooming GLB (Hair + Eyebrows + Eyelashes + Beard)."
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=here / "toc_co_trang_kiem_hiep.glb",
        help="Output GLB file path (default: tools/toc_co_trang_kiem_hiep.glb)",
    )
    parser.add_argument(
        "--body",
        type=Path,
        default=repo_root / "assets/characters/bodies/human_body.glb",
        help="Reference head/body GLB for conformal face & skull snapping",
    )
    parser.add_argument(
        "--no-beard",
        action="store_true",
        help="Generate clean-shaven variant (hair + eyebrows + eyelashes only)",
    )
    parser.add_argument(
        "--install",
        action="store_true",
        help="Also install to assets/characters/equipment/hair/toc_co_trang_kiem_hiep.glb",
    )
    args = parser.parse_args()

    out_path = args.out.resolve()
    build_wuxia_grooming(out_path, args.body.resolve(), include_beard=(not args.no_beard))
    if args.install:
        dst = repo_root / "assets/characters/equipment/hair/toc_co_trang_kiem_hiep.glb"
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(out_path.read_bytes())
        print(f"Installed game-ready grooming set -> {dst}")


if __name__ == "__main__":
    main()
