#!/usr/bin/env python3
"""
tools/animations/jianshu_32_anims.py — Complete 32-Form Competition Wushu Jianshu (竞赛剑术 32式) Library

Authentic IWUF Competition Jianshu ("Kiếm Tẩu Khinh Linh — 剑走轻灵, 动静分明, 刚柔相济")
choreographed across all 4 canonical sections (32 distinct forms, zero duplicates):
  Section 1 (01-08): Opening Salute & Core Eight Sword Methods
                     (Khởi Thức, Trực Thích, Phách, Điểm, Băng, Quải-Thích, Chính Liêu, Phản Liêu)
  Section 2 (09-16): Agile Swordplay, True 3D Conical Wrist Flowers, Low Stances & Statutory Balances
                     (Vân-Mạt, Giảo-Xuyên, Song Uyển Hoa, Đề Tất, Phốc Bộ Tảo, Hiết Bộ Tiết, Yến Thức, Vọng Nguyệt)
  Section 3 (17-24): Nándù Aerial Jumps, 360° Spins & Drop Strikes
                     (Đằng Không Phi Cước, Toàn Phong Cước 360°, Bãi Liên 360°, Toàn Tử,
                      Khiêu Bộ Sáp, Chuyển Thân Phách 360°, Phi Bộ Thượng Thích, Điệt Bộ Đê Thích)
  Section 4 (25-32): Rapid Flurries, Back-Sword Feint, Trảm/Tiêu Climax, Độc Lập Lượng Tướng & Closing Salute
                     (Liên Hoàn Tam Thích, Tả Hữu Quải, Hồi Phong Vân 360°, Bối Kiếm Phản Thích,
                      Tiến Bộ Trảm Kiếm, Hành Bộ Xuyên Quải Tiêu Kiếm, Độc Lập Giá Kiếm Lượng Tướng, Thu Thức Bão Kiếm)

Biomechanical Joint-Safety Architecture:
  - Swing-twist zero-flip shoulder & elbow pole solver (eliminates 180° shoulder/forearm snaps)
  - Rosetta-Stone double-edged Jian socket alignment (keeps wrist swing <= 52° and twist <= 35°)
  - Constant-up dorsal Kiếm Quyết left-wrist solver (prevents segment-boundary roll discontinuities)
  - 2-branch continuous YXZ Euler ankle solver + forward kneecap constraint (eliminates ankle & knee flips
    even on >90° high slap-kicks, Swallow Scale, Moon-Gazing Scorpion fold, and Butterfly spins)
  - Automatic torso-clearance semicircular arc waypoints & C2 Catmull-Rom polish
"""

from __future__ import annotations

import math
from dataclasses import replace
from typing import List, Optional, Sequence, Tuple
import numpy as np

from .rig_core import (
    GAME_MIXAMO_20,
    SWORD_BLADE_BASE_Y,
    SWORD_BLADE_TIP_Y,
    SWORD_SOCKET_R,
    AnimationClip,
    Keyframe,
    PoseSpec,
    build_arm_frame,
    build_leg_frame,
    euler_yxz,
    mat_to_quat,
    quat_slerp,
    quat_to_mat,
    rot_x,
    rot_y,
    rot_z,
    sample_clip,
    solve_two_bone_ik,
    v_norm,
)


# ==============================================================================
# 0. BIOMECHANICAL JOINT-LOCKED POSE & ARC COMPILER
# ==============================================================================

_REST_LOC_R = [quat_to_mat(b[4]) for b in GAME_MIXAMO_20]
_R_FOOT_REST = np.array([[1.0, 0.0, 0.0], [0.0, -1.0, 0.0], [0.0, 0.0, -1.0]], dtype=np.float64)


def _unwrap_deg(deg: float, ref_deg: float) -> float:
    while deg - ref_deg > 180.0:
        deg -= 360.0
    while deg - ref_deg < -180.0:
        deg += 360.0
    return deg


def _shortest_arc_rot(u_from: np.ndarray, u_to: np.ndarray) -> np.ndarray:
    a = v_norm(u_from)
    b = v_norm(u_to)
    c = float(np.dot(a, b))
    if c > 0.999999:
        return np.eye(3, dtype=np.float64)
    if c < -0.999999:
        ortho = np.array([1.0, 0.0, 0.0]) if abs(a[0]) < 0.9 else np.array([0.0, 0.0, 1.0])
        ax = v_norm(np.cross(a, ortho))
        return 2.0 * np.outer(ax, ax) - np.eye(3, dtype=np.float64)
    v = np.cross(a, b)
    s = float(np.linalg.norm(v))
    vx = np.array([[0.0, -v[2], v[1]], [v[2], 0.0, -v[0]], [-v[1], v[0], 0.0]], dtype=np.float64)
    return np.eye(3, dtype=np.float64) + vx + (vx @ vx) * ((1.0 - c) / (s * s))


def _clamp_cone(vec: np.ndarray, axis: np.ndarray, max_deg: float) -> np.ndarray:
    v = v_norm(vec)
    a = v_norm(axis)
    c = max(-1.0, min(1.0, float(np.dot(v, a))))
    if math.degrees(math.acos(c)) <= max_deg:
        return v
    perp = v - c * a
    if float(np.linalg.norm(perp)) < 1e-6:
        ortho = np.array([0.0, 1.0, 0.0]) if abs(a[1]) < 0.9 else np.array([1.0, 0.0, 0.0])
        perp = ortho - np.dot(ortho, a) * a
    perp = v_norm(perp)
    rad = math.radians(max_deg)
    return v_norm(math.cos(rad) * a + math.sin(rad) * perp)


def _foot_euler_two_branch(
    R_ft: np.ndarray, ref_yaw: float, ref_pitch: float, ref_roll: float
) -> Tuple[float, float, float]:
    """Invert rot_y(yaw) @ rot_x(-pitch) @ rot_z(roll) @ R_FOOT_REST across both Euler branches.

    Eliminates ±90° pitch gimbal-lock 180° yaw/roll flips during high kicks & aerial scales.
    """
    M = R_ft @ _R_FOOT_REST.T
    sin_a = max(-1.0, min(1.0, -float(M[1, 2])))
    cos_a = math.sqrt(max(0.0, 1.0 - sin_a * sin_a))
    p1 = -math.degrees(math.asin(sin_a))
    if cos_a > 1e-4:
        y1 = math.degrees(math.atan2(float(M[0, 2]), float(M[2, 2])))
        r1 = math.degrees(math.atan2(float(M[1, 0]), float(M[1, 1])))
        p2 = (180.0 - p1) if p1 >= 0.0 else (-180.0 - p1)
        cands = [
            (_unwrap_deg(y1, ref_yaw), _unwrap_deg(p1, ref_pitch), _unwrap_deg(r1, ref_roll)),
            (_unwrap_deg(y1 + 180.0, ref_yaw), _unwrap_deg(p2, ref_pitch), _unwrap_deg(r1 + 180.0, ref_roll)),
        ]
        return min(
            cands,
            key=lambda c: (c[0] - ref_yaw) ** 2 + (c[1] - ref_pitch) ** 2 + (c[2] - ref_roll) ** 2,
        )
    return ref_yaw, _unwrap_deg(p1, ref_pitch), ref_roll


def _torso_frames(p: PoseSpec) -> Tuple[np.ndarray, np.ndarray]:
    wP = np.zeros((12, 3), dtype=np.float64)
    wR = np.zeros((12, 3, 3), dtype=np.float64)
    wP[0] = np.array([0.0, float(p.hips_y), 0.0], dtype=np.float64)
    wR[0] = euler_yxz(p.hips_yaw, p.hips_pitch, p.hips_roll)
    for bi, w in ((1, 0.22), (2, 0.34), (3, 0.44)):
        par = GAME_MIXAMO_20[bi][2]
        wP[bi] = wP[par] + wR[par] @ np.array(GAME_MIXAMO_20[bi][3], dtype=np.float64)
        wR[bi] = wR[par] @ euler_yxz(p.torso_yaw * w, p.torso_pitch * w, p.torso_roll * w)

    wP[4] = wP[3] + wR[3] @ np.array(GAME_MIXAMO_20[4][3], dtype=np.float64)
    if p.head_world_space:
        R_hd = euler_yxz(p.head_yaw, p.head_pitch, p.head_roll)
        wR[4] = quat_to_mat(quat_slerp(mat_to_quat(wR[3]), mat_to_quat(R_hd), 0.45))
        wP[5] = wP[4] + wR[4] @ np.array(GAME_MIXAMO_20[5][3], dtype=np.float64)
        wR[5] = R_hd
    else:
        wR[4] = wR[3] @ euler_yxz(p.head_yaw * 0.45, p.head_pitch * 0.45, p.head_roll * 0.45)
        wP[5] = wP[4] + wR[4] @ np.array(GAME_MIXAMO_20[5][3], dtype=np.float64)
        wR[5] = wR[4] @ euler_yxz(p.head_yaw * 0.55, p.head_pitch * 0.55, p.head_roll * 0.55)

    wP[6] = wP[3] + wR[3] @ np.array(GAME_MIXAMO_20[6][3], dtype=np.float64)
    wR[6] = wR[3] @ _REST_LOC_R[6] @ rot_z(-p.l_shoulder_fwd) @ rot_x(p.l_shoulder_lift)
    wP[7] = wP[6] + wR[6] @ np.array(GAME_MIXAMO_20[7][3], dtype=np.float64)

    wP[10] = wP[3] + wR[3] @ np.array(GAME_MIXAMO_20[10][3], dtype=np.float64)
    wR[10] = wR[3] @ _REST_LOC_R[10] @ rot_z(p.r_shoulder_fwd) @ rot_x(p.r_shoulder_lift)
    wP[11] = wP[10] + wR[10] @ np.array(GAME_MIXAMO_20[11][3], dtype=np.float64)
    return wP, wR


def _stabilize_pose(
    p: PoseSpec,
    prev_pose: Optional[PoseSpec] = None,
    prev_edge_w: Optional[np.ndarray] = None,
    l_air_flex: float = 24.0,
    r_air_flex: float = 26.0,
) -> Tuple[PoseSpec, np.ndarray]:
    """Project any authored PoseSpec onto strict human joint-angle, pole, wrist & ankle manifolds."""
    wP, wR = _torso_frames(p)
    R_by = rot_y(p.hips_yaw)
    R_by_inv = R_by.T

    def to_w_hand(pt: Sequence[float]) -> np.ndarray:
        v = np.array(pt, dtype=np.float64)
        return (R_by @ np.array([v[0], 0.0, v[2]]) + np.array([0.0, v[1], 0.0])) if p.local_body_space else v

    def from_w_hand(pt_w: np.ndarray) -> Tuple[float, float, float]:
        v = (R_by_inv @ np.array([pt_w[0], 0.0, pt_w[2]]) + np.array([0.0, pt_w[1], 0.0])) if p.local_body_space else pt_w
        return (round(float(v[0]), 4), round(float(v[1]), 4), round(float(v[2]), 4))

    def to_w_dir(d: Sequence[float]) -> np.ndarray:
        v = np.array(d, dtype=np.float64)
        return (R_by @ v) if p.local_body_space else v

    def from_w_dir(d_w: np.ndarray) -> Tuple[float, float, float]:
        v = (R_by_inv @ d_w) if p.local_body_space else d_w
        return (round(float(v[0]), 4), round(float(v[1]), 4), round(float(v[2]), 4))

    def to_w_foot(pt: Sequence[float]) -> np.ndarray:
        v = np.array(pt, dtype=np.float64)
        return (R_by @ np.array([v[0], 0.0, v[2]]) + np.array([0.0, v[1], 0.0])) if p.feet_in_body_space else v

    def from_w_foot(pt_w: np.ndarray) -> Tuple[float, float, float]:
        v = (R_by_inv @ np.array([pt_w[0], 0.0, pt_w[2]]) + np.array([0.0, pt_w[1], 0.0])) if p.feet_in_body_space else pt_w
        return (round(float(v[0]), 4), round(float(v[1]), 4), round(float(v[2]), 4))

    # 1. Left Arm: reach [0.325, 0.455] (elbow bend 36°..100°) + zero-flip swing-only shoulder pole
    lh_w = to_w_hand(p.l_hand_pos)
    d_l_vec = lh_w - wP[7]
    dist_l = max(0.325, min(0.455, float(np.linalg.norm(d_l_vec))))
    dir_l = v_norm(d_l_vec, wR[6][:, 1])
    lh_w = wP[7] + dir_l * dist_l

    R_sw_l = _shortest_arc_rot(wR[6][:, 1], dir_l)
    R_la_ideal = (R_sw_l @ wR[6]) @ rot_y(-18.0)
    p_perp_l = v_norm(np.cross(R_la_ideal[:, 2], dir_l), np.array([1.0, -0.3, 0.0]))
    l_pole_w = wP[7] + dir_l * (dist_l * 0.50) + p_perp_l * 0.38

    l_elbow_w, l_hand_w = solve_two_bone_ik(wP[7], lh_w, 0.26, 0.24, l_pole_w, min_bend_deg=8.0)
    u_la = v_norm(l_elbow_w - wP[7])
    u_lfa = v_norm(l_hand_w - l_elbow_w)
    l_hinge = v_norm(np.cross(u_lfa, u_la), np.array([0.0, 1.0, 0.0]))
    R_lfa = build_arm_frame(u_lfa, l_hinge, is_right=False)

    # Constant l_hand_up=(0,1,0) avoids discrete jump in rig_core._vec_to_pose across segments
    lh_up_const = (0.0, 1.0, 0.0)
    lh_up_w = v_norm(to_w_dir(lh_up_const))
    raw_lh_d = to_w_dir(p.l_hand_dir if p.l_hand_dir is not None else u_lfa)
    graceful_kq = v_norm(u_lfa * 0.90 + R_lfa[:, 2] * 0.30)
    lh_dir_w = _clamp_cone(v_norm(0.50 * v_norm(raw_lh_d) + 0.50 * graceful_kq), u_lfa, 30.0)
    if abs(float(np.dot(lh_dir_w, lh_up_w))) > 0.86:
        horiz = v_norm(np.array([lh_dir_w[0], 0.0, lh_dir_w[2]]), np.array([-0.3, 0.0, 0.95]))
        lh_dir_w = _clamp_cone(v_norm(lh_dir_w * 0.72 + horiz * 0.55), u_lfa, 34.0)

    # 2. Right Arm + Sword Socket: steer shoulder twist gently toward blade, clamp wrist <= 50°
    rh_w = to_w_hand(p.r_hand_pos)
    d_r_vec = rh_w - wP[11]
    dist_r = max(0.300, min(0.455, float(np.linalg.norm(d_r_vec))))
    dir_r = v_norm(d_r_vec, wR[10][:, 1])
    rh_w = wP[11] + dir_r * dist_r

    raw_b_dir = v_norm(to_w_dir(p.blade_dir if p.blade_dir is not None else (0.20, 0.42, 0.88)))

    # Smooth, continuous shoulder twist (16.0 deg) — zero discrete pole-selection jumps!
    R_sw_r = _shortest_arc_rot(wR[10][:, 1], dir_r)
    R_ra_ideal = (R_sw_r @ wR[10]) @ rot_y(16.0)
    pp_r = v_norm(np.cross(dir_r, R_ra_ideal[:, 2]), np.array([-1.0, -0.3, 0.0]))
    r_pole_w = wP[11] + dir_r * (dist_r * 0.50) + pp_r * 0.38
    r_elbow_w, r_hand_w = solve_two_bone_ik(wP[11], rh_w, 0.26, 0.24, r_pole_w, min_bend_deg=8.0)
    u_ra = v_norm(r_elbow_w - wP[11])
    u_rfa = v_norm(r_hand_w - r_elbow_w)
    r_hinge = v_norm(np.cross(u_ra, u_rfa), np.array([0.0, 1.0, 0.0]))
    R_rfa = build_arm_frame(u_rfa, r_hinge, is_right=True)

    b_neutral = v_norm(R_rfa @ SWORD_SOCKET_R[:, 1])
    b_y = _clamp_cone(raw_b_dir, b_neutral, 40.0)

    # Guarantee strict head & floor clearance on sword blade
    head_c = wP[5] + np.array([0.0, 0.06, 0.02], dtype=np.float64)
    for _ in range(5):
        b_base = r_hand_w + b_y * SWORD_BLADE_BASE_Y
        b_tip = r_hand_w + b_y * SWORD_BLADE_TIP_Y
        seg = b_tip - b_base
        u_h = max(0.0, min(1.0, float(np.dot(head_c - b_base, seg)) / max(1e-8, float(np.dot(seg, seg)))))
        diff_h = (b_base + u_h * seg) - head_c
        d_h = float(np.linalg.norm(diff_h))
        if d_h < 0.175:
            push = v_norm(diff_h, np.array([-0.8, 0.3, 0.5]))
            b_y = _clamp_cone(v_norm(b_y + push * (0.22 - d_h) * 2.4), b_neutral, 44.0)
        if r_hand_w[1] + b_y[1] * SWORD_BLADE_TIP_Y < 0.22:
            b_y = _clamp_cone(v_norm(b_y + np.array([0.0, 0.35, 0.0])), b_neutral, 44.0)

    # Symmetric double-edged Jian edge alignment (prevents ±180° wrist candy-wrapper twist)
    e_neutral = R_rfa @ SWORD_SOCKET_R[:, 0]
    e_0 = e_neutral - np.dot(e_neutral, b_y) * b_y
    if float(np.linalg.norm(e_0)) < 1e-5:
        alt = R_rfa[:, 2]
        e_0 = alt - np.dot(alt, b_y) * b_y
    e_0 = v_norm(e_0)

    raw_e = to_w_dir(p.blade_edge if p.blade_edge is not None else e_0)
    e_cand = raw_e - np.dot(raw_e, b_y) * b_y
    if float(np.linalg.norm(e_cand)) < 1e-5:
        e_cand = e_0.copy()
    else:
        e_cand = v_norm(e_cand)
        if float(np.dot(e_cand, e_0)) < 0.0:
            e_cand = -e_cand

    e_x = _clamp_cone(e_cand, e_0, 42.0)
    e_x = v_norm(e_x - np.dot(e_x, b_y) * b_y)

    # 3. Legs, Knees & 2-Branch Continuous Ankles
    leg_out = []
    for side_idx, (i_up, ft_pos, ft_yaw, ft_pitch, turnout, air_flex) in enumerate([
        (14, p.l_foot_pos, p.l_foot_yaw, p.l_foot_pitch, 10.0, l_air_flex),
        (17, p.r_foot_pos, p.r_foot_yaw, p.r_foot_pitch, -12.0, r_air_flex),
    ]):
        hip_p = wP[0] + wR[0] @ np.array(GAME_MIXAMO_20[i_up][3], dtype=np.float64)
        fp_w = to_w_foot(ft_pos)
        if fp_w[1] < 0.080:
            fp_w[1] = 0.080
        d_leg_vec = fp_w - hip_p
        dist_leg = max(0.36, min(0.792, float(np.linalg.norm(d_leg_vec))))
        dir_leg = v_norm(d_leg_vec, np.array([0.0, -1.0, 0.0]))
        fp_w = hip_p + dir_leg * dist_leg
        if fp_w[1] < 0.080:
            fp_w[1] = 0.080
            d_leg_vec = fp_w - hip_p
            dir_leg = v_norm(d_leg_vec)
            dist_leg = float(np.linalg.norm(d_leg_vec))

        R_ul_base = wR[0] @ _REST_LOC_R[i_up]
        R_sw_leg = _shortest_arc_rot(R_ul_base[:, 1], dir_leg)
        R_ul_ideal = (R_sw_leg @ R_ul_base) @ rot_y(-turnout)
        k_ideal = R_ul_ideal[:, 0]
        p_perp_leg = v_norm(np.cross(dir_leg, k_ideal), np.array([0.0, 0.0, 1.0]))
        kp_w = hip_p + dir_leg * (dist_leg * 0.50) + p_perp_leg * 0.38

        knee_w, ankle_w = solve_two_bone_ik(hip_p, fp_w, 0.42, 0.40, kp_w, min_bend_deg=6.0)
        u_th = v_norm(knee_w - hip_p)
        u_sh = v_norm(ankle_w - knee_w)
        k_ax = v_norm(np.cross(u_th, u_sh), k_ideal)
        R_shin = build_leg_frame(u_sh, k_ax)

        ref_fy_w = (prev_pose.l_foot_yaw if side_idx == 0 else prev_pose.r_foot_yaw) if prev_pose else ft_yaw
        ref_fp = (prev_pose.l_foot_pitch if side_idx == 0 else prev_pose.r_foot_pitch) if prev_pose else 0.0
        ref_fr = (prev_pose.l_foot_roll if side_idx == 0 else prev_pose.r_foot_roll) if prev_pose else 0.0
        if prev_pose and prev_pose.feet_in_body_space:
            ref_fy_w += prev_pose.hips_yaw

        if ankle_w[1] <= 0.090:
            shin_fwd = v_norm(np.cross(k_ax, np.array([0.0, 1.0, 0.0])), np.array([0.0, 0.0, 1.0]))
            nat_yaw = math.degrees(math.atan2(float(shin_fwd[0]), float(shin_fwd[2])))
            want_yaw = _unwrap_deg(ft_yaw + (p.hips_yaw if p.feet_in_body_space else 0.0), nat_yaw)
            fy_w = _unwrap_deg(nat_yaw + max(-18.0, min(18.0, want_yaw - nat_yaw)), ref_fy_w)
            # Automatically ease heel pitch when shin is steeply inclined in deep stances (Pūbù / Điệt Bộ)
            shin_tilt_deg = math.degrees(math.acos(max(-1.0, min(1.0, float(-u_sh[1])))))
            if shin_tilt_deg > 46.0 and u_sh[2] < -0.20:
                ankle_w_lift = min(0.102, 0.080 + (shin_tilt_deg - 46.0) * 0.0011)
                fp_w[1] = max(fp_w[1], ankle_w_lift)
                fpitch_out = -min(20.0, (shin_tilt_deg - 42.0) * 0.70)
            else:
                fpitch_out = max(-16.0, min(12.0, ft_pitch))
            froll_out = 0.0
        elif ankle_w[1] <= 0.135:
            R_ft_air = R_shin @ rot_x(-18.0)
            fy_w, fpitch_out, froll_out = _foot_euler_two_branch(R_ft_air, ref_fy_w, ref_fp, ref_fr)
            fpitch_out = max(-32.0, min(24.0, fpitch_out))
            froll_out *= 0.35
        else:
            R_ft_air = R_shin @ rot_x(-air_flex)
            fy_w, fpitch_out, froll_out = _foot_euler_two_branch(R_ft_air, ref_fy_w, ref_fp, ref_fr)

        fy_spec = fy_w - (p.hips_yaw if p.feet_in_body_space else 0.0)
        leg_out.append((
            from_w_foot(fp_w),
            from_w_foot(kp_w),
            round(fy_spec, 3),
            round(fpitch_out, 3),
            round(froll_out, 3),
        ))

    out_p = replace(
        p,
        l_hand_pos=from_w_hand(lh_w),
        l_elbow_pole=from_w_hand(l_pole_w),
        l_hand_dir=from_w_dir(lh_dir_w),
        l_hand_up=lh_up_const,
        r_hand_pos=from_w_hand(rh_w),
        r_elbow_pole=from_w_hand(r_pole_w),
        blade_dir=from_w_dir(b_y),
        blade_edge=from_w_dir(e_x),
        blade_spin_deg=0.0,
        l_foot_pos=leg_out[0][0],
        l_knee_pole=leg_out[0][1],
        l_foot_yaw=leg_out[0][2],
        l_foot_pitch=leg_out[0][3],
        l_foot_roll=leg_out[0][4],
        r_foot_pos=leg_out[1][0],
        r_knee_pole=leg_out[1][1],
        r_foot_yaw=leg_out[1][2],
        r_foot_pitch=leg_out[1][3],
        r_foot_roll=leg_out[1][4],
    )
    return out_p, e_x


def _lerp_pose_arc(a: PoseSpec, b: PoseSpec, u: float = 0.5) -> PoseSpec:
    """Interpolate two PoseSpecs while pushing hand chords outward into natural semicircular arcs."""
    def lp(x: float, y: float) -> float:
        return x + (y - x) * u

    def lv(va: Sequence[float], vb: Sequence[float]) -> np.ndarray:
        return np.array(va, dtype=np.float64) * (1.0 - u) + np.array(vb, dtype=np.float64) * u

    mid = PoseSpec(
        hips_y=lp(a.hips_y, b.hips_y),
        hips_yaw=lp(a.hips_yaw, b.hips_yaw),
        hips_pitch=lp(a.hips_pitch, b.hips_pitch),
        hips_roll=lp(a.hips_roll, b.hips_roll),
        torso_yaw=lp(a.torso_yaw, b.torso_yaw),
        torso_pitch=lp(a.torso_pitch, b.torso_pitch),
        torso_roll=lp(a.torso_roll, b.torso_roll),
        head_yaw=lp(a.head_yaw, b.head_yaw),
        head_pitch=lp(a.head_pitch, b.head_pitch),
        head_roll=lp(a.head_roll, b.head_roll),
        head_world_space=a.head_world_space,
        l_shoulder_lift=lp(a.l_shoulder_lift, b.l_shoulder_lift),
        l_shoulder_fwd=lp(a.l_shoulder_fwd, b.l_shoulder_fwd),
        r_shoulder_lift=lp(a.r_shoulder_lift, b.r_shoulder_lift),
        r_shoulder_fwd=lp(a.r_shoulder_fwd, b.r_shoulder_fwd),
        r_hand_pos=tuple(lv(a.r_hand_pos, b.r_hand_pos).tolist()),
        r_elbow_pole=tuple(lv(a.r_elbow_pole, b.r_elbow_pole).tolist()),
        blade_dir=tuple(v_norm(lv(a.blade_dir or (0, 0.4, 0.9), b.blade_dir or (0, 0.4, 0.9))).tolist()),
        blade_edge=tuple(v_norm(lv(a.blade_edge or (0, -0.9, 0.4), b.blade_edge or (0, -0.9, 0.4))).tolist()),
        l_hand_pos=tuple(lv(a.l_hand_pos, b.l_hand_pos).tolist()),
        l_elbow_pole=tuple(lv(a.l_elbow_pole, b.l_elbow_pole).tolist()),
        l_hand_dir=tuple(v_norm(lv(a.l_hand_dir or (-0.2, 0.3, 0.9), b.l_hand_dir or (-0.2, 0.3, 0.9))).tolist()),
        l_foot_pos=tuple(lv(a.l_foot_pos, b.l_foot_pos).tolist()),
        l_knee_pole=tuple(lv(a.l_knee_pole, b.l_knee_pole).tolist()),
        l_foot_yaw=lp(a.l_foot_yaw, b.l_foot_yaw),
        l_foot_pitch=lp(a.l_foot_pitch, b.l_foot_pitch),
        r_foot_pos=tuple(lv(a.r_foot_pos, b.r_foot_pos).tolist()),
        r_knee_pole=tuple(lv(a.r_knee_pole, b.r_knee_pole).tolist()),
        r_foot_yaw=lp(a.r_foot_yaw, b.r_foot_yaw),
        r_foot_pitch=lp(a.r_foot_pitch, b.r_foot_pitch),
        local_body_space=a.local_body_space,
        feet_in_body_space=a.feet_in_body_space,
    )

    # Smooth radial shoulder-clearance push (C1 continuous, zero direction jump!)
    wP, _ = _torso_frames(mid)
    R_by = rot_y(mid.hips_yaw)
    R_by_inv = R_by.T

    lh_w = (R_by @ np.array([mid.l_hand_pos[0], 0.0, mid.l_hand_pos[2]]) + np.array([0.0, mid.l_hand_pos[1], 0.0])) if mid.local_body_space else np.array(mid.l_hand_pos)
    dl = float(np.linalg.norm(lh_w - wP[7]))
    if dl < 0.340:
        lh_w = wP[7] + v_norm(lh_w - wP[7], np.array([0.8, -0.2, 0.5])) * 0.340
        lh_loc = (R_by_inv @ np.array([lh_w[0], 0.0, lh_w[2]]) + np.array([0.0, lh_w[1], 0.0])) if mid.local_body_space else lh_w
        mid = replace(mid, l_hand_pos=tuple(lh_loc.tolist()))

    rh_w = (R_by @ np.array([mid.r_hand_pos[0], 0.0, mid.r_hand_pos[2]]) + np.array([0.0, mid.r_hand_pos[1], 0.0])) if mid.local_body_space else np.array(mid.r_hand_pos)
    dr = float(np.linalg.norm(rh_w - wP[11]))
    if dr < 0.330:
        rh_w = wP[11] + v_norm(rh_w - wP[11], np.array([-0.8, -0.2, 0.5])) * 0.330
        rh_loc = (R_by_inv @ np.array([rh_w[0], 0.0, rh_w[2]]) + np.array([0.0, rh_w[1], 0.0])) if mid.local_body_space else rh_w
        mid = replace(mid, r_hand_pos=tuple(rh_loc.tolist()))

    return mid


def _compile_clip(
    name: str,
    slot: str,
    duration: float,
    description: str,
    keyframes: List[Keyframe],
    tags: List[str],
    hit_window: Optional[Tuple[float, float]] = None,
    l_air_flex: float = 24.0,
    r_air_flex: float = 26.0,
    smooth_passes: int = 4,
) -> AnimationClip:
    """Subdivide wide arcs for torso/head clearance and stabilize every keyframe anatomically."""
    kfs_sorted = sorted(keyframes, key=lambda k: k.t)
    expanded: List[Keyframe] = [kfs_sorted[0]]

    for i in range(1, len(kfs_sorted)):
        ka, kb = expanded[-1], kfs_sorted[i]
        dt = kb.t - ka.t
        dyaw = abs(kb.pose.hips_yaw - ka.pose.hips_yaw)
        dlh = float(np.linalg.norm(np.array(kb.pose.l_hand_pos) - np.array(ka.pose.l_hand_pos)))
        drh = float(np.linalg.norm(np.array(kb.pose.r_hand_pos) - np.array(ka.pose.r_hand_pos)))
        ba = v_norm(np.array(ka.pose.blade_dir or (0.0, 0.4, 0.9)))
        bb = v_norm(np.array(kb.pose.blade_dir or (0.0, 0.4, 0.9)))
        db_deg = math.degrees(math.acos(max(-1.0, min(1.0, float(np.dot(ba, bb))))))

        if dt >= 0.17 and (dyaw > 68.0 or dlh > 0.44 or drh > 0.44 or db_deg > 72.0):
            p_mid = _lerp_pose_arc(ka.pose, kb.pose, 0.5)
            expanded.append(Keyframe(round((ka.t + kb.t) * 0.5, 4), p_mid, "flow", tension=0.0))
        expanded.append(kb)

    def _stabilize_list(kf_list: List[Keyframe]) -> List[Keyframe]:
        out: List[Keyframe] = []
        prev_p: Optional[PoseSpec] = None
        prev_e: Optional[np.ndarray] = None
        for idx, kf in enumerate(kf_list):
            sp, prev_e = _stabilize_pose(
                kf.pose,
                prev_pose=prev_p,
                prev_edge_w=prev_e,
                l_air_flex=l_air_flex,
                r_air_flex=r_air_flex,
            )
            is_guard_yaw = abs(((sp.hips_yaw - (-24.0) + 180.0) % 360.0) - 180.0) < 1e-3
            if idx == 0 and is_guard_yaw:
                sp = _spin_guard(sp.hips_yaw) if sp.local_body_space else _CANONICAL_GUARD
                prev_e = np.array(_CANONICAL_GUARD.blade_edge, dtype=np.float64)
            elif idx == len(kf_list) - 1 and is_guard_yaw:
                base_g = _spin_guard(sp.hips_yaw) if sp.local_body_space else _CANONICAL_GUARD
                sp = replace(
                    base_g,
                    l_foot_yaw=_unwrap_deg(base_g.l_foot_yaw, prev_p.l_foot_yaw if prev_p else base_g.l_foot_yaw),
                    r_foot_yaw=_unwrap_deg(base_g.r_foot_yaw, prev_p.r_foot_yaw if prev_p else base_g.r_foot_yaw),
                )
            prev_p = sp
            ease = "whip" if kf.ease == "snap" else ("ease_out" if kf.ease == "heavy_drop" else kf.ease)
            out.append(Keyframe(kf.t, sp, ease, tension=min(0.22, kf.tension)))
        return out

    stabilized = _stabilize_list(expanded)

    # Prevent S^2 parallel-transport holonomy flips in rig_core.sample_clip:
    # subdivide any segment where stabilized blade_edge or blade_dir rotates >= 65°!
    for _ in range(3):
        need_sub = False
        rebuilt: List[Keyframe] = [stabilized[0]]
        for i in range(1, len(stabilized)):
            ka, kb = rebuilt[-1], stabilized[i]
            ea = np.array(ka.pose.blade_edge, dtype=np.float64)
            eb = np.array(kb.pose.blade_edge, dtype=np.float64)
            ba = np.array(ka.pose.blade_dir, dtype=np.float64)
            bb = np.array(kb.pose.blade_dir, dtype=np.float64)
            # Also check world-space edge continuity for local_body_space spins!
            ea_w = (rot_y(ka.pose.hips_yaw) @ ea) if ka.pose.local_body_space else ea
            eb_w = (rot_y(kb.pose.hips_yaw) @ eb) if kb.pose.local_body_space else eb
            if (kb.t - ka.t) > 0.05 and (
                float(np.dot(ea, eb)) < 0.42
                or float(np.dot(ea_w, eb_w)) < 0.35
                or float(np.dot(ba, bb)) < 0.42
            ):
                p_mid = _lerp_pose_arc(ka.pose, kb.pose, 0.5)
                rebuilt.append(Keyframe(round((ka.t + kb.t) * 0.5, 4), p_mid, "flow", tension=0.0))
                need_sub = True
            rebuilt.append(kb)
        if not need_sub:
            break
        stabilized = _stabilize_list(rebuilt)

    return AnimationClip(
        name=name,
        slot=slot,
        duration=duration,
        loop=False,
        description=description,
        keyframes=stabilized,
        tags=tags,
        hit_window=hit_window,
        smooth_passes=smooth_passes,
    )


# ==============================================================================
# CANONICAL COMPETITION JIANSHU GUARD (Kiếm Chỉ Khởi Thế)
# ==============================================================================

_RAW_GUARD = PoseSpec(
    hips_y=0.905,
    hips_yaw=-24.0,
    hips_pitch=3.5,
    hips_roll=0.0,
    torso_yaw=14.0,
    torso_pitch=3.0,
    torso_roll=-1.5,
    head_yaw=10.0,
    head_pitch=-5.0,
    head_roll=1.5,
    head_world_space=False,
    l_shoulder_lift=-2.0,
    l_shoulder_fwd=8.0,
    r_shoulder_lift=-3.0,
    r_shoulder_fwd=10.0,
    # Right hand holds Jian crisply forward-right; ascending diagonal line
    r_hand_pos=(-0.18, 1.12, 0.35),
    r_elbow_pole=(-0.54, 0.92, -0.04),
    blade_dir=(0.18, 0.44, 0.88),
    blade_edge=(0.0, -0.88, 0.44),
    blade_spin_deg=0.0,
    # Left Kiếm Quyết: poised gracefully at chest-left with relaxed elbow
    l_hand_pos=(0.24, 1.24, 0.30),
    l_elbow_pole=(0.58, 1.04, -0.04),
    l_hand_dir=(-0.28, 0.32, 0.90),
    l_hand_up=(0.0, 1.0, 0.0),
    # Grounded martial ready footwork
    l_foot_pos=(0.16, 0.080, 0.18),
    l_knee_pole=(0.22, 0.48, 0.72),
    l_foot_yaw=10.0,
    l_foot_pitch=0.0,
    r_foot_pos=(-0.19, 0.080, -0.18),
    r_knee_pole=(-0.28, 0.48, 0.42),
    r_foot_yaw=-24.0,
    r_foot_pitch=0.0,
)
_CANONICAL_GUARD, _ = _stabilize_pose(_RAW_GUARD)


def _guard(**overrides) -> PoseSpec:
    """Crisp, proud Competition Jianshu guard: upright spine, taut sword line, alert Kiếm Quyết."""
    return replace(_CANONICAL_GUARD, **overrides) if overrides else _CANONICAL_GUARD


def _spin_guard(yaw_deg: float) -> PoseSpec:
    """Body-space equivalent of _CANONICAL_GUARD at any 360-degree multiple (e.g. -24, 336, -384)."""
    # Convert _CANONICAL_GUARD world coordinates into pelvis-yaw body space at -24.0 deg
    R_inv = rot_y(24.0)
    def bw_pt(pt: Tuple[float, float, float]) -> Tuple[float, float, float]:
        v = R_inv @ np.array([pt[0], 0.0, pt[2]], dtype=np.float64)
        return (float(v[0]), float(pt[1]), float(v[2]))
    def bw_dir(d: Tuple[float, float, float]) -> Tuple[float, float, float]:
        v = R_inv @ np.array(d, dtype=np.float64)
        return (float(v[0]), float(v[1]), float(v[2]))

    g = _CANONICAL_GUARD
    return PoseSpec(
        hips_y=g.hips_y,
        hips_yaw=yaw_deg,
        hips_pitch=g.hips_pitch,
        hips_roll=g.hips_roll,
        torso_yaw=g.torso_yaw,
        torso_pitch=g.torso_pitch,
        torso_roll=g.torso_roll,
        head_yaw=g.head_yaw,
        head_pitch=g.head_pitch,
        head_roll=g.head_roll,
        head_world_space=False,
        l_shoulder_lift=g.l_shoulder_lift,
        l_shoulder_fwd=g.l_shoulder_fwd,
        r_shoulder_lift=g.r_shoulder_lift,
        r_shoulder_fwd=g.r_shoulder_fwd,
        r_hand_pos=bw_pt(g.r_hand_pos),
        r_elbow_pole=bw_pt(g.r_elbow_pole),
        blade_dir=bw_dir(g.blade_dir),
        blade_edge=bw_dir(g.blade_edge),
        l_hand_pos=bw_pt(g.l_hand_pos),
        l_elbow_pole=bw_pt(g.l_elbow_pole),
        l_hand_dir=bw_dir(g.l_hand_dir),
        l_hand_up=(0.0, 1.0, 0.0),
        l_foot_pos=bw_pt(g.l_foot_pos),
        l_knee_pole=bw_pt(g.l_knee_pole),
        l_foot_yaw=g.l_foot_yaw + 24.0,
        l_foot_pitch=g.l_foot_pitch,
        r_foot_pos=bw_pt(g.r_foot_pos),
        r_knee_pole=bw_pt(g.r_knee_pole),
        r_foot_yaw=g.r_foot_yaw + 24.0,
        r_foot_pitch=g.r_foot_pitch,
        local_body_space=True,
        feet_in_body_space=True,
    )


# ==============================================================================
# SECTION 1 (01-08): OPENING SALUTE & CORE EIGHT SWORD METHODS
# ==============================================================================

def make_js01_qi_shi() -> AnimationClip:
    """01. Khởi Thức Bão Kiếm (起势抱剑): Competition opening salute -> flourish unfurl -> alert Lượng Tướng."""
    g0 = _guard()
    baojian_salute = _guard(
        hips_y=0.928, hips_yaw=0.0, hips_pitch=1.0, hips_roll=0.0,
        torso_yaw=0.0, torso_pitch=1.0, torso_roll=0.0, head_yaw=0.0, head_pitch=0.0,
        l_shoulder_lift=-2.0, l_shoulder_fwd=12.0, r_shoulder_lift=-2.0, r_shoulder_fwd=12.0,
        # Dignified Bão Kiếm salute at chest: forearm angled forward-up so wrist stays 100% natural!
        r_hand_pos=(-0.10, 1.20, 0.34),
        blade_dir=(0.04, 0.84, 0.54),
        l_hand_pos=(0.08, 1.24, 0.36), l_hand_dir=(-0.22, 0.54, 0.81),
        l_foot_pos=(0.10, 0.080, 0.02), l_foot_yaw=4.0,
        r_foot_pos=(-0.10, 0.080, 0.02), r_foot_yaw=-4.0,
    )
    liangxiang_open = _guard(
        hips_y=0.910, hips_yaw=-16.0, hips_pitch=2.5, torso_yaw=12.0, torso_pitch=1.0,
        head_yaw=14.0, head_pitch=-1.5,
        r_shoulder_lift=6.0, r_shoulder_fwd=-6.0, l_shoulder_lift=10.0, l_shoulder_fwd=16.0,
        # Right sword sweeps diagonally right-back-up; left Kiếm Quyết pierces left-forward (Lượng Tướng!)
        r_hand_pos=(-0.42, 1.26, 0.08),
        blade_dir=(-0.62, 0.64, 0.45),
        l_hand_pos=(0.34, 1.38, 0.38), l_hand_dir=(0.36, 0.34, 0.87),
        l_foot_pos=(0.15, 0.080, 0.14), r_foot_pos=(-0.16, 0.080, -0.12),
    )
    poise_hold = replace(
        liangxiang_open, hips_y=0.918,
        l_hand_pos=(0.36, 1.40, 0.40), r_hand_pos=(-0.43, 1.28, 0.07),
    )
    return _compile_clip(
        name="js01_qi_shi", slot="jianshu_open", duration=1.35,
        tags=["jianshu", "wushu", "section1", "opening", "liangxiang"],
        description="Competition Jianshu opening salute & Lượng Tướng poise (Khởi Thức Bão Kiếm).",
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.28, baojian_salute, "flow"),
            Keyframe(0.58, liangxiang_open, "snap"),
            Keyframe(0.98, poise_hold, "flow"),
            Keyframe(1.35, g0, "flow"),
        ],
    )


def make_js02_bingbu_zhici() -> AnimationClip:
    """02. Tịnh Bộ Trực Thích (并步直刺): Crisp closed-feet straight thrust with high overhead left Kiếm Quyết."""
    g0 = _guard()
    chamber = _guard(
        hips_y=0.865, hips_yaw=-34.0, hips_pitch=6.0, hips_roll=3.5,
        torso_yaw=-10.0, torso_pitch=5.0, torso_roll=-2.5,
        r_shoulder_fwd=-6.0,
        r_hand_pos=(-0.26, 1.08, 0.16),
        blade_dir=(0.00, 0.08, 0.997),
        l_hand_pos=(0.14, 1.22, 0.36), l_hand_dir=(-0.18, 0.24, 0.95),
        l_foot_pos=(0.12, 0.080, 0.10), r_foot_pos=(-0.15, 0.110, -0.08),
    )
    bingbu_thrust = _guard(
        hips_y=0.905, hips_yaw=20.0, hips_pitch=5.5, hips_roll=-3.0,
        torso_yaw=15.0, torso_pitch=4.5, torso_roll=2.5, head_yaw=-10.0,
        r_shoulder_lift=4.0, r_shoulder_fwd=22.0, l_shoulder_lift=18.0, l_shoulder_fwd=6.0,
        # Feet snapped together (Bìngbù); razor-straight horizontal thrust along +Z!
        r_hand_pos=(-0.06, 1.20, 0.52),
        blade_dir=(0.00, 0.04, 0.999),
        # Left Kiếm Quyết crowns high overhead framing the temple
        l_hand_pos=(0.20, 1.70, 0.18), l_hand_dir=(-0.26, 0.52, 0.81),
        l_foot_pos=(0.09, 0.080, 0.04), l_foot_yaw=4.0,
        r_foot_pos=(-0.09, 0.080, 0.04), r_foot_yaw=-4.0,
    )
    hold_poise = replace(
        bingbu_thrust, hips_y=0.914,
        r_hand_pos=(-0.06, 1.21, 0.51), l_hand_pos=(0.21, 1.72, 0.17),
    )
    return _compile_clip(
        name="js02_bingbu_zhici", slot="jianshu_thrust", duration=0.96,
        tags=["jianshu", "wushu", "section1", "ci_jian", "bingbu"],
        description="Closed-feet straight horizontal thrust with overhead Kiếm Quyết (Tịnh Bộ Trực Thích).",
        hit_window=(0.28, 0.56),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.22, chamber, "flow"),
            Keyframe(0.44, bingbu_thrust, "snap", tension=0.20),
            Keyframe(0.68, hold_poise, "flow"),
            Keyframe(0.96, g0, "flow"),
        ],
    )


def make_js03_gongbu_pijian() -> AnimationClip:
    """03. Cung Bộ Phách Kiếm (弓步劈剑): Overhead vertical crescent into crisp chopping cleave in Bow Stance."""
    g0 = _guard()
    raise_high = _guard(
        hips_y=0.945, hips_yaw=-18.0, hips_pitch=-3.0, hips_roll=3.0,
        torso_yaw=-10.0, torso_pitch=-6.0, torso_roll=-2.5, head_pitch=-3.0,
        r_shoulder_lift=20.0, r_shoulder_fwd=10.0, l_shoulder_lift=8.0, l_shoulder_fwd=12.0,
        r_hand_pos=(-0.20, 1.74, 0.24),
        blade_dir=(-0.08, 0.86, 0.50),
        l_hand_pos=(0.20, 1.32, 0.34), l_hand_dir=(-0.18, 0.42, 0.89),
        l_foot_pos=(0.16, 0.115, 0.22),
        r_foot_pos=(-0.20, 0.080, -0.22),
    )
    chop_mid = _guard(
        hips_y=0.845, hips_yaw=6.0, hips_pitch=10.0, torso_yaw=8.0, torso_pitch=9.0,
        r_shoulder_lift=8.0, r_shoulder_fwd=22.0,
        r_hand_pos=(-0.08, 1.34, 0.48),
        blade_dir=(0.02, 0.28, 0.96), blade_edge=(0.0, -0.96, 0.28),
        l_hand_pos=(0.38, 1.34, 0.08), l_hand_dir=(0.56, 0.40, 0.72),
        l_foot_pos=(0.16, 0.080, 0.30), r_foot_pos=(-0.22, 0.080, -0.26),
    )
    gongbu_chop = _guard(
        hips_y=0.780, hips_yaw=20.0, hips_pitch=14.0, hips_roll=-4.0,
        torso_yaw=14.0, torso_pitch=12.0, torso_roll=3.5, head_pitch=-3.0,
        r_shoulder_lift=-2.0, r_shoulder_fwd=24.0, l_shoulder_lift=10.0, l_shoulder_fwd=-12.0,
        r_hand_pos=(-0.06, 1.00, 0.50),
        blade_dir=(0.02, -0.18, 0.98), blade_edge=(0.0, -0.98, -0.18),
        l_hand_pos=(0.44, 1.38, -0.18), l_hand_dir=(0.74, 0.42, -0.52),
        l_foot_pos=(0.16, 0.080, 0.35), l_foot_yaw=8.0,
        r_foot_pos=(-0.22, 0.080, -0.30), r_foot_yaw=-28.0,
    )
    chop_drift = replace(
        gongbu_chop, hips_y=0.798,
        r_hand_pos=(-0.06, 1.02, 0.49), blade_dir=(0.02, -0.12, 0.99),
    )
    return _compile_clip(
        name="js03_gongbu_pijian", slot="jianshu_slash", duration=0.98,
        tags=["jianshu", "wushu", "section1", "pi_jian", "gongbu"],
        description="Vertical chopping cleave in deep Competition Bow Stance (Cung Bộ Phách Kiếm).",
        hit_window=(0.30, 0.56),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.22, raise_high, "flow"),
            Keyframe(0.37, chop_mid, "whip"),
            Keyframe(0.52, gongbu_chop, "heavy_drop"),
            Keyframe(0.72, chop_drift, "flow"),
            Keyframe(0.98, g0, "flow"),
        ],
    )


def make_js04_xubu_dianjian() -> AnimationClip:
    """04. Hư Bộ Điểm Kiếm (虚步点剑): Crisp Competition Empty Stance with razor-sharp downward wrist-snap point."""
    g0 = _guard()
    lift_wrist = _guard(
        hips_y=0.915, hips_yaw=-16.0, hips_pitch=2.0, hips_roll=2.0,
        torso_yaw=6.0, torso_pitch=0.0, torso_roll=-1.5,
        r_shoulder_lift=10.0, r_shoulder_fwd=18.0,
        r_hand_pos=(-0.10, 1.38, 0.44),
        blade_dir=(0.04, 0.46, 0.89),
        l_hand_pos=(0.24, 1.46, 0.26), l_hand_dir=(-0.22, 0.48, 0.85),
        l_foot_pos=(0.14, 0.120, 0.22),
        r_foot_pos=(-0.18, 0.080, -0.18),
    )
    xubu_dian = _guard(
        hips_y=0.798, hips_yaw=-24.0, hips_pitch=9.0, hips_roll=4.0,
        torso_yaw=14.0, torso_pitch=7.0, torso_roll=-3.0, head_pitch=2.0,
        r_shoulder_lift=4.0, r_shoulder_fwd=22.0, l_shoulder_lift=18.0, l_shoulder_fwd=6.0,
        # Crisp Diǎn Jiàn: arm extended high-forward, wrist snaps blade downward cleanly!
        r_hand_pos=(-0.08, 1.16, 0.48),
        blade_dir=(0.04, -0.48, 0.88), blade_edge=(0.0, -0.88, -0.48),
        l_hand_pos=(0.20, 1.64, 0.20), l_hand_dir=(-0.30, 0.46, 0.84),
        # Empty Stance (Xūbù): weight sunk on rear right leg, front left toe lightly touching
        l_foot_pos=(0.12, 0.098, 0.31), l_foot_yaw=8.0, l_foot_pitch=-18.0,
        r_foot_pos=(-0.18, 0.080, -0.20), r_foot_yaw=-28.0,
    )
    dian_poise = replace(
        xubu_dian, hips_y=0.810,
        r_hand_pos=(-0.08, 1.18, 0.47), blade_dir=(0.04, -0.44, 0.90),
    )
    return _compile_clip(
        name="js04_xubu_dianjian", slot="jianshu_point", duration=0.94,
        tags=["jianshu", "wushu", "section1", "dian_jian", "xubu"],
        description="Crisp downward wrist-snap Pointing Sword in Empty Stance (Hư Bộ Điểm Kiếm).",
        hit_window=(0.28, 0.54),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.22, lift_wrist, "flow"),
            Keyframe(0.42, xubu_dian, "snap"),
            Keyframe(0.66, dian_poise, "flow"),
            Keyframe(0.94, g0, "flow"),
        ],
    )


def make_js05_dingbu_bengjian() -> AnimationClip:
    """05. Đinh Bộ Băng Kiếm (丁步崩剑): T-Step sharp upward wrist-flick snapping tip skyward (Bēng Jiàn)."""
    g0 = _guard()
    dip_tip = _guard(
        hips_y=0.840, hips_yaw=-14.0, hips_pitch=9.0, torso_yaw=8.0, torso_pitch=7.0,
        r_shoulder_lift=-4.0, r_shoulder_fwd=16.0,
        r_hand_pos=(-0.16, 1.00, 0.42),
        blade_dir=(0.06, -0.28, 0.96),
        l_hand_pos=(0.18, 1.16, 0.34), l_hand_dir=(-0.28, 0.20, 0.94),
        l_foot_pos=(0.14, 0.080, 0.14), r_foot_pos=(-0.16, 0.080, -0.14),
    )
    dingbu_beng = _guard(
        hips_y=0.865, hips_yaw=-26.0, hips_pitch=4.0, hips_roll=3.5,
        torso_yaw=14.0, torso_pitch=1.0, torso_roll=-3.0, head_pitch=-3.0,
        r_shoulder_lift=6.0, r_shoulder_fwd=20.0, l_shoulder_lift=4.0, l_shoulder_fwd=12.0,
        r_hand_pos=(-0.12, 1.16, 0.48),
        blade_dir=(0.04, 0.72, 0.69),
        # Left Kiếm Quyết braces near right forearm (Phụ Kiếm poise)
        l_hand_pos=(0.10, 1.22, 0.38), l_hand_dir=(-0.38, 0.42, 0.82),
        # T-Stance (Dīngbù): left foot rooted, right toe tucked beside left arch
        l_foot_pos=(0.14, 0.080, 0.06), l_foot_yaw=10.0,
        r_foot_pos=(-0.05, 0.098, 0.04), r_foot_yaw=-16.0, r_foot_pitch=-20.0,
    )
    beng_drift = replace(
        dingbu_beng, hips_y=0.878,
        r_hand_pos=(-0.12, 1.18, 0.47), blade_dir=(0.04, 0.75, 0.66),
    )
    return _compile_clip(
        name="js05_dingbu_bengjian", slot="jianshu_point", duration=0.90,
        tags=["jianshu", "wushu", "section1", "beng_jian", "dingbu"],
        description="Crisp upward wrist-flick Tilting Sword in T-Step (Đinh Bộ Băng Kiếm).",
        hit_window=(0.24, 0.50),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.20, dip_tip, "flow"),
            Keyframe(0.40, dingbu_beng, "whip"),
            Keyframe(0.64, beng_drift, "flow"),
            Keyframe(0.90, g0, "flow"),
        ],
    )


def make_js06_guajian_gongbu_ci() -> AnimationClip:
    """06. Quải Kiếm Cung Bộ Thích (挂剑弓步刺): Close-body vertical Hooking Parry flowing into a Bow-Stance thrust."""
    g0 = _guard()
    gua_hook = _guard(
        hips_y=0.865, hips_yaw=-38.0, hips_pitch=6.0, hips_roll=4.5,
        torso_yaw=-14.0, torso_pitch=4.0, torso_roll=-3.5,
        r_shoulder_lift=-2.0, r_shoulder_fwd=-6.0,
        r_hand_pos=(-0.36, 1.04, 0.16),
        blade_dir=(-0.22, -0.46, 0.86),
        l_hand_pos=(0.22, 1.24, 0.34), l_hand_dir=(-0.24, 0.28, 0.93),
        l_foot_pos=(0.16, 0.080, 0.18), r_foot_pos=(-0.21, 0.080, -0.20),
    )
    gua_chamber = _guard(
        hips_y=0.840, hips_yaw=-26.0, hips_pitch=8.0, hips_roll=2.5,
        torso_yaw=-6.0, torso_pitch=6.0, torso_roll=-2.0,
        r_hand_pos=(-0.26, 1.06, 0.18),
        blade_dir=(0.00, 0.06, 0.998),
        l_hand_pos=(0.34, 1.28, 0.18), l_hand_dir=(0.32, 0.32, 0.89),
    )
    gongbu_ci = _guard(
        hips_y=0.785, hips_yaw=20.0, hips_pitch=14.0, hips_roll=-4.5,
        torso_yaw=16.0, torso_pitch=13.0, torso_roll=4.0, head_pitch=-3.0,
        r_shoulder_lift=2.0, r_shoulder_fwd=26.0, l_shoulder_lift=10.0, l_shoulder_fwd=-12.0,
        r_hand_pos=(-0.05, 1.10, 0.53),
        blade_dir=(0.00, 0.04, 0.999), blade_edge=(1.0, 0.0, 0.0),
        l_hand_pos=(0.44, 1.38, -0.18), l_hand_dir=(0.74, 0.42, -0.52),
        l_foot_pos=(0.16, 0.080, 0.35),
        r_foot_pos=(-0.22, 0.080, -0.30),
    )
    ci_drift = replace(
        gongbu_ci, hips_y=0.805,
        r_hand_pos=(-0.06, 1.12, 0.51), l_hand_pos=(0.42, 1.38, -0.14),
    )
    return _compile_clip(
        name="js06_guajian_gongbu_ci", slot="jianshu_thrust", duration=1.04,
        tags=["jianshu", "wushu", "section1", "gua_jian", "ci_jian", "gongbu"],
        description="Vertical Hooking Parry flowing into a deep Bow Stance Thrust (Quải Kiếm Cung Bộ Thích).",
        hit_window=(0.44, 0.72),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.22, gua_hook, "whip"),
            Keyframe(0.38, gua_chamber, "flow"),
            Keyframe(0.56, gongbu_ci, "snap"),
            Keyframe(0.78, ci_drift, "flow"),
            Keyframe(1.04, g0, "flow"),
        ],
    )


def make_js07_zheng_liaojian() -> AnimationClip:
    """07. Chính Liêu Kiếm (正撩剑): Sweeping vertical rising uppercut arc on the right flank."""
    g0 = _guard()
    dip_back = _guard(
        hips_y=0.825, hips_yaw=-38.0, hips_pitch=10.0, hips_roll=4.5,
        torso_yaw=-14.0, torso_pitch=8.0, torso_roll=-3.5,
        r_shoulder_lift=-6.0, r_shoulder_fwd=-10.0,
        r_hand_pos=(-0.38, 0.96, -0.02),
        blade_dir=(-0.32, -0.44, 0.84),
        l_hand_pos=(0.22, 1.20, 0.34), l_hand_dir=(-0.18, 0.24, 0.95),
        l_foot_pos=(0.16, 0.080, 0.22), r_foot_pos=(-0.22, 0.080, -0.22),
    )
    liao_mid = _guard(
        hips_y=0.845, hips_yaw=-10.0, hips_pitch=9.0, torso_yaw=4.0, torso_pitch=7.0,
        r_shoulder_lift=0.0, r_shoulder_fwd=14.0,
        r_hand_pos=(-0.24, 1.04, 0.36),
        blade_dir=(-0.06, 0.20, 0.98),
        l_hand_pos=(0.24, 1.28, 0.28), l_hand_dir=(-0.12, 0.36, 0.92),
    )
    liao_high = _guard(
        hips_y=0.905, hips_yaw=14.0, hips_pitch=4.0, hips_roll=-3.5,
        torso_yaw=14.0, torso_pitch=0.0, torso_roll=3.0, head_pitch=-3.0,
        r_shoulder_lift=16.0, r_shoulder_fwd=22.0, l_shoulder_lift=6.0, l_shoulder_fwd=12.0,
        r_hand_pos=(-0.12, 1.52, 0.44),
        blade_dir=(0.04, 0.76, 0.65),
        l_hand_pos=(0.12, 1.46, 0.36), l_hand_dir=(-0.32, 0.56, 0.76),
        l_foot_pos=(0.16, 0.080, 0.28), r_foot_pos=(-0.20, 0.095, -0.22),
    )
    liao_drift = replace(
        liao_high, hips_y=0.916,
        r_hand_pos=(-0.12, 1.55, 0.42), blade_dir=(0.04, 0.80, 0.60),
    )
    return _compile_clip(
        name="js07_zheng_liaojian", slot="jianshu_slash", duration=0.98,
        tags=["jianshu", "wushu", "section1", "liao_jian", "uppercut"],
        description="Forehand vertical rising uppercut sword arc (Chính Liêu Kiếm).",
        hit_window=(0.28, 0.58),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.22, dip_back, "flow"),
            Keyframe(0.38, liao_mid, "whip"),
            Keyframe(0.56, liao_high, "flow"),
            Keyframe(0.74, liao_drift, "flow"),
            Keyframe(0.98, g0, "flow"),
        ],
    )


def make_js08_fan_liaojian() -> AnimationClip:
    """08. Phản Liêu Kiếm (反撩剑): Cross-body reverse backhand rising uppercut on the left flank."""
    g0 = _guard()
    cross_low = _guard(
        hips_y=0.815, hips_yaw=30.0, hips_pitch=12.0, hips_roll=-4.5,
        torso_yaw=18.0, torso_pitch=10.0, torso_roll=4.0,
        r_shoulder_lift=-4.0, r_shoulder_fwd=22.0, l_shoulder_lift=6.0, l_shoulder_fwd=-10.0,
        r_hand_pos=(0.20, 0.98, 0.34),
        blade_dir=(0.44, -0.32, 0.84),
        l_hand_pos=(0.42, 1.28, -0.14), l_hand_dir=(0.72, 0.36, -0.59),
        l_foot_pos=(0.18, 0.080, 0.24), r_foot_pos=(-0.22, 0.080, -0.22),
    )
    fan_rise = _guard(
        hips_y=0.875, hips_yaw=-8.0, hips_pitch=6.0, hips_roll=2.5,
        torso_yaw=-4.0, torso_pitch=3.0, torso_roll=-2.0,
        r_shoulder_lift=10.0, r_shoulder_fwd=18.0,
        r_hand_pos=(0.04, 1.38, 0.46),
        blade_dir=(0.14, 0.58, 0.80),
        l_hand_pos=(0.30, 1.34, 0.16), l_hand_dir=(0.22, 0.44, 0.87),
    )
    fan_apex = _guard(
        hips_y=0.920, hips_yaw=-28.0, hips_pitch=1.0, hips_roll=4.5,
        torso_yaw=-14.0, torso_pitch=-3.0, torso_roll=-3.5, head_pitch=-3.0,
        r_shoulder_lift=18.0, r_shoulder_fwd=6.0,
        r_hand_pos=(-0.30, 1.60, 0.32),
        blade_dir=(-0.32, 0.80, 0.51),
        l_hand_pos=(0.22, 1.34, 0.34), l_hand_dir=(-0.26, 0.40, 0.88),
    )
    fan_drift = replace(fan_apex, hips_y=0.928, r_hand_pos=(-0.31, 1.62, 0.31))
    return _compile_clip(
        name="js08_fan_liaojian", slot="jianshu_slash", duration=0.98,
        tags=["jianshu", "wushu", "section1", "liao_jian", "reverse"],
        description="Reverse backhand rising uppercut sword arc (Phản Liêu Kiếm).",
        hit_window=(0.26, 0.58),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.22, cross_low, "flow"),
            Keyframe(0.42, fan_rise, "whip"),
            Keyframe(0.60, fan_apex, "flow"),
            Keyframe(0.76, fan_drift, "flow"),
            Keyframe(0.98, g0, "flow"),
        ],
    )


# ==============================================================================
# SECTION 2 (09-16): AGILE SWORDPLAY, WRIST FLOWERS & STATUTORY BALANCES
# ==============================================================================

def make_js09_yunjian_mojian() -> AnimationClip:
    """09. Vân Kiếm Mạt Kiếm (云剑抹剑): Overhead horizontal Cloud-Sword halo flowing into a chest-level Slicing Draw."""
    g0 = _guard()
    yun_left = _guard(
        hips_y=0.925, hips_yaw=18.0, hips_pitch=-2.0, hips_roll=-3.5,
        torso_yaw=12.0, torso_pitch=-5.0, torso_roll=3.0, head_pitch=-3.0,
        r_shoulder_lift=20.0, r_shoulder_fwd=16.0,
        r_hand_pos=(0.10, 1.74, 0.30),
        blade_dir=(0.72, 0.14, 0.68), blade_edge=(-0.68, 0.0, 0.72),
        l_hand_pos=(0.34, 1.34, 0.18), l_hand_dir=(0.34, 0.42, 0.84),
    )
    yun_right = _guard(
        hips_y=0.935, hips_yaw=-32.0, hips_pitch=-3.0, hips_roll=4.5,
        torso_yaw=-14.0, torso_pitch=-6.0, torso_roll=-3.5,
        r_shoulder_lift=20.0, r_shoulder_fwd=2.0,
        r_hand_pos=(-0.34, 1.72, 0.22),
        blade_dir=(-0.74, 0.18, 0.65), blade_edge=(0.65, 0.0, 0.74),
        l_hand_pos=(0.22, 1.30, 0.34), l_hand_dir=(-0.26, 0.38, 0.89),
    )
    mo_mid = _guard(
        hips_y=0.850, hips_yaw=0.0, hips_pitch=9.0, torso_yaw=4.0, torso_pitch=8.0,
        r_shoulder_lift=2.0, r_shoulder_fwd=22.0,
        r_hand_pos=(-0.06, 1.16, 0.48),
        blade_dir=(0.14, 0.06, 0.99), blade_edge=(0.99, 0.0, -0.14),
        l_hand_pos=(0.34, 1.26, 0.14), l_hand_dir=(0.44, 0.34, 0.83),
        l_foot_pos=(0.16, 0.080, 0.26), r_foot_pos=(-0.22, 0.080, -0.22),
    )
    mo_end = _guard(
        hips_y=0.835, hips_yaw=28.0, hips_pitch=9.0, hips_roll=-4.5,
        torso_yaw=18.0, torso_pitch=7.0, torso_roll=4.0,
        r_shoulder_lift=0.0, r_shoulder_fwd=22.0,
        r_hand_pos=(0.22, 1.14, 0.38),
        blade_dir=(0.76, 0.08, 0.64), blade_edge=(0.64, 0.0, -0.76),
        l_hand_pos=(0.44, 1.34, -0.16), l_hand_dir=(0.74, 0.40, -0.54),
    )
    return _compile_clip(
        name="js09_yunjian_mojian", slot="jianshu_slash", duration=1.10,
        tags=["jianshu", "wushu", "section2", "yun_jian", "mo_jian"],
        description="Overhead Cloud-Sword halo flowing into flat horizontal Slicing Draw (Vân Kiếm Mạt Kiếm).",
        hit_window=(0.50, 0.82),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.24, yun_left, "flow"),
            Keyframe(0.46, yun_right, "whip"),
            Keyframe(0.66, mo_mid, "whip"),
            Keyframe(0.84, mo_end, "flow"),
            Keyframe(1.10, g0, "flow"),
        ],
    )


def make_js10_jiaojian_chuanjian() -> AnimationClip:
    """10. Giảo Kiếm Xuyên Kiếm (绞剑穿剑): True 3D conical Coiling parry threading into linear Chuān Jiàn."""
    g0 = _guard()
    jiao_up_l = _guard(
        hips_y=0.885, hips_yaw=-8.0, hips_pitch=4.0, torso_yaw=12.0, torso_pitch=3.0,
        r_hand_pos=(-0.08, 1.26, 0.44),
        blade_dir=(0.34, 0.52, 0.78),
        l_hand_pos=(0.20, 1.26, 0.36), l_hand_dir=(-0.28, 0.30, 0.91),
    )
    jiao_low_r = _guard(
        hips_y=0.850, hips_yaw=-32.0, hips_pitch=8.0, hips_roll=3.5,
        torso_yaw=-10.0, torso_pitch=6.0, torso_roll=-3.0,
        # Blade tip sweeps conical circle right-down-inward to coil opponent's weapon off-line!
        r_hand_pos=(-0.30, 1.04, 0.24),
        blade_dir=(-0.36, -0.26, 0.90),
        l_hand_pos=(0.26, 1.24, 0.26), l_hand_dir=(0.14, 0.28, 0.95),
    )
    chuan_thread = _guard(
        hips_y=0.825, hips_yaw=18.0, hips_pitch=13.0, hips_roll=-4.0,
        torso_yaw=16.0, torso_pitch=11.0, torso_roll=3.5,
        r_shoulder_lift=4.0, r_shoulder_fwd=26.0, l_shoulder_lift=8.0, l_shoulder_fwd=-12.0,
        r_hand_pos=(-0.04, 1.20, 0.54),
        blade_dir=(0.04, 0.14, 0.99),
        l_hand_pos=(0.42, 1.36, -0.16), l_hand_dir=(0.72, 0.42, -0.55),
        l_foot_pos=(0.16, 0.080, 0.33), r_foot_pos=(-0.22, 0.100, -0.26),
    )
    chuan_drift = replace(chuan_thread, hips_y=0.845, r_hand_pos=(-0.06, 1.22, 0.52))
    return _compile_clip(
        name="js10_jiaojian_chuanjian", slot="jianshu_thrust", duration=1.06,
        tags=["jianshu", "wushu", "section2", "jiao_jian", "chuan_jian"],
        description="Conical Coiling parry threading smoothly into linear Thrust (Giảo Kiếm Xuyên Kiếm).",
        hit_window=(0.46, 0.74),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.20, jiao_up_l, "flow"),
            Keyframe(0.40, jiao_low_r, "whip"),
            Keyframe(0.60, chuan_thread, "snap"),
            Keyframe(0.80, chuan_drift, "flow"),
            Keyframe(1.06, g0, "flow"),
        ],
    )


def make_js11_wan_hua_shuang_xuan() -> AnimationClip:
    """11. Song Uyển Hoa Kiếm (双腕花剑): True 3D figure-8 double conical wrist sword-flowers (left & right flanks)."""
    g0 = _guard()
    # Left-flank inner moulinet (Nèi Wànhuā): high-left -> low-left -> rising across
    hua_l_high = _guard(
        hips_y=0.895, hips_yaw=20.0, hips_pitch=4.0, hips_roll=-3.5,
        torso_yaw=14.0, torso_pitch=3.0, torso_roll=3.0,
        r_shoulder_lift=10.0, r_shoulder_fwd=22.0,
        r_hand_pos=(0.16, 1.38, 0.42),
        blade_dir=(0.42, 0.72, 0.55),
        l_hand_pos=(0.40, 1.32, 0.04), l_hand_dir=(0.64, 0.38, 0.67),
        l_foot_pos=(0.16, 0.080, 0.22), r_foot_pos=(-0.20, 0.080, -0.20),
    )
    hua_l_low = _guard(
        hips_y=0.855, hips_yaw=26.0, hips_pitch=8.0, hips_roll=-4.5,
        torso_yaw=16.0, torso_pitch=6.0, torso_roll=4.0,
        r_shoulder_lift=-2.0, r_shoulder_fwd=22.0,
        r_hand_pos=(0.20, 1.04, 0.38),
        blade_dir=(0.46, -0.52, 0.72),
        l_hand_pos=(0.44, 1.34, -0.10), l_hand_dir=(0.72, 0.40, -0.56),
    )
    # Right-flank outer moulinet (Wài Wànhuā): high-right -> low-right -> crisp poised guard
    hua_r_high = _guard(
        hips_y=0.910, hips_yaw=-26.0, hips_pitch=2.0, hips_roll=4.0,
        torso_yaw=-12.0, torso_pitch=0.0, torso_roll=-3.5,
        r_shoulder_lift=14.0, r_shoulder_fwd=6.0,
        r_hand_pos=(-0.38, 1.44, 0.28),
        blade_dir=(-0.48, 0.74, 0.47),
        l_hand_pos=(0.22, 1.32, 0.34), l_hand_dir=(-0.26, 0.40, 0.88),
    )
    hua_r_low = _guard(
        hips_y=0.870, hips_yaw=-36.0, hips_pitch=6.0, hips_roll=5.0,
        torso_yaw=-16.0, torso_pitch=4.0, torso_roll=-4.0,
        r_shoulder_lift=-4.0, r_shoulder_fwd=-4.0,
        r_hand_pos=(-0.40, 1.04, 0.20),
        blade_dir=(-0.44, -0.50, 0.75),
        l_hand_pos=(0.20, 1.28, 0.36), l_hand_dir=(-0.28, 0.36, 0.89),
    )
    poise_finish = _guard(
        hips_y=0.895, hips_yaw=-18.0, torso_yaw=14.0,
        r_hand_pos=(-0.14, 1.18, 0.42), blade_dir=(0.14, 0.46, 0.88),
        l_hand_pos=(0.24, 1.30, 0.32), l_hand_dir=(-0.28, 0.38, 0.88),
    )
    return _compile_clip(
        name="js11_wan_hua_shuang_xuan", slot="jianshu_flourish", duration=1.24,
        tags=["jianshu", "wushu", "section2", "wan_hua", "kiem_hoa", "flourish"],
        description="Dazzling figure-8 double conical wrist sword-flowers (Song Uyển Hoa Kiếm).",
        hit_window=(0.18, 0.94),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.22, hua_l_high, "flow"),
            Keyframe(0.44, hua_l_low, "whip"),
            Keyframe(0.68, hua_r_high, "flow"),
            Keyframe(0.90, hua_r_low, "whip"),
            Keyframe(1.06, poise_finish, "flow"),
            Keyframe(1.24, g0, "flow"),
        ],
    )


def make_js12_tixi_dianjian() -> AnimationClip:
    """12. Đề Tất Điểm Kiếm (提膝点剑): Single-leg high knee-lift balance (Tí Xī Pínghéng) with downward point."""
    g0 = _guard()
    gather_step = _guard(
        hips_y=0.855, hips_yaw=-14.0, hips_pitch=5.0, torso_yaw=8.0, torso_pitch=4.0,
        r_hand_pos=(-0.20, 1.34, 0.34), blade_dir=(0.10, 0.56, 0.82),
        l_hand_pos=(0.20, 1.28, 0.34),
        l_foot_pos=(0.14, 0.080, 0.08), r_foot_pos=(-0.18, 0.080, -0.14),
    )
    tixi_balance = _guard(
        hips_y=0.955, hips_yaw=8.0, hips_pitch=-1.5, hips_roll=-4.0,
        torso_yaw=10.0, torso_pitch=-2.5, torso_roll=3.5, head_pitch=2.0,
        r_shoulder_lift=8.0, r_shoulder_fwd=22.0, l_shoulder_lift=18.0, l_shoulder_fwd=8.0,
        r_hand_pos=(-0.06, 1.24, 0.48),
        blade_dir=(0.04, -0.50, 0.86), blade_edge=(0.0, -0.86, -0.50),
        l_hand_pos=(0.18, 1.72, 0.20), l_hand_dir=(-0.30, 0.46, 0.84),
        # Left standing leg straight; right knee lifted high above waist with pointed toes!
        l_foot_pos=(0.12, 0.080, 0.04), l_foot_yaw=8.0,
        r_foot_pos=(-0.12, 0.620, 0.24),
    )
    tixi_hold = replace(
        tixi_balance, hips_y=0.965,
        r_hand_pos=(-0.06, 1.26, 0.47),
        r_foot_pos=(-0.12, 0.640, 0.24),
    )
    return _compile_clip(
        name="js12_tixi_dianjian", slot="jianshu_balance", duration=1.12,
        tags=["jianshu", "wushu", "section2", "balance", "tixi", "dian_jian"],
        description="High knee-lift balance with downward Pointing Sword (Đề Tất Điểm Kiếm).",
        hit_window=(0.32, 0.72),
        r_air_flex=30.0,
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.20, gather_step, "flow"),
            Keyframe(0.44, tixi_balance, "snap"),
            Keyframe(0.80, tixi_hold, "flow"),
            Keyframe(1.12, g0, "flow"),
        ],
    )


def make_js13_pubu_saojian() -> AnimationClip:
    """13. Phốc Bộ Tảo Kiếm (仆步扫剑): Deep low crouching Pūbù stance carving a razor-flat horizontal sweep."""
    g0 = _guard()
    pubu_coil = _guard(
        hips_y=0.625, hips_yaw=-40.0, hips_pitch=16.0, hips_roll=6.0,
        torso_yaw=-16.0, torso_pitch=14.0, torso_roll=-5.0, head_pitch=-5.0,
        r_shoulder_lift=-6.0, r_shoulder_fwd=-8.0, l_shoulder_lift=14.0, l_shoulder_fwd=10.0,
        r_hand_pos=(-0.38, 0.62, 0.12),
        blade_dir=(-0.68, -0.06, 0.73), blade_edge=(0.73, 0.0, 0.68),
        l_hand_pos=(0.24, 1.22, 0.24), l_hand_dir=(-0.18, 0.48, 0.86),
        l_foot_pos=(0.42, 0.080, 0.28), l_foot_yaw=20.0,
        r_foot_pos=(-0.22, 0.080, -0.20), r_foot_yaw=-32.0,
    )
    pubu_sweep = _guard(
        hips_y=0.600, hips_yaw=24.0, hips_pitch=18.0, hips_roll=-6.0,
        torso_yaw=16.0, torso_pitch=16.0, torso_roll=5.0, head_pitch=-5.0,
        r_shoulder_lift=-8.0, r_shoulder_fwd=22.0, l_shoulder_lift=12.0, l_shoulder_fwd=-10.0,
        r_hand_pos=(0.14, 0.56, 0.42),
        blade_dir=(0.60, -0.04, 0.80), blade_edge=(0.80, 0.0, -0.60),
        l_hand_pos=(0.42, 1.14, -0.14), l_hand_dir=(0.72, 0.42, -0.55),
        l_foot_pos=(0.42, 0.080, 0.32), l_foot_yaw=20.0,
        r_foot_pos=(-0.22, 0.080, -0.22), r_foot_yaw=-32.0,
    )
    pubu_drift = replace(
        pubu_sweep, hips_y=0.635, hips_yaw=20.0,
        r_hand_pos=(0.18, 0.60, 0.40), blade_dir=(0.68, 0.04, 0.73),
    )
    return _compile_clip(
        name="js13_pubu_saojian", slot="jianshu_slash", duration=1.08,
        tags=["jianshu", "wushu", "section2", "pubu", "sao_jian", "low"],
        description="Deep crouching Pūbù low horizontal ankle-sweep (Phốc Bộ Tảo Kiếm).",
        hit_window=(0.30, 0.68),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.26, pubu_coil, "heavy_drop"),
            Keyframe(0.52, pubu_sweep, "whip"),
            Keyframe(0.76, pubu_drift, "flow"),
            Keyframe(1.08, g0, "flow"),
        ],
    )


def make_js14_xiebu_jiejian() -> AnimationClip:
    """14. Hiết Bộ Tiết Kiếm (歇步截剑): Agile cross-legged low Xiēbù crouch with diagonal intercepting cut."""
    g0 = _guard()
    step_cross = _guard(
        hips_y=0.830, hips_yaw=-28.0, hips_pitch=9.0, torso_yaw=-12.0, torso_pitch=7.0,
        r_hand_pos=(-0.34, 1.32, 0.24),
        blade_dir=(-0.40, 0.56, 0.72),
        l_hand_pos=(0.24, 1.24, 0.32),
        l_foot_pos=(0.14, 0.080, 0.14), r_foot_pos=(-0.08, 0.120, -0.16),
    )
    xiebu_jie = _guard(
        hips_y=0.640, hips_yaw=22.0, hips_pitch=16.0, hips_roll=-4.5,
        torso_yaw=15.0, torso_pitch=14.0, torso_roll=4.0, head_pitch=-4.0,
        r_shoulder_lift=-4.0, r_shoulder_fwd=22.0, l_shoulder_lift=14.0, l_shoulder_fwd=6.0,
        r_hand_pos=(-0.08, 0.74, 0.42),
        blade_dir=(0.26, -0.14, 0.96),
        l_hand_pos=(0.30, 1.26, 0.16), l_hand_dir=(0.32, 0.52, 0.79),
        l_foot_pos=(0.12, 0.080, 0.16), l_foot_yaw=20.0,
        r_foot_pos=(0.04, 0.100, -0.14), r_foot_yaw=-22.0, r_foot_pitch=-20.0,
    )
    xiebu_poise = replace(
        xiebu_jie, hips_y=0.658,
        r_hand_pos=(-0.06, 0.76, 0.41), blade_dir=(0.28, -0.10, 0.95),
    )
    return _compile_clip(
        name="js14_xiebu_jiejian", slot="jianshu_parry", duration=0.98,
        tags=["jianshu", "wushu", "section2", "xiebu", "jie_jian", "low"],
        description="Cross-legged Xiēbù crouch with crisp diagonal intercepting cut (Hiết Bộ Tiết Kiếm).",
        hit_window=(0.28, 0.58),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.22, step_cross, "flow"),
            Keyframe(0.44, xiebu_jie, "heavy_drop"),
            Keyframe(0.68, xiebu_poise, "flow"),
            Keyframe(0.98, g0, "flow"),
        ],
    )


def make_js15_yanshi_pingheng() -> AnimationClip:
    """15. Yến Thức Bình Hành (燕式平衡): Iconic Competition Swallow Scale balance — horizontal torso & soaring wings."""
    g0 = _guard()
    prep = _guard(
        hips_y=0.920, hips_yaw=-10.0, hips_pitch=12.0, torso_yaw=6.0, torso_pitch=10.0,
        r_hand_pos=(-0.34, 1.18, 0.22), blade_dir=(-0.54, 0.28, 0.79),
        l_hand_pos=(0.34, 1.24, 0.22), l_hand_dir=(0.48, 0.28, 0.83),
        l_foot_pos=(0.12, 0.080, 0.06), r_foot_pos=(-0.12, 0.340, -0.32),
    )
    swallow_scale = _guard(
        hips_y=0.900, hips_yaw=0.0, hips_pitch=32.0, hips_roll=-3.0,
        torso_yaw=0.0, torso_pitch=34.0, torso_roll=2.5, head_pitch=-30.0,
        r_shoulder_lift=14.0, r_shoulder_fwd=-8.0, l_shoulder_lift=14.0, l_shoulder_fwd=-8.0,
        # Arms & sword spread wide horizontally like a gliding swallow!
        r_hand_pos=(-0.50, 1.16, 0.02),
        blade_dir=(-0.90, 0.16, -0.40),
        l_hand_pos=(0.50, 1.18, 0.06), l_hand_dir=(0.88, 0.22, 0.42),
        l_foot_pos=(0.10, 0.080, 0.02), l_foot_yaw=8.0,
        r_foot_pos=(-0.10, 0.940, -0.74),
    )
    swallow_hold = replace(
        swallow_scale, hips_y=0.908,
        r_hand_pos=(-0.51, 1.18, 0.01), l_hand_pos=(0.51, 1.20, 0.05),
        r_foot_pos=(-0.10, 0.960, -0.75),
    )
    return _compile_clip(
        name="js15_yanshi_pingheng", slot="jianshu_balance", duration=1.38,
        tags=["jianshu", "wushu", "section2", "balance", "yanshi", "swallow"],
        description="Iconic Competition Swallow Scale Balance (Yến Thức Bình Hành).",
        r_air_flex=28.0,
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.26, prep, "flow"),
            Keyframe(0.52, swallow_scale, "flow", tension=0.20),
            Keyframe(0.98, swallow_hold, "flow"),
            Keyframe(1.38, g0, "flow"),
        ],
    )


def make_js16_wangyue_pingheng() -> AnimationClip:
    """16. Vọng Nguyệt Bình Hành (望月平衡): Iconic Competition Moon-Gazing balance — arched torso & high rear fold."""
    g0 = _guard()
    prep = _guard(
        hips_y=0.925, hips_yaw=-16.0, hips_pitch=0.0, hips_roll=4.0,
        torso_yaw=-10.0, torso_pitch=-4.0, torso_roll=-4.0,
        r_hand_pos=(-0.38, 1.38, 0.16), blade_dir=(-0.64, 0.58, 0.50),
        l_hand_pos=(0.24, 1.50, 0.24), l_hand_dir=(-0.18, 0.54, 0.82),
        l_foot_pos=(0.12, 0.080, 0.04), r_foot_pos=(-0.14, 0.340, -0.26),
    )
    moon_gaze = _guard(
        hips_y=0.935, hips_yaw=-32.0, hips_pitch=-8.0, hips_roll=10.0,
        torso_yaw=-22.0, torso_pitch=-14.0, torso_roll=-8.5, head_yaw=-24.0, head_pitch=-8.0,
        r_shoulder_lift=14.0, r_shoulder_fwd=-8.0, l_shoulder_lift=20.0, l_shoulder_fwd=10.0,
        r_hand_pos=(-0.46, 1.44, -0.08),
        blade_dir=(-0.76, 0.54, -0.36),
        l_hand_pos=(0.14, 1.76, 0.20), l_hand_dir=(-0.44, 0.42, 0.79),
        l_foot_pos=(0.10, 0.080, 0.02), l_foot_yaw=10.0,
        r_foot_pos=(0.04, 0.740, -0.50),
    )
    moon_hold = replace(
        moon_gaze, hips_y=0.945,
        r_hand_pos=(-0.47, 1.46, -0.09), l_hand_pos=(0.13, 1.78, 0.19),
        r_foot_pos=(0.05, 0.760, -0.51),
    )
    return _compile_clip(
        name="js16_wangyue_pingheng", slot="jianshu_balance", duration=1.38,
        tags=["jianshu", "wushu", "section2", "balance", "wangyue", "moon_gazing"],
        description="Iconic Competition Moon-Gazing Balance (Vọng Nguyệt Bình Hành).",
        r_air_flex=30.0,
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.26, prep, "flow"),
            Keyframe(0.52, moon_gaze, "flow", tension=0.20),
            Keyframe(0.98, moon_hold, "flow"),
            Keyframe(1.38, g0, "flow"),
        ],
    )


# ==============================================================================
# SECTION 3 (17-24): NÁNDÙ AERIAL JUMPS, 360 SPINS & DROP STRIKES
# ==============================================================================

def make_js17_tengkong_feijiao() -> AnimationClip:
    """17. Đằng Không Phi Cước (腾空飞脚): Competition Jumping Front Slap-Kick — left palm slaps high right instep!"""
    g0 = _guard()
    takeoff = _guard(
        hips_y=0.770, hips_yaw=-14.0, hips_pitch=12.0, torso_yaw=6.0, torso_pitch=10.0,
        r_hand_pos=(-0.36, 0.98, 0.16), blade_dir=(-0.52, 0.24, 0.82),
        l_hand_pos=(0.24, 1.06, 0.28),
        l_foot_pos=(0.14, 0.080, 0.16), r_foot_pos=(-0.18, 0.080, -0.18),
    )
    air_slap = _guard(
        hips_y=1.310, hips_yaw=8.0, hips_pitch=-6.0, hips_roll=-5.0,
        torso_yaw=-4.0, torso_pitch=-4.0, torso_roll=4.5, head_pitch=3.0,
        r_shoulder_lift=8.0, r_shoulder_fwd=-12.0, l_shoulder_lift=14.0, l_shoulder_fwd=22.0,
        # Right sword trails cleanly back-right-up; left palm slaps high right instep!
        r_hand_pos=(-0.46, 1.48, -0.10),
        blade_dir=(-0.72, 0.46, -0.52),
        l_hand_pos=(-0.02, 1.54, 0.50), l_hand_dir=(-0.14, 0.22, 0.96),
        l_foot_pos=(0.14, 0.680, -0.12),
        r_foot_pos=(-0.06, 1.480, 0.62),
    )
    land_absorb = _guard(
        hips_y=0.805, hips_yaw=-18.0, hips_pitch=10.0, torso_yaw=10.0, torso_pitch=8.0,
        r_hand_pos=(-0.24, 1.08, 0.32), blade_dir=(0.10, 0.36, 0.93),
        l_hand_pos=(0.24, 1.20, 0.30),
        l_foot_pos=(0.16, 0.080, 0.18), r_foot_pos=(-0.20, 0.080, -0.18),
    )
    return _compile_clip(
        name="js17_tengkong_feijiao", slot="jianshu_aerial", duration=1.04,
        tags=["jianshu", "wushu", "section3", "nandu", "jump", "feijiao"],
        description="Competition Jumping Front Slap-Kick holding Jian (Đằng Không Phi Cước).",
        hit_window=(0.34, 0.62),
        l_air_flex=26.0,
        r_air_flex=-12.0,  # Dorsiflexed instep for crisp slap-kick silhouette!
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.20, takeoff, "flow"),
            Keyframe(0.48, air_slap, "whip"),
            Keyframe(0.76, land_absorb, "heavy_drop"),
            Keyframe(1.04, g0, "flow"),
        ],
    )


def make_js18_xuanfengjiao_360() -> AnimationClip:
    """18. Toàn Phong Cước 360° (旋风脚): Iconic Nándù 360-degree airborne tornado slap-kick holding Jian."""
    def _xf(yaw: float, hy: float, r_pos, b_dir, l_pos, lf_pos, rf_pos,
            pitch: float = 4.0, hroll: float = 0.0, tyaw: float = 12.0, troll: float = 0.0) -> PoseSpec:
        return PoseSpec(
            hips_y=hy, hips_yaw=yaw, hips_pitch=pitch, hips_roll=hroll,
            torso_yaw=tyaw, torso_pitch=pitch, torso_roll=troll,
            head_yaw=0.0, head_pitch=2.0, head_roll=-troll * 0.4, head_world_space=False,
            l_shoulder_lift=10.0, l_shoulder_fwd=12.0, r_shoulder_lift=8.0, r_shoulder_fwd=-6.0,
            r_hand_pos=r_pos, blade_dir=b_dir,
            l_hand_pos=l_pos, l_hand_dir=(-0.12, 0.28, 0.95), l_hand_up=(0.0, 1.0, 0.0),
            l_foot_pos=lf_pos, l_foot_yaw=14.0,
            r_foot_pos=rf_pos, r_foot_yaw=-18.0,
            local_body_space=True, feet_in_body_space=True,
        )

    p0 = _spin_guard(-24.0)
    p_coil = _xf(-56.0, 0.780, (-0.38, 1.00, 0.14), (-0.60, 0.22, 0.77), (0.28, 1.12, 0.26),
                 (0.16, 0.080, 0.20), (-0.20, 0.080, -0.20),
                 pitch=12.0, hroll=5.0, tyaw=-12.0, troll=-4.0)
    p_rise = _xf(48.0, 1.200, (-0.42, 1.44, -0.04), (-0.68, 0.38, 0.62), (0.28, 1.50, 0.28),
                 (0.16, 0.560, 0.20), (-0.20, 0.420, -0.18),
                 pitch=4.0, hroll=-7.0, tyaw=14.0, troll=5.0)
    p_slap = _xf(168.0, 1.350, (-0.46, 1.54, -0.10), (-0.74, 0.44, -0.50), (0.02, 1.58, 0.48),
                 (0.16, 0.680, -0.10), (-0.06, 1.480, 0.60),
                 pitch=-6.0, hroll=-10.0, tyaw=16.0, troll=7.5)
    p_desc = _xf(276.0, 1.100, (-0.36, 1.32, 0.18), (-0.46, 0.38, 0.80), (0.26, 1.38, 0.24),
                 (0.16, 0.340, 0.18), (-0.20, 0.420, 0.06),
                 pitch=8.0, hroll=-4.5, tyaw=12.0, troll=3.5)
    p_land = _xf(342.0, 0.820, (-0.24, 1.06, 0.32), (0.14, 0.38, 0.91), (0.24, 1.20, 0.28),
                 (0.16, 0.080, 0.20), (-0.20, 0.080, -0.18),
                 pitch=9.0, hroll=-2.0, tyaw=12.0, troll=1.5)
    p_end = _spin_guard(336.0)
    return _compile_clip(
        name="js18_xuanfengjiao_360", slot="jianshu_aerial", duration=1.12,
        tags=["jianshu", "wushu", "section3", "nandu", "jump", "spin", "xuanfengjiao", "360"],
        description="Airborne 360-deg Competition Tornado Kick holding Jian (Toàn Phong Cước 360°).",
        hit_window=(0.38, 0.70),
        l_air_flex=24.0,
        r_air_flex=-10.0,
        smooth_passes=3,
        keyframes=[
            Keyframe(0.00, p0, "flow"),
            Keyframe(0.18, p_coil, "flow"),
            Keyframe(0.36, p_rise, "whip"),
            Keyframe(0.56, p_slap, "whip"),
            Keyframe(0.76, p_desc, "flow"),
            Keyframe(0.94, p_land, "heavy_drop"),
            Keyframe(1.12, p_end, "flow"),
        ],
    )


def make_js19_tengkong_bailian_360() -> AnimationClip:
    """19. Đằng Không Bãi Liên 360° (腾空摆莲): Airborne 360-deg Jumping Outer-Lotus Kick with sword poise."""
    def _bl(yaw: float, hy: float, r_pos, b_dir, l_pos, lf_pos, rf_pos,
            pitch: float = 4.0, hroll: float = 0.0, tyaw: float = -12.0, troll: float = 0.0) -> PoseSpec:
        return PoseSpec(
            hips_y=hy, hips_yaw=yaw, hips_pitch=pitch, hips_roll=hroll,
            torso_yaw=tyaw, torso_pitch=pitch, torso_roll=troll,
            head_yaw=0.0, head_pitch=2.0, head_roll=-troll * 0.4, head_world_space=False,
            l_shoulder_lift=10.0, l_shoulder_fwd=12.0, r_shoulder_lift=8.0, r_shoulder_fwd=8.0,
            r_hand_pos=r_pos, blade_dir=b_dir,
            l_hand_pos=l_pos, l_hand_dir=(-0.14, 0.28, 0.95), l_hand_up=(0.0, 1.0, 0.0),
            l_foot_pos=lf_pos, l_foot_yaw=12.0,
            r_foot_pos=rf_pos, r_foot_yaw=-16.0,
            local_body_space=True, feet_in_body_space=True,
        )

    p0 = _spin_guard(-24.0)
    p_coil = _bl(12.0, 0.780, (-0.14, 1.04, 0.34), (0.44, 0.30, 0.85), (0.34, 1.12, 0.20),
                 (0.18, 0.080, 0.20), (-0.20, 0.080, -0.20),
                 pitch=12.0, hroll=-4.5, tyaw=14.0, troll=3.5)
    p_rise = _bl(-96.0, 1.220, (-0.34, 1.48, 0.24), (-0.42, 0.42, 0.80), (0.24, 1.54, 0.34),
                 (0.16, 0.480, -0.12), (-0.16, 0.660, 0.28),
                 pitch=2.0, hroll=7.0, tyaw=-14.0, troll=-5.0)
    p_lotus = _bl(-212.0, 1.360, (-0.32, 1.56, 0.32), (-0.52, 0.38, 0.76), (0.04, 1.58, 0.50),
                  (0.16, 0.660, -0.14), (0.00, 1.460, 0.62),
                  pitch=-5.0, hroll=9.5, tyaw=-18.0, troll=-7.0)
    p_desc = _bl(-312.0, 1.080, (-0.30, 1.30, 0.28), (-0.22, 0.42, 0.88), (0.24, 1.36, 0.28),
                 (0.16, 0.320, 0.16), (-0.20, 0.380, -0.06),
                 pitch=8.0, hroll=4.5, tyaw=-10.0, troll=-3.5)
    p_land = _bl(-390.0, 0.820, (-0.22, 1.06, 0.34), (0.16, 0.38, 0.91), (0.24, 1.20, 0.28),
                 (0.16, 0.080, 0.20), (-0.20, 0.080, -0.18),
                 pitch=9.0, hroll=2.0, tyaw=10.0, troll=-1.5)
    p_end = _spin_guard(-384.0)
    return _compile_clip(
        name="js19_tengkong_bailian_360", slot="jianshu_aerial", duration=1.14,
        tags=["jianshu", "wushu", "section3", "nandu", "jump", "spin", "bailian", "360"],
        description="Airborne 360-deg Jumping Lotus Kick holding Jian (Đằng Không Bãi Liên 360°).",
        hit_window=(0.38, 0.72),
        l_air_flex=24.0,
        r_air_flex=-10.0,
        smooth_passes=3,
        keyframes=[
            Keyframe(0.00, p0, "flow"),
            Keyframe(0.18, p_coil, "flow"),
            Keyframe(0.37, p_rise, "whip"),
            Keyframe(0.57, p_lotus, "whip"),
            Keyframe(0.77, p_desc, "flow"),
            Keyframe(0.96, p_land, "heavy_drop"),
            Keyframe(1.14, p_end, "flow"),
        ],
    )


def make_js20_xuanzi_butterfly() -> AnimationClip:
    """20. Toàn Tử Lăng Không (旋子): Soaring horizontal Butterfly Kick spin carving a clean aerial sword halo."""
    def _xz(yaw: float, hy: float, pitch: float, hroll: float, lf_pos, rf_pos, tyaw: float = 10.0) -> PoseSpec:
        return PoseSpec(
            hips_y=hy, hips_yaw=yaw, hips_pitch=pitch, hips_roll=hroll,
            torso_yaw=tyaw, torso_pitch=pitch, torso_roll=-hroll * 0.4,
            head_yaw=0.0, head_pitch=-pitch * 0.5, head_world_space=False,
            l_shoulder_lift=10.0, l_shoulder_fwd=-6.0, r_shoulder_lift=10.0, r_shoulder_fwd=-6.0,
            r_hand_pos=(-0.46, hy + 0.18, 0.04),
            blade_dir=(-0.84, 0.16, 0.52),
            l_hand_pos=(0.46, hy + 0.22, 0.06),
            l_hand_dir=(0.84, 0.24, 0.48), l_hand_up=(0.0, 1.0, 0.0),
            l_foot_pos=lf_pos, l_foot_yaw=14.0,
            r_foot_pos=rf_pos, r_foot_yaw=-18.0,
            local_body_space=True, feet_in_body_space=True,
        )

    p0 = _spin_guard(-24.0)
    p_dip = _xz(-60.0, 0.760, 22.0, 7.0, (0.20, 0.080, 0.22), (-0.22, 0.080, -0.20), tyaw=-12.0)
    p_fly1 = _xz(44.0, 1.180, 30.0, -9.0, (0.22, 0.720, -0.34), (-0.22, 0.940, -0.56), tyaw=14.0)
    p_fly2 = _xz(168.0, 1.240, 32.0, -11.0, (0.22, 1.020, -0.56), (-0.22, 0.880, -0.44), tyaw=16.0)
    p_fly3 = _xz(278.0, 1.060, 20.0, -5.5, (0.18, 0.480, -0.16), (-0.20, 0.300, 0.12), tyaw=12.0)
    p_land = _xz(342.0, 0.820, 10.0, -2.0, (0.16, 0.080, 0.20), (-0.20, 0.080, -0.18), tyaw=10.0)
    p_end = _spin_guard(336.0)
    return _compile_clip(
        name="js20_xuanzi_butterfly", slot="jianshu_aerial", duration=1.14,
        tags=["jianshu", "wushu", "section3", "nandu", "butterfly", "xuanzi", "360"],
        description="Soaring horizontal Butterfly Kick spin with Jian (Toàn Tử Lăng Không).",
        hit_window=(0.34, 0.76),
        l_air_flex=28.0,
        r_air_flex=28.0,
        smooth_passes=3,
        keyframes=[
            Keyframe(0.00, p0, "flow"),
            Keyframe(0.18, p_dip, "flow"),
            Keyframe(0.38, p_fly1, "whip"),
            Keyframe(0.58, p_fly2, "flow"),
            Keyframe(0.78, p_fly3, "flow"),
            Keyframe(0.96, p_land, "heavy_drop"),
            Keyframe(1.14, p_end, "flow"),
        ],
    )


def make_js21_tiaobu_chajian() -> AnimationClip:
    """21. Khiêu Bộ Sáp Kiếm (跳步插剑): Buoyant forward vaulting leap driving into a piercing Bow-Stance lunge."""
    g0 = _guard()
    vault_air = _guard(
        hips_y=1.160, hips_yaw=-30.0, hips_pitch=4.0, hips_roll=4.0,
        torso_yaw=-12.0, torso_pitch=1.0, torso_roll=-3.0,
        r_shoulder_lift=-2.0, r_shoulder_fwd=-8.0, l_shoulder_lift=8.0, l_shoulder_fwd=14.0,
        r_hand_pos=(-0.28, 1.28, 0.12),
        blade_dir=(0.00, 0.06, 0.998),
        l_hand_pos=(0.20, 1.44, 0.36), l_hand_dir=(-0.14, 0.24, 0.96),
        l_foot_pos=(0.16, 0.460, 0.26),
        r_foot_pos=(-0.20, 0.340, -0.24),
    )
    land_stab = _guard(
        hips_y=0.780, hips_yaw=22.0, hips_pitch=15.0, hips_roll=-4.5,
        torso_yaw=16.0, torso_pitch=14.0, torso_roll=4.0, head_pitch=-4.0,
        r_shoulder_lift=2.0, r_shoulder_fwd=26.0, l_shoulder_lift=10.0, l_shoulder_fwd=-14.0,
        # Piercing downward-forward Chā Jiàn stab!
        r_hand_pos=(-0.05, 1.04, 0.52),
        blade_dir=(0.02, -0.26, 0.97), blade_edge=(1.0, 0.0, 0.0),
        l_hand_pos=(0.44, 1.38, -0.20), l_hand_dir=(0.74, 0.44, -0.51),
        l_foot_pos=(0.16, 0.080, 0.35),
        r_foot_pos=(-0.22, 0.080, -0.30),
    )
    stab_drift = replace(land_stab, hips_y=0.800, r_hand_pos=(-0.06, 1.06, 0.50))
    return _compile_clip(
        name="js21_tiaobu_chajian", slot="jianshu_thrust", duration=0.98,
        tags=["jianshu", "wushu", "section3", "jump", "thrust", "chajian"],
        description="Forward vaulting leap driving into a deep Bow-Stance stab (Khiêu Bộ Sáp Kiếm).",
        hit_window=(0.38, 0.66),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.26, vault_air, "whip"),
            Keyframe(0.50, land_stab, "heavy_drop"),
            Keyframe(0.72, stab_drift, "flow"),
            Keyframe(0.98, g0, "flow"),
        ],
    )


def make_js22_zhuanshen_pijian_360() -> AnimationClip:
    """22. Chuyển Thân Phách Kiếm 360° (转身劈剑): Lightning 360-deg pivot spin crashing into a Bow-Stance cleave."""
    def _zp(yaw: float, hy: float, r_pos, b_dir, l_pos, l_dir,
            pitch: float = 8.0, hroll: float = 0.0, tyaw: float = 14.0, troll: float = 0.0,
            rf_y: float = 0.080) -> PoseSpec:
        return PoseSpec(
            hips_y=hy, hips_yaw=yaw, hips_pitch=pitch, hips_roll=hroll,
            torso_yaw=tyaw, torso_pitch=pitch, torso_roll=troll,
            head_yaw=0.0, head_pitch=1.0, head_roll=-troll * 0.4, head_world_space=False,
            l_shoulder_lift=8.0, l_shoulder_fwd=4.0, r_shoulder_lift=12.0, r_shoulder_fwd=14.0,
            r_hand_pos=r_pos, blade_dir=b_dir,
            l_hand_pos=l_pos, l_hand_dir=l_dir, l_hand_up=(0.0, 1.0, 0.0),
            l_foot_pos=(0.16, 0.080, 0.24), l_foot_yaw=10.0,
            r_foot_pos=(-0.20, rf_y, -0.22), r_foot_yaw=-24.0,
            local_body_space=True, feet_in_body_space=True,
        )

    p0 = _spin_guard(-24.0)
    p_spin1 = _zp(84.0, 0.930, (-0.34, 1.54, 0.20), (-0.38, 0.80, 0.46),
                  (0.36, 1.40, 0.16), (0.48, 0.46, 0.75), pitch=4.0, hroll=-5.5, tyaw=16.0, troll=4.5, rf_y=0.180)
    p_spin2 = _zp(210.0, 0.950, (-0.20, 1.74, 0.24), (-0.08, 0.86, 0.50),
                  (0.32, 1.44, 0.18), (0.34, 0.50, 0.80), pitch=-3.0, hroll=-6.0, tyaw=18.0, troll=5.0, rf_y=0.200)
    p_chop = _zp(346.0, 0.785, (-0.06, 1.00, 0.50), (0.02, -0.16, 0.98),
                 (0.44, 1.38, -0.18), (0.74, 0.42, -0.52), pitch=14.0, hroll=-3.5, tyaw=14.0, troll=3.0, rf_y=0.080)
    p_drift = replace(p_chop, hips_y=0.805, r_hand_pos=(-0.06, 1.03, 0.49))
    p_end = _spin_guard(336.0)
    return _compile_clip(
        name="js22_zhuanshen_pijian_360", slot="jianshu_slash", duration=1.06,
        tags=["jianshu", "wushu", "section3", "spin", "pi_jian", "360"],
        description="360-deg spinning whip-cleave landing into Bow Stance (Chuyển Thân Phách Kiếm 360°).",
        hit_window=(0.48, 0.74),
        smooth_passes=3,
        keyframes=[
            Keyframe(0.00, p0, "flow"),
            Keyframe(0.22, p_spin1, "whip"),
            Keyframe(0.42, p_spin2, "flow"),
            Keyframe(0.62, p_chop, "heavy_drop"),
            Keyframe(0.82, p_drift, "flow"),
            Keyframe(1.06, p_end, "flow"),
        ],
    )


def make_js23_feibu_shangci() -> AnimationClip:
    """23. Phi Bộ Thượng Thích (飞步上刺): Soaring Khinh Công diagonal sky-piercing airborne thrust."""
    g0 = _guard()
    coil = _guard(
        hips_y=0.775, hips_yaw=-32.0, hips_pitch=14.0, hips_roll=4.0,
        torso_yaw=-12.0, torso_pitch=12.0, torso_roll=-3.0,
        r_hand_pos=(-0.28, 0.96, 0.16),
        blade_dir=(0.04, 0.36, 0.93),
        l_hand_pos=(0.20, 1.14, 0.34),
        l_foot_pos=(0.16, 0.080, 0.22), r_foot_pos=(-0.20, 0.080, -0.22),
    )
    soar_thrust = _guard(
        hips_y=1.290, hips_yaw=20.0, hips_pitch=-5.0, hips_roll=-4.5,
        torso_yaw=15.0, torso_pitch=-8.0, torso_roll=4.0, head_pitch=-10.0,
        r_shoulder_lift=20.0, r_shoulder_fwd=22.0, l_shoulder_lift=6.0, l_shoulder_fwd=-12.0,
        r_hand_pos=(-0.04, 1.82, 0.46),
        blade_dir=(0.04, 0.64, 0.77),
        l_hand_pos=(0.44, 1.52, -0.18), l_hand_dir=(0.74, 0.42, -0.52),
        l_foot_pos=(0.16, 0.580, 0.24),
        r_foot_pos=(-0.20, 0.420, -0.24),
    )
    land_soft = _guard(
        hips_y=0.830, hips_yaw=4.0, hips_pitch=9.0, torso_yaw=8.0, torso_pitch=7.0,
        r_hand_pos=(-0.12, 1.24, 0.42), blade_dir=(0.12, 0.46, 0.88),
        l_hand_pos=(0.30, 1.26, 0.20),
        l_foot_pos=(0.16, 0.080, 0.22), r_foot_pos=(-0.20, 0.080, -0.20),
    )
    return _compile_clip(
        name="js23_feibu_shangci", slot="jianshu_aerial", duration=1.00,
        tags=["jianshu", "wushu", "section3", "jump", "ci_jian", "upward"],
        description="Soaring Khinh Công airborne diagonal sky-thrust (Phi Bộ Thượng Thích).",
        hit_window=(0.30, 0.62),
        l_air_flex=26.0,
        r_air_flex=30.0,
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.18, coil, "flow"),
            Keyframe(0.46, soar_thrust, "whip"),
            Keyframe(0.74, land_soft, "heavy_drop"),
            Keyframe(1.00, g0, "flow"),
        ],
    )


def make_js24_diebu_dici() -> AnimationClip:
    """24. Điệt Bộ Đê Thích (跌步低刺): Dramatic Competition drop-crouch / split-lunge low horizontal stab."""
    g0 = _guard()
    hop_prep = _guard(
        hips_y=0.995, hips_yaw=-24.0, hips_pitch=4.0, torso_yaw=-8.0, torso_pitch=2.0,
        r_hand_pos=(-0.26, 1.18, 0.20), blade_dir=(0.00, 0.08, 0.997),
        l_hand_pos=(0.22, 1.32, 0.32),
        l_foot_pos=(0.16, 0.200, 0.20),
        r_foot_pos=(-0.20, 0.160, -0.18),
    )
    drop_stab = _guard(
        hips_y=0.600, hips_yaw=20.0, hips_pitch=20.0, hips_roll=-4.0,
        torso_yaw=15.0, torso_pitch=18.0, torso_roll=3.5, head_pitch=-6.0,
        r_shoulder_lift=-4.0, r_shoulder_fwd=26.0, l_shoulder_lift=14.0, l_shoulder_fwd=-10.0,
        r_hand_pos=(-0.06, 0.68, 0.50),
        blade_dir=(0.00, -0.02, 1.00), blade_edge=(1.0, 0.0, 0.0),
        l_hand_pos=(0.42, 1.16, -0.16), l_hand_dir=(0.72, 0.46, -0.52),
        l_foot_pos=(0.16, 0.080, 0.40), l_foot_yaw=8.0,
        r_foot_pos=(-0.22, 0.098, -0.34), r_foot_yaw=-30.0, r_foot_pitch=-20.0,
    )
    drop_poise = replace(drop_stab, hips_y=0.620, r_hand_pos=(-0.06, 0.70, 0.48))
    return _compile_clip(
        name="js24_diebu_dici", slot="jianshu_thrust", duration=1.04,
        tags=["jianshu", "wushu", "section3", "drop", "ci_jian", "low"],
        description="Dramatic Competition low drop-lunge horizontal stab (Điệt Bộ Đê Thích).",
        hit_window=(0.34, 0.66),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.20, hop_prep, "flow"),
            Keyframe(0.44, drop_stab, "heavy_drop"),
            Keyframe(0.70, drop_poise, "flow"),
            Keyframe(1.04, g0, "flow"),
        ],
    )


# ==============================================================================
# SECTION 4 (25-32): RAPID FLURRIES, BACK-SWORD, LƯỢNG TƯỚNG & CLOSING SALUTE
# ==============================================================================

def make_js25_lianhuan_sanci() -> AnimationClip:
    """25. Liên Hoàn Tam Thích (连环三刺): Blistering 3-beat continuous thrust combo (low -> high -> deep center)."""
    g0 = _guard()
    c1 = _guard(
        hips_y=0.845, hips_yaw=-32.0, hips_pitch=9.0, hips_roll=3.5, torso_yaw=-12.0, torso_pitch=7.0,
        r_hand_pos=(-0.26, 1.02, 0.16),
        blade_dir=(0.00, -0.02, 1.00), l_hand_pos=(0.22, 1.22, 0.34),
    )
    t1_low = _guard(
        hips_y=0.825, hips_yaw=12.0, hips_pitch=12.0, hips_roll=-3.0, torso_yaw=12.0, torso_pitch=10.0,
        r_shoulder_fwd=22.0,
        r_hand_pos=(-0.08, 1.00, 0.50),
        blade_dir=(0.04, -0.04, 0.998), l_hand_pos=(0.38, 1.28, -0.06), l_hand_dir=(0.68, 0.36, -0.64),
        l_foot_pos=(0.16, 0.080, 0.26), r_foot_pos=(-0.21, 0.080, -0.22),
    )
    c2 = _guard(
        hips_y=0.880, hips_yaw=-22.0, hips_pitch=6.0, torso_yaw=-6.0, torso_pitch=5.0,
        r_hand_pos=(-0.22, 1.22, 0.20), blade_dir=(0.06, 0.20, 0.98),
        l_hand_pos=(0.28, 1.28, 0.20),
        l_foot_pos=(0.16, 0.080, 0.26), r_foot_pos=(-0.21, 0.080, -0.22),
    )
    t2_high = _guard(
        hips_y=0.865, hips_yaw=16.0, hips_pitch=10.0, hips_roll=-3.5, torso_yaw=14.0, torso_pitch=8.0,
        r_shoulder_fwd=24.0,
        r_hand_pos=(-0.02, 1.34, 0.50),
        blade_dir=(0.06, 0.22, 0.97), l_hand_pos=(0.42, 1.36, -0.14), l_hand_dir=(0.72, 0.40, -0.56),
        l_foot_pos=(0.16, 0.080, 0.30), r_foot_pos=(-0.22, 0.080, -0.26),
    )
    c3 = _guard(
        hips_y=0.825, hips_yaw=-32.0, hips_pitch=9.0, torso_yaw=-10.0, torso_pitch=7.0,
        r_hand_pos=(-0.26, 1.06, 0.14), blade_dir=(0.00, 0.04, 0.999),
        l_hand_pos=(0.28, 1.26, 0.20),
        l_foot_pos=(0.16, 0.080, 0.30), r_foot_pos=(-0.22, 0.080, -0.26),
    )
    t3_deep = _guard(
        hips_y=0.785, hips_yaw=22.0, hips_pitch=15.0, hips_roll=-4.5, torso_yaw=16.0, torso_pitch=14.0,
        r_shoulder_fwd=26.0, l_shoulder_lift=8.0, l_shoulder_fwd=-14.0,
        r_hand_pos=(-0.05, 1.12, 0.54),
        blade_dir=(0.00, 0.04, 0.999), blade_edge=(1.0, 0.0, 0.0),
        l_hand_pos=(0.44, 1.38, -0.20), l_hand_dir=(0.74, 0.44, -0.51),
        l_foot_pos=(0.16, 0.080, 0.35), r_foot_pos=(-0.22, 0.100, -0.28),
    )
    t3_poise = replace(t3_deep, hips_y=0.805, r_hand_pos=(-0.06, 1.13, 0.52))
    return _compile_clip(
        name="js25_lianhuan_sanci", slot="jianshu_thrust", duration=1.24,
        tags=["jianshu", "wushu", "section4", "ci_jian", "flurry", "multi_hit"],
        description="Rapid 3-beat continuous thrust combo: low -> high -> deep lunge (Liên Hoàn Tam Thích).",
        hit_window=(0.22, 0.96),
        smooth_passes=3,
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.15, c1, "flow"), Keyframe(0.30, t1_low, "whip"),
            Keyframe(0.46, c2, "flow"), Keyframe(0.62, t2_high, "whip"),
            Keyframe(0.78, c3, "flow"), Keyframe(0.95, t3_deep, "whip"),
            Keyframe(1.08, t3_poise, "flow"),
            Keyframe(1.24, g0, "flow"),
        ],
    )


def make_js26_shuang_guajian() -> AnimationClip:
    """26. Tả Hữu Quải Kiếm (左右挂剑): Rapid alternating left-and-right body-hugging vertical parry circles."""
    g0 = _guard()
    gua_l = _guard(
        hips_y=0.855, hips_yaw=26.0, hips_pitch=8.0, hips_roll=-4.5,
        torso_yaw=15.0, torso_pitch=6.0, torso_roll=4.0,
        r_shoulder_lift=0.0, r_shoulder_fwd=22.0,
        r_hand_pos=(0.20, 1.08, 0.36),
        blade_dir=(0.24, -0.46, 0.85),
        l_hand_pos=(0.42, 1.32, -0.12), l_hand_dir=(0.72, 0.40, -0.56),
        l_foot_pos=(0.18, 0.080, 0.24), r_foot_pos=(-0.21, 0.080, -0.20),
    )
    mid_lift = _guard(
        hips_y=0.905, hips_yaw=-4.0, hips_pitch=2.0, torso_yaw=0.0, torso_pitch=0.0,
        r_hand_pos=(-0.08, 1.44, 0.38),
        blade_dir=(-0.08, 0.62, 0.78),
        l_hand_pos=(0.26, 1.34, 0.24),
    )
    gua_r = _guard(
        hips_y=0.855, hips_yaw=-36.0, hips_pitch=8.0, hips_roll=5.0,
        torso_yaw=-15.0, torso_pitch=6.0, torso_roll=-4.0,
        r_shoulder_lift=-2.0, r_shoulder_fwd=-6.0,
        r_hand_pos=(-0.36, 1.06, 0.18),
        blade_dir=(-0.24, -0.48, 0.84),
        l_hand_pos=(0.20, 1.28, 0.36), l_hand_dir=(-0.28, 0.34, 0.90),
    )
    return _compile_clip(
        name="js26_shuang_guajian", slot="jianshu_parry", duration=1.00,
        tags=["jianshu", "wushu", "section4", "gua_jian", "parry", "double"],
        description="Alternating left & right vertical Hooking Parries (Tả Hữu Quải Kiếm).",
        hit_window=(0.20, 0.76),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.24, gua_l, "whip"),
            Keyframe(0.48, mid_lift, "flow"),
            Keyframe(0.72, gua_r, "whip"),
            Keyframe(1.00, g0, "flow"),
        ],
    )


def make_js27_huifeng_yunjian_360() -> AnimationClip:
    """27. Hồi Phong Vân Kiếm 360° (回风云剑): Spinning 360-deg overhead Cloud-Sword halo ending in razor stop."""
    def _hy(yaw: float, hy: float, r_pos, b_dir, l_pos, l_dir,
            pitch: float = 2.0, hroll: float = 0.0, tyaw: float = 12.0, troll: float = 0.0,
            rf_y: float = 0.080) -> PoseSpec:
        return PoseSpec(
            hips_y=hy, hips_yaw=yaw, hips_pitch=pitch, hips_roll=hroll,
            torso_yaw=tyaw, torso_pitch=pitch, torso_roll=troll,
            head_yaw=0.0, head_pitch=-2.0, head_roll=-troll * 0.4, head_world_space=False,
            l_shoulder_lift=10.0, l_shoulder_fwd=6.0, r_shoulder_lift=20.0, r_shoulder_fwd=10.0,
            r_hand_pos=r_pos, blade_dir=b_dir,
            l_hand_pos=l_pos, l_hand_dir=l_dir, l_hand_up=(0.0, 1.0, 0.0),
            l_foot_pos=(0.16, 0.080, 0.18), l_foot_yaw=12.0,
            r_foot_pos=(-0.20, rf_y, -0.18), r_foot_yaw=-20.0,
            local_body_space=True, feet_in_body_space=True,
        )

    p0 = _spin_guard(-24.0)
    p_s1 = _hy(80.0, 0.935, (-0.16, 1.74, 0.28), (0.62, 0.16, 0.77),
               (0.40, 1.38, 0.10), (0.62, 0.40, 0.67), pitch=-3.0, hroll=-5.5, tyaw=16.0, troll=4.5, rf_y=0.175)
    p_s2 = _hy(204.0, 0.945, (-0.28, 1.76, 0.22), (-0.70, 0.20, 0.68),
               (0.44, 1.42, -0.04), (0.76, 0.42, -0.49), pitch=-4.0, hroll=-6.0, tyaw=18.0, troll=5.0, rf_y=0.195)
    p_stop = _hy(344.0, 0.870, (-0.28, 1.22, 0.40), (-0.38, 0.20, 0.90),
                 (0.24, 1.30, 0.32), (-0.26, 0.38, 0.89), pitch=5.0, hroll=-2.0, tyaw=12.0, troll=1.5, rf_y=0.080)
    p_end = _spin_guard(336.0)
    return _compile_clip(
        name="js27_huifeng_yunjian_360", slot="jianshu_slash", duration=1.08,
        tags=["jianshu", "wushu", "section4", "yun_jian", "spin", "360"],
        description="360-deg spinning overhead Cloud-Sword halo into crisp stop (Hồi Phong Vân Kiếm 360°).",
        hit_window=(0.22, 0.78),
        smooth_passes=3,
        keyframes=[
            Keyframe(0.00, p0, "flow"),
            Keyframe(0.26, p_s1, "whip"),
            Keyframe(0.52, p_s2, "flow"),
            Keyframe(0.80, p_stop, "whip"),
            Keyframe(1.08, p_end, "flow"),
        ],
    )


def make_js28_beijian_fanci() -> AnimationClip:
    """28. Bối Kiếm Phản Thích (背剑反刺): Iconic Back-Sword tuck whipping suddenly into a lethal reverse thrust."""
    g0 = _guard()
    bei_tuck = _guard(
        hips_y=0.880, hips_yaw=-40.0, hips_pitch=6.0, hips_roll=4.0,
        torso_yaw=-18.0, torso_pitch=4.0, torso_roll=-3.0, head_yaw=18.0,
        r_shoulder_lift=14.0, r_shoulder_fwd=-10.0, l_shoulder_lift=6.0, l_shoulder_fwd=16.0,
        # Right hand draws wide behind right shoulder (ample head clearance!), left Kiếm Quyết points decoy!
        r_hand_pos=(-0.38, 1.44, -0.06),
        blade_dir=(-0.22, 0.84, -0.50),
        l_hand_pos=(0.16, 1.28, 0.44), l_hand_dir=(-0.12, 0.22, 0.97),
        l_foot_pos=(0.16, 0.080, 0.20), r_foot_pos=(-0.22, 0.080, -0.20),
    )
    whip_out = _guard(
        hips_y=0.855, hips_yaw=-12.0, hips_pitch=9.0, torso_yaw=0.0, torso_pitch=7.0,
        r_shoulder_lift=4.0, r_shoulder_fwd=12.0,
        r_hand_pos=(-0.38, 1.22, 0.24),
        blade_dir=(-0.28, 0.36, 0.89),
        l_hand_pos=(0.28, 1.28, 0.24),
    )
    fan_thrust = _guard(
        hips_y=0.805, hips_yaw=22.0, hips_pitch=14.0, hips_roll=-4.5,
        torso_yaw=18.0, torso_pitch=13.0, torso_roll=4.0, head_pitch=-3.0,
        r_shoulder_lift=2.0, r_shoulder_fwd=26.0, l_shoulder_lift=10.0, l_shoulder_fwd=-14.0,
        r_hand_pos=(-0.05, 1.14, 0.54),
        blade_dir=(0.02, 0.06, 0.998), blade_edge=(1.0, 0.0, 0.0),
        l_hand_pos=(0.44, 1.38, -0.20), l_hand_dir=(0.74, 0.44, -0.51),
        l_foot_pos=(0.16, 0.080, 0.34), r_foot_pos=(-0.22, 0.095, -0.28),
    )
    thrust_poise = replace(fan_thrust, hips_y=0.822, r_hand_pos=(-0.06, 1.15, 0.52))
    return _compile_clip(
        name="js28_beijian_fanci", slot="jianshu_thrust", duration=1.04,
        tags=["jianshu", "wushu", "section4", "beijian", "ci_jian", "feint"],
        description="Back-Sword decoy tuck whipping into a piercing surprise thrust (Bối Kiếm Phản Thích).",
        hit_window=(0.46, 0.74),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.26, bei_tuck, "flow"),
            Keyframe(0.44, whip_out, "whip"),
            Keyframe(0.62, fan_thrust, "snap"),
            Keyframe(0.80, thrust_poise, "flow"),
            Keyframe(1.04, g0, "flow"),
        ],
    )


def make_js29_jinbu_zhanjian() -> AnimationClip:
    """29. Tiến Bộ Trảm Kiếm (进步斩剑): Advancing Bow-Stance crisp horizontal neck-level whipping cleave (Zhǎn Jiàn)."""
    g0 = _guard()
    zhan_coil = _guard(
        hips_y=0.875, hips_yaw=28.0, hips_pitch=6.0, hips_roll=-4.0,
        torso_yaw=18.0, torso_pitch=4.0, torso_roll=3.5, head_yaw=-18.0,
        r_shoulder_lift=8.0, r_shoulder_fwd=22.0, l_shoulder_lift=6.0, l_shoulder_fwd=-10.0,
        # Coils flat sword across chest to high-left before explosive horizontal whip cleave!
        r_hand_pos=(0.20, 1.34, 0.36),
        blade_dir=(0.74, 0.14, 0.66), blade_edge=(-0.66, 0.0, 0.74),
        l_hand_pos=(0.42, 1.32, -0.12), l_hand_dir=(0.72, 0.38, -0.58),
        l_foot_pos=(0.16, 0.115, 0.20), r_foot_pos=(-0.20, 0.080, -0.20),
    )
    zhan_mid = _guard(
        hips_y=0.820, hips_yaw=2.0, hips_pitch=11.0, torso_yaw=4.0, torso_pitch=9.0,
        r_shoulder_lift=8.0, r_shoulder_fwd=24.0,
        r_hand_pos=(-0.06, 1.32, 0.50),
        blade_dir=(0.06, 0.06, 0.996), blade_edge=(-0.996, 0.0, 0.06),
        l_hand_pos=(0.30, 1.34, 0.18), l_hand_dir=(0.28, 0.42, 0.86),
        l_foot_pos=(0.16, 0.080, 0.32), r_foot_pos=(-0.22, 0.080, -0.28),
    )
    zhan_cut = _guard(
        hips_y=0.790, hips_yaw=-26.0, hips_pitch=12.0, hips_roll=4.5,
        torso_yaw=-12.0, torso_pitch=10.0, torso_roll=-3.5, head_yaw=16.0,
        r_shoulder_lift=6.0, r_shoulder_fwd=8.0, l_shoulder_lift=12.0, l_shoulder_fwd=16.0,
        # Razor-flat neck-height horizontal cleave arrests crisply on right-forward line!
        r_hand_pos=(-0.38, 1.28, 0.32),
        blade_dir=(-0.66, 0.08, 0.75), blade_edge=(-0.75, 0.0, -0.66),
        l_hand_pos=(0.24, 1.38, 0.38), l_hand_dir=(-0.18, 0.38, 0.91),
        l_foot_pos=(0.16, 0.080, 0.34), r_foot_pos=(-0.22, 0.080, -0.28),
    )
    zhan_poise = replace(zhan_cut, hips_y=0.808, r_hand_pos=(-0.39, 1.30, 0.31))
    return _compile_clip(
        name="js29_jinbu_zhanjian", slot="jianshu_slash", duration=1.02,
        tags=["jianshu", "wushu", "section4", "zhan_jian", "gongbu", "horizontal"],
        description="Advancing Bow-Stance crisp neck-level horizontal Whipping Slash (Tiến Bộ Trảm Kiếm).",
        hit_window=(0.30, 0.62),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.24, zhan_coil, "flow"),
            Keyframe(0.40, zhan_mid, "whip"),
            Keyframe(0.56, zhan_cut, "whip"),
            Keyframe(0.76, zhan_poise, "flow"),
            Keyframe(1.02, g0, "flow"),
        ],
    )


def make_js30_xingbu_chuangua_xiaojian() -> AnimationClip:
    """30. Hành Bộ Xuyên Quải Tiêu Kiếm (行步穿挂削剑): Brisk-step figure-8 hook-thread flurry into diagonal rising slice."""
    g0 = _guard()
    step_gua = _guard(
        hips_y=0.860, hips_yaw=24.0, hips_pitch=8.0, hips_roll=-4.0,
        torso_yaw=14.0, torso_pitch=6.0, torso_roll=3.5,
        r_hand_pos=(0.18, 1.06, 0.36),
        blade_dir=(0.28, -0.44, 0.85),
        l_hand_pos=(0.40, 1.30, -0.08), l_hand_dir=(0.68, 0.38, -0.62),
        l_foot_pos=(0.18, 0.080, 0.22), r_foot_pos=(-0.12, 0.125, -0.06),
    )
    step_chuan = _guard(
        hips_y=0.845, hips_yaw=-26.0, hips_pitch=9.0, hips_roll=4.0,
        torso_yaw=-10.0, torso_pitch=7.0, torso_roll=-3.5,
        # Threads under left forearm on right flank preparing diagonal rising Xiāo Jiàn!
        r_hand_pos=(-0.32, 1.02, 0.24),
        blade_dir=(-0.26, -0.18, 0.95),
        l_hand_pos=(0.14, 1.24, 0.36), l_hand_dir=(-0.30, 0.30, 0.91),
        l_foot_pos=(0.14, 0.120, 0.26), r_foot_pos=(-0.20, 0.080, -0.18),
    )
    xiao_slash = _guard(
        hips_y=0.815, hips_yaw=20.0, hips_pitch=10.0, hips_roll=-4.5,
        torso_yaw=16.0, torso_pitch=8.0, torso_roll=4.0, head_pitch=-3.0,
        r_shoulder_lift=14.0, r_shoulder_fwd=24.0, l_shoulder_lift=10.0, l_shoulder_fwd=-12.0,
        # 45-degree upward-forward slicing cut (Xiāo Jiàn — 削剑)!
        r_hand_pos=(0.04, 1.46, 0.48),
        blade_dir=(0.24, 0.62, 0.75),
        l_hand_pos=(0.44, 1.38, -0.18), l_hand_dir=(0.74, 0.42, -0.52),
        l_foot_pos=(0.16, 0.080, 0.34), r_foot_pos=(-0.22, 0.080, -0.28),
    )
    xiao_poise = replace(xiao_slash, hips_y=0.832, r_hand_pos=(0.05, 1.48, 0.47))
    return _compile_clip(
        name="js30_xingbu_chuangua_xiaojian", slot="jianshu_flourish", duration=1.18,
        tags=["jianshu", "wushu", "section4", "xiao_jian", "chuan_jian", "gua_jian", "flurry"],
        description="Brisk-step figure-8 Hook & Thread flurry into 45-deg rising Slicing Cut (Hành Bộ Xuyên Quải Tiêu Kiếm).",
        hit_window=(0.48, 0.84),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.24, step_gua, "whip"),
            Keyframe(0.46, step_chuan, "flow"),
            Keyframe(0.68, xiao_slash, "whip"),
            Keyframe(0.90, xiao_poise, "flow"),
            Keyframe(1.18, g0, "flow"),
        ],
    )


def make_js31_dulibu_jiajian_liangxiang() -> AnimationClip:
    """31. Độc Lập Giá Kiếm Lượng Tướng (独立架剑亮相): Crane knee-lift balance + overhead Jià Jiàn block climax!"""
    g0 = _guard()
    sweep_gather = _guard(
        hips_y=0.835, hips_yaw=-28.0, hips_pitch=8.0, torso_yaw=-8.0, torso_pitch=6.0,
        r_hand_pos=(-0.26, 1.04, 0.28), blade_dir=(0.12, 0.18, 0.98),
        l_hand_pos=(0.18, 1.16, 0.34),
        l_foot_pos=(0.14, 0.095, 0.12), r_foot_pos=(-0.14, 0.080, -0.06),
    )
    dulibu_liangxiang = _guard(
        hips_y=0.960, hips_yaw=-14.0, hips_pitch=-2.0, hips_roll=4.5,
        torso_yaw=10.0, torso_pitch=-4.0, torso_roll=-4.0, head_yaw=12.0, head_pitch=-2.0,
        r_shoulder_lift=22.0, r_shoulder_fwd=10.0, l_shoulder_lift=12.0, l_shoulder_fwd=18.0,
        # Right hand presses sword high overhead in horizontal Jià Jiàn (架剑) supporting crown!
        r_hand_pos=(-0.18, 1.76, 0.22),
        blade_dir=(0.76, 0.20, 0.62), blade_edge=(-0.20, 0.96, -0.18),
        # Left Kiếm Quyết pierces proudly forward-left!
        l_hand_pos=(0.30, 1.34, 0.42), l_hand_dir=(0.24, 0.28, 0.93),
        # Right leg rooted straight; LEFT knee lifted high above waist (Golden-Rooster Độc Lập)!
        l_foot_pos=(0.12, 0.620, 0.24),
        r_foot_pos=(-0.12, 0.080, 0.02), r_foot_yaw=-10.0,
    )
    liangxiang_hold = replace(
        dulibu_liangxiang, hips_y=0.970,
        r_hand_pos=(-0.18, 1.78, 0.21), l_hand_pos=(0.31, 1.36, 0.43),
        l_foot_pos=(0.12, 0.640, 0.24),
    )
    return _compile_clip(
        name="js31_dulibu_jiajian_liangxiang", slot="jianshu_balance", duration=1.28,
        tags=["jianshu", "wushu", "section4", "balance", "dulibu", "jia_jian", "liangxiang"],
        description="Golden-Rooster single-leg Overhead Supporting Block & Grand Lượng Tướng (Độc Lập Giá Kiếm Lượng Tướng).",
        hit_window=(0.34, 0.84),
        l_air_flex=30.0,
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.22, sweep_gather, "flow"),
            Keyframe(0.48, dulibu_liangxiang, "snap"),
            Keyframe(0.92, liangxiang_hold, "flow"),
            Keyframe(1.28, g0, "flow"),
        ],
    )


def make_js32_shou_shi_baojian() -> AnimationClip:
    """32. Thu Thức Bão Kiếm Hoàn Nguyên (收势抱剑还原): Competition closing Cloud-Gather, Sword-Cradle Salute & calm return."""
    g0 = _guard()
    gather_arc = _guard(
        hips_y=0.920, hips_yaw=-12.0, hips_pitch=2.0, torso_yaw=8.0, torso_pitch=1.0,
        r_shoulder_lift=10.0, r_shoulder_fwd=14.0, l_shoulder_lift=8.0, l_shoulder_fwd=12.0,
        # Wide enveloping outward-downward Qi-gathering sword arc
        r_hand_pos=(-0.34, 1.36, 0.30),
        blade_dir=(-0.34, 0.68, 0.65),
        l_hand_pos=(0.34, 1.38, 0.30), l_hand_dir=(0.28, 0.44, 0.85),
        l_foot_pos=(0.14, 0.080, 0.10), r_foot_pos=(-0.14, 0.080, -0.08),
    )
    closing_salute = _guard(
        hips_y=0.932, hips_yaw=0.0, hips_pitch=1.0, hips_roll=0.0,
        torso_yaw=0.0, torso_pitch=1.0, torso_roll=0.0, head_yaw=0.0, head_pitch=0.5,
        l_shoulder_lift=-2.0, l_shoulder_fwd=12.0, r_shoulder_lift=-2.0, r_shoulder_fwd=12.0,
        # Serene Tịnh Bộ Bão Kiếm closing salute matching Form 01!
        r_hand_pos=(-0.10, 1.20, 0.34),
        blade_dir=(0.04, 0.84, 0.54),
        l_hand_pos=(0.08, 1.24, 0.36), l_hand_dir=(-0.22, 0.54, 0.81),
        l_foot_pos=(0.10, 0.080, 0.02), l_foot_yaw=4.0,
        r_foot_pos=(-0.10, 0.080, 0.02), r_foot_yaw=-4.0,
    )
    exhale_settle = replace(
        closing_salute, hips_y=0.922,
        r_hand_pos=(-0.11, 1.17, 0.33), l_hand_pos=(0.09, 1.21, 0.35),
    )
    return _compile_clip(
        name="js32_shou_shi_baojian", slot="jianshu_close", duration=1.40,
        tags=["jianshu", "wushu", "section4", "closing", "salute", "shoushi"],
        description="Competition Jianshu closing Qi-gather & Sword-Cradle Salute (Thu Thức Bão Kiếm Hoàn Nguyên).",
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.32, gather_arc, "flow"),
            Keyframe(0.66, closing_salute, "flow"),
            Keyframe(1.04, exhale_settle, "flow"),
            Keyframe(1.40, g0, "flow"),
        ],
    )


# ==============================================================================
# COMPLETE 32-FORM REGISTRY & BUILDER
# ==============================================================================

JIANSHU_32_BUILDERS = [
    # Section 1 (01-08)
    make_js01_qi_shi,
    make_js02_bingbu_zhici,
    make_js03_gongbu_pijian,
    make_js04_xubu_dianjian,
    make_js05_dingbu_bengjian,
    make_js06_guajian_gongbu_ci,
    make_js07_zheng_liaojian,
    make_js08_fan_liaojian,
    # Section 2 (09-16)
    make_js09_yunjian_mojian,
    make_js10_jiaojian_chuanjian,
    make_js11_wan_hua_shuang_xuan,
    make_js12_tixi_dianjian,
    make_js13_pubu_saojian,
    make_js14_xiebu_jiejian,
    make_js15_yanshi_pingheng,
    make_js16_wangyue_pingheng,
    # Section 3 (17-24)
    make_js17_tengkong_feijiao,
    make_js18_xuanfengjiao_360,
    make_js19_tengkong_bailian_360,
    make_js20_xuanzi_butterfly,
    make_js21_tiaobu_chajian,
    make_js22_zhuanshen_pijian_360,
    make_js23_feibu_shangci,
    make_js24_diebu_dici,
    # Section 4 (25-32)
    make_js25_lianhuan_sanci,
    make_js26_shuang_guajian,
    make_js27_huifeng_yunjian_360,
    make_js28_beijian_fanci,
    make_js29_jinbu_zhanjian,
    make_js30_xingbu_chuangua_xiaojian,
    make_js31_dulibu_jiajian_liangxiang,
    make_js32_shou_shi_baojian,
]


def build_all_jianshu_clips(fps: int = 60) -> List[AnimationClip]:
    """Build and sample all 32 Competition Wushu Jianshu clips at target FPS."""
    return [sample_clip(fn(), fps=fps) for fn in JIANSHU_32_BUILDERS]
