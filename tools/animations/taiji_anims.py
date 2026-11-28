#!/usr/bin/env python3
"""
tools/animations/taiji_anims.py — Complete 30-Clip Tai Chi (Thái Cực Quyền) Library

Combines Yang-style 24-form continuous silk-reeling flow (Triền Ti Kình) with Chen-style
rooted Fajin bursts, built on rig_core.py with:
  - True C2 Catmull-Rom continuous arcs ("flow") and organic moving Zanshin holds
  - Empty/Full (Hư Thực) weight transfer, waist-led kinetic chain & S-curve spine poise
  - All 30 clips tuned to pass strict FK/anatomy verification (grounded footY=0.08,
    virtual tipMinY > 0.25m, smooth wrist orientation without sudden 180-deg flips)
"""

from __future__ import annotations

from dataclasses import replace
from typing import List

from .rig_core import AnimationClip, Keyframe, PoseSpec, sample_clip


# ==============================================================================
# CANONICAL TAI CHI GUARD POSE (Vô Cực Khởi Thức / Ôm Cầu)
# ==============================================================================

def _guard(**overrides) -> PoseSpec:
    """Relaxed, sunk-shoulder Tai Chi guard: left palm extended in Peng, right palm cradling Dan-Tian."""
    base = PoseSpec(
        hips_y=0.895,
        hips_yaw=-20.0,
        hips_pitch=3.0,
        hips_roll=0.0,
        torso_yaw=12.0,
        torso_pitch=2.5,
        torso_roll=-1.0,
        head_yaw=0.0,
        head_pitch=1.5,
        head_roll=0.0,
        l_shoulder_lift=-4.0,
        l_shoulder_fwd=10.0,
        r_shoulder_lift=-5.0,
        r_shoulder_fwd=8.0,
        # Right hand softly suspended near lower chest/Dan-Tian (sunk elbow)
        r_hand_pos=(-0.20, 1.06, 0.32),
        r_elbow_pole=(-0.56, 0.86, -0.06),
        blade_dir=(0.14, 0.28, 0.95),
        blade_edge=(0.0, 0.96, -0.28),
        blade_spin_deg=0.0,
        # Left palm floating forward-left in soft rounded Peng arc
        l_hand_pos=(0.18, 1.22, 0.42),
        l_elbow_pole=(0.56, 0.98, 0.06),
        l_hand_dir=(-0.18, 0.24, 0.95),
        l_hand_up=(0.10, 0.97, -0.22),
        # Softly rooted Taiji bow-empty stance (left light forward, right anchored)
        l_foot_pos=(0.16, 0.080, 0.20),
        l_knee_pole=(0.22, 0.46, 0.74),
        l_foot_yaw=10.0,
        l_foot_pitch=0.0,
        r_foot_pos=(-0.19, 0.080, -0.18),
        r_knee_pole=(-0.29, 0.46, 0.42),
        r_foot_yaw=-24.0,
        r_foot_pitch=0.0,
    )
    return replace(base, **overrides) if overrides else base


# ==============================================================================
# 1. STANCES & LOCOMOTION (5 CLIPS)
# ==============================================================================

def make_taiji_idle() -> AnimationClip:
    """1. Vô Cực Khởi Thức: Harmonic Dan-Tian breathing with subtle floating Taiji sphere sway."""
    g0 = _guard()
    g1 = _guard(
        hips_y=0.912, hips_yaw=-16.5, hips_roll=-1.2,
        torso_yaw=14.5, torso_pitch=1.2, torso_roll=0.8, head_roll=-0.8,
        r_hand_pos=(-0.18, 1.105, 0.35), r_elbow_pole=(-0.54, 0.90, -0.04),
        blade_dir=(0.12, 0.36, 0.93),
        l_hand_pos=(0.21, 1.260, 0.44), l_elbow_pole=(0.58, 1.02, 0.08),
        l_hand_dir=(-0.14, 0.32, 0.94),
    )
    g2 = _guard(
        hips_y=0.880, hips_yaw=-23.5, hips_roll=1.2,
        torso_yaw=9.5, torso_pitch=4.0, torso_roll=-2.0, head_roll=0.8,
        r_hand_pos=(-0.22, 1.025, 0.30), r_elbow_pole=(-0.58, 0.83, -0.08),
        blade_dir=(0.18, 0.20, 0.96),
        l_hand_pos=(0.16, 1.185, 0.40), l_elbow_pole=(0.54, 0.94, 0.04),
        l_hand_dir=(-0.22, 0.18, 0.96),
    )
    g3 = _guard(
        hips_y=0.904, hips_yaw=-18.5, hips_roll=-0.6,
        torso_yaw=13.0, torso_pitch=2.0, torso_roll=-0.4,
        r_hand_pos=(-0.19, 1.080, 0.33),
        l_hand_pos=(0.19, 1.235, 0.43),
    )
    return AnimationClip(
        name="taiji_idle", slot="taiji_idle", duration=2.20, loop=True,
        tags=["taiji", "locomotion", "idle", "wuxia"],
        description="Serene Taiji Wuji-Peng breathing stance (Vô Cực Khởi Thức).",
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.55, g1, "flow"),
            Keyframe(1.10, g2, "flow"),
            Keyframe(1.65, g3, "flow"),
            Keyframe(2.20, g0, "flow"),
        ],
    )


def make_taiji_walk() -> AnimationClip:
    """2. Miêu Bộ Hành Vân: Level-hips Taiji cat-step walk (treading silently like a cat, flowing palms)."""
    base = dict(hips_pitch=4.5, torso_pitch=3.5, head_yaw=0.0, head_pitch=1.0,
                r_shoulder_lift=-5.0, l_shoulder_lift=-5.0)
    p0 = PoseSpec(
        **base, hips_y=0.875, hips_yaw=8.0, torso_yaw=-6.5, hips_roll=-1.5, torso_roll=1.5,
        r_shoulder_fwd=12.0, l_shoulder_fwd=-2.0,
        r_hand_pos=(-0.20, 1.08, 0.30), r_elbow_pole=(-0.54, 0.92, -0.06),
        blade_dir=(0.12, 0.18, 0.98), blade_edge=(0.0, 0.98, -0.18),
        l_hand_pos=(0.24, 1.00, 0.06), l_elbow_pole=(0.54, 0.96, -0.22),
        l_hand_dir=(-0.10, -0.06, 0.99), l_hand_up=(0.10, 0.99, 0.06),
        l_foot_pos=(0.13, 0.085, 0.25), l_knee_pole=(0.16, 0.46, 0.74), l_foot_yaw=6.0, l_foot_pitch=10.0,
        r_foot_pos=(-0.13, 0.090, -0.23), r_knee_pole=(-0.16, 0.44, 0.42), r_foot_yaw=-6.0, r_foot_pitch=-14.0,
    )
    p1 = PoseSpec(
        **base, hips_y=0.892, hips_yaw=0.0, torso_yaw=0.0, hips_roll=-2.0, torso_roll=2.0,
        r_shoulder_fwd=5.0, l_shoulder_fwd=5.0,
        r_hand_pos=(-0.22, 1.04, 0.18), r_elbow_pole=(-0.55, 0.94, -0.16),
        blade_dir=(0.10, 0.06, 0.99), blade_edge=(0.0, 0.99, -0.06),
        l_hand_pos=(0.22, 1.04, 0.18), l_elbow_pole=(0.55, 0.94, -0.16),
        l_hand_dir=(-0.10, 0.06, 0.99), l_hand_up=(0.10, 0.99, -0.06),
        l_foot_pos=(0.13, 0.080, 0.01), l_knee_pole=(0.15, 0.48, 0.62), l_foot_yaw=3.0, l_foot_pitch=0.0,
        r_foot_pos=(-0.13, 0.160, 0.02), r_knee_pole=(-0.16, 0.48, 0.76), r_foot_yaw=-4.0, r_foot_pitch=-4.0,
    )
    p2 = PoseSpec(
        **base, hips_y=0.875, hips_yaw=-8.0, torso_yaw=6.5, hips_roll=1.5, torso_roll=-1.5,
        r_shoulder_fwd=-2.0, l_shoulder_fwd=12.0,
        r_hand_pos=(-0.24, 1.00, 0.06), r_elbow_pole=(-0.54, 0.96, -0.22),
        blade_dir=(0.10, -0.06, 0.99), blade_edge=(0.0, 0.99, 0.06),
        l_hand_pos=(0.20, 1.08, 0.30), l_elbow_pole=(0.54, 0.92, -0.06),
        l_hand_dir=(-0.12, 0.18, 0.98), l_hand_up=(0.10, 0.98, -0.18),
        l_foot_pos=(0.13, 0.090, -0.23), l_knee_pole=(0.16, 0.44, 0.42), l_foot_yaw=6.0, l_foot_pitch=-14.0,
        r_foot_pos=(-0.13, 0.085, 0.25), r_knee_pole=(-0.16, 0.46, 0.74), r_foot_yaw=-6.0, r_foot_pitch=10.0,
    )
    p3 = PoseSpec(
        **base, hips_y=0.892, hips_yaw=0.0, torso_yaw=0.0, hips_roll=2.0, torso_roll=-2.0,
        r_shoulder_fwd=5.0, l_shoulder_fwd=5.0,
        r_hand_pos=(-0.22, 1.04, 0.18), r_elbow_pole=(-0.55, 0.94, -0.16),
        blade_dir=(0.10, 0.06, 0.99), blade_edge=(0.0, 0.99, -0.06),
        l_hand_pos=(0.22, 1.04, 0.18), l_elbow_pole=(0.55, 0.94, -0.16),
        l_hand_dir=(-0.10, 0.06, 0.99), l_hand_up=(0.10, 0.99, -0.06),
        l_foot_pos=(0.13, 0.160, 0.02), l_knee_pole=(0.16, 0.48, 0.76), l_foot_yaw=4.0, l_foot_pitch=-4.0,
        r_foot_pos=(-0.13, 0.080, 0.01), r_knee_pole=(-0.15, 0.48, 0.62), r_foot_yaw=-3.0, r_foot_pitch=0.0,
    )
    return AnimationClip(
        name="taiji_walk", slot="taiji_walk", duration=1.15, loop=True,
        tags=["taiji", "locomotion", "walk", "wuxia"],
        description="Level-hips silent Taiji cat-step walk (Miêu Bộ Hành Vân).",
        keyframes=[
            Keyframe(0.00, p0, "flow"), Keyframe(0.2875, p1, "flow"),
            Keyframe(0.575, p2, "flow"), Keyframe(0.8625, p3, "flow"), Keyframe(1.15, p0, "flow"),
        ],
    )


def make_taiji_run() -> AnimationClip:
    """3. Ngự Phong Khinh Bộ: Buoyant Taiji Khinh Công glide-run with poised forward palms & zero wrist flip."""
    base = dict(hips_pitch=15.0, torso_pitch=15.0, head_yaw=0.0, head_pitch=-3.0)
    p0 = PoseSpec(
        **base, hips_y=0.840, hips_yaw=11.0, torso_yaw=-12.0, hips_roll=-2.2, torso_roll=2.5, head_roll=-0.8,
        r_shoulder_lift=-1.0, r_shoulder_fwd=16.0, l_shoulder_lift=-4.0, l_shoulder_fwd=-10.0,
        r_hand_pos=(-0.22, 1.14, 0.36), r_elbow_pole=(-0.54, 0.98, -0.02),
        blade_dir=(0.12, 0.22, 0.97), blade_edge=(0.0, 0.97, -0.22),
        l_hand_pos=(0.30, 0.98, -0.14), l_elbow_pole=(0.56, 1.06, -0.40),
        l_hand_dir=(-0.08, -0.18, 0.98), l_hand_up=(0.08, 0.98, 0.18),
        l_foot_pos=(0.13, 0.085, 0.34), l_knee_pole=(0.16, 0.48, 0.86), l_foot_yaw=4.0, l_foot_pitch=14.0,
        r_foot_pos=(-0.13, 0.265, -0.34), r_knee_pole=(-0.16, 0.42, 0.24), r_foot_yaw=-4.0, r_foot_pitch=-32.0,
    )
    p1 = PoseSpec(
        **base, hips_y=0.905, hips_yaw=0.0, torso_yaw=0.0, hips_roll=-2.8, torso_roll=2.8,
        r_shoulder_lift=-2.0, r_shoulder_fwd=4.0, l_shoulder_lift=-2.0, l_shoulder_fwd=4.0,
        r_hand_pos=(-0.26, 1.06, 0.12), r_elbow_pole=(-0.56, 1.00, -0.22),
        blade_dir=(0.08, 0.02, 0.996), blade_edge=(0.0, 0.998, -0.02),
        l_hand_pos=(0.26, 1.06, 0.12), l_elbow_pole=(0.56, 1.00, -0.22),
        l_hand_dir=(-0.08, 0.02, 0.996), l_hand_up=(0.08, 0.996, -0.02),
        l_foot_pos=(0.13, 0.100, -0.08), l_knee_pole=(0.16, 0.46, 0.56), l_foot_yaw=2.0, l_foot_pitch=-10.0,
        r_foot_pos=(-0.13, 0.305, 0.12), r_knee_pole=(-0.16, 0.62, 0.88), r_foot_yaw=-3.0, r_foot_pitch=-10.0,
    )
    p2 = PoseSpec(
        **base, hips_y=0.840, hips_yaw=-11.0, torso_yaw=12.0, hips_roll=2.2, torso_roll=-2.5, head_roll=0.8,
        r_shoulder_lift=-4.0, r_shoulder_fwd=-10.0, l_shoulder_lift=-1.0, l_shoulder_fwd=16.0,
        r_hand_pos=(-0.30, 0.98, -0.14), r_elbow_pole=(-0.56, 1.06, -0.40),
        blade_dir=(0.08, -0.18, 0.98), blade_edge=(0.0, 0.98, 0.18),
        l_hand_pos=(0.22, 1.14, 0.36), l_elbow_pole=(0.54, 0.98, -0.02),
        l_hand_dir=(-0.12, 0.22, 0.97), l_hand_up=(0.08, 0.97, -0.22),
        l_foot_pos=(0.13, 0.265, -0.34), l_knee_pole=(0.16, 0.42, 0.24), l_foot_yaw=4.0, l_foot_pitch=-32.0,
        r_foot_pos=(-0.13, 0.085, 0.34), r_knee_pole=(-0.16, 0.48, 0.86), r_foot_yaw=-4.0, r_foot_pitch=14.0,
    )
    p3 = PoseSpec(
        **base, hips_y=0.905, hips_yaw=0.0, torso_yaw=0.0, hips_roll=2.8, torso_roll=-2.8,
        r_shoulder_lift=-2.0, r_shoulder_fwd=4.0, l_shoulder_lift=-2.0, l_shoulder_fwd=4.0,
        r_hand_pos=(-0.26, 1.06, 0.12), r_elbow_pole=(-0.56, 1.00, -0.22),
        blade_dir=(0.08, 0.02, 0.996), blade_edge=(0.0, 0.998, -0.02),
        l_hand_pos=(0.26, 1.06, 0.12), l_elbow_pole=(0.56, 1.00, -0.22),
        l_hand_dir=(-0.08, 0.02, 0.996), l_hand_up=(0.08, 0.996, -0.02),
        l_foot_pos=(0.13, 0.305, 0.12), l_knee_pole=(0.16, 0.62, 0.88), l_foot_yaw=3.0, l_foot_pitch=-10.0,
        r_foot_pos=(-0.13, 0.100, -0.08), r_knee_pole=(-0.16, 0.46, 0.56), r_foot_yaw=-2.0, r_foot_pitch=-10.0,
    )
    return AnimationClip(
        name="taiji_run", slot="taiji_run", duration=0.74, loop=True,
        tags=["taiji", "locomotion", "run", "khinh_cong"],
        description="Buoyant Taiji Khinh Công glide-run (Ngự Phong Khinh Bộ).",
        keyframes=[
            Keyframe(0.00, p0, "flow"), Keyframe(0.185, p1, "flow"),
            Keyframe(0.37, p2, "flow"), Keyframe(0.555, p3, "flow"), Keyframe(0.74, p0, "flow"),
        ],
    )


def make_taiji_strafe_left() -> AnimationClip:
    """4. Vân Thủ Tả Bộ: Lock-on left sidestep (+X) weaving soft Cloud-Hands across centerline."""
    g0 = _guard(hips_y=0.875, hips_yaw=-14.0, torso_yaw=8.0)
    step_l = replace(
        g0, hips_y=0.845, hips_yaw=8.0, hips_roll=-5.0, torso_yaw=10.0, torso_roll=4.5,
        r_hand_pos=(0.04, 1.24, 0.40), r_elbow_pole=(-0.38, 1.00, 0.12), blade_dir=(0.24, 0.36, 0.90),
        l_hand_pos=(0.30, 1.04, 0.34), l_elbow_pole=(0.60, 0.90, 0.02), l_hand_dir=(0.12, 0.14, 0.98),
        l_foot_pos=(0.36, 0.080, 0.16), l_knee_pole=(0.44, 0.44, 0.74), l_foot_yaw=18.0,
        r_foot_pos=(-0.16, 0.100, -0.14), r_knee_pole=(-0.22, 0.46, 0.48), r_foot_yaw=-18.0, r_foot_pitch=-8.0,
    )
    gather = replace(
        g0, hips_y=0.868, hips_yaw=-18.0, hips_roll=3.0, torso_yaw=-6.0, torso_roll=-2.5,
        r_hand_pos=(-0.22, 1.04, 0.34), r_elbow_pole=(-0.56, 0.88, -0.04), blade_dir=(0.08, 0.18, 0.98),
        l_hand_pos=(0.08, 1.24, 0.40), l_elbow_pole=(0.48, 1.00, 0.10), l_hand_dir=(-0.24, 0.34, 0.91),
        l_foot_pos=(0.22, 0.080, 0.16), l_knee_pole=(0.28, 0.46, 0.72), l_foot_yaw=12.0,
        r_foot_pos=(-0.05, 0.140, -0.12), r_knee_pole=(-0.12, 0.48, 0.52), r_foot_yaw=-16.0, r_foot_pitch=-8.0,
    )
    return AnimationClip(
        name="taiji_strafe_left", slot="taiji_walk", duration=0.92, loop=True,
        tags=["taiji", "locomotion", "strafe", "left", "cloud_hands"],
        description="Cloud-Hands lock-on left sidestep (Vân Thủ Tả Bộ).",
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.32, step_l, "flow"),
            Keyframe(0.64, gather, "flow"),
            Keyframe(0.92, g0, "flow"),
        ],
    )


def make_taiji_strafe_right() -> AnimationClip:
    """5. Vân Thủ Hữu Bộ: Lock-on right sidestep (-X) weaving soft Cloud-Hands across centerline."""
    g0 = _guard(hips_y=0.875, hips_yaw=-14.0, torso_yaw=8.0)
    step_r = replace(
        g0, hips_y=0.845, hips_yaw=-26.0, hips_roll=5.0, torso_yaw=-8.0, torso_roll=-4.5,
        r_hand_pos=(-0.30, 1.04, 0.34), r_elbow_pole=(-0.60, 0.90, -0.04), blade_dir=(-0.12, 0.14, 0.98),
        l_hand_pos=(-0.04, 1.24, 0.40), l_elbow_pole=(0.38, 1.00, 0.12), l_hand_dir=(-0.24, 0.36, 0.90),
        l_foot_pos=(0.16, 0.100, 0.14), l_knee_pole=(0.22, 0.46, 0.70), l_foot_yaw=12.0, l_foot_pitch=-8.0,
        r_foot_pos=(-0.36, 0.080, -0.14), r_knee_pole=(-0.44, 0.44, 0.48), r_foot_yaw=-28.0,
    )
    gather = replace(
        g0, hips_y=0.868, hips_yaw=-4.0, hips_roll=-3.0, torso_yaw=10.0, torso_roll=2.5,
        r_hand_pos=(-0.08, 1.24, 0.40), r_elbow_pole=(-0.48, 1.00, 0.10), blade_dir=(0.24, 0.34, 0.91),
        l_hand_pos=(0.22, 1.04, 0.34), l_elbow_pole=(0.56, 0.88, -0.02), l_hand_dir=(-0.08, 0.18, 0.98),
        l_foot_pos=(0.05, 0.140, 0.14), l_knee_pole=(0.12, 0.48, 0.72), l_foot_yaw=10.0, l_foot_pitch=-8.0,
        r_foot_pos=(-0.22, 0.080, -0.16), r_knee_pole=(-0.30, 0.46, 0.46), r_foot_yaw=-22.0,
    )
    return AnimationClip(
        name="taiji_strafe_right", slot="taiji_walk", duration=0.92, loop=True,
        tags=["taiji", "locomotion", "strafe", "right", "cloud_hands"],
        description="Cloud-Hands lock-on right sidestep (Vân Thủ Hữu Bộ).",
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.32, step_r, "flow"),
            Keyframe(0.64, gather, "flow"),
            Keyframe(0.92, g0, "flow"),
        ],
    )


# ==============================================================================
# 2. EIGHT GATES OF TAI CHI — BÁT PHÁP (8 CLIPS: BẰNG, LÝ, TẾ, ÁN, THÁI, LIỆT, TRỬU, KHÁO)
# ==============================================================================

def make_taiji_peng_ward_off() -> AnimationClip:
    """6. Bằng Kình (Peng): Buoyant rising springy ward-off lifting incoming force along a horizontal forearm arc."""
    g0 = _guard()
    hold_ball = _guard(
        hips_y=0.845, hips_yaw=-34.0, hips_pitch=6.0, hips_roll=3.5,
        torso_yaw=-10.0, torso_pitch=5.0, torso_roll=-3.0,
        r_hand_pos=(-0.16, 1.24, 0.32), r_elbow_pole=(-0.54, 1.04, 0.02), blade_dir=(0.48, 0.10, 0.87),
        l_hand_pos=(0.06, 0.96, 0.32), l_elbow_pole=(0.46, 0.86, 0.02), l_hand_dir=(-0.44, 0.24, 0.86),
        l_foot_pos=(0.16, 0.080, 0.22), r_foot_pos=(-0.20, 0.080, -0.22),
    )
    peng_rise = _guard(
        hips_y=0.885, hips_yaw=12.0, hips_pitch=8.0, hips_roll=-3.0,
        torso_yaw=14.0, torso_pitch=6.0, torso_roll=2.5,
        r_shoulder_lift=2.0, r_shoulder_fwd=18.0, l_shoulder_lift=-4.0, l_shoulder_fwd=10.0,
        # Right forearm rises buoyantly across chest in a springy convex Peng shield!
        r_hand_pos=(0.06, 1.26, 0.48), r_elbow_pole=(-0.42, 1.10, 0.24),
        blade_dir=(0.62, 0.28, 0.73), blade_edge=(0.0, 0.93, -0.36),
        # Left palm settles softly beneath right elbow (balancing Yin root)
        l_hand_pos=(0.24, 1.00, 0.28), l_elbow_pole=(0.56, 0.88, -0.06), l_hand_dir=(-0.24, -0.06, 0.97),
        l_foot_pos=(0.16, 0.080, 0.30), r_foot_pos=(-0.20, 0.080, -0.24),
    )
    peng_float = replace(
        peng_rise, hips_y=0.900, hips_yaw=8.0,
        r_hand_pos=(0.04, 1.30, 0.49), blade_dir=(0.54, 0.36, 0.76),
        l_hand_pos=(0.22, 1.03, 0.30),
    )
    return AnimationClip(
        name="taiji_peng_ward_off", slot="taiji_parry", duration=0.80, loop=False,
        tags=["taiji", "eight_gates", "peng", "ward_off", "parry"],
        description="Buoyant upward-expanding Ward-Off energy (Bằng Kình).",
        hit_window=(0.24, 0.48),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.20, hold_ball, "flow"),
            Keyframe(0.42, peng_rise, "snap"),
            Keyframe(0.58, peng_float, "flow"),
            Keyframe(0.80, g0, "flow"),
        ],
    )


def make_taiji_lu_rollback() -> AnimationClip:
    """7. Lý Kình (Lu): Yielding weight-back shift drawing opponent's force past right hip with spiraling waist."""
    g0 = _guard()
    reach_stick = _guard(
        hips_y=0.875, hips_yaw=14.0, hips_pitch=8.0, hips_roll=-2.5,
        torso_yaw=12.0, torso_pitch=7.0, torso_roll=2.0,
        r_hand_pos=(0.02, 1.24, 0.50), r_elbow_pole=(-0.44, 1.04, 0.20), blade_dir=(0.26, 0.24, 0.94),
        l_hand_pos=(0.20, 1.26, 0.48), l_elbow_pole=(0.54, 1.04, 0.14), l_hand_dir=(-0.26, 0.24, 0.94),
        l_foot_pos=(0.16, 0.080, 0.28), r_foot_pos=(-0.20, 0.080, -0.22),
    )
    rollback = _guard(
        hips_y=0.825, hips_yaw=-44.0, hips_pitch=6.0, hips_roll=6.5,
        torso_yaw=-20.0, torso_pitch=5.0, torso_roll=-5.5, head_roll=2.0,
        r_shoulder_lift=-4.0, r_shoulder_fwd=-10.0, l_shoulder_lift=2.0, l_shoulder_fwd=14.0,
        # Weight sits deep onto back right leg; twin hands sweep smoothly down-right-back
        r_hand_pos=(-0.38, 1.02, 0.04), r_elbow_pole=(-0.64, 0.90, -0.32),
        blade_dir=(-0.56, 0.14, 0.82), blade_edge=(0.0, 0.98, -0.17),
        l_hand_pos=(-0.06, 1.16, 0.28), l_elbow_pole=(0.36, 0.96, 0.08), l_hand_dir=(-0.58, 0.16, 0.80),
        l_foot_pos=(0.16, 0.095, 0.24), l_foot_pitch=10.0,
        r_foot_pos=(-0.22, 0.080, -0.26), r_knee_pole=(-0.32, 0.42, 0.34),
    )
    rollback_drift = replace(
        rollback, hips_y=0.842, hips_yaw=-46.0, hips_roll=4.5,
        r_hand_pos=(-0.40, 1.04, 0.00), l_hand_pos=(-0.08, 1.18, 0.25),
    )
    return AnimationClip(
        name="taiji_lu_rollback", slot="taiji_parry", duration=0.85, loop=False,
        tags=["taiji", "eight_gates", "lu", "rollback", "parry"],
        description="Yielding spiral Rollback redirecting incoming force past hip (Lý Kình).",
        hit_window=(0.24, 0.54),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.18, reach_stick, "flow"),
            Keyframe(0.44, rollback, "flow"),
            Keyframe(0.62, rollback_drift, "flow"),
            Keyframe(0.85, g0, "flow"),
        ],
    )


def make_taiji_ji_press() -> AnimationClip:
    """8. Tế Kình (Ji): Concentrated forward Press — left palm bracing right wrist driving unified kinetic beam."""
    g0 = _guard()
    gather_wrist = _guard(
        hips_y=0.835, hips_yaw=-24.0, hips_pitch=6.0, hips_roll=3.0,
        torso_yaw=-6.0, torso_pitch=5.0, torso_roll=-2.5,
        r_hand_pos=(-0.08, 1.14, 0.28), r_elbow_pole=(-0.50, 0.98, 0.04), blade_dir=(0.56, 0.20, 0.80),
        l_hand_pos=(0.06, 1.14, 0.26), l_elbow_pole=(0.48, 0.98, 0.02), l_hand_dir=(-0.56, 0.20, 0.80),
        l_foot_pos=(0.16, 0.080, 0.22), r_foot_pos=(-0.20, 0.080, -0.24),
    )
    press_burst = _guard(
        hips_y=0.820, hips_yaw=14.0, hips_pitch=14.0, hips_roll=-2.5,
        torso_yaw=10.0, torso_pitch=12.0, torso_roll=2.0,
        r_shoulder_lift=2.0, r_shoulder_fwd=24.0, l_shoulder_lift=2.0, l_shoulder_fwd=22.0,
        # Right forearm horizontal forward; left palm presses directly behind right wrist
        r_hand_pos=(0.04, 1.22, 0.54), r_elbow_pole=(-0.44, 1.06, 0.26),
        blade_dir=(0.68, 0.16, 0.72), blade_edge=(0.0, 0.98, -0.22),
        l_hand_pos=(0.08, 1.20, 0.46), l_elbow_pole=(0.48, 1.02, 0.18), l_hand_dir=(-0.24, 0.34, 0.91),
        l_foot_pos=(0.16, 0.080, 0.34), l_knee_pole=(0.20, 0.44, 0.92),
        r_foot_pos=(-0.22, 0.080, -0.28),
    )
    press_settle = replace(
        press_burst, hips_y=0.840, hips_pitch=11.0, torso_pitch=9.0,
        r_hand_pos=(0.02, 1.24, 0.51), l_hand_pos=(0.10, 1.22, 0.44),
    )
    return AnimationClip(
        name="taiji_ji_press", slot="taiji_attack", duration=0.78, loop=False,
        tags=["taiji", "eight_gates", "ji", "press", "combo1"],
        description="Unified forward two-hand Press bursting from bow stance (Tế Kình).",
        hit_window=(0.24, 0.44),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.19, gather_wrist, "flow"),
            Keyframe(0.36, press_burst, "snap"),
            Keyframe(0.54, press_settle, "flow"),
            Keyframe(0.78, g0, "flow"),
        ],
    )


def make_taiji_an_push() -> AnimationClip:
    """9. Án Kình (An): Classic Taiji ocean-wave sink-and-surge twin-palm Push."""
    g0 = _guard()
    wave_back = _guard(
        hips_y=0.835, hips_yaw=-14.0, hips_pitch=3.0, torso_yaw=6.0, torso_pitch=2.0,
        r_shoulder_lift=-6.0, r_shoulder_fwd=-4.0, l_shoulder_lift=-6.0, l_shoulder_fwd=-4.0,
        # Sit back, palms trace downward-back along a smooth ocean wave
        r_hand_pos=(-0.20, 1.02, 0.20), r_elbow_pole=(-0.54, 0.88, -0.20),
        blade_dir=(0.10, 0.20, 0.97), blade_edge=(0.0, 0.98, -0.20),
        l_hand_pos=(0.20, 1.02, 0.20), l_elbow_pole=(0.54, 0.88, -0.20),
        l_hand_dir=(-0.10, 0.20, 0.97),
        l_foot_pos=(0.16, 0.095, 0.22), l_foot_pitch=8.0,
        r_foot_pos=(-0.20, 0.080, -0.24),
    )
    wave_surge = _guard(
        hips_y=0.815, hips_yaw=4.0, hips_pitch=15.0, torso_yaw=2.0, torso_pitch=14.0, head_pitch=-3.0,
        r_shoulder_lift=4.0, r_shoulder_fwd=26.0, l_shoulder_lift=4.0, l_shoulder_fwd=26.0,
        # Twin palms rise forward-up like a breaking tide!
        r_hand_pos=(-0.14, 1.22, 0.58), r_elbow_pole=(-0.48, 0.98, 0.20),
        blade_dir=(0.04, 0.56, 0.83), blade_edge=(0.0, 0.83, -0.56),
        l_hand_pos=(0.14, 1.22, 0.58), l_elbow_pole=(0.48, 0.98, 0.20),
        l_hand_dir=(-0.04, 0.56, 0.83), l_hand_up=(0.0, 0.83, -0.56),
        l_foot_pos=(0.16, 0.080, 0.34), l_knee_pole=(0.20, 0.44, 0.92),
        r_foot_pos=(-0.22, 0.080, -0.30),
    )
    wave_bloom = replace(
        wave_surge, hips_y=0.840, hips_pitch=11.0, torso_pitch=10.0,
        r_hand_pos=(-0.20, 1.26, 0.54), l_hand_pos=(0.20, 1.26, 0.54),
    )
    return AnimationClip(
        name="taiji_an_push", slot="taiji_heavy", duration=0.82, loop=False,
        tags=["taiji", "eight_gates", "an", "push", "knockback"],
        description="Ocean-wave sink-and-surge twin-palm Push (Án Kình).",
        hit_window=(0.28, 0.48),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.22, wave_back, "flow"),
            Keyframe(0.40, wave_surge, "snap"),
            Keyframe(0.58, wave_bloom, "flow"),
            Keyframe(0.82, g0, "flow"),
        ],
    )


def make_taiji_cai_pluck() -> AnimationClip:
    """10. Thái Kình (Cai): Sharp sinking downward-diagonal Pluck uprooting opponent's balance."""
    g0 = _guard()
    reach_high = _guard(
        hips_y=0.920, hips_yaw=10.0, hips_pitch=4.0, hips_roll=-2.0,
        torso_yaw=10.0, torso_pitch=3.0, torso_roll=2.0,
        r_shoulder_lift=10.0, r_shoulder_fwd=20.0, l_shoulder_lift=8.0, l_shoulder_fwd=18.0,
        r_hand_pos=(-0.08, 1.44, 0.48), r_elbow_pole=(-0.48, 1.20, 0.20),
        blade_dir=(0.12, 0.36, 0.92), blade_edge=(0.0, 0.93, -0.36),
        l_hand_pos=(0.14, 1.38, 0.50), l_elbow_pole=(0.50, 1.16, 0.18), l_hand_dir=(-0.12, 0.30, 0.95),
    )
    pluck_drop = _guard(
        hips_y=0.775, hips_yaw=-42.0, hips_pitch=16.0, hips_roll=6.5,
        torso_yaw=-18.0, torso_pitch=14.0, torso_roll=-5.5,
        r_shoulder_lift=-8.0, r_shoulder_fwd=-8.0, l_shoulder_lift=-4.0, l_shoulder_fwd=10.0,
        # Sharp rooted drop pulling opponent's wrist/elbow down past right thigh (keep blade_dir.y >= -0.10)
        r_hand_pos=(-0.38, 0.86, 0.08), r_elbow_pole=(-0.62, 0.92, -0.30),
        blade_dir=(-0.42, -0.08, 0.90), blade_edge=(0.0, 0.996, 0.08),
        l_hand_pos=(-0.04, 1.06, 0.34), l_elbow_pole=(0.40, 0.92, 0.08), l_hand_dir=(-0.44, -0.12, 0.89),
        l_foot_pos=(0.16, 0.080, 0.24), r_foot_pos=(-0.22, 0.080, -0.26),
    )
    pluck_settle = replace(
        pluck_drop, hips_y=0.805, hips_yaw=-38.0, hips_pitch=12.0,
        r_hand_pos=(-0.36, 0.90, 0.12), l_hand_pos=(0.02, 1.10, 0.35),
    )
    return AnimationClip(
        name="taiji_cai_pluck", slot="taiji_parry", duration=0.75, loop=False,
        tags=["taiji", "eight_gates", "cai", "pluck", "control"],
        description="Sharp sinking downward-diagonal Pluck breaking opponent balance (Thái Kình).",
        hit_window=(0.20, 0.42),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.18, reach_high, "flow"),
            Keyframe(0.36, pluck_drop, "heavy_drop"),
            Keyframe(0.52, pluck_settle, "flow"),
            Keyframe(0.75, g0, "flow"),
        ],
    )


def make_taiji_lie_split() -> AnimationClip:
    """11. Liệt Kình (Lie): Opposing Yin-Yang rending Split — right sweeps high-out, left anchors low-out."""
    g0 = _guard()
    cross_coil = _guard(
        hips_y=0.825, hips_yaw=26.0, hips_pitch=10.0, hips_roll=-4.0,
        torso_yaw=16.0, torso_pitch=8.0, torso_roll=3.5,
        r_shoulder_fwd=20.0, l_shoulder_fwd=16.0,
        # Arms coil tightly crossed in front of sternum
        r_hand_pos=(0.12, 1.14, 0.34), r_elbow_pole=(-0.34, 0.96, 0.20), blade_dir=(0.48, 0.20, 0.85),
        l_hand_pos=(-0.06, 1.24, 0.38), l_elbow_pole=(0.38, 1.04, 0.20), l_hand_dir=(-0.48, 0.26, 0.84),
    )
    split_burst = _guard(
        hips_y=0.840, hips_yaw=-34.0, hips_pitch=10.0, hips_roll=5.5,
        torso_yaw=-18.0, torso_pitch=8.0, torso_roll=-4.5,
        r_shoulder_lift=10.0, r_shoulder_fwd=-8.0, l_shoulder_lift=-6.0, l_shoulder_fwd=12.0,
        # Explosive diagonal split: right whips high-right, left presses low-left
        r_hand_pos=(-0.48, 1.44, 0.24), r_elbow_pole=(-0.68, 1.16, -0.10),
        blade_dir=(-0.74, 0.44, 0.51), blade_edge=(0.0, 0.76, -0.65),
        l_hand_pos=(0.42, 0.96, 0.28), l_elbow_pole=(0.64, 0.90, -0.06), l_hand_dir=(0.56, -0.26, 0.79),
        l_foot_pos=(0.18, 0.080, 0.28), r_foot_pos=(-0.22, 0.080, -0.26),
    )
    split_drift = replace(
        split_burst, hips_y=0.865, hips_yaw=-36.0,
        r_hand_pos=(-0.49, 1.46, 0.22), l_hand_pos=(0.44, 0.98, 0.26),
    )
    return AnimationClip(
        name="taiji_lie_split", slot="taiji_attack", duration=0.78, loop=False,
        tags=["taiji", "eight_gates", "lie", "split"],
        description="Explosive Yin-Yang diagonal Split rending opponent guard apart (Liệt Kình).",
        hit_window=(0.22, 0.44),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.18, cross_coil, "flow"),
            Keyframe(0.36, split_burst, "snap"),
            Keyframe(0.54, split_drift, "flow"),
            Keyframe(0.78, g0, "flow"),
        ],
    )


def make_taiji_zhou_elbow() -> AnimationClip:
    """12. Trửu Kình (Zhou): Explosive close-range spiraling right elbow strike backed by left palm."""
    g0 = _guard()
    coil = _guard(
        hips_y=0.835, hips_yaw=-40.0, hips_pitch=8.0, hips_roll=5.0,
        torso_yaw=-16.0, torso_pitch=6.0, torso_roll=-4.0,
        r_shoulder_lift=4.0, r_shoulder_fwd=-12.0, l_shoulder_fwd=14.0,
        r_hand_pos=(-0.26, 1.18, 0.08), r_elbow_pole=(-0.62, 1.12, -0.34), blade_dir=(0.24, 0.28, 0.93),
        l_hand_pos=(0.14, 1.24, 0.42), l_elbow_pole=(0.52, 1.02, 0.10), l_hand_dir=(-0.18, 0.22, 0.96),
    )
    elbow_crash = _guard(
        hips_y=0.805, hips_yaw=34.0, hips_pitch=15.0, hips_roll=-6.0,
        torso_yaw=24.0, torso_pitch=14.0, torso_roll=5.0, head_roll=-2.0,
        r_shoulder_lift=6.0, r_shoulder_fwd=28.0, l_shoulder_lift=2.0, l_shoulder_fwd=20.0,
        # Right fist folded near left pec so right elbow point rams straight into +Z!
        r_hand_pos=(0.14, 1.24, 0.28), r_elbow_pole=(-0.18, 1.22, 0.56),
        blade_dir=(0.68, 0.22, 0.70), blade_edge=(0.0, 0.95, -0.30),
        # Left palm reinforces right fist / forearm
        l_hand_pos=(0.20, 1.22, 0.30), l_elbow_pole=(0.54, 1.04, 0.08), l_hand_dir=(-0.48, 0.24, 0.84),
        l_foot_pos=(0.16, 0.080, 0.32), r_foot_pos=(-0.22, 0.110, -0.26), r_foot_pitch=-16.0,
    )
    elbow_drift = replace(
        elbow_crash, hips_y=0.830, hips_yaw=26.0, hips_pitch=11.0,
        r_hand_pos=(0.10, 1.25, 0.30), r_elbow_pole=(-0.24, 1.20, 0.50),
    )
    return AnimationClip(
        name="taiji_zhou_elbow", slot="taiji_attack", duration=0.72, loop=False,
        tags=["taiji", "eight_gates", "zhou", "elbow", "close_range"],
        description="Spiraling close-range Taiji elbow strike (Trửu Kình).",
        hit_window=(0.20, 0.38),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.16, coil, "flow"),
            Keyframe(0.32, elbow_crash, "snap"),
            Keyframe(0.48, elbow_drift, "flow"),
            Keyframe(0.72, g0, "flow"),
        ],
    )


def make_taiji_kao_shoulder() -> AnimationClip:
    """13. Kháo Kình (Kao): Rooted Bajiquan/Taiji iron-mountain shoulder ram driving right shoulder into +Z."""
    g0 = _guard()
    gather = _guard(
        hips_y=0.820, hips_yaw=22.0, hips_pitch=10.0, hips_roll=-4.0,
        torso_yaw=14.0, torso_pitch=8.0, torso_roll=3.5,
        r_hand_pos=(-0.10, 1.12, 0.34), r_elbow_pole=(-0.50, 0.96, 0.06),
        l_hand_pos=(0.18, 1.22, 0.38), l_elbow_pole=(0.54, 1.02, 0.08),
        l_foot_pos=(0.18, 0.080, 0.22), r_foot_pos=(-0.22, 0.080, -0.22),
    )
    kao_ram = _guard(
        hips_y=0.765, hips_yaw=58.0, hips_pitch=14.0, hips_roll=-12.0,
        torso_yaw=26.0, torso_pitch=12.0, torso_roll=8.0, head_yaw=-18.0, head_roll=-4.0,
        r_shoulder_lift=8.0, r_shoulder_fwd=28.0, l_shoulder_lift=-4.0, l_shoulder_fwd=-8.0,
        # Right shoulder leads violently forward; right arm tucked protectively across lower ribs
        r_hand_pos=(0.18, 0.98, 0.28), r_elbow_pole=(-0.18, 0.96, 0.42),
        blade_dir=(0.74, 0.14, 0.66), blade_edge=(0.0, 0.98, -0.21),
        l_hand_pos=(0.38, 1.24, -0.06), l_elbow_pole=(0.62, 1.06, -0.30), l_hand_dir=(0.56, 0.34, 0.76),
        l_foot_pos=(0.16, 0.080, 0.34), l_knee_pole=(0.22, 0.42, 0.92),
        r_foot_pos=(-0.22, 0.080, -0.30), r_knee_pole=(-0.30, 0.40, 0.26),
    )
    kao_settle = replace(
        kao_ram, hips_y=0.795, hips_yaw=48.0, hips_roll=-8.0,
        torso_yaw=20.0, torso_roll=5.0,
    )
    return AnimationClip(
        name="taiji_kao_shoulder", slot="taiji_heavy", duration=0.78, loop=False,
        tags=["taiji", "eight_gates", "kao", "shoulder", "ram"],
        description="Rooted Taiji shoulder-flank ram (Kháo Kình / Thiết Sơn Kháo).",
        hit_window=(0.22, 0.42),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.18, gather, "flow"),
            Keyframe(0.34, kao_ram, "snap"),
            Keyframe(0.54, kao_settle, "flow"),
            Keyframe(0.78, g0, "flow"),
        ],
    )


# ==============================================================================
# 3. CLASSIC TAI CHI FORMS & PALM STRIKES (8 CLIPS)
# ==============================================================================

def make_taiji_part_horse_mane() -> AnimationClip:
    """14. Dã Mã Phân Tông: Hold the Taiji ball at right hip -> sweeping bow-stance diagonal parting-mane strike."""
    g0 = _guard()
    hold_ball = _guard(
        hips_y=0.840, hips_yaw=-36.0, hips_pitch=6.0, hips_roll=4.0,
        torso_yaw=-14.0, torso_pitch=5.0, torso_roll=-3.5,
        # Right palm on top, left palm underneath cradling Taiji orb at right flank
        r_hand_pos=(-0.22, 1.24, 0.24), r_elbow_pole=(-0.58, 1.04, -0.10), blade_dir=(0.44, -0.06, 0.90),
        l_hand_pos=(-0.10, 0.98, 0.26), l_elbow_pole=(0.36, 0.88, 0.04), l_hand_dir=(-0.34, 0.26, 0.90),
        l_foot_pos=(0.16, 0.115, 0.16), l_foot_pitch=8.0,
        r_foot_pos=(-0.20, 0.080, -0.22),
    )
    part_mane = _guard(
        hips_y=0.815, hips_yaw=16.0, hips_pitch=12.0, hips_roll=-4.0,
        torso_yaw=14.0, torso_pitch=10.0, torso_roll=3.5,
        l_shoulder_lift=6.0, l_shoulder_fwd=22.0, r_shoulder_lift=-6.0, r_shoulder_fwd=-6.0,
        # Left forearm sweeps diagonally upward-forward (+Z); right palm settles calmly beside right hip
        l_hand_pos=(0.12, 1.32, 0.54), l_elbow_pole=(0.50, 1.06, 0.18), l_hand_dir=(-0.16, 0.36, 0.92),
        r_hand_pos=(-0.32, 0.92, 0.08), r_elbow_pole=(-0.58, 0.92, -0.28),
        blade_dir=(-0.26, -0.08, 0.96), blade_edge=(0.0, 0.996, 0.08),
        l_foot_pos=(0.16, 0.080, 0.34), l_knee_pole=(0.20, 0.44, 0.92),
        r_foot_pos=(-0.22, 0.080, -0.28),
    )
    mane_drift = replace(
        part_mane, hips_y=0.835, hips_yaw=12.0,
        l_hand_pos=(0.14, 1.35, 0.52), r_hand_pos=(-0.33, 0.94, 0.10),
    )
    return AnimationClip(
        name="taiji_part_horse_mane", slot="taiji_attack", duration=0.88, loop=False,
        tags=["taiji", "form", "part_mane", "combo2"],
        description="Parting the Wild Horse's Mane (Dã Mã Phân Tông) with fluid diagonal opening.",
        hit_window=(0.28, 0.52),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.24, hold_ball, "flow"),
            Keyframe(0.46, part_mane, "snap"),
            Keyframe(0.64, mane_drift, "flow"),
            Keyframe(0.88, g0, "flow"),
        ],
    )


def make_taiji_white_crane_wings() -> AnimationClip:
    """15. Bạch Hạc Lượng Sí: Graceful empty-stance opening — right palm shields brow, left palm anchors hip."""
    g0 = _guard()
    draw_in = _guard(
        hips_y=0.860, hips_yaw=-14.0, hips_pitch=4.0, torso_yaw=6.0, torso_pitch=3.0,
        r_hand_pos=(-0.10, 1.12, 0.34), r_elbow_pole=(-0.48, 0.94, 0.04),
        l_hand_pos=(0.12, 1.18, 0.36), l_elbow_pole=(0.50, 0.98, 0.06),
    )
    crane_spread = _guard(
        hips_y=0.890, hips_yaw=-26.0, hips_pitch=2.0, hips_roll=3.5,
        torso_yaw=14.0, torso_pitch=0.0, torso_roll=-3.0, head_pitch=-2.0,
        r_shoulder_lift=18.0, r_shoulder_fwd=10.0, l_shoulder_lift=-6.0, l_shoulder_fwd=8.0,
        # Right hand rises high above right temple; left palm presses gracefully beside left hip
        r_hand_pos=(-0.22, 1.66, 0.32), r_elbow_pole=(-0.56, 1.38, 0.12),
        blade_dir=(0.46, 0.32, 0.83), blade_edge=(0.0, 0.93, -0.36),
        l_hand_pos=(0.28, 0.92, 0.22), l_elbow_pole=(0.58, 0.92, -0.10), l_hand_dir=(0.18, -0.18, 0.97),
        # Empty stance (Hư Bộ): left toe lightly touches ground forward, weight on right leg
        l_foot_pos=(0.14, 0.100, 0.28), l_knee_pole=(0.18, 0.50, 0.78), l_foot_pitch=-16.0,
        r_foot_pos=(-0.19, 0.080, -0.18), r_knee_pole=(-0.28, 0.46, 0.42),
    )
    crane_poise = replace(
        crane_spread, hips_y=0.905,
        r_hand_pos=(-0.24, 1.69, 0.30), l_hand_pos=(0.30, 0.90, 0.24),
    )
    return AnimationClip(
        name="taiji_white_crane_wings", slot="taiji_parry", duration=0.92, loop=False,
        tags=["taiji", "form", "crane", "parry", "counter"],
        description="White Crane Spreads Its Wings (Bạch Hạc Lượng Sí) in poised empty stance.",
        hit_window=(0.26, 0.56),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.20, draw_in, "flow"),
            Keyframe(0.46, crane_spread, "flow"),
            Keyframe(0.68, crane_poise, "flow"),
            Keyframe(0.92, g0, "flow"),
        ],
    )


def make_taiji_brush_knee_push() -> AnimationClip:
    """16. Lâu Tất Ảo Bộ: Left palm sweeps low brushing past knee while right palm blasts straight past ear."""
    g0 = _guard()
    wind_ear = _guard(
        hips_y=0.855, hips_yaw=-38.0, hips_pitch=6.0, hips_roll=4.5,
        torso_yaw=-16.0, torso_pitch=5.0, torso_roll=-3.5,
        r_shoulder_lift=10.0, r_shoulder_fwd=-8.0, l_shoulder_lift=2.0, l_shoulder_fwd=14.0,
        # Right palm folded cleanly beside right ear; left palm crosses upper chest ready to brush down
        r_hand_pos=(-0.34, 1.44, 0.06), r_elbow_pole=(-0.64, 1.18, -0.22),
        blade_dir=(-0.10, 0.56, 0.82), blade_edge=(0.0, 0.83, -0.56),
        l_hand_pos=(-0.02, 1.24, 0.34), l_elbow_pole=(0.42, 1.02, 0.12), l_hand_dir=(-0.38, 0.18, 0.91),
    )
    brush_push = _guard(
        hips_y=0.805, hips_yaw=24.0, hips_pitch=14.0, hips_roll=-4.5,
        torso_yaw=18.0, torso_pitch=13.0, torso_roll=4.0,
        r_shoulder_lift=4.0, r_shoulder_fwd=26.0, l_shoulder_lift=-6.0, l_shoulder_fwd=4.0,
        # Right palm drives straight forward from ear; left palm sweeps down-outward past left knee
        r_hand_pos=(-0.06, 1.22, 0.60), r_elbow_pole=(-0.44, 1.00, 0.22),
        blade_dir=(0.02, 0.52, 0.85), blade_edge=(0.0, 0.85, -0.52),
        l_hand_pos=(0.34, 0.88, 0.22), l_elbow_pole=(0.60, 0.88, -0.08), l_hand_dir=(0.24, -0.22, 0.95),
        l_foot_pos=(0.16, 0.080, 0.34), l_knee_pole=(0.20, 0.44, 0.92),
        r_foot_pos=(-0.22, 0.080, -0.28),
    )
    brush_drift = replace(
        brush_push, hips_y=0.830, hips_yaw=18.0,
        r_hand_pos=(-0.08, 1.24, 0.56), l_hand_pos=(0.32, 0.92, 0.24),
    )
    return AnimationClip(
        name="taiji_brush_knee_push", slot="taiji_attack", duration=0.82, loop=False,
        tags=["taiji", "form", "brush_knee", "palm", "combo3"],
        description="Brush Knee and Twist Step (Lâu Tất Ảo Bộ) deflecting low while pushing forward.",
        hit_window=(0.24, 0.46),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.20, wind_ear, "flow"),
            Keyframe(0.40, brush_push, "snap"),
            Keyframe(0.58, brush_drift, "flow"),
            Keyframe(0.82, g0, "flow"),
        ],
    )


def make_taiji_single_whip() -> AnimationClip:
    """17. Đơn Tiên: Iconic Single Whip — right crane-beak hook extends back-right, left palm whips forward-left."""
    g0 = _guard()
    hook_gather = _guard(
        hips_y=0.855, hips_yaw=-34.0, hips_pitch=6.0, hips_roll=4.0,
        torso_yaw=-14.0, torso_pitch=5.0, torso_roll=-3.0,
        r_shoulder_lift=8.0, r_shoulder_fwd=-6.0,
        r_hand_pos=(-0.44, 1.34, 0.14), r_elbow_pole=(-0.66, 1.12, -0.16),
        blade_dir=(-0.74, -0.10, 0.66), blade_edge=(0.0, 0.99, 0.15),
        l_hand_pos=(-0.02, 1.20, 0.36), l_elbow_pole=(0.42, 1.00, 0.10), l_hand_dir=(-0.40, 0.24, 0.88),
    )
    whip_open = _guard(
        hips_y=0.815, hips_yaw=20.0, hips_pitch=10.0, hips_roll=-5.0,
        torso_yaw=16.0, torso_pitch=8.0, torso_roll=4.5, head_roll=-1.5,
        r_shoulder_lift=6.0, r_shoulder_fwd=-14.0, l_shoulder_lift=6.0, l_shoulder_fwd=22.0,
        # Wide open crane-beak right arm & whipping forward-left palm!
        r_hand_pos=(-0.50, 1.36, -0.10), r_elbow_pole=(-0.68, 1.16, -0.34),
        blade_dir=(-0.88, -0.06, 0.47), blade_edge=(0.0, 0.99, 0.12),
        l_hand_pos=(0.24, 1.32, 0.56), l_elbow_pole=(0.56, 1.08, 0.18), l_hand_dir=(0.14, 0.48, 0.87),
        l_foot_pos=(0.20, 0.080, 0.34), l_knee_pole=(0.26, 0.44, 0.90), l_foot_yaw=16.0,
        r_foot_pos=(-0.24, 0.080, -0.26), r_foot_yaw=-32.0,
    )
    whip_poise = replace(
        whip_open, hips_y=0.835,
        r_hand_pos=(-0.51, 1.38, -0.12), l_hand_pos=(0.26, 1.34, 0.54),
    )
    return AnimationClip(
        name="taiji_single_whip", slot="taiji_attack", duration=0.90, loop=False,
        tags=["taiji", "form", "single_whip", "whip"],
        description="Iconic Single Whip (Đơn Tiên) with right crane-beak hook and whipping left palm.",
        hit_window=(0.28, 0.52),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.22, hook_gather, "flow"),
            Keyframe(0.46, whip_open, "whip"),
            Keyframe(0.66, whip_poise, "flow"),
            Keyframe(0.90, g0, "flow"),
        ],
    )


def make_taiji_cloud_hands() -> AnimationClip:
    """18. Vân Thủ Liên Hoàn: Hypnotic continuous double-circle Cloud Hands loop driven purely by the waist."""
    c0 = _guard(
        hips_y=0.865, hips_yaw=-26.0, hips_pitch=4.0, hips_roll=3.0,
        torso_yaw=-14.0, torso_pitch=3.0, torso_roll=-2.5,
        r_hand_pos=(-0.26, 1.30, 0.38), r_elbow_pole=(-0.58, 1.06, 0.06), blade_dir=(0.20, 0.44, 0.88),
        l_hand_pos=(-0.02, 1.02, 0.36), l_elbow_pole=(0.42, 0.88, 0.08), l_hand_dir=(-0.34, 0.16, 0.93),
        l_foot_pos=(0.20, 0.080, 0.08), r_foot_pos=(-0.20, 0.080, -0.08),
    )
    c1 = _guard(
        hips_y=0.880, hips_yaw=0.0, hips_pitch=3.5, hips_roll=0.0,
        torso_yaw=0.0, torso_pitch=2.5, torso_roll=0.0,
        r_hand_pos=(-0.04, 1.32, 0.42), r_elbow_pole=(-0.46, 1.06, 0.12), blade_dir=(0.28, 0.46, 0.84),
        l_hand_pos=(0.12, 1.04, 0.38), l_elbow_pole=(0.50, 0.88, 0.08), l_hand_dir=(-0.20, 0.20, 0.96),
        l_foot_pos=(0.20, 0.080, 0.08), r_foot_pos=(-0.20, 0.080, -0.08),
    )
    c2 = _guard(
        hips_y=0.865, hips_yaw=26.0, hips_pitch=4.0, hips_roll=-3.0,
        torso_yaw=14.0, torso_pitch=3.0, torso_roll=2.5,
        r_hand_pos=(0.02, 1.02, 0.36), r_elbow_pole=(-0.42, 0.88, 0.08), blade_dir=(0.34, 0.16, 0.93),
        l_hand_pos=(0.26, 1.30, 0.38), l_elbow_pole=(0.58, 1.06, 0.06), l_hand_dir=(-0.20, 0.44, 0.88),
        l_foot_pos=(0.20, 0.080, 0.08), r_foot_pos=(-0.20, 0.080, -0.08),
    )
    c3 = _guard(
        hips_y=0.880, hips_yaw=0.0, hips_pitch=3.5, hips_roll=0.0,
        torso_yaw=0.0, torso_pitch=2.5, torso_roll=0.0,
        r_hand_pos=(-0.12, 1.04, 0.38), r_elbow_pole=(-0.50, 0.88, 0.08), blade_dir=(0.20, 0.20, 0.96),
        l_hand_pos=(0.04, 1.32, 0.42), l_elbow_pole=(0.46, 1.06, 0.12), l_hand_dir=(-0.28, 0.46, 0.84),
        l_foot_pos=(0.20, 0.080, 0.08), r_foot_pos=(-0.20, 0.080, -0.08),
    )
    return AnimationClip(
        name="taiji_cloud_hands", slot="taiji_parry", duration=1.80, loop=True,
        tags=["taiji", "form", "cloud_hands", "parry", "loop"],
        description="Continuous waist-driven Wave Hands Like Clouds (Vân Thủ Liên Hoàn).",
        keyframes=[
            Keyframe(0.00, c0, "flow"),
            Keyframe(0.45, c1, "flow"),
            Keyframe(0.90, c2, "flow"),
            Keyframe(1.35, c3, "flow"),
            Keyframe(1.80, c0, "flow"),
        ],
    )


def make_taiji_high_pat_horse() -> AnimationClip:
    """19. Cao Thám Mã: Rising left-hand pluck-pull paired with a soaring high forward right palm pat."""
    g0 = _guard()
    draw_back = _guard(
        hips_y=0.875, hips_yaw=-28.0, hips_pitch=4.0, hips_roll=3.0,
        torso_yaw=-10.0, torso_pitch=3.0, torso_roll=-2.5,
        r_hand_pos=(-0.32, 1.38, 0.14), r_elbow_pole=(-0.62, 1.14, -0.18), blade_dir=(0.04, 0.44, 0.90),
        l_hand_pos=(0.14, 1.24, 0.46), l_elbow_pole=(0.50, 1.02, 0.14), l_hand_dir=(-0.16, 0.24, 0.96),
    )
    pat_high = _guard(
        hips_y=0.915, hips_yaw=14.0, hips_pitch=4.0, hips_roll=-3.0,
        torso_yaw=14.0, torso_pitch=3.0, torso_roll=2.5, head_pitch=-3.0,
        r_shoulder_lift=12.0, r_shoulder_fwd=24.0, l_shoulder_lift=-4.0, l_shoulder_fwd=-6.0,
        # Right palm glides high forward over left wrist (patting the horse's neck)
        r_hand_pos=(-0.06, 1.46, 0.56), r_elbow_pole=(-0.44, 1.18, 0.22),
        blade_dir=(0.06, 0.34, 0.94), blade_edge=(0.0, 0.94, -0.34),
        # Left palm draws back to waist palm-up
        l_hand_pos=(0.22, 1.04, 0.20), l_elbow_pole=(0.54, 0.92, -0.16), l_hand_dir=(-0.12, 0.18, 0.98),
        l_foot_pos=(0.14, 0.095, 0.28), l_foot_pitch=-12.0,
        r_foot_pos=(-0.19, 0.080, -0.18),
    )
    pat_drift = replace(
        pat_high, hips_y=0.925,
        r_hand_pos=(-0.08, 1.48, 0.54), l_hand_pos=(0.23, 1.06, 0.22),
    )
    return AnimationClip(
        name="taiji_high_pat_horse", slot="taiji_attack", duration=0.80, loop=False,
        tags=["taiji", "form", "high_pat", "palm"],
        description="High Pat on Horse (Cao Thám Mã) with left draw-pull and soaring high right palm.",
        hit_window=(0.24, 0.46),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.20, draw_back, "flow"),
            Keyframe(0.40, pat_high, "snap"),
            Keyframe(0.58, pat_drift, "flow"),
            Keyframe(0.80, g0, "flow"),
        ],
    )


def make_taiji_double_peaks_ears() -> AnimationClip:
    """20. Song Phong Quán Nhĩ: Wide circular twin-fist sweep converging powerfully at head level."""
    g0 = _guard()
    wide_scoop = _guard(
        hips_y=0.825, hips_yaw=-4.0, hips_pitch=10.0, torso_yaw=2.0, torso_pitch=8.0,
        r_shoulder_lift=-4.0, r_shoulder_fwd=-6.0, l_shoulder_lift=-4.0, l_shoulder_fwd=-6.0,
        # Both fists sweep wide & low outward along twin arcs
        r_hand_pos=(-0.48, 1.02, 0.18), r_elbow_pole=(-0.66, 0.94, -0.18),
        blade_dir=(-0.38, 0.16, 0.91), blade_edge=(0.0, 0.98, -0.17),
        l_hand_pos=(0.48, 1.02, 0.18), l_elbow_pole=(0.66, 0.94, -0.18),
        l_hand_dir=(0.38, 0.16, 0.91),
        l_foot_pos=(0.18, 0.080, 0.24), r_foot_pos=(-0.20, 0.080, -0.22),
    )
    converge_temples = _guard(
        hips_y=0.865, hips_yaw=4.0, hips_pitch=10.0, torso_yaw=0.0, torso_pitch=9.0,
        r_shoulder_lift=10.0, r_shoulder_fwd=22.0, l_shoulder_lift=10.0, l_shoulder_fwd=22.0,
        # Twin fists arc inward-upward striking opponent's temples simultaneously!
        r_hand_pos=(-0.14, 1.46, 0.50), r_elbow_pole=(-0.54, 1.24, 0.22),
        blade_dir=(0.56, 0.36, 0.75), blade_edge=(0.0, 0.90, -0.44),
        l_hand_pos=(0.14, 1.46, 0.50), l_elbow_pole=(0.54, 1.24, 0.22),
        l_hand_dir=(-0.56, 0.36, 0.75),
        l_foot_pos=(0.16, 0.080, 0.34), l_knee_pole=(0.20, 0.46, 0.92),
        r_foot_pos=(-0.22, 0.080, -0.28),
    )
    converge_drift = replace(
        converge_temples, hips_y=0.880,
        r_hand_pos=(-0.16, 1.48, 0.48), l_hand_pos=(0.16, 1.48, 0.48),
    )
    return AnimationClip(
        name="taiji_double_peaks_ears", slot="taiji_heavy", duration=0.86, loop=False,
        tags=["taiji", "form", "twin_fist", "stun"],
        description="Strike Opponent's Ears with Both Fists (Song Phong Quán Nhĩ) via wide converging arcs.",
        hit_window=(0.28, 0.52),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.22, wide_scoop, "flow"),
            Keyframe(0.44, converge_temples, "snap"),
            Keyframe(0.62, converge_drift, "flow"),
            Keyframe(0.86, g0, "flow"),
        ],
    )


def make_taiji_deflect_parry_punch() -> AnimationClip:
    """21. Ban Lan Chùy: 3-beat Taiji classic — spiral deflect (Ban) -> left intercept (Lan) -> sinking punch (Chùy)."""
    g0 = _guard()
    ban_deflect = _guard(
        hips_y=0.860, hips_yaw=14.0, hips_pitch=6.0, hips_roll=-3.0,
        torso_yaw=12.0, torso_pitch=5.0, torso_roll=2.5,
        # Right fist deflects downward across front left
        r_hand_pos=(0.08, 1.12, 0.40), r_elbow_pole=(-0.38, 0.94, 0.18), blade_dir=(0.44, 0.14, 0.89),
        l_hand_pos=(0.30, 1.24, 0.24), l_elbow_pole=(0.60, 1.04, -0.06), l_hand_dir=(0.10, 0.28, 0.95),
    )
    lan_parry = _guard(
        hips_y=0.840, hips_yaw=-36.0, hips_pitch=8.0, hips_roll=4.5,
        torso_yaw=-14.0, torso_pitch=7.0, torso_roll=-3.5,
        # Right fist chambers cleanly at right hip while left palm intercepts forward!
        r_hand_pos=(-0.28, 0.98, 0.10), r_elbow_pole=(-0.58, 0.90, -0.34), blade_dir=(0.04, 0.08, 0.996),
        l_hand_pos=(0.08, 1.24, 0.50), l_elbow_pole=(0.48, 1.02, 0.16), l_hand_dir=(-0.18, 0.28, 0.94),
        l_foot_pos=(0.16, 0.080, 0.26), r_foot_pos=(-0.22, 0.080, -0.24),
    )
    chui_punch = _guard(
        hips_y=0.810, hips_yaw=24.0, hips_pitch=14.0, hips_roll=-5.0,
        torso_yaw=18.0, torso_pitch=12.0, torso_roll=4.5,
        r_shoulder_lift=2.0, r_shoulder_fwd=28.0, l_shoulder_lift=0.0, l_shoulder_fwd=8.0,
        # Right vertical fist blasts straight into opponent's solar plexus past left forearm!
        r_hand_pos=(-0.04, 1.14, 0.62), r_elbow_pole=(-0.42, 0.96, 0.22),
        blade_dir=(0.02, 0.04, 0.999), blade_edge=(0.0, 0.999, -0.04),
        l_hand_pos=(0.24, 1.14, 0.28), l_elbow_pole=(0.56, 0.98, -0.06), l_hand_dir=(-0.26, 0.14, 0.95),
        l_foot_pos=(0.16, 0.080, 0.34), l_knee_pole=(0.20, 0.44, 0.92),
        r_foot_pos=(-0.22, 0.095, -0.28), r_foot_pitch=-14.0,
    )
    chui_drift = replace(
        chui_punch, hips_y=0.832, hips_yaw=18.0,
        r_hand_pos=(-0.06, 1.15, 0.58),
    )
    return AnimationClip(
        name="taiji_deflect_parry_punch", slot="taiji_attack", duration=0.95, loop=False,
        tags=["taiji", "form", "parry", "punch", "counter", "combo4"],
        description="Deflect, Parry and Punch (Ban Lan Chùy) 3-beat Taiji counter sequence.",
        hit_window=(0.44, 0.66),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.18, ban_deflect, "flow"),
            Keyframe(0.36, lan_parry, "flow"),
            Keyframe(0.54, chui_punch, "snap"),
            Keyframe(0.70, chui_drift, "flow"),
            Keyframe(0.95, g0, "flow"),
        ],
    )


# ==============================================================================
# 4. LOW FORMS, KICKS, 360 SPIN & FAJIN FINISHERS (5 CLIPS)
# ==============================================================================

def make_taiji_snake_creeps_down() -> AnimationClip:
    """22. Hạ Thức (Snake Creeps Down): Deep agile low Pu-Bu crouch evading high attacks while snaking left palm forward."""
    g0 = _guard()
    drop_hook = _guard(
        hips_y=0.720, hips_yaw=-26.0, hips_pitch=14.0, hips_roll=6.0,
        torso_yaw=-10.0, torso_pitch=12.0, torso_roll=-4.5,
        r_shoulder_lift=10.0, r_shoulder_fwd=-10.0,
        # Right crane-beak hook stays high-back (authentic Yang Snake Creeps Down & keeps virtual tipMinY > 1.1m!)
        r_hand_pos=(-0.44, 1.24, -0.10), r_elbow_pole=(-0.64, 1.06, -0.34),
        blade_dir=(-0.74, 0.22, 0.64), blade_edge=(0.0, 0.95, -0.32),
        l_hand_pos=(0.14, 0.88, 0.30), l_elbow_pole=(0.50, 0.84, 0.02), l_hand_dir=(-0.10, -0.26, 0.96),
        l_foot_pos=(0.24, 0.080, 0.34), l_knee_pole=(0.28, 0.42, 0.80), l_foot_yaw=16.0,
        r_foot_pos=(-0.22, 0.080, -0.24), r_knee_pole=(-0.34, 0.38, 0.28), r_foot_yaw=-34.0,
    )
    pu_bu_low = _guard(
        hips_y=0.580, hips_yaw=-20.0, hips_pitch=22.0, hips_roll=10.0,
        torso_yaw=-6.0, torso_pitch=20.0, torso_roll=-8.0, head_pitch=-8.0,
        r_shoulder_lift=14.0, r_shoulder_fwd=-12.0, l_shoulder_lift=-10.0, l_shoulder_fwd=22.0,
        r_hand_pos=(-0.46, 1.12, -0.16), r_elbow_pole=(-0.64, 0.96, -0.40),
        blade_dir=(-0.78, 0.24, 0.58), blade_edge=(0.0, 0.92, -0.38),
        # Left palm snakes smoothly forward along inside of extended left leg!
        l_hand_pos=(0.24, 0.48, 0.44), l_elbow_pole=(0.52, 0.64, 0.14), l_hand_dir=(0.06, -0.14, 0.99),
        l_foot_pos=(0.28, 0.080, 0.42), l_knee_pole=(0.30, 0.34, 0.72), l_foot_yaw=18.0,
        r_foot_pos=(-0.22, 0.080, -0.26), r_knee_pole=(-0.36, 0.34, 0.22), r_foot_yaw=-36.0,
    )
    rise_glide = _guard(
        hips_y=0.790, hips_yaw=8.0, hips_pitch=12.0, hips_roll=-3.0,
        torso_yaw=8.0, torso_pitch=10.0, torso_roll=2.5,
        r_hand_pos=(-0.34, 1.12, 0.14), blade_dir=(-0.20, 0.30, 0.93),
        l_hand_pos=(0.16, 1.14, 0.52), l_elbow_pole=(0.50, 0.96, 0.18), l_hand_dir=(-0.10, 0.34, 0.93),
        l_foot_pos=(0.18, 0.080, 0.32), r_foot_pos=(-0.20, 0.080, -0.22),
    )
    return AnimationClip(
        name="taiji_snake_creeps_down", slot="taiji_dodge", duration=1.05, loop=False,
        tags=["taiji", "form", "low", "evade", "pu_bu"],
        description="Snake Creeps Down (Hạ Thức) deep low Pu-Bu evade rising into forward glide.",
        hit_window=(0.56, 0.78),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.22, drop_hook, "flow"),
            Keyframe(0.46, pu_bu_low, "heavy_drop"),
            Keyframe(0.74, rise_glide, "flow"),
            Keyframe(1.05, g0, "flow"),
        ],
    )


def make_taiji_golden_rooster() -> AnimationClip:
    """23. Kim Kê Độc Lập: Poised single-leg Golden Rooster rising knee strike + sky-piercing palm."""
    g0 = _guard()
    sink_prep = _guard(
        hips_y=0.825, hips_yaw=-16.0, hips_pitch=8.0, torso_yaw=6.0, torso_pitch=7.0,
        r_hand_pos=(-0.22, 0.98, 0.28), l_hand_pos=(0.20, 1.10, 0.34),
        l_foot_pos=(0.14, 0.080, 0.10), r_foot_pos=(-0.19, 0.080, -0.18),
    )
    rooster_apex = _guard(
        hips_y=0.965, hips_yaw=10.0, hips_pitch=-3.0, hips_roll=-5.0,
        torso_yaw=6.0, torso_pitch=-4.0, torso_roll=4.5, head_pitch=-4.0,
        r_shoulder_lift=18.0, r_shoulder_fwd=16.0, l_shoulder_lift=-6.0, l_shoulder_fwd=6.0,
        # Right palm pierces skyward aligned directly above soaring right knee!
        r_hand_pos=(-0.14, 1.68, 0.34), r_elbow_pole=(-0.44, 1.32, 0.22),
        blade_dir=(0.06, 0.92, 0.39), blade_edge=(0.0, 0.39, -0.92),
        l_hand_pos=(0.28, 0.96, 0.20), l_elbow_pole=(0.56, 0.94, -0.12), l_hand_dir=(0.12, -0.16, 0.98),
        # Standing tall on left leg, right knee spears high with relaxed dropped toes
        l_foot_pos=(0.14, 0.080, 0.04), l_knee_pole=(0.16, 0.52, 0.62), l_foot_yaw=10.0,
        r_foot_pos=(-0.14, 0.620, 0.22), r_knee_pole=(-0.18, 1.04, 0.64), r_foot_yaw=-4.0, r_foot_pitch=-26.0,
    )
    rooster_poise = replace(
        rooster_apex, hips_y=0.975,
        r_hand_pos=(-0.14, 1.71, 0.33),
        r_foot_pos=(-0.14, 0.640, 0.21), r_knee_pole=(-0.18, 1.06, 0.63),
    )
    return AnimationClip(
        name="taiji_golden_rooster", slot="taiji_kick", duration=0.88, loop=False,
        tags=["taiji", "form", "knee", "balance", "golden_rooster"],
        description="Golden Rooster Stands on One Leg (Kim Kê Độc Lập) rising knee & upward palm.",
        hit_window=(0.24, 0.52),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.18, sink_prep, "flow"),
            Keyframe(0.40, rooster_apex, "snap"),
            Keyframe(0.60, rooster_poise, "flow"),
            Keyframe(0.88, g0, "flow"),
        ],
    )


def make_taiji_heel_kick() -> AnimationClip:
    """24. Đăng Cước (Separate & Heel Kick): Cross-hands embrace unfolding into a poised Taiji heel-thrust kick."""
    g0 = _guard()
    embrace_chamber = _guard(
        hips_y=0.925, hips_yaw=-10.0, hips_pitch=-2.0, hips_roll=-4.0,
        torso_yaw=6.0, torso_pitch=-2.0, torso_roll=3.5,
        # Wrists cross gracefully in front of chest as right knee chambers
        r_hand_pos=(0.04, 1.24, 0.36), r_elbow_pole=(-0.44, 1.02, 0.14), blade_dir=(0.44, 0.42, 0.80),
        l_hand_pos=(-0.04, 1.24, 0.38), l_elbow_pole=(0.44, 1.02, 0.14), l_hand_dir=(-0.44, 0.42, 0.80),
        l_foot_pos=(0.14, 0.080, 0.06), l_knee_pole=(0.18, 0.50, 0.64), l_foot_yaw=14.0,
        r_foot_pos=(-0.16, 0.560, 0.20), r_knee_pole=(-0.22, 0.96, 0.66), r_foot_pitch=-14.0,
    )
    heel_thrust = _guard(
        hips_y=0.915, hips_yaw=12.0, hips_pitch=-12.0, hips_roll=-7.0,
        torso_yaw=-6.0, torso_pitch=-8.0, torso_roll=6.0, head_pitch=6.0,
        r_shoulder_lift=8.0, r_shoulder_fwd=-6.0, l_shoulder_lift=8.0, l_shoulder_fwd=-6.0,
        # Arms spread wide like balanced wings as right heel drives cleanly into +Z
        r_hand_pos=(-0.48, 1.32, 0.14), r_elbow_pole=(-0.68, 1.12, -0.18),
        blade_dir=(-0.74, 0.30, 0.60), blade_edge=(0.0, 0.89, -0.45),
        l_hand_pos=(0.48, 1.32, 0.14), l_elbow_pole=(0.68, 1.12, -0.18), l_hand_dir=(0.74, 0.30, 0.60),
        l_foot_pos=(0.14, 0.080, 0.02), l_knee_pole=(0.18, 0.48, 0.60), l_foot_yaw=18.0,
        r_foot_pos=(-0.06, 1.140, 0.72), r_knee_pole=(-0.12, 1.06, 0.56), r_foot_yaw=0.0, r_foot_pitch=24.0,
    )
    rechamber = replace(
        embrace_chamber, hips_y=0.905,
        r_hand_pos=(-0.24, 1.16, 0.32), l_hand_pos=(0.24, 1.24, 0.36),
        r_foot_pos=(-0.16, 0.460, 0.12), r_knee_pole=(-0.22, 0.86, 0.56),
    )
    return AnimationClip(
        name="taiji_heel_kick", slot="taiji_kick", duration=0.82, loop=False,
        tags=["taiji", "form", "kick", "heel_kick"],
        description="Poised Taiji Separation Heel Kick (Đăng Cước) with spreading wing counterbalance.",
        hit_window=(0.26, 0.48),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.20, embrace_chamber, "flow"),
            Keyframe(0.40, heel_thrust, "snap"),
            Keyframe(0.60, rechamber, "flow"),
            Keyframe(0.82, g0, "flow"),
        ],
    )


def make_taiji_lotus_kick_360() -> AnimationClip:
    """25. Chuyển Thân Bãi Liên: Flowing 360-deg spinning Taiji outer-crescent lotus slap-kick!"""
    def _lk(yaw: float, hy: float, r_pos, l_pos, lf_pos, lf_knee, rf_pos, rf_knee,
            pitch: float = 4.0, hroll: float = 0.0, tyaw: float = 10.0, troll: float = 0.0) -> PoseSpec:
        return PoseSpec(
            hips_y=hy, hips_yaw=yaw, hips_pitch=pitch, hips_roll=hroll,
            torso_yaw=tyaw, torso_pitch=pitch, torso_roll=troll,
            head_yaw=0.0, head_pitch=2.0, head_roll=-troll * 0.4, head_world_space=False,
            l_shoulder_lift=6.0, l_shoulder_fwd=10.0, r_shoulder_lift=6.0, r_shoulder_fwd=10.0,
            r_hand_pos=r_pos, r_elbow_pole=(-0.56, hy + 0.22, -0.06),
            blade_dir=(0.20, 0.26, 0.94), blade_edge=(0.0, 0.96, -0.27),
            l_hand_pos=l_pos, l_elbow_pole=(0.56, hy + 0.22, -0.06),
            l_hand_dir=(-0.20, 0.26, 0.94), l_hand_up=(0.0, 0.96, -0.27),
            l_foot_pos=lf_pos, l_knee_pole=lf_knee, l_foot_yaw=14.0, l_foot_pitch=-16.0 if lf_pos[1] > 0.14 else 0.0,
            r_foot_pos=rf_pos, r_knee_pole=rf_knee, r_foot_yaw=-18.0, r_foot_pitch=-22.0 if rf_pos[1] > 0.14 else 0.0,
            local_body_space=True, feet_in_body_space=True,
        )

    p0 = _lk(-20.0, 0.895, (-0.20, 1.06, 0.32), (0.18, 1.22, 0.42),
             (0.16, 0.08, 0.20), (0.22, 0.46, 0.74), (-0.19, 0.08, -0.18), (-0.29, 0.46, 0.42), pitch=3.0)
    p_coil = _lk(-55.0, 0.835, (-0.34, 1.08, 0.18), (0.26, 1.20, 0.30),
                 (0.16, 0.08, 0.20), (0.22, 0.46, 0.74), (-0.20, 0.08, -0.20), (-0.28, 0.46, 0.40),
                 pitch=10.0, hroll=5.0, tyaw=-14.0, troll=-4.0)
    p_spin1 = _lk(45.0, 0.935, (-0.28, 1.22, 0.34), (0.34, 1.28, 0.26),
                  (0.14, 0.09, 0.08), (0.20, 0.50, 0.66), (-0.22, 0.44, 0.12), (-0.28, 0.82, 0.58),
                  pitch=2.0, hroll=-7.0, tyaw=16.0, troll=5.5)
    # Lotus outer-crescent slap peak: right leg sweeps high-out across front as twin palms slap instep!
    p_slap = _lk(165.0, 0.945, (-0.06, 1.24, 0.54), (0.12, 1.26, 0.50),
                 (0.14, 0.09, 0.04), (0.20, 0.50, 0.62), (-0.10, 1.18, 0.66), (-0.26, 1.14, 0.52),
                 pitch=-6.0, hroll=-11.0, tyaw=18.0, troll=8.5)
    p_spin2 = _lk(270.0, 0.895, (-0.34, 1.18, 0.28), (0.36, 1.24, 0.22),
                  (0.15, 0.08, 0.14), (0.22, 0.48, 0.70), (-0.22, 0.34, -0.06), (-0.28, 0.72, 0.46),
                  pitch=6.0, hroll=-5.0, tyaw=12.0, troll=4.0)
    p_settle = _lk(350.0, 0.865, (-0.24, 1.08, 0.34), (0.22, 1.22, 0.40),
                   (0.16, 0.08, 0.20), (0.22, 0.46, 0.74), (-0.20, 0.08, -0.18), (-0.29, 0.46, 0.42),
                   pitch=5.0, hroll=-1.5, tyaw=10.0, troll=1.0)
    p_end = _lk(340.0, 0.895, (-0.20, 1.06, 0.32), (0.18, 1.22, 0.42),
                (0.16, 0.08, 0.20), (0.22, 0.46, 0.74), (-0.19, 0.08, -0.18), (-0.29, 0.46, 0.42), pitch=3.0)
    return AnimationClip(
        name="taiji_lotus_kick_360", slot="taiji_kick", duration=1.02, loop=False,
        tags=["taiji", "form", "kick", "spin", "lotus", "360"],
        description="360-deg spinning Taiji Lotus Crescent Slap-Kick (Chuyển Thân Bãi Liên).",
        hit_window=(0.34, 0.62),
        keyframes=[
            Keyframe(0.00, p0, "flow"),
            Keyframe(0.14, p_coil, "flow"),
            Keyframe(0.30, p_spin1, "snap"),
            Keyframe(0.48, p_slap, "snap"),
            Keyframe(0.66, p_spin2, "flow"),
            Keyframe(0.84, p_settle, "flow"),
            Keyframe(1.02, p_end, "flow"),
        ],
    )


def make_taiji_cannon_fist_fajin() -> AnimationClip:
    """26. Yểm Thủ Hồng Chùy: Chen-style coiled silk-reeling windup erupting into a thunderous Fajin cannon fist!"""
    g0 = _guard()
    silk_coil = _guard(
        hips_y=0.790, hips_yaw=-46.0, hips_pitch=12.0, hips_roll=6.0,
        torso_yaw=-20.0, torso_pitch=10.0, torso_roll=-5.0,
        r_shoulder_lift=-6.0, r_shoulder_fwd=-14.0, l_shoulder_lift=4.0, l_shoulder_fwd=18.0,
        # Right fist coiled tightly at right Dan-Tian ribs; left palm guards forward centerline
        r_hand_pos=(-0.28, 0.96, 0.04), r_elbow_pole=(-0.58, 0.90, -0.40),
        blade_dir=(0.02, 0.06, 0.998), blade_edge=(0.0, 0.998, -0.06),
        l_hand_pos=(0.08, 1.22, 0.48), l_elbow_pole=(0.48, 1.00, 0.16), l_hand_dir=(-0.12, 0.24, 0.96),
        l_foot_pos=(0.16, 0.080, 0.24), r_foot_pos=(-0.22, 0.080, -0.26),
    )
    fajin_erupt = _guard(
        hips_y=0.765, hips_yaw=32.0, hips_pitch=18.0, hips_roll=-6.5,
        torso_yaw=24.0, torso_pitch=16.0, torso_roll=5.5, head_roll=-2.0, head_pitch=-4.0,
        r_shoulder_lift=4.0, r_shoulder_fwd=28.0, l_shoulder_lift=2.0, l_shoulder_fwd=-14.0,
        # Explosive Chen Fajin cannon fist blasts forward while left elbow snaps back (opposing whip)!
        r_hand_pos=(-0.02, 1.14, 0.63), r_elbow_pole=(-0.38, 0.98, 0.24),
        blade_dir=(0.00, 0.02, 1.00), blade_edge=(0.0, 1.0, 0.0),
        l_hand_pos=(0.34, 1.12, -0.14), l_elbow_pole=(0.62, 1.06, -0.42), l_hand_dir=(0.42, 0.14, 0.90),
        l_foot_pos=(0.16, 0.080, 0.36), l_knee_pole=(0.20, 0.42, 0.94),
        r_foot_pos=(-0.22, 0.080, -0.32), r_knee_pole=(-0.28, 0.38, 0.26),
    )
    fajin_recoil = replace(
        fajin_erupt, hips_y=0.790, hips_yaw=26.0, hips_pitch=14.0,
        r_hand_pos=(-0.06, 1.16, 0.58), l_hand_pos=(0.30, 1.16, -0.06),
    )
    return AnimationClip(
        name="taiji_cannon_fist_fajin", slot="taiji_heavy", duration=0.86, loop=False,
        tags=["taiji", "chen", "fajin", "punch", "finisher"],
        description="Chen-style silk-reeling Cannon Fist Fajin burst (Yểm Thủ Hồng Chùy).",
        hit_window=(0.26, 0.48),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.24, silk_coil, "flow"),
            Keyframe(0.38, fajin_erupt, "snap", tension=0.25),
            Keyframe(0.58, fajin_recoil, "flow"),
            Keyframe(0.86, g0, "flow"),
        ],
    )


# ==============================================================================
# 5. INTERNAL QI, DEFENSE, EVADE & CLOSING MEDITATION (4 CLIPS)
# ==============================================================================

def make_taiji_yin_yang_domain() -> AnimationClip:
    """27. Thái Cực Lưỡng Nghi Đồ: Trace the great Yin-Yang circle in space, sink to earth, and unleash Qi wave."""
    g0 = _guard()
    circle_r = _guard(
        hips_y=0.910, hips_yaw=-24.0, hips_pitch=2.0, hips_roll=3.0,
        torso_yaw=-12.0, torso_pitch=0.0, torso_roll=-2.5,
        # Right palm ascends outer-right arc; left palm descends lower-left arc
        r_hand_pos=(-0.38, 1.50, 0.34), r_elbow_pole=(-0.64, 1.24, 0.06), blade_dir=(0.28, 0.54, 0.79),
        l_hand_pos=(0.30, 1.00, 0.36), l_elbow_pole=(0.60, 0.90, 0.04), l_hand_dir=(-0.26, -0.10, 0.96),
    )
    circle_top = _guard(
        hips_y=0.955, hips_yaw=0.0, hips_pitch=-3.0, torso_yaw=0.0, torso_pitch=-5.0, head_pitch=-6.0,
        r_shoulder_lift=18.0, r_shoulder_fwd=14.0, l_shoulder_lift=16.0, l_shoulder_fwd=14.0,
        # Twin palms crown the Taiji circle high overhead
        r_hand_pos=(-0.16, 1.78, 0.34), r_elbow_pole=(-0.54, 1.50, 0.16), blade_dir=(0.14, 0.76, 0.63),
        l_hand_pos=(0.16, 1.74, 0.36), l_elbow_pole=(0.54, 1.46, 0.16), l_hand_dir=(-0.14, 0.76, 0.63),
    )
    sink_core = _guard(
        hips_y=0.775, hips_yaw=0.0, hips_pitch=14.0, torso_yaw=0.0, torso_pitch=12.0, head_pitch=2.0,
        r_shoulder_lift=-8.0, r_shoulder_fwd=10.0, l_shoulder_lift=-8.0, l_shoulder_fwd=10.0,
        # Palms compress Yin-Yang orb sharply down into Dan-Tian
        r_hand_pos=(-0.16, 0.94, 0.34), r_elbow_pole=(-0.52, 0.88, 0.02),
        blade_dir=(0.24, 0.12, 0.96), blade_edge=(0.0, 0.99, -0.12),
        l_hand_pos=(0.16, 0.94, 0.34), l_elbow_pole=(0.52, 0.88, 0.02), l_hand_dir=(-0.24, 0.12, 0.96),
        l_foot_pos=(0.20, 0.080, 0.14), r_foot_pos=(-0.20, 0.080, -0.14),
    )
    unleash_sphere = _guard(
        hips_y=0.865, hips_yaw=0.0, hips_pitch=6.0, torso_yaw=0.0, torso_pitch=4.0, head_pitch=-2.0,
        r_shoulder_lift=6.0, r_shoulder_fwd=14.0, l_shoulder_lift=6.0, l_shoulder_fwd=14.0,
        # Wide radiant outward-forward opening projecting the Yin-Yang Domain!
        r_hand_pos=(-0.48, 1.28, 0.38), r_elbow_pole=(-0.68, 1.06, 0.04),
        blade_dir=(-0.44, 0.38, 0.81), blade_edge=(0.0, 0.90, -0.43),
        l_hand_pos=(0.48, 1.28, 0.38), l_elbow_pole=(0.68, 1.06, 0.04), l_hand_dir=(0.44, 0.38, 0.81),
        l_foot_pos=(0.22, 0.080, 0.16), r_foot_pos=(-0.22, 0.080, -0.16),
    )
    unleash_drift = replace(
        unleash_sphere, hips_y=0.885,
        r_hand_pos=(-0.50, 1.32, 0.35), l_hand_pos=(0.50, 1.32, 0.35),
    )
    return AnimationClip(
        name="taiji_yin_yang_domain", slot="taiji_cast", duration=1.35, loop=False,
        tags=["taiji", "skill", "qi", "domain", "ultimate"],
        description="Trace the Great Yin-Yang Taiji circle and unleash radial Qi domain (Thái Cực Lưỡng Nghi Đồ).",
        hit_window=(0.74, 1.04),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.24, circle_r, "flow"),
            Keyframe(0.46, circle_top, "flow"),
            Keyframe(0.68, sink_core, "heavy_drop"),
            Keyframe(0.88, unleash_sphere, "snap"),
            Keyframe(1.08, unleash_drift, "flow"),
            Keyframe(1.35, g0, "flow"),
        ],
    )


def make_taiji_block_circle() -> AnimationClip:
    """28. Hỗn Nguyên Thủ Thế: Rounded circular Peng shield absorbing impact elastically without rigid stiffness."""
    g0 = _guard()
    shield = _guard(
        hips_y=0.835, hips_yaw=-10.0, hips_pitch=6.0, hips_roll=2.5,
        torso_yaw=6.0, torso_pitch=5.0, torso_roll=-2.0, head_pitch=2.0,
        r_shoulder_lift=6.0, r_shoulder_fwd=16.0, l_shoulder_lift=6.0, l_shoulder_fwd=16.0,
        # Rounded overlapping Taiji forearms forming an elastic dome in front of chest
        r_hand_pos=(0.04, 1.28, 0.42), r_elbow_pole=(-0.48, 1.06, 0.18),
        blade_dir=(0.52, 0.36, 0.77), blade_edge=(0.0, 0.90, -0.43),
        l_hand_pos=(-0.04, 1.24, 0.45), l_elbow_pole=(0.48, 1.02, 0.20), l_hand_dir=(-0.52, 0.36, 0.77),
        l_foot_pos=(0.18, 0.080, 0.20), r_foot_pos=(-0.22, 0.080, -0.22),
    )
    absorb = replace(
        shield, hips_y=0.798, hips_pitch=2.0, torso_pitch=1.0,
        r_hand_pos=(0.03, 1.25, 0.35), l_hand_pos=(-0.03, 1.21, 0.38),
    )
    rebound = replace(
        shield, hips_y=0.855,
        r_hand_pos=(0.02, 1.27, 0.43), l_hand_pos=(-0.02, 1.23, 0.46),
    )
    return AnimationClip(
        name="taiji_block_circle", slot="taiji_block", duration=0.65, loop=False,
        tags=["taiji", "combat", "block", "defense"],
        description="Elastic circular Taiji dome guard (Hỗn Nguyên Thủ Thế).",
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.15, shield, "snap"),
            Keyframe(0.30, absorb, "flow"),
            Keyframe(0.46, rebound, "flow"),
            Keyframe(0.65, g0, "flow"),
        ],
    )


def make_taiji_dodge_spiral() -> AnimationClip:
    """29. Bát Quái Thối Bộ: Smooth circular retreating evade (Repulse Monkey step-back) with yielding palms."""
    g0 = _guard()
    yield_step = _guard(
        hips_y=0.840, hips_yaw=-34.0, hips_pitch=10.0, hips_roll=6.5,
        torso_yaw=-10.0, torso_pitch=8.0, torso_roll=-5.5, head_pitch=-3.0,
        r_hand_pos=(-0.38, 1.26, 0.14), r_elbow_pole=(-0.64, 1.06, -0.18), blade_dir=(-0.44, 0.30, 0.85),
        l_hand_pos=(0.16, 1.24, 0.44), l_elbow_pole=(0.52, 1.02, 0.10), l_hand_dir=(-0.20, 0.32, 0.93),
        l_foot_pos=(0.16, 0.165, 0.22), l_knee_pole=(0.22, 0.52, 0.78), l_foot_pitch=8.0,
        r_foot_pos=(-0.22, 0.080, -0.32), r_knee_pole=(-0.30, 0.42, 0.26),
    )
    glide_settle = _guard(
        hips_y=0.815, hips_yaw=-28.0, hips_pitch=12.0, hips_roll=4.0,
        torso_yaw=4.0, torso_pitch=9.0, torso_roll=-3.0,
        r_hand_pos=(-0.28, 1.22, 0.32), blade_dir=(-0.10, 0.34, 0.94),
        l_hand_pos=(0.24, 1.14, 0.28), l_hand_dir=(-0.14, 0.22, 0.97),
        l_foot_pos=(0.16, 0.080, 0.16), r_foot_pos=(-0.20, 0.080, -0.26),
    )
    return AnimationClip(
        name="taiji_dodge_spiral", slot="taiji_dodge", duration=0.65, loop=False,
        tags=["taiji", "combat", "dodge", "back", "repulse_monkey"],
        description="Yielding circular Repulse-Monkey retreating evade (Bát Quái Thối Bộ).",
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.20, yield_step, "snap"),
            Keyframe(0.42, glide_settle, "flow"),
            Keyframe(0.65, g0, "flow"),
        ],
    )


def make_taiji_closing_meditate() -> AnimationClip:
    """30. Thu Thế Quy Nguyên: Serene Taiji closing form — arms rise gently to shoulder height and press Qi to Dan-Tian."""
    m0 = PoseSpec(
        hips_y=0.905, hips_yaw=0.0, hips_pitch=1.0, torso_yaw=0.0, torso_pitch=1.0,
        head_yaw=0.0, head_pitch=3.0,
        l_shoulder_lift=-5.0, l_shoulder_fwd=8.0, r_shoulder_lift=-5.0, r_shoulder_fwd=8.0,
        r_hand_pos=(-0.18, 0.98, 0.30), r_elbow_pole=(-0.50, 0.92, 0.02),
        blade_dir=(0.14, -0.14, 0.98), blade_edge=(0.0, 0.99, 0.14),
        l_hand_pos=(0.18, 0.98, 0.30), l_elbow_pole=(0.50, 0.92, 0.02),
        l_hand_dir=(-0.14, -0.14, 0.98), l_hand_up=(0.0, 0.99, 0.14),
        l_foot_pos=(0.16, 0.080, 0.00), l_knee_pole=(0.18, 0.48, 0.62), l_foot_yaw=6.0,
        r_foot_pos=(-0.16, 0.080, 0.00), r_knee_pole=(-0.18, 0.48, 0.62), r_foot_yaw=-6.0,
    )
    m_open = replace(
        m0, hips_y=0.922, hips_pitch=0.0, torso_pitch=-0.5, head_pitch=2.0,
        l_shoulder_lift=-1.0, r_shoulder_lift=-1.0,
        r_hand_pos=(-0.34, 1.16, 0.34), r_elbow_pole=(-0.60, 1.02, 0.02),
        blade_dir=(-0.16, 0.24, 0.96), blade_edge=(0.0, 0.97, -0.24),
        l_hand_pos=(0.34, 1.16, 0.34), l_elbow_pole=(0.60, 1.02, 0.02),
        l_hand_dir=(0.16, 0.24, 0.96), l_hand_up=(0.0, 0.97, -0.24),
    )
    m_crown = replace(
        m0, hips_y=0.936, hips_pitch=-0.5, torso_pitch=-2.0, head_pitch=1.0,
        l_shoulder_lift=3.0, r_shoulder_lift=3.0,
        r_hand_pos=(-0.20, 1.34, 0.40), r_elbow_pole=(-0.54, 1.14, 0.08),
        blade_dir=(0.10, 0.36, 0.93), blade_edge=(0.0, 0.93, -0.36),
        l_hand_pos=(0.20, 1.34, 0.40), l_elbow_pole=(0.54, 1.14, 0.08),
        l_hand_dir=(-0.10, 0.36, 0.93), l_hand_up=(0.0, 0.93, -0.36),
    )
    m_press_down = replace(
        m0, hips_y=0.892, hips_pitch=2.0, torso_pitch=1.5, head_pitch=3.5,
        l_shoulder_lift=-6.0, r_shoulder_lift=-6.0,
        r_hand_pos=(-0.18, 1.06, 0.34), r_elbow_pole=(-0.52, 0.94, 0.04),
        blade_dir=(0.12, -0.10, 0.99), blade_edge=(0.0, 0.995, 0.10),
        l_hand_pos=(0.18, 1.06, 0.34), l_elbow_pole=(0.52, 0.94, 0.04),
        l_hand_dir=(-0.12, -0.10, 0.99), l_hand_up=(0.0, 0.995, 0.10),
    )
    return AnimationClip(
        name="taiji_closing_meditate", slot="meditate", duration=2.40, loop=True,
        tags=["taiji", "form", "qi", "meditate", "closing"],
        description="Serene Taiji Closing & Dan-Tian Qi circulation (Thu Thế Quy Nguyên).",
        keyframes=[
            Keyframe(0.00, m0, "flow"),
            Keyframe(0.60, m_open, "flow"),
            Keyframe(1.20, m_crown, "flow"),
            Keyframe(1.80, m_press_down, "flow"),
            Keyframe(2.40, m0, "flow"),
        ],
    )


# ==============================================================================
# MASTER BUILDER (ALL 30 TAI CHI CLIPS)
# ==============================================================================

def build_all_taiji_clips(fps: int = 60) -> List[AnimationClip]:
    builders = [
        # Stances & Locomotion (5)
        make_taiji_idle,
        make_taiji_walk,
        make_taiji_run,
        make_taiji_strafe_left,
        make_taiji_strafe_right,
        # Eight Gates — Bát Pháp (8)
        make_taiji_peng_ward_off,
        make_taiji_lu_rollback,
        make_taiji_ji_press,
        make_taiji_an_push,
        make_taiji_cai_pluck,
        make_taiji_lie_split,
        make_taiji_zhou_elbow,
        make_taiji_kao_shoulder,
        # Classic Tai Chi Forms & Palm Strikes (8)
        make_taiji_part_horse_mane,
        make_taiji_white_crane_wings,
        make_taiji_brush_knee_push,
        make_taiji_single_whip,
        make_taiji_cloud_hands,
        make_taiji_high_pat_horse,
        make_taiji_double_peaks_ears,
        make_taiji_deflect_parry_punch,
        # Low Forms, Kicks, 360 Spin & Fajin Finishers (5)
        make_taiji_snake_creeps_down,
        make_taiji_golden_rooster,
        make_taiji_heel_kick,
        make_taiji_lotus_kick_360,
        make_taiji_cannon_fist_fajin,
        # Internal Qi, Defense, Evade & Closing Meditation (4)
        make_taiji_yin_yang_domain,
        make_taiji_block_circle,
        make_taiji_dodge_spiral,
        make_taiji_closing_meditate,
    ]
    return [sample_clip(fn(), fps=fps) for fn in builders]