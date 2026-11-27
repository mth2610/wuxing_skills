#!/usr/bin/env python3
"""
blender_giao_linh_mixamo_engine.py — Standalone Engine-Ready Áo Giao Lĩnh Generator

Pure Python + NumPy (NO Blender, NO external mesh/image files required).
Generates a game-ready binary glTF 2.0 (.glb) Traditional Cross-Collar Wuxia Robe
(Áo Giao Lĩnh Lam Y — Hữu Nhậm) tailored directly to the 1.79 m Mixamo T-pose
human_body.glb envelope and 20-bone animation bank (humanoid_anims.glb) plus 20
engine-native secondary cloth_* spring bones (40 bones total).

RUN:
    python3 tools/blender_giao_linh_mixamo_engine.py
    python3 tools/blender_giao_linh_mixamo_engine.py --install
    python3 tools/blender_giao_linh_mixamo_engine.py --out path/to/ao_giao_linh.glb

DEPS:
    numpy (stdlib zlib/struct/json handle PNG texture encoding & GLB 2.0 packing)

Engine invariants guaranteed:
- Coordinate system: glTF / engine native Y-up (+Y), Front (+Z), Wearer's Left (+X), meters.
- Exact 20-bone GAME_MIXAMO_20 T-pose bind hierarchy & quaternions (matching humanoid_anims.glb).
- 20 secondary cloth_* bones (sleeves, front/back skirt panels, waist ribbons) strictly
  topologically sorted (parent index < child index) with downward rest directions for
  Raylib BuildPoseFromParentJoints() and SpringBone_RegisterModelCloth().
- One continuous 3D sculpted Giao Lĩnh Hữu Nhậm collar band (nape -> lapels -> waist tie)
  exposing the neck cleanly with zero shoulder spillover or notched seams.
- Tailored athletic martial-arts V-taper + sleek draped bell sleeves (Trung Quảng Tụ)
  verified clear of the waist sash & clasp under the real 74-deg Idle arm drop.
- True 2-sided solidified cloth shells (2.0-3.2 mm thickness, crisp split-normal rims,
  outward normals everywhere, doubleSided=False) — zero backface-culling holes.
- Calibrated specifically for core/shaders/surface_lit.fs (deep ink-indigo silk damask,
  shaded moon-ivory trim, antique burnished gold Hồi Văn brocade, baked fold AO & ink ombré).
"""

import argparse
import json
import math
import struct
import zlib
from pathlib import Path
import numpy as np


# ==============================================================================
# 1. EXACT GAME_MIXAMO_20 T-POSE SKELETON & TAILORED PROPORTIONS
# ==============================================================================

# Matches assets/characters/bodies/human_body.glb & humanoid_anims.glb exactly:
# (name, parent_idx, local_translation_xyz, local_rotation_xyzw)
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

# Material indices (merged cleanly into 4 engine primitives)
MAT_TEAL  = 0   # Deep ink-indigo (Mặc Lam) fine cloud-damask silk (also tinted dark navy for sash)
MAT_IVORY = 1   # Shaded moon-ivory (Nguyệt Bạch) silk collar facing, cuff border, ribbons & jade inlay
MAT_GOLD  = 2   # Antique burnished imperial gold Hồi Văn brocade trim & belt clasp frame
MAT_INNER = 3   # Luminous warm pearl-cream Trung Y inner under-collar silk

# Tailored vertical landmarks calibrated directly to 1.79 m human_body.glb (Y-up, +Z front):
#   Body chin starts at Y=1.56; neck base Y=1.50..1.54 (r~0.062); trap slope 1.535->1.498;
#   pectoral peak Y=1.35 (z=+0.150); waist Y=1.07 (rx=0.150, z=[-0.090,+0.123]); ankles Y=0.10..0.22.
PROPORTIONS = {
    "hem_y":         0.220,   # Heroic mid-calf / upper-ankle hem (shows boots/ankles cleanly)
    "waist_y":       1.075,   # Cinched athletic waist sash center
    "chest_y":       1.350,   # Pectoral / scapula peak
    "shoulder_y":    1.518,   # Upper trapezius / acromion line (clears body y_max=1.515..1.525)
    "neck_y":        1.544,   # Closed bodice neckline base (hugs neck tightly, zero bucket gap!)
    "collar_top_y":  1.560,   # Outer Giao Lĩnh standing nape collar apex (regal vertical stand)
    "inner_neck_y":  1.566,   # Inner Trung Y cream collar apex (+6 mm at nape, lower at front throat)
}


# ==============================================================================
# 2. LINEAR ALGEBRA & QUATERNION UTILITIES
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
    """Right-handed 3x3 rotation whose local +Y points along y_dir."""
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


# ==============================================================================
# 3. ANATOMICAL HEROIC V-TAPER TORSO & A-LINE GARMENT ENVELOPE
# ==============================================================================

def get_garment_profile(y: float):
    """Return (rx, rz_front, rz_back, z_mid) tailored closely to 1.79 m human_body.glb.

    Calibrated directly to measured polar body radii (0/35/70/115/145/180 deg):
      - Cinched athletic waist (Y=1.075): rx=0.162, rz_f=0.135, rz_b=0.118.
      - Anatomical glute clearance (Y=0.94..1.00): back swell clears body 145-deg glutes (0.163) cleanly.
      - Structured chest/scapula (Y=1.35..1.518): preserves full diagonal clearance over outer pecs
        and upper scapulae while keeping centerline front/throat trim and un-bloated.
    """
    hem_y = PROPORTIONS["hem_y"]
    waist_y = PROPORTIONS["waist_y"]
    chest_y = PROPORTIONS["chest_y"]
    shoulder_y = PROPORTIONS["shoulder_y"]
    neck_y = PROPORTIONS["neck_y"]

    if y <= waist_y:
        s = max(0.0, min(1.0, (y - hem_y) / (waist_y - hem_y)))
        t_down = 1.0 - s
        hip_swell_x = 0.024 * math.exp(-0.5 * ((t_down - 0.18) / 0.12) ** 2)
        hip_swell_f = 0.012 * math.exp(-0.5 * ((t_down - 0.16) / 0.11) ** 2)
        # Anatomical glute drape (body glutes peak at Y=0.94..1.00 -> t_down=0.09..0.16, r_145=0.163)
        hip_swell_b = 0.046 * math.exp(-0.5 * ((t_down - 0.15) / 0.12) ** 2)
        flare = t_down ** 1.06
        rx = 0.162 + 0.180 * flare + hip_swell_x
        rz_f = 0.135 + 0.108 * flare + hip_swell_f
        rz_b = 0.118 + 0.128 * flare + hip_swell_b
        z_mid = 0.008 * (1.0 - t_down) - 0.004 * t_down
    elif y <= chest_y:
        s = (y - waist_y) / (chest_y - waist_y)
        blouse = 0.005 * math.sin(s * math.pi)
        rx = 0.162 + 0.062 * (s ** 0.85) + 0.4 * blouse
        rz_f = 0.135 + 0.030 * math.sin(s * math.pi * 0.62) + blouse
        rz_b = 0.118 + 0.036 * (s ** 0.85) + 0.4 * blouse
        z_mid = 0.008 + 0.003 * math.sin(s * math.pi)
    elif y <= shoulder_y:
        # Anatomical upper-chest, scapula, and trapezius slope:
        # sleekly contoured at the armhole (rx=0.218) so sleeve wraps tangentially without a pipe crease!
        s = (y - chest_y) / (shoulder_y - chest_y)
        sm = s * s * (3.0 - 2.0 * s)
        rx = 0.218 + 0.003 * math.sin(s * math.pi) - 0.036 * (s ** 2.4)
        rz_f = 0.163 * (1.0 - 0.50 * (s ** 1.25))   # 0.163 -> 0.0815 (clears upper pecs & clavicle)
        rz_b = 0.152 * (1.0 - 0.30 * (s ** 1.35))   # 0.152 -> 0.1064 (clears scapulae & upper traps)
        z_mid = 0.008 * (1.0 - sm) - 0.011 * sm
    else:
        # Closes bodice yoke ALL THE WAY IN around the neck base (rx=0.072, rz=0.068) — ZERO neck gap!
        s = max(0.0, min(1.0, (y - shoulder_y) / max(neck_y - shoulder_y, 1e-6)))
        sm = 0.5 * (1.0 - math.cos(math.pi * s))
        rx = 0.182 * (1.0 - sm) + 0.072 * sm
        rz_f = 0.0815 * (1.0 - sm) + 0.064 * sm
        rz_b = 0.1064 * (1.0 - sm) + 0.070 * sm
        z_mid = -0.011 * (1.0 - sm) + 0.002 * sm

    return rx, rz_f, rz_b, z_mid


def eval_garment_point(y: float, phi: float, radial_offset: float = 0.0):
    """Evaluate 3D point on the garment shell in Y-up, +Z front, +X wearer's-left coords.

    phi = 0       -> Front (+Z)
    phi = +pi/2   -> Wearer's Left (+X)
    phi = +/-pi   -> Back (-Z)
    phi = -pi/2   -> Wearer's Right (-X)
    """
    rx, rz_f, rz_b, z_mid = get_garment_profile(y)
    c = math.cos(phi)
    s = math.sin(phi)
    w_front = 0.5 * (c + 1.0)
    rz = rz_b * (1.0 - w_front) + rz_f * w_front
    # Anatomical rectangular/squircle chest & scapula fullness (body diagonal radii at 35/145 deg
    # are ~1.4-1.8x larger than centerline sternum/spine between Y=1.30 and Y=1.51!)
    upper_box = float(_smooth(1.24, 1.38, y)) * (1.0 - float(_smooth(1.505, 1.535, y)))
    glute_box = float(_smooth(0.84, 0.94, y)) * (1.0 - float(_smooth(0.98, 1.06, y))) * (1.0 - w_front)
    sq_amp = 0.044 + 0.115 * upper_box + 0.055 * glute_box
    squircle = 1.0 + sq_amp * (math.sin(2.0 * phi) ** 2)
    eff_rx = max(0.042, rx * squircle + radial_offset)
    eff_rz = max(0.042, rz * squircle + radial_offset)
    x = eff_rx * s
    z = eff_rz * c + z_mid
    return np.array([x, y, z], dtype=np.float64)


# ==============================================================================
# 4. 40-BONE SKELETON BUILDER (20 MIXAMO + 20 TOPOLOGICAL cloth_* SPRING BONES)
# ==============================================================================

def build_skeleton():
    """Construct exact 40-bone hierarchy (20 Mixamo T-pose + 20 cloth_* spring bones).

    Every parent index is strictly < child index. Every cloth_* bone has its local +Y
    aligned with its downward rest direction so SpringBone_RegisterModelCloth() measures
    exact bindDir, localHeadOffset, localRestDir, and length natively.
    """
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

    # 2. Sleek Bell Sleeve Secondary Chains (4 bones, parented to ForeArm in T-pose)
    # _01 drives mid-forearm lower flare (1.405->1.355) and _02 drives strictly lower cuff hem (1.355->1.298)
    for tag, par_name, s in [("l", "mixamorig:LeftForeArm", 1.0), ("r", "mixamorig:RightForeArm", -1.0)]:
        p0 = [s * 0.56, 1.405, 0.00]
        p1 = [s * 0.630, 1.355, 0.00]
        p2 = [s * 0.642, 1.295, 0.00]
        add_cloth_bone(f"cloth_sleeve_{tag}_01", par_name, p0, p1)
        add_cloth_bone(f"cloth_sleeve_{tag}_02", f"cloth_sleeve_{tag}_01", p1, p2)

    # 3. Front Skirt Secondary Chains (6 bones, parented to Hips)
    for b1, b2, phi in [
        ("cloth_skirt_f_l",    "cloth_skirt_f_l_02",  0.30 * math.pi),
        ("cloth_skirt_f_c_01", "cloth_skirt_f_c_02",  0.00),
        ("cloth_skirt_f_r",    "cloth_skirt_f_r_02", -0.30 * math.pi),
    ]:
        p0 = eval_garment_point(0.99, phi, radial_offset=0.008)
        p1 = eval_garment_point(0.60, phi, radial_offset=0.008)
        p2 = eval_garment_point(PROPORTIONS["hem_y"], phi, radial_offset=0.008)
        add_cloth_bone(b1, "mixamorig:Hips", p0, p1)
        add_cloth_bone(b2, b1, p1, p2)

    # 4. Back Skirt Secondary Chains (6 bones, parented to Hips)
    for b1, b2, phi in [
        ("cloth_skirt_b_l",    "cloth_skirt_b_l_02",  0.70 * math.pi),
        ("cloth_skirt_b_c_01", "cloth_skirt_b_c_02",  1.00 * math.pi),
        ("cloth_skirt_b_r",    "cloth_skirt_b_r_02",  1.30 * math.pi),
    ]:
        p0 = eval_garment_point(0.99, phi, radial_offset=0.004)
        p1 = eval_garment_point(0.60, phi, radial_offset=0.004)
        p2 = eval_garment_point(PROPORTIONS["hem_y"], phi, radial_offset=0.004)
        add_cloth_bone(b1, "mixamorig:Hips", p0, p1)
        add_cloth_bone(b2, b1, p1, p2)

    # 5. Waist Ribbon Secondary Chains (4 bones, parented to Hips, distinct twin Hữu Nhậm streamers)
    for b1, b2, phi, y_bot, r_off in [
        ("cloth_ribbon_r", "cloth_ribbon_r_02", -0.32, 0.40, 0.017),
        ("cloth_ribbon_l", "cloth_ribbon_l_02", -0.10, 0.50, 0.022),
    ]:
        y_top = PROPORTIONS["waist_y"] - 0.028
        y_mid = 0.5 * (y_top + y_bot)
        p0 = eval_garment_point(y_top, phi, radial_offset=r_off)
        p1 = eval_garment_point(y_mid, phi, radial_offset=r_off)
        p2 = eval_garment_point(y_bot, phi, radial_offset=r_off)
        add_cloth_bone(b1, "mixamorig:Hips", p0, p1)
        add_cloth_bone(b2, b1, p1, p2)

    return bones, name_to_idx


# ==============================================================================
# 5. PURE-NUMPY MESH ACCUMULATOR & CRISP 2-SIDED SOLIDIFIER
# ==============================================================================

class MeshBuilder:
    """Accumulates triangulated, weighted, vertex-colored garment parts with crisp split rims."""

    def __init__(self, num_bones: int):
        self.num_bones = num_bones
        self.pos = []
        self.uv = []
        self.col = []
        self.w = []
        self.tris = []
        self.mat = []
        self._vcount = 0

    def add_grid(
        self,
        P: np.ndarray,
        UV: np.ndarray,
        COL: np.ndarray,
        W: np.ndarray,
        face_mats: np.ndarray,
        wrap_u: bool = False,
        flip: bool = False,
    ):
        R, C, _ = P.shape
        base = self._vcount
        self.pos.append(P.reshape(-1, 3))
        self.uv.append(UV.reshape(-1, 2))
        self.col.append(COL.reshape(-1, 3))
        self.w.append(W.reshape(-1, self.num_bones))
        self._vcount += R * C

        c_steps = C if wrap_u else (C - 1)
        tris = []
        mats = []
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
        return base, R, C

    def _add_rim_strip(self, Pa, Pb, UVa, UVb, COLa, COLb, Wa, Wb, mats_1d, wrap=False, flip=False):
        """Emit a dedicated split-normal rim strip (2 x K grid) so 90-deg edges never invert sheet normals."""
        Pr = np.stack([Pa, Pb], axis=0)
        UVr = np.stack([UVa, UVb], axis=0)
        COLr = np.stack([COLa, COLb], axis=0)
        Wr = np.stack([Wa, Wb], axis=0)
        fm = np.asarray(mats_1d, dtype=np.int32)[None, :]
        self.add_grid(Pr, UVr, COLr, Wr, fm, wrap_u=wrap, flip=flip)

    def add_solidified_grid(
        self,
        P: np.ndarray,
        UV: np.ndarray,
        COL_out: np.ndarray,
        W: np.ndarray,
        face_mats_out: np.ndarray,
        thickness: float = 0.0024,
        wrap_u: bool = False,
        flip: bool = False,
        face_mats_in: np.ndarray = None,
        inner_shade: float = 0.82,
        rim_mat: int = None,
        close_top: bool = True,
        close_bottom: bool = True,
        close_sides: bool = True,
    ):
        """Build a watertight 2-sided cloth shell with split-normal rims (zero shading artifacts)."""
        R, C, _ = P.shape
        N = _grid_normals(P, wrap_u=wrap_u, flip=flip)
        P_in = P - N * thickness
        COL_in = np.clip(COL_out * inner_shade, 0.0, 1.0)
        fm_in = face_mats_out if face_mats_in is None else face_mats_in

        self.add_grid(P, UV, COL_out, W, face_mats_out, wrap_u=wrap_u, flip=flip)
        self.add_grid(P_in, UV, COL_in, W, fm_in, wrap_u=wrap_u, flip=(not flip))

        c_steps = C if wrap_u else (C - 1)
        if close_bottom:
            m_b = np.full(c_steps, rim_mat, dtype=np.int32) if rim_mat is not None else face_mats_out[0, :c_steps]
            self._add_rim_strip(
                P_in[0], P[0], UV[0], UV[0], COL_in[0], COL_out[0], W[0], W[0],
                m_b, wrap=wrap_u, flip=flip,
            )
        if close_top:
            m_t = np.full(c_steps, rim_mat, dtype=np.int32) if rim_mat is not None else face_mats_out[R - 2, :c_steps]
            self._add_rim_strip(
                P[R - 1], P_in[R - 1], UV[R - 1], UV[R - 1], COL_out[R - 1], COL_in[R - 1], W[R - 1], W[R - 1],
                m_t, wrap=wrap_u, flip=flip,
            )
        if close_sides and not wrap_u:
            m_l = np.full(R - 1, rim_mat, dtype=np.int32) if rim_mat is not None else face_mats_out[:, 0]
            self._add_rim_strip(
                P[:, 0], P_in[:, 0], UV[:, 0], UV[:, 0], COL_out[:, 0], COL_in[:, 0], W[:, 0], W[:, 0],
                m_l, wrap=False, flip=flip,
            )
            m_r = np.full(R - 1, rim_mat, dtype=np.int32) if rim_mat is not None else face_mats_out[:, C - 2]
            self._add_rim_strip(
                P_in[:, C - 1], P[:, C - 1], UV[:, C - 1], UV[:, C - 1],
                COL_in[:, C - 1], COL_out[:, C - 1], W[:, C - 1], W[:, C - 1],
                m_r, wrap=False, flip=flip,
            )

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
            W[zero_mask, 0] = 1.0
            ws = W.sum(axis=1, keepdims=True)
        W /= ws
        J4 = np.argsort(-W, axis=1)[:, :4]
        W4 = np.take_along_axis(W, J4, axis=1)
        W4 /= np.maximum(W4.sum(axis=1, keepdims=True), 1e-12)
        J4[W4 <= 1e-6] = 0
        return V, N, UV, COL, J4.astype(np.uint8), W4.astype(np.float32), F, M


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


# ==============================================================================
# 6. ENGINE-EXACT WEIGHT GENERATORS (LOCKSTEP WITH human_body.glb)
# ==============================================================================

def _shared_shoulder_arm_weights(ax, y):
    """Unified (w_shd, w_arm) curve shared identically by bodice & sleeve root so the
    shoulder-to-sleeve seam deforms in 100% lockstep with zero creasing or pinching!"""
    shd_env = _smooth(0.075, 0.145, ax) * (1.0 - 0.52 * _smooth(0.185, 0.245, ax))
    w_shd = shd_env * _smooth(1.26, 1.42, y) * 0.46
    w_arm = _smooth(0.162, 0.235, ax) * _smooth(1.30, 1.44, y) * 0.54
    return w_shd, w_arm


def _torso_weights(P: np.ndarray, bidx: dict, num_bones: int, is_collar: bool = False) -> np.ndarray:
    """Smooth Mixamo spine + anatomical shoulder drape weights aligned to GAME_MIXAMO_20."""
    R, C, _ = P.shape
    pts = P.reshape(-1, 3)
    x, y = pts[:, 0], pts[:, 1]
    ax = np.abs(x)
    W = np.zeros((len(pts), num_bones), dtype=np.float64)

    t_sp0 = _smooth(0.96, 1.09, y)
    t_sp1 = _smooth(1.09, 1.23, y)
    t_sp2 = _smooth(1.23, 1.37, y)
    t_nk = _smooth(1.47, 1.55, y) * (1.0 - _smooth(0.080, 0.130, ax)) * (0.62 if is_collar else 0.48)

    w_hips = 1.0 - t_sp0
    w_sp = t_sp0 * (1.0 - t_sp1)
    w_sp1 = t_sp1 * (1.0 - t_sp2)
    w_sp2 = t_sp2 * (1.0 - t_nk)
    w_neck = t_sp2 * t_nk

    if is_collar:
        # Collar stays anchored cleanly to Spine2 / Neck so lapels never warp when arms drop
        w_shd = _smooth(0.095, 0.165, ax) * _smooth(1.36, 1.49, y) * 0.16
        w_arm = np.zeros_like(w_shd)
    else:
        w_shd, w_arm = _shared_shoulder_arm_weights(ax, y)
    w_lat = np.clip(w_shd + w_arm, 0.0, 0.86)
    scale_core = 1.0 - w_lat

    W[:, bidx["mixamorig:Hips"]] = w_hips * scale_core
    W[:, bidx["mixamorig:Spine"]] = w_sp * scale_core
    W[:, bidx["mixamorig:Spine1"]] = w_sp1 * scale_core
    W[:, bidx["mixamorig:Spine2"]] = w_sp2 * scale_core
    W[:, bidx["mixamorig:Neck"]] = w_neck * scale_core

    left = (x >= 0.0).astype(np.float64)
    right = 1.0 - left
    W[:, bidx["mixamorig:LeftShoulder"]] = w_shd * left
    W[:, bidx["mixamorig:RightShoulder"]] = w_shd * right
    W[:, bidx["mixamorig:LeftArm"]] = w_arm * left
    W[:, bidx["mixamorig:RightArm"]] = w_arm * right
    return W.reshape(R, C, num_bones)


def _skirt_weights(P: np.ndarray, is_front: bool, bidx: dict, num_bones: int) -> np.ndarray:
    """Smooth Hips + subtle UpLeg + 2-segment cloth_skirt_{f,b}_{l,c,r} weights."""
    R, C, _ = P.shape
    pts = P.reshape(-1, 3)
    x, y = pts[:, 0], pts[:, 1]
    W = np.zeros((len(pts), num_bones), dtype=np.float64)

    y_hem = PROPORTIONS["hem_y"]
    y_waist = PROPORTIONS["waist_y"]
    t_down = np.clip((y_waist - y) / (y_waist - y_hem), 0.0, 1.0)

    w_hips = 1.0 - _smooth(0.03, 0.16, t_down)
    w_leg = 0.12 * np.sin(np.clip(t_down * 1.35, 0.0, 1.0) * math.pi) * _smooth(0.06, 0.22, t_down)
    w_cloth = np.clip(1.0 - w_hips - w_leg, 0.0, 1.0)

    W[:, bidx["mixamorig:Hips"]] = w_hips
    left_mask = (x >= 0.0).astype(np.float64)
    W[:, bidx["mixamorig:LeftUpLeg"]] = w_leg * left_mask
    W[:, bidx["mixamorig:RightUpLeg"]] = w_leg * (1.0 - left_mask)

    w_seg2 = _smooth(0.32, 0.72, t_down)
    w_seg1 = 1.0 - w_seg2

    rx_ref = np.array([get_garment_profile(float(yy))[0] for yy in y])
    nx = np.clip(x / np.maximum(rx_ref, 0.14), -1.0, 1.0)
    w_side = _smooth(0.12, 0.84, np.abs(nx))
    w_mid = 1.0 - w_side

    pf = "f" if is_front else "b"
    b_l1, b_l2 = bidx[f"cloth_skirt_{pf}_l"], bidx[f"cloth_skirt_{pf}_l_02"]
    b_c1, b_c2 = bidx[f"cloth_skirt_{pf}_c_01"], bidx[f"cloth_skirt_{pf}_c_02"]
    b_r1, b_r2 = bidx[f"cloth_skirt_{pf}_r"], bidx[f"cloth_skirt_{pf}_r_02"]

    W[:, b_l1] = w_cloth * w_side * left_mask * w_seg1
    W[:, b_l2] = w_cloth * w_side * left_mask * w_seg2
    W[:, b_r1] = w_cloth * w_side * (1.0 - left_mask) * w_seg1
    W[:, b_r2] = w_cloth * w_side * (1.0 - left_mask) * w_seg2
    W[:, b_c1] = w_cloth * w_mid * w_seg1
    W[:, b_c2] = w_cloth * w_mid * w_seg2
    return W.reshape(R, C, num_bones)


# ==============================================================================
# 7. GARMENT PART BUILDERS (ALL Y-UP, +Z FRONT, T-POSE)
# ==============================================================================

def _add_inner_trung_y(mb: MeshBuilder, bidx: dict):
    """Crisp cream silk Trung Y inner collar band nestled cleanly inside the outer Giao Lĩnh collar.

    Hugs the neck vertically (r~0.069, +6 mm above outer collar nape) and frames the inner V-chest
    without ever extending sideways toward the shoulders or armpits.
    """
    n_path = 64
    u_cross = [0.000, 0.350, 0.700, 1.000]
    R, C = n_path + 1, len(u_cross)

    y_under_start = 1.290
    y_over_end = 1.255
    y_neck_side = 1.542
    y_nape_top = PROPORTIONS["inner_neck_y"]  # 1.566 (+6 mm proud above outer standing collar)

    phi_i = np.zeros(R, dtype=np.float64)
    y_i   = np.zeros(R, dtype=np.float64)
    phi_o = np.zeros(R, dtype=np.float64)
    y_o   = np.zeros(R, dtype=np.float64)
    y_ev_o = np.zeros(R, dtype=np.float64)
    r_off = np.zeros(R, dtype=np.float64)
    ao_path = np.ones(R, dtype=np.float64)

    for k in range(R):
        s = k / n_path
        if s <= 0.32:
            t = s / 0.32
            phi_i[k] = 0.08 * (1.0 - t) + (-0.22) * t
            y_i[k]   = y_under_start + (y_neck_side - y_under_start) * (t ** 0.95)
            phi_o[k] = phi_i[k] - 0.19 * (1.0 - 0.15 * t)
            y_o[k]   = y_i[k] - 0.014
            y_ev_o[k] = y_o[k] + 0.008 * (t ** 3.0)
            r_off[k] = -0.0010 + 0.0015 * t
            ao_path[k] = 0.85 + 0.13 * t
        elif s < 0.64:
            t = (s - 0.32) / 0.32
            arch = math.sin(math.pi * t)
            phi_i[k] = (-0.22) * (1.0 - t) + (-2.0 * math.pi + 0.22) * t
            phi_o[k] = (-0.38) * (1.0 - t) + (-2.0 * math.pi + 0.38) * t
            y_i[k]   = y_neck_side + (y_nape_top - y_neck_side) * (arch ** 0.62)
            y_o[k]   = (y_neck_side - 0.014) + ((y_nape_top - 0.026) - (y_neck_side - 0.014)) * (arch ** 0.65)
            # Tight neck cylinder profile (evaluated at neck_y=1.544 -> rx=0.072, rz_b=0.070)
            y_ev_o[k] = PROPORTIONS["neck_y"]
            r_off[k] = (0.0005 * (1.0 - t) + 0.0025 * t) - 0.0020 * arch
            ao_path[k] = 0.96 + 0.04 * arch
        else:
            t = (s - 0.64) / 0.36
            phi_i[k] = (-2.0 * math.pi + 0.22) * (1.0 - t) + (-2.0 * math.pi - 0.14) * t
            y_i[k]   = y_neck_side * (1.0 - t ** 1.02) + y_over_end * (t ** 1.02)
            phi_o[k] = phi_i[k] + 0.19 * (0.85 + 0.15 * t)
            y_o[k]   = y_i[k] - 0.014
            y_ev_o[k] = y_o[k] + 0.008 * ((1.0 - t) ** 3.0)
            r_off[k] = 0.0025 + 0.0008 * math.sin(math.pi * t)
            ao_path[k] = 0.96 - 0.08 * t

    for arr in (phi_i, y_i, phi_o, y_o, y_ev_o, r_off):
        for _ in range(5):
            tmp = arr.copy()
            tmp[1:-1] = 0.25 * arr[:-2] + 0.50 * arr[1:-1] + 0.25 * arr[2:]
            arr[:] = tmp

    def eval_in_pt(y_prof: float, y_true: float, phi_val: float, roff_val: float) -> np.ndarray:
        pt = eval_garment_point(min(y_prof, PROPORTIONS["neck_y"]), phi_val, radial_offset=roff_val)
        pt[1] = y_true
        return pt

    P_in = np.array([eval_in_pt(float(y_i[k]), float(y_i[k]), float(phi_i[k]), float(r_off[k])) for k in range(R)])
    P_out = np.array([eval_in_pt(float(y_ev_o[k]), float(y_o[k]), float(phi_o[k]), float(r_off[k]) - 0.0006) for k in range(R)])

    T = np.zeros_like(P_in)
    T[1:-1] = P_in[2:] - P_in[:-2]
    T[0] = P_in[1] - P_in[0]
    T[-1] = P_in[-1] - P_in[-2]
    T /= np.maximum(np.linalg.norm(T, axis=1, keepdims=True), 1e-12)

    P = np.zeros((R, C, 3), dtype=np.float64)
    UV = np.zeros((R, C, 2), dtype=np.float64)
    COL = np.ones((R, C, 3), dtype=np.float64)

    for k in range(R):
        span = P_out[k] - P_in[k]
        B = span / max(float(np.linalg.norm(span)), 1e-12)
        N_surf = np.cross(T[k], B)
        N_surf /= max(float(np.linalg.norm(N_surf)), 1e-12)
        for i, u in enumerate(u_cross):
            phi_u = float(phi_i[k] * (1.0 - u) + phi_o[k] * u)
            y_u   = float(y_i[k] * (1.0 - u) + y_o[k] * u)
            ye_u  = float(y_i[k] * (1.0 - u) + y_ev_o[k] * u)
            p_base = eval_in_pt(ye_u, y_u, phi_u, float(r_off[k]) - 0.0006 * u)
            crown = 0.0014 * math.sin(math.pi * u)
            P[k, i] = p_base + N_surf * crown
            UV[k, i] = (u, (k / n_path) * 10.0)
            COL[k, i] = np.clip(ao_path[k] * (0.94 + 0.06 * (1.0 - u)), 0.78, 1.02)

    face_mats = np.full((R - 1, C - 1), MAT_INNER, dtype=np.int32)
    W = _torso_weights(P, bidx, mb.num_bones, is_collar=True)
    mb.add_solidified_grid(P, UV, COL, W, face_mats, thickness=0.0018, flip=True, inner_shade=0.84)


def _add_giao_linh_bodice(mb: MeshBuilder, bidx: dict):
    """Tailored deep ink-indigo silk upper bodice closing ALL THE WAY IN around the neck base.

    Dense upper-yoke rings (v=0.88..1.00) drape smoothly across the entire trapezius right up
    to the base of the neck (rx=0.072, rz=0.070 at Y=1.544) — ZERO gaping neck bucket hole!
    """
    segs_u = 38
    u_list = [i / segs_u for i in range(segs_u + 1)]
    v_body = [0.000] + [0.030 + 0.850 * (r / 15) for r in range(16)]
    v_list = v_body + [0.910, 0.940, 0.968, 0.986, 1.000]

    R, C = len(v_list), len(u_list)
    y_min = PROPORTIONS["waist_y"] - 0.032
    y_max = PROPORTIONS["neck_y"]  # 1.544: closes tightly all the way around the neck base!

    P = np.zeros((R, C, 3), dtype=np.float64)
    UV = np.zeros((R, C, 2), dtype=np.float64)
    COL = np.ones((R, C, 3), dtype=np.float64)

    for r, v in enumerate(v_list):
        y = y_min + v * (y_max - y_min)
        t_top = 1.0 - v
        # Front V-split tucked strictly underneath the outer collar band (phi=0.38 > collar inner 0.28!)
        phi_start = 0.38 - 0.84 * (t_top ** 0.95)
        phi_end = (2.0 * math.pi - 0.38) + 0.78 * (t_top ** 0.95)

        for i, u in enumerate(u_list):
            phi = phi_start + u * (phi_end - phi_start)
            lat_sq = math.sin(phi) ** 2
            # Keep trapezius dome full (zero dip below body traps) while gently hugging shoulder slope
            trap_drop = 0.004 * lat_sq * _smooth(0.80, 0.95, v) * (1.0 - _smooth(0.95, 1.00, v))
            y_eff = y - trap_drop

            layer_offset = 0.0036 * math.cos(math.pi * u) * (0.40 + 0.60 * t_top)

            # Soft directional silk blousing folds (dampened near top neckline so collar seats flush!)
            fold_damp = 1.0 - float(_smooth(0.84, 0.96, v))
            fold_w = 0.0032 * math.sin(7.0 * phi + 3.2 * v) * (1.0 - _smooth(0.15, 0.76, v)) * _smooth(0.0, 0.14, v)
            fold_c = 0.0024 * math.sin(5.0 * phi - 5.0 * v) * math.sin(math.pi * v) * fold_damp
            fold = fold_w + fold_c

            pt = eval_garment_point(y_eff, phi, radial_offset=layer_offset + fold)
            P[r, i] = pt
            UV[r, i] = (phi / (2.0 * math.pi) * 6.4, (y - PROPORTIONS["hem_y"]) * 4.2)

            ao = 1.0
            if u > 0.84:
                ao -= 0.18 * ((u - 0.84) / 0.16) * (1.0 - 0.45 * v)
            ao -= 0.14 * (1.0 - _smooth(0.0, 0.18, v))
            # Soft shadow right under the standing collar base around the neck
            ao -= 0.08 * float(_smooth(0.93, 1.00, v))
            ao += 0.050 * (fold / 0.0030)
            COL[r, i] = np.clip(ao, 0.70, 1.04)

    face_mats = np.full((R - 1, C - 1), MAT_TEAL, dtype=np.int32)
    face_mats_in = np.full((R - 1, C - 1), MAT_TEAL, dtype=np.int32)
    W = _torso_weights(P, bidx, mb.num_bones, is_collar=False)
    mb.add_solidified_grid(
        P, UV, COL, W, face_mats,
        thickness=0.0022,
        face_mats_in=face_mats_in,
        inner_shade=0.68,
    )


def _add_giao_linh_collar(mb: MeshBuilder, bidx: dict):
    """Build ONE unbroken, 100%-proud, regal standing + crossing 3D Giao Lĩnh Hữu Nhậm collar band.

    Seats completely OUTSIDE the closed bodice neckline dome (Y_nape_base=1.536 -> Y_nape_top=1.560,
    r_in=0.077 -> r_out=0.089) so BOTH the ivory silk band AND its antique gold Hồi Văn outer piping
    wrap 100% visibly and continuously around the back nape, neck sides, and crossed front lapels —
    tucking cleanly beneath the top gold rim of the waist sash with zero notches!
    """
    n_path = 84
    u_cross = [0.000, 0.250, 0.520, 0.750, 0.790, 0.900, 1.000]
    R, C = n_path + 1, len(u_cross)

    y_waist = PROPORTIONS["waist_y"] - 0.010  # Ends cleanly inside the waist sash (1.065)
    y_under_start = 1.258                     # Hidden cleanly behind hero left lapel crossover
    y_neck_side = PROPORTIONS["neck_y"]       # 1.544: 100% C0/C1 continuous with nape profile!
    y_nape_top = PROPORTIONS["collar_top_y"]  # 1.560 regal vertical standing nape collar!

    phi_i = np.zeros(R, dtype=np.float64)
    y_i   = np.zeros(R, dtype=np.float64)
    phi_o = np.zeros(R, dtype=np.float64)
    y_o   = np.zeros(R, dtype=np.float64)
    y_ev_i = np.zeros(R, dtype=np.float64)
    y_ev_o = np.zeros(R, dtype=np.float64)
    r_off = np.zeros(R, dtype=np.float64)
    ao_path = np.ones(R, dtype=np.float64)

    for k in range(R):
        s = k / n_path
        if s <= 0.30:
            # Right under-lapel: (+0.06, 1.258) -> right side of neck (-0.30, 1.544)
            t = s / 0.30
            phi_i[k] = 0.06 * (1.0 - t) + (-0.30) * t
            y_i[k]   = y_under_start + (y_neck_side - y_under_start) * (t ** 0.94)
            d_phi    = 0.21 * (1.0 - 0.16 * (t ** 1.3))
            phi_o[k] = phi_i[k] - d_phi
            y_o[k]   = y_i[k] - 0.012 * (0.65 + 0.35 * (1.0 - t))
            # Strictly continuous radial evaluation at t=1: y_ev_i=1.544, y_ev_o=1.539 (ZERO kink!)
            y_ev_i[k] = y_i[k]
            y_ev_o[k] = y_o[k] + (1.539 - (y_neck_side - 0.0078)) * (t ** 2.4)
            r_off[k] = 0.0022 + 0.0068 * float(_smooth(0.05, 0.40, t))
            ao_path[k] = 0.84 + 0.14 * float(_smooth(0.20, 0.80, t))
        elif s < 0.60:
            # Proud vertical standing nape collar wrapping seamlessly around closed neck base!
            t = (s - 0.30) / 0.30
            arch = math.sin(math.pi * t)
            phi_i[k] = (-0.30) * (1.0 - t) + (-2.0 * math.pi + 0.30) * t
            phi_o[k] = (-0.476) * (1.0 - t) + (-2.0 * math.pi + 0.476) * t
            y_i[k]   = y_neck_side + (y_nape_top - y_neck_side) * (arch ** 0.58)
            y_o[k]   = (y_neck_side - 0.0078) + ((y_nape_top - 0.024) - (y_neck_side - 0.0078)) * (arch ** 0.60)
            y_ev_i[k] = PROPORTIONS["neck_y"]
            y_ev_o[k] = 1.539 + 0.002 * arch
            r_off[k] = (0.0090 * (1.0 - t) + 0.0114 * t) - 0.0030 * arch
            ao_path[k] = 0.96 + 0.04 * arch
        else:
            # Hero Left OVER-lapel: left side of neck (+0.30, 1.544) -> tucked cleanly inside belt (-0.44, 1.065)
            t = (s - 0.60) / 0.40
            phi_i[k] = (-2.0 * math.pi + 0.30) * (1.0 - t) + (-2.0 * math.pi - 0.44) * t
            y_i[k]   = y_neck_side * (1.0 - t ** 1.04) + y_waist * (t ** 1.04)
            d_phi    = 0.21 * (0.84 + 0.22 * (t ** 0.8))
            phi_o[k] = phi_i[k] + d_phi
            y_o[k]   = y_i[k] - 0.012 * (0.65 + 0.35 * t)
            y_ev_i[k] = y_i[k]
            y_ev_o[k] = y_o[k] + (1.539 - (y_neck_side - 0.0078)) * ((1.0 - t) ** 2.4)
            waist_tuck = float(_smooth(0.84, 1.00, t))
            r_off[k] = (0.0114 + 0.0018 * math.sin(math.pi * t)) * (1.0 - 0.40 * waist_tuck)
            ao_path[k] = 1.00 - 0.12 * waist_tuck

    for arr in (phi_i, y_i, phi_o, y_o, y_ev_i, y_ev_o, r_off):
        for _ in range(7):
            tmp = arr.copy()
            tmp[1:-1] = 0.25 * arr[:-2] + 0.50 * arr[1:-1] + 0.25 * arr[2:]
            arr[:] = tmp

    def eval_collar_pt(y_prof: float, y_true: float, phi_val: float, roff_val: float) -> np.ndarray:
        pt = eval_garment_point(min(y_prof, PROPORTIONS["neck_y"]), phi_val, radial_offset=roff_val)
        pt[1] = y_true
        return pt

    P_in = np.array([eval_collar_pt(float(y_ev_i[k]), float(y_i[k]), float(phi_i[k]), float(r_off[k])) for k in range(R)])
    P_out = np.array([eval_collar_pt(float(y_ev_o[k]), float(y_o[k]), float(phi_o[k]), float(r_off[k]) - 0.0005) for k in range(R)])

    P = np.zeros((R, C, 3), dtype=np.float64)
    UV = np.zeros((R, C, 2), dtype=np.float64)
    COL = np.ones((R, C, 3), dtype=np.float64)

    T = np.zeros_like(P_in)
    T[1:-1] = P_in[2:] - P_in[:-2]
    T[0] = P_in[1] - P_in[0]
    T[-1] = P_in[-1] - P_in[-2]
    T /= np.maximum(np.linalg.norm(T, axis=1, keepdims=True), 1e-12)

    for k in range(R):
        span = P_out[k] - P_in[k]
        B = span / max(float(np.linalg.norm(span)), 1e-12)
        N_surf = np.cross(T[k], B)
        N_surf /= max(float(np.linalg.norm(N_surf)), 1e-12)

        for i, u in enumerate(u_cross):
            phi_u = float(phi_i[k] * (1.0 - u) + phi_o[k] * u)
            y_u   = float(y_i[k] * (1.0 - u) + y_o[k] * u)
            ye_u  = float(y_ev_i[k] * (1.0 - u) + y_ev_o[k] * u)
            p_base = eval_collar_pt(ye_u, y_u, phi_u, float(r_off[k]) - 0.0005 * u)
            crown = 0.0024 * math.sin(math.pi * u) + 0.0008 * (1.0 - u)
            P[k, i] = p_base + N_surf * crown
            UV[k, i] = (u * 1.0, (k / n_path) * 15.0)
            seam_ao = 1.0 - 0.09 * math.exp(-((u - 0.77) / 0.055) ** 2) - 0.05 * (1.0 - u)
            COL[k, i] = np.clip(ao_path[k] * seam_ao, 0.72, 1.02)

    face_mats = np.zeros((R - 1, C - 1), dtype=np.int32)
    for i in range(C - 1):
        um = 0.5 * (u_cross[i] + u_cross[i + 1])
        face_mats[:, i] = MAT_GOLD if um >= 0.77 else MAT_IVORY

    W = _torso_weights(P, bidx, mb.num_bones, is_collar=True)
    mb.add_solidified_grid(
        P, UV, COL, W, face_mats,
        thickness=0.0028,
        flip=True,
        face_mats_in=np.full((R - 1, C - 1), MAT_IVORY, dtype=np.int32),
        inner_shade=0.80,
    )


def _add_skirt_panel(
    mb: MeshBuilder,
    bidx: dict,
    phi_left_waist: float,
    phi_right_waist: float,
    phi_left_hem: float,
    phi_right_hem: float,
    base_offset: float,
    is_front: bool,
    arch_camber: float = 0.0040,
    pleat_amp: float = 0.0105,
    pleat_freq: float = 7.0,
    with_hem_trim: bool = True,
):
    """Build a 2-sided flowing pleated A-line skirt panel with ink-wash ombré, gold & ivory border."""
    u_mid_pts = [0.032 + (0.968 - 0.032) * (i / 24) for i in range(25)]
    u_list = [0.000, 0.015, 0.025] + u_mid_pts + [0.975, 0.985, 1.000]
    v_mid_pts = [0.056 + (0.990 - 0.056) * (r / 15) for r in range(16)]
    v_list = [0.000, 0.022, 0.036, 0.048] + v_mid_pts + [1.000]

    R, C = len(v_list), len(u_list)
    y_hem = PROPORTIONS["hem_y"]
    y_top = PROPORTIONS["waist_y"] + 0.010

    P = np.zeros((R, C, 3), dtype=np.float64)
    UV = np.zeros((R, C, 2), dtype=np.float64)
    COL = np.ones((R, C, 3), dtype=np.float64)

    for r, v in enumerate(v_list):
        y = y_hem + v * (y_top - y_hem)
        t_down = 1.0 - v
        phi_0 = phi_left_waist * (1.0 - t_down) + phi_left_hem * t_down
        phi_1 = phi_right_waist * (1.0 - t_down) + phi_right_hem * t_down

        for i, u in enumerate(u_list):
            phi = phi_0 + u * (phi_1 - phi_0)
            camber = arch_camber * math.sin(math.pi * u) * (0.25 + 0.75 * t_down)
            # Crisp architectural silk drapery flutes (break up specular into realistic cloth folds!)
            edge_env = math.sin(math.pi * u) ** 0.75
            pleat_wave = math.cos(pleat_freq * math.pi * u) * edge_env
            pleat = pleat_amp * pleat_wave * (t_down ** 0.80)
            # Subtle graceful curve up toward the martial-arts side vents
            hem_curve = 0.018 * ((2.0 * u - 1.0) ** 2) * (t_down ** 2.0)

            pt = eval_garment_point(y + hem_curve, phi, radial_offset=base_offset + camber + pleat)
            P[r, i] = pt
            UV[r, i] = (u * 3.8, (y - y_hem) * 4.2)

            # Ink-wash (Thủy Mặc) vertical ombré + crisp fold ridge/valley shading + belt cast shadow
            ombre = 1.0 - 0.16 * (t_down ** 1.35)
            belt_ao = 1.0 - 0.16 * _smooth(0.84, 1.00, v)
            fold_ao = 1.0 + 0.11 * pleat_wave * (t_down ** 0.70)
            shade = np.clip(ombre * belt_ao * fold_ao, 0.64, 1.05)
            # Subtle cool midnight-indigo ink shift near hem (gives painterly Wuxia silk richness)
            COL[r, i] = np.clip([shade * (1.0 - 0.04 * t_down), shade * (1.0 - 0.02 * t_down), shade], 0.58, 1.05)

    face_mats = np.zeros((R - 1, C - 1), dtype=np.int32)
    face_mats_in = np.full((R - 1, C - 1), MAT_IVORY, dtype=np.int32)
    for r in range(R - 1):
        vm = 0.5 * (v_list[r] + v_list[r + 1])
        for i in range(C - 1):
            um = 0.5 * (u_list[i] + u_list[i + 1])
            if with_hem_trim:
                in_ivory = (vm < 0.022) or (um < 0.015) or (um > 0.985)
                in_gold = (0.022 <= vm <= 0.036) or (0.015 <= um <= 0.025) or (0.975 <= um <= 0.985)
                # Subtle dark ink-indigo band just above gold hem border (vm 0.036..0.048) via COL darken
                face_mats[r, i] = MAT_IVORY if in_ivory else (MAT_GOLD if in_gold else MAT_TEAL)
            else:
                face_mats[r, i] = MAT_TEAL

    W = _skirt_weights(P, is_front=is_front, bidx=bidx, num_bones=mb.num_bones)
    mb.add_solidified_grid(
        P, UV, COL, W, face_mats,
        thickness=0.0024,
        face_mats_in=face_mats_in,
        inner_shade=0.82,
    )


def _add_all_skirt_panels(mb: MeshBuilder, bidx: dict):
    """Build Heroic Split-Panel A-Line Skirt (Front Outer, Front Hữu Nhậm Underlap, Back Panel)."""
    # 1. Front Outer Panel — crisp martial-arts side vents opening gracefully at +/-0.46*pi at hem
    _add_skirt_panel(
        mb, bidx,
        phi_left_waist=-0.52 * math.pi, phi_right_waist=0.52 * math.pi,
        phi_left_hem=-0.46 * math.pi,   phi_right_hem=0.46 * math.pi,
        base_offset=0.0080, is_front=True,
        arch_camber=0.0045, pleat_amp=0.0105, pleat_freq=7.0,
    )
    # 2. Inner Traditional Right-Side Underlap Flap (layered depth visible through side slit & motion)
    _add_skirt_panel(
        mb, bidx,
        phi_left_waist=-0.51 * math.pi, phi_right_waist=-0.04 * math.pi,
        phi_left_hem=-0.48 * math.pi,   phi_right_hem=-0.10 * math.pi,
        base_offset=-0.0010, is_front=True,
        arch_camber=0.0018, pleat_amp=0.0040, pleat_freq=3.0,
        with_hem_trim=True,
    )
    # 3. Back Split Panel — flowing pleated martial-arts back tail
    _add_skirt_panel(
        mb, bidx,
        phi_left_waist=0.48 * math.pi, phi_right_waist=1.52 * math.pi,
        phi_left_hem=0.54 * math.pi,   phi_right_hem=1.46 * math.pi,
        base_offset=0.0030, is_front=False,
        arch_camber=0.0050, pleat_amp=0.0110, pleat_freq=7.0,
    )


def _add_wide_sleeves(mb: MeshBuilder, bidx: dict):
    """Build 2-sided sleek martial-arts draped bell sleeves (Trung Quảng Tụ) for Left & Right arms.

    Designed specifically from ANIM 1: idle kinematics (74.3-deg downward arm rotation):
    - Tailored upper-arm & shoulder cap (y_top=1.498..1.526, rz=0.092..0.095) matching the
      anatomical trapezius-deltoid slope cleanly — ZERO puffy football shoulder pads!
    - Graceful flared bell cuff from elbow to wrist (y_bot=1.365->1.305, rz=0.094->0.110,
      ending cleanly at |X|=0.675 top / 0.695 bottom to reveal wrists & relaxed hands).
    - Under the real 74-deg Idle arm drop, the inner cuff lip hangs cleanly OUTSIDE the hips
      (|X| >= 0.19m), leaving 100% of the waist sash, jade clasp, and flowing ribbons unobstructed!
    """
    t_body = [0.855 * (r / 16) for r in range(17)]
    t_outer = t_body + [0.882, 0.905, 0.960, 1.000]
    t_inner = [1.000, 0.982, 0.920, 0.800, 0.650, 0.500]
    segs = 30

    for side, s, s_tag in [("Left", 1.0, "l"), ("Right", -1.0, "r")]:
        b_sh = bidx[f"mixamorig:{side}Shoulder"]
        b_arm = bidx[f"mixamorig:{side}Arm"]
        b_fa = bidx[f"mixamorig:{side}ForeArm"]
        b_ha = bidx[f"mixamorig:{side}Hand"]
        b_sp2 = bidx["mixamorig:Spine2"]
        b_slv1 = bidx[f"cloth_sleeve_{s_tag}_01"]
        b_slv2 = bidx[f"cloth_sleeve_{s_tag}_02"]

        def build_sleeve_grid(t_list, inset: float, is_inner: bool):
            R = len(t_list)
            P = np.zeros((R, segs, 3), dtype=np.float64)
            UV = np.zeros((R, segs, 2), dtype=np.float64)
            COL = np.ones((R, segs, 3), dtype=np.float64)
            W = np.zeros((R, segs, mb.num_bones), dtype=np.float64)

            for r, t in enumerate(t_list):
                # Tangential drop-shoulder (Lạc Kiên): starts smoothly inside shoulder yoke (0.145)
                # and flares its root Z-profile (0.128 -> 0.105) to merge tangentially with chest/scapula!
                x_base = 0.145 + (0.678 - 0.145) * t
                root_flare = (1.0 - float(_smooth(0.00, 0.30, t))) ** 1.4

                # Top line matches bodice shoulder dome smoothly (1.516 -> 1.530 -> 1.524)
                y_top = (
                    1.516
                    + 0.017 * _smooth(0.08, 0.56, t)
                    - 0.008 * _smooth(0.68, 1.00, t)
                    + 0.004 * math.sin(math.pi * t)
                    - inset
                )

                # Bottom curve: sweeps smoothly out from torso rib wall (1.275) up into fitted upper arm (1.366)
                # then gracefully flares into the martial-arts bell cuff (1.300 at wrist)
                armpit_blend = 1.0 - _smooth(0.00, 0.38, t)
                bell_flare = _smooth(0.38, 1.00, t) ** 1.15
                y_bot = 1.366 - 0.088 * armpit_blend - 0.066 * bell_flare + inset

                cy = 0.5 * (y_top + y_bot)
                ry = max(0.032, 0.5 * (y_top - y_bot))

                # Tangential root Z-flare (0.136 at bodice shoulder wall -> 0.104 mid-arm -> 0.114 bell cuff)
                # wraps smoothly OVER the bodice armhole so lighting glides seamlessly from chest to sleeve!
                rz = max(0.030, (0.104 + 0.032 * root_flare + 0.011 * (t ** 1.35) + 0.003 * math.sin(math.pi * t)) - inset)
                z_bias = 0.012 * math.sin(math.pi * t) + 0.003 * root_flare

                for i in range(segs):
                    theta = (i / segs) * 2.0 * math.pi
                    sv = math.sin(theta)  # +1 top, -1 bottom (draping cuff bell)
                    ch = math.cos(theta)  # +1 front (+Z), -1 back (-Z)

                    # Gentle squircle fullness at shoulder root matches bodice upper-chest/scapula contour!
                    pouch_taper = 1.0 - 0.14 * max(0.0, -sv - 0.15) * (t ** 0.8)
                    fold = (
                        0.0038 * math.sin(5.0 * theta + 1.8 * t) * (0.35 + 0.65 * max(0.0, -sv)) * (t ** 0.7)
                        if not is_inner else 0.0
                    )
                    x_cuff_slant = 0.016 * max(0.0, -sv) * (t ** 1.8)
                    # Slightly taper the hidden inner-most root ring (t=0) inward -3 mm so it tucks silently inside bodice
                    root_tuck = -0.0035 * (1.0 - float(_smooth(0.00, 0.08, t))) if not is_inner else 0.0

                    vx = s * (x_base + x_cuff_slant)
                    vy = cy + (ry + root_tuck) * sv
                    vz = z_bias + ((rz + root_tuck) * pouch_taper + fold) * ch

                    P[r, i] = (vx, vy, vz)
                    UV[r, i] = (t * 4.2, (theta / (2.0 * math.pi)) * 3.8)

                    if is_inner:
                        depth_t = (1.0 - t) / 0.50
                        COL[r, i] = np.clip(0.90 - 0.40 * depth_t, 0.44, 0.92)
                    else:
                        ao = 1.0
                        ao -= 0.12 * (1.0 - _smooth(0.0, 0.24, t)) * max(0.0, -sv)
                        ao += 0.05 * (fold / 0.0038) if fold != 0.0 else 0.0
                        ao -= 0.04 * (t ** 1.2)
                        COL[r, i] = np.clip(ao, 0.76, 1.04)

                    # Engine-native weights:
                    # 1) Lower bell flare below forearm uses cloth_sleeve_*
                    hang = (1.0 - _smooth(-0.85, -0.25, sv)) * _smooth(0.48, 0.74, t) * (1.0 - _smooth(1.375, 1.415, vy))
                    w_cloth = 0.80 * hang
                    w_main = 1.0 - w_cloth

                    # 2) Unified shoulder weights inside |X| < 0.25 (IDENTICAL to bodice!) smoothly handing off to Arm/ForeArm!
                    ax_p = abs(vx)
                    blend_arm = float(_smooth(0.195, 0.275, ax_p))
                    w_sh_u, w_arm_u = _shared_shoulder_arm_weights(ax_p, vy)
                    w_sh_u, w_arm_u = float(w_sh_u), float(w_arm_u)
                    w_sp2_u = max(0.0, 1.0 - w_sh_u - w_arm_u)

                    w_fa_v = float(_smooth(0.435, 0.525, ax_p))
                    w_ha_v = float(_smooth(0.655, 0.725, ax_p)) * 0.12
                    w_arm_outer = max(0.0, 1.0 - w_fa_v)
                    w_fa_v = max(0.0, w_fa_v - w_ha_v)

                    w_sp2_v = w_sp2_u * (1.0 - blend_arm)
                    w_sh_v  = w_sh_u  * (1.0 - blend_arm)
                    w_arm_v = w_arm_u * (1.0 - blend_arm) + w_arm_outer * blend_arm

                    W[r, i, b_sp2] = w_main * w_sp2_v
                    W[r, i, b_sh] = w_main * w_sh_v
                    W[r, i, b_arm] = w_main * w_arm_v
                    W[r, i, b_fa] = w_main * w_fa_v * blend_arm
                    W[r, i, b_ha] = w_main * w_ha_v * blend_arm

                    if w_cloth > 1e-6:
                        # Strictly below cloth_sleeve_02 head (Y=1.350) -> clean downward terminal dir!
                        w_s2 = 1.0 - _smooth(1.320, 1.360, vy)
                        W[r, i, b_slv1] = w_cloth * (1.0 - w_s2)
                        W[r, i, b_slv2] = w_cloth * w_s2

            return P, UV, COL, W

        P_out, UV_out, COL_out, W_out = build_sleeve_grid(t_outer, inset=0.0, is_inner=False)
        P_in, UV_in, COL_in, W_in = build_sleeve_grid(t_inner, inset=0.0026, is_inner=True)

        fm_out = np.zeros((len(t_outer) - 1, segs), dtype=np.int32)
        for r in range(len(t_outer) - 1):
            tm = 0.5 * (t_outer[r] + t_outer[r + 1])
            fm_out[r, :] = MAT_IVORY if tm > 0.895 else (MAT_GOLD if tm >= 0.855 else MAT_TEAL)
        fm_in = np.full((len(t_inner) - 1, segs), MAT_IVORY, dtype=np.int32)

        outer_flip = (s > 0)
        mb.add_grid(P_out, UV_out, COL_out, W_out, fm_out, wrap_u=True, flip=outer_flip)
        mb.add_grid(P_in, UV_in, COL_in, W_in, fm_in, wrap_u=True, flip=outer_flip)
        mb._add_rim_strip(
            P_out[-1], P_in[0], UV_out[-1], UV_in[0], COL_out[-1], COL_in[0], W_out[-1], W_in[0],
            np.full(segs, MAT_IVORY, dtype=np.int32), wrap=True, flip=outer_flip,
        )


def _add_waist_sash_and_ribbons(mb: MeshBuilder, bidx: dict):
    """Build structured deep ink-navy Yêu Đới sash, jade-gold clasp, and contrasting ivory-gold ribbons."""
    y_c = PROPORTIONS["waist_y"]

    # 1. Structured Martial-Arts Waist Sash / Yêu Đới (7.6 cm tall, deep obsidian-navy contrast + gold trim)
    v_belt = [0.000, 0.140, 0.320, 0.500, 0.680, 0.860, 1.000]
    segs = 38
    belt_half_h = 0.038
    R = len(v_belt)

    P = np.zeros((R, segs, 3), dtype=np.float64)
    UV = np.zeros((R, segs, 2), dtype=np.float64)
    COL = np.ones((R, segs, 3), dtype=np.float64)
    W = np.zeros((R, segs, mb.num_bones), dtype=np.float64)

    for r, v in enumerate(v_belt):
        y = (y_c - belt_half_h) + v * (2.0 * belt_half_h)
        # Sculpted double-layer sash profile (subtle central silk cinch groove)
        pillow = 0.0032 * math.sin(v * math.pi) - 0.0008 * math.exp(-((v - 0.5) / 0.12) ** 2)
        is_gold_row = (v <= 0.145) or (v >= 0.855)
        for i in range(segs):
            phi = (i / segs) * 2.0 * math.pi
            pt = eval_garment_point(y, phi, radial_offset=0.0115 + pillow)
            P[r, i] = pt
            UV[r, i] = (i / segs * 8.0, v)
            if is_gold_row:
                COL[r, i] = 0.94
            else:
                # Deep midnight ink-navy tint for high visual contrast against slate-indigo robe!
                sash_sh = 0.84 + 0.16 * math.sin(v * math.pi)
                COL[r, i] = [0.46 * sash_sh, 0.50 * sash_sh, 0.62 * sash_sh]
            W[r, i, bidx["mixamorig:Hips"]] = 0.58
            W[r, i, bidx["mixamorig:Spine"]] = 0.42

    fm_belt = np.zeros((R - 1, segs), dtype=np.int32)
    for r in range(R - 1):
        vm = 0.5 * (v_belt[r] + v_belt[r + 1])
        fm_belt[r, :] = MAT_GOLD if (vm < 0.145 or vm > 0.855) else MAT_TEAL
    mb.add_solidified_grid(P, UV, COL, W, fm_belt, thickness=0.0032, wrap_u=True, inner_shade=0.78)

    # 2. Ornate Beveled Front Ngọc Đới Clasp (burnished gold frame + inlaid celadon-jade cabochon)
    clasp_u = [-1.0, -0.82, -0.58, -0.28, 0.0, 0.28, 0.58, 0.82, 1.0]
    clasp_v = [-1.0, -0.78, -0.45, 0.0, 0.45, 0.78, 1.0]
    Pc = np.zeros((len(clasp_v), len(clasp_u), 3), dtype=np.float64)
    UVc = np.zeros((len(clasp_v), len(clasp_u), 2), dtype=np.float64)
    COLc = np.ones((len(clasp_v), len(clasp_u), 3), dtype=np.float64)
    Wc = np.zeros((len(clasp_v), len(clasp_u), mb.num_bones), dtype=np.float64)
    for r, vv in enumerate(clasp_v):
        y = y_c + vv * 0.032
        for i, uu in enumerate(clasp_u):
            phi = uu * 0.22
            in_jade = (abs(uu) < 0.62) and (abs(vv) < 0.60)
            bevel = 0.0048 * (1.0 - 0.35 * (uu ** 4 + vv ** 4)) + (0.0014 if in_jade else 0.0)
            Pc[r, i] = eval_garment_point(y, phi, radial_offset=0.0160 + max(0.0, bevel))
            UVc[r, i] = (0.5 * (uu + 1.0), 0.5 * (vv + 1.0))
            if in_jade:
                # Luminous celadon-jade stone tint over ivory silk base!
                jade_glow = 0.88 + 0.12 * (1.0 - uu * uu - vv * vv)
                COLc[r, i] = np.clip([0.34 * jade_glow, 0.72 * jade_glow, 0.66 * jade_glow], 0.0, 1.0)
            else:
                COLc[r, i] = 0.90 + 0.10 * max(0.0, bevel) / 0.0048
            Wc[r, i, bidx["mixamorig:Hips"]] = 0.58
            Wc[r, i, bidx["mixamorig:Spine"]] = 0.42
    fm_clasp = np.full((len(clasp_v) - 1, len(clasp_u) - 1), MAT_GOLD, dtype=np.int32)
    fm_clasp[1:-1, 2:-2] = MAT_IVORY  # Tinted celadon-jade via COLc!
    mb.add_solidified_grid(Pc, UVc, COLc, Wc, fm_clasp, thickness=0.0042, inner_shade=0.75)

    # 3. Twin Contrasting Moon-Ivory & Gold Silk Ribbons with Weighted Gold-Jade Tassel Tips
    #    (Distinct dual streamers cleanly separated at phi=-0.32 and phi=-0.10!)
    u_rib = [0.000, 0.160, 0.500, 0.840, 1.000]
    for b1_name, b2_name, phi_base, y_bot, r_off, sway_dir in [
        ("cloth_ribbon_r", "cloth_ribbon_r_02", -0.32, 0.40, 0.017, -1.0),
        ("cloth_ribbon_l", "cloth_ribbon_l_02", -0.10, 0.50, 0.022,  1.0),
    ]:
        rows = 15
        half_w = 0.0155
        y_top = y_c - 0.024
        Pr = np.zeros((rows + 1, len(u_rib), 3), dtype=np.float64)
        UVr = np.zeros((rows + 1, len(u_rib), 2), dtype=np.float64)
        COLr = np.ones((rows + 1, len(u_rib), 3), dtype=np.float64)
        Wr = np.zeros((rows + 1, len(u_rib), mb.num_bones), dtype=np.float64)
        rtang = np.array([math.cos(phi_base), 0.0, -math.sin(phi_base)], dtype=np.float64)
        rout = np.array([math.sin(phi_base), 0.0, math.cos(phi_base)], dtype=np.float64)

        for r in range(rows + 1):
            v = r / rows  # 0 at bottom tassel tip -> 1 at belt anchor
            t_down = 1.0 - v
            y = y_top * (1.0 - t_down) + y_bot * t_down
            # Gentle graceful parting drape so both ribbons read cleanly from front & 3/4 views
            sway_t = (0.012 * sway_dir * t_down + 0.005 * math.sin(1.8 * math.pi * t_down)) * t_down
            sway_out = 0.004 * math.sin(1.4 * math.pi * t_down) * t_down
            base_pt = eval_garment_point(y, phi_base, radial_offset=r_off) + rtang * sway_t + rout * sway_out
            w_scale = 1.0 + 0.28 * t_down

            for i, u in enumerate(u_rib):
                off = (u - 0.5) * 2.0 * half_w * w_scale
                # Slight 3D arch across ribbon width
                arch = 0.0014 * math.sin(math.pi * u)
                Pr[r, i] = base_pt + rtang * off + rout * arch
                UVr[r, i] = (u, t_down * 3.5)
                # Weighted gold/jade tassel cap at bottom 2 rows, warm shaded ivory along shaft
                shade = 0.84 + 0.14 * (1.0 - 0.5 * t_down)
                COLr[r, i] = shade
                w_h = 1.0 - _smooth(0.02, 0.18, t_down)
                w_c = 1.0 - w_h
                w2 = _smooth(0.32, 0.72, t_down)
                Wr[r, i, bidx["mixamorig:Hips"]] = w_h
                Wr[r, i, bidx[b1_name]] = w_c * (1.0 - w2)
                Wr[r, i, bidx[b2_name]] = w_c * w2

        fmr = np.zeros((rows, len(u_rib) - 1), dtype=np.int32)
        for r in range(rows):
            for i in range(len(u_rib) - 1):
                um = 0.5 * (u_rib[i] + u_rib[i + 1])
                is_gold = (um < 0.165) or (um > 0.835) or (r <= 1) or (r == 3)
                fmr[r, i] = MAT_GOLD if is_gold else MAT_IVORY
        # Subtle celadon-jade accent band at r=2 between gold tassel stripes
        COLr[2:4, 1:4] = np.array([0.36, 0.72, 0.66], dtype=np.float64)
        mb.add_solidified_grid(Pr, UVr, COLr, Wr, fmr, thickness=0.0020, inner_shade=0.84)


# ==============================================================================
# 8. PROCEDURAL EMBEDDED PNG SILK DAMASK & GOLD BROCADE TEXTURES (NO PILLOW)
# ==============================================================================

def _encode_png_rgb(rgb_u8: np.ndarray) -> bytes:
    """Encode an (H, W, 3) uint8 numpy array to a valid PNG byte stream using stdlib zlib."""
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


def _make_teal_damask_png(size: int = 256) -> bytes:
    """Deep Wuxia Ink-Indigo (Mặc Lam) woven silk jacquard calibrated for surface_lit.fs.

    Calibrated specifically against surface_lit.fs (sun + sky + teal ground bounce + cool rim
    + Blinn sheen) so the robe renders as rich aristocratic midnight-cerulean silk, never neon cyan.
    """
    u = np.linspace(0.0, 2.0 * math.pi, size, endpoint=False)
    U, V = np.meshgrid(u, u)
    # Fine diagonal silk twill micro-grain (breaks up flat sheen visually)
    twill = 0.5 + 0.5 * np.sin((U + 2.0 * V) * 48.0) * np.cos((U - V) * 32.0)
    slub = 0.5 + 0.5 * np.sin(V * 64.0 + 0.35 * np.sin(U * 12.0))
    # Delicate fine-scale Wuxia cloud-scroll jacquard (Vân Văn)
    wx = U + 0.32 * np.sin(4.0 * V) + 0.14 * np.cos(8.0 * U - 4.0 * V)
    wy = V + 0.32 * np.cos(4.0 * U) + 0.14 * np.sin(4.0 * U + 8.0 * V)
    cloud = np.sin(4.0 * wx) * np.cos(4.0 * wy) + 0.40 * np.cos(8.0 * wx - 4.0 * wy)
    motif = np.exp(-((cloud - 0.38) / 0.22) ** 2) * 0.78 + np.clip(cloud, 0.0, 1.0) * 0.22

    # Deep aristocratic slate-indigo / ink-teal silk
    base = np.array([0.082, 0.158, 0.252], dtype=np.float64)[None, None, :]
    hi = np.array([0.142, 0.242, 0.356], dtype=np.float64)[None, None, :]
    rgb = base * (1.0 - 0.72 * motif[:, :, None]) + hi * (0.72 * motif[:, :, None])
    rgb *= (0.935 + 0.075 * twill[:, :, None] + 0.040 * slub[:, :, None])
    return _encode_png_rgb(np.clip(rgb * 255.0, 0, 255).astype(np.uint8))


def _make_ivory_silk_png(size: int = 128) -> bytes:
    """Shaded warm moon-ivory silk (Nguyệt Bạch) with subtle slub grain — never blows out in HDR."""
    u = np.linspace(0.0, 2.0 * math.pi, size, endpoint=False)
    U, V = np.meshgrid(u, u)
    slub = 0.5 + 0.5 * (0.55 * np.sin(U * 36.0) + 0.45 * np.sin(V * 44.0 + 0.35 * np.sin(U * 10.0)))
    twill = 0.5 + 0.5 * np.sin((U + V) * 48.0)
    base = np.array([0.66, 0.635, 0.580], dtype=np.float64)[None, None, :]
    rgb = base * (0.945 + 0.055 * slub[:, :, None] + 0.030 * twill[:, :, None])
    return _encode_png_rgb(np.clip(rgb * 255.0, 0, 255).astype(np.uint8))


def _make_gold_brocade_png(size: int = 128) -> bytes:
    """Antique burnished imperial gold brocade with crisp traditional Hồi Văn meander fretwork."""
    y_i, x_i = np.mgrid[0:size, 0:size]
    cell = size // 8
    cx = (x_i % cell) / float(cell)
    cy = (y_i % cell) / float(cell)
    ring1 = ((cx < 0.14) | (cx > 0.86) | (cy < 0.14) | (cy > 0.86))
    ring2 = ((cx > 0.28) & (cx < 0.72) & (cy > 0.28) & (cy < 0.72) &
             ((cx < 0.40) | (cx > 0.60) | (cy < 0.40) | (cy > 0.60)))
    fret = (ring1 | ring2).astype(np.float64)
    dark_gold = np.array([0.38, 0.29, 0.14], dtype=np.float64)[None, None, :]
    bright_gold = np.array([0.74, 0.59, 0.31], dtype=np.float64)[None, None, :]
    rgb = dark_gold * (1.0 - fret[:, :, None]) + bright_gold * fret[:, :, None]
    return _encode_png_rgb(np.clip(rgb * 255.0, 0, 255).astype(np.uint8))


# ==============================================================================
# 9. BINARY GLB 2.0 EXPORTER & BUILT-IN SELF-VERIFICATION
# ==============================================================================

def export_glb(out_path: Path, bones: list, V, N, UV, COL, J4, W4, F, M):
    """Pack skeleton, 4 merged material primitives, and embedded PNG textures into GLB 2.0."""
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

    img_teal = add_bytes(_make_teal_damask_png(256))
    img_ivory = add_bytes(_make_ivory_silk_png(128))
    img_gold = add_bytes(_make_gold_brocade_png(128))
    images = [
        {"bufferView": img_teal,  "mimeType": "image/png", "name": "Indigo_Cloud_Damask"},
        {"bufferView": img_ivory, "mimeType": "image/png", "name": "Moon_Ivory_Silk"},
        {"bufferView": img_gold,  "mimeType": "image/png", "name": "Antique_Gold_Brocade"},
    ]
    samplers = [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}]
    textures = [{"sampler": 0, "source": i} for i in range(3)]

    materials = [
        {
            "name": "MAT_Teal_Cloud_Damask",
            "doubleSided": False,
            "pbrMetallicRoughness": {
                "baseColorTexture": {"index": 0},
                "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
                "metallicFactor": 0.0,
                "roughnessFactor": 0.56,
            },
        },
        {
            "name": "MAT_Ivory_Silk_Trim",
            "doubleSided": False,
            "pbrMetallicRoughness": {
                "baseColorTexture": {"index": 1},
                "baseColorFactor": [0.95, 0.94, 0.91, 1.0],
                "metallicFactor": 0.0,
                "roughnessFactor": 0.52,
            },
        },
        {
            "name": "MAT_Gold_Brocade_Accent",
            "doubleSided": False,
            "pbrMetallicRoughness": {
                "baseColorTexture": {"index": 2},
                "baseColorFactor": [1.0, 0.96, 0.86, 1.0],
                "metallicFactor": 0.62,
                "roughnessFactor": 0.30,
            },
        },
        {
            "name": "MAT_Inner_Trung_Y_Silk",
            "doubleSided": False,
            "pbrMetallicRoughness": {
                "baseColorTexture": {"index": 1},
                "baseColorFactor": [1.0, 0.99, 0.97, 1.0],
                "metallicFactor": 0.0,
                "roughnessFactor": 0.50,
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
    nodes.append({"name": "Wuxia_AoGiaoLinh_Mesh", "mesh": 0, "skin": 0})
    nodes.append({"name": "RefBodyArmature", "children": [nb, 0]})

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
            "generator": "Standalone Ao Giao Linh Engine Pipeline (40-Bone Mixamo + Cloth)",
        },
        "scene": 0,
        "scenes": [{"name": "Scene", "nodes": [nb + 1]}],
        "nodes": nodes,
        "skins": [{"name": "RefBodyArmature", "joints": list(range(nb)), "inverseBindMatrices": ibm_acc}],
        "materials": materials,
        "textures": textures,
        "images": images,
        "samplers": samplers,
        "meshes": [{"name": "OuterRobeMesh", "primitives": prims}],
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

    # Built-in self-verification
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
    print(f"Exported standalone Ao Giao Linh GLB -> {out_path}")
    print(f"  Bones     : {nb} (20 Mixamo T-Pose + {nb - 20} cloth_* spring bones)")
    print(f"  Geometry  : {total_v} verts | {total_t} tris | {len(prims)} material prims")
    print(f"  Bounds    : {np.round(V.min(0), 4).tolist()} .. {np.round(V.max(0), 4).tolist()}")
    print(f"  Quality   : IBM_err={max_ibm_err:.2e} | dot(fn,vn)_min={dot_min:.3f} | size={out_path.stat().st_size:,} B")
    print("==================================================================")


def build_engine_ready_giao_linh(out_path: Path):
    bones, bidx = build_skeleton()
    mb = MeshBuilder(num_bones=len(bones))

    _add_inner_trung_y(mb, bidx)
    _add_giao_linh_bodice(mb, bidx)
    _add_giao_linh_collar(mb, bidx)
    _add_all_skirt_panels(mb, bidx)
    _add_wide_sleeves(mb, bidx)
    _add_waist_sash_and_ribbons(mb, bidx)

    V, N, UV, COL, J4, W4, F, M = mb.finalize()
    export_glb(out_path, bones, V, N, UV, COL, J4, W4, F, M)


def main():
    here = Path(__file__).resolve().parent
    repo_root = here.parent
    parser = argparse.ArgumentParser(
        description="Standalone generator for engine-ready 40-bone Mixamo T-Pose Ao Giao Linh GLB (no Blender required)."
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=here / "ao_giao_linh.glb",
        help="Output GLB file path (default: tools/ao_giao_linh.glb)",
    )
    parser.add_argument(
        "--install",
        action="store_true",
        help="Also copy directly to assets/characters/equipment/torso/ao_giao_linh.glb",
    )
    args = parser.parse_args()

    out_path = args.out.resolve()
    build_engine_ready_giao_linh(out_path)
    if args.install:
        dst = repo_root / "assets/characters/equipment/torso/ao_giao_linh.glb"
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(out_path.read_bytes())
        print(f"Installed game-ready garment -> {dst}")


if __name__ == "__main__":
    main()
