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

V3 — Contact-aware kinematic animation compiler (see implementation below):
  - Nonuniform time-aware cubic interpolation, spherical blade paths, one constraint pass per sample.
  - Continuous pole transport, wrist swing/twist limits and approximate capsule clearance.
  - Ground contact / swing-foot trajectories, continuous ankle orientation and jump timing.
  - In-place animation: no horizontal pelvis translation is available in this PoseSpec.
  - Direct FK-verified bake with temporal bend-plane filtering and actual sword socket offset.
  - Requires the original rig_core; geometric limits are approximate, not mesh collision guarantees.
"""

from __future__ import annotations

import math
import bisect
from dataclasses import replace, fields
from typing import List, Optional, Sequence, Tuple
import numpy as np

from .rig_core import (
    GAME_MIXAMO_20,
    SWORD_BLADE_BASE_Y,
    SWORD_BLADE_TIP_Y,
    SWORD_SOCKET_R,
    SWORD_SOCKET_POS,
    solve_pose,
    fk_from_channels,
    sword_kinematics_from_pose,
    axis_angle_mat,
    build_foot_frame,
    decompose_swing_twist_y,
    NUM_BONES,
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


def _foot_euler_two_branch(R_ft, ref_yaw, ref_pitch, ref_roll):
    """Invert Ry(yaw) Rx(-pitch) Rz(roll), including the coupled gimbal solution."""
    M = R_ft @ _R_FOOT_REST.T
    p = math.degrees(math.asin(float(np.clip(M[1, 2], -1, 1))))
    if abs(math.cos(math.radians(p))) > 1e-7:
        y = math.degrees(math.atan2(M[0, 2], M[2, 2]))
        r = math.degrees(math.atan2(M[1, 0], M[1, 1]))
        p2 = 180-p if p >= 0 else -180-p
        candidates = [(y,p,r),(y+180,p2,r+180)]
    else:
        # At pitch +90, only yaw+roll is observable; at -90, yaw-roll.
        sign = 1.0 if p >= 0 else -1.0
        coupled = math.degrees(math.atan2(-M[2,0], M[0,0]))
        delta = _unwrap_deg(coupled, ref_yaw + sign*ref_roll) - (ref_yaw + sign*ref_roll)
        candidates = [(ref_yaw + delta/2, p, ref_roll + sign*delta/2)]
    ref = (ref_yaw, ref_pitch, ref_roll)
    candidates = [tuple(_unwrap_deg(x, r) for x,r in zip(c,ref)) for c in candidates]
    return min(candidates, key=lambda c: sum((x-r)**2 for x,r in zip(c,ref)))


def _unit(v, fallback=(0.,0.,1.)):
    v = np.asarray(v, dtype=float)
    n = float(np.linalg.norm(v))
    return v/n if n > 1e-9 else np.asarray(fallback,dtype=float)/max(1e-9,np.linalg.norm(fallback))


def _project(v, axis):
    return v - np.dot(v, axis)*axis


def _perp(axis):
    return _unit(_project(np.eye(3)[int(np.argmin(np.abs(axis)))],axis))


def _slerp_dir(a,b,u, hint=None):
    a,b = _unit(a),_unit(b)
    c = float(np.clip(np.dot(a,b),-1,1))
    v = b-c*a
    n = np.linalg.norm(v)
    if n < 1e-8:
        if c > 0: return a
        v = _project(np.asarray(hint),a) if hint is not None else _perp(a)
        v = _unit(v,_perp(a))
    else: v /= n
    theta = math.acos(c)*u
    return _unit(a*math.cos(theta)+v*math.sin(theta),a)


def _smooth(u):
    u = float(np.clip(u,0,1))
    return u*u*(3-2*u)


def _soft_pole(axis, authored, anatomical, previous=None):
    """Use authored bend plane; transport previous plane through singularities."""
    aa = _project(anatomical,axis)
    au = _project(authored,axis)
    base = _unit(aa,_perp(axis))
    if np.linalg.norm(au)>1e-8:
        weight = 0.60*_smooth(np.linalg.norm(au)/0.16)
        base = _slerp_dir(base,_unit(au),weight,previous)
    if previous is not None:
        pp = _project(previous,axis)
        if np.linalg.norm(pp)>1e-6:
            pp = _unit(pp)
            angle = math.acos(float(np.clip(np.dot(pp,base),-1,1)))
            # Restrict change of the pole plane, not the limb itself.
            base = _slerp_dir(pp,base,min(1., math.radians(16)/max(angle,1e-9)))
    return _unit(_project(base,axis),_perp(axis))


def _segment_distance(a,b,c,d):
    """Exact finite-segment distance including parallel and point segments."""
    u,v,w=b-a,d-c,a-c
    A,B,C,D,E=map(float,(u@u,u@v,v@v,u@w,v@w))
    def pt_seg(p,x,y):
        q=y-x; return np.linalg.norm(p-x-q*np.clip((p-x)@q/max(1e-15,q@q),0.,1.))
    vals=[pt_seg(a,c,d),pt_seg(b,c,d),pt_seg(c,a,b),pt_seg(d,a,b)]
    den=A*C-B*B
    if den>1e-15:
        ss=(B*E-C*D)/den; tt=(A*E-B*D)/den
        if 0<=ss<=1 and 0<=tt<=1: vals.append(np.linalg.norm(w+ss*u-tt*v))
    return min(vals)


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
    # Preserve ground anchors by lowering pelvis BEFORE the leg IK solve.
    rootR = euler_yxz(p.hips_yaw,p.hips_pitch,p.hips_roll)
    bodyR = rot_y(p.hips_yaw)
    max_y = p.hips_y
    for idx, ft in ((14,p.l_foot_pos),(17,p.r_foot_pos)):
        f = np.array(ft,dtype=float)
        if p.feet_in_body_space: f = bodyR @ f
        if f[1] < 0.19:
            off = rootR @ np.asarray(GAME_MIXAMO_20[idx][3],dtype=float)
            horizontal = np.linalg.norm((f-off)[[0,2]])
            limit = f[1]-off[1]+math.sqrt(max(0.,0.808**2-horizontal**2))
            weight = 1.-_smooth((f[1]-.095)/.095)
            max_y = min(max_y, p.hips_y + weight*min(0.,limit-p.hips_y))
    dy = max_y-p.hips_y
    if dy < 0:
        p = replace(p, hips_y=max_y,
                    l_hand_pos=tuple(np.asarray(p.l_hand_pos)+[0,dy,0]),
                    r_hand_pos=tuple(np.asarray(p.r_hand_pos)+[0,dy,0]),
                    l_elbow_pole=tuple(np.asarray(p.l_elbow_pole)+[0,dy,0]) if p.l_elbow_pole is not None else None,
                    r_elbow_pole=tuple(np.asarray(p.r_elbow_pole)+[0,dy,0]) if p.r_elbow_pole is not None else None)
    wP, wR = _torso_frames(p)
    R_by = rot_y(p.hips_yaw)
    R_by_inv = R_by.T

    def to_w_hand(pt: Sequence[float]) -> np.ndarray:
        v = np.array(pt, dtype=np.float64)
        return (R_by @ np.array([v[0], 0.0, v[2]]) + np.array([0.0, v[1], 0.0])) if p.local_body_space else v

    def from_w_hand(pt_w: np.ndarray) -> Tuple[float, float, float]:
        v = (R_by_inv @ np.array([pt_w[0], 0.0, pt_w[2]]) + np.array([0.0, pt_w[1], 0.0])) if p.local_body_space else pt_w
        return tuple(map(float,v))

    def to_w_dir(d: Sequence[float]) -> np.ndarray:
        v = np.array(d, dtype=np.float64)
        return (R_by @ v) if p.local_body_space else v

    def from_w_dir(d_w: np.ndarray) -> Tuple[float, float, float]:
        v = (R_by_inv @ d_w) if p.local_body_space else d_w
        return tuple(map(float,v))

    def to_w_foot(pt: Sequence[float]) -> np.ndarray:
        v = np.array(pt, dtype=np.float64)
        return (R_by @ np.array([v[0], 0.0, v[2]]) + np.array([0.0, v[1], 0.0])) if p.feet_in_body_space else v

    def from_w_foot(pt_w: np.ndarray) -> Tuple[float, float, float]:
        v = (R_by_inv @ np.array([pt_w[0], 0.0, pt_w[2]]) + np.array([0.0, pt_w[1], 0.0])) if p.feet_in_body_space else pt_w
        return tuple(map(float,v))

    def clear_hand(hand, side):
        # Keep hand targets out of the approximate torso cylinder, before IK.
        a,b=wP[0],wP[4]; ab=b-a
        q=a+float(np.clip((hand-a)@ab/max(1e-9,ab@ab),0,1))*ab
        d=hand-q; dist=np.linalg.norm(d)
        if dist < .185:
            return q+_unit(d,R_by@np.array([side,0.,.4]))*.185
        return hand

    # 1. Left arm: retain folded salutes and near-extended sword fingers.
    lh_w = clear_hand(to_w_hand(p.l_hand_pos),1.)
    d_l_vec = lh_w - wP[7]
    dist_l = max(0.19, min(0.489, float(np.linalg.norm(d_l_vec))))
    dir_l = v_norm(d_l_vec, wR[6][:, 1])
    lh_w = wP[7] + dir_l * dist_l

    R_sw_l = _shortest_arc_rot(wR[6][:, 1], dir_l)
    R_la_ideal = (R_sw_l @ wR[6]) @ rot_y(-18.0)
    anatomical_l = np.cross(R_la_ideal[:, 2], dir_l)
    authored_l = to_w_hand(p.l_elbow_pole)-wP[7] if p.l_elbow_pole is not None else anatomical_l
    previous_l = None
    if prev_pose is not None:
        pp,_ = _torso_frames(prev_pose)
        previous_l = (rot_y(prev_pose.hips_yaw) @ np.asarray(prev_pose.l_elbow_pole) if prev_pose.local_body_space else np.asarray(prev_pose.l_elbow_pole))-pp[7]
        previous_l = R_by @ rot_y(prev_pose.hips_yaw).T @ previous_l
    p_perp_l = _soft_pole(dir_l,authored_l,anatomical_l,previous_l)
    l_pole_w = wP[7] + dir_l * (dist_l * 0.50) + p_perp_l * 0.38

    l_elbow_w, l_hand_w = solve_two_bone_ik(wP[7], lh_w, 0.26, 0.24, l_pole_w, min_bend_deg=8.0)
    u_la = v_norm(l_elbow_w - wP[7])
    u_lfa = v_norm(l_hand_w - l_elbow_w)
    l_hinge = v_norm(np.cross(u_lfa, u_la), np.array([0.0, 1.0, 0.0]))
    R_lfa = build_arm_frame(u_lfa, l_hinge, is_right=False)

    # Constant l_hand_up=(0,1,0) avoids discrete jump in rig_core._vec_to_pose across segments
    lh_up_const = (0.0, 1.0, 0.0)
    lh_up_w = v_norm(to_w_dir(lh_up_const))
    raw_lh_d = to_w_dir(p.l_hand_dir) if p.l_hand_dir is not None else u_lfa
    graceful_kq = v_norm(u_lfa * 0.90 + R_lfa[:, 2] * 0.30)
    lh_dir_w = _clamp_cone(v_norm(0.50 * v_norm(raw_lh_d) + 0.50 * graceful_kq), u_lfa, 30.0)
    if abs(float(np.dot(lh_dir_w, lh_up_w))) > 0.86:
        horiz = v_norm(np.array([lh_dir_w[0], 0.0, lh_dir_w[2]]), np.array([-0.3, 0.0, 0.95]))
        lh_dir_w = _clamp_cone(v_norm(lh_dir_w * 0.72 + horiz * 0.55), u_lfa, 34.0)

    # 2. Right arm + sword socket: optimize elbow plane, then limit wrist swing.
    rh_w = clear_hand(to_w_hand(p.r_hand_pos),-1.)
    d_r_vec = rh_w - wP[11]
    dist_r = max(0.18, min(0.489, float(np.linalg.norm(d_r_vec))))
    dir_r = v_norm(d_r_vec, wR[10][:, 1])
    rh_w = wP[11] + dir_r * dist_r

    raw_b_dir = v_norm(to_w_dir(p.blade_dir if p.blade_dir is not None else (0.20, 0.42, 0.88)))

    # Nominal shoulder twist (16 degrees); the continuous pole solver refines it.
    R_sw_r = _shortest_arc_rot(wR[10][:, 1], dir_r)
    R_ra_ideal = (R_sw_r @ wR[10]) @ rot_y(16.0)
    anatomical_r = np.cross(dir_r,R_ra_ideal[:,2])
    authored_r = to_w_hand(p.r_elbow_pole)-wP[11] if p.r_elbow_pole is not None else anatomical_r
    previous_r = None
    if prev_pose is not None:
        pp,_ = _torso_frames(prev_pose)
        previous_r = (rot_y(prev_pose.hips_yaw) @ np.asarray(prev_pose.r_elbow_pole) if prev_pose.local_body_space else np.asarray(prev_pose.r_elbow_pole))-pp[11]
        previous_r = R_by @ rot_y(prev_pose.hips_yaw).T @ previous_r
    pp_r = _soft_pole(dir_r,authored_r,anatomical_r,previous_r)
    # Steer the elbow plane toward the desired blade instead of erasing the sword gesture.
    # The grip frame is rig-specific, so evaluate candidate planes using the real socket.
    candidates = []
    for ang in (-50,-25,0,25,50):
        ar = math.radians(ang)
        pole = pp_r*math.cos(ar)+np.cross(dir_r,pp_r)*math.sin(ar)
        if previous_r is not None:
            prev_proj = _unit(_project(previous_r,dir_r),pp_r)
            pole = _slerp_dir(prev_proj,pole,min(1., math.radians(16)/max(1e-8,math.acos(float(np.clip(prev_proj@pole,-1,1))))))
        ep,hp = solve_two_bone_ik(wP[11],rh_w,0.26,0.24,wP[11]+dir_r*dist_r*.5+pole*.38,min_bend_deg=8.)
        ua,uf = _unit(ep-wP[11]),_unit(hp-ep)
        rf = build_arm_frame(uf,_unit(np.cross(ua,uf),(0,1,0)),is_right=True)
        bn = _unit(rf @ SWORD_SOCKET_R[:,1])
        angle = math.acos(float(np.clip(bn@raw_b_dir,-1,1)))
        cost = max(0.,angle-math.radians(42))**2+0.18*angle**2+0.20*ar**2
        candidates.append((cost,pole))
    costs=np.array([c[0] for c in candidates]); weights=np.exp(-(costs-costs.min())/0.075)
    pp_r=_unit(_project(sum(w*c[1] for w,c in zip(weights,candidates)),dir_r),pp_r)
    r_pole_w = wP[11] + dir_r * (dist_r * 0.50) + pp_r * 0.38
    r_elbow_w, r_hand_w = solve_two_bone_ik(wP[11], rh_w, 0.26, 0.24, r_pole_w, min_bend_deg=8.0)
    u_ra = v_norm(r_elbow_w - wP[11])
    u_rfa = v_norm(r_hand_w - r_elbow_w)
    r_hinge = v_norm(np.cross(u_ra, u_rfa), np.array([0.0, 1.0, 0.0]))
    R_rfa = build_arm_frame(u_rfa, r_hinge, is_right=True)

    b_neutral = v_norm(R_rfa @ SWORD_SOCKET_R[:, 1])
    b_y = _clamp_cone(raw_b_dir, b_neutral, 40.0)

    # Approximate capsule clearance: head, spine, left arm. Soft gradient correction
    # remains inside the wrist cone; a mesh-level collision guarantee needs rig geometry.
    head_c = wP[5] + wR[5] @ np.array([0.,0.06,0.02])
    capsules = [(head_c,head_c,0.165),(wP[0],wP[4],0.15),
                (wP[7],l_elbow_w,0.075),(l_elbow_w,l_hand_w,0.065)]
    def clearance_cost(d):
        # Real hilt offset in the hand socket, with neutral transported edge.
        ex=_unit(_shortest_arc_rot(b_neutral,d) @ (R_rfa @ SWORD_SOCKET_R[:,0]))
        ex=_unit(_project(ex,d),_perp(d))
        Rsw=np.column_stack((ex,d,np.cross(ex,d)))
        hilt=r_hand_w+Rsw@SWORD_SOCKET_R.T@SWORD_SOCKET_POS
        base=hilt+d*SWORD_BLADE_BASE_Y; tip=hilt+d*SWORD_BLADE_TIP_Y
        return sum(max(0.,rad-_segment_distance(base,tip,c,e))**2 for c,e,rad in capsules)+max(0.,0.035-min(base[1],tip[1]))**2
    for _ in range(4):
        if clearance_cost(b_y)<1e-8: break
        t1=_perp(b_y); t2=np.cross(b_y,t1); eps=0.025
        grad=np.array([(clearance_cost(_unit(b_y+eps*t))-clearance_cost(_unit(b_y-eps*t)))/(2*eps) for t in (t1,t2)])
        delta=grad[0]*t1+grad[1]*t2
        cand=_clamp_cone(_unit(b_y-2.0*delta),b_neutral,44.)
        if clearance_cost(cand)>=clearance_cost(b_y): break
        b_y=cand
    # Swing the complete neutral socket, not just its projected edge. This measures
    # real axial twist after swing and preserves a right-handed orthonormal blade frame.
    e_0=_unit(_shortest_arc_rot(b_neutral,b_y) @ (R_rfa @ SWORD_SOCKET_R[:,0]))
    e_0=_unit(_project(e_0,b_y),_perp(b_y))
    raw_e=to_w_dir(p.blade_edge) if p.blade_edge is not None else e_0
    ec=_unit(_project(raw_e,b_y),e_0)
    def roll_of(e): return math.degrees(math.atan2(b_y @ np.cross(e_0,e),e_0@e))
    prev_roll=0.
    if prev_edge_w is not None and prev_pose is not None:
        prev_b=rot_y(prev_pose.hips_yaw) @ np.asarray(prev_pose.blade_dir) if prev_pose.local_body_space else np.asarray(prev_pose.blade_dir)
        transported=_shortest_arc_rot(prev_b,b_y) @ prev_edge_w
        prev_roll=roll_of(_unit(_project(transported,b_y),e_0))
        prev_roll=float(np.clip(prev_roll,-35.,35.))
    roll=roll_of(ec)+float(p.blade_spin_deg)
    # A double-edged blade has 180-degree symmetry, but the wrist does not.
    roll=min((roll+180*k for k in range(-3,4)),key=lambda x: abs(x-prev_roll))
    roll=float(np.clip(roll,-35.,35.))
    if prev_edge_w is not None: roll=float(np.clip(roll,prev_roll-12.,prev_roll+12.))
    rr=math.radians(roll)
    e_x=_unit(e_0*math.cos(rr)+np.cross(b_y,e_0)*math.sin(rr))

    # 3. Contact-aware leg IK. Preserve authored foot heading and toe poses.
    leg_out=[]
    for side_idx,(i_up,ft_pos,ft_yaw,ft_pitch,turnout,air_flex) in enumerate([
        (14,p.l_foot_pos,p.l_foot_yaw,p.l_foot_pitch,10.,l_air_flex),
        (17,p.r_foot_pos,p.r_foot_yaw,p.r_foot_pitch,-12.,r_air_flex)]):
        side='l' if side_idx==0 else 'r'
        hip_p=wP[0]+wR[0] @ np.asarray(GAME_MIXAMO_20[i_up][3],dtype=float)
        fp_w=to_w_foot(ft_pos); fp_w[1]=max(.080,fp_w[1])
        contact=1.-_smooth((fp_w[1]-.095)/.10)
        d=fp_w-hip_p; dist=float(np.linalg.norm(d)); direction=_unit(d,(0,-1,0))
        if dist>.811:
            if contact>.5 and abs(d[1])<.811:
                # Keep ankle height; clamp horizontal reach only if pelvis correction cannot help.
                max_xz=math.sqrt(max(0.,.811**2-d[1]**2)); xz=d[[0,2]]
                fp_w[[0,2]]=hip_p[[0,2]]+xz*min(1.,max_xz/max(1e-9,np.linalg.norm(xz)))
            else: fp_w=hip_p+direction*.811
        elif dist<.16: fp_w=hip_p+direction*.16
        fp_w[1]=max(.080,fp_w[1]); d=fp_w-hip_p; dist=np.linalg.norm(d); direction=_unit(d,(0,-1,0))
        fy=ft_yaw+(p.hips_yaw if p.feet_in_body_space else 0.)
        fwd=rot_y(fy) @ np.array([0.,0.,1.])
        body_fwd=R_by @ np.array([0.,0.,1.])
        anatomy=(.7*contact)*fwd+(1.-.7*contact)*body_fwd
        raw_pole=getattr(p,side+'_knee_pole')
        authored=to_w_foot(raw_pole)-hip_p if raw_pole is not None else anatomy
        previous=None
        if prev_pose is not None:
            prev_hip=np.array([0.,prev_pose.hips_y,0.])+euler_yxz(prev_pose.hips_yaw,prev_pose.hips_pitch,prev_pose.hips_roll)@np.asarray(GAME_MIXAMO_20[i_up][3])
            previous=np.asarray(getattr(prev_pose,side+'_knee_pole'))
            if prev_pose.feet_in_body_space: previous=rot_y(prev_pose.hips_yaw)@previous
            previous=previous-prev_hip
            if contact<.5: previous=R_by@rot_y(prev_pose.hips_yaw).T@previous
        pole=_soft_pole(direction,authored,anatomy,previous)
        kp_w=hip_p+direction*(dist*.5)+pole*.38
        knee_w,ankle_w=solve_two_bone_ik(hip_p,fp_w,.42,.40,kp_w,min_bend_deg=6.)
        u_th=_unit(knee_w-hip_p); u_sh=_unit(ankle_w-knee_w)
        k_ax=_unit(np.cross(u_th,u_sh),np.cross(direction,pole))
        R_shin=build_leg_frame(u_sh,k_ax)
        ref_y,ref_p,ref_r=fy,ft_pitch,getattr(p,side+'_foot_roll')
        if prev_pose is not None:
            ref_y=getattr(prev_pose,side+'_foot_yaw')+(prev_pose.hips_yaw if prev_pose.feet_in_body_space else 0.)
            ref_p=getattr(prev_pose,side+'_foot_pitch'); ref_r=getattr(prev_pose,side+'_foot_roll')
        R_ground=rot_y(fy)@rot_x(-float(np.clip(ft_pitch,-25,18)))@rot_z(float(np.clip(getattr(p,side+'_foot_roll'),-10,10)))@_R_FOOT_REST
        R_air=R_shin@rot_x(-air_flex)
        # Blend actual rotations (not Euler components) through toe-off and touchdown.
        R_ft=quat_to_mat(quat_slerp(mat_to_quat(R_air),mat_to_quat(R_ground),contact))
        fy_w,fpitch,froll=_foot_euler_two_branch(R_ft,ref_y,ref_p,ref_r)
        leg_out.append((from_w_foot(ankle_w),from_w_foot(kp_w),fy_w-(p.hips_yaw if p.feet_in_body_space else 0.),fpitch,froll))

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


# Time-aware monotone cubic interpolation. Unlike uniform Catmull-Rom, short
# attack phases cannot inherit an oversized tangent from a long hold.
def _curve_slopes(t, x):
    x=np.asarray(x,dtype=float); t=np.asarray(t,dtype=float)
    h=np.diff(t); d=np.diff(x,axis=0)/h.reshape((-1,)+(1,)*(x.ndim-1))
    m=np.zeros_like(x)
    for i in range(1,len(t)-1):
        a,b=d[i-1],d[i]; good=(a*b)>0
        w1=2*h[i]+h[i-1]; w2=h[i]+2*h[i-1]
        denom=w1/np.where(good,a,1.)+w2/np.where(good,b,1.)
        m[i]=np.where(good,(w1+w2)/np.where(good,denom,1.),0.)
    return m


def _hermite(a,b,ma,mb,h,u):
    return (2*u**3-3*u*u+1)*a+(u**3-2*u*u+u)*h*ma+(-2*u**3+3*u*u)*b+(u**3-u*u)*h*mb


def _sphere_log(a,b):
    c=float(np.clip(a@b,-1,1)); v=_project(b,a)
    return _unit(v,_perp(a))*math.acos(c)


def _sphere_exp(a,v):
    n=np.linalg.norm(v)
    return _unit(a*math.cos(n)+v*(math.sin(n)/max(n,1e-12)),a)


def _sphere_tangents(t, vs):
    out=np.zeros_like(vs)
    for i in range(1,len(t)-1):
        a=-_sphere_log(vs[i],vs[i-1])/(t[i]-t[i-1]); b=_sphere_log(vs[i],vs[i+1])/(t[i+1]-t[i])
        if a@b>0:
            out[i]=(a*(t[i+1]-t[i])+b*(t[i]-t[i-1]))/(t[i+1]-t[i-1])
            limit=2*min(np.linalg.norm(a),np.linalg.norm(b)); out[i]*=min(1.,limit/max(1e-9,np.linalg.norm(out[i])))
    return out


def _sphere_cubic(a,b,ma,mb,h,u):
    c=_sphere_exp(a,ma*h/3); d=_sphere_exp(b,-mb*h/3)
    ab=_slerp_dir(a,c,u,b); bc=_slerp_dir(c,d,u,b); cd=_slerp_dir(d,b,u,a)
    return _slerp_dir(_slerp_dir(ab,bc,u,b),_slerp_dir(bc,cd,u,a),u,b)


def _world_feet(p):
    if not p.feet_in_body_space: return p
    R=rot_y(p.hips_yaw); kw={'feet_in_body_space':False}
    for side in ('l','r'):
        for suffix in ('_foot_pos','_knee_pole'):
            value=getattr(p,side+suffix)
            if value is not None: kw[side+suffix]=tuple(R@np.asarray(value))
        kw[side+'_foot_yaw']=getattr(p,side+'_foot_yaw')+p.hips_yaw
    return replace(p,**kw)


def _compile_clip(name,slot,duration,description,keyframes,tags,hit_window=None,
                  l_air_flex=24.,r_air_flex=26.,smooth_passes=4):
    """Bake authored poses on a fine time grid; apply constraints exactly once.

    Original names, duration and hit windows are deliberately preserved. The
    sampler still belongs to rig_core; disable its optional extra smoothing to
    avoid filtering planted feet and impact timing a second time.
    """
    ks=sorted(keyframes,key=lambda k:k.t); times=np.array([k.t for k in ks],dtype=float)
    if len(ks)<2 or np.any(np.diff(times)<=0) or abs(times[0])>1e-8 or abs(times[-1]-duration)>1e-6:
        raise ValueError(f'{name}: invalid keyframe times')
    raw=[k.pose for k in ks]
    if any(p.local_body_space!=raw[0].local_body_space or p.feet_in_body_space!=raw[0].feet_in_body_space or p.head_world_space!=raw[0].head_world_space for p in raw):
        raise ValueError(f'{name}: mixed coordinate spaces; convert authored keys first')
    # Airborne spins retain local foot coordinates; grounded spins use world
    # contacts so a rotating pelvis does not drag both feet around the origin.
    grounded_spin=name.startswith(('js22_','js27_'))
    if grounded_spin: raw=[_world_feet(p) for p in raw]
    # Single-leg balance: position the supporting ankle under an approximate
    # projected mass center at the authored hold. Keep the same anchor from
    # preparation through hold (not a frame-by-frame moving foot target).
    if 'balance' in tags:
        raised=[j for j,p in enumerate(raw) if min(p.l_foot_pos[1],p.r_foot_pos[1])<=.105 and max(p.l_foot_pos[1],p.r_foot_pos[1])>.30]
        if raised:
            peak=raw[raised[len(raised)//2]]
            side='l' if peak.l_foot_pos[1]<peak.r_foot_pos[1] else 'r'
            other='r' if side=='l' else 'l'
            wp,_=_torso_frames(peak)
            # Rough segment-mass proxy: pelvis/trunk/head/arms/raised leg.
            com=.24*wp[0]+.38*(wp[1]+wp[3])*.5+.08*wp[5]+.05*np.asarray(peak.l_hand_pos)+.05*np.asarray(peak.r_hand_pos)+.20*(wp[0]+np.asarray(getattr(peak,other+'_foot_pos')))*.5
            anchor=np.array(getattr(peak,side+'_foot_pos'),dtype=float)
            delta=(com-anchor)[[0,2]]; length=np.linalg.norm(delta)
            anchor[[0,2]]+=delta*min(.70,.085/max(1e-9,length))
            for j in range(max(1,raised[0]-1),raised[-1]+1):
                old=np.array(getattr(raw[j],side+'_foot_pos'),dtype=float)
                if old[1]<=.105:
                    old[[0,2]]=anchor[[0,2]]
                    raw[j]=replace(raw[j],**{side+'_foot_pos':tuple(old)})
    ts=times; h=np.diff(ts)
    # Keep all dataclass fields, including foot roll, blade spin, and hand up.
    dirs={'blade_dir':(0,.4,.9),'blade_edge':(0,-.9,.4),'l_hand_dir':(-.2,.3,.9),'l_hand_up':(0,1,0)}
    curves={}; spherical={}
    for field in fields(raw[0]):
        f=field.name; vals=[getattr(p,f) for p in raw]
        if isinstance(vals[0],(bool,np.bool_)): continue
        if f in dirs:
            # Unspecified edge requests use the previous authored edge (or neutral default).
            if f=='blade_edge' and all(v is None for v in vals): continue
            vs=[]
            for v in vals:
                vv=_unit(v if v is not None else (vs[-1] if vs else dirs[f]))
                if f=='blade_edge' and vs and vv@vs[-1]<0: vv=-vv
                vs.append(vv)
            vs=np.array(vs); spherical[f]=(vs,_sphere_tangents(ts,vs)); continue
        if any(v is None for v in vals): continue
        try: x=np.asarray(vals,dtype=float)
        except (TypeError,ValueError): continue
        if x.ndim not in (1,2): continue
        # Pelvis yaw is authored unwrapped (full +/-360 spins). Never shortest-path it.
        if f.endswith('_foot_yaw') and not grounded_spin:
            for i in range(1,len(x)): x[i]=_unwrap_deg(x[i],x[i-1])
        curves[f]=(x,_curve_slopes(ts,x))
    # Identify authored planted-to-planted steps. Lift a moving foot instead of
    # sliding it along the floor; when both move, stagger their swing phases.
    footplans={}
    for i in range(len(raw)-1):
        moving=[]
        for side in ('l','r'):
            a=np.asarray(getattr(raw[i],side+'_foot_pos')); b=np.asarray(getattr(raw[i+1],side+'_foot_pos'))
            distance=np.linalg.norm((b-a)[[0,2]])
            if max(a[1],b[1])<=.105 and distance>.025:
                moving.append((side,a,b,distance))
        if len(moving)==2:
            # Start with the smaller adjustment, unless the other leg already rises next.
            moving.sort(key=lambda item:item[3])
        for j,(side,a,b,distance) in enumerate(moving):
            start,end=(.08,.92) if len(moving)==1 else ((.03,.49) if j==0 else (.51,.97))
            footplans[i,side]=(a,b,start,end,min(.15,max(.035,.30*distance)))
    # Approximate ballistic pelvis arc for the six authored airborne techniques.
    # Keep authored apex silhouette via a height offset; change all airborne
    # limb targets by the same vertical amount. Flight phases use a symmetric
    # constant-acceleration reference blended into the authored curve.
    flight=None
    if 'jump' in tags or 'butterfly' in tags or 'nandu' in tags:
        air=[j for j,p in enumerate(raw) if min(p.l_foot_pos[1],p.r_foot_pos[1])>.15]
        if air:
            j0,j1=air[0],air[-1]
            if j0>0 and j1<len(raw)-1:
                t0,t1=ts[j0-1],ts[j1+1]
                # Lift-off occurs after the crouch; touchdown precedes the landing compression.
                take=t0+.10*(ts[j0]-t0); land=t1-.10*(t1-ts[j1]);
                y0=max(.90,raw[j0-1].hips_y); y1=max(.90,raw[j1+1].hips_y)
                flight=(take,land,y0,y1)
    # Each authored interval has its own uniform subgrid, keeping original
    # accents exactly while avoiding almost coincident inserted samples.
    grid=np.concatenate([np.linspace(ts[i],ts[i+1],max(1,int(math.ceil(h[i]*90)))+1)[:-1] for i in range(len(h))]+[ts[-1:]])
    out=[]; prev=None; prev_edge=None
    for t in grid:
        i=min(len(ts)-2,max(0,bisect.bisect_right(ts,t)-1)); u=float(np.clip((t-ts[i])/h[i],0,1))
        kw={}
        for f,(x,m) in curves.items():
            v=_hermite(x[i],x[i+1],m[i],m[i+1],h[i],u)
            kw[f]=float(v) if np.ndim(v)==0 else tuple(map(float,v))
        for f,(x,m) in spherical.items(): kw[f]=tuple(_sphere_cubic(x[i],x[i+1],m[i],m[i+1],h[i],u))
        for side in ('l','r'):
            plan=footplans.get((i,side))
            if plan is None: continue
            a,b,start,end,lift=plan; z=float(np.clip((u-start)/(end-start),0,1)); q=_smooth(z)
            pos=(1-q)*a+q*b
            # sin^2 lift gives zero vertical speed at both contact boundaries.
            pos[1]+=lift*math.sin(math.pi*z)**2
            kw[side+'_foot_pos']=tuple(pos)
            # Foot pivot is intentional; otherwise swing heading follows the foot.
            if not grounded_spin:
                va=getattr(raw[i],side+'_foot_yaw'); vb=_unwrap_deg(getattr(raw[i+1],side+'_foot_yaw'),va)
                kw[side+'_foot_yaw']=va+(vb-va)*q
        p=replace(raw[i],**kw)
        if flight is not None and flight[0]<t<flight[1]:
            take,land,y0,y1=flight; T=land-take; tau=t-take
            ballistic=y0+(y1-y0)*tau/T+4.905*tau*(T-tau)
            blend=.70*_smooth(tau/.085)*_smooth((T-tau)/.085)
            dy=(ballistic-p.hips_y)*blend
            updates={'hips_y':p.hips_y+dy}
            for side in ('l','r'):
                for suffix in ('_hand_pos','_elbow_pole','_foot_pos','_knee_pole'):
                    f=side+suffix; value=getattr(p,f)
                    if value is not None:
                        v=np.array(value,dtype=float); v[1]+=dy if suffix in ('_hand_pos','_elbow_pole') else dy*_smooth((v[1]-.08)/.18)
                        if suffix=='_foot_pos': v[1]=max(.08,v[1])
                        updates[f]=tuple(v)
            p=replace(p,**updates)
        sp,edge=_stabilize_pose(p,prev_pose=prev,prev_edge_w=prev_edge,l_air_flex=l_air_flex,r_air_flex=r_air_flex)
        prev,prev_edge=sp,edge
        out.append(Keyframe(float(t),sp,'flow',tension=0.0))
    # Align terminal guard exactly with initial guard when the authored movement
    # returns to it. Keep unwrapped yaw (and feet heading for grounded spins).
    a,b=out[0].pose,out[-1].pose
    if abs(((raw[-1].hips_yaw-raw[0].hips_yaw+180)%360)-180)<1e-5 and abs(raw[-1].hips_y-raw[0].hips_y)<1e-5:
        if not grounded_spin and raw[0].feet_in_body_space:
            target=replace(a,hips_yaw=b.hips_yaw)
        else:
            target=replace(a,hips_yaw=b.hips_yaw,
                           l_foot_yaw=_unwrap_deg(a.l_foot_yaw,b.l_foot_yaw),r_foot_yaw=_unwrap_deg(a.r_foot_yaw,b.r_foot_yaw))
        # Only blend rotation/pole corrections; keep the final contact trajectory.
        # Endpoint is exactly the same world-space guard (up to full turns).
        tail=min(.24,duration*.22)
        for j in range(len(out)-2,0,-1):
            k=out[j]; remain=duration-k.t
            if remain>=tail: break
            w=_smooth(1-remain/tail); changes={}
            for f in ('blade_dir','blade_edge','l_hand_dir','l_hand_up'):
                av,bv=getattr(k.pose,f),getattr(target,f)
                if av is not None and bv is not None: changes[f]=tuple(_slerp_dir(av,bv,w))
            if 'blade_dir' in changes and 'blade_edge' in changes:
                bb=np.asarray(changes['blade_dir']); changes['blade_edge']=tuple(_unit(_project(np.asarray(changes['blade_edge']),bb),_perp(bb)))
            for f in ('l_elbow_pole','r_elbow_pole','l_knee_pole','r_knee_pole'):
                av,bv=getattr(k.pose,f),getattr(target,f)
                if av is not None and bv is not None: changes[f]=tuple((1-w)*np.asarray(av)+w*np.asarray(bv))
            for f in ('l_foot_roll','r_foot_roll','l_foot_pitch','r_foot_pitch','l_foot_yaw','r_foot_yaw'):
                av,bv=getattr(k.pose,f),getattr(target,f); changes[f]=av+w*(_unwrap_deg(bv,av)-av)
            out[j]=Keyframe(k.t,replace(k.pose,**changes),'flow',tension=0.)
        out[-1]=Keyframe(duration,target,'flow',tension=0.)
    return AnimationClip(name=name,slot=slot,duration=duration,loop=False,description=description,
                         keyframes=out,tags=tags,hit_window=hit_window,smooth_passes=0)


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
    return replace(_RAW_GUARD, **overrides) if overrides else _RAW_GUARD


def _spin_guard(yaw_deg: float) -> PoseSpec:
    """Body-space equivalent of the raw guard, projected only during compilation."""
    # Convert authored world coordinates into pelvis-yaw body space at -24 degrees.
    R_inv = rot_y(24.0)
    def bw_pt(pt: Tuple[float, float, float]) -> Tuple[float, float, float]:
        v = R_inv @ np.array([pt[0], 0.0, pt[2]], dtype=np.float64)
        return (float(v[0]), float(pt[1]), float(v[2]))
    def bw_dir(d: Tuple[float, float, float]) -> Tuple[float, float, float]:
        v = R_inv @ np.array(d, dtype=np.float64)
        return (float(v[0]), float(v[1]), float(v[2]))

    g = _RAW_GUARD
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
        l_foot_roll=g.l_foot_roll,
        r_foot_pos=bw_pt(g.r_foot_pos),
        r_knee_pole=bw_pt(g.r_knee_pole),
        r_foot_yaw=g.r_foot_yaw + 24.0,
        r_foot_pitch=g.r_foot_pitch,
        r_foot_roll=g.r_foot_roll,
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


# ==============================================================================
# V3: DIRECT KINEMATIC BAKE — only affects Jianshu, not other animation libraries.
# ==============================================================================

def _temporal_filter(t, x, sigma, endpoint=True):
    """Gaussian low-pass in real seconds, exact endpoint and smooth edge taper."""
    x=np.asarray(x,dtype=float); out=x.copy()
    for i,ti in enumerate(t):
        ids=np.flatnonzero(np.abs(t-ti)<=3.0*sigma)
        w=np.exp(-.5*((t[ids]-ti)/sigma)**2); w/=w.sum()
        filtered=np.tensordot(w,x[ids],axes=(0,0))
        blend=_smooth(min(ti-t[0],t[-1]-ti)/max(.001,2*sigma)) if endpoint else 1.
        out[i]=x[i]+blend*(filtered-x[i])
    return out


def _filter_bend_planes(poses, t, worlds):
    """Smooth signed bend-plane angle in parallel-transported tangent frames.

    No averaging of local joint quaternions: hand and ankle endpoints stay fixed.
    Angles are measured in pelvis-yaw space, so full body spins are retained.
    """
    updates=[{} for _ in poses]
    for root,mid,end,field,bodyflag,sigma in (
        (7,8,9,'l_elbow_pole','local_body_space',.052),
        (11,12,13,'r_elbow_pole','local_body_space',.035),
        (14,15,16,'l_knee_pole','feet_in_body_space',.036),
        (17,18,19,'r_knee_pole','feet_in_body_space',.036)):
        dirs=[]; bases=[]; ang=[]; prev_axis=prev_base=None
        for p,wp in zip(poses,worlds):
            inv=rot_y(p.hips_yaw).T
            a=_unit(inv@(wp[end]-wp[root])); v=_unit(_project(inv@(wp[mid]-wp[root]),a),_perp(a))
            base=v if prev_base is None else _unit(_project(_shortest_arc_rot(prev_axis,a)@prev_base,a),v)
            dirs.append(a); bases.append(base)
            ang.append(math.atan2(a@np.cross(base,v),base@v)); prev_axis,prev_base=a,base
        ang=np.unwrap(ang); filt=_temporal_filter(t,ang,sigma)
        for i,(p,wp,a,b,theta) in enumerate(zip(poses,worlds,dirs,bases,filt)):
            pole_dir=b*math.cos(theta)+np.cross(a,b)*math.sin(theta)
            world_pole=wp[root]+rot_y(p.hips_yaw)@(a*.20+pole_dir*.38)
            if getattr(p,bodyflag): world_pole=rot_y(p.hips_yaw).T@world_pole
            updates[i][field]=tuple(world_pole)
    return [replace(p,**u) for p,u in zip(poses,updates)]


def _smooth_world_orientations(t, mats, yaw, sigma):
    """Filter rotations in rotating pelvis coordinates, using unit quaternions."""
    q=np.array([mat_to_quat(rot_y(y).T@R) for y,R in zip(yaw,mats)])
    for i in range(1,len(q)):
        if q[i]@q[i-1]<0: q[i]*=-1
    out=[]
    for i,ti in enumerate(t):
        ids=np.flatnonzero(np.abs(t-ti)<=3*sigma); w=np.exp(-.5*((t[ids]-ti)/sigma)**2); w/=w.sum()
        local=q[ids].copy(); local[(local@q[i])<0]*=-1
        avg=np.sum(local*w[:,None],axis=0); avg/=max(1e-12,np.linalg.norm(avg))
        blend=_smooth(min(ti-t[0],t[-1]-ti)/(2*sigma))
        out.append(rot_y(yaw[i])@quat_to_mat(quat_slerp(q[i],avg,blend)))
    return out


def _channels_from_world(wR):
    return np.array([mat_to_quat(wR[i] if b[2]<0 else wR[b[2]].T@wR[i]) for i,b in enumerate(GAME_MIXAMO_20)])


def _sample_jianshu(clip, fps=60):
    """Bake dense solved channels directly; no second cubic interpolation of IK targets."""
    if not isinstance(fps,(int,float)) or not math.isfinite(fps) or fps<=0: raise ValueError('fps must be positive')
    ks=clip.keyframes; t=np.array([k.t for k in ks]); poses=[k.pose for k in ks]
    first=[solve_pose(p) for p in poses]
    poses=_filter_bend_planes(poses,t,[v[2] for v in first])
    # Smooth ankle and left-hand world orientations separately without moving wrists/ankles.
    yaw=[p.hips_yaw for p in poses]
    rotations={i:_smooth_world_orientations(t,[v[3][i] for v in first],yaw,sigma) for i,sigma in ((9,.030),(16,.022),(19,.022))}
    hs=[]; qs=[]; state={}
    for i,p in enumerate(poses):
        # Use correct ankle orientation to prevent toe/heel penetration of an approximate foot sole.
        # Raised sole corners are a proxy, not a shoe mesh. Do not alter XZ foot anchors.
        kw={}
        for side,bi in (('l',16),('r',19)):
            R=rotations[bi][i]
            f=np.array(getattr(p,side+'_foot_pos'),dtype=float)
            corners=np.array([[x,.08,z] for x in (-.045,.045) for z in (-.16,.05)])
            need=max(0.,-float(np.min(f[1]+(corners@R.T)[:,1])))
            f[1]+=need
            kw[side+'_foot_pos']=tuple(f)
        p=replace(p,**kw)
        hp,lq,wp,wr=solve_pose(p,state=state)
        # Replace only terminal frames; these joints have no children in the 20-bone rig.
        for bi in (9,16,19): wr[bi]=rotations[bi][i]
        lq=_channels_from_world(wr)
        if qs: lq[np.sum(qs[-1]*lq,axis=1)<0]*=-1
        hs.append(hp); qs.append(lq)
    hs=np.asarray(hs); qs=np.asarray(qs)
    n=max(2,int(round(clip.duration*fps))+1); times=np.linspace(0,clip.duration,n)
    h_out=[]; q_out=[]
    for tt in times:
        j=min(len(t)-2,max(0,int(np.searchsorted(t,tt,side='right')-1)))
        u=float(np.clip((tt-t[j])/(t[j+1]-t[j]),0,1))
        hp=(1-u)*hs[j]+u*hs[j+1]
        q=np.array([quat_slerp(a,b,u) for a,b in zip(qs[j],qs[j+1])])
        wp,wr=fk_from_channels(hp,q)
        # Sub-millimeter numerical floor protection; preserves in-place root contract.
        min_y=min(wp[16,1],wp[19,1]); hp[1]+=max(0.,.08-min_y)
        if q_out: q[np.sum(q_out[-1]*q,axis=1)<0]*=-1
        h_out.append(hp); q_out.append(q)
    clip.times=times.astype(np.float32); clip.hips_trans=np.asarray(h_out,dtype=np.float32)
    clip.rot_tracks=np.asarray(q_out,dtype=np.float32).transpose(1,0,2).copy()
    return clip


def build_all_jianshu_clips(fps: int = 60) -> List[AnimationClip]:
    """Build and sample all 32 clips on the exact GAME_MIXAMO_20 skeleton."""
    return [_sample_jianshu(fn(), fps=fps) for fn in JIANSHU_32_BUILDERS]

# Explicit alias for callers that sample individual Jianshu builders.
sample_clip = _sample_jianshu
