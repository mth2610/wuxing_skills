"""Core conformal head sampler, normalized FaceLandmarks (male/female), MeshBuilder, and GLB 2.0 exporter.

Pure Python + NumPy (no Blender, no Pillow).
Every grooming part (hair, eyebrows+eyelashes, beard) positions geometry in normalized
head-relative coordinates derived from the target body GLB, then snaps onto the actual
head/face surface via HeadSurfaceSampler — so switching from a male body to a female body
only requires passing --body <female.glb> --profile female.
"""

from dataclasses import dataclass
import json
import math
import struct
import zlib
from pathlib import Path
import numpy as np


# ==============================================================================
# 1. STANDARD MIXAMO 20-BONE T-POSE SKELETON & MATERIAL IDS
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

MAT_HAIR = 0     # Deep ink-charcoal (Huyền Mặc) fine hair-strand silk
MAT_GOLD = 1     # Antique burnished imperial gold coronet & hairpin caps
MAT_JADE = 2     # Luminous celadon-jade hairpin shaft & crown inlay
MAT_STUBBLE = 3  # Soft alpha-blended 3-day stubble / freshly-shaved beard overlay


# ==============================================================================
# 2. LINEAR ALGEBRA HELPERS
# ==============================================================================

def smoothstep(a, b, x):
    t = np.clip((x - a) / np.maximum(b - a, 1e-12), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def quat_to_mat(q) -> np.ndarray:
    x, y, z, w = q
    n = math.sqrt(x * x + y * y + z * z + w * w)
    if n > 1e-12:
        x, y, z, w = x / n, y / n, z / n, w / n
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w)],
        [2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y)],
    ], dtype=np.float64)


def mat_to_quat(R: np.ndarray) -> list:
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
    n = math.sqrt(x * x + y * y + z * z + w * w)
    if n > 1e-12:
        x, y, z, w = x / n, y / n, z / n, w / n
    return [float(x), float(y), float(z), float(w)]


def make_bone_basis(y_dir, hint_z=(0.0, 0.0, 1.0)) -> np.ndarray:
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


def head_weights(num_bones: int, bidx: dict, rows: int) -> np.ndarray:
    W = np.zeros((rows, num_bones), dtype=np.float64)
    W[:, bidx["mixamorig:Head"]] = 1.0
    return W


# ==============================================================================
# 3. NORMALIZED FACE PROFILES & AUTO-EXTRACTED HEAD LANDMARKS (MALE / FEMALE)
# ==============================================================================

@dataclass(frozen=True)
class FaceProfile:
    """Gender/archetype facial proportion ratios relative to measured skull height & half-width."""
    name: str
    # Vertical fractions in [0=chin_y .. 1=skull_top_y]
    brow_y_frac: float
    eye_upper_y_frac: float
    eye_lower_y_frac: float
    lip_upper_y_frac: float
    chin_top_y_frac: float
    chin_bot_y_frac: float
    cheek_y_frac: float
    # Horizontal fractions of skull_half_w
    brow_x0_frac: float
    brow_x1_frac: float
    eye_x0_frac: float
    eye_x1_frac: float
    # Style & aesthetic scaling
    brow_width_scale: float
    brow_arch_scale: float
    lash_length_scale: float
    crown_loft_scale: float
    hairline_front_elev: float


FACE_PROFILES = {
    # Calibrated directly against assets/characters/bodies/human_body.glb
    # (chin_y=1.560, skull_top_y=1.792 -> skull_h=0.232, skull_half_w=0.075)
    "male": FaceProfile(
        name="male",
        brow_y_frac=0.594,       # Y ~ 1.6978
        eye_upper_y_frac=0.532,  # Y ~ 1.6834
        eye_lower_y_frac=0.506,  # Y ~ 1.6774
        lip_upper_y_frac=0.265,  # Y ~ 1.6215
        chin_top_y_frac=0.075,   # Y ~ 1.5775
        chin_bot_y_frac=0.017,   # Y ~ 1.5640
        cheek_y_frac=0.366,      # Y ~ 1.6450
        brow_x0_frac=0.153,      # X ~ 0.0115
        brow_x1_frac=0.680,      # X ~ 0.0510
        eye_x0_frac=0.247,       # X ~ 0.0185
        eye_x1_frac=0.587,       # X ~ 0.0440
        brow_width_scale=1.00,
        brow_arch_scale=1.00,
        lash_length_scale=1.00,
        crown_loft_scale=1.00,
        hairline_front_elev=0.57,
    ),
    # Feminine Wuxia proportions (softer higher arch, longer lashes, fuller airy crown)
    "female": FaceProfile(
        name="female",
        brow_y_frac=0.602,
        eye_upper_y_frac=0.534,
        eye_lower_y_frac=0.504,
        lip_upper_y_frac=0.268,
        chin_top_y_frac=0.078,
        chin_bot_y_frac=0.020,
        cheek_y_frac=0.372,
        brow_x0_frac=0.148,
        brow_x1_frac=0.695,
        eye_x0_frac=0.240,
        eye_x1_frac=0.600,
        brow_width_scale=0.66,
        brow_arch_scale=1.35,
        lash_length_scale=1.45,
        crown_loft_scale=1.10,
        hairline_front_elev=0.54,
    ),
}


@dataclass
class FaceLandmarks:
    """Measured anatomical anchor frame extracted from any Mixamo-compatible body GLB."""
    head_bone_pos: np.ndarray
    center: np.ndarray
    chin_y: float
    skull_top_y: float
    skull_h: float
    skull_half_w: float
    forehead_z: float
    back_z: float
    scale: float  # skull_h / 0.232 reference ratio

    def y(self, frac: float) -> float:
        return self.chin_y + frac * self.skull_h

    def x(self, frac: float) -> float:
        return frac * self.skull_half_w

    def m(self, meters_at_ref: float) -> float:
        """Scale a reference-head metric (thickness, offset, radius) proportionally to this head."""
        return meters_at_ref * self.scale


class HeadSurfaceSampler:
    """Sub-millimeter conformal surface sampler & automatic landmark detector for any body GLB."""

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

        # Extract head/neck bone world positions from the body GLB if present
        head_bone_pos = np.array([0.0, 1.660, 0.0], dtype=np.float64)
        neck_bone_y = 1.520
        nodes = gltf.get("nodes", [])
        if nodes:
            parent_of = {}
            for ni, nd in enumerate(nodes):
                for ch in nd.get("children", []):
                    parent_of[ch] = ni
            world_cache = {}

            def get_world(ni):
                if ni in world_cache:
                    return world_cache[ni]
                nd = nodes[ni]
                loc = np.eye(4, dtype=np.float64)
                if "rotation" in nd:
                    loc[:3, :3] = quat_to_mat(nd["rotation"])
                if "scale" in nd:
                    loc[:3, :3] *= np.asarray(nd["scale"], dtype=np.float64)[None, :]
                if "translation" in nd:
                    loc[:3, 3] = nd["translation"]
                w = get_world(parent_of[ni]) @ loc if ni in parent_of else loc
                world_cache[ni] = w
                return w

            for ni, nd in enumerate(nodes):
                nm = nd.get("name", "")
                if nm == "mixamorig:Head":
                    head_bone_pos = get_world(ni)[:3, 3].copy()
                elif nm == "mixamorig:Neck":
                    neck_bone_y = float(get_world(ni)[1, 3])

        p0 = gltf["meshes"][0]["primitives"][0]
        pos = racc(p0["attributes"]["POSITION"])
        nrm = racc(p0["attributes"]["NORMAL"])
        idx_raw = racc(p0["indices"]).astype(np.int32).reshape(-1, 3)
        self.full_pos = pos
        self.full_nrm = nrm
        self.full_tris = idx_raw

        y_cut = max(neck_bone_y + 0.015, float(head_bone_pos[1]) - 0.125)
        m = pos[:, 1] >= y_cut
        self.pos = pos[m]
        self.nrm = nrm[m]

        skull_top_y = float(self.pos[:, 1].max())
        # Chin lies near the front-center bottom of the head mesh (|x| < 0.035, z > center_z)
        front_low = self.pos[(np.abs(self.pos[:, 0]) < 0.035) & (self.pos[:, 2] > 0.075)]
        chin_y = float(front_low[:, 1].min()) if len(front_low) > 0 else float(head_bone_pos[1] - 0.100)
        skull_h = max(0.16, skull_top_y - chin_y)

        upper = self.pos[self.pos[:, 1] > chin_y + 0.50 * skull_h]
        skull_half_w = float(np.percentile(np.abs(upper[:, 0]), 97)) if len(upper) > 0 else 0.075
        forehead_z = float(np.percentile(upper[:, 2], 99)) if len(upper) > 0 else 0.128
        back_z = float(upper[:, 2].min()) if len(upper) > 0 else -0.079

        self.center = np.array([
            0.0,
            chin_y + 0.500 * skull_h,
            0.5 * (forehead_z + back_z),
        ], dtype=np.float64)

        self.landmarks = FaceLandmarks(
            head_bone_pos=head_bone_pos,
            center=self.center.copy(),
            chin_y=chin_y,
            skull_top_y=skull_top_y,
            skull_h=skull_h,
            skull_half_w=skull_half_w,
            forehead_z=forehead_z,
            back_z=back_z,
            scale=skull_h / 0.232,
        )

        mf = (self.pos[:, 2] > self.center[2] + 0.018) & (self.nrm[:, 2] > -0.15)
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
        z_mean = float(np.dot(w, self.f_pos[idx, 2]))
        z_max = float(self.f_pos[idx[:5], 2].max())
        z = 0.35 * z_mean + 0.65 * z_max
        n = (w[:, None] * self.f_nrm[idx]).sum(axis=0)
        n /= max(float(np.linalg.norm(n)), 1e-12)
        return np.array([x, y, z], dtype=np.float64) + n * z_offset, n

    def extract_lower_face_mesh(self, offset: float = 0.00075):
        """Extract the exact lower-face / jaw / chin / upper-lip triangles from the body mesh,
        pushed outward by `offset` meters along body normals for 100% gap-free stubble overlays.
        """
        lm = self.landmarks
        V = self.full_pos
        N = self.full_nrm
        F = self.full_tris
        # Keep triangles in the lower-face / jaw / cheek / upper-lip zone
        y_hi = lm.y(0.56)
        y_lo = lm.chin_y - lm.m(0.025)
        z_lo = lm.center[2] - lm.m(0.015)
        vc = V[F].mean(axis=1)
        fn = np.cross(V[F[:, 1]] - V[F[:, 0]], V[F[:, 2]] - V[F[:, 0]])
        fn /= np.maximum(np.linalg.norm(fn, axis=1, keepdims=True), 1e-12)
        out_dir = vc - self.center[None, :]
        out_dir /= np.maximum(np.linalg.norm(out_dir, axis=1, keepdims=True), 1e-12)
        outward = (fn * out_dir).sum(axis=1) > 0.25
        mask = (vc[:, 1] <= y_hi) & (vc[:, 1] >= y_lo) & (vc[:, 2] >= z_lo) & outward
        sel_f = F[mask]
        V_off = V + N * offset
        fn_off = np.cross(V_off[sel_f[:, 1]] - V_off[sel_f[:, 0]], V_off[sel_f[:, 2]] - V_off[sel_f[:, 0]])
        fn_off /= np.maximum(np.linalg.norm(fn_off, axis=1, keepdims=True), 1e-12)
        sel_f = sel_f[(fn_off * fn[mask]).sum(axis=1) > 0.40]
        uniq, inv = np.unique(sel_f.ravel(), return_inverse=True)
        out_v = V_off[uniq]
        out_n = N[uniq]
        out_f = inv.reshape(-1, 3)
        return out_v, out_n, out_f

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
# 4. SKELETON BUILDER (20 MIXAMO + OPTIONAL cloth_hair_* SPRING BONES)
# ==============================================================================

def build_skeleton(lm: FaceLandmarks, include_hair_springs: bool = False):
    """Build 20-bone Mixamo skeleton (+ 8 cloth_hair_* spring bones when building hair)."""
    bones = []
    name_to_idx = {}

    for name, par, tr, rot in GAME_MIXAMO_20:
        loc = np.eye(4, dtype=np.float64)
        loc[:3, :3] = quat_to_mat(rot)
        loc[:3, 3] = tr
        world = bones[par]["world"] @ loc if par >= 0 else loc
        name_to_idx[name] = len(bones)
        bones.append(dict(name=name, parent=par, tr=list(tr), rot=list(rot), world=world))

    if include_hair_springs:
        def add_cloth_bone(name: str, parent_name: str, head_world, tail_world, hint_z=(0.0, 0.0, 1.0)):
            par = name_to_idx[parent_name]
            head_w = np.asarray(head_world, dtype=np.float64)
            tail_w = np.asarray(tail_world, dtype=np.float64)
            R_w = make_bone_basis(tail_w - head_w, hint_z=hint_z)
            M_w = np.eye(4, dtype=np.float64)
            M_w[:3, :3] = R_w
            M_w[:3, 3] = head_w
            M_loc = np.linalg.inv(bones[par]["world"]) @ M_w
            tr = M_loc[:3, 3].tolist()
            rot = mat_to_quat(M_loc[:3, :3])
            name_to_idx[name] = len(bones)
            bones.append(dict(name=name, parent=par, tr=tr, rot=rot, world=M_w))

        hy = float(lm.head_bone_pos[1])
        s = lm.scale
        for b1, b2, x0, z0, z1, z2 in [
            ("cloth_hair_c_01", "cloth_hair_c_02",  0.000, -0.092, -0.145, -0.168),
            ("cloth_hair_l_01", "cloth_hair_l_02",  0.058, -0.080, -0.128, -0.152),
            ("cloth_hair_r_01", "cloth_hair_r_02", -0.058, -0.080, -0.128, -0.152),
        ]:
            p0 = [x0 * s, hy, z0 * s]
            p1 = [x0 * 1.18 * s, hy - 0.165 * s, z1 * s]
            p2 = [x0 * 1.32 * s, hy - 0.350 * s, z2 * s]
            add_cloth_bone(b1, "mixamorig:Head", p0, p1)
            add_cloth_bone(b2, b1, p1, p2)

        for tag, sgn in [("l", 1.0), ("r", -1.0)]:
            add_cloth_bone(
                f"cloth_hair_bang_{tag}", "mixamorig:Head",
                [sgn * 0.068 * s, hy + 0.035 * s, 0.062 * s],
                [sgn * 0.078 * s, hy - 0.100 * s, 0.058 * s],
            )

    return bones, name_to_idx


# ==============================================================================
# 5. PURE-NUMPY MESH BUILDER (SOLID SHELLS & OUTWARD-NORMAL TAPERED RIBBONS)
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

    def add_indexed_mesh(self, V, UV, COL, W, F, mat_id: int):
        """Add an arbitrary pre-indexed triangle mesh directly into the builder."""
        base = self._vcount
        self.pos.append(np.asarray(V, dtype=np.float64))
        self.uv.append(np.asarray(UV, dtype=np.float64))
        self.col.append(np.asarray(COL, dtype=np.float64))
        self.w.append(np.asarray(W, dtype=np.float64))
        self._vcount += len(V)
        F_arr = np.asarray(F, dtype=np.int32) + base
        self.tris.append(F_arr)
        self.mat.append(np.full(len(F_arr), mat_id, dtype=np.int32))

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


# ==============================================================================
# 6. PROCEDURAL PNG TEXTURES & BINARY GLB 2.0 EXPORTER (PRUNES UNUSED MATERIALS)
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


def _encode_png_rgba(rgba_u8: np.ndarray) -> bytes:
    h, w, _ = rgba_u8.shape
    raw = b"".join(b"\x00" + rgba_u8[y].tobytes() for y in range(h))

    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I4s", len(data), tag) + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 6))
        + chunk(b"IEND", b"")
    )


def _make_stubble_rgba_png(size: int = 512) -> bytes:
    """512x512 procedural full short-cropped / heavy-stubble beard RGBA texture.

    UV layout (exact alignment with human_body.glb lower face: y_lo=chin-18mm .. y_hi=y(0.54)):
      u in [0..1] -> azimuth across lower face (0.5 = chin/lip center, 0/1 = right/left ear)
      v in [0..1] -> vertical (0 = under-chin/jaw, ~0.465 = mouth center, ~0.585 = upper lip, 1 = sideburn top)
    """
    rng = np.random.default_rng(42)
    ys = np.linspace(0.0, 1.0, size)
    xs = np.linspace(0.0, 1.0, size)
    U, V = np.meshgrid(xs, ys)
    au = np.abs(2.0 * U - 1.0)

    # 1. Full Beard Silhouette (bold, rich coverage across chin, jaw, cheeks, sideburns & mustache)
    cheek_top = 0.58 + 0.36 * (np.clip((au - 0.30) / 0.62, 0.0, 1.0) ** 1.15)
    cheek_top = cheek_top - 0.04 * np.exp(-(((au - 0.44) / 0.22) ** 2))
    jaw_bot = 0.02 + 0.22 * (au ** 1.85)

    m_top = 1.0 - smoothstep(cheek_top - 0.09, cheek_top + 0.04, V)
    m_bot = smoothstep(jaw_bot - 0.04, jaw_bot + 0.07, V)
    m_side = 1.0 - smoothstep(0.88, 0.99, au)
    base_mask = m_top * m_bot * m_side

    # Exact lip cutout centered on human_body.glb mouth (V ~ 0.465, radius 0.078)
    lip_d = np.sqrt(((au / 0.185) ** 2) + (((V - 0.465) / 0.076) ** 2))
    lip_clear = smoothstep(0.76, 1.12, lip_d)

    # Full mustache band sitting right above upper lip (V ~ 0.585) and seamlessly joining cheek beard!
    stache_d = np.sqrt(((au / 0.30) ** 2) + (((V - 0.585) / 0.062) ** 2))
    stache_mask = 1.0 - smoothstep(0.60, 1.12, stache_d)
    # Mouth-corner vertical connectors joining mustache cleanly into full jaw beard
    corner_d = np.sqrt((((au - 0.22) / 0.11) ** 2) + (((V - 0.48) / 0.14) ** 2))
    corner_mask = 1.0 - smoothstep(0.55, 1.10, corner_d)

    mask = np.clip(base_mask + stache_mask + corner_mask * 0.95, 0.0, 1.0) * lip_clear

    # 2. Rich, dense short-cropped beard / heavy stubble texture (clearly visible in-engine!)
    fine = rng.random((size, size))
    med = 0.25 * (fine + np.roll(fine, 1, 0) + np.roll(fine, 1, 1) + np.roll(np.roll(fine, 1, 0), 1, 1))
    # Short vertical hair-grain micro-streaks (2-3 px vertical correlation)
    streak = 0.5 * fine + 0.3 * np.roll(fine, 1, 0) + 0.2 * np.roll(fine, -1, 0)
    follicles = (streak > 0.44).astype(np.float64) * 0.65 + (med > 0.48).astype(np.float64) * 0.35

    density = np.clip(0.56 + 0.44 * follicles, 0.0, 1.0)
    zone_boost = 0.92 + 0.14 * np.exp(-((au / 0.45) ** 2)) * (1.0 - 0.25 * V)
    alpha = np.clip(mask * density * zone_boost * 0.90, 0.0, 0.92)

    # Deep natural ink-charcoal beard hair color with subtle follicle variation
    rgb = np.zeros((size, size, 3), dtype=np.float64)
    rgb[:, :, 0] = 0.032 + 0.024 * (1.0 - follicles)
    rgb[:, :, 1] = 0.028 + 0.020 * (1.0 - follicles)
    rgb[:, :, 2] = 0.028 + 0.018 * (1.0 - follicles)

    rgba = np.concatenate([rgb, alpha[:, :, None]], axis=2)
    return _encode_png_rgba(np.clip(rgba * 255.0, 0, 255).astype(np.uint8))


_MAT_DEFS = {
    MAT_HAIR:    ("Wuxia_Ink_Hair", _make_hair_strand_png, 256, "MAT_Wuxia_Ink_Hair", [1.0, 1.0, 1.0, 1.0], 0.0, 0.54, "OPAQUE"),
    MAT_GOLD:    ("Antique_Gold_Crown", _make_gold_png, 128, "MAT_Gold_Hair_Crown", [1.0, 0.96, 0.86, 1.0], 0.65, 0.28, "OPAQUE"),
    MAT_JADE:    ("Celadon_Jade_Pin", _make_jade_png, 64, "MAT_Jade_Hairpin", [1.0, 1.0, 1.0, 1.0], 0.05, 0.24, "OPAQUE"),
    MAT_STUBBLE: ("Wuxia_Stubble_RGBA", _make_stubble_rgba_png, 512, "MAT_Wuxia_Stubble", [1.0, 1.0, 1.0, 1.0], 0.0, 0.88, "BLEND"),
}


def export_glb(out_path: Path, bones: list, V, N, UV, COL, J4, W4, F, M, mesh_name: str = "WuxiaGrooming"):
    """Export standalone binary GLB 2.0 with self-verification (topology, IBM, normals, doubleSided=False)."""
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

    used_mats = [mi for mi in sorted(_MAT_DEFS.keys()) if np.any(M == mi)]
    images, textures, materials = [], [], []
    mat_remap = {}
    for new_idx, mi in enumerate(used_mats):
        img_name, fn, sz, mat_name, bcf, metal, rough, alpha_mode = _MAT_DEFS[mi]
        bv_img = add_bytes(fn(sz))
        images.append({"bufferView": bv_img, "mimeType": "image/png", "name": img_name})
        textures.append({"sampler": 0, "source": new_idx})
        mat_def = {
            "name": mat_name,
            "doubleSided": False,
            "pbrMetallicRoughness": {
                "baseColorTexture": {"index": new_idx},
                "baseColorFactor": bcf,
                "metallicFactor": metal,
                "roughnessFactor": rough,
            },
        }
        if alpha_mode != "OPAQUE":
            mat_def["alphaMode"] = alpha_mode
        materials.append(mat_def)
        mat_remap[mi] = new_idx
    samplers = [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}]

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
    nodes.append({"name": f"{mesh_name}_Mesh", "mesh": 0, "skin": 0})
    nodes.append({"name": f"{mesh_name}_Armature", "children": [nb, 0]})

    prims = []
    total_v = 0
    total_t = 0
    col_rgba = np.column_stack([
        np.round(np.clip(COL, 0.0, 1.0) * 255.0).astype(np.uint8),
        np.full((len(COL), 1), 255, dtype=np.uint8),
    ])

    for mi in used_mats:
        faces = F[M == mi]
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
            "material": mat_remap[mi],
            "mode": 4,
        })
        total_v += len(ids)
        total_t += len(faces)

    doc = {
        "asset": {"version": "2.0", "generator": f"Wuxing Modular Grooming ({mesh_name})"},
        "scene": 0,
        "scenes": [{"name": "Scene", "nodes": [nb + 1]}],
        "nodes": nodes,
        "skins": [{"name": f"{mesh_name}_Armature", "joints": list(range(nb)), "inverseBindMatrices": ibm_acc}],
        "materials": materials,
        "textures": textures,
        "images": images,
        "samplers": samplers,
        "meshes": [{"name": mesh_name, "primitives": prims}],
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
        loc[:3, :3] = quat_to_mat(b["rot"])
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

    print(
        f"[{mesh_name}] -> {out_path.name} | bones={nb} | verts={total_v} | tris={total_t} | "
        f"prims={len(prims)} | IBM_err={max_ibm_err:.2e} | dot_min={dot_min:+.3f} | {out_path.stat().st_size:,} B"
    )
