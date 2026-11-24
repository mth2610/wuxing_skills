#!/usr/bin/env python3
"""
Martial Arts 3D Animation glTF 2.0 Generator for Raylib (LoadModelAnimations)
- 22-bone Mixamo skeleton (Right-Handed, Y-Up, Rest Pose exact match)
- Strict Right-Hand Rule local rotations & continuous normalized quaternions
- Built-in Forward Kinematics (FK) audit & automatic stance-foot grounding
- Zero external dependencies (Python 3 standard library only)
"""

import argparse
import base64
import json
import math
import os
import struct
from typing import Dict, List, Tuple

# ==============================================================================
# 1. SKELETON DEFINITION (EXACT MATCH SECTION 3.3)
# ==============================================================================

BONES = [
    # (index, name, parent_idx, rest_translation, children, euler_mode)
    (0,  "mixamorig:Hips",          -1, [ 0.00,  3.15,  0.00], [1, 14, 18], "yxz"),
    (1,  "mixamorig:Spine",          0, [ 0.00,  0.48, -0.04], [2],         "yxz"),
    (2,  "mixamorig:Spine1",         1, [ 0.00,  0.58,  0.02], [3],         "yxz"),
    (3,  "mixamorig:Spine2",         2, [ 0.00,  0.64,  0.04], [4, 6, 10],  "yxz"),
    (4,  "mixamorig:Neck",           3, [ 0.00,  0.54,  0.08], [5],         "yxz"),
    (5,  "mixamorig:Head",           4, [ 0.00,  0.34,  0.10], [],          "yxz"),
    (6,  "mixamorig:LeftShoulder",   3, [ 0.38,  0.44, -0.05], [7],         "yzx"),
    (7,  "mixamorig:LeftArm",        6, [ 0.76,  0.04,  0.00], [8],         "yzx"),
    (8,  "mixamorig:LeftForeArm",    7, [ 1.18,  0.00,  0.00], [9],         "yzx"),
    (9,  "mixamorig:LeftHand",       8, [ 1.12,  0.00,  0.00], [],          "yzx"),
    (10, "mixamorig:RightShoulder",  3, [-0.38,  0.44, -0.05], [11],        "yzx"),
    (11, "mixamorig:RightArm",      10, [-0.76,  0.04,  0.00], [12],        "yzx"),
    (12, "mixamorig:RightForeArm",  11, [-1.18,  0.00,  0.00], [13],        "yzx"),
    (13, "mixamorig:RightHand",     12, [-1.12,  0.00,  0.00], [],          "yzx"),
    (14, "mixamorig:LeftUpLeg",      0, [ 0.56, -0.18,  0.00], [15],        "yxz"),
    (15, "mixamorig:LeftLeg",       14, [ 0.00, -1.42,  0.00], [16],        "yxz"),
    (16, "mixamorig:LeftFoot",      15, [ 0.00, -1.35,  0.08], [17],        "yxz"),
    (17, "mixamorig:LeftToeBase",   16, [ 0.00, -0.18,  0.48], [],          "yxz"),
    (18, "mixamorig:RightUpLeg",     0, [-0.56, -0.18,  0.00], [19],        "yxz"),
    (19, "mixamorig:RightLeg",      18, [ 0.00, -1.42,  0.00], [20],        "yxz"),
    (20, "mixamorig:RightFoot",     19, [ 0.00, -1.35,  0.08], [21],        "yxz"),
    (21, "mixamorig:RightToeBase",  20, [ 0.00, -0.18,  0.48], [],          "yxz"),
]

SHORT_TO_IDX = {
    "Hips": 0, "Spine": 1, "Spine1": 2, "Spine2": 3, "Neck": 4, "Head": 5,
    "LeftShoulder": 6, "LeftArm": 7, "LeftForeArm": 8, "LeftHand": 9,
    "RightShoulder": 10, "RightArm": 11, "RightForeArm": 12, "RightHand": 13,
    "LeftUpLeg": 14, "LeftLeg": 15, "LeftFoot": 16, "LeftToeBase": 17,
    "RightUpLeg": 18, "RightLeg": 19, "RightFoot": 20, "RightToeBase": 21,
}

# ==============================================================================
# 2. QUATERNION & FORWARD KINEMATICS MATH (RIGHT-HANDED Y-UP)
# ==============================================================================

def quat_norm(q: Tuple[float, float, float, float]) -> Tuple[float, float, float, float]:
    x, y, z, w = q
    mag = math.sqrt(x*x + y*y + z*z + w*w)
    if mag < 1e-12:
        return (0.0, 0.0, 0.0, 1.0)
    return (x / mag, y / mag, z / mag, w / mag)

def quat_mul(a: Tuple[float, float, float, float],
             b: Tuple[float, float, float, float]) -> Tuple[float, float, float, float]:
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (
        aw*bx + ax*bw + ay*bz - az*by,
        aw*by - ax*bz + ay*bw + az*bx,
        aw*bz + ax*by - ay*bx + az*bw,
        aw*bw - ax*bx - ay*by - az*bz,
    )

def quat_dot(a: Tuple[float, float, float, float],
             b: Tuple[float, float, float, float]) -> float:
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3]

def quat_axis_deg(axis: str, deg: float) -> Tuple[float, float, float, float]:
    half = math.radians(deg) * 0.5
    s, c = math.sin(half), math.cos(half)
    if axis == "x":
        return (s, 0.0, 0.0, c)
    if axis == "y":
        return (0.0, s, 0.0, c)
    return (0.0, 0.0, s, c)

def euler_to_quat(rx: float, ry: float, rz: float, mode: str) -> Tuple[float, float, float, float]:
    qx = quat_axis_deg("x", rx)
    qy = quat_axis_deg("y", ry)
    qz = quat_axis_deg("z", rz)
    if mode == "yzx":
        # Arms (+X / -X): axial twist (X) -> raise/lower (Z) -> swing forward/back (Y)
        return quat_norm(quat_mul(qy, quat_mul(qz, qx)))
    # Torso & Legs (+Y / -Y): roll/abduct (Z) -> pitch/flex (X) -> yaw/turn (Y)
    return quat_norm(quat_mul(qy, quat_mul(qx, qz)))

def quat_rotate_vec(q: Tuple[float, float, float, float],
                    v: List[float]) -> List[float]:
    qx, qy, qz, qw = q
    vx, vy, vz = v
    tx = 2.0 * (qy*vz - qz*vy)
    ty = 2.0 * (qz*vx - qx*vz)
    tz = 2.0 * (qx*vy - qy*vx)
    return [
        vx + qw*tx + (qy*tz - qz*ty),
        vy + qw*ty + (qz*tx - qx*tz),
        vz + qw*tz + (qx*ty - qy*tx),
    ]

def compute_fk(local_quats: List[Tuple[float, float, float, float]],
               hips_pos: List[float]) -> List[List[float]]:
    """Compute world-space [x, y, z] for all 22 bones via Forward Kinematics."""
    world_pos: List[List[float]] = [[0.0, 0.0, 0.0]] * 22
    world_rot: List[Tuple[float, float, float, float]] = [(0.0, 0.0, 0.0, 1.0)] * 22

    world_pos[0] = list(hips_pos)
    world_rot[0] = local_quats[0]

    for idx, _name, parent, rest_t, _ch, _mode in BONES[1:]:
        rot_offset = quat_rotate_vec(world_rot[parent], rest_t)
        wp = world_pos[parent]
        world_pos[idx] = [wp[0] + rot_offset[0], wp[1] + rot_offset[1], wp[2] + rot_offset[2]]
        world_rot[idx] = quat_norm(quat_mul(world_rot[parent], local_quats[idx]))

    return world_pos

# ==============================================================================
# 3. EASING & POSE INTERPOLATION
# ==============================================================================

def apply_ease(u: float, ease: str) -> float:
    u = max(0.0, min(1.0, u))
    if ease == "linear":
        return u
    if ease == "ease_in":
        return u * u
    if ease == "ease_out":
        return 1.0 - (1.0 - u) * (1.0 - u)
    if ease == "snap":
        # Explosive martial-arts strike curve (fast acceleration into Hit-Apex)
        return 1.0 - (1.0 - u) ** 3
    if ease == "ease_in_out":
        return u * u * (3.0 - 2.0 * u)
    return u

def expand_pose(sparse: Dict[str, Tuple[float, float, float]]) -> List[Tuple[float, float, float]]:
    pose = [(0.0, 0.0, 0.0)] * 22
    for bone_short, angles in sparse.items():
        pose[SHORT_TO_IDX[bone_short]] = angles
    # Enforce physiological knee constraint (Leg.rx >= 0)
    for knee_idx in (15, 19):
        rx, ry, rz = pose[knee_idx]
        if rx < 0.0:
            raise ValueError(f"Anatomical violation: knee bone {knee_idx} rx={rx} < 0")
    return pose

def sample_animation(anim_def: dict, fps: int):
    duration = anim_def["duration"]
    frame_count = max(2, int(round(duration * fps)) + 1)
    dt = 1.0 / float(fps)
    times = [round(i * dt, 7) for i in range(frame_count)]
    # Match exact final timestamp only if strictly increasing
    if frame_count > 1 and duration > times[-2]:
        times[-1] = round(duration, 7)

    # Pre-expand keyframe poses
    kfs = []
    for kf in anim_def["keyframes"]:
        kfs.append((kf["t"], expand_pose(kf["pose"]), kf.get("ease", "ease_in_out")))

    rot_tracks: List[List[Tuple[float, float, float, float]]] = [[] for _ in range(22)]
    hips_trans_track: List[List[float]] = []
    fk_frames: List[List[List[float]]] = []

    for f_idx, t in enumerate(times):
        # Find surrounding keyframes
        if t <= kfs[0][0]:
            angles = kfs[0][1]
        elif t >= kfs[-1][0]:
            angles = kfs[-1][1]
        else:
            seg = 0
            for s in range(len(kfs) - 1):
                if kfs[s][0] <= t <= kfs[s + 1][0]:
                    seg = s
                    break
            t0, p0, _ = kfs[seg]
            t1, p1, ease_to = kfs[seg + 1]
            span = max(1e-6, t1 - t0)
            u = apply_ease((t - t0) / span, ease_to)
            angles = [
                (
                    p0[b][0] + (p1[b][0] - p0[b][0]) * u,
                    p0[b][1] + (p1[b][1] - p0[b][1]) * u,
                    p0[b][2] + (p1[b][2] - p0[b][2]) * u,
                )
                for b in range(22)
            ]

        # Convert to normalized quaternions + enforce hemisphere continuity
        frame_quats: List[Tuple[float, float, float, float]] = []
        for b in range(22):
            rx, ry, rz = angles[b]
            if b in (15, 19):
                rx = max(0.0, rx)  # Hard physiological guard on knees
            q = euler_to_quat(rx, ry, rz, BONES[b][5])
            if f_idx > 0 and quat_dot(rot_tracks[b][-1], q) < 0.0:
                q = (-q[0], -q[1], -q[2], -q[3])
            frame_quats.append(q)
            rot_tracks[b].append(q)

        # Automatic stance-foot grounding solver:
        # Target grounded Foot.y = 0.20 (center of valid range [0.10, 0.35])
        probe_fk = compute_fk(frame_quats, [0.0, 3.15, 0.0])
        lowest_foot_y = min(probe_fk[16][1], probe_fk[20][1])
        hips_y = 3.15 + (0.20 - lowest_foot_y)
        hips_y = max(2.85, min(3.15, hips_y))
        hips_pos = [0.0, round(hips_y, 6), 0.0]

        hips_trans_track.append(hips_pos)
        fk_frames.append(compute_fk(frame_quats, hips_pos))

    return times, rot_tracks, hips_trans_track, fk_frames

# ==============================================================================
# 4. SECTION 7: FORWARD KINEMATICS & ANATOMY VERIFICATION
# ==============================================================================

def verify_fk(anim_def: dict, times, rot_tracks, hips_trans, fk_frames) -> dict:
    errors = []
    min_foot_y_all = 999.0
    max_foot_y_all = -999.0
    min_hips_y = min(p[1] for p in hips_trans)
    max_hips_y = max(p[1] for p in hips_trans)
    max_q_err = 0.0

    for f_idx, wp in enumerate(fk_frames):
        # 7.1 Head.y > Hips.y
        if wp[5][1] <= wp[0][1]:
            errors.append(f"Frame {f_idx}: Head.y ({wp[5][1]:.3f}) <= Hips.y ({wp[0][1]:.3f})")

        # 7.2 Stance foot contact in [0.10, 0.35]
        stance_foot_y = min(wp[16][1], wp[20][1])
        min_foot_y_all = min(min_foot_y_all, stance_foot_y)
        max_foot_y_all = max(max_foot_y_all, stance_foot_y)
        if not (0.10 <= stance_foot_y <= 0.35):
            errors.append(f"Frame {f_idx}: Stance Foot.y={stance_foot_y:.3f} out of [0.10, 0.35]")

        # 5.1 Unit quaternion check
        for b in range(22):
            q = rot_tracks[b][f_idx]
            err = abs(math.sqrt(q[0]**2 + q[1]**2 + q[2]**2 + q[3]**2) - 1.0)
            max_q_err = max(max_q_err, err)
            if err > 1e-4:
                errors.append(f"Frame {f_idx} Bone {b}: |q| error = {err:.6f}")

    # 7.3 Check attacking hand/limb Z > 0 at Hit-Apex timestamp(s)
    apex_checks = []
    for apex_t, bone_short in anim_def["apex_checks"]:
        closest_f = min(range(len(times)), key=lambda i: abs(times[i] - apex_t))
        b_idx = SHORT_TO_IDX[bone_short]
        pos = fk_frames[closest_f][b_idx]
        apex_checks.append((apex_t, closest_f, bone_short, pos))
        if pos[2] <= 0.0:
            errors.append(
                f"Apex @{apex_t}s (f{closest_f}): {bone_short}.z={pos[2]:.3f} <= 0 (must be > 0)"
            )

    return {
        "ok": len(errors) == 0,
        "errors": errors,
        "stance_foot_y_range": (min_foot_y_all, max_foot_y_all),
        "hips_y_range": (min_hips_y, max_hips_y),
        "max_q_err": max_q_err,
        "apex_checks": apex_checks,
    }

# ==============================================================================
# 5. GLTF 2.0 SERIALIZER (EMBEDDED BASE64, RAYLIB COMPATIBLE)
# ==============================================================================

def build_gltf(anim_def: dict, times, rot_tracks, hips_trans) -> dict:
    binary = bytearray()
    buffer_views = []
    accessors = []

    def append_accessor(values_flat: List[float], acc_type: str, count: int) -> int:
        # 4-byte alignment (automatically satisfied by float32 packing)
        byte_offset = len(binary)
        raw = struct.pack(f"<{len(values_flat)}f", *values_flat)
        binary.extend(raw)
        bv_idx = len(buffer_views)
        buffer_views.append({
            "buffer": 0,
            "byteOffset": byte_offset,
            "byteLength": len(raw),
        })

        stride = {"SCALAR": 1, "VEC3": 3, "VEC4": 4}[acc_type]
        mins = [min(values_flat[c::stride]) for c in range(stride)]
        maxs = [max(values_flat[c::stride]) for c in range(stride)]

        acc_idx = len(accessors)
        accessors.append({
            "bufferView": bv_idx,
            "byteOffset": 0,
            "componentType": 5126,  # FLOAT
            "count": count,
            "type": acc_type,
            "min": [round(v, 7) for v in mins],
            "max": [round(v, 7) for v in maxs],
        })
        return acc_idx

    # Shared timestamp accessor (index 0)
    time_acc = append_accessor(times, "SCALAR", len(times))

    samplers = []
    channels = []

    # 1) Hips translation track (ONLY node 0 per Section 5.2)
    hips_flat = [coord for pos in hips_trans for coord in pos]
    hips_t_acc = append_accessor(hips_flat, "VEC3", len(hips_trans))
    samplers.append({"input": time_acc, "interpolation": "LINEAR", "output": hips_t_acc})
    channels.append({
        "sampler": 0,
        "target": {"node": 0, "path": "translation"},
    })

    # 2) All 22 rotation tracks (Section 5.1)
    for b in range(22):
        rot_flat = [comp for q in rot_tracks[b] for comp in q]
        rot_acc = append_accessor(rot_flat, "VEC4", len(rot_tracks[b]))
        s_idx = len(samplers)
        samplers.append({"input": time_acc, "interpolation": "LINEAR", "output": rot_acc})
        channels.append({
            "sampler": s_idx,
            "target": {"node": b, "path": "rotation"},
        })

    b64_data = base64.b64encode(bytes(binary)).decode("ascii")

    nodes = []
    for _idx, name, _parent, rest_t, children, _mode in BONES:
        node = {
            "name": name,
            "translation": rest_t,
            "rotation": [0, 0, 0, 1],
            "scale": [1, 1, 1],
        }
        if children:
            node["children"] = children
        nodes.append(node)

    return {
        "asset": {
            "version": "2.0",
            "generator": "Wuxia Technical Animator glTF 2.0 Pipeline (Raylib Compatible)",
        },
        "scene": 0,
        "scenes": [{"name": "Scene", "nodes": [0]}],
        "nodes": nodes,
        "skins": [
            {
                "name": "Armature",
                "joints": list(range(22)),
                "skeleton": 0,
            }
        ],
        "animations": [
            {
                "name": anim_def["name"],
                "extras": anim_def["extras"],
                "channels": channels,
                "samplers": samplers,
            }
        ],
        "accessors": accessors,
        "bufferViews": buffer_views,
        "buffers": [
            {
                "uri": f"data:application/octet-stream;base64,{b64_data}",
                "byteLength": len(binary),
            }
        ],
    }

# ==============================================================================
# 6. CHOREOGRAPHY DEFINITIONS FOR ALL 8 ANIMATIONS (SECTION 8)
# ==============================================================================
# Rotation reminder (Section 4):
# - LeftArm/ForeArm (+X):  Z- lower, Z+ raise, Y- forward (+Z), Y+ backward (-Z)
# - RightArm/ForeArm (-X): Z+ lower, Z- raise, Y+ forward (+Z), Y- backward (-Z)
# - UpLeg (-Y):            X- raise forward (+Z), Z+ abduct Left, Z- abduct Right
# - Leg (-Y):              X+ bend knee back (NEVER negative!)
# - Foot (-Y):             X- compensate upward to stay flat
# - Spine/Head/Hips (+Y):  X+ lean forward, X- lean back, Y+ turn left, Y- turn right

ANIMATIONS = [
    # --------------------------------------------------------------------------
    # 1. wuxia_sword_slash_1 (Beat 1 — Hoành Tảo)
    # --------------------------------------------------------------------------
    {
        "name": "wuxia_sword_slash_1",
        "duration": 0.46,
        "apex_checks": [(0.12, "RightHand")],
        "extras": {
            "slot": "sword_slash",
            "loop": False,
            "tags": ["martial_arts", "wuxia", "sword", "combo_beat_1"],
            "description": "Beat 1 Hoang Tao: horizontal right-to-left chest-level sword slash with 10-deg tilt and left sword-finger balance.",
        },
        "keyframes": [
            {
                "t": 0.00, "ease": "linear",
                "pose": {
                    "Hips": (0, -14, 0), "Spine": (4, -8, 0), "Spine1": (3, -6, 0), "Spine2": (2, -6, 0),
                    "Neck": (0, 14, 0), "Head": (0, 14, 0),
                    "LeftShoulder": (0, -8, -4), "LeftArm": (0, -38, -44), "LeftForeArm": (0, -48, 0), "LeftHand": (0, 0, 10),
                    "RightShoulder": (0, -10, 6), "RightArm": (-8, -28, 44), "RightForeArm": (0, 48, 0), "RightHand": (0, 0, -10),
                    "LeftUpLeg": (-14, 6, 4), "LeftLeg": (28, 0, 0), "LeftFoot": (-14, 0, 0),
                    "RightUpLeg": (-12, -8, -5), "RightLeg": (24, 0, 0), "RightFoot": (-12, 0, 0),
                },
            },
            {
                "t": 0.05, "ease": "ease_out",
                "pose": {
                    "Hips": (0, -32, 0), "Spine": (6, -16, 0), "Spine1": (4, -12, 0), "Spine2": (4, -14, 0),
                    "Neck": (0, 34, 0), "Head": (0, 30, 0),
                    "LeftShoulder": (0, -14, -4), "LeftArm": (0, -52, -38), "LeftForeArm": (0, -58, 0), "LeftHand": (0, 0, 14),
                    "RightShoulder": (0, -18, 8), "RightArm": (-12, -48, 52), "RightForeArm": (0, 56, 0), "RightHand": (10, -12, -16),
                    "LeftUpLeg": (-16, 10, 6), "LeftLeg": (32, 0, 0), "LeftFoot": (-16, 0, 0),
                    "RightUpLeg": (-16, -12, -6), "RightLeg": (32, 0, 0), "RightFoot": (-16, 0, 0),
                },
            },
            {
                "t": 0.12, "ease": "snap",  # Hit-Apex (frame 7 @60fps)
                "pose": {
                    "Hips": (0, 22, 0), "Spine": (8, 14, -3), "Spine1": (6, 14, -3), "Spine2": (4, 16, -4),
                    "Neck": (-4, -28, 0), "Head": (-2, -24, 0),
                    "LeftShoulder": (0, 14, -6), "LeftArm": (10, 32, -36), "LeftForeArm": (0, -38, 0), "LeftHand": (0, -10, 22),
                    "RightShoulder": (0, 16, 2), "RightArm": (-10, 92, 14), "RightForeArm": (0, 14, 0), "RightHand": (-10, 16, -12),
                    "LeftUpLeg": (-22, 8, 6), "LeftLeg": (42, 0, 0), "LeftFoot": (-20, 0, 0),
                    "RightUpLeg": (-12, -10, -6), "RightLeg": (24, 0, 0), "RightFoot": (-12, 0, 0),
                },
            },
            {
                "t": 0.17, "ease": "ease_out",
                "pose": {
                    "Hips": (0, 34, 0), "Spine": (6, 18, -3), "Spine1": (4, 16, -2), "Spine2": (2, 16, -2),
                    "Neck": (-4, -36, 0), "Head": (-2, -32, 0),
                    "LeftShoulder": (0, 18, -6), "LeftArm": (12, 42, -34), "LeftForeArm": (0, -44, 0), "LeftHand": (0, -12, 24),
                    "RightShoulder": (0, 22, 4), "RightArm": (-10, 114, 18), "RightForeArm": (0, 34, 0), "RightHand": (-12, 20, -10),
                    "LeftUpLeg": (-20, 8, 6), "LeftLeg": (40, 0, 0), "LeftFoot": (-20, 0, 0),
                    "RightUpLeg": (-14, -10, -6), "RightLeg": (28, 0, 0), "RightFoot": (-14, 0, 0),
                },
            },
            {
                "t": 0.46, "ease": "ease_in_out",
                "pose": {
                    "Hips": (0, 28, 0), "Spine": (4, 12, -2), "Spine1": (3, 10, 0), "Spine2": (2, 10, 0),
                    "Neck": (-2, -26, 0), "Head": (-2, -24, 0),
                    "LeftShoulder": (0, 14, -6), "LeftArm": (10, 34, -38), "LeftForeArm": (0, -48, 0), "LeftHand": (0, -10, 20),
                    "RightShoulder": (0, 18, 6), "RightArm": (-6, 106, 24), "RightForeArm": (0, 40, 0), "RightHand": (-8, 12, -8),
                    "LeftUpLeg": (-18, 8, 5), "LeftLeg": (36, 0, 0), "LeftFoot": (-18, 0, 0),
                    "RightUpLeg": (-14, -8, -5), "RightLeg": (28, 0, 0), "RightFoot": (-14, 0, 0),
                },
            },
        ],
    },

    # --------------------------------------------------------------------------
    # 2. wuxia_sword_slash_2 (Beat 2 — Nghịch Liêu)
    # --------------------------------------------------------------------------
    {
        "name": "wuxia_sword_slash_2",
        "duration": 0.50,
        "apex_checks": [(0.14, "RightHand")],
        "extras": {
            "slot": "sword_slash",
            "loop": False,
            "tags": ["martial_arts", "wuxia", "sword", "combo_beat_2"],
            "description": "Beat 2 Nghich Lieu: low-left drop into 45-deg rising diagonal slash from bottom-left to top-right.",
        },
        "keyframes": [
            {
                "t": 0.00, "ease": "linear",
                "pose": {
                    "Hips": (0, 28, 0), "Spine": (4, 12, -2), "Spine1": (3, 10, 0), "Spine2": (2, 10, 0),
                    "Neck": (-2, -26, 0), "Head": (-2, -24, 0),
                    "LeftShoulder": (0, 14, -6), "LeftArm": (10, 34, -38), "LeftForeArm": (0, -48, 0), "LeftHand": (0, -10, 20),
                    "RightShoulder": (0, 18, 6), "RightArm": (-6, 106, 24), "RightForeArm": (0, 40, 0), "RightHand": (-8, 12, -8),
                    "LeftUpLeg": (-18, 8, 5), "LeftLeg": (36, 0, 0), "LeftFoot": (-18, 0, 0),
                    "RightUpLeg": (-14, -8, -5), "RightLeg": (28, 0, 0), "RightFoot": (-14, 0, 0),
                },
            },
            {
                "t": 0.07, "ease": "ease_out",  # Drop tip near left knee, flip wrist upward
                "pose": {
                    "Hips": (0, 36, 0), "Spine": (10, 16, 4), "Spine1": (8, 12, 4), "Spine2": (6, 12, 4),
                    "Neck": (-8, -32, 0), "Head": (-6, -30, 0),
                    "LeftShoulder": (0, 16, -8), "LeftArm": (8, 38, -42), "LeftForeArm": (0, -52, 0), "LeftHand": (0, 0, 16),
                    "RightShoulder": (0, 20, 10), "RightArm": (42, 102, 54), "RightForeArm": (0, 30, 0), "RightHand": (36, 14, 16),
                    "LeftUpLeg": (-22, 10, 6), "LeftLeg": (44, 0, 0), "LeftFoot": (-22, 0, 0),
                    "RightUpLeg": (-18, -10, -6), "RightLeg": (36, 0, 0), "RightFoot": (-18, 0, 0),
                },
            },
            {
                "t": 0.14, "ease": "snap",  # Hit-Apex (frame 8 @60fps): 45-deg rising slash across +Z
                "pose": {
                    "Hips": (0, -18, 0), "Spine": (-4, -12, -4), "Spine1": (-4, -12, -4), "Spine2": (-4, -14, -4),
                    "Neck": (4, 24, 0), "Head": (4, 22, 0),
                    "LeftShoulder": (0, -10, -4), "LeftArm": (0, -28, -44), "LeftForeArm": (0, -62, 0), "LeftHand": (0, 0, 18),
                    "RightShoulder": (0, 8, -14), "RightArm": (30, 62, -20), "RightForeArm": (0, 14, 0), "RightHand": (24, 10, 18),
                    "LeftUpLeg": (-12, 6, 5), "LeftLeg": (24, 0, 0), "LeftFoot": (-12, 0, 0),
                    "RightUpLeg": (-16, -8, -5), "RightLeg": (32, 0, 0), "RightFoot": (-16, 0, 0),
                },
            },
            {
                "t": 0.21, "ease": "ease_out",  # Sweep past right shoulder
                "pose": {
                    "Hips": (0, -32, 0), "Spine": (-4, -16, -4), "Spine1": (-4, -14, -4), "Spine2": (-2, -16, -2),
                    "Neck": (4, 34, 0), "Head": (4, 30, 0),
                    "LeftShoulder": (0, -12, -4), "LeftArm": (0, -40, -42), "LeftForeArm": (0, -68, 0), "LeftHand": (0, 0, 20),
                    "RightShoulder": (0, -8, -18), "RightArm": (20, 18, -44), "RightForeArm": (0, 42, 0), "RightHand": (12, -10, 10),
                    "LeftUpLeg": (-14, 6, 5), "LeftLeg": (28, 0, 0), "LeftFoot": (-14, 0, 0),
                    "RightUpLeg": (-18, -10, -6), "RightLeg": (36, 0, 0), "RightFoot": (-18, 0, 0),
                },
            },
            {
                "t": 0.50, "ease": "ease_in_out",  # Controlled high-ready guard
                "pose": {
                    "Hips": (0, -18, 0), "Spine": (2, -10, 0), "Spine1": (2, -8, 0), "Spine2": (2, -8, 0),
                    "Neck": (0, 20, 0), "Head": (0, 18, 0),
                    "LeftShoulder": (0, -10, -4), "LeftArm": (0, -42, -38), "LeftForeArm": (0, -60, 0), "LeftHand": (0, 0, 16),
                    "RightShoulder": (0, 4, -8), "RightArm": (10, 36, -16), "RightForeArm": (0, 56, 0), "RightHand": (0, 0, 8),
                    "LeftUpLeg": (-14, 6, 5), "LeftLeg": (28, 0, 0), "LeftFoot": (-14, 0, 0),
                    "RightUpLeg": (-14, -8, -5), "RightLeg": (28, 0, 0), "RightFoot": (-14, 0, 0),
                },
            },
        ],
    },

    # --------------------------------------------------------------------------
    # 3. wuxia_sword_thrust_3 (Beat 3 — Trường Hồng Quán Nhật)
    # --------------------------------------------------------------------------
    {
        "name": "wuxia_sword_thrust_3",
        "duration": 0.54,
        "apex_checks": [(0.16, "RightHand")],
        "extras": {
            "slot": "sword_thrust",
            "loop": False,
            "tags": ["martial_arts", "wuxia", "sword", "combo_beat_3", "thrust"],
            "description": "Beat 3 Truong Hong Quan Nhat: coiled right-rib draw into full-extension straight forward sword lance.",
        },
        "keyframes": [
            {
                "t": 0.00, "ease": "linear",
                "pose": {
                    "Hips": (0, -18, 0), "Spine": (2, -10, 0), "Spine1": (2, -8, 0), "Spine2": (2, -8, 0),
                    "Neck": (0, 20, 0), "Head": (0, 18, 0),
                    "LeftShoulder": (0, -10, -4), "LeftArm": (0, -44, -38), "LeftForeArm": (0, -56, 0), "LeftHand": (0, 0, 16),
                    "RightShoulder": (0, -6, 6), "RightArm": (0, -12, 42), "RightForeArm": (0, 70, 0), "RightHand": (0, 0, -12),
                    "LeftUpLeg": (-14, 6, 5), "LeftLeg": (28, 0, 0), "LeftFoot": (-14, 0, 0),
                    "RightUpLeg": (-16, -8, -5), "RightLeg": (32, 0, 0), "RightFoot": (-16, 0, 0),
                },
            },
            {
                "t": 0.08, "ease": "ease_out",  # Coil right leg & hilt tight at right ribs
                "pose": {
                    "Hips": (0, -34, 0), "Spine": (6, -14, 0), "Spine1": (4, -12, 0), "Spine2": (4, -12, 0),
                    "Neck": (-4, 34, 0), "Head": (-2, 30, 0),
                    "LeftShoulder": (0, -16, -4), "LeftArm": (0, -64, -28), "LeftForeArm": (0, -44, 0), "LeftHand": (0, 0, 22),
                    "RightShoulder": (0, -18, 8), "RightArm": (0, -38, 48), "RightForeArm": (0, 92, 0), "RightHand": (0, 12, -18),
                    "LeftUpLeg": (-14, 8, 6), "LeftLeg": (28, 0, 0), "LeftFoot": (-14, 0, 0),
                    "RightUpLeg": (-22, -12, -6), "RightLeg": (44, 0, 0), "RightFoot": (-22, 0, 0),
                },
            },
            {
                "t": 0.16, "ease": "snap",  # Hit-Apex (frame 10 @60fps): full straight thrust + counter-balance left arm
                "pose": {
                    "Hips": (0, 30, 0), "Spine": (8, 14, 0), "Spine1": (6, 14, 0), "Spine2": (4, 18, 0),
                    "Neck": (-8, -32, 0), "Head": (-6, -32, 0),
                    "LeftShoulder": (0, 18, -4), "LeftArm": (0, 48, -20), "LeftForeArm": (0, -8, 0), "LeftHand": (0, 0, 24),
                    "RightShoulder": (0, 18, 2), "RightArm": (0, 88, 6), "RightForeArm": (0, 4, 0), "RightHand": (0, 14, -14),
                    "LeftUpLeg": (-22, 6, 6), "LeftLeg": (44, 0, 0), "LeftFoot": (-22, 0, 0),
                    "RightUpLeg": (-10, -10, -6), "RightLeg": (20, 0, 0), "RightFoot": (-10, 0, 0),
                },
            },
            {
                "t": 0.23, "ease": "ease_out",  # Impact settle
                "pose": {
                    "Hips": (0, 32, 0), "Spine": (8, 16, 0), "Spine1": (6, 14, 0), "Spine2": (4, 18, 0),
                    "Neck": (-8, -34, 0), "Head": (-6, -34, 0),
                    "LeftShoulder": (0, 18, -4), "LeftArm": (0, 50, -22), "LeftForeArm": (0, -10, 0), "LeftHand": (0, 0, 24),
                    "RightShoulder": (0, 18, 2), "RightArm": (0, 90, 8), "RightForeArm": (0, 6, 0), "RightHand": (0, 14, -14),
                    "LeftUpLeg": (-22, 6, 6), "LeftLeg": (44, 0, 0), "LeftFoot": (-22, 0, 0),
                    "RightUpLeg": (-10, -10, -6), "RightLeg": (20, 0, 0), "RightFoot": (-10, 0, 0),
                },
            },
            {
                "t": 0.54, "ease": "ease_in_out",  # Withdraw to hip ready for Beat 4 spin
                "pose": {
                    "Hips": (0, -8, 0), "Spine": (3, -6, 0), "Spine1": (2, -4, 0), "Spine2": (2, -4, 0),
                    "Neck": (-2, 10, 0), "Head": (-2, 10, 0),
                    "LeftShoulder": (0, -6, -4), "LeftArm": (0, -32, -40), "LeftForeArm": (0, -54, 0), "LeftHand": (0, 0, 14),
                    "RightShoulder": (0, -6, 6), "RightArm": (0, 12, 46), "RightForeArm": (0, 66, 0), "RightHand": (0, 0, -10),
                    "LeftUpLeg": (-14, 6, 5), "LeftLeg": (28, 0, 0), "LeftFoot": (-14, 0, 0),
                    "RightUpLeg": (-14, -6, -5), "RightLeg": (28, 0, 0), "RightFoot": (-14, 0, 0),
                },
            },
        ],
    },

    # --------------------------------------------------------------------------
    # 4. wuxia_sword_finisher_4 (Beat 4 — Toàn Phong Trảm)
    # --------------------------------------------------------------------------
    {
        "name": "wuxia_sword_finisher_4",
        "duration": 0.66,
        "apex_checks": [(0.22, "RightHand")],
        "extras": {
            "slot": "sword_slash",
            "loop": False,
            "tags": ["martial_arts", "wuxia", "sword", "combo_beat_4", "finisher", "guard_break"],
            "description": "Beat 4 Toan Phong Tram: 360-deg spinning heel pivot into wide 270-deg shoulder-height crescent finisher.",
        },
        "keyframes": [
            {
                "t": 0.00, "ease": "linear",
                "pose": {
                    "Hips": (0, 0, 0), "Spine": (4, -6, 0), "Spine1": (2, -4, 0), "Spine2": (2, -4, 0),
                    "Neck": (-2, 6, 0), "Head": (-2, 6, 0),
                    "LeftShoulder": (0, -6, -4), "LeftArm": (0, -32, -40), "LeftForeArm": (0, -52, 0),
                    "RightShoulder": (0, -6, 6), "RightArm": (0, 12, 44), "RightForeArm": (0, 62, 0),
                    "LeftUpLeg": (-14, 6, 5), "LeftLeg": (28, 0, 0), "LeftFoot": (-14, 0, 0),
                    "RightUpLeg": (-14, -6, -5), "RightLeg": (28, 0, 0), "RightFoot": (-14, 0, 0),
                },
            },
            {
                "t": 0.06, "ease": "ease_in",  # 90 deg into spin, raising sword
                "pose": {
                    "Hips": (0, 90, 0), "Spine": (6, 12, 0), "Spine1": (4, 10, 0), "Spine2": (4, 10, 0),
                    "Neck": (-4, -18, 0), "Head": (-2, -18, 0),
                    "LeftShoulder": (0, -8, -6), "LeftArm": (0, -38, -36), "LeftForeArm": (0, -64, 0),
                    "RightShoulder": (0, 4, -12), "RightArm": (0, 18, -38), "RightForeArm": (0, 46, 0),
                    "LeftUpLeg": (-16, 6, 5), "LeftLeg": (32, 0, 0), "LeftFoot": (-16, 0, 0),
                    "RightUpLeg": (-18, -6, -5), "RightLeg": (36, 0, 0), "RightFoot": (-18, 0, 0),
                },
            },
            {
                "t": 0.12, "ease": "linear",  # 180 deg back turned, sword high overhead
                "pose": {
                    "Hips": (0, 185, 0), "Spine": (6, 16, 0), "Spine1": (4, 14, 0), "Spine2": (4, 14, 0),
                    "Neck": (-4, -14, 0), "Head": (-2, -14, 0),
                    "LeftShoulder": (0, -10, -8), "LeftArm": (0, -36, -34), "LeftForeArm": (0, -68, 0),
                    "RightShoulder": (0, 4, -22), "RightArm": (-8, 12, -64), "RightForeArm": (0, 38, 0),
                    "LeftUpLeg": (-16, 6, 5), "LeftLeg": (32, 0, 0), "LeftFoot": (-16, 0, 0),
                    "RightUpLeg": (-16, -6, -5), "RightLeg": (32, 0, 0), "RightFoot": (-16, 0, 0),
                },
            },
            {
                "t": 0.17, "ease": "linear",  # 275 deg whipping sword into horizontal plane
                "pose": {
                    "Hips": (0, 278, 0), "Spine": (8, 18, -3), "Spine1": (6, 16, -3), "Spine2": (4, 20, -3),
                    "Neck": (-4, 14, 0), "Head": (-2, 16, 0),
                    "LeftShoulder": (0, 12, -6), "LeftArm": (0, 24, -36), "LeftForeArm": (0, -40, 0),
                    "RightShoulder": (0, 10, -4), "RightArm": (-6, 48, -8), "RightForeArm": (0, 14, 0),
                    "LeftUpLeg": (-18, 6, 6), "LeftLeg": (36, 0, 0), "LeftFoot": (-18, 0, 0),
                    "RightUpLeg": (-16, -6, -6), "RightLeg": (32, 0, 0), "RightFoot": (-16, 0, 0),
                },
            },
            {
                "t": 0.22, "ease": "snap",  # Hit-Apex (frame 13 @60fps): 360 deg + full 270-deg crescent slash!
                "pose": {
                    "Hips": (0, 360, 0), "Spine": (8, 16, -4), "Spine1": (6, 18, -4), "Spine2": (4, 20, -4),
                    "Neck": (-6, -28, 0), "Head": (-4, -26, 0),
                    "LeftShoulder": (0, 16, -6), "LeftArm": (8, 38, -30), "LeftForeArm": (0, -34, 0), "LeftHand": (0, 0, 22),
                    "RightShoulder": (0, 18, 2), "RightArm": (-6, 96, 6), "RightForeArm": (0, 10, 0), "RightHand": (-6, 16, -12),
                    "LeftUpLeg": (-22, 8, 8), "LeftLeg": (44, 0, 0), "LeftFoot": (-22, 0, 0),
                    "RightUpLeg": (-16, -8, -8), "RightLeg": (32, 0, 0), "RightFoot": (-16, 0, 0),
                },
            },
            {
                "t": 0.29, "ease": "ease_out",  # Wide follow-through
                "pose": {
                    "Hips": (0, 382, 0), "Spine": (6, 16, -3), "Spine1": (4, 14, -2), "Spine2": (2, 14, -2),
                    "Neck": (-4, -34, 0), "Head": (-2, -32, 0),
                    "LeftShoulder": (0, 18, -6), "LeftArm": (8, 42, -34), "LeftForeArm": (0, -42, 0), "LeftHand": (0, 0, 20),
                    "RightShoulder": (0, 22, 4), "RightArm": (-6, 118, 16), "RightForeArm": (0, 28, 0), "RightHand": (-8, 18, -10),
                    "LeftUpLeg": (-22, 8, 8), "LeftLeg": (44, 0, 0), "LeftFoot": (-22, 0, 0),
                    "RightUpLeg": (-18, -8, -8), "RightLeg": (36, 0, 0), "RightFoot": (-18, 0, 0),
                },
            },
            {
                "t": 0.66, "ease": "ease_in_out",  # Plant solid stance -> sword_idle_wuxia
                "pose": {
                    "Hips": (0, 360, 0), "Spine": (3, -6, 0), "Spine1": (2, -4, 0), "Spine2": (2, -4, 0),
                    "Neck": (-2, 8, 0), "Head": (-2, 6, 0),
                    "LeftShoulder": (0, -8, -4), "LeftArm": (0, -40, -38), "LeftForeArm": (0, -52, 0), "LeftHand": (0, 0, 18),
                    "RightShoulder": (0, 6, 6), "RightArm": (0, 36, 38), "RightForeArm": (0, 54, 0), "RightHand": (0, 0, -8),
                    "LeftUpLeg": (-16, 8, 6), "LeftLeg": (32, 0, 0), "LeftFoot": (-16, 0, 0),
                    "RightUpLeg": (-16, -8, -6), "RightLeg": (32, 0, 0), "RightFoot": (-16, 0, 0),
                },
            },
        ],
    },

    # --------------------------------------------------------------------------
    # 5. baji_shoulder_ram (Thiết Sơn Kháo)
    # --------------------------------------------------------------------------
    {
        "name": "baji_shoulder_ram",
        "duration": 0.42,
        "apex_checks": [(0.15, "RightForeArm")],
        "extras": {
            "slot": "custom",
            "loop": False,
            "tags": ["martial_arts", "bajiquan", "shoulder_ram", "heavy_impact"],
            "description": "Thiet Son Khao: low Ma Bo stance into explosive right-shoulder and elbow mountain ram with tucked chin.",
        },
        "keyframes": [
            {
                "t": 0.00, "ease": "linear",
                "pose": {
                    "Hips": (0, -10, 0), "Spine": (4, -4, 0), "Spine1": (3, -4, 0), "Spine2": (2, -4, 0),
                    "Neck": (2, 10, 0), "Head": (2, 8, 0),
                    "LeftShoulder": (0, -4, -4), "LeftArm": (0, -28, -48), "LeftForeArm": (0, -78, 0),
                    "RightShoulder": (0, -6, 4), "RightArm": (0, 24, 48), "RightForeArm": (0, 82, 0),
                    "LeftUpLeg": (-16, 8, 8), "LeftLeg": (32, 0, 0), "LeftFoot": (-16, 0, 0),
                    "RightUpLeg": (-16, -8, -8), "RightLeg": (32, 0, 0), "RightFoot": (-16, 0, 0),
                },
            },
            {
                "t": 0.09, "ease": "ease_out",  # Low Ma Bo, wind right shoulder back, tight fists at waist
                "pose": {
                    "Hips": (0, -26, 0), "Spine": (8, -12, 4), "Spine1": (6, -10, 4), "Spine2": (4, -12, 4),
                    "Neck": (6, 28, 0), "Head": (6, 26, 0),
                    "LeftShoulder": (0, 8, -4), "LeftArm": (0, 14, -56), "LeftForeArm": (0, -96, 0),
                    "RightShoulder": (0, -16, 8), "RightArm": (0, -20, 56), "RightForeArm": (0, 98, 0),
                    "LeftUpLeg": (-24, 10, 10), "LeftLeg": (48, 0, 0), "LeftFoot": (-24, 0, 0),
                    "RightUpLeg": (-24, -10, -10), "RightLeg": (48, 0, 0), "RightFoot": (-24, 0, 0),
                },
            },
            {
                "t": 0.15, "ease": "snap",  # Hit-Apex (frame 9 @60fps): explosive right shoulder & elbow ram forward
                "pose": {
                    "Hips": (0, 38, 0), "Spine": (10, 14, -4), "Spine1": (8, 16, -4), "Spine2": (6, 22, -6),
                    "Neck": (16, -34, 8), "Head": (10, -30, 6),  # Chin tucked hard into shoulder
                    "LeftShoulder": (0, 14, -6), "LeftArm": (0, 26, -50), "LeftForeArm": (0, -82, 0),
                    "RightShoulder": (0, 24, 10), "RightArm": (10, 70, 42), "RightForeArm": (0, 106, 0),
                    "LeftUpLeg": (-24, 8, 10), "LeftLeg": (48, 0, 0), "LeftFoot": (-24, 0, 0),
                    "RightUpLeg": (-20, -10, -10), "RightLeg": (40, 0, 0), "RightFoot": (-20, 0, 0),
                },
            },
            {
                "t": 0.22, "ease": "ease_out",  # Brake impact firmly in stance
                "pose": {
                    "Hips": (0, 40, 0), "Spine": (10, 14, -4), "Spine1": (8, 16, -4), "Spine2": (6, 20, -6),
                    "Neck": (16, -36, 8), "Head": (10, -32, 6),
                    "LeftShoulder": (0, 14, -6), "LeftArm": (0, 28, -50), "LeftForeArm": (0, -84, 0),
                    "RightShoulder": (0, 24, 10), "RightArm": (10, 72, 44), "RightForeArm": (0, 106, 0),
                    "LeftUpLeg": (-24, 8, 10), "LeftLeg": (48, 0, 0), "LeftFoot": (-24, 0, 0),
                    "RightUpLeg": (-22, -10, -10), "RightLeg": (44, 0, 0), "RightFoot": (-22, 0, 0),
                },
            },
            {
                "t": 0.42, "ease": "ease_in_out",  # Controlled recovery to compact Baji guard
                "pose": {
                    "Hips": (0, 6, 0), "Spine": (4, 0, 0), "Spine1": (3, 0, 0), "Spine2": (2, 0, 0),
                    "Neck": (2, -4, 0), "Head": (2, -4, 0),
                    "LeftShoulder": (0, -6, -4), "LeftArm": (0, -34, -46), "LeftForeArm": (0, -76, 0),
                    "RightShoulder": (0, 6, 4), "RightArm": (0, 32, 46), "RightForeArm": (0, 80, 0),
                    "LeftUpLeg": (-18, 8, 8), "LeftLeg": (36, 0, 0), "LeftFoot": (-18, 0, 0),
                    "RightUpLeg": (-18, -8, -8), "RightLeg": (36, 0, 0), "RightFoot": (-18, 0, 0),
                },
            },
        ],
    },

    # --------------------------------------------------------------------------
    # 6. palm_twin_push (Song Chưởng Thôi Sơn)
    # --------------------------------------------------------------------------
    {
        "name": "palm_twin_push",
        "duration": 0.50,
        "apex_checks": [(0.18, "LeftHand"), (0.18, "RightHand")],
        "extras": {
            "slot": "palm",
            "loop": False,
            "tags": ["martial_arts", "palm", "twin_push", "jin_force"],
            "description": "Song Chuong Thoi Son: chambered double palms bursting forward in parallel with subtle jin-force fingertip release.",
        },
        "keyframes": [
            {
                "t": 0.00, "ease": "linear",
                "pose": {
                    "Hips": (0, 6, 0), "Spine": (2, 0, 0), "Spine1": (2, 0, 0), "Spine2": (2, 0, 0),
                    "Neck": (0, -4, 0), "Head": (0, -4, 0),
                    "LeftShoulder": (0, -4, -4), "LeftArm": (0, -34, -46), "LeftForeArm": (0, -68, 0), "LeftHand": (0, 0, 36),
                    "RightShoulder": (0, 4, 4), "RightArm": (0, 36, 44), "RightForeArm": (0, 64, 0), "RightHand": (0, 0, -36),
                    "LeftUpLeg": (-16, 6, 6), "LeftLeg": (32, 0, 0), "LeftFoot": (-16, 0, 0),
                    "RightUpLeg": (-14, -6, -6), "RightLeg": (28, 0, 0), "RightFoot": (-14, 0, 0),
                },
            },
            {
                "t": 0.11, "ease": "ease_out",  # Chamber palms at ribs, elbows pinned back, chest expanded
                "pose": {
                    "Hips": (0, 8, 0), "Spine": (-4, -2, 0), "Spine1": (-4, -2, 0), "Spine2": (-4, 0, 0),
                    "Neck": (4, -4, 0), "Head": (4, -4, 0),
                    "LeftShoulder": (0, 14, -4), "LeftArm": (0, 32, -52), "LeftForeArm": (0, -104, 0), "LeftHand": (0, 0, 56),
                    "RightShoulder": (0, -14, 4), "RightArm": (0, -30, 54), "RightForeArm": (0, 102, 0), "RightHand": (0, 0, -54),
                    "LeftUpLeg": (-20, 8, 6), "LeftLeg": (40, 0, 0), "LeftFoot": (-20, 0, 0),
                    "RightUpLeg": (-20, -8, -6), "RightLeg": (40, 0, 0), "RightFoot": (-20, 0, 0),
                },
            },
            {
                "t": 0.18, "ease": "snap",  # Hit-Apex (frame 11 @60fps): explosive parallel twin-palm push
                "pose": {
                    "Hips": (0, -6, 0), "Spine": (6, 2, 0), "Spine1": (6, 2, 0), "Spine2": (6, 2, 0),
                    "Neck": (-8, 2, 0), "Head": (-6, 2, 0),
                    "LeftShoulder": (0, -16, -2), "LeftArm": (0, -82, -12), "LeftForeArm": (0, -10, 0), "LeftHand": (0, 0, 64),
                    "RightShoulder": (0, 18, 2), "RightArm": (0, 86, 14), "RightForeArm": (0, 12, 0), "RightHand": (0, 0, -62),
                    "LeftUpLeg": (-24, 6, 6), "LeftLeg": (46, 0, 0), "LeftFoot": (-22, 0, 0),
                    "RightUpLeg": (-12, -8, -6), "RightLeg": (24, 0, 0), "RightFoot": (-12, 0, 0),
                },
            },
            {
                "t": 0.25, "ease": "linear",  # Subtle fingertip vibration pulse 1
                "pose": {
                    "Hips": (0, -6, 0), "Spine": (6, 2, 0), "Spine1": (5, 2, 0), "Spine2": (5, 2, 0),
                    "Neck": (-8, 2, 0), "Head": (-6, 2, 0),
                    "LeftShoulder": (0, -16, -2), "LeftArm": (0, -84, -12), "LeftForeArm": (0, -8, 0), "LeftHand": (0, 0, 72),
                    "RightShoulder": (0, 18, 2), "RightArm": (0, 88, 14), "RightForeArm": (0, 10, 0), "RightHand": (0, 0, -70),
                    "LeftUpLeg": (-24, 6, 6), "LeftLeg": (46, 0, 0), "LeftFoot": (-22, 0, 0),
                    "RightUpLeg": (-12, -8, -6), "RightLeg": (24, 0, 0), "RightFoot": (-12, 0, 0),
                },
            },
            {
                "t": 0.31, "ease": "linear",  # Subtle fingertip vibration pulse 2
                "pose": {
                    "Hips": (0, -5, 0), "Spine": (5, 2, 0), "Spine1": (5, 2, 0), "Spine2": (4, 2, 0),
                    "Neck": (-6, 2, 0), "Head": (-6, 2, 0),
                    "LeftShoulder": (0, -14, -2), "LeftArm": (0, -80, -14), "LeftForeArm": (0, -14, 0), "LeftHand": (0, 0, 54),
                    "RightShoulder": (0, 16, 2), "RightArm": (0, 84, 16), "RightForeArm": (0, 16, 0), "RightHand": (0, 0, -52),
                    "LeftUpLeg": (-22, 6, 6), "LeftLeg": (44, 0, 0), "LeftFoot": (-22, 0, 0),
                    "RightUpLeg": (-14, -8, -6), "RightLeg": (28, 0, 0), "RightFoot": (-14, 0, 0),
                },
            },
            {
                "t": 0.50, "ease": "ease_in_out",  # Retract to martial guard
                "pose": {
                    "Hips": (0, 4, 0), "Spine": (3, 0, 0), "Spine1": (2, 0, 0), "Spine2": (2, 0, 0),
                    "Neck": (-2, -2, 0), "Head": (-2, -2, 0),
                    "LeftShoulder": (0, -6, -4), "LeftArm": (0, -38, -44), "LeftForeArm": (0, -66, 0), "LeftHand": (0, 0, 38),
                    "RightShoulder": (0, 6, 4), "RightArm": (0, 40, 42), "RightForeArm": (0, 62, 0), "RightHand": (0, 0, -36),
                    "LeftUpLeg": (-16, 6, 6), "LeftLeg": (32, 0, 0), "LeftFoot": (-16, 0, 0),
                    "RightUpLeg": (-14, -6, -6), "RightLeg": (28, 0, 0), "RightFoot": (-14, 0, 0),
                },
            },
        ],
    },

    # --------------------------------------------------------------------------
    # 7. palm_combo_rapid (Liên Hoàn Chưởng — Beat 4 Unarmed)
    # --------------------------------------------------------------------------
    {
        "name": "palm_combo_rapid",
        "duration": 0.55,
        "apex_checks": [(0.16, "LeftHand"), (0.28, "RightHand")],
        "extras": {
            "slot": "palm",
            "loop": False,
            "tags": ["martial_arts", "palm", "rapid_combo", "unarmed_beat_4"],
            "description": "Lien Hoan Chuong: rapid left chest palm strike snapping back into a heavier hip-driven right palm strike.",
        },
        "keyframes": [
            {
                "t": 0.00, "ease": "linear",
                "pose": {
                    "Hips": (0, 12, 0), "Spine": (3, 6, 0), "Spine1": (2, 4, 0), "Spine2": (2, 4, 0),
                    "Neck": (-2, -12, 0), "Head": (-2, -10, 0),
                    "LeftShoulder": (0, 8, -4), "LeftArm": (0, 12, -50), "LeftForeArm": (0, -90, 0), "LeftHand": (0, 0, 48),
                    "RightShoulder": (0, 6, 4), "RightArm": (0, 36, 42), "RightForeArm": (0, 68, 0), "RightHand": (0, 0, -36),
                    "LeftUpLeg": (-16, 6, 6), "LeftLeg": (32, 0, 0), "LeftFoot": (-16, 0, 0),
                    "RightUpLeg": (-16, -6, -6), "RightLeg": (32, 0, 0), "RightFoot": (-16, 0, 0),
                },
            },
            {
                "t": 0.07, "ease": "ease_out",  # Coil left palm at left waist
                "pose": {
                    "Hips": (0, 22, 0), "Spine": (4, 10, 0), "Spine1": (4, 8, 0), "Spine2": (4, 8, 0),
                    "Neck": (-4, -24, 0), "Head": (-2, -22, 0),
                    "LeftShoulder": (0, 14, -4), "LeftArm": (0, 26, -52), "LeftForeArm": (0, -98, 0), "LeftHand": (0, 0, 56),
                    "RightShoulder": (0, 8, 4), "RightArm": (0, 44, 38), "RightForeArm": (0, 62, 0), "RightHand": (0, 0, -36),
                    "LeftUpLeg": (-18, 6, 6), "LeftLeg": (36, 0, 0), "LeftFoot": (-18, 0, 0),
                    "RightUpLeg": (-16, -6, -6), "RightLeg": (32, 0, 0), "RightFoot": (-16, 0, 0),
                },
            },
            {
                "t": 0.16, "ease": "snap",  # Hit-Apex 1 (frame 10 @60fps): Left palm strike
                "pose": {
                    "Hips": (0, -24, 0), "Spine": (6, -12, 0), "Spine1": (4, -12, 0), "Spine2": (4, -14, 0),
                    "Neck": (-6, 30, 0), "Head": (-4, 28, 0),
                    "LeftShoulder": (0, -18, -2), "LeftArm": (0, -88, -10), "LeftForeArm": (0, -8, 0), "LeftHand": (0, 0, 62),
                    "RightShoulder": (0, -14, 4), "RightArm": (0, -28, 52), "RightForeArm": (0, 100, 0), "RightHand": (0, 0, -56),
                    "LeftUpLeg": (-14, 6, 6), "LeftLeg": (28, 0, 0), "LeftFoot": (-14, 0, 0),
                    "RightUpLeg": (-22, -8, -6), "RightLeg": (42, 0, 0), "RightFoot": (-20, 0, 0),
                },
            },
            {
                "t": 0.21, "ease": "ease_in",  # Rapid left recoil while loading right hip
                "pose": {
                    "Hips": (0, -14, 0), "Spine": (6, -8, 0), "Spine1": (4, -8, 0), "Spine2": (4, -8, 0),
                    "Neck": (-4, 18, 0), "Head": (-4, 16, 0),
                    "LeftShoulder": (0, 10, -4), "LeftArm": (0, 16, -52), "LeftForeArm": (0, -96, 0), "LeftHand": (0, 0, 50),
                    "RightShoulder": (0, -16, 4), "RightArm": (0, -20, 50), "RightForeArm": (0, 96, 0), "RightHand": (0, 0, -58),
                    "LeftUpLeg": (-18, 6, 6), "LeftLeg": (36, 0, 0), "LeftFoot": (-18, 0, 0),
                    "RightUpLeg": (-18, -8, -6), "RightLeg": (36, 0, 0), "RightFoot": (-18, 0, 0),
                },
            },
            {
                "t": 0.28, "ease": "snap",  # Hit-Apex 2 (frame 17 @60fps): Stronger right palm strike
                "pose": {
                    "Hips": (0, 32, 0), "Spine": (8, 16, 0), "Spine1": (6, 16, 0), "Spine2": (6, 20, 0),
                    "Neck": (-8, -36, 0), "Head": (-6, -34, 0),
                    "LeftShoulder": (0, 16, -4), "LeftArm": (0, 32, -52), "LeftForeArm": (0, -102, 0), "LeftHand": (0, 0, 52),
                    "RightShoulder": (0, 20, 2), "RightArm": (0, 90, 8), "RightForeArm": (0, 6, 0), "RightHand": (0, 0, -64),
                    "LeftUpLeg": (-24, 6, 6), "LeftLeg": (46, 0, 0), "LeftFoot": (-22, 0, 0),
                    "RightUpLeg": (-12, -8, -6), "RightLeg": (24, 0, 0), "RightFoot": (-12, 0, 0),
                },
            },
            {
                "t": 0.36, "ease": "ease_out",  # Follow-through hold
                "pose": {
                    "Hips": (0, 28, 0), "Spine": (6, 14, 0), "Spine1": (5, 14, 0), "Spine2": (4, 16, 0),
                    "Neck": (-6, -32, 0), "Head": (-4, -30, 0),
                    "LeftShoulder": (0, 14, -4), "LeftArm": (0, 28, -50), "LeftForeArm": (0, -96, 0), "LeftHand": (0, 0, 46),
                    "RightShoulder": (0, 18, 4), "RightArm": (0, 86, 12), "RightForeArm": (0, 14, 0), "RightHand": (0, 0, -56),
                    "LeftUpLeg": (-22, 6, 6), "LeftLeg": (44, 0, 0), "LeftFoot": (-22, 0, 0),
                    "RightUpLeg": (-14, -8, -6), "RightLeg": (28, 0, 0), "RightFoot": (-14, 0, 0),
                },
            },
            {
                "t": 0.55, "ease": "ease_in_out",  # Return to balanced martial guard
                "pose": {
                    "Hips": (0, 4, 0), "Spine": (3, 0, 0), "Spine1": (2, 0, 0), "Spine2": (2, 0, 0),
                    "Neck": (-2, -2, 0), "Head": (-2, -2, 0),
                    "LeftShoulder": (0, -6, -4), "LeftArm": (0, -40, -42), "LeftForeArm": (0, -72, 0), "LeftHand": (0, 0, 32),
                    "RightShoulder": (0, 6, 4), "RightArm": (0, 42, 40), "RightForeArm": (0, 70, 0), "RightHand": (0, 0, -30),
                    "LeftUpLeg": (-16, 6, 6), "LeftLeg": (32, 0, 0), "LeftFoot": (-16, 0, 0),
                    "RightUpLeg": (-16, -6, -6), "RightLeg": (32, 0, 0), "RightFoot": (-16, 0, 0),
                },
            },
        ],
    },

    # --------------------------------------------------------------------------
    # 8. martial_ground_stomp (Chấn Địa Cước)
    # --------------------------------------------------------------------------
    {
        "name": "martial_ground_stomp",
        "duration": 0.55,
        "apex_checks": [(0.15, "RightLeg"), (0.22, "RightFoot")],
        "extras": {
            "slot": "kick",
            "loop": False,
            "tags": ["martial_arts", "stomp", "shockwave", "earth_pillar"],
            "description": "Chan Dia Cuoc: high right-knee chamber with raised arms slamming into a seismic heel stomp and dual hip-pull fists.",
        },
        "keyframes": [
            {
                "t": 0.00, "ease": "linear",
                "pose": {
                    "Hips": (0, 0, 0), "Spine": (2, 0, 0), "Spine1": (2, 0, 0), "Spine2": (2, 0, 0),
                    "Neck": (-2, 0, 0), "Head": (-2, 0, 0),
                    "LeftShoulder": (0, -4, -4), "LeftArm": (0, -30, -46), "LeftForeArm": (0, -64, 0),
                    "RightShoulder": (0, 4, 4), "RightArm": (0, 30, 46), "RightForeArm": (0, 64, 0),
                    "LeftUpLeg": (-12, 4, 4), "LeftLeg": (24, 0, 0), "LeftFoot": (-12, 0, 0),
                    "RightUpLeg": (-12, -4, -4), "RightLeg": (24, 0, 0), "RightFoot": (-12, 0, 0),
                },
            },
            {
                "t": 0.15, "ease": "ease_out",  # High right knee chamber to waist + arms raised wide shoulder-high
                "pose": {
                    "Hips": (0, -8, 0), "Spine": (-4, 4, -4), "Spine1": (-4, 4, -2), "Spine2": (-2, 4, 0),
                    "Neck": (6, 2, 0), "Head": (6, 2, 0),
                    "LeftShoulder": (0, -6, 8), "LeftArm": (0, -28, -8), "LeftForeArm": (0, -56, 0),
                    "RightShoulder": (0, 6, -8), "RightArm": (0, 32, 6), "RightForeArm": (0, 58, 0),
                    "LeftUpLeg": (-10, 4, 2), "LeftLeg": (20, 0, 0), "LeftFoot": (-10, 0, 0),
                    "RightUpLeg": (-82, -6, -6), "RightLeg": (92, 0, 0), "RightFoot": (-10, 0, 0),
                },
            },
            {
                "t": 0.22, "ease": "snap",  # Hit-Apex (frame 13 @60fps): seismic heel stomp + dual fists yanked to hips!
                "pose": {
                    "Hips": (0, 4, 0), "Spine": (6, -2, 0), "Spine1": (6, -2, 0), "Spine2": (4, 0, 0),
                    "Neck": (-8, -2, 0), "Head": (-6, 0, 0),
                    "LeftShoulder": (0, 12, -6), "LeftArm": (0, 20, -54), "LeftForeArm": (0, -94, 0),
                    "RightShoulder": (0, -12, 6), "RightArm": (0, -18, 54), "RightForeArm": (0, 96, 0),
                    "LeftUpLeg": (-22, 10, 10), "LeftLeg": (44, 0, 0), "LeftFoot": (-22, 0, 0),
                    "RightUpLeg": (-24, -10, -11), "RightLeg": (48, 0, 0), "RightFoot": (-24, 0, 0),
                },
            },
            {
                "t": 0.36, "ease": "ease_out",  # Hold imposing stance
                "pose": {
                    "Hips": (0, 4, 0), "Spine": (5, -2, 0), "Spine1": (4, -2, 0), "Spine2": (4, 0, 0),
                    "Neck": (-6, -2, 0), "Head": (-6, 0, 0),
                    "LeftShoulder": (0, 10, -6), "LeftArm": (0, 16, -54), "LeftForeArm": (0, -90, 0),
                    "RightShoulder": (0, -10, 6), "RightArm": (0, -16, 54), "RightForeArm": (0, 92, 0),
                    "LeftUpLeg": (-22, 10, 10), "LeftLeg": (44, 0, 0), "LeftFoot": (-22, 0, 0),
                    "RightUpLeg": (-22, -10, -10), "RightLeg": (44, 0, 0), "RightFoot": (-22, 0, 0),
                },
            },
            {
                "t": 0.55, "ease": "ease_in_out",  # Rise cleanly back to upright stance
                "pose": {
                    "Hips": (0, 0, 0), "Spine": (2, 0, 0), "Spine1": (1, 0, 0), "Spine2": (1, 0, 0),
                    "Neck": (-2, 0, 0), "Head": (-2, 0, 0),
                    "LeftShoulder": (0, -4, -4), "LeftArm": (0, -28, -48), "LeftForeArm": (0, -58, 0),
                    "RightShoulder": (0, 4, 4), "RightArm": (0, 28, 48), "RightForeArm": (0, 58, 0),
                    "LeftUpLeg": (-10, 4, 4), "LeftLeg": (20, 0, 0), "LeftFoot": (-10, 0, 0),
                    "RightUpLeg": (-10, -4, -4), "RightLeg": (20, 0, 0), "RightFoot": (-10, 0, 0),
                },
            },
        ],
    },
]

# ==============================================================================
# 7. CLI ENTRYPOINT
# ==============================================================================

def main():
    parser = argparse.ArgumentParser(description="Generate FK-verified glTF 2.0 martial arts animations for Raylib.")
    parser.add_argument("--fps", type=int, default=30, choices=[30, 60], help="Output frame rate (default: 30 per Section 5.3)")
    parser.add_argument("--out", type=str, default="output_gltf", help="Output directory")
    parser.add_argument("--only", type=str, default="", help="Generate only a specific animation name")
    args = parser.parse_args()

    os.makedirs(args.out, exist_ok=True)
    targets = [a for a in ANIMATIONS if not args.only or a["name"] == args.only]
    if not targets:
        raise SystemExit(f"Animation '{args.only}' not found.")

    all_passed = True
    for anim_def in targets:
        times, rot_tracks, hips_trans, fk_frames = sample_animation(anim_def, args.fps)
        report = verify_fk(anim_def, times, rot_tracks, hips_trans, fk_frames)

        status = "PASS" if report["ok"] else "FAIL"
        print(f"[{status}] {anim_def['name']} ({len(times)} frames @{args.fps}fps, {anim_def['duration']}s)")
        print(f"       Stance Foot.y: {report['stance_foot_y_range'][0]:.3f} .. {report['stance_foot_y_range'][1]:.3f} (target 0.10..0.35)")
        print(f"       Hips.y:        {report['hips_y_range'][0]:.3f} .. {report['hips_y_range'][1]:.3f} (target 2.85..3.15)")
        print(f"       Max |q|-1 err: {report['max_q_err']:.2e}")
        for apex_t, cf, bname, pos in report["apex_checks"]:
            print(f"       Apex @{apex_t:.2f}s (f{cf}): {bname} = ({pos[0]:+.2f}, {pos[1]:+.2f}, {pos[2]:+.2f})")

        if not report["ok"]:
            all_passed = False
            for err in report["errors"]:
                print(f"       ERROR: {err}")
            continue

        gltf_doc = build_gltf(anim_def, times, rot_tracks, hips_trans)
        out_path = os.path.join(args.out, f"{anim_def['name']}.gltf")
        with open(out_path, "w", encoding="utf-8") as f:
            json.dump(gltf_doc, f, separators=(",", ":"))
        print(f"       -> Wrote {out_path} ({os.path.getsize(out_path):,} bytes)\n")

    if not all_passed:
        raise SystemExit("FK verification failed on one or more animations.")

if __name__ == "__main__":
    main()