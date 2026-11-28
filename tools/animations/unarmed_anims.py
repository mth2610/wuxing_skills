#!/usr/bin/env python3
"""
tools/animations/unarmed_anims.py — 20-Clip Ethereal Wuxia Unarmed Martial Arts Library
(Thái Cực Du Long Chưởng & Khinh Công Cước Pháp)

Built on the exact rig_core.py PoseSpec / AnimationClip pipeline with:
  - True C2 Catmull-Rom continuous flow (moving Zanshin holds, zero dead freezes)
  - Full-body kinetic wave: footwork -> hips_y/yaw/roll -> spine counter-tilt -> shoulder -> palm/kick
  - Clean 3D breakdown arcs for punches, twin-palm waves, high kicks, ground sweeps & aerial 360 spins
  - Right wrist oriented via blade_dir/blade_edge (palm/fist forward axis) and left wrist via l_hand_dir/l_hand_up
"""

from __future__ import annotations

from dataclasses import replace
from typing import List

from .rig_core import AnimationClip, Keyframe, PoseSpec, sample_clip


# ==============================================================================
# CANONICAL WUXIA UNARMED GUARD POSE (Hạc Lập Thanh Phong)
# ==============================================================================

def _guard(**overrides) -> PoseSpec:
    """Graceful Wuxia open-palm martial guard: left palm leading, right palm guarding Dan-Tian / ribs."""
    base = PoseSpec(
        hips_y=0.905,
        hips_yaw=-24.0,
        hips_pitch=4.0,
        hips_roll=0.0,
        torso_yaw=14.0,
        torso_pitch=3.5,
        torso_roll=-1.5,
        head_yaw=0.0,
        head_pitch=1.0,
        head_roll=0.0,
        l_shoulder_lift=-1.0,
        l_shoulder_fwd=12.0,
        r_shoulder_lift=-4.0,
        r_shoulder_fwd=8.0,
        # Right hand poised near chest-right (ready to launch palm/fist)
        r_hand_pos=(-0.22, 1.10, 0.28),
        r_elbow_pole=(-0.58, 0.88, -0.10),
        blade_dir=(0.14, 0.34, 0.93),
        blade_edge=(0.0, 0.94, -0.34),
        blade_spin_deg=0.0,
        # Left palm leading gracefully forward-left in Wuxia open-hand guard
        l_hand_pos=(0.20, 1.24, 0.40),
        l_elbow_pole=(0.56, 1.00, 0.04),
        l_hand_dir=(-0.20, 0.28, 0.94),
        l_hand_up=(0.12, 0.96, -0.25),
        # Grounded agile martial stance (left foot forward, right coiled behind)
        l_foot_pos=(0.16, 0.080, 0.20),
        l_knee_pole=(0.22, 0.48, 0.74),
        l_foot_yaw=12.0,
        l_foot_pitch=0.0,
        r_foot_pos=(-0.19, 0.080, -0.20),
        r_knee_pole=(-0.30, 0.48, 0.40),
        r_foot_yaw=-26.0,
        r_foot_pitch=0.0,
    )
    return replace(base, **overrides) if overrides else base


# ==============================================================================
# 1. LOCOMOTION (3 CLIPS: IDLE, WALK, RUN)
# ==============================================================================

def make_unarmed_idle() -> AnimationClip:
    """1. Hạc Lập Thanh Phong: Serene breathing Taiji stance with floating counter-swaying palms."""
    g0 = _guard()
    g1 = _guard(
        hips_y=0.920, hips_yaw=-20.5, hips_roll=-1.5,
        torso_yaw=16.5, torso_pitch=2.0, torso_roll=0.5, head_roll=-1.0,
        r_hand_pos=(-0.20, 1.135, 0.30), r_elbow_pole=(-0.56, 0.91, -0.08),
        blade_dir=(0.12, 0.40, 0.91),
        l_hand_pos=(0.22, 1.275, 0.42), l_elbow_pole=(0.59, 1.04, 0.06),
        l_hand_dir=(-0.16, 0.36, 0.92),
    )
    g2 = _guard(
        hips_y=0.892, hips_yaw=-27.5, hips_roll=1.5,
        torso_yaw=11.5, torso_pitch=5.0, torso_roll=-2.5, head_roll=1.0,
        r_hand_pos=(-0.24, 1.070, 0.26), r_elbow_pole=(-0.60, 0.85, -0.12),
        blade_dir=(0.18, 0.28, 0.94),
        l_hand_pos=(0.18, 1.205, 0.38), l_elbow_pole=(0.54, 0.97, 0.02),
        l_hand_dir=(-0.24, 0.22, 0.95),
    )
    g3 = _guard(
        hips_y=0.912, hips_yaw=-22.5, hips_roll=-0.8,
        torso_yaw=15.0, torso_pitch=2.8, torso_roll=-0.5,
        r_hand_pos=(-0.21, 1.115, 0.29),
        l_hand_pos=(0.21, 1.255, 0.41),
    )
    return AnimationClip(
        name="wuxia_unarmed_idle", slot="unarmed_idle", duration=2.00, loop=True,
        tags=["unarmed", "locomotion", "idle", "wuxia"],
        description="Serene Wuxia open-palm guard (Hạc Lập Thanh Phong) with harmonic Taiji breathing.",
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.50, g1, "flow"),
            Keyframe(1.00, g2, "flow"),
            Keyframe(1.50, g3, "flow"),
            Keyframe(2.00, g0, "flow"),
        ],
    )


def make_unarmed_walk() -> AnimationClip:
    """2. Hành Vân Bộ: Light, composed martial stride with soft sweeping open palms."""
    base = dict(hips_pitch=5.0, torso_pitch=4.0, head_yaw=0.0, head_pitch=1.0,
                r_shoulder_lift=-4.0, l_shoulder_lift=-4.0)
    p0 = PoseSpec(
        **base, hips_y=0.900, hips_yaw=7.0, torso_yaw=-6.0, hips_roll=-2.0, torso_roll=2.2, head_roll=-0.5,
        r_shoulder_fwd=10.0, l_shoulder_fwd=-6.0,
        r_hand_pos=(-0.26, 1.02, 0.22), r_elbow_pole=(-0.54, 0.98, -0.14),
        blade_dir=(0.08, 0.08, 0.99), blade_edge=(0.0, 0.99, -0.08),
        l_hand_pos=(0.28, 0.96, -0.10), l_elbow_pole=(0.54, 1.04, -0.34),
        l_hand_dir=(-0.05, -0.20, 0.98), l_hand_up=(0.15, 0.98, 0.0),
        l_foot_pos=(0.13, 0.085, 0.26), l_knee_pole=(0.16, 0.48, 0.74), l_foot_yaw=4.0, l_foot_pitch=10.0,
        r_foot_pos=(-0.13, 0.095, -0.24), r_knee_pole=(-0.16, 0.44, 0.40), r_foot_yaw=-4.0, r_foot_pitch=-16.0,
    )
    p1 = PoseSpec(
        **base, hips_y=0.934, hips_yaw=0.0, torso_yaw=0.0, hips_roll=-2.5, torso_roll=2.5, head_roll=-0.5,
        r_shoulder_fwd=2.0, l_shoulder_fwd=2.0,
        r_hand_pos=(-0.28, 0.99, 0.06), r_elbow_pole=(-0.55, 1.01, -0.26),
        blade_dir=(0.04, -0.06, 0.99), blade_edge=(0.0, 0.99, 0.06),
        l_hand_pos=(0.28, 0.99, 0.06), l_elbow_pole=(0.55, 1.01, -0.26),
        l_hand_dir=(-0.04, -0.06, 0.99), l_hand_up=(0.15, 0.98, 0.0),
        l_foot_pos=(0.13, 0.080, 0.01), l_knee_pole=(0.15, 0.50, 0.62), l_foot_yaw=2.0, l_foot_pitch=0.0,
        r_foot_pos=(-0.13, 0.175, 0.02), r_knee_pole=(-0.16, 0.50, 0.76), r_foot_yaw=-3.0, r_foot_pitch=-5.0,
    )
    p2 = PoseSpec(
        **base, hips_y=0.900, hips_yaw=-7.0, torso_yaw=6.0, hips_roll=2.0, torso_roll=-2.2, head_roll=0.5,
        r_shoulder_fwd=-6.0, l_shoulder_fwd=10.0,
        r_hand_pos=(-0.28, 0.96, -0.10), r_elbow_pole=(-0.54, 1.04, -0.34),
        blade_dir=(0.05, -0.20, 0.98), blade_edge=(0.0, 0.98, 0.20),
        l_hand_pos=(0.26, 1.02, 0.22), l_elbow_pole=(0.54, 0.98, -0.14),
        l_hand_dir=(-0.08, 0.08, 0.99), l_hand_up=(0.15, 0.98, 0.0),
        l_foot_pos=(0.13, 0.095, -0.24), l_knee_pole=(0.16, 0.44, 0.40), l_foot_yaw=4.0, l_foot_pitch=-16.0,
        r_foot_pos=(-0.13, 0.085, 0.26), r_knee_pole=(-0.16, 0.48, 0.74), r_foot_yaw=-4.0, r_foot_pitch=10.0,
    )
    p3 = PoseSpec(
        **base, hips_y=0.934, hips_yaw=0.0, torso_yaw=0.0, hips_roll=2.5, torso_roll=-2.5, head_roll=0.5,
        r_shoulder_fwd=2.0, l_shoulder_fwd=2.0,
        r_hand_pos=(-0.28, 0.99, 0.06), r_elbow_pole=(-0.55, 1.01, -0.26),
        blade_dir=(0.04, -0.06, 0.99), blade_edge=(0.0, 0.99, 0.06),
        l_hand_pos=(0.28, 0.99, 0.06), l_elbow_pole=(0.55, 1.01, -0.26),
        l_hand_dir=(-0.04, -0.06, 0.99), l_hand_up=(0.15, 0.98, 0.0),
        l_foot_pos=(0.13, 0.175, 0.02), l_knee_pole=(0.16, 0.50, 0.76), l_foot_yaw=3.0, l_foot_pitch=-5.0,
        r_foot_pos=(-0.13, 0.080, 0.01), r_knee_pole=(-0.15, 0.50, 0.62), r_foot_yaw=-2.0, r_foot_pitch=0.0,
    )
    return AnimationClip(
        name="wuxia_unarmed_walk", slot="unarmed_walk", duration=1.05, loop=True,
        tags=["unarmed", "locomotion", "walk", "wuxia"],
        description="Composed martial walk (Hành Vân Bộ) with fluid alternating open-palm swing.",
        keyframes=[
            Keyframe(0.00, p0, "flow"), Keyframe(0.2625, p1, "flow"),
            Keyframe(0.525, p2, "flow"), Keyframe(0.7875, p3, "flow"), Keyframe(1.05, p0, "flow"),
        ],
    )


def make_unarmed_run() -> AnimationClip:
    """3. Lăng Ba Tật Bộ: Buoyant Khinh Công sprint with streamlined torso & smooth forearm swing."""
    base = dict(hips_pitch=17.0, torso_pitch=17.0, head_yaw=0.0, head_pitch=-4.0)
    p0 = PoseSpec(
        **base, hips_y=0.835, hips_yaw=12.0, torso_yaw=-14.0, hips_roll=-2.5, torso_roll=3.0, head_roll=-1.0,
        r_shoulder_lift=0.0, r_shoulder_fwd=16.0, l_shoulder_lift=-4.0, l_shoulder_fwd=-14.0,
        r_hand_pos=(-0.24, 1.14, 0.34), r_elbow_pole=(-0.54, 1.00, -0.04),
        blade_dir=(0.14, 0.22, 0.96), blade_edge=(0.0, 0.97, -0.22),
        l_hand_pos=(0.34, 0.96, -0.20), l_elbow_pole=(0.56, 1.10, -0.46),
        l_hand_dir=(-0.08, -0.28, 0.96), l_hand_up=(0.08, 0.96, 0.28),
        l_foot_pos=(0.13, 0.085, 0.35), l_knee_pole=(0.16, 0.48, 0.88), l_foot_yaw=3.0, l_foot_pitch=14.0,
        r_foot_pos=(-0.13, 0.280, -0.35), r_knee_pole=(-0.16, 0.42, 0.22), r_foot_yaw=-4.0, r_foot_pitch=-34.0,
    )
    p1 = PoseSpec(
        **base, hips_y=0.910, hips_yaw=0.0, torso_yaw=0.0, hips_roll=-3.2, torso_roll=3.2, head_roll=-0.5,
        r_shoulder_lift=-2.0, r_shoulder_fwd=2.0, l_shoulder_lift=-2.0, l_shoulder_fwd=2.0,
        r_hand_pos=(-0.30, 1.04, 0.06), r_elbow_pole=(-0.56, 1.04, -0.28),
        blade_dir=(0.08, -0.04, 0.996), blade_edge=(0.0, 0.998, 0.04),
        l_hand_pos=(0.30, 1.04, 0.06), l_elbow_pole=(0.56, 1.04, -0.28),
        l_hand_dir=(-0.08, -0.04, 0.996), l_hand_up=(0.08, 0.996, 0.04),
        l_foot_pos=(0.13, 0.100, -0.08), l_knee_pole=(0.16, 0.46, 0.56), l_foot_yaw=2.0, l_foot_pitch=-12.0,
        r_foot_pos=(-0.13, 0.320, 0.13), r_knee_pole=(-0.16, 0.62, 0.90), r_foot_yaw=-3.0, r_foot_pitch=-10.0,
    )
    p2 = PoseSpec(
        **base, hips_y=0.835, hips_yaw=-12.0, torso_yaw=14.0, hips_roll=2.5, torso_roll=-3.0, head_roll=1.0,
        r_shoulder_lift=-4.0, r_shoulder_fwd=-14.0, l_shoulder_lift=0.0, l_shoulder_fwd=16.0,
        r_hand_pos=(-0.34, 0.96, -0.20), r_elbow_pole=(-0.56, 1.10, -0.46),
        blade_dir=(0.08, -0.28, 0.96), blade_edge=(0.0, 0.96, 0.28),
        l_hand_pos=(0.24, 1.14, 0.34), l_elbow_pole=(0.54, 1.00, -0.04),
        l_hand_dir=(-0.14, 0.22, 0.96), l_hand_up=(0.08, 0.97, -0.22),
        l_foot_pos=(0.13, 0.280, -0.35), l_knee_pole=(0.16, 0.42, 0.22), l_foot_yaw=4.0, l_foot_pitch=-34.0,
        r_foot_pos=(-0.13, 0.085, 0.35), r_knee_pole=(-0.16, 0.48, 0.88), r_foot_yaw=-3.0, r_foot_pitch=14.0,
    )
    p3 = PoseSpec(
        **base, hips_y=0.910, hips_yaw=0.0, torso_yaw=0.0, hips_roll=3.2, torso_roll=-3.2, head_roll=0.5,
        r_shoulder_lift=-2.0, r_shoulder_fwd=2.0, l_shoulder_lift=-2.0, l_shoulder_fwd=2.0,
        r_hand_pos=(-0.30, 1.04, 0.06), r_elbow_pole=(-0.56, 1.04, -0.28),
        blade_dir=(0.08, -0.04, 0.996), blade_edge=(0.0, 0.998, 0.04),
        l_hand_pos=(0.30, 1.04, 0.06), l_elbow_pole=(0.56, 1.04, -0.28),
        l_hand_dir=(-0.08, -0.04, 0.996), l_hand_up=(0.08, 0.996, 0.04),
        l_foot_pos=(0.13, 0.320, 0.13), l_knee_pole=(0.16, 0.62, 0.90), l_foot_yaw=3.0, l_foot_pitch=-10.0,
        r_foot_pos=(-0.13, 0.100, -0.08), r_knee_pole=(-0.16, 0.46, 0.56), r_foot_yaw=-2.0, r_foot_pitch=-12.0,
    )
    return AnimationClip(
        name="wuxia_unarmed_run", slot="unarmed_run", duration=0.72, loop=True,
        tags=["unarmed", "locomotion", "run", "khinh_cong"],
        description="Buoyant Khinh Công martial sprint (Lăng Ba Tật Bộ).",
        keyframes=[
            Keyframe(0.00, p0, "flow"), Keyframe(0.18, p1, "flow"),
            Keyframe(0.36, p2, "flow"), Keyframe(0.54, p3, "flow"), Keyframe(0.72, p0, "flow"),
        ],
    )


# ==============================================================================
# 2. PALM & FIST COMBOS (6 CLIPS)
# ==============================================================================

def make_unarmed_jab_left() -> AnimationClip:
    """4. Tả Chưởng Thám Hải: Lightning lead left palm-jab with crisp shoulder rotation & right-hand chamber."""
    g0 = _guard()
    coil = _guard(
        hips_y=0.875, hips_yaw=-14.0, hips_pitch=6.0, hips_roll=-2.5,
        torso_yaw=6.0, torso_pitch=5.0, torso_roll=2.0,
        l_shoulder_fwd=4.0, r_shoulder_fwd=12.0,
        l_hand_pos=(0.22, 1.18, 0.24), l_elbow_pole=(0.56, 0.96, -0.14), l_hand_dir=(-0.12, 0.16, 0.98),
        r_hand_pos=(-0.18, 1.14, 0.32), r_elbow_pole=(-0.54, 0.92, -0.04),
    )
    strike = _guard(
        hips_y=0.855, hips_yaw=-38.0, hips_pitch=10.0, hips_roll=4.5,
        torso_yaw=-14.0, torso_pitch=9.0, torso_roll=-3.5, head_roll=1.5,
        l_shoulder_lift=4.0, l_shoulder_fwd=26.0, r_shoulder_lift=-4.0, r_shoulder_fwd=-8.0,
        # Full snapping extension straight along +Z
        l_hand_pos=(0.04, 1.24, 0.62), l_elbow_pole=(0.40, 1.06, 0.22),
        l_hand_dir=(-0.04, 0.08, 0.996), l_hand_up=(0.0, 0.99, -0.08),
        # Right hand pulls crisply to right hip chamber (Thu Quyền)
        r_hand_pos=(-0.28, 1.02, 0.10), r_elbow_pole=(-0.58, 0.94, -0.34),
        blade_dir=(0.04, 0.10, 0.994),
        l_foot_pos=(0.15, 0.080, 0.28), r_foot_pos=(-0.20, 0.095, -0.24), r_foot_pitch=-10.0,
    )
    retract_arc = _guard(
        hips_y=0.880, hips_yaw=-30.0, hips_pitch=6.0, hips_roll=2.0,
        torso_yaw=4.0, torso_pitch=5.0, torso_roll=-1.5,
        l_hand_pos=(0.14, 1.26, 0.50), l_elbow_pole=(0.52, 1.04, 0.12),
        r_hand_pos=(-0.24, 1.07, 0.22),
    )
    return AnimationClip(
        name="wuxia_unarmed_jab_left", slot="unarmed_attack", duration=0.58, loop=False,
        tags=["unarmed", "combat", "jab", "palm", "combo1"],
        description="Snappy lead left palm-jab (Tả Chưởng Thám Hải) with right-hip chamber.",
        hit_window=(0.16, 0.30),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.11, coil, "flow"),
            Keyframe(0.24, strike, "snap"),
            Keyframe(0.38, retract_arc, "flow"),
            Keyframe(0.58, g0, "flow"),
        ],
    )


def make_unarmed_cross_right() -> AnimationClip:
    """5. Hữu Quyền Phá Không: Explosive waist-whip right straight punch with crane-wing left counterbalance."""
    g0 = _guard()
    coil = _guard(
        hips_y=0.845, hips_yaw=-44.0, hips_pitch=8.0, hips_roll=5.0,
        torso_yaw=-16.0, torso_pitch=6.0, torso_roll=-4.0,
        r_shoulder_lift=-4.0, r_shoulder_fwd=-14.0, l_shoulder_lift=2.0, l_shoulder_fwd=16.0,
        r_hand_pos=(-0.30, 1.02, 0.04), r_elbow_pole=(-0.56, 0.92, -0.40),
        blade_dir=(0.02, 0.04, 0.999), blade_edge=(0.0, 1.0, 0.0),
        l_hand_pos=(0.10, 1.22, 0.46), l_elbow_pole=(0.48, 1.00, 0.12), l_hand_dir=(-0.10, 0.14, 0.98),
        l_foot_pos=(0.16, 0.080, 0.22), r_foot_pos=(-0.22, 0.095, -0.24), r_foot_pitch=-12.0,
    )
    impact = _guard(
        hips_y=0.820, hips_yaw=28.0, hips_pitch=14.0, hips_roll=-5.5,
        torso_yaw=22.0, torso_pitch=13.0, torso_roll=5.0, head_roll=-2.0,
        r_shoulder_lift=4.0, r_shoulder_fwd=28.0, l_shoulder_lift=6.0, l_shoulder_fwd=-16.0,
        r_hand_pos=(-0.02, 1.18, 0.62), r_elbow_pole=(-0.38, 1.02, 0.22),
        blade_dir=(0.00, 0.02, 1.00), blade_edge=(0.0, 1.0, 0.0),
        # Left palm sweeps backward-left like a crane wing for kinetic whip
        l_hand_pos=(0.44, 1.24, -0.18), l_elbow_pole=(0.64, 1.06, -0.40), l_hand_dir=(0.74, 0.34, -0.58),
        l_foot_pos=(0.15, 0.080, 0.32), r_foot_pos=(-0.22, 0.125, -0.28), r_foot_pitch=-22.0,
    )
    drift = replace(
        impact, hips_y=0.842, hips_yaw=20.0, hips_pitch=11.0, hips_roll=-3.0,
        torso_yaw=16.0, torso_pitch=10.0, torso_roll=2.5,
        r_hand_pos=(-0.06, 1.19, 0.56),
        l_hand_pos=(0.38, 1.26, -0.06),
        r_foot_pos=(-0.21, 0.095, -0.24), r_foot_pitch=-12.0,
    )
    return AnimationClip(
        name="wuxia_unarmed_cross_right", slot="unarmed_attack", duration=0.68, loop=False,
        tags=["unarmed", "combat", "punch", "cross", "combo2"],
        description="Explosive waist-whip right straight strike (Hữu Quyền Phá Không).",
        hit_window=(0.18, 0.34),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.14, coil, "flow"),
            Keyframe(0.28, impact, "snap"),
            Keyframe(0.44, drift, "flow"),
            Keyframe(0.68, g0, "flow"),
        ],
    )


def make_unarmed_palm_push() -> AnimationClip:
    """6. Hàng Long Song Chưởng: Deep circular Qi gather into an earth-shaking twin-palm shockwave push."""
    g0 = _guard()
    gather_circle = _guard(
        hips_y=0.820, hips_yaw=-10.0, hips_pitch=10.0, hips_roll=0.0,
        torso_yaw=2.0, torso_pitch=8.0, torso_roll=0.0,
        r_shoulder_lift=-6.0, r_shoulder_fwd=-8.0, l_shoulder_lift=-6.0, l_shoulder_fwd=-8.0,
        # Both palms coil low near Dan-Tian ribs gathering internal Qi
        r_hand_pos=(-0.22, 0.98, 0.14), r_elbow_pole=(-0.56, 0.92, -0.32),
        blade_dir=(0.18, 0.24, 0.95), blade_edge=(0.0, 0.97, -0.24),
        l_hand_pos=(0.22, 0.98, 0.14), l_elbow_pole=(0.56, 0.92, -0.32),
        l_hand_dir=(-0.18, 0.24, 0.95), l_hand_up=(0.0, 0.97, -0.24),
        l_foot_pos=(0.20, 0.080, 0.22), r_foot_pos=(-0.22, 0.080, -0.22),
    )
    push_burst = _guard(
        hips_y=0.785, hips_yaw=4.0, hips_pitch=18.0, hips_roll=0.0,
        torso_yaw=0.0, torso_pitch=16.0, torso_roll=0.0, head_pitch=-4.0,
        r_shoulder_lift=4.0, r_shoulder_fwd=26.0, l_shoulder_lift=4.0, l_shoulder_fwd=26.0,
        # Twin open palms blast forward with wrists flexed upward (Chưởng Kình)
        r_hand_pos=(-0.14, 1.18, 0.58), r_elbow_pole=(-0.50, 0.98, 0.18),
        blade_dir=(0.04, 0.56, 0.83), blade_edge=(0.0, 0.83, -0.56),
        l_hand_pos=(0.14, 1.18, 0.58), l_elbow_pole=(0.50, 0.98, 0.18),
        l_hand_dir=(-0.04, 0.56, 0.83), l_hand_up=(0.0, 0.83, -0.56),
        l_foot_pos=(0.18, 0.080, 0.34), l_knee_pole=(0.22, 0.44, 0.92),
        r_foot_pos=(-0.22, 0.080, -0.32), r_knee_pole=(-0.28, 0.40, 0.26),
    )
    push_bloom = replace(
        push_burst, hips_y=0.815, hips_pitch=13.0, torso_pitch=11.0,
        # Palms separate slightly outward in a graceful Taiji bloom
        r_hand_pos=(-0.24, 1.22, 0.54), l_hand_pos=(0.24, 1.22, 0.54),
        blade_dir=(-0.20, 0.48, 0.85), l_hand_dir=(0.20, 0.48, 0.85),
    )
    return AnimationClip(
        name="wuxia_unarmed_palm_push", slot="unarmed_heavy", duration=0.78, loop=False,
        tags=["unarmed", "combat", "palm", "knockback", "combo3"],
        description="Deep-stance twin-palm Qi shockwave push (Hàng Long Song Chưởng).",
        hit_window=(0.26, 0.44),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.20, gather_circle, "flow"),
            Keyframe(0.35, push_burst, "snap"),
            Keyframe(0.54, push_bloom, "flow"),
            Keyframe(0.78, g0, "flow"),
        ],
    )


def make_unarmed_uppercut() -> AnimationClip:
    """7. Thăng Long Phá Thiên: Low crouched spring surging upward into a sky-piercing rising dragon uppercut."""
    g0 = _guard()
    crouch = _guard(
        hips_y=0.735, hips_yaw=-42.0, hips_pitch=18.0, hips_roll=6.0,
        torso_yaw=-18.0, torso_pitch=16.0, torso_roll=-5.0, head_pitch=-6.0,
        r_shoulder_lift=-10.0, r_shoulder_fwd=-2.0, l_shoulder_lift=2.0, l_shoulder_fwd=14.0,
        r_hand_pos=(-0.30, 0.74, 0.16), r_elbow_pole=(-0.58, 0.84, -0.18),
        blade_dir=(-0.10, -0.20, 0.97), blade_edge=(0.0, 0.98, 0.20),
        l_hand_pos=(0.16, 1.06, 0.36), l_elbow_pole=(0.52, 0.90, 0.04), l_hand_dir=(-0.14, 0.14, 0.98),
        l_foot_pos=(0.18, 0.080, 0.24), r_foot_pos=(-0.22, 0.080, -0.24),
    )
    scoop_mid = _guard(
        hips_y=0.890, hips_yaw=-4.0, hips_pitch=8.0, hips_roll=0.0,
        torso_yaw=4.0, torso_pitch=4.0, torso_roll=0.0,
        r_shoulder_lift=4.0, r_shoulder_fwd=22.0,
        # Wide upward scooping arc in front of torso
        r_hand_pos=(-0.12, 1.12, 0.48), r_elbow_pole=(-0.46, 0.90, 0.18),
        blade_dir=(0.02, 0.56, 0.83), blade_edge=(0.0, 0.83, -0.56),
        l_hand_pos=(0.34, 1.18, 0.08), l_elbow_pole=(0.62, 1.00, -0.20), l_hand_dir=(0.45, 0.30, 0.84),
        l_foot_pos=(0.16, 0.130, 0.22), l_foot_pitch=-10.0,
        r_foot_pos=(-0.20, 0.120, -0.20), r_foot_pitch=-14.0,
    )
    sky_apex = _guard(
        hips_y=1.100, hips_yaw=26.0, hips_pitch=-5.0, hips_roll=-5.0,
        torso_yaw=16.0, torso_pitch=-11.0, torso_roll=4.5, head_pitch=-10.0,
        r_shoulder_lift=24.0, r_shoulder_fwd=16.0, l_shoulder_lift=6.0, l_shoulder_fwd=-14.0,
        r_hand_pos=(0.04, 1.82, 0.34), r_elbow_pole=(-0.30, 1.48, 0.26),
        blade_dir=(0.06, 0.96, 0.28), blade_edge=(0.0, 0.28, -0.96),
        l_hand_pos=(0.46, 1.32, -0.20), l_elbow_pole=(0.66, 1.12, -0.40), l_hand_dir=(0.74, 0.42, -0.52),
        l_foot_pos=(0.16, 0.260, 0.20), l_knee_pole=(0.22, 0.66, 0.76), l_foot_pitch=-22.0,
        r_foot_pos=(-0.20, 0.200, -0.18), r_knee_pole=(-0.28, 0.60, 0.38), r_foot_pitch=-26.0,
    )
    land_soft = _guard(
        hips_y=0.835, hips_yaw=6.0, hips_pitch=9.0, hips_roll=-2.0,
        torso_yaw=10.0, torso_pitch=7.0, torso_roll=1.5,
        r_hand_pos=(-0.10, 1.30, 0.36), l_hand_pos=(0.28, 1.22, 0.22),
        l_foot_pos=(0.16, 0.080, 0.20), r_foot_pos=(-0.20, 0.080, -0.20),
    )
    return AnimationClip(
        name="wuxia_unarmed_uppercut", slot="unarmed_heavy", duration=0.75, loop=False,
        tags=["unarmed", "combat", "uppercut", "launcher"],
        description="Coiled springing dragon uppercut (Thăng Long Phá Thiên) launching opponent upward.",
        hit_window=(0.22, 0.44),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.16, crouch, "flow"),
            Keyframe(0.28, scoop_mid, "snap"),
            Keyframe(0.42, sky_apex, "flow"),
            Keyframe(0.58, land_soft, "heavy_drop"),
            Keyframe(0.75, g0, "flow"),
        ],
    )


def make_unarmed_backfist_spin() -> AnimationClip:
    """8. Hồi Phong Toàn Chưởng: Silky 360-deg pivot spinning whip-palm / backfist in local_body_space."""
    def _sp(yaw: float, hy: float, r_pos, r_el, l_pos, l_el, l_dir,
            pitch: float = 7.0, hroll: float = 0.0, tyaw: float = 12.0, troll: float = 0.0,
            rf_y: float = 0.085, rf_pitch: float = 0.0) -> PoseSpec:
        return PoseSpec(
            hips_y=hy, hips_yaw=yaw, hips_pitch=pitch, hips_roll=hroll,
            torso_yaw=tyaw, torso_pitch=pitch, torso_roll=troll,
            head_yaw=0.0, head_pitch=1.0, head_roll=-troll * 0.4, head_world_space=False,
            l_shoulder_lift=4.0, l_shoulder_fwd=4.0, r_shoulder_lift=4.0, r_shoulder_fwd=12.0,
            r_hand_pos=r_pos, r_elbow_pole=r_el,
            blade_dir=(-0.72, 0.08, 0.69), blade_edge=(0.69, 0.0, 0.72),
            l_hand_pos=l_pos, l_elbow_pole=l_el, l_hand_dir=l_dir, l_hand_up=(0.1, 0.98, 0.0),
            l_foot_pos=(0.15, 0.085, 0.16), l_knee_pole=(0.22, 0.48, 0.72), l_foot_yaw=12.0,
            r_foot_pos=(-0.20, rf_y, -0.18), r_knee_pole=(-0.28, 0.48, 0.44), r_foot_yaw=-20.0, r_foot_pitch=rf_pitch,
            local_body_space=True, feet_in_body_space=True,
        )

    p0 = _sp(-24.0, 0.905, (-0.22, 1.10, 0.28), (-0.58, 0.88, -0.10),
             (0.20, 1.24, 0.40), (0.56, 1.00, 0.04), (-0.20, 0.28, 0.94), pitch=4.0, tyaw=14.0)
    p1 = _sp(-115.0, 0.935, (-0.12, 1.18, 0.26), (-0.50, 1.00, -0.06),
             (0.44, 1.32, 0.06), (0.66, 1.10, -0.22), (0.76, 0.36, 0.54),
             pitch=8.0, hroll=6.5, tyaw=-14.0, troll=-5.0, rf_y=0.20, rf_pitch=-18.0)
    p2 = _sp(-220.0, 0.945, (-0.36, 1.22, 0.38), (-0.64, 1.04, -0.02),
             (0.46, 1.36, -0.12), (0.66, 1.14, -0.34), (0.82, 0.38, -0.42),
             pitch=10.0, hroll=8.0, tyaw=-20.0, troll=-6.0, rf_y=0.22, rf_pitch=-22.0)
    p3 = _sp(-325.0, 0.880, (-0.52, 1.24, 0.42), (-0.70, 1.06, 0.06),
             (0.32, 1.26, 0.16), (0.60, 1.06, -0.14), (0.20, 0.34, 0.92),
             pitch=9.0, hroll=4.5, tyaw=-24.0, troll=-3.5, rf_y=0.09, rf_pitch=-6.0)
    p4 = _sp(-396.0, 0.875, (-0.48, 1.20, 0.24), (-0.68, 1.00, -0.16),
             (0.22, 1.24, 0.36), (0.58, 1.02, 0.00), (-0.18, 0.30, 0.94),
             pitch=5.0, hroll=1.5, tyaw=-10.0, troll=-1.0, rf_y=0.080)
    p5 = _sp(-384.0, 0.905, (-0.22, 1.10, 0.28), (-0.58, 0.88, -0.10),
             (0.20, 1.24, 0.40), (0.56, 1.00, 0.04), (-0.20, 0.28, 0.94), pitch=4.0, tyaw=14.0)
    return AnimationClip(
        name="wuxia_unarmed_backfist_spin", slot="unarmed_attack", duration=0.85, loop=False,
        tags=["unarmed", "combat", "spin", "backfist", "whip"],
        description="Silky 360-deg pivot spinning whip-palm backfist (Hồi Phong Toàn Chưởng).",
        hit_window=(0.34, 0.56),
        keyframes=[
            Keyframe(0.00, p0, "flow"),
            Keyframe(0.17, p1, "flow"),
            Keyframe(0.34, p2, "flow"),
            Keyframe(0.50, p3, "snap"),
            Keyframe(0.68, p4, "flow"),
            Keyframe(0.85, p5, "flow"),
        ],
    )


def make_unarmed_flurry_palms() -> AnimationClip:
    """9. Liên Hoàn Bát Quái Chưởng: Rapid 4-hit alternating Bagua palm storm ending in a double-palm strike."""
    g0 = _guard()
    p1_l = _guard(
        hips_y=0.845, hips_yaw=-36.0, hips_pitch=10.0, hips_roll=4.5,
        torso_yaw=-14.0, torso_pitch=9.0, torso_roll=-3.5,
        l_shoulder_fwd=24.0, r_shoulder_fwd=-10.0,
        l_hand_pos=(0.06, 1.26, 0.58), l_elbow_pole=(0.42, 1.06, 0.18), l_hand_dir=(-0.06, 0.30, 0.95),
        r_hand_pos=(-0.28, 1.02, 0.12), r_elbow_pole=(-0.58, 0.90, -0.30),
        l_foot_pos=(0.16, 0.080, 0.26), r_foot_pos=(-0.20, 0.080, -0.22),
    )
    p2_r = _guard(
        hips_y=0.835, hips_yaw=24.0, hips_pitch=12.0, hips_roll=-4.5,
        torso_yaw=18.0, torso_pitch=11.0, torso_roll=4.0,
        r_shoulder_fwd=26.0, l_shoulder_fwd=-10.0,
        r_hand_pos=(-0.04, 1.20, 0.60), r_elbow_pole=(-0.40, 1.00, 0.20), blade_dir=(0.04, 0.34, 0.94),
        l_hand_pos=(0.28, 1.06, 0.12), l_elbow_pole=(0.58, 0.94, -0.28), l_hand_dir=(-0.10, 0.20, 0.97),
        l_foot_pos=(0.16, 0.080, 0.28), r_foot_pos=(-0.22, 0.095, -0.24), r_foot_pitch=-10.0,
    )
    p3_l = _guard(
        hips_y=0.840, hips_yaw=-34.0, hips_pitch=12.0, hips_roll=5.0,
        torso_yaw=-16.0, torso_pitch=10.0, torso_roll=-4.0,
        l_shoulder_lift=6.0, l_shoulder_fwd=26.0, r_shoulder_fwd=-12.0,
        l_hand_pos=(0.02, 1.32, 0.60), l_elbow_pole=(0.38, 1.12, 0.22), l_hand_dir=(-0.04, 0.42, 0.91),
        r_hand_pos=(-0.30, 1.00, 0.08), r_elbow_pole=(-0.58, 0.90, -0.34),
        l_foot_pos=(0.15, 0.080, 0.30), r_foot_pos=(-0.22, 0.080, -0.26),
    )
    coil_twin = _guard(
        hips_y=0.810, hips_yaw=-6.0, hips_pitch=11.0, torso_yaw=2.0, torso_pitch=9.0,
        r_shoulder_fwd=-6.0, l_shoulder_fwd=-6.0,
        r_hand_pos=(-0.22, 1.04, 0.16), r_elbow_pole=(-0.56, 0.94, -0.28),
        l_hand_pos=(0.22, 1.04, 0.16), l_elbow_pole=(0.56, 0.94, -0.28),
    )
    p4_twin = _guard(
        hips_y=0.785, hips_yaw=6.0, hips_pitch=17.0, torso_yaw=2.0, torso_pitch=15.0,
        r_shoulder_lift=4.0, r_shoulder_fwd=26.0, l_shoulder_lift=4.0, l_shoulder_fwd=26.0,
        r_hand_pos=(-0.12, 1.20, 0.60), r_elbow_pole=(-0.48, 1.00, 0.20), blade_dir=(0.04, 0.50, 0.86),
        l_hand_pos=(0.12, 1.20, 0.60), l_elbow_pole=(0.48, 1.00, 0.20), l_hand_dir=(-0.04, 0.50, 0.86),
        l_foot_pos=(0.15, 0.080, 0.34), r_foot_pos=(-0.22, 0.110, -0.30), r_foot_pitch=-16.0,
    )
    return AnimationClip(
        name="wuxia_unarmed_flurry_palms", slot="unarmed_attack", duration=0.98, loop=False,
        tags=["unarmed", "combat", "flurry", "palm", "multi_hit"],
        description="Blistering 4-hit Bagua palm flurry (Liên Hoàn Bát Quái Chưởng) finishing with twin palms.",
        hit_window=(0.14, 0.78),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.16, p1_l, "snap"),
            Keyframe(0.32, p2_r, "snap"),
            Keyframe(0.48, p3_l, "snap"),
            Keyframe(0.60, coil_twin, "flow"),
            Keyframe(0.75, p4_twin, "snap"),
            Keyframe(0.98, g0, "flow"),
        ],
    )


# ==============================================================================
# 3. KICKS & KHINH CÔNG AERIALS (5 CLIPS)
# ==============================================================================

def make_unarmed_front_kick() -> AnimationClip:
    """10. Đàn Thoái Điểm Tinh: Crisp chambered Wuxia snap front kick with poised crane-arm counterbalance."""
    g0 = _guard()
    chamber = _guard(
        hips_y=0.925, hips_yaw=-10.0, hips_pitch=-4.0, hips_roll=-4.0,
        torso_yaw=6.0, torso_pitch=-4.0, torso_roll=4.0,
        r_hand_pos=(-0.28, 1.18, 0.26), r_elbow_pole=(-0.60, 0.96, -0.10),
        l_hand_pos=(0.26, 1.28, 0.34), l_elbow_pole=(0.60, 1.08, -0.02),
        # Standing left foot grounded; right knee chambers high!
        l_foot_pos=(0.14, 0.080, 0.06), l_knee_pole=(0.18, 0.50, 0.64), l_foot_yaw=14.0,
        r_foot_pos=(-0.14, 0.540, 0.22), r_knee_pole=(-0.18, 0.94, 0.68), r_foot_yaw=-6.0, r_foot_pitch=-22.0,
    )
    snap_kick = _guard(
        hips_y=0.915, hips_yaw=8.0, hips_pitch=-14.0, hips_roll=-7.0,
        torso_yaw=-6.0, torso_pitch=-10.0, torso_roll=6.0, head_pitch=8.0,
        r_shoulder_lift=4.0, r_shoulder_fwd=-10.0, l_shoulder_lift=6.0, l_shoulder_fwd=10.0,
        # Arms open like crane wings stabilizing high thrust kick
        r_hand_pos=(-0.44, 1.18, -0.04), r_elbow_pole=(-0.66, 1.00, -0.32),
        blade_dir=(-0.74, 0.28, 0.61),
        l_hand_pos=(0.30, 1.34, 0.28), l_elbow_pole=(0.62, 1.14, -0.04), l_hand_dir=(-0.10, 0.42, 0.90),
        l_foot_pos=(0.14, 0.080, 0.02), l_knee_pole=(0.18, 0.48, 0.60), l_foot_yaw=18.0,
        r_foot_pos=(-0.06, 1.120, 0.74), r_knee_pole=(-0.10, 1.04, 0.58), r_foot_yaw=0.0, r_foot_pitch=16.0,
    )
    rechamber = replace(
        chamber, hips_y=0.905, hips_pitch=-2.0,
        r_foot_pos=(-0.15, 0.480, 0.14), r_knee_pole=(-0.20, 0.88, 0.58),
    )
    return AnimationClip(
        name="wuxia_unarmed_front_kick", slot="unarmed_kick", duration=0.68, loop=False,
        tags=["unarmed", "combat", "kick", "front_kick"],
        description="Crisp chambered Wuxia snapping front kick (Đàn Thoái Điểm Tinh).",
        hit_window=(0.22, 0.38),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.16, chamber, "flow"),
            Keyframe(0.30, snap_kick, "snap"),
            Keyframe(0.48, rechamber, "flow"),
            Keyframe(0.68, g0, "flow"),
        ],
    )


def make_unarmed_roundhouse_kick() -> AnimationClip:
    """11. Tảo Diệp Toàn Cước: High fluid whipping roundhouse kick with full hip turnover & agile arm counterbalance."""
    g0 = _guard()
    pivot_chamber = _guard(
        hips_y=0.920, hips_yaw=34.0, hips_pitch=-4.0, hips_roll=-16.0,
        torso_yaw=-16.0, torso_pitch=-4.0, torso_roll=12.0, head_roll=-6.0,
        r_hand_pos=(-0.34, 1.18, 0.18), r_elbow_pole=(-0.62, 1.00, -0.18),
        l_hand_pos=(0.30, 1.32, 0.24), l_elbow_pole=(0.62, 1.12, -0.08),
        l_foot_pos=(0.14, 0.080, 0.06), l_knee_pole=(0.24, 0.48, 0.58), l_foot_yaw=42.0,
        r_foot_pos=(-0.34, 0.640, 0.14), r_knee_pole=(-0.44, 0.96, 0.48), r_foot_yaw=-40.0, r_foot_pitch=-26.0,
    )
    whip_ext = _guard(
        hips_y=0.905, hips_yaw=64.0, hips_pitch=-8.0, hips_roll=-24.0,
        torso_yaw=-34.0, torso_pitch=-6.0, torso_roll=16.0, head_yaw=-12.0, head_roll=-8.0,
        r_shoulder_lift=6.0, r_shoulder_fwd=-14.0, l_shoulder_lift=8.0, l_shoulder_fwd=10.0,
        r_hand_pos=(-0.46, 1.14, -0.16), r_elbow_pole=(-0.66, 1.02, -0.44),
        blade_dir=(-0.78, 0.20, -0.59),
        l_hand_pos=(0.36, 1.34, 0.22), l_elbow_pole=(0.64, 1.16, -0.08), l_hand_dir=(0.26, 0.44, 0.86),
        l_foot_pos=(0.14, 0.080, 0.02), l_knee_pole=(0.28, 0.48, 0.50), l_foot_yaw=62.0,
        # High whipping instep arc across target plane (+Z)
        r_foot_pos=(0.08, 1.180, 0.66), r_knee_pole=(-0.22, 1.12, 0.54), r_foot_yaw=32.0, r_foot_pitch=-32.0,
    )
    follow_fold = _guard(
        hips_y=0.895, hips_yaw=28.0, hips_pitch=0.0, hips_roll=-10.0,
        torso_yaw=-8.0, torso_pitch=2.0, torso_roll=8.0,
        r_hand_pos=(-0.32, 1.12, 0.16),
        l_hand_pos=(0.28, 1.28, 0.28),
        l_foot_pos=(0.15, 0.080, 0.12), l_foot_yaw=28.0,
        r_foot_pos=(-0.18, 0.460, 0.06), r_knee_pole=(-0.28, 0.80, 0.48), r_foot_pitch=-16.0,
    )
    return AnimationClip(
        name="wuxia_unarmed_roundhouse_kick", slot="unarmed_kick", duration=0.78, loop=False,
        tags=["unarmed", "combat", "kick", "roundhouse"],
        description="High whipping crescent roundhouse kick (Tảo Diệp Toàn Cước) with full hip turnover.",
        hit_window=(0.26, 0.44),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.18, pivot_chamber, "flow"),
            Keyframe(0.34, whip_ext, "snap"),
            Keyframe(0.54, follow_fold, "flow"),
            Keyframe(0.78, g0, "flow"),
        ],
    )


def make_unarmed_low_sweep() -> AnimationClip:
    """12. Địa Long Tảo Vĩ: Low crouched 360-deg dragon-tail leg sweep braced by left palm near ground."""
    def _sw(yaw: float, hy: float, pitch: float, hroll: float,
            r_hand, l_hand, r_foot, r_knee, l_foot_y: float = 0.080) -> PoseSpec:
        return PoseSpec(
            hips_y=hy, hips_yaw=yaw, hips_pitch=pitch, hips_roll=hroll,
            torso_yaw=14.0, torso_pitch=pitch + 6.0, torso_roll=-hroll * 0.6,
            head_yaw=0.0, head_pitch=-6.0, head_world_space=False,
            l_shoulder_lift=-6.0, l_shoulder_fwd=16.0, r_shoulder_lift=8.0, r_shoulder_fwd=-6.0,
            r_hand_pos=r_hand, r_elbow_pole=(-0.60, hy + 0.34, -0.22),
            blade_dir=(-0.74, 0.44, -0.51), blade_edge=(0.0, 0.76, 0.65),
            l_hand_pos=l_hand, l_elbow_pole=(0.52, hy + 0.10, 0.14),
            l_hand_dir=(0.10, -0.82, 0.56), l_hand_up=(0.0, 0.56, 0.82),
            l_foot_pos=(0.14, l_foot_y, 0.08), l_knee_pole=(0.24, hy + 0.16, 0.62), l_foot_yaw=16.0,
            r_foot_pos=r_foot, r_knee_pole=r_knee, r_foot_yaw=-34.0, r_foot_pitch=-20.0,
            local_body_space=True, feet_in_body_space=True,
        )

    p0 = _sw(-24.0, 0.905, 4.0, 0.0, (-0.22, 1.10, 0.28), (0.20, 1.24, 0.40), (-0.19, 0.08, -0.20), (-0.30, 0.48, 0.40))
    p_drop = _sw(-50.0, 0.500, 24.0, 10.0, (-0.38, 0.78, -0.06), (0.26, 0.20, 0.28), (-0.54, 0.12, 0.16), (-0.42, 0.28, 0.34))
    p_sw1 = _sw(65.0, 0.470, 26.0, 12.0, (-0.44, 0.82, -0.14), (0.24, 0.16, 0.26), (-0.64, 0.13, 0.42), (-0.44, 0.26, 0.44))
    p_sw2 = _sw(195.0, 0.470, 26.0, 12.0, (-0.44, 0.82, -0.14), (0.24, 0.16, 0.26), (-0.64, 0.13, 0.42), (-0.44, 0.26, 0.44))
    p_sw3 = _sw(315.0, 0.530, 22.0, 8.0, (-0.36, 0.84, 0.04), (0.26, 0.28, 0.28), (-0.46, 0.11, 0.24), (-0.36, 0.30, 0.38))
    p_end = _sw(336.0, 0.905, 4.0, 0.0, (-0.22, 1.10, 0.28), (0.20, 1.24, 0.40), (-0.19, 0.08, -0.20), (-0.30, 0.48, 0.40))
    return AnimationClip(
        name="wuxia_unarmed_low_sweep", slot="unarmed_kick", duration=0.88, loop=False,
        tags=["unarmed", "combat", "kick", "sweep", "low", "360"],
        description="Low crouched 360-deg dragon-tail leg sweep (Địa Long Tảo Vĩ) braced by left palm.",
        hit_window=(0.20, 0.64),
        keyframes=[
            Keyframe(0.00, p0, "flow"),
            Keyframe(0.16, p_drop, "heavy_drop"),
            Keyframe(0.32, p_sw1, "snap"),
            Keyframe(0.50, p_sw2, "flow"),
            Keyframe(0.68, p_sw3, "flow"),
            Keyframe(0.88, p_end, "flow"),
        ],
    )


def make_unarmed_tornado_kick() -> AnimationClip:
    """13. Toàn Phong Phi Cước: Soaring Khinh Công airborne 360-deg tornado crescent kick."""
    def _tk(yaw: float, hy: float, lf_pos, lf_knee, rf_pos, rf_knee,
            pitch: float = 4.0, hroll: float = 0.0, tyaw: float = 12.0, troll: float = 0.0) -> PoseSpec:
        return PoseSpec(
            hips_y=hy, hips_yaw=yaw, hips_pitch=pitch, hips_roll=hroll,
            torso_yaw=tyaw, torso_pitch=pitch, torso_roll=troll,
            head_yaw=0.0, head_pitch=2.0, head_roll=-troll * 0.4, head_world_space=False,
            l_shoulder_lift=10.0, l_shoulder_fwd=4.0, r_shoulder_lift=8.0, r_shoulder_fwd=-6.0,
            r_hand_pos=(-0.44, hy + 0.30, -0.10), r_elbow_pole=(-0.66, hy + 0.14, -0.32),
            blade_dir=(-0.82, 0.32, -0.48), blade_edge=(0.0, 0.83, 0.55),
            l_hand_pos=(0.44, hy + 0.34, 0.10), l_elbow_pole=(0.66, hy + 0.16, -0.14),
            l_hand_dir=(0.76, 0.44, 0.48), l_hand_up=(0.0, 0.96, -0.28),
            l_foot_pos=lf_pos, l_knee_pole=lf_knee, l_foot_yaw=14.0, l_foot_pitch=-22.0 if lf_pos[1] > 0.14 else 0.0,
            r_foot_pos=rf_pos, r_knee_pole=rf_knee, r_foot_yaw=-20.0, r_foot_pitch=-28.0 if rf_pos[1] > 0.14 else 0.0,
            local_body_space=True, feet_in_body_space=True,
        )

    p0 = _tk(-24.0, 0.905, (0.16, 0.08, 0.20), (0.22, 0.48, 0.74), (-0.19, 0.08, -0.20), (-0.30, 0.48, 0.40), pitch=4.0)
    p_coil = _tk(-58.0, 0.760, (0.16, 0.08, 0.22), (0.22, 0.44, 0.78), (-0.20, 0.08, -0.22), (-0.30, 0.44, 0.36), pitch=14.0, hroll=6.0, tyaw=-14.0, troll=-4.5)
    p_launch = _tk(48.0, 1.180, (0.16, 0.56, 0.24), (0.22, 0.96, 0.84), (-0.22, 0.36, -0.22), (-0.30, 0.74, 0.36), pitch=4.0, hroll=-8.0, tyaw=16.0, troll=6.0)
    p_apex = _tk(175.0, 1.350, (0.18, 0.64, -0.06), (0.24, 1.00, 0.52), (-0.06, 1.48, 0.68), (-0.22, 1.44, 0.54), pitch=-8.0, hroll=-16.0, tyaw=22.0, troll=12.0)
    p_desc = _tk(288.0, 1.120, (0.16, 0.32, 0.18), (0.22, 0.72, 0.74), (-0.20, 0.46, 0.14), (-0.28, 0.82, 0.58), pitch=8.0, hroll=-6.0, tyaw=14.0, troll=5.0)
    p_land = _tk(348.0, 0.820, (0.16, 0.08, 0.22), (0.22, 0.44, 0.78), (-0.20, 0.08, -0.20), (-0.30, 0.44, 0.38), pitch=10.0, hroll=-2.0, tyaw=10.0, troll=1.5)
    p_end = _tk(336.0, 0.905, (0.16, 0.08, 0.20), (0.22, 0.48, 0.74), (-0.19, 0.08, -0.20), (-0.30, 0.48, 0.40), pitch=4.0)
    return AnimationClip(
        name="wuxia_unarmed_tornado_kick", slot="unarmed_kick", duration=0.95, loop=False,
        tags=["unarmed", "combat", "kick", "aerial", "spin", "360", "khinh_cong"],
        description="Soaring Khinh Công airborne 360-deg tornado kick (Toàn Phong Phi Cước).",
        hit_window=(0.34, 0.60),
        keyframes=[
            Keyframe(0.00, p0, "flow"),
            Keyframe(0.14, p_coil, "flow"),
            Keyframe(0.28, p_launch, "snap"),
            Keyframe(0.46, p_apex, "snap"),
            Keyframe(0.64, p_desc, "flow"),
            Keyframe(0.80, p_land, "heavy_drop"),
            Keyframe(0.95, p_end, "flow"),
        ],
    )


def make_unarmed_jump_slam() -> AnimationClip:
    """14. Thiên Giáng Chấn Địa: High Khinh Công leap crashing down into a flat twin-palm ground shockwave."""
    g0 = _guard()
    takeoff = _guard(
        hips_y=0.735, hips_yaw=-14.0, hips_pitch=16.0, torso_yaw=6.0, torso_pitch=14.0,
        r_hand_pos=(-0.30, 0.86, 0.14), blade_dir=(0.10, 0.22, 0.97),
        l_hand_pos=(0.30, 0.86, 0.14), l_hand_dir=(-0.10, 0.22, 0.97),
        l_foot_pos=(0.16, 0.080, 0.16), r_foot_pos=(-0.18, 0.080, -0.16),
    )
    air_apex = _guard(
        hips_y=1.420, hips_yaw=-6.0, hips_pitch=-8.0, torso_yaw=2.0, torso_pitch=-14.0, head_pitch=8.0,
        r_shoulder_lift=24.0, r_shoulder_fwd=12.0, l_shoulder_lift=24.0, l_shoulder_fwd=12.0,
        r_hand_pos=(-0.22, 2.16, 0.18), r_elbow_pole=(-0.56, 1.88, 0.24),
        blade_dir=(0.06, 0.72, 0.69), blade_edge=(0.0, 0.69, -0.72),
        l_hand_pos=(0.22, 2.16, 0.18), l_elbow_pole=(0.56, 1.88, 0.24),
        l_hand_dir=(-0.06, 0.72, 0.69), l_hand_up=(0.0, 0.69, -0.72),
        l_foot_pos=(0.16, 0.720, 0.20), l_knee_pole=(0.22, 1.14, 0.82), l_foot_pitch=-26.0,
        r_foot_pos=(-0.18, 0.620, -0.22), r_knee_pole=(-0.24, 1.02, 0.42), r_foot_pitch=-32.0,
    )
    drop_arc = _guard(
        hips_y=1.020, hips_yaw=4.0, hips_pitch=14.0, torso_yaw=0.0, torso_pitch=16.0, head_pitch=6.0,
        r_shoulder_lift=4.0, r_shoulder_fwd=24.0, l_shoulder_lift=4.0, l_shoulder_fwd=24.0,
        r_hand_pos=(-0.16, 1.24, 0.48), r_elbow_pole=(-0.50, 1.18, 0.22),
        blade_dir=(0.02, 0.18, 0.98), blade_edge=(0.0, 0.98, -0.18),
        l_hand_pos=(0.16, 1.24, 0.48), l_elbow_pole=(0.50, 1.18, 0.22),
        l_hand_dir=(-0.02, 0.18, 0.98), l_hand_up=(0.0, 0.98, -0.18),
        l_foot_pos=(0.18, 0.260, 0.26), r_foot_pos=(-0.20, 0.220, -0.24),
    )
    slam_impact = _guard(
        hips_y=0.615, hips_yaw=4.0, hips_pitch=28.0, torso_yaw=0.0, torso_pitch=26.0, head_pitch=10.0,
        r_shoulder_lift=-12.0, r_shoulder_fwd=24.0, l_shoulder_lift=-12.0, l_shoulder_fwd=24.0,
        # Flat palms slap the earth with fingers pointing forward (+Z), keeping virtual tip cleanly above ground
        r_hand_pos=(-0.18, 0.18, 0.42), r_elbow_pole=(-0.50, 0.56, 0.22),
        blade_dir=(0.0, 0.08, 0.997), blade_edge=(0.0, 0.997, -0.08),
        l_hand_pos=(0.18, 0.18, 0.42), l_elbow_pole=(0.50, 0.56, 0.22),
        l_hand_dir=(0.0, 0.08, 0.997), l_hand_up=(0.0, 0.997, -0.08),
        l_foot_pos=(0.22, 0.080, 0.26), l_knee_pole=(0.28, 0.40, 0.84),
        r_foot_pos=(-0.22, 0.080, -0.28), r_knee_pole=(-0.30, 0.34, 0.24),
    )
    slam_pulse = replace(
        slam_impact, hips_y=0.600, hips_pitch=30.0, torso_pitch=28.0,
        r_hand_pos=(-0.20, 0.16, 0.43), l_hand_pos=(0.20, 0.16, 0.43),
        blade_dir=(0.0, 0.10, 0.995), l_hand_dir=(0.0, 0.10, 0.995),
    )
    return AnimationClip(
        name="wuxia_unarmed_jump_slam", slot="unarmed_heavy", duration=1.02, loop=False,
        tags=["unarmed", "combat", "aerial", "slam", "aoe"],
        description="High Khinh Công leap crashing into a flat twin-palm ground shockwave (Thiên Giáng Chấn Địa).",
        hit_window=(0.46, 0.68),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.14, takeoff, "flow"),
            Keyframe(0.36, air_apex, "flow"),
            Keyframe(0.49, drop_arc, "heavy_drop"),
            Keyframe(0.60, slam_impact, "snap", tension=0.40),
            Keyframe(0.75, slam_pulse, "flow", tension=0.40),
            Keyframe(1.02, g0, "flow"),
        ],
    )


# ==============================================================================
# 4. QI SKILLS, DEFENSE, EVADE, HIT & MEDITATION (6 CLIPS)
# ==============================================================================

def make_unarmed_qi_blast() -> AnimationClip:
    """15. Thái Cực Khí Ba: Circular Taiji sphere gathering at Dan-Tian -> radiant double-palm Qi wave blast."""
    g0 = _guard()
    sphere_open = _guard(
        hips_y=0.875, hips_yaw=-14.0, hips_pitch=4.0, hips_roll=-2.0,
        torso_yaw=6.0, torso_pitch=2.0, torso_roll=2.0,
        r_shoulder_lift=2.0, r_shoulder_fwd=10.0, l_shoulder_lift=2.0, l_shoulder_fwd=10.0,
        # Circular Taiji embrace cradling an orb of Qi
        r_hand_pos=(-0.26, 1.24, 0.34), r_elbow_pole=(-0.60, 1.04, 0.04),
        blade_dir=(0.76, -0.20, 0.62), blade_edge=(0.0, 0.95, 0.31),
        l_hand_pos=(0.26, 1.06, 0.34), l_elbow_pole=(0.60, 0.90, 0.04),
        l_hand_dir=(-0.76, 0.20, 0.62), l_hand_up=(0.0, 0.95, -0.31),
    )
    compress_core = _guard(
        hips_y=0.805, hips_yaw=-34.0, hips_pitch=10.0, hips_roll=4.5,
        torso_yaw=-12.0, torso_pitch=8.0, torso_roll=-3.5,
        r_shoulder_lift=-4.0, r_shoulder_fwd=-2.0, l_shoulder_lift=-2.0, l_shoulder_fwd=10.0,
        # Both hands compress Qi orb tightly near right waist-ribs
        r_hand_pos=(-0.24, 1.00, 0.18), r_elbow_pole=(-0.58, 0.92, -0.24),
        blade_dir=(0.30, 0.42, 0.86), blade_edge=(0.0, 0.90, -0.44),
        l_hand_pos=(-0.06, 1.12, 0.24), l_elbow_pole=(0.42, 0.96, 0.04),
        l_hand_dir=(-0.26, -0.36, 0.90), l_hand_up=(0.0, 0.93, 0.37),
        l_foot_pos=(0.18, 0.080, 0.24), r_foot_pos=(-0.22, 0.080, -0.24),
    )
    blast_release = _guard(
        hips_y=0.815, hips_yaw=16.0, hips_pitch=14.0, hips_roll=-3.5,
        torso_yaw=10.0, torso_pitch=12.0, torso_roll=3.0, head_pitch=-3.0,
        r_shoulder_lift=6.0, r_shoulder_fwd=26.0, l_shoulder_lift=6.0, l_shoulder_fwd=26.0,
        r_hand_pos=(-0.10, 1.20, 0.60), r_elbow_pole=(-0.48, 1.00, 0.20),
        blade_dir=(0.04, 0.62, 0.78), blade_edge=(0.0, 0.78, -0.62),
        l_hand_pos=(0.10, 1.20, 0.60), l_elbow_pole=(0.48, 1.00, 0.20),
        l_hand_dir=(-0.04, 0.62, 0.78), l_hand_up=(0.0, 0.78, -0.62),
        l_foot_pos=(0.16, 0.080, 0.34), r_foot_pos=(-0.22, 0.080, -0.30),
    )
    blast_drift = replace(
        blast_release, hips_y=0.840, hips_pitch=10.0, torso_pitch=8.0,
        r_hand_pos=(-0.16, 1.24, 0.56), l_hand_pos=(0.16, 1.24, 0.56),
    )
    return AnimationClip(
        name="wuxia_unarmed_qi_blast", slot="unarmed_cast", duration=1.10, loop=False,
        tags=["unarmed", "skill", "qi", "blast", "taiji"],
        description="Taiji Qi-sphere gather bursting into a twin-palm Qi wave (Thái Cực Khí Ba).",
        hit_window=(0.56, 0.82),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.24, sphere_open, "flow"),
            Keyframe(0.46, compress_core, "flow"),
            Keyframe(0.64, blast_release, "snap"),
            Keyframe(0.84, blast_drift, "flow"),
            Keyframe(1.10, g0, "flow"),
        ],
    )


def make_unarmed_block() -> AnimationClip:
    """16. Kim Cang Hộ Thể: Compact X-forearm martial cross-guard brace with elastic impact absorb."""
    g0 = _guard()
    brace = _guard(
        hips_y=0.835, hips_yaw=-14.0, hips_pitch=8.0, hips_roll=3.0,
        torso_yaw=6.0, torso_pitch=6.0, torso_roll=-2.5, head_pitch=4.0,
        r_shoulder_lift=8.0, r_shoulder_fwd=18.0, l_shoulder_lift=8.0, l_shoulder_fwd=18.0,
        # Tight crossed forearms shielding face & chest
        r_hand_pos=(0.06, 1.32, 0.36), r_elbow_pole=(-0.44, 1.06, 0.14),
        blade_dir=(0.48, 0.78, 0.40), blade_edge=(0.0, 0.46, -0.89),
        l_hand_pos=(-0.06, 1.30, 0.39), l_elbow_pole=(0.44, 1.04, 0.16),
        l_hand_dir=(-0.48, 0.78, 0.40),
        l_foot_pos=(0.20, 0.080, 0.18), r_foot_pos=(-0.22, 0.080, -0.20),
    )
    absorb = replace(
        brace, hips_y=0.798, hips_pitch=3.0, torso_pitch=1.0,
        r_hand_pos=(0.05, 1.29, 0.30), l_hand_pos=(-0.05, 1.27, 0.33),
    )
    rebound = replace(
        brace, hips_y=0.855, hips_roll=1.5, torso_roll=-1.0,
        r_hand_pos=(0.04, 1.31, 0.38), l_hand_pos=(-0.04, 1.29, 0.40),
    )
    return AnimationClip(
        name="wuxia_unarmed_block", slot="unarmed_block", duration=0.62, loop=False,
        tags=["unarmed", "combat", "block", "defense"],
        description="Compact X-forearm cross-guard block (Kim Cang Hộ Thể) with elastic shock absorb.",
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.14, brace, "snap"),
            Keyframe(0.28, absorb, "flow"),
            Keyframe(0.44, rebound, "flow"),
            Keyframe(0.62, g0, "flow"),
        ],
    )


def make_unarmed_parry() -> AnimationClip:
    """17. Tá Lực Hóa Kình: Silky circular Taiji double-palm deflection redirecting enemy force past flank."""
    g0 = _guard()
    receive = _guard(
        hips_y=0.870, hips_yaw=14.0, hips_roll=-3.5, torso_yaw=10.0, torso_roll=3.0,
        l_hand_pos=(0.14, 1.24, 0.44), l_elbow_pole=(0.52, 1.02, 0.10), l_hand_dir=(-0.28, 0.34, 0.90),
        r_hand_pos=(-0.10, 1.14, 0.40), r_elbow_pole=(-0.48, 0.94, 0.08), blade_dir=(0.24, 0.32, 0.92),
    )
    redirect = _guard(
        hips_y=0.850, hips_yaw=-40.0, hips_roll=6.0, torso_yaw=-18.0, torso_pitch=4.0, torso_roll=-5.0,
        r_shoulder_lift=4.0, r_shoulder_fwd=-10.0, l_shoulder_lift=6.0, l_shoulder_fwd=16.0,
        # Right hand draws force smoothly past right flank; left palm intercepts centerline!
        r_hand_pos=(-0.44, 1.18, 0.06), r_elbow_pole=(-0.66, 1.00, -0.28),
        blade_dir=(-0.78, 0.24, 0.58),
        l_hand_pos=(0.06, 1.28, 0.46), l_elbow_pole=(0.48, 1.06, 0.14), l_hand_dir=(-0.38, 0.38, 0.84),
        l_foot_pos=(0.18, 0.080, 0.22), r_foot_pos=(-0.22, 0.080, -0.24),
    )
    redirect_flow = replace(
        redirect, hips_y=0.872, hips_yaw=-42.0, hips_roll=4.0, torso_roll=-3.0,
        r_hand_pos=(-0.46, 1.21, 0.02), l_hand_pos=(0.08, 1.30, 0.44),
    )
    return AnimationClip(
        name="wuxia_unarmed_parry", slot="unarmed_parry", duration=0.65, loop=False,
        tags=["unarmed", "combat", "parry", "taiji", "defense"],
        description="Circular Taiji force-redirection parry (Tá Lực Hóa Kình).",
        hit_window=(0.14, 0.34),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.13, receive, "flow"),
            Keyframe(0.29, redirect, "snap"),
            Keyframe(0.45, redirect_flow, "flow"),
            Keyframe(0.65, g0, "flow"),
        ],
    )


def make_unarmed_dodge_back() -> AnimationClip:
    """18. Hạc Lạc Thối Bộ: Feather-light Khinh Công backward skim with balanced open crane arms."""
    g0 = _guard()
    push_off = _guard(
        hips_y=0.855, hips_yaw=-18.0, hips_pitch=13.0, hips_roll=2.5,
        torso_yaw=8.0, torso_pitch=10.0, torso_roll=-2.0, head_pitch=-4.0,
        r_hand_pos=(-0.32, 1.12, 0.22), l_hand_pos=(0.26, 1.24, 0.36),
        l_foot_pos=(0.16, 0.160, 0.22), l_foot_pitch=-8.0,
        r_foot_pos=(-0.20, 0.095, -0.24), r_foot_pitch=-12.0,
    )
    skim = _guard(
        hips_y=1.030, hips_yaw=-24.0, hips_pitch=14.0, hips_roll=4.5,
        torso_yaw=10.0, torso_pitch=12.0, torso_roll=-3.5, head_pitch=-6.0,
        r_shoulder_lift=4.0, r_shoulder_fwd=-10.0, l_shoulder_lift=6.0, l_shoulder_fwd=10.0,
        r_hand_pos=(-0.44, 1.22, 0.06), r_elbow_pole=(-0.66, 1.06, -0.26), blade_dir=(-0.64, 0.30, 0.71),
        l_hand_pos=(0.30, 1.32, 0.38), l_elbow_pole=(0.60, 1.12, 0.02), l_hand_dir=(-0.18, 0.32, 0.93),
        l_foot_pos=(0.16, 0.290, 0.24), l_knee_pole=(0.22, 0.67, 0.82), l_foot_pitch=-16.0,
        r_foot_pos=(-0.20, 0.170, -0.28), r_knee_pole=(-0.28, 0.55, 0.32), r_foot_pitch=-22.0,
    )
    land_soft = _guard(
        hips_y=0.848, hips_yaw=-22.0, hips_pitch=9.0, hips_roll=1.5,
        torso_yaw=12.0, torso_pitch=7.0, torso_roll=-1.5,
        l_foot_pos=(0.16, 0.080, 0.20), r_foot_pos=(-0.20, 0.080, -0.22),
    )
    return AnimationClip(
        name="wuxia_unarmed_dodge_back", slot="unarmed_dodge", duration=0.60, loop=False,
        tags=["unarmed", "combat", "dodge", "back", "khinh_cong"],
        description="Feather-light Khinh Công backward skim evade (Hạc Lạc Thối Bộ).",
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.13, push_off, "snap"),
            Keyframe(0.28, skim, "flow"),
            Keyframe(0.44, land_soft, "heavy_drop"),
            Keyframe(0.60, g0, "flow"),
        ],
    )


def make_unarmed_hit() -> AnimationClip:
    """19. Trúng Chiêu Hồi Kình: Whip-action frontal impact stagger with reflexive arms & poised recovery."""
    g0 = _guard()
    impact_snap = _guard(
        hips_y=0.855, hips_yaw=-28.0, hips_pitch=-6.0, hips_roll=3.0,
        torso_yaw=-8.0, torso_pitch=-14.0, torso_roll=5.0,
        head_pitch=2.5, head_yaw=-3.0, head_roll=-2.0,
        r_hand_pos=(-0.34, 1.18, 0.18), r_elbow_pole=(-0.62, 0.98, -0.18),
        l_hand_pos=(0.24, 1.32, 0.26), l_elbow_pole=(0.56, 1.08, -0.10),
        l_foot_pos=(0.18, 0.105, 0.16), l_foot_pitch=6.0,
        r_foot_pos=(-0.22, 0.080, -0.22),
    )
    recoil_follow = _guard(
        hips_y=0.832, hips_yaw=-36.0, hips_pitch=-9.0, hips_roll=5.0,
        torso_yaw=-12.0, torso_pitch=-17.0, torso_roll=6.5,
        head_pitch=-12.0, head_yaw=-7.0, head_roll=3.0,
        r_shoulder_lift=9.0, r_shoulder_fwd=-10.0, l_shoulder_lift=8.0,
        r_hand_pos=(-0.40, 1.22, 0.10), r_elbow_pole=(-0.64, 1.04, -0.26),
        l_hand_pos=(0.28, 1.38, 0.16), l_elbow_pole=(0.58, 1.12, -0.18),
        l_foot_pos=(0.18, 0.120, 0.14), l_foot_pitch=10.0,
        r_foot_pos=(-0.22, 0.080, -0.26),
    )
    brace_recover = _guard(
        hips_y=0.858, hips_yaw=-26.0, hips_pitch=5.0, hips_roll=1.5,
        torso_yaw=6.0, torso_pitch=4.5, torso_roll=-1.0, head_pitch=-1.5,
        l_foot_pos=(0.17, 0.080, 0.18), r_foot_pos=(-0.20, 0.080, -0.20),
    )
    return AnimationClip(
        name="wuxia_unarmed_hit", slot="unarmed_hit", duration=0.55, loop=False,
        tags=["unarmed", "combat", "hit", "reaction"],
        description="Organic whip-action frontal stagger & guard recovery (Trúng Chiêu Hồi Kình).",
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.11, impact_snap, "snap"),
            Keyframe(0.24, recoil_follow, "flow"),
            Keyframe(0.39, brace_recover, "flow"),
            Keyframe(0.55, g0, "flow"),
        ],
    )


def make_unarmed_meditate() -> AnimationClip:
    """20. Ngưng Thần Tụ Khí: Standing Taiji Dan-Tian Qi circulation with floating rising & sinking palms."""
    m0 = PoseSpec(
        hips_y=0.910, hips_yaw=0.0, hips_pitch=1.0, torso_yaw=0.0, torso_pitch=1.0,
        head_yaw=0.0, head_pitch=3.5,
        l_shoulder_lift=-4.0, l_shoulder_fwd=10.0, r_shoulder_lift=-4.0, r_shoulder_fwd=10.0,
        # Twin palms softly facing downward over Dan-Tian
        r_hand_pos=(-0.14, 1.00, 0.34), r_elbow_pole=(-0.48, 0.92, 0.06),
        blade_dir=(0.24, -0.34, 0.91), blade_edge=(0.0, 0.94, 0.34),
        l_hand_pos=(0.14, 1.00, 0.34), l_elbow_pole=(0.48, 0.92, 0.06),
        l_hand_dir=(-0.24, -0.34, 0.91), l_hand_up=(0.0, 0.94, 0.34),
        l_foot_pos=(0.16, 0.080, 0.00), l_knee_pole=(0.18, 0.48, 0.62), l_foot_yaw=8.0,
        r_foot_pos=(-0.16, 0.080, 0.00), r_knee_pole=(-0.18, 0.48, 0.62), r_foot_yaw=-8.0,
    )
    m_rise = replace(
        m0, hips_y=0.922, hips_pitch=0.0, torso_pitch=-0.5, head_pitch=2.5,
        l_shoulder_lift=-2.0, r_shoulder_lift=-2.0,
        r_hand_pos=(-0.16, 1.11, 0.36), r_elbow_pole=(-0.50, 0.98, 0.05),
        blade_dir=(0.18, 0.24, 0.95), blade_edge=(0.0, 0.97, -0.24),
        l_hand_pos=(0.16, 1.11, 0.36), l_elbow_pole=(0.50, 0.98, 0.05),
        l_hand_dir=(-0.18, 0.24, 0.95), l_hand_up=(0.0, 0.97, -0.24),
    )
    m_apex = replace(
        m0, hips_y=0.934, hips_pitch=-0.5, torso_pitch=-2.0, head_pitch=1.5,
        l_shoulder_lift=0.0, r_shoulder_lift=0.0,
        r_hand_pos=(-0.18, 1.22, 0.37), r_elbow_pole=(-0.52, 1.04, 0.04),
        blade_dir=(0.14, 0.42, 0.90), blade_edge=(0.0, 0.91, -0.42),
        l_hand_pos=(0.18, 1.22, 0.37), l_elbow_pole=(0.52, 1.04, 0.04),
        l_hand_dir=(-0.14, 0.42, 0.90), l_hand_up=(0.0, 0.91, -0.42),
    )
    m_sink = replace(
        m0, hips_y=0.918, hips_pitch=0.5, torso_pitch=0.4, head_pitch=3.0,
        l_shoulder_lift=-3.5, r_shoulder_lift=-3.5,
        r_hand_pos=(-0.15, 1.09, 0.35), r_elbow_pole=(-0.49, 0.96, 0.055),
        blade_dir=(0.20, -0.18, 0.96), blade_edge=(0.0, 0.98, 0.18),
        l_hand_pos=(0.15, 1.09, 0.35), l_elbow_pole=(0.49, 0.96, 0.055),
        l_hand_dir=(-0.20, -0.18, 0.96), l_hand_up=(0.0, 0.98, 0.18),
    )
    return AnimationClip(
        name="wuxia_unarmed_meditate", slot="meditate", duration=2.20, loop=True,
        tags=["unarmed", "qi", "meditate", "taiji", "wuxia"],
        description="Standing Taiji Dan-Tian Qi circulation meditation (Ngưng Thần Tụ Khí).",
        keyframes=[
            Keyframe(0.00, m0, "flow"),
            Keyframe(0.55, m_rise, "flow"),
            Keyframe(1.10, m_apex, "flow"),
            Keyframe(1.65, m_sink, "flow"),
            Keyframe(2.20, m0, "flow"),
        ],
    )


# ==============================================================================
# MASTER BUILDER (ALL 20 UNARMED CLIPS)
# ==============================================================================

def build_all_unarmed_clips(fps: int = 60) -> List[AnimationClip]:
    builders = [
        # Locomotion (3)
        make_unarmed_idle,
        make_unarmed_walk,
        make_unarmed_run,
        # Palm & Fist Combos (6)
        make_unarmed_jab_left,
        make_unarmed_cross_right,
        make_unarmed_palm_push,
        make_unarmed_uppercut,
        make_unarmed_backfist_spin,
        make_unarmed_flurry_palms,
        # Kicks & Khinh Công Aerials (5)
        make_unarmed_front_kick,
        make_unarmed_roundhouse_kick,
        make_unarmed_low_sweep,
        make_unarmed_tornado_kick,
        make_unarmed_jump_slam,
        # Qi Skills, Defense, Evade, Hit & Meditation (6)
        make_unarmed_qi_blast,
        make_unarmed_block,
        make_unarmed_parry,
        make_unarmed_dodge_back,
        make_unarmed_hit,
        make_unarmed_meditate,
    ]
    return [sample_clip(fn(), fps=fps) for fn in builders]