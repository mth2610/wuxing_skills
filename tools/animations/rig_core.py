#!/usr/bin/env python3
"""
tools/animations/rig_core.py — Graceful C2-Spline GAME_MIXAMO_20 Wuxia Animation Engine

Upgraded for fluid, ethereal Chinese Wuxia / Xianxia martial-arts choreography:
  - Exact 20-bone GAME_MIXAMO_20 skeleton matching male_body.glb, female_body.glb & humanoid_anims.glb
  - True C2-continuous Centripetal Catmull-Rom spline interpolation across keyframes
    (zero mechanical stop-start pauses between poses; periodic C2 seam for seamless loops!)
  - Native Sword-Twirl (Kiếm Hoa / blade_spin_deg) & Pelvis-Yaw Space (local_body_space)
    for silky 360-deg aerial spins, soaring Khinh Công leaps, and flowing sword flourishes
  - Kinetic-chain spine/neck wave distribution + continuous forearm pronation/supination twist
  - Optional light SLERP motion-polish pass for butter-smooth 60 FPS joint trajectories
  - Automated FK, anatomy, grounding & blade-clearance verification
  - Standalone .glb exporter, non-destructive humanoid_anims.glb merger, and crisp solid
    3D silhouette + glowing sword-ribbon contact-sheet renderer for visual QA
"""

from __future__ import annotations

import copy
import json
import math
import struct
import zlib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple
import numpy as np


# ==============================================================================
# 1. EXACT GAME_MIXAMO_20 SKELETON (Matches male_body.glb & humanoid_anims.glb)
# ==============================================================================

GAME_MIXAMO_20: List[Tuple[int, str, int, Tuple[float, float, float], Tuple[float, float, float, float]]] = [
    ( 0, "mixamorig:Hips",           -1, ( 0.00,  0.95, 0.00), ( 0.0,  0.0,  0.0, 1.0)),
    ( 1, "mixamorig:Spine",           0, ( 0.00,  0.13, 0.00), ( 0.0,  0.0,  0.0, 1.0)),
    ( 2, "mixamorig:Spine1",          1, ( 0.00,  0.14, 0.00), ( 0.0,  0.0,  0.0, 1.0)),
    ( 3, "mixamorig:Spine2",          2, ( 0.00,  0.14, 0.00), ( 0.0,  0.0,  0.0, 1.0)),
    ( 4, "mixamorig:Neck",            3, ( 0.00,  0.16, 0.00), ( 0.0,  0.0,  0.0, 1.0)),
    ( 5, "mixamorig:Head",            4, ( 0.00,  0.14, 0.00), ( 0.0,  0.0,  0.0, 1.0)),
    ( 6, "mixamorig:LeftShoulder",    3, ( 0.08,  0.12, 0.00), (-0.5, -0.5, -0.5, 0.5)),
    ( 7, "mixamorig:LeftArm",         6, ( 0.00,  0.14, 0.00), ( 0.0,  0.0,  0.0, 1.0)),
    ( 8, "mixamorig:LeftForeArm",     7, ( 0.00,  0.26, 0.00), ( 0.0,  0.0,  0.0, 1.0)),
    ( 9, "mixamorig:LeftHand",        8, ( 0.00,  0.24, 0.00), ( 0.0,  0.0,  0.0, 1.0)),
    (10, "mixamorig:RightShoulder",   3, (-0.08,  0.12, 0.00), (-0.5,  0.5,  0.5, 0.5)),
    (11, "mixamorig:RightArm",       10, ( 0.00,  0.14, 0.00), ( 0.0,  0.0,  0.0, 1.0)),
    (12, "mixamorig:RightForeArm",   11, ( 0.00,  0.26, 0.00), ( 0.0,  0.0,  0.0, 1.0)),
    (13, "mixamorig:RightHand",      12, ( 0.00,  0.24, 0.00), ( 0.0,  0.0,  0.0, 1.0)),
    (14, "mixamorig:LeftUpLeg",       0, ( 0.10, -0.05, 0.00), (-1.0,  0.0,  0.0, 0.0)),
    (15, "mixamorig:LeftLeg",        14, ( 0.00,  0.42, 0.00), ( 0.0,  0.0,  0.0, 1.0)),
    (16, "mixamorig:LeftFoot",       15, ( 0.00,  0.40, 0.00), ( 0.0,  0.0,  0.0, 1.0)),
    (17, "mixamorig:RightUpLeg",      0, (-0.10, -0.05, 0.00), (-1.0,  0.0,  0.0, 0.0)),
    (18, "mixamorig:RightLeg",       17, ( 0.00,  0.42, 0.00), ( 0.0,  0.0,  0.0, 1.0)),
    (19, "mixamorig:RightFoot",      18, ( 0.00,  0.40, 0.00), ( 0.0,  0.0,  0.0, 1.0)),
]

BONE_NAMES = [b[1] for b in GAME_MIXAMO_20]
BONE_IDX = {b[1]: b[0] for b in GAME_MIXAMO_20}
NUM_BONES = len(GAME_MIXAMO_20)
ANKLE_GROUND_Y = 0.080

# Exact kiem_thuan_thien right-hand socket transform from assets/characters/item_catalog.json
SWORD_SOCKET_POS = np.array([-0.042, 0.130, -0.024], dtype=np.float64)
SWORD_SOCKET_ROT_DEG = np.array([0.0, 0.0, 32.0], dtype=np.float64)
SWORD_BLADE_BASE_Y = 0.080
SWORD_BLADE_TIP_Y = 0.850


# ==============================================================================
# 2. 3D ROTATION & QUATERNION MATH
# ==============================================================================

def v_norm(v: np.ndarray, fallback: Optional[np.ndarray] = None) -> np.ndarray:
    v = np.asarray(v, dtype=np.float64)
    n = float(np.linalg.norm(v))
    if n < 1e-10:
        return np.array([0.0, 1.0, 0.0] if fallback is None else fallback, dtype=np.float64)
    return v / n


def rot_x(deg: float) -> np.ndarray:
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    return np.array([[1.0, 0.0, 0.0], [0.0, c, -s], [0.0, s, c]], dtype=np.float64)


def rot_y(deg: float) -> np.ndarray:
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, 0.0, s], [0.0, 1.0, 0.0], [-s, 0.0, c]], dtype=np.float64)


def rot_z(deg: float) -> np.ndarray:
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, -s, 0.0], [s, c, 0.0], [0.0, 0.0, 1.0]], dtype=np.float64)


def euler_yxz(yaw_deg: float = 0.0, pitch_deg: float = 0.0, roll_deg: float = 0.0) -> np.ndarray:
    return rot_y(yaw_deg) @ rot_x(pitch_deg) @ rot_z(roll_deg)


def socket_rot_matrix(euler_deg: Sequence[float]) -> np.ndarray:
    return rot_x(float(euler_deg[0])) @ rot_y(float(euler_deg[1])) @ rot_z(float(euler_deg[2]))


SWORD_SOCKET_R = socket_rot_matrix(SWORD_SOCKET_ROT_DEG)


def quat_to_mat(q: Sequence[float]) -> np.ndarray:
    x, y, z, w = float(q[0]), float(q[1]), float(q[2]), float(q[3])
    n = math.sqrt(x * x + y * y + z * z + w * w)
    if n > 1e-12:
        x, y, z, w = x / n, y / n, z / n, w / n
    return np.array([
        [1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - z * w),       2.0 * (x * z + y * w)],
        [2.0 * (x * y + z * w),       1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z - x * w)],
        [2.0 * (x * z - y * w),       2.0 * (y * z + x * w),       1.0 - 2.0 * (x * x + y * y)],
    ], dtype=np.float64)


def mat_to_quat(R: np.ndarray) -> np.ndarray:
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
    q = np.array([x, y, z, w], dtype=np.float64)
    return q / max(float(np.linalg.norm(q)), 1e-12)


def quat_slerp(q0: np.ndarray, q1: np.ndarray, u: float) -> np.ndarray:
    dot = float(np.dot(q0, q1))
    if dot < 0.0:
        q1 = -q1
        dot = -dot
    if dot > 0.9995:
        q = q0 + u * (q1 - q0)
        return q / max(float(np.linalg.norm(q)), 1e-12)
    theta = math.acos(max(-1.0, min(1.0, dot)))
    s = math.sin(theta)
    return (math.sin((1.0 - u) * theta) * q0 + math.sin(u * theta) * q1) / s


def axis_angle_mat(axis: np.ndarray, rad: float) -> np.ndarray:
    a = v_norm(axis)
    c, s = math.cos(rad), math.sin(rad)
    t = 1.0 - c
    x, y, z = a
    return np.array([
        [t * x * x + c,     t * x * y - s * z, t * x * z + s * y],
        [t * x * y + s * z, t * y * y + c,     t * y * z - s * x],
        [t * x * z - s * y, t * y * z + s * x, t * z * z + c    ],
    ], dtype=np.float64)


def decompose_swing_twist_y(R_local: np.ndarray) -> Tuple[float, np.ndarray]:
    q = mat_to_quat(R_local)
    tw_n = math.hypot(q[1], q[3])
    if tw_n < 1e-8:
        return 0.0, R_local
    qy, qw = q[1] / tw_n, q[3] / tw_n
    twist_rad = 2.0 * math.atan2(qy, qw)
    if twist_rad > math.pi:
        twist_rad -= 2.0 * math.pi
    elif twist_rad < -math.pi:
        twist_rad += 2.0 * math.pi
    R_twist = rot_y(math.degrees(twist_rad))
    return twist_rad, R_local @ R_twist.T


def _unwrap_angle(rad: float, ref_rad: Optional[float]) -> float:
    if ref_rad is None:
        return rad
    while rad - ref_rad > math.pi:
        rad -= 2.0 * math.pi
    while rad - ref_rad < -math.pi:
        rad += 2.0 * math.pi
    return rad


# ==============================================================================
# 3. ANALYTICAL 2-BONE IK & ANATOMICAL POSE SOLVER
# ==============================================================================

def solve_two_bone_ik(
    root: np.ndarray,
    target: np.ndarray,
    L1: float,
    L2: float,
    pole_hint: np.ndarray,
    min_bend_deg: float = 5.0,
) -> Tuple[np.ndarray, np.ndarray]:
    d_vec = target - root
    d = float(np.linalg.norm(d_vec))
    max_reach = (L1 + L2) * math.cos(math.radians(min_bend_deg * 0.5))
    min_reach = abs(L1 - L2) + 0.04
    d_clamped = max(min_reach, min(max_reach, d))
    dir_rt = v_norm(d_vec, np.array([0.0, -1.0, 0.0]))
    end_pos = root + dir_rt * d_clamped

    cos_a = max(-1.0, min(1.0, (L1 * L1 + d_clamped * d_clamped - L2 * L2) / (2.0 * L1 * d_clamped)))
    sin_a = math.sqrt(max(0.0, 1.0 - cos_a * cos_a))

    p_dir = pole_hint - root
    p_perp = p_dir - np.dot(p_dir, dir_rt) * dir_rt
    if float(np.linalg.norm(p_perp)) < 1e-6:
        alt = np.array([0.0, 0.0, 1.0]) if abs(dir_rt[2]) < 0.9 else np.array([1.0, 0.0, 0.0])
        p_perp = alt - np.dot(alt, dir_rt) * dir_rt
    p_perp = v_norm(p_perp)

    mid = root + (dir_rt * cos_a + p_perp * sin_a) * L1
    return mid, end_pos


def build_leg_frame(bone_dir: np.ndarray, knee_axis_hint: np.ndarray) -> np.ndarray:
    y = v_norm(bone_dir)
    x = knee_axis_hint - np.dot(knee_axis_hint, y) * y
    x = v_norm(x, np.array([1.0, 0.0, 0.0]))
    z = v_norm(np.cross(x, y))
    x = v_norm(np.cross(y, z))
    return np.column_stack([x, y, z])


def build_foot_frame(facing_yaw_deg: float, pitch_deg: float = 0.0, roll_deg: float = 0.0) -> np.ndarray:
    R_rest = np.array([[1.0, 0.0, 0.0], [0.0, -1.0, 0.0], [0.0, 0.0, -1.0]], dtype=np.float64)
    return rot_y(facing_yaw_deg) @ rot_x(-pitch_deg) @ rot_z(roll_deg) @ R_rest


def build_arm_frame(bone_dir: np.ndarray, up_hint: np.ndarray, is_right: bool) -> np.ndarray:
    y = v_norm(bone_dir)
    z = up_hint - np.dot(up_hint, y) * y
    z = v_norm(z, np.array([0.0, 1.0, 0.0]))
    x = v_norm(np.cross(y, z))
    z = v_norm(np.cross(x, y))
    return np.column_stack([x, y, z])


@dataclass
class PoseSpec:
    """World/Body-space martial-arts keyframe pose specification (meters & degrees).

    Coordinate convention (GAME_MIXAMO_20):
      +X = Left, -X = Right | +Y = Up (floor=0, ankle=0.08, hips=0.95, head=1.66) | +Z = Forward
    """
    hips_y: float = 0.905
    hips_yaw: float = 0.0
    hips_pitch: float = 4.0
    hips_roll: float = 0.0

    torso_yaw: float = 0.0
    torso_pitch: float = 4.0
    torso_roll: float = 0.0

    head_yaw: float = 0.0
    head_pitch: float = 0.0
    head_roll: float = 0.0
    head_world_space: bool = True

    l_shoulder_lift: float = -4.0
    l_shoulder_fwd: float = 4.0
    r_shoulder_lift: float = -4.0
    r_shoulder_fwd: float = 4.0

    # Right Hand target & Sword Blade orientation
    r_hand_pos: Tuple[float, float, float] = (-0.22, 1.08, 0.34)
    r_elbow_pole: Tuple[float, float, float] = (-0.60, 0.86, -0.14)
    blade_dir: Optional[Tuple[float, float, float]] = (0.24, 0.48, 0.84)
    blade_edge: Optional[Tuple[float, float, float]] = (0.0, 1.0, 0.0)
    # Continuous Kiếm Hoa wrist sword-flourish spin angle (degrees) in the blade's cutting plane!
    blade_spin_deg: float = 0.0
    r_hand_up: Tuple[float, float, float] = (0.0, 1.0, 0.0)

    # Left Hand (Kiếm Quyết / balance)
    l_hand_pos: Tuple[float, float, float] = (0.26, 1.20, 0.28)
    l_elbow_pole: Tuple[float, float, float] = (0.64, 0.96, -0.18)
    l_hand_dir: Optional[Tuple[float, float, float]] = None
    l_hand_up: Tuple[float, float, float] = (0.0, 1.0, 0.0)

    # Feet & Knees
    l_foot_pos: Tuple[float, float, float] = (0.17, 0.08, 0.17)
    l_knee_pole: Tuple[float, float, float] = (0.24, 0.48, 0.72)
    l_foot_yaw: float = 12.0
    l_foot_pitch: float = 0.0
    l_foot_roll: float = 0.0

    r_foot_pos: Tuple[float, float, float] = (-0.20, 0.08, -0.17)
    r_knee_pole: Tuple[float, float, float] = (-0.32, 0.48, 0.42)
    r_foot_yaw: float = -24.0
    r_foot_pitch: float = 0.0
    r_foot_roll: float = 0.0

    # Pelvis-yaw rotating coordinate mode for graceful 180/360-deg aerial & pivot spins!
    local_body_space: bool = False
    feet_in_body_space: bool = False


def solve_pose(spec: PoseSpec, state: Optional[dict] = None) -> Tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    world_P = np.zeros((NUM_BONES, 3), dtype=np.float64)
    world_R = np.zeros((NUM_BONES, 3, 3), dtype=np.float64)
    for i in range(NUM_BONES):
        world_R[i] = np.eye(3)

    # 1. Hips & Spine chain (organic wave distribution: 22% / 34% / 44%)
    hips_pos = np.array([0.0, float(spec.hips_y), 0.0], dtype=np.float64)
    world_P[0] = hips_pos
    world_R[0] = euler_yxz(spec.hips_yaw, spec.hips_pitch, spec.hips_roll)

    for bi, w in ((1, 0.22), (2, 0.34), (3, 0.44)):
        parent = GAME_MIXAMO_20[bi][2]
        t_loc = np.array(GAME_MIXAMO_20[bi][3], dtype=np.float64)
        world_P[bi] = world_P[parent] + world_R[parent] @ t_loc
        world_R[bi] = world_R[parent] @ euler_yxz(spec.torso_yaw * w, spec.torso_pitch * w, spec.torso_roll * w)

    # 2. Neck & Head
    world_P[4] = world_P[3] + world_R[3] @ np.array(GAME_MIXAMO_20[4][3], dtype=np.float64)
    if spec.head_world_space:
        R_head_tgt = euler_yxz(spec.head_yaw, spec.head_pitch, spec.head_roll)
        world_R[4] = quat_to_mat(quat_slerp(mat_to_quat(world_R[3]), mat_to_quat(R_head_tgt), 0.45))
        world_P[5] = world_P[4] + world_R[4] @ np.array(GAME_MIXAMO_20[5][3], dtype=np.float64)
        world_R[5] = R_head_tgt
    else:
        world_R[4] = world_R[3] @ euler_yxz(spec.head_yaw * 0.45, spec.head_pitch * 0.45, spec.head_roll * 0.45)
        world_P[5] = world_P[4] + world_R[4] @ np.array(GAME_MIXAMO_20[5][3], dtype=np.float64)
        world_R[5] = world_R[4] @ euler_yxz(spec.head_yaw * 0.55, spec.head_pitch * 0.55, spec.head_roll * 0.55)

    # 3. Clavicles / Shoulders
    R_ls_rest = quat_to_mat(GAME_MIXAMO_20[6][4])
    world_P[6] = world_P[3] + world_R[3] @ np.array(GAME_MIXAMO_20[6][3], dtype=np.float64)
    world_R[6] = world_R[3] @ R_ls_rest @ rot_z(-spec.l_shoulder_fwd) @ rot_x(spec.l_shoulder_lift)

    R_rs_rest = quat_to_mat(GAME_MIXAMO_20[10][4])
    world_P[10] = world_P[3] + world_R[3] @ np.array(GAME_MIXAMO_20[10][3], dtype=np.float64)
    world_R[10] = world_R[3] @ R_rs_rest @ rot_z(spec.r_shoulder_fwd) @ rot_x(spec.r_shoulder_lift)

    # Pelvis-yaw space helpers for smooth 360-deg spins
    R_by = rot_y(spec.hips_yaw)
    def _hand_p(p):
        v = np.array(p, dtype=np.float64)
        return (R_by @ np.array([v[0], 0.0, v[2]]) + np.array([0.0, v[1], 0.0])) if spec.local_body_space else v
    def _hand_d(d):
        v = np.array(d, dtype=np.float64)
        return (R_by @ v) if spec.local_body_space else v
    def _foot_p(p):
        v = np.array(p, dtype=np.float64)
        return (R_by @ np.array([v[0], 0.0, v[2]]) + np.array([0.0, v[1], 0.0])) if spec.feet_in_body_space else v

    # 4. Left Arm IK (Kiếm Quyết / balance)
    world_P[7] = world_P[6] + world_R[6] @ np.array(GAME_MIXAMO_20[7][3], dtype=np.float64)
    l_elbow, l_hand = solve_two_bone_ik(
        world_P[7], _hand_p(spec.l_hand_pos), 0.26, 0.24, _hand_p(spec.l_elbow_pole), min_bend_deg=8.0,
    )
    world_P[8] = l_elbow
    world_P[9] = l_hand
    u_la = v_norm(l_elbow - world_P[7])
    u_lfa = v_norm(l_hand - l_elbow)
    l_hinge = np.cross(u_lfa, u_la)
    l_hinge = v_norm(l_hinge, np.array([0.0, 1.0, 0.0]))
    world_R[7] = build_arm_frame(u_la, l_hinge, is_right=False)
    world_R[8] = build_arm_frame(u_lfa, l_hinge, is_right=False)
    if spec.l_hand_dir is not None:
        lh_dir = v_norm(_hand_d(spec.l_hand_dir))
        R_lh_des = build_arm_frame(lh_dir, _hand_d(spec.l_hand_up), is_right=False)
        tw_rad, _ = decompose_swing_twist_y(world_R[8].T @ R_lh_des)
        if state is not None:
            tw_rad = _unwrap_angle(tw_rad, state.get("l_tw"))
            state["l_tw"] = tw_rad
        tw_arm = max(-math.radians(80.0), min(math.radians(80.0), tw_rad * 0.60))
        world_R[8] = world_R[8] @ rot_y(math.degrees(tw_arm))
        world_R[9] = R_lh_des
    else:
        world_R[9] = world_R[8].copy()

    # 5. Right Arm IK + Sword Blade-Tip & Kiếm Hoa Wrist-Flourish Solver
    world_P[11] = world_P[10] + world_R[10] @ np.array(GAME_MIXAMO_20[11][3], dtype=np.float64)
    r_elbow, r_hand = solve_two_bone_ik(
        world_P[11], _hand_p(spec.r_hand_pos), 0.26, 0.24, _hand_p(spec.r_elbow_pole), min_bend_deg=8.0,
    )
    world_P[12] = r_elbow
    world_P[13] = r_hand
    u_ra = v_norm(r_elbow - world_P[11])
    u_rfa = v_norm(r_hand - r_elbow)
    r_hinge = np.cross(u_ra, u_rfa)
    r_hinge = v_norm(r_hinge, np.array([0.0, 1.0, 0.0]))
    world_R[11] = build_arm_frame(u_ra, r_hinge, is_right=True)
    world_R[12] = build_arm_frame(u_rfa, r_hinge, is_right=True)

    if spec.blade_dir is not None:
        b_y = v_norm(_hand_d(spec.blade_dir))
        e_hint = _hand_d(spec.blade_edge if spec.blade_edge is not None else (0.0, 1.0, 0.0))
        e_x = e_hint - np.dot(e_hint, b_y) * b_y
        if float(np.linalg.norm(e_x)) < 1e-5:
            alt = np.array([0.0, 1.0, 0.0]) if abs(b_y[1]) < 0.9 else np.array([1.0, 0.0, 0.0])
            e_x = alt - np.dot(alt, b_y) * b_y
        e_x = v_norm(e_x)

        ref_edge = state.get("r_edge") if (state is not None and "r_edge" in state) else (world_R[12] @ SWORD_SOCKET_R[:, 0])
        if float(np.dot(e_x, ref_edge)) < 0.0:
            e_x = -e_x
        if state is not None:
            state["r_edge"] = e_x.copy()

        f_z = v_norm(np.cross(e_x, b_y))
        e_x = v_norm(np.cross(b_y, f_z))
        R_sword_world = np.column_stack([e_x, b_y, f_z])

        # Apply smooth Kiếm Hoa (sword-flower) wrist spin in the blade's cutting plane (around local +Z flat normal)!
        if abs(spec.blade_spin_deg) > 1e-4:
            R_sword_world = R_sword_world @ rot_z(spec.blade_spin_deg)

        R_rh_des = R_sword_world @ SWORD_SOCKET_R.T
        tw_rad, _ = decompose_swing_twist_y(world_R[12].T @ R_rh_des)
        if state is not None:
            tw_rad = _unwrap_angle(tw_rad, state.get("r_tw"))
            state["r_tw"] = tw_rad
        tw_fa = max(-math.radians(80.0), min(math.radians(80.0), tw_rad * 0.65))
        world_R[12] = world_R[12] @ rot_y(math.degrees(tw_fa))
        world_R[13] = R_rh_des
    else:
        world_R[13] = build_arm_frame(u_rfa, _hand_d(spec.r_hand_up), is_right=True)

    # 6. Leg IK
    leg_specs = [
        (14, 15, 16, spec.l_foot_pos, spec.l_knee_pole, spec.l_foot_yaw, spec.l_foot_pitch, spec.l_foot_roll),
        (17, 18, 19, spec.r_foot_pos, spec.r_knee_pole, spec.r_foot_yaw, spec.r_foot_pitch, spec.r_foot_roll),
    ]
    for i_up, i_leg, i_ft, ft_pos, kn_pole, ft_yaw, ft_pitch, ft_roll in leg_specs:
        world_P[i_up] = world_P[0] + world_R[0] @ np.array(GAME_MIXAMO_20[i_up][3], dtype=np.float64)
        fp_w = _foot_p(ft_pos)
        kp_w = _foot_p(kn_pole)
        fy_w = ft_yaw + (spec.hips_yaw if spec.feet_in_body_space else 0.0)
        knee_p, ankle_p = solve_two_bone_ik(world_P[i_up], fp_w, 0.42, 0.40, kp_w, min_bend_deg=5.0)
        world_P[i_leg] = knee_p
        world_P[i_ft] = ankle_p
        u_th = v_norm(knee_p - world_P[i_up])
        u_sh = v_norm(ankle_p - knee_p)
        k_axis = np.cross(u_th, u_sh)
        if float(np.linalg.norm(k_axis)) < 1e-4:
            k_axis = rot_y(fy_w) @ np.array([1.0, 0.0, 0.0])
        k_axis = v_norm(k_axis)
        world_R[i_up] = build_leg_frame(u_th, k_axis)
        world_R[i_leg] = build_leg_frame(u_sh, k_axis)
        world_R[i_ft] = build_foot_frame(fy_w, ft_pitch, ft_roll)

    # 7. World -> exact GAME_MIXAMO_20 local channel quaternions
    local_quats = np.zeros((NUM_BONES, 4), dtype=np.float64)
    for bi in range(NUM_BONES):
        parent = GAME_MIXAMO_20[bi][2]
        R_loc = world_R[bi] if parent < 0 else (world_R[parent].T @ world_R[bi])
        local_quats[bi] = mat_to_quat(R_loc)

    return hips_pos, local_quats, world_P, world_R


def fk_from_channels(hips_pos: np.ndarray, local_quats: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
    wP = np.zeros((NUM_BONES, 3), dtype=np.float64)
    wR = np.zeros((NUM_BONES, 3, 3), dtype=np.float64)
    wP[0] = hips_pos
    wR[0] = quat_to_mat(local_quats[0])
    for bi in range(1, NUM_BONES):
        p = GAME_MIXAMO_20[bi][2]
        wP[bi] = wP[p] + wR[p] @ np.array(GAME_MIXAMO_20[bi][3], dtype=np.float64)
        wR[bi] = wR[p] @ quat_to_mat(local_quats[bi])
    return wP, wR


def sword_kinematics_from_pose(wP: np.ndarray, wR: np.ndarray) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    p_rh, R_rh = wP[13], wR[13]
    hilt = p_rh + R_rh @ SWORD_SOCKET_POS
    R_sw = R_rh @ SWORD_SOCKET_R
    base = hilt + R_sw @ np.array([0.0, SWORD_BLADE_BASE_Y, 0.0])
    tip = hilt + R_sw @ np.array([0.0, SWORD_BLADE_TIP_Y, 0.0])
    return hilt, base, tip


# ==============================================================================
# 4. TRUE C2 CENTRIPETAL CATMULL-ROM SPLINE SAMPLER + SILKY SMOOTHING
# ==============================================================================

def _catmull_rom_val(p_m1: np.ndarray, p0: np.ndarray, p1: np.ndarray, p2: np.ndarray,
                     t_m1: float, t0: float, t1: float, t2: float, u: float, tension: float = 0.0) -> np.ndarray:
    """Non-uniform Cubic Hermite / Catmull-Rom spline with C1 velocity continuity across keyframes!

    Uses time-aware slopes so uneven keyframe timings never overshoot wildly while flowing
    silky-smoothly through every pose without stopping!
    """
    dt0 = max(1e-4, t0 - t_m1)
    dt1 = max(1e-4, t1 - t0)
    dt2 = max(1e-4, t2 - t1)

    # Time-weighted Catmull-Rom tangents scaled to segment duration dt1
    m0 = (1.0 - tension) * 0.5 * ((p1 - p0) / dt1 + (p0 - p_m1) / dt0) * dt1
    m1 = (1.0 - tension) * 0.5 * ((p2 - p1) / dt2 + (p1 - p0) / dt1) * dt1

    # Soft monotone limiter prevents wild overshoot on reversal keyframes while preserving flow!
    delta = p1 - p0
    lim = np.abs(delta) * 2.2 + 1e-6
    m0 = np.clip(m0, -lim, lim)
    m1 = np.clip(m1, -lim, lim)

    u2 = u * u
    u3 = u2 * u
    h00 = 2.0 * u3 - 3.0 * u2 + 1.0
    h10 = u3 - 2.0 * u2 + u
    h01 = -2.0 * u3 + 3.0 * u2
    h11 = u3 - u2
    return h00 * p0 + h10 * m0 + h01 * p1 + h11 * m1


def _time_warp(u: float, ease: str) -> float:
    """Optional timing warp inside a spline segment (keeps smooth velocity for 'flow'/'linear')."""
    u = max(0.0, min(1.0, float(u)))
    if ease in ("flow", "linear", "catmull"):
        return u
    if ease == "snap":
        # Accelerates into strike without killing entry velocity!
        return u + 0.24 * math.sin(u * math.pi)
    if ease == "whip":
        return u + 0.16 * math.sin(u * math.pi)
    if ease == "heavy_drop":
        return u - 0.22 * math.sin(u * math.pi)
    if ease == "ease_out":
        return 1.0 - (1.0 - u) ** 1.75
    if ease == "ease_in":
        return u ** 1.75
    if ease == "ease_in_out":
        return u * u * (3.0 - 2.0 * u)
    return u


@dataclass
class Keyframe:
    t: float
    pose: PoseSpec
    ease: str = "flow"   # "flow" = C2 silk continuity; "snap"/"whip"/"heavy_drop"/"hold"
    tension: float = 0.0 # 0.0 = fluid Catmull-Rom, 0.65 = crisper stop/hold


@dataclass
class AnimationClip:
    name: str
    slot: str
    duration: float
    loop: bool
    description: str
    keyframes: List[Keyframe]
    tags: List[str] = field(default_factory=list)
    hit_window: Optional[Tuple[float, float]] = None
    allow_ground_stab: bool = False
    smooth_passes: int = 2  # Gentle SLERP polish passes for butter-smooth 60 FPS joints
    times: np.ndarray = field(default_factory=lambda: np.zeros(0))
    hips_trans: np.ndarray = field(default_factory=lambda: np.zeros((0, 3)))
    rot_tracks: np.ndarray = field(default_factory=lambda: np.zeros((NUM_BONES, 0, 4)))


def _pose_to_vec(p: PoseSpec) -> np.ndarray:
    """Pack continuous PoseSpec attributes into a single 38-float vector for C2 spline evaluation."""
    b_dir = v_norm(np.array(p.blade_dir if p.blade_dir is not None else (0.0, 1.0, 0.0)))
    b_edg = v_norm(np.array(p.blade_edge if p.blade_edge is not None else (0.0, 1.0, 0.0)))
    lh_d = v_norm(np.array(p.l_hand_dir if p.l_hand_dir is not None else (0.0, 0.0, 1.0)))
    return np.array([
        p.hips_y, p.hips_yaw, p.hips_pitch, p.hips_roll,
        p.torso_yaw, p.torso_pitch, p.torso_roll,
        p.head_yaw, p.head_pitch, p.head_roll,
        p.l_shoulder_lift, p.l_shoulder_fwd, p.r_shoulder_lift, p.r_shoulder_fwd,
        *p.r_hand_pos, *p.r_elbow_pole,
        *b_dir, *b_edg, p.blade_spin_deg,
        *p.l_hand_pos, *p.l_elbow_pole, *lh_d,
        *p.l_foot_pos, *p.l_knee_pole, p.l_foot_yaw, p.l_foot_pitch, p.l_foot_roll,
        *p.r_foot_pos, *p.r_knee_pole, p.r_foot_yaw, p.r_foot_pitch, p.r_foot_roll,
    ], dtype=np.float64)


def _vec_to_pose(v: np.ndarray, ref: PoseSpec) -> PoseSpec:
    b_dir = tuple(v_norm(v[20:23]).tolist()) if ref.blade_dir is not None else None
    b_edg = tuple(v_norm(v[23:26]).tolist()) if ref.blade_edge is not None else None
    lh_d = tuple(v_norm(v[33:36]).tolist()) if ref.l_hand_dir is not None else None
    return PoseSpec(
        hips_y=float(v[0]), hips_yaw=float(v[1]), hips_pitch=float(v[2]), hips_roll=float(v[3]),
        torso_yaw=float(v[4]), torso_pitch=float(v[5]), torso_roll=float(v[6]),
        head_yaw=float(v[7]), head_pitch=float(v[8]), head_roll=float(v[9]),
        head_world_space=ref.head_world_space,
        l_shoulder_lift=float(v[10]), l_shoulder_fwd=float(v[11]),
        r_shoulder_lift=float(v[12]), r_shoulder_fwd=float(v[13]),
        r_hand_pos=(float(v[14]), float(v[15]), float(v[16])),
        r_elbow_pole=(float(v[17]), float(v[18]), float(v[19])),
        blade_dir=b_dir, blade_edge=b_edg, blade_spin_deg=float(v[26]),
        r_hand_up=ref.r_hand_up,
        l_hand_pos=(float(v[27]), float(v[28]), float(v[29])),
        l_elbow_pole=(float(v[30]), float(v[31]), float(v[32])),
        l_hand_dir=lh_d, l_hand_up=ref.l_hand_up,
        l_foot_pos=(float(v[36]), float(v[37]), float(v[38])),
        l_knee_pole=(float(v[39]), float(v[40]), float(v[41])),
        l_foot_yaw=float(v[42]), l_foot_pitch=float(v[43]), l_foot_roll=float(v[44]),
        r_foot_pos=(float(v[45]), float(v[46]), float(v[47])),
        r_knee_pole=(float(v[48]), float(v[49]), float(v[50])),
        r_foot_yaw=float(v[51]), r_foot_pitch=float(v[52]), r_foot_roll=float(v[53]),
        local_body_space=ref.local_body_space,
        feet_in_body_space=ref.feet_in_body_space,
    )


def sample_clip(clip: AnimationClip, fps: int = 60) -> AnimationClip:
    """Sample clip at 60 FPS using C2 Catmull-Rom splines + gentle SLERP motion polish."""
    kfs = sorted(clip.keyframes, key=lambda k: k.t)
    nk = len(kfs)
    vecs = [_pose_to_vec(k.pose) for k in kfs]
    kt = [float(k.t) for k in kfs]

    # Make consecutive blade_edge & blade_dir vectors hemisphere-aligned before spline evaluation
    for i in range(1, nk):
        if float(np.dot(vecs[i - 1][23:26], vecs[i][23:26])) < 0.0:
            vecs[i][23:26] = -vecs[i][23:26]

    n_frames = max(2, int(round(clip.duration * fps)) + 1)
    dt = clip.duration / float(n_frames - 1)
    times = np.array([i * dt for i in range(n_frames)], dtype=np.float32)

    hips_trans = np.zeros((n_frames, 3), dtype=np.float32)
    rot_tracks = np.zeros((NUM_BONES, n_frames, 4), dtype=np.float32)
    solve_state: dict = {}

    for fi, t in enumerate(times):
        tf = float(t)
        if tf <= kt[0]:
            pose = kfs[0].pose
        elif tf >= kt[-1]:
            pose = kfs[-1].pose
        else:
            seg = 0
            for s in range(nk - 1):
                if kt[s] <= tf <= kt[s + 1]:
                    seg = s
                    break
            span = max(1e-6, kt[seg + 1] - kt[seg])
            u_raw = (tf - kt[seg]) / span
            u = _time_warp(u_raw, kfs[seg + 1].ease)

            # Periodic wraparound neighbors for C2 seamless loops!
            if clip.loop and nk >= 3:
                i_m1 = (seg - 1) if seg > 0 else (nk - 2)
                i_p2 = (seg + 2) if (seg + 2) < nk else 1
                t_m1 = kt[i_m1] if seg > 0 else (kt[0] - (kt[-1] - kt[-2]))
                t_p2 = kt[i_p2] if (seg + 2) < nk else (kt[-1] + (kt[1] - kt[0]))
            else:
                i_m1 = max(0, seg - 1)
                i_p2 = min(nk - 1, seg + 2)
                t_m1 = kt[i_m1] if seg > 0 else (kt[0] - span)
                t_p2 = kt[i_p2] if (seg + 2) < nk else (kt[-1] + span)

            tens = max(kfs[seg].tension, kfs[seg + 1].tension)
            v_eval = _catmull_rom_val(
                vecs[i_m1], vecs[seg], vecs[seg + 1], vecs[i_p2],
                t_m1, kt[seg], kt[seg + 1], t_p2, u, tension=tens,
            )
            # Keep blade_dir slerped cleanly if spline chord got very short during a big swing
            if float(np.linalg.norm(v_eval[20:23])) < 0.25:
                v_eval[20:23] = v_norm(vecs[seg][20:23] * (1.0 - u) + vecs[seg + 1][20:23] * u)
            pose = _vec_to_pose(v_eval, kfs[seg].pose)

        h_pos, l_quats, _, _ = solve_pose(pose, state=solve_state)

        # Auto-grounding floor clamp (allows full Khinh Công leaps while blocking underground foot sink!)
        wP, _ = fk_from_channels(h_pos, l_quats)
        min_foot_y = min(float(wP[16, 1]), float(wP[19, 1]))
        if min_foot_y < ANKLE_GROUND_Y - 0.002:
            h_pos[1] += (ANKLE_GROUND_Y - min_foot_y)

        if fi > 0:
            for bi in range(NUM_BONES):
                if float(np.dot(rot_tracks[bi, fi - 1], l_quats[bi])) < 0.0:
                    l_quats[bi] = -l_quats[bi]

        hips_trans[fi] = h_pos.astype(np.float32)
        rot_tracks[:, fi, :] = l_quats.astype(np.float32)

    # Gentle SLERP motion-polish pass (eliminates micro-kinks while preserving impact sharpness!)
    for _ in range(max(0, clip.smooth_passes)):
        if n_frames <= 4:
            break
        h_new = hips_trans.copy()
        r_new = rot_tracks.copy()
        for fi in range(n_frames):
            if not clip.loop and (fi == 0 or fi == n_frames - 1):
                continue
            f_prev = (fi - 1) if fi > 0 else (n_frames - 2)
            f_next = (fi + 1) if fi < n_frames - 1 else 1
            h_new[fi] = 0.22 * hips_trans[f_prev] + 0.56 * hips_trans[fi] + 0.22 * hips_trans[f_next]
            for bi in range(NUM_BONES):
                qp = rot_tracks[bi, f_prev].astype(np.float64)
                qc = rot_tracks[bi, fi].astype(np.float64)
                qn = rot_tracks[bi, f_next].astype(np.float64)
                if np.dot(qp, qc) < 0.0:
                    qp = -qp
                if np.dot(qn, qc) < 0.0:
                    qn = -qn
                qm = 0.22 * qp + 0.56 * qc + 0.22 * qn
                qm /= max(float(np.linalg.norm(qm)), 1e-12)
                r_new[bi, fi] = qm.astype(np.float32)
        hips_trans = h_new
        rot_tracks = r_new

    # Re-enforce floor clamp & hemisphere continuity after polish
    for fi in range(n_frames):
        wP, _ = fk_from_channels(hips_trans[fi].astype(np.float64), rot_tracks[:, fi, :].astype(np.float64))
        min_foot_y = min(float(wP[16, 1]), float(wP[19, 1]))
        if min_foot_y < ANKLE_GROUND_Y - 0.002:
            hips_trans[fi, 1] += float(ANKLE_GROUND_Y - min_foot_y)
        if fi > 0:
            for bi in range(NUM_BONES):
                if float(np.dot(rot_tracks[bi, fi - 1], rot_tracks[bi, fi])) < 0.0:
                    rot_tracks[bi, fi] = -rot_tracks[bi, fi]

    if clip.loop and n_frames > 2:
        hips_trans[-1] = hips_trans[0]
        for bi in range(NUM_BONES):
            q_end = rot_tracks[bi, 0].copy()
            if float(np.dot(rot_tracks[bi, -2], q_end)) < 0.0:
                q_end = -q_end
            rot_tracks[bi, -1] = q_end

    clip.times = times
    clip.hips_trans = hips_trans
    clip.rot_tracks = rot_tracks
    return clip


def verify_clip(clip: AnimationClip) -> Dict[str, object]:
    n = len(clip.times)
    errors: List[str] = []
    min_foot_y, max_foot_y = 99.0, -99.0
    min_blade_tip_y, max_blade_z = 99.0, -99.0
    max_q_err, max_step_deg = 0.0, 0.0

    for fi in range(n):
        h = clip.hips_trans[fi].astype(np.float64)
        qs = clip.rot_tracks[:, fi, :].astype(np.float64)
        if abs(h[0]) > 1e-5 or abs(h[2]) > 1e-5:
            errors.append(f"f{fi}: Hips X/Z not in-place ({h[0]:.4f}, {h[2]:.4f})")
        for bi in range(NUM_BONES):
            err = abs(float(np.linalg.norm(qs[bi])) - 1.0)
            max_q_err = max(max_q_err, err)
            if err > 1e-4:
                errors.append(f"f{fi} bone {bi}: |q| err={err:.5f}")
            if fi > 0:
                d = float(np.dot(clip.rot_tracks[bi, fi - 1], qs[bi]))
                if d < -1e-4:
                    errors.append(f"f{fi} bone {bi}: quaternion flip dot={d:.4f}")
                ang = math.degrees(2.0 * math.acos(max(-1.0, min(1.0, abs(d)))))
                max_step_deg = max(max_step_deg, ang)

        wP, wR = fk_from_channels(h, qs)
        if wP[5, 1] <= wP[0, 1] + 0.30:
            errors.append(f"f{fi}: Head.y={wP[5,1]:.3f} too close to Hips.y={wP[0,1]:.3f}")
        st_y = min(float(wP[16, 1]), float(wP[19, 1]))
        min_foot_y = min(min_foot_y, st_y)
        max_foot_y = max(max_foot_y, st_y)
        if st_y < 0.055:
            errors.append(f"f{fi}: Stance ankle sank below floor ({st_y:.3f}m)")

        _, b_base, b_tip = sword_kinematics_from_pose(wP, wR)
        min_blade_tip_y = min(min_blade_tip_y, float(b_tip[1]))
        max_blade_z = max(max_blade_z, float(b_tip[2]))
        if not clip.allow_ground_stab and b_tip[1] < 0.04:
            errors.append(f"f{fi}: Sword tip hit ground (y={b_tip[1]:.3f}m)")

        seg = b_tip - b_base
        seg_l2 = max(1e-8, float(np.dot(seg, seg)))
        head_c = wP[5] + np.array([0.0, 0.06, 0.02])
        u_h = max(0.0, min(1.0, float(np.dot(head_c - b_base, seg)) / seg_l2))
        d_head = float(np.linalg.norm((b_base + u_h * seg) - head_c))
        if d_head < 0.105:
            errors.append(f"f{fi}: Blade clips head (dist={d_head:.3f}m)")

    return {
        "ok": len(errors) == 0,
        "errors": errors,
        "frames": n,
        "stance_y": (round(min_foot_y, 3), round(max_foot_y, 3)),
        "blade_tip_min_y": round(min_blade_tip_y, 3),
        "blade_tip_max_z": round(max_blade_z, 3),
        "max_q_err": max_q_err,
        "max_step_deg": round(max_step_deg, 1),
    }


# ==============================================================================
# 5. STANDALONE GLB EXPORTER & SAFE HUMANOID_ANIMS.GLB MERGER
# ==============================================================================

def _pack_glb(gltf_dict: dict, bin_bytes: bytearray) -> bytes:
    while len(bin_bytes) % 4 != 0:
        bin_bytes.append(0)
    gltf_dict["buffers"][0]["byteLength"] = len(bin_bytes)
    js = json.dumps(gltf_dict, separators=(",", ":")).encode("utf-8")
    while len(js) % 4 != 0:
        js += b" "
    total_len = 12 + 8 + len(js) + 8 + len(bin_bytes)
    return (
        struct.pack("<4sII", b"glTF", 2, total_len)
        + struct.pack("<I4s", len(js), b"JSON") + js
        + struct.pack("<I4s", len(bin_bytes), b"BIN\0") + bytes(bin_bytes)
    )


def _append_acc(d: dict, data: bytearray, arr: np.ndarray, acc_type: str) -> int:
    a = np.ascontiguousarray(arr, dtype="<f4")
    while len(data) % 4 != 0:
        data.append(0)
    off = len(data)
    data.extend(a.tobytes())
    bv_idx = len(d["bufferViews"])
    d["bufferViews"].append({"buffer": 0, "byteOffset": off, "byteLength": a.nbytes})
    acc = {"bufferView": bv_idx, "componentType": 5126, "count": len(a), "type": acc_type}
    if acc_type == "SCALAR":
        acc["min"] = [float(a.min())]
        acc["max"] = [float(a.max())]
    else:
        acc["min"] = [float(x) for x in a.min(axis=0)]
        acc["max"] = [float(x) for x in a.max(axis=0)]
    acc_idx = len(d["accessors"])
    d["accessors"].append(acc)
    return acc_idx


def _encode_clip_into_gltf(d: dict, data: bytearray, clip: AnimationClip, name_to_node: Dict[str, int]) -> dict:
    time_acc = _append_acc(d, data, clip.times, "SCALAR")
    samplers, channels = [], []

    hips_node = name_to_node["mixamorig:Hips"]
    hips_acc = _append_acc(d, data, clip.hips_trans, "VEC3")
    samplers.append({"input": time_acc, "interpolation": "LINEAR", "output": hips_acc})
    channels.append({"sampler": 0, "target": {"node": hips_node, "path": "translation"}})

    for bi, bname in enumerate(BONE_NAMES):
        ni = name_to_node[bname]
        rot_acc = _append_acc(d, data, clip.rot_tracks[bi], "VEC4")
        s_idx = len(samplers)
        samplers.append({"input": time_acc, "interpolation": "LINEAR", "output": rot_acc})
        channels.append({"sampler": s_idx, "target": {"node": ni, "path": "rotation"}})

    anim_obj = {
        "name": clip.name,
        "channels": channels,
        "samplers": samplers,
        "extras": {"slot": clip.slot, "loop": clip.loop, "tags": clip.tags, "description": clip.description},
    }
    if clip.hit_window is not None:
        anim_obj["extras"]["events"] = {"hit_window": [round(clip.hit_window[0], 3), round(clip.hit_window[1], 3)]}
    return anim_obj


def export_standalone_glb(clips: Sequence[AnimationClip], out_path: Path) -> None:
    children_map: Dict[int, List[int]] = {i: [] for i in range(NUM_BONES)}
    for idx, _nm, parent, _t, _r in GAME_MIXAMO_20:
        if parent >= 0:
            children_map[parent].append(idx)

    nodes = []
    for idx, nm, _p, t, r in GAME_MIXAMO_20:
        nd = {"name": nm, "translation": list(t), "rotation": list(r)}
        if children_map[idx]:
            nd["children"] = children_map[idx]
        nodes.append(nd)
    nodes.append({"name": "Humanoid_Armature", "children": [0]})

    d = {
        "asset": {"version": "2.0", "generator": "WuxingSkills rig_core.py (C2-Spline GAME_MIXAMO_20)"},
        "scene": 0,
        "scenes": [{"name": "Scene", "nodes": [NUM_BONES]}],
        "nodes": nodes,
        "skins": [{"name": "GAME_MIXAMO_20", "joints": list(range(NUM_BONES)), "skeleton": 0}],
        "animations": [],
        "accessors": [],
        "bufferViews": [],
        "buffers": [{"byteLength": 0}],
    }
    data = bytearray()
    name_to_node = {b[1]: b[0] for b in GAME_MIXAMO_20}
    for clip in clips:
        d["animations"].append(_encode_clip_into_gltf(d, data, clip, name_to_node))

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(_pack_glb(d, data))


def merge_clips_into_bank(bank_path: Path, clips: Sequence[AnimationClip]) -> Tuple[int, int, int]:
    raw = bank_path.read_bytes()
    jl = struct.unpack_from("<I", raw, 12)[0]
    d = json.loads(raw[20:20 + jl])
    bin_off = 20 + jl + 8
    bin_len = struct.unpack_from("<I", raw, 20 + jl)[0]
    data = bytearray(raw[bin_off:bin_off + bin_len])

    name_to_node = {n.get("name", ""): i for i, n in enumerate(d["nodes"])}
    missing = [bn for bn in BONE_NAMES if bn not in name_to_node]
    if missing:
        raise RuntimeError(f"Animation bank {bank_path} is missing bones: {missing}")

    existing_by_name = {a.get("name", ""): idx for idx, a in enumerate(d.get("animations", []))}
    replaced, added = 0, 0
    for clip in clips:
        anim_obj = _encode_clip_into_gltf(d, data, clip, name_to_node)
        if clip.name in existing_by_name:
            d["animations"][existing_by_name[clip.name]] = anim_obj
            replaced += 1
        else:
            d["animations"].append(anim_obj)
            added += 1

    d, data = _compact_glb_buffers(d, data)
    bank_path.write_bytes(_pack_glb(d, data))
    return replaced, added, len(d["animations"])


def _compact_glb_buffers(d: dict, old_data: bytearray) -> Tuple[dict, bytearray]:
    used_acc = set()
    for m in d.get("meshes", []):
        for p in m.get("primitives", []):
            for v in p.get("attributes", {}).values():
                used_acc.add(v)
            if "indices" in p:
                used_acc.add(p["indices"])
    for sk in d.get("skins", []):
        if "inverseBindMatrices" in sk:
            used_acc.add(sk["inverseBindMatrices"])
    for a in d.get("animations", []):
        for s in a.get("samplers", []):
            used_acc.add(s["input"])
            used_acc.add(s["output"])

    used_bv = {d["accessors"][ai]["bufferView"] for ai in used_acc if "bufferView" in d["accessors"][ai]}
    for im in d.get("images", []):
        if "bufferView" in im:
            used_bv.add(im["bufferView"])

    new_data = bytearray()
    bv_remap, new_bvs = {}, []
    for old_i in sorted(used_bv):
        bv = copy.deepcopy(d["bufferViews"][old_i])
        o_off, o_len = bv.get("byteOffset", 0), bv["byteLength"]
        while len(new_data) % 4 != 0:
            new_data.append(0)
        bv["byteOffset"] = len(new_data)
        new_data.extend(old_data[o_off:o_off + o_len])
        bv_remap[old_i] = len(new_bvs)
        new_bvs.append(bv)

    acc_remap, new_accs = {}, []
    for old_i in sorted(used_acc):
        ac = copy.deepcopy(d["accessors"][old_i])
        if "bufferView" in ac:
            ac["bufferView"] = bv_remap[ac["bufferView"]]
        acc_remap[old_i] = len(new_accs)
        new_accs.append(ac)

    for m in d.get("meshes", []):
        for p in m.get("primitives", []):
            for k in list(p.get("attributes", {}).keys()):
                p["attributes"][k] = acc_remap[p["attributes"][k]]
            if "indices" in p:
                p["indices"] = acc_remap[p["indices"]]
    for sk in d.get("skins", []):
        if "inverseBindMatrices" in sk:
            sk["inverseBindMatrices"] = acc_remap[sk["inverseBindMatrices"]]
    for a in d.get("animations", []):
        for s in a.get("samplers", []):
            s["input"] = acc_remap[s["input"]]
            s["output"] = acc_remap[s["output"]]
    for im in d.get("images", []):
        if "bufferView" in im:
            im["bufferView"] = bv_remap[im["bufferView"]]

    d["bufferViews"] = new_bvs
    d["accessors"] = new_accs
    return d, new_data


# ==============================================================================
# 6. FAST CRISP SOLID 3D MANNEQUIN + FULL SWORD + RIBBON TRAIL RENDERER
# ==============================================================================

def _load_glb_mesh(path: Path):
    b = path.read_bytes()
    jl = struct.unpack_from("<I", b, 12)[0]
    d = json.loads(b[20:20 + jl])
    buf = b[20 + jl + 8:]

    def acc(i):
        a = d["accessors"][i]
        bv = d["bufferViews"][a["bufferView"]]
        dt = {5121: np.uint8, 5123: "<u2", 5125: "<u4", 5126: "<f4"}[a["componentType"]]
        nc = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[a["type"]]
        off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
        arr = np.frombuffer(buf, dtype=dt, count=a["count"] * nc, offset=off)
        return arr.reshape(a["count"], nc) if nc > 1 else arr

    mats = d.get("materials", [])
    prims = []
    for pr in d["meshes"][0]["primitives"]:
        V = acc(pr["attributes"]["POSITION"]).astype(np.float32)
        F = acc(pr["indices"]).astype(np.int32).reshape(-1, 3)
        mi = pr.get("material", 0)
        m = mats[mi] if mi < len(mats) else {}
        base = np.array(m.get("pbrMetallicRoughness", {}).get("baseColorFactor", [0.8, 0.8, 0.8, 1.0])[:3], dtype=np.float32)
        prims.append((V, F, base))
    return prims


def _make_taper_tube(p0: np.ndarray, p1: np.ndarray, r0: float, r1: float, segs: int = 8):
    d = p1 - p0
    L = float(np.linalg.norm(d))
    if L < 1e-5:
        return np.zeros((0, 3), dtype=np.float32), np.zeros((0, 3), dtype=np.int32)
    y = d / L
    ref = np.array([1.0, 0.0, 0.0]) if abs(y[0]) < 0.85 else np.array([0.0, 0.0, 1.0])
    x = v_norm(np.cross(y, ref))
    z = np.cross(x, y)
    ang = np.linspace(0.0, 2.0 * math.pi, segs, endpoint=False)
    ca, sa = np.cos(ang)[:, None], np.sin(ang)[:, None]
    ring0 = p0[None, :] + (ca * x[None, :] + sa * z[None, :]) * r0
    ring1 = p1[None, :] + (ca * x[None, :] + sa * z[None, :]) * r1
    V = np.vstack([ring0, ring1, p0[None, :], p1[None, :]]).astype(np.float32)
    F = []
    for i in range(segs):
        j = (i + 1) % segs
        F.append((i, j, segs + j))
        F.append((i, segs + j, segs + i))
        F.append((2 * segs, j, i))
        F.append((2 * segs + 1, segs + i, segs + j))
    return V, np.asarray(F, dtype=np.int32)


def _build_solid_mannequin(wP: np.ndarray, wR: np.ndarray):
    """Build crisp, solid articulated Wuxia figure (indigo tunic, gold sash, porcelain head/hands)."""
    segs_spec = [
        (0, 1, 0.125, 0.115, [0.16, 0.24, 0.44]),   # Pelvis / sash
        (1, 2, 0.115, 0.130, [0.14, 0.28, 0.56]),   # Lower torso
        (2, 3, 0.132, 0.148, [0.16, 0.34, 0.66]),   # Chest
        (3, 4, 0.060, 0.055, [0.76, 0.58, 0.46]),   # Neck
        (6, 7, 0.068, 0.058, [0.18, 0.36, 0.68]),   # L Shoulder
        (7, 8, 0.056, 0.044, [0.16, 0.34, 0.64]),   # L UpperArm
        (8, 9, 0.044, 0.032, [0.76, 0.58, 0.46]),   # L ForeArm
        (10, 11, 0.068, 0.058, [0.18, 0.36, 0.68]), # R Shoulder
        (11, 12, 0.056, 0.044, [0.16, 0.34, 0.64]), # R UpperArm
        (12, 13, 0.044, 0.032, [0.76, 0.58, 0.46]), # R ForeArm
        (14, 15, 0.080, 0.058, [0.12, 0.18, 0.36]), # L Thigh
        (15, 16, 0.056, 0.038, [0.10, 0.14, 0.26]), # L Shin
        (17, 18, 0.080, 0.058, [0.12, 0.18, 0.36]), # R Thigh
        (18, 19, 0.056, 0.038, [0.10, 0.14, 0.26]), # R Shin
    ]
    out = []
    for a, b, r0, r1, col in segs_spec:
        V, F = _make_taper_tube(wP[a], wP[b], r0, r1, segs=8)
        out.append((V, F, np.array(col, dtype=np.float32)))
    # Head + dark hair crown
    p_head_top = wP[5] + wR[5] @ np.array([0.0, 0.17, 0.0])
    V, F = _make_taper_tube(wP[5] - wR[5] @ np.array([0.0, 0.03, 0.0]), p_head_top, 0.088, 0.072, segs=10)
    out.append((V, F, np.array([0.78, 0.60, 0.48], dtype=np.float32)))
    # Feet forward blocks
    for fi in (16, 19):
        fwd_ft = -wR[fi][:, 2]
        p_toe = wP[fi] + fwd_ft * 0.18 + wR[fi][:, 1] * 0.04
        V, F = _make_taper_tube(wP[fi], p_toe, 0.042, 0.028, segs=6)
        out.append((V, F, np.array([0.14, 0.12, 0.14], dtype=np.float32)))
    return out


def _draw_line_2d(img: np.ndarray, p0, p1, color, thickness: int = 2):
    H, W, _ = img.shape
    x0, y0 = float(p0[0]), float(p0[1])
    x1, y1 = float(p1[0]), float(p1[1])
    steps = max(2, int(math.hypot(x1 - x0, y1 - y0)) + 1)
    xs = np.round(np.linspace(x0, x1, steps)).astype(np.int32)
    ys = np.round(np.linspace(y0, y1, steps)).astype(np.int32)
    for dy in range(-thickness // 2, thickness // 2 + 1):
        for dx in range(-thickness // 2, thickness // 2 + 1):
            xx, yy = xs + dx, ys + dy
            ok = (xx >= 0) & (xx < W) & (yy >= 0) & (yy < H)
            img[yy[ok], xx[ok]] = color


def render_clip_strip(
    clip: AnimationClip,
    body_glb: Path,
    sword_glb: Path,
    n_panels: int = 7,
    panel_w: int = 190,
    panel_h: int = 230,
    cam_pos: Tuple[float, float, float] = (1.65, 1.38, 2.75),
    cam_tgt: Tuple[float, float, float] = (0.0, 1.02, 0.08),
) -> np.ndarray:
    """Fast, crystal-clear solid 3D figure + full solid sword + luminous blade ribbon strip."""
    sword_prims = _load_glb_mesh(sword_glb)
    n_f = len(clip.times)
    sample_idx = [int(round(k * (n_f - 1) / max(1, n_panels - 1))) for k in range(n_panels)]

    all_base, all_tip = [], []
    for fi in range(n_f):
        wP, wR = fk_from_channels(clip.hips_trans[fi].astype(np.float64), clip.rot_tracks[:, fi, :].astype(np.float64))
        _, b, t = sword_kinematics_from_pose(wP, wR)
        all_base.append(b)
        all_tip.append(t)
    all_base = np.asarray(all_base, dtype=np.float64)
    all_tip = np.asarray(all_tip, dtype=np.float64)

    eye = np.array(cam_pos, dtype=np.float32)
    tgt = np.array(cam_tgt, dtype=np.float32)
    fwd = tgt - eye; fwd /= np.linalg.norm(fwd)
    right = np.cross(fwd, [0.0, 1.0, 0.0]); right /= np.linalg.norm(right)
    up = np.cross(right, fwd)
    f_len = 0.5 * panel_h / math.tan(math.radians(40.0 * 0.5))
    L1 = np.array([0.45, 0.70, 0.55], dtype=np.float32); L1 /= np.linalg.norm(L1)

    def proj(pts):
        rel = pts.astype(np.float32) - eye
        cz = rel @ fwd
        cx = rel @ right
        cy = rel @ up
        px = panel_w * 0.5 + (cx / np.maximum(cz, 1e-4)) * f_len
        py = panel_h * 0.5 - (cy / np.maximum(cz, 1e-4)) * f_len
        return px, py, cz

    panels = []
    for fi in sample_idx:
        img = np.full((panel_h, panel_w, 3), [0.11, 0.13, 0.19], dtype=np.float32)
        zbuf = np.full((panel_h, panel_w), 1e9, dtype=np.float32)

        # Ground plane grid (Y=0)
        for gx in (-0.8, -0.4, 0.0, 0.4, 0.8):
            p0 = proj(np.array([[gx, 0.0, -0.8]]))[0:2]
            p1 = proj(np.array([[gx, 0.0,  1.2]]))[0:2]
            _draw_line_2d(img, (p0[0][0], p0[1][0]), (p1[0][0], p1[1][0]), [0.20, 0.23, 0.31], 1)
        for gz in (-0.8, -0.4, 0.0, 0.4, 0.8, 1.2):
            p0 = proj(np.array([[-0.8, 0.0, gz]]))[0:2]
            p1 = proj(np.array([[ 0.8, 0.0, gz]]))[0:2]
            _draw_line_2d(img, (p0[0][0], p0[1][0]), (p1[0][0], p1[1][0]),
                          [0.32, 0.44, 0.36] if abs(gz) < 1e-3 else [0.20, 0.23, 0.31], 1)

        wP, wR = fk_from_channels(clip.hips_trans[fi].astype(np.float64), clip.rot_tracks[:, fi, :].astype(np.float64))
        draw_list = _build_solid_mannequin(wP, wR)

        # Full solid sword in exact engine socket matrix
        M_rh = np.eye(4); M_rh[:3, :3] = wR[13]; M_rh[:3, 3] = wP[13]
        M_off = np.eye(4); M_off[:3, :3] = SWORD_SOCKET_R; M_off[:3, 3] = SWORD_SOCKET_POS
        M_sw = M_rh @ M_off
        for V, F, base in sword_prims:
            V_sw = (V.astype(np.float64) @ M_sw[:3, :3].T) + M_sw[:3, 3]
            draw_list.append((V_sw.astype(np.float32), F, base))

        # Luminous sword-slash ribbon mesh across last 16 frames!
        if fi > 1:
            t0 = max(0, fi - 16)
            nb = fi - t0 + 1
            Vr = np.vstack([all_base[t0:fi + 1], all_tip[t0:fi + 1]]).astype(np.float32)
            Fr = []
            for k in range(nb - 1):
                Fr.append((k, k + 1, nb + k + 1))
                Fr.append((k, nb + k + 1, nb + k))
            draw_list.append((Vr, np.asarray(Fr, dtype=np.int32), np.array([0.38, 0.82, 1.0], dtype=np.float32)))

        for V, F, base in draw_list:
            if len(F) == 0:
                continue
            px, py, cz = proj(V)
            i0, i1, i2 = F[:, 0], F[:, 1], F[:, 2]
            z0, z1, z2 = cz[i0], cz[i1], cz[i2]
            x0, y0 = px[i0], py[i0]; x1, y1 = px[i1], py[i1]; x2, y2 = px[i2], py[i2]
            area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0)
            ok = (z0 > 0.1) & (z1 > 0.1) & (z2 > 0.1) & (np.abs(area) > 0.05)
            idx = np.where(ok)[0]
            if len(idx) == 0:
                continue
            v0, v1, v2 = V[i0[idx]], V[i1[idx]], V[i2[idx]]
            fn = np.cross(v1 - v0, v2 - v0)
            fn /= np.maximum(np.linalg.norm(fn, axis=1, keepdims=True), 1e-8)
            view = eye - (v0 + v1 + v2) / 3.0
            ndv = (fn * view).sum(1)
            fn = np.where(ndv[:, None] < 0, -fn, fn)
            diff = np.clip(fn @ L1, 0.0, 1.0) * 0.68 + 0.34
            tcol = np.power(np.clip(base[None, :] * diff[:, None], 0.0, 1.0), 1.0 / 2.2)

            minx = np.maximum(0, np.floor(np.minimum.reduce([x0[idx], x1[idx], x2[idx]])).astype(np.int32))
            maxx = np.minimum(panel_w - 1, np.ceil(np.maximum.reduce([x0[idx], x1[idx], x2[idx]])).astype(np.int32))
            miny = np.maximum(0, np.floor(np.minimum.reduce([y0[idx], y1[idx], y2[idx]])).astype(np.int32))
            maxy = np.minimum(panel_h - 1, np.ceil(np.maximum.reduce([y0[idx], y1[idx], y2[idx]])).astype(np.int32))
            vis = (minx <= maxx) & (miny <= maxy)
            for k in np.where(vis)[0]:
                ti = idx[k]
                inv_a = 1.0 / area[ti]
                xs = np.arange(minx[k], maxx[k] + 1, dtype=np.float32) + 0.5
                ys = np.arange(miny[k], maxy[k] + 1, dtype=np.float32) + 0.5
                gx, gy = np.meshgrid(xs, ys)
                w0 = ((x1[ti] - gx) * (y2[ti] - gy) - (y1[ti] - gy) * (x2[ti] - gx)) * inv_a
                w1 = ((x2[ti] - gx) * (y0[ti] - gy) - (y2[ti] - gy) * (x0[ti] - gx)) * inv_a
                w2 = 1.0 - w0 - w1
                inside = (w0 >= -1e-4) & (w1 >= -1e-4) & (w2 >= -1e-4)
                if not inside.any():
                    continue
                zz = w0 * z0[ti] + w1 * z1[ti] + w2 * z2[ti]
                sub_z = zbuf[miny[k]:maxy[k] + 1, minx[k]:maxx[k] + 1]
                upd = inside & (zz < sub_z)
                if upd.any():
                    sub_z[upd] = zz[upd]
                    img[miny[k]:maxy[k] + 1, minx[k]:maxx[k] + 1][upd] = tcol[k]

        # Crisp bright tip trail edge on top
        if fi > 1:
            t0 = max(0, fi - 16)
            tx, ty, _ = proj(all_tip[t0:fi + 1])
            for k in range(len(tx) - 1):
                fade = (k + 1) / max(1, len(tx) - 1)
                _draw_line_2d(img, (tx[k], ty[k]), (tx[k + 1], ty[k + 1]),
                              [0.55 + 0.45 * fade, 0.90, 1.0], 2)

        img[:, 0] = [0.26, 0.30, 0.40]
        img[0, :] = [0.26, 0.30, 0.40]
        panels.append(img)

    return np.concatenate(panels, axis=1)


def write_png(path: Path, rgb: np.ndarray) -> None:
    rgb8 = (np.clip(rgb, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    H, W, _ = rgb8.shape
    raw = b"".join(b"\0" + rgb8[y].tobytes() for y in range(H))

    def chunk(t: bytes, d: bytes) -> bytes:
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 6))
        + chunk(b"IEND", b"")
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)
