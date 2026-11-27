#!/usr/bin/env python3
"""
female_body_v1.py — Standalone Game-Ready Wuxia Heroine (Female Body) Generator

Builds a distinctively feminine, graceful East-Asian Wuxia heroine (Nữ Hiệp / Tiên Tử)
on the exact 20-bone Mixamo T-Pose skeleton (GAME_MIXAMO_20), complete with tasteful
Wuxia modesty underwear (silk bandeau chest wrap `yếm lụa` + matching silk shorts):
  - Unmistakably feminine face: +16% larger luminous eyes with opened almond eyelids,
    shorter delicate heart-shaped face (-8% lower-face vertical length), petite V-line
    jaw & chin, smooth brow ridge & soft rounded forehead, dainty nose, soft coral-rose
    Cupid's-bow lips, warm glowing golden-porcelain skin.
  - Unmistakably feminine silhouette: narrow sloping shoulders (-15%), slender swan neck
    (-18%), cinched hourglass waist (-22%), sculpted anatomical teardrop bust, flared
    rounded hips (+13%) & lifted glutes, slender arms (-18%), petite hands (-16%) and
    dainty feet (-16%).
  - Always installs the clothed-underwear version to assets/characters/bodies/female_body.glb.

RUN:
    python3 tools/female_body/female_body_v1.py --install
"""

import argparse
import copy
import json
import shutil
import struct
import sys
import tempfile
from pathlib import Path
import numpy as np

_TOOLS_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(_TOOLS_DIR / "male_body"))
import male_body_v6 as M  # noqa: E402


def _add_silk_bandeau_to_glb(glb_path: Path):
    """Add a tailored, smooth-hemmed conformal silk bandeau chest-wrap (`yếm lụa`) onto the female GLB.

    Combines ALL skin primitives (main body + areola disks) in the chest band and performs exact
    per-triangle polygon clipping along smooth top/bottom hem curves (zero holes, zero jagged edges!).
    """
    g = M.GLB(glb_path)
    d = copy.deepcopy(g.d)
    data = bytearray(g.b)

    # Combine all material-0 skin primitives (main torso + areolas so zero gaps ever appear!)
    V_list, N_list, UV_list, J_list, W_list, F_list = [], [], [], [], [], []
    v_off = 0
    for p in d["meshes"][0]["primitives"]:
        if p["material"] != 0:
            continue
        at = p["attributes"]
        Vp = g.read(at["POSITION"]).astype(np.float64)
        Np = g.read(at["NORMAL"]).astype(np.float64)
        UVp = g.read(at["TEXCOORD_0"]).astype(np.float64)
        Jp = g.read(at["JOINTS_0"]).astype(np.float64)
        Wp = g.read(at["WEIGHTS_0"]).astype(np.float64)
        Fp = g.read(p["indices"]).astype(np.int32).reshape(-1, 3) + v_off
        V_list.append(Vp); N_list.append(Np); UV_list.append(UVp)
        J_list.append(Jp); W_list.append(Wp); F_list.append(Fp)
        v_off += len(Vp)

    V = np.concatenate(V_list, axis=0)
    N = np.concatenate(N_list, axis=0)
    UV = np.concatenate(UV_list, axis=0)
    J4 = np.concatenate(J_list, axis=0)
    W4 = np.concatenate(W_list, axis=0)
    F = np.concatenate(F_list, axis=0)

    # Identify arm/shoulder joints so bandeau NEVER pulls into armpit wings in T-pose or sword animations!
    skin_joints = d["skins"][0]["joints"]
    joint_names = [d["nodes"][jn].get("name", "") for jn in skin_joints]
    arm_joint_ids = {
        idx for idx, nm in enumerate(joint_names)
        if any(k in nm.lower() for k in ("arm", "shoulder", "clav", "hand", "finger"))
    }
    chest_joint_id = next(
        (idx for idx, nm in enumerate(joint_names) if "spine2" in nm.lower() or "chest" in nm.lower()),
        next((idx for idx, nm in enumerate(joint_names) if "spine" in nm.lower()), 0),
    )

    # Smooth curved top rim (gentle sweetheart dip at sternum front, dips cleanly below armpits at sides)
    ax_v = np.abs(V[:, 0])
    front_w = M._smooth(-0.01, 0.04, V[:, 2])
    armpit_dip = 0.026 * M._smooth(0.095, 0.148, ax_v)
    top_curve = 1.260 - 0.012 * np.exp(-((V[:, 0] / 0.048) ** 2)) - armpit_dip
    y_top_v = (1.230 - 0.55 * armpit_dip) * (1.0 - front_w) + top_curve * front_w
    y_bot_v = 1.132

    # Compute per-vertex arm weight so armpit/deltoid skin triangles are excluded cleanly
    arm_w = np.zeros(len(V), dtype=np.float64)
    for k in range(4):
        is_arm = np.isin(np.round(J4[:, k]).astype(np.int32), list(arm_joint_ids))
        arm_w += np.where(is_arm, W4[:, k], 0.0)

    # Pack per-vertex attributes + signed distances to bottom (d_lo = y - y_bot) and top (d_hi = y_top - y)
    # Columns: 0..2 V, 3..5 N, 6..7 UV, 8..11 J4, 12..15 W4, 16 d_lo, 17 d_hi
    A = np.column_stack([V, N, UV, J4, W4, V[:, 1] - y_bot_v, y_top_v - V[:, 1]])
    in_x = (np.abs(V[F, 0]).max(axis=1) <= 0.155) & (arm_w[F].max(axis=1) < 0.35)
    in_y = (A[F, 16].max(axis=1) > 0.0) & (A[F, 17].max(axis=1) > 0.0)
    chosen = F[in_x & in_y]

    def clip_poly(poly, col_idx):
        res = []
        for i, p_a in enumerate(poly):
            p_b = poly[(i + 1) % len(poly)]
            da, db = p_a[col_idx], p_b[col_idx]
            if da >= 0.0:
                res.append(p_a)
            if (da >= 0.0) != (db >= 0.0):
                t = da / (da - db)
                pt = p_a + (p_b - p_a) * t
                # Keep discrete bone indices from dominant endpoint
                pt[8:12] = p_a[8:12] if t < 0.5 else p_b[8:12]
                res.append(pt)
        return res

    arr_rows = []
    faces = []
    for tri in chosen:
        poly = clip_poly([A[tri[0]], A[tri[1]], A[tri[2]]], 16)
        if len(poly) < 3:
            continue
        poly = clip_poly(poly, 17)
        if len(poly) < 3:
            continue
        j0 = len(arr_rows)
        arr_rows.extend(poly)
        for k in range(1, len(poly) - 1):
            faces.append((j0, j0 + k, j0 + k + 1))

    arr = np.asarray(arr_rows, dtype=np.float64)
    Fb = np.asarray(faces, dtype=np.int32)
    _, ii, iv = np.unique(np.round(arr[:, :3], 6), axis=0, return_index=True, return_inverse=True)
    arr = arr[ii]
    Fb = iv[Fb]
    Fb = Fb[(Fb[:, 0] != Fb[:, 1]) & (Fb[:, 1] != Fb[:, 2]) & (Fb[:, 2] != Fb[:, 0])]

    Vb = arr[:, 0:3]
    Nb = arr[:, 3:6]
    Nb /= np.maximum(np.linalg.norm(Nb, axis=1, keepdims=True), 1e-12)
    UVb = arr[:, 6:8]
    Jb = np.round(arr[:, 8:12]).astype(np.uint8)
    Wb = np.maximum(arr[:, 12:16], 0.0)
    # Strip any residual arm/shoulder weights so bandeau hugs ribcage cleanly in T-pose & raised-arm animations
    if arm_joint_ids:
        for k in range(4):
            Wb[np.isin(Jb[:, k], list(arm_joint_ids)), k] = 0.0
        zero_row = Wb.sum(axis=1) < 1e-6
        if zero_row.any():
            Jb[zero_row, 0] = chest_joint_id
            Wb[zero_row, 0] = 1.0
    Wb /= np.maximum(Wb.sum(axis=1, keepdims=True), 1e-12)
    Wb = Wb.astype(np.float32)

    # Ease offset + delicate upper/lower silk hem ridge + soft cleavage bridging
    vy = Vb[:, 1]
    vx = np.abs(Vb[:, 0])
    vz = Vb[:, 2]
    hem_top = (1.0 - M._smooth(0.0, 0.014, arr[:, 17])) * 0.0014
    hem_bot = (1.0 - M._smooth(0.0, 0.016, arr[:, 16])) * 0.0018
    ease = 0.0054 + hem_top + hem_bot
    Vb_out = Vb + Nb * ease[:, None]

    cleav = M._ga(vx, 0.0, 0.028) * M._ga(vy, 1.200, 0.048) * M._smooth(0.03, 0.08, vz)
    Vb_out[:, 2] += 0.0075 * cleav

    f0, f1, f2 = Fb[:, 0], Fb[:, 1], Fb[:, 2]
    cross = np.cross(Vb_out[f1] - Vb_out[f0], Vb_out[f2] - Vb_out[f0])
    Nb_out = np.zeros_like(Vb_out)
    np.add.at(Nb_out, f0, cross)
    np.add.at(Nb_out, f1, cross)
    np.add.at(Nb_out, f2, cross)
    Nb_out /= np.maximum(np.linalg.norm(Nb_out, axis=1, keepdims=True), 1e-12)

    def add_acc(arr_in, typ, ct, target, bounds=False):
        dt = {5121: np.uint8, 5123: "<u2", 5125: "<u4", 5126: "<f4"}[ct]
        a = np.ascontiguousarray(arr_in, dtype=dt)
        data.extend(b"\0" * ((-len(data)) % 4))
        off = len(data)
        data.extend(a.tobytes())
        d["bufferViews"].append({"buffer": 0, "byteOffset": off, "byteLength": a.nbytes, "target": target})
        ac = {"bufferView": len(d["bufferViews"]) - 1, "componentType": ct, "count": len(a), "type": typ}
        if bounds:
            ac["min"] = a.min(0).tolist()
            ac["max"] = a.max(0).tolist()
        d["accessors"].append(ac)
        return len(d["accessors"]) - 1

    idx_ct = 5123 if len(Vb_out) < 65536 else 5125
    d["meshes"][0]["primitives"].append({
        "attributes": {
            "POSITION":   add_acc(Vb_out, "VEC3", 5126, 34962, bounds=True),
            "NORMAL":     add_acc(Nb_out, "VEC3", 5126, 34962),
            "TEXCOORD_0": add_acc(UVb, "VEC2", 5126, 34962),
            "JOINTS_0":   add_acc(Jb, "VEC4", 5121, 34962),
            "WEIGHTS_0":  add_acc(Wb, "VEC4", 5126, 34962),
        },
        "indices": add_acc(Fb.ravel(), "SCALAR", idx_ct, 34963),
        "material": 4,
        "mode": 4,
    })

    if len(d["materials"]) > 4:
        d["materials"][4]["name"] = "Wuxia silk undergarments (Yếm & Quần lụa)"
        d["materials"][4]["pbrMetallicRoughness"]["baseColorFactor"] = [0.16, 0.14, 0.22, 1.0]
        d["materials"][4]["pbrMetallicRoughness"]["roughnessFactor"] = 0.68

    d["buffers"][0]["byteLength"] = len(data)
    js = json.dumps(d, separators=(",", ":")).encode("utf-8")
    js += b" " * ((-len(js)) % 4)
    data.extend(b"\0" * ((-len(data)) % 4))
    glb_path.write_bytes(
        struct.pack("<4sII", b"glTF", 2, 28 + len(js) + len(data))
        + struct.pack("<I4s", len(js), b"JSON") + js
        + struct.pack("<I4s", len(data), b"BIN\0") + data
    )


def _scale_eyeballs_and_irises(glb_path: Path, iris_boost: float = 1.07):
    """Subtly enlarge dark iris & pupil along the cornea sphere WITHOUT pushing forward in +Z."""
    g = M.GLB(glb_path)
    d = copy.deepcopy(g.d)
    data = bytearray(g.b)

    iris_v = np.concatenate([
        g.read(p["attributes"]["POSITION"]).astype(np.float64)
        for p in d["meshes"][0]["primitives"] if p["material"] == 5
    ], axis=0)
    c_l = iris_v[iris_v[:, 0] > 0].mean(axis=0)
    c_r = iris_v[iris_v[:, 0] < 0].mean(axis=0)
    R_eye = 0.0145  # True eyeball radius in Master40 space

    def add_pos(arr):
        a = np.ascontiguousarray(arr, dtype="<f4")
        data.extend(b"\0" * ((-len(data)) % 4))
        off = len(data)
        data.extend(a.tobytes())
        d["bufferViews"].append({"buffer": 0, "byteOffset": off, "byteLength": a.nbytes, "target": 34962})
        d["accessors"].append({
            "bufferView": len(d["bufferViews"]) - 1,
            "componentType": 5126,
            "count": len(a),
            "type": "VEC3",
            "min": a.min(0).tolist(),
            "max": a.max(0).tolist(),
        })
        return len(d["accessors"]) - 1

    for p in d["meshes"][0]["primitives"]:
        mi = p["material"]
        if mi not in (5, 6):
            continue
        V = g.read(p["attributes"]["POSITION"]).astype(np.float64)
        out = V.copy()
        for mask, ctr in ((V[:, 0] >= 0, c_l), (V[:, 0] < 0, c_r)):
            if not mask.any():
                continue
            dxy = V[mask, :2] - ctr[:2]
            r2_old = (dxy ** 2).sum(axis=1)
            dxy_new = dxy * iris_boost
            r2_new = (dxy_new ** 2).sum(axis=1)
            # Curve expanded rim BACKWARD along eyeball sphere so iris stays strictly behind eyelids!
            dz = -(r2_new - r2_old) / (2.0 * R_eye) - 0.00025
            out[mask, :2] = ctr[:2] + dxy_new
            out[mask, 2] = V[mask, 2] + dz
        p["attributes"]["POSITION"] = add_pos(out)

    d["buffers"][0]["byteLength"] = len(data)
    js = json.dumps(d, separators=(",", ":")).encode("utf-8")
    js += b" " * ((-len(js)) % 4)
    data.extend(b"\0" * ((-len(data)) % 4))
    glb_path.write_bytes(
        struct.pack("<4sII", b"glTF", 2, 28 + len(js) + len(data))
        + struct.pack("<I4s", len(js), b"JSON") + js
        + struct.pack("<I4s", len(data), b"BIN\0") + data
    )


def build_female_v1(D: Path):
    """Unmistakably feminine Wuxia Heroine (Nữ Hiệp) diffeomorphic sculpt on Master40 anatomy mesh."""
    smooth, ga = M._smooth, M._ga

    def _limb_basis(A, B, lat_hint, fwd_hint):
        ab = B - A
        L = np.linalg.norm(ab)
        u = ab / max(L, 1e-12)
        lat = lat_hint - (lat_hint @ u) * u
        lat /= max(np.linalg.norm(lat), 1e-12)
        fwd = fwd_hint - (fwd_hint @ u) * u
        fwd /= max(np.linalg.norm(fwd), 1e-12)
        return ab, u, lat, fwd

    def sculpt(v):
        x, y, z = v.T
        a = np.abs(x)
        sx = np.sign(x)
        out = v.copy().astype(float)

        front = smooth(0.014, 0.064, z)
        back = 1.0 - smooth(-0.064, -0.014, z)
        # Tight orbital protection centered directly on eyeballs so surrounding lids can open cleanly!
        eye_core = ga(a, 0.0285, 0.012) * ga(y, 1.541, 0.011) * smooth(0.075, 0.105, z)
        eye_protect = 1.0 - eye_core

        # 1. Strong Feminine Hourglass Silhouette:
        #    Narrower sloping shoulders (-14%), slender swan neck (-16%), tapered ribcage (-13%),
        #    cinched high hourglass waist (-20%), flared rounded feminine pelvis/hips (+13%).
        upper_narrow = ga(y, 1.295, 0.085) * smooth(0.02, 0.09, a) * (1.0 - smooth(0.28, 0.44, a))
        rib_slim = ga(y, 1.185, 0.058) * smooth(0.02, 0.08, a) * (1.0 - smooth(0.18, 0.26, a))
        waist_cinch = ga(y, 1.072, 0.056) * smooth(0.02, 0.07, a) * (1.0 - smooth(0.16, 0.23, a))
        hip_flare = ga(y, 0.916, 0.068) * smooth(0.03, 0.09, a) * (1.0 - smooth(0.20, 0.28, a))

        out[:, 0] += (
            -0.135 * x * upper_narrow
            - 0.115 * x * rib_slim
            - 0.205 * x * waist_cinch
            + 0.125 * x * hip_flare
        )
        # Taper waist & ribcage front-to-back depth + graceful feminine lumbar curve
        waist_depth = ga(y, 1.072, 0.056) * (1.0 - smooth(0.15, 0.21, a))
        out[:, 2] -= 0.0115 * front * waist_depth
        out[:, 2] += 0.0085 * back * ga(y, 1.055, 0.050) * (1.0 - smooth(0.12, 0.18, a))

        # Delicate sloping shoulders & slender feminine swan neck
        trap_soft = ga(y, 1.385 - 0.30 * np.clip(a - 0.04, 0.0, 0.16), 0.034) * smooth(0.030, 0.068, a) * (1.0 - smooth(0.18, 0.26, a))
        out[:, 1] -= 0.0055 * trap_soft
        neck_zone = smooth(1.330, 1.362, y) * (1.0 - smooth(1.430, 1.458, y)) * (1.0 - smooth(0.075, 0.115, a))
        out[:, 0] -= 0.165 * x * neck_zone
        out[:, 2] -= 0.0068 * neck_zone * (front - 0.30 * back)

        # Delicate feminine collarbones
        clav_y = 1.324 + 0.13 * np.clip(a - 0.018, 0.0, 0.14)
        clav_bone = ga(y, clav_y, 0.0085) * smooth(0.018, 0.036, a) * (1.0 - smooth(0.120, 0.155, a))
        out[:, 2] += 0.0026 * front * clav_bone

        # 2. Full, Natural Teardrop Feminine Bust (unmistakably feminine even under loose robes):
        bx = a - 0.074
        r_bust = np.sqrt((bx / 0.056) ** 2 + ((y - 1.202) / 0.060) ** 2)
        bust_dome = np.exp(-0.5 * (r_bust ** 2.1)) * smooth(0.012, 0.040, a) * (1.0 - smooth(0.138, 0.182, a))
        bust_lower = np.exp(-0.5 * (((a - 0.075) / 0.048) ** 2 + ((y - 1.184) / 0.033) ** 2)) * smooth(0.014, 0.042, a)
        infra_fold = ga(y, 1.148, 0.014) * smooth(0.024, 0.052, a) * (1.0 - smooth(0.115, 0.148, a))
        cleavage = ga(x, 0.0, 0.016) * ga(y, 1.202, 0.048)

        out[:, 2] += front * (0.0365 * bust_dome + 0.0135 * bust_lower - 0.0052 * infra_fold - 0.0055 * cleavage)
        out[:, 1] -= front * (0.0068 * bust_lower)
        out[:, 0] += sx * front * (0.0055 * bust_dome * smooth(0.068, 0.115, a))

        # 3. Soft Feminine Lower Belly & Fuller Lifted Glutes:
        soft_belly = ga(y, 0.992, 0.052) * ga(a, 0.0, 0.068)
        out[:, 2] += front * 0.0045 * soft_belly

        glute_lobe = (
            (ga(x, 0.072, 0.052) + ga(x, -0.072, 0.052))
            * ga(y, 0.898, 0.058)
            * (1.0 - smooth(0.155, 0.210, a))
        )
        glute_upper = (
            (ga(x, 0.070, 0.050) + ga(x, -0.070, 0.050))
            * ga(y, 0.936, 0.042)
            * (1.0 - smooth(0.155, 0.210, a))
        )
        glute_cleft = ga(x, 0.0, 0.014) * ga(y, 0.888, 0.052)
        glute_fold = ga(y, 0.824 + 0.06 * a, 0.013) * ga(a, 0.068, 0.048)
        out[:, 2] -= back * (0.0205 * glute_lobe + 0.0090 * glute_upper - 0.0036 * glute_cleft - 0.0024 * glute_fold)

        # 4. Slender Feminine Limbs + Petite Delicate Hands (-15%) & Dainty Feet (-16%)!
        for s in (-1.0, 1.0):
            # Upper Arm (slender feminine taper)
            A = np.array([s * 0.187, 1.315, -0.008])
            B = np.array([s * 0.344, 1.133, -0.009])
            ab, _, lat, fwd = _limb_basis(A, B, np.array([0.0, 1.0, 0.0]), np.array([0.0, 0.0, 1.0]))
            t = (v - A) @ ab / (ab @ ab)
            radial = v - A - t[:, None] * ab
            rlen = np.linalg.norm(radial, axis=1)
            arm_mask = smooth(0.140, 0.210, a) * (x * s > 0) * (1.0 - smooth(0.080, 0.140, rlen))
            end_gate = smooth(-0.18, 0.08, t) * (1.0 - smooth(0.86, 1.06, t))
            out -= radial * (arm_mask * end_gate * 0.185)[:, None]

            # Forearm & Delicate Wrist
            A = np.array([s * 0.344, 1.133, -0.009])
            B = np.array([s * 0.471, 1.012, 0.172])
            ab, _, lat, fwd = _limb_basis(A, B, np.array([0.0, 1.0, 0.0]), np.array([0.0, 0.0, 1.0]))
            t = (v - A) @ ab / (ab @ ab)
            radial = v - A - t[:, None] * ab
            rlen = np.linalg.norm(radial, axis=1)
            fa_mask = smooth(0.24, 0.31, a) * (x * s > 0) * (1.0 - smooth(0.065, 0.118, rlen))
            fa_gate = smooth(-0.12, 0.12, t) * (1.0 - smooth(0.78, 1.04, t))
            out -= radial * (fa_mask * fa_gate * 0.170)[:, None]

            # Petite Feminine Hands (-15% uniform scale around wrist anchor B)
            d_wr = v - B
            wr_dist = np.linalg.norm(d_wr, axis=1)
            hand_mask = smooth(0.415, 0.468, a) * (x * s > 0) * (1.0 - smooth(0.14, 0.20, wr_dist)) * smooth(0.85, 1.02, t)
            out -= d_wr * (0.145 * hand_mask)[:, None]

            # Thigh (graceful feminine curve tapering to slender knees)
            A = np.array([s * 0.102, 0.883, -0.032])
            B = np.array([s * 0.140, 0.480, 0.000])
            ab, _, lat, fwd = _limb_basis(A, B, np.array([s, 0.0, 0.0]), np.array([0.0, 0.0, 1.0]))
            t = (v - A) @ ab / (ab @ ab)
            radial = v - A - t[:, None] * ab
            rlen = np.linalg.norm(radial, axis=1)
            rhat = radial / np.maximum(rlen[:, None], 1e-6)
            c_lat = rhat @ lat
            th_mask = smooth(0.018, 0.062, x * s) * (1.0 - smooth(0.095, 0.155, rlen))
            th_gate = smooth(-0.08, 0.08, t) * (1.0 - smooth(0.84, 1.00, t))
            upper_curve = 0.060 * ga(t, 0.11, 0.16) * smooth(0.10, 0.85, c_lat)
            knee_taper = -0.085 * smooth(0.42, 0.92, t)
            out += radial * (th_mask * th_gate * (upper_curve + knee_taper))[:, None]

            # Lower Leg & Slender Ankle
            A = np.array([s * 0.140, 0.480, 0.000])
            B = np.array([s * 0.186, 0.068, -0.012])
            ab, _, lat, fwd = _limb_basis(A, B, np.array([s, 0.0, 0.0]), np.array([0.0, 0.0, 1.0]))
            t = (v - A) @ ab / (ab @ ab)
            radial = v - A - t[:, None] * ab
            rlen = np.linalg.norm(radial, axis=1)
            cf_mask = smooth(0.030, 0.075, x * s) * (1.0 - smooth(0.078, 0.130, rlen))
            cf_gate = smooth(0.00, 0.12, t) * (1.0 - smooth(0.88, 1.04, t))
            slim_calf = -0.125 * (0.78 + 0.32 * smooth(0.45, 0.88, t))
            out += radial * (cf_mask * cf_gate * slim_calf)[:, None]

            # Petite Dainty Feet (-16% X/Z around ankle B, keeping sole Y grounded!)
            foot_mask = (1.0 - smooth(0.072, 0.115, y)) * (x * s > 0) * smooth(0.03, 0.09, a)
            out[:, 0] -= 0.16 * (x - B[0]) * foot_mask
            out[:, 2] -= 0.16 * (z - B[2]) * foot_mask

        # 5. Unmistakably Feminine Xianxia Heroine Face (Soft Oval-Heart Proportions + Expressive Almond Eyes):
        # 5a. Overall Head/Face Proportion Feminization (gentle -4.5% shorter face, preserve orbital eye alignment!)
        head_mask = smooth(1.425, 1.455, y) * (1.0 - smooth(0.095, 0.135, a))
        out[:, 1] -= 0.045 * (y - 1.541) * head_mask
        out[:, 0] -= 0.030 * x * head_mask * eye_protect

        # 5b. Expressive Almond Eyelids (subtle feminine upper lid arch + delicate outer wing)
        upper_lid = ga(a, 0.0280, 0.010) * ga(y, 1.5485, 0.0045) * smooth(0.092, 0.112, z)
        lower_lid = ga(a, 0.0285, 0.009) * ga(y, 1.5335, 0.0042) * smooth(0.092, 0.112, z)
        outer_wing = ga(a, 0.0405, 0.008) * ga(y, 1.5435, 0.0065) * smooth(0.082, 0.106, z)
        out[:, 1] += 0.0014 * upper_lid - 0.0006 * lower_lid + 0.0016 * outer_wing

        # 5c. Delicate Rose-Petal Lips (narrower width -16%, soft sweet smile corners, clean shape)
        y_mouth = 1.4745
        lip_zone = ga(y, y_mouth, 0.0125) * ga(a, 0.0, 0.024) * smooth(0.092, 0.112, z)
        lower_lip = ga(y, 1.4675, 0.0075) * ga(a, 0.0, 0.020) * smooth(0.095, 0.114, z)
        mouth_corner = ga(a, 0.0215, 0.0075) * ga(y, 1.4730, 0.0085) * smooth(0.092, 0.112, z)
        mentolabial = ga(y, 1.4570, 0.0075) * ga(a, 0.0, 0.020) * smooth(0.092, 0.112, z)
        out[:, 1] += -0.18 * (y - y_mouth) * lip_zone + 0.0020 * mouth_corner
        out[:, 0] -= 0.16 * x * lip_zone
        out[:, 2] += -0.0022 * lower_lip - 0.0012 * mentolabial

        # 5d. Petite Dainty Straight Nose (narrow alae -25%, refined tiny tip -20% with sweet upward tip)
        nose_alae = ga(y, 1.502, 0.011) * smooth(0.004, 0.013, a) * (1.0 - smooth(0.019, 0.031, a)) * smooth(0.100, 0.118, z)
        nose_tip = ga(y, 1.505, 0.010) * ga(a, 0.0, 0.013) * smooth(0.110, 0.126, z)
        nose_bridge = smooth(1.504, 1.514, y) * (1.0 - smooth(1.540, 1.552, y)) * ga(a, 0.0, 0.0085) * smooth(0.106, 0.122, z)
        nose_sides = smooth(1.505, 1.516, y) * (1.0 - smooth(1.538, 1.550, y)) * ga(a, 0.011, 0.007) * smooth(0.104, 0.120, z)
        out[:, 0] -= 0.25 * x * nose_alae + 0.20 * x * nose_tip + 0.18 * x * nose_sides
        out[:, 2] += 0.0016 * nose_bridge + 0.0006 * nose_tip
        out[:, 1] += 0.0024 * nose_tip

        # 5e. Soft Classical Oval-Heart Jaw, Rounded Petite Chin & Youthful Soft Cheeks
        jaw_taper = ga(a, 0.052, 0.018) * ga(y, 1.464, 0.024) * smooth(0.012, 0.080, z) * eye_protect
        soft_cheek = ga(a, 0.038, 0.020) * ga(y, 1.496, 0.022) * smooth(0.058, 0.100, z) * eye_protect
        apple_cheek = ga(a, 0.038, 0.016) * ga(y, 1.518, 0.014) * smooth(0.068, 0.104, z) * eye_protect
        chin_v = ga(y, 1.445, 0.016) * ga(a, 0.0, 0.032) * smooth(0.068, 0.108, z)
        out[:, 0] += -0.095 * x * jaw_taper - 0.105 * x * chin_v
        out[:, 2] += 0.0024 * apple_cheek + 0.0028 * soft_cheek
        out[:, 1] += 0.0022 * chin_v

        # 5f. Smooth Delicate Brow Ridge & Softly Rounded Forehead (zero masculine brow bossing)
        brow_smooth = ga(y, 1.561, 0.010) * ga(a, 0.022, 0.022) * smooth(0.078, 0.108, z) * eye_protect
        forehead_round = ga(y, 1.588, 0.022) * ga(a, 0.0, 0.042) * smooth(0.075, 0.110, z)
        under_eye = ga(a, 0.028, 0.014) * ga(y, 1.526, 0.007) * smooth(0.085, 0.108, z) * eye_protect
        out[:, 2] += -0.0024 * brow_smooth + 0.0022 * forehead_round - 0.0014 * under_eye
        return out

    def colors(v):
        x, y, z = v.T
        a = np.abs(x)
        front = smooth(0.018, 0.068, z)
        back = 1.0 - smooth(-0.068, -0.018, z)
        col = np.ones((len(v), 3), dtype=float)

        # Warm glowing rosy-peach blush on upper cheeks, nose tip & ears (radiant youthful femininity!)
        blush = (
            0.85 * ga(a, 0.040, 0.018) * ga(y, 1.516, 0.016) * smooth(0.072, 0.104, z)
            + 0.30 * ga(a, 0.0, 0.010) * ga(y, 1.506, 0.012) * smooth(0.098, 0.122, z)
            + 0.30 * ga(a, 0.076, 0.014) * ga(y, 1.518, 0.020) * (1.0 - smooth(0.02, 0.06, z))
            + 0.22 * ga(a, 0.142, 0.026) * ga(y, 0.475, 0.028) * smooth(0.005, 0.045, z)
        )
        col -= blush[:, None] * np.array([0.002, 0.068, 0.062])

        # Delicate crisp upper lash line + soft warm peach-rose outer eyelid tint (clean, no dark smudge!)
        lash_y = 1.5468 - 0.9 * (a - 0.0280) ** 2 + 0.0014 * smooth(0.032, 0.043, a)
        lash = ga(y, lash_y, 0.0014) * smooth(0.015, 0.020, a) * (1.0 - smooth(0.037, 0.044, a)) * smooth(0.090, 0.106, z)
        col -= lash[:, None] * np.array([0.28, 0.32, 0.34])
        eyeshadow = ga(a, 0.034, 0.011) * ga(y, 1.549, 0.0045) * smooth(0.088, 0.108, z)
        col -= eyeshadow[:, None] * np.array([0.005, 0.045, 0.042])

        # Luminous nose-bridge highlight
        nose_hi = ga(a, 0.0, 0.0055) * smooth(1.504, 1.514, y) * (1.0 - smooth(1.542, 1.554, y)) * smooth(0.110, 0.125, z)
        col += nose_hi[:, None] * np.array([0.040, 0.036, 0.032])

        # Soft feminine anatomical micro-AO
        ao = np.zeros(len(v), dtype=float)
        ao += 0.060 * ga(x, 0.0, 0.017) * ga(y, 1.202, 0.044) * front
        ao += 0.065 * ga(y, 1.148, 0.016) * smooth(0.024, 0.055, a) * (1.0 - smooth(0.115, 0.148, a)) * front
        ao += 0.070 * ga(a, 0.0, 0.042) * ga(y, 1.432, 0.020) * smooth(0.015, 0.065, z)
        ao += 0.040 * ga(x, 0.0, 0.015) * smooth(0.95, 1.02, y) * (1.0 - smooth(1.30, 1.36, y)) * back
        ao += 0.055 * ga(y, 0.824 + 0.06 * a, 0.014) * ga(a, 0.068, 0.048) * back
        ao += 0.060 * ga(a, 0.160, 0.022) * ga(y, 1.190, 0.028) * ga(z, -0.015, 0.034)

        ao = np.clip(ao, 0.0, 0.14)
        col -= ao[:, None] * np.array([0.68, 0.84, 0.92])
        return np.clip(col, 0.0, 1.0)

    M._export_sculpted_pair(
        D, sculpt, colors,
        ("female_body_v1.glb", "female_body_v1_shorts.glb"),
        "FemaleBody_V1_Master40", "Female Anatomy V1 Wuxia Heroine sculpt",
        mat_overrides={
            0: {"baseColorFactor": [0.74, 0.545, 0.425, 1], "roughnessFactor": 0.72},  # Luminous warm peach-porcelain skin
            1: {"baseColorFactor": [0.70, 0.52, 0.45, 1], "roughnessFactor": 0.38},    # Delicate nails
            2: {"baseColorFactor": [0.72, 0.335, 0.325, 1], "roughnessFactor": 0.46},  # Soft classical Wuxia coral-rose lips
            3: {"baseColorFactor": [0.80, 0.79, 0.78, 1], "roughnessFactor": 0.18},    # Bright clear sclera
            5: {"baseColorFactor": [0.080, 0.042, 0.024, 1], "roughnessFactor": 0.12}, # Deep luminous espresso iris
            6: {"baseColorFactor": [0.008, 0.008, 0.008, 1], "roughnessFactor": 0.10}, # Crisp pupil
        },
        skip_sculpt_materials={3, 5, 6},
    )
    # Subtly enlarge irises along eyeball sphere (strictly behind eyelids!) and add tailored silk bandeau top
    for name in ("female_body_v1.glb", "female_body_v1_shorts.glb"):
        _scale_eyeballs_and_irises(D / name, iris_boost=1.07)
    _add_silk_bandeau_to_glb(D / "female_body_v1_shorts.glb")


def main():
    root = Path(__file__).resolve().parent
    repo_root = root.parent.parent
    parser = argparse.ArgumentParser(description="Build game-ready 20-bone Mixamo T-Pose Female Body GLB (with underwear).")
    parser.add_argument("--out-dir", type=Path, default=root, help="Output directory for female_body.glb")
    parser.add_argument("--install", action="store_true", help="Install clothed-underwear version to assets/characters/bodies/female_body.glb")
    args = parser.parse_args()

    out_dir = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    source, reference = M.embedded_inputs()

    with tempfile.TemporaryDirectory(prefix="female_body_v1_") as tmp:
        work = Path(tmp)
        M.build_anatomy(source, reference, work)
        build_female_v1(work)
        dst_clothed = out_dir / "female_body.glb"
        M.export_game_tpose_20(work / "female_body_v1_shorts.glb", dst_clothed, source)
        if args.install:
            game_dst = repo_root / "assets/characters/bodies/female_body.glb"
            game_dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(dst_clothed, game_dst)
            print(f"Installed (with underwear) -> {game_dst}")


if __name__ == "__main__":
    main()
