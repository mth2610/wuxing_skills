#!/usr/bin/env python3
"""
tools/animations/sword_anims.py — Complete 31-Clip Ethereal Wuxia Sword (Thanh Thoát Kiếm Pháp) Library

All 31 GAME_MIXAMO_20 sword animations unified in ONE self-contained file, refined for silk-smooth
Catmull-Rom flow and authentic Wuxia / Xianxia martial grace:
  - Moving Zanshin holds replacing duplicate-keyframe pauses (eliminates Catmull-Rom wobble & freezes)
  - Full 3D breakdown arcs (figure-8 slashes, wide crescents, clean blade-inversion wrist turns)
  - S-curve spine kinetic chain: footwork -> hips_y/yaw/roll -> torso counter-roll -> head poise
  - Finely staged 360-deg pivot & airborne spins with centrifugal body lean and cushioned Khinh Công landings
  - Intentional Left-Hand Sword-Finger (Kiếm Quyết — 劍訣) choreography on EVERY frame:
      * Dẫn Kiếm (guiding grace near right wrist / chest in guard & transitions)
      * Nghịch Phối (flowing crane/swallow counter-wings during slashes, thrusts & spins)
      * Phụ Kiếm (two-hand forearm brace in block, Saya-biki pull in Iaido, pommel drive in plunge)
      * Dẫn Khí (blade Qi-trace & heavenly halo in Cast / Sky Summon / Meditate)
"""

from __future__ import annotations

from dataclasses import replace
from typing import List

from .rig_core import AnimationClip, Keyframe, PoseSpec, sample_clip


# ==============================================================================
# CANONICAL WUXIA GUARD POSE (Khởi Thức Thanh Phong Kiếm)
# ==============================================================================

def _guard(**overrides) -> PoseSpec:
    """Graceful Wudang / Xianxia sword guard with active poised left Kiếm Quyết."""
    base = PoseSpec(
        hips_y=0.910,
        hips_yaw=-26.0,
        hips_pitch=3.5,
        hips_roll=0.0,
        torso_yaw=14.0,
        torso_pitch=3.5,
        torso_roll=-1.5,
        head_yaw=0.0,
        head_pitch=1.0,
        head_roll=0.0,
        l_shoulder_lift=-2.0,
        l_shoulder_fwd=8.0,
        r_shoulder_lift=-4.0,
        r_shoulder_fwd=10.0,
        # Right hand holds sword fluidly forward-right; blade draws an elegant ascending diagonal
        r_hand_pos=(-0.18, 1.08, 0.36),
        r_elbow_pole=(-0.58, 0.88, -0.08),
        blade_dir=(0.24, 0.46, 0.85),
        blade_edge=(0.0, 1.0, 0.0),
        blade_spin_deg=0.0,
        # Left Kiếm Quyết: poised softly near chest-left with lifted graceful wrist & elbow
        l_hand_pos=(0.22, 1.24, 0.30),
        l_elbow_pole=(0.60, 1.04, -0.06),
        l_hand_dir=(-0.38, 0.30, 0.87),
        l_hand_up=(0.15, 0.96, -0.22),
        # Grounded martial footwork (left foot forward, right anchored)
        l_foot_pos=(0.16, 0.08, 0.18),
        l_knee_pole=(0.22, 0.48, 0.72),
        l_foot_yaw=10.0,
        l_foot_pitch=0.0,
        r_foot_pos=(-0.19, 0.08, -0.18),
        r_knee_pole=(-0.30, 0.48, 0.42),
        r_foot_yaw=-26.0,
        r_foot_pitch=0.0,
    )
    return replace(base, **overrides) if overrides else base


_guard_pose = _guard


# ==============================================================================
# 1. LOCOMOTION & WEAPON HANDLING (7 CLIPS: IDLE, WALK, RUN, STRAFES, DRAW, SHEATH)
# ==============================================================================

def make_sword_idle() -> AnimationClip:
    """Khởi Thức Thanh Phong: Ethereal breathing sway with figure-8 drifting Kiếm Quyết & sword tip."""
    g0 = _guard()
    g1 = _guard(
        hips_y=0.923, hips_yaw=-22.5, hips_roll=-1.5,
        torso_yaw=16.5, torso_pitch=2.0, torso_roll=0.5, head_roll=-1.0,
        r_shoulder_lift=-2.5, l_shoulder_lift=-0.5,
        r_hand_pos=(-0.16, 1.115, 0.38), r_elbow_pole=(-0.56, 0.91, -0.06),
        blade_dir=(0.19, 0.52, 0.83),
        l_hand_pos=(0.25, 1.275, 0.32), l_elbow_pole=(0.64, 1.08, -0.04),
        l_hand_dir=(-0.32, 0.38, 0.87),
    )
    g2 = _guard(
        hips_y=0.896, hips_yaw=-29.5, hips_roll=1.5,
        torso_yaw=11.5, torso_pitch=5.0, torso_roll=-2.5, head_roll=1.0,
        r_shoulder_lift=-5.5, l_shoulder_lift=-3.5,
        r_hand_pos=(-0.21, 1.050, 0.34), r_elbow_pole=(-0.60, 0.85, -0.10),
        blade_dir=(0.28, 0.40, 0.87),
        l_hand_pos=(0.19, 1.200, 0.28), l_elbow_pole=(0.56, 1.00, -0.10),
        l_hand_dir=(-0.44, 0.22, 0.87),
    )
    g3 = _guard(
        hips_y=0.915, hips_yaw=-24.0, hips_roll=-0.8,
        torso_yaw=15.0, torso_pitch=2.8, torso_roll=-0.5, head_roll=-0.5,
        r_shoulder_lift=-3.2, l_shoulder_lift=-1.2,
        r_hand_pos=(-0.17, 1.095, 0.37), r_elbow_pole=(-0.57, 0.895, -0.07),
        blade_dir=(0.22, 0.48, 0.85),
        l_hand_pos=(0.24, 1.255, 0.31), l_elbow_pole=(0.62, 1.06, -0.05),
        l_hand_dir=(-0.35, 0.34, 0.87),
    )
    return AnimationClip(
        name="wuxia_sword_idle", slot="sword_idle", duration=2.00, loop=True,
        tags=["sword", "locomotion", "idle", "wuxia"],
        description="Ethereal Wudang sword guard with harmonic breathing and drifting Kiếm Quyết.",
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.50, g1, "flow"),
            Keyframe(1.00, g2, "flow"),
            Keyframe(1.50, g3, "flow"),
            Keyframe(2.00, g0, "flow"),
        ],
    )


def make_sword_walk() -> AnimationClip:
    """Kiếm Bộ Tản Hành: Composed floating martial walk — sword trailed back-right, left Kiếm Quyết poised gracefully."""
    base = dict(hips_pitch=5.0, torso_pitch=4.0, head_yaw=0.0, head_pitch=1.0,
                r_shoulder_lift=-6.0, r_shoulder_fwd=-4.0, l_shoulder_lift=-3.0, l_shoulder_fwd=8.0)
    p0 = PoseSpec(
        **base, hips_y=0.900, hips_yaw=6.5, torso_yaw=-5.5, hips_roll=-1.8, torso_roll=2.0, head_roll=-0.5,
        r_hand_pos=(-0.32, 0.94, 0.04), r_elbow_pole=(-0.56, 1.06, -0.34),
        blade_dir=(-0.32, -0.14, -0.94), blade_edge=(0.0, 1.0, 0.0),
        l_hand_pos=(0.28, 1.06, 0.14), l_elbow_pole=(0.56, 1.02, -0.20),
        l_hand_dir=(-0.18, 0.10, 0.98), l_hand_up=(0.15, 0.98, 0.0),
        l_foot_pos=(0.13, 0.085, 0.25), l_knee_pole=(0.16, 0.48, 0.72), l_foot_yaw=4.0, l_foot_pitch=10.0,
        r_foot_pos=(-0.13, 0.095, -0.23), r_knee_pole=(-0.16, 0.44, 0.42), r_foot_yaw=-4.0, r_foot_pitch=-16.0,
    )
    p1 = PoseSpec(
        **base, hips_y=0.934, hips_yaw=0.5, torso_yaw=-0.5, hips_roll=-2.4, torso_roll=2.4, head_roll=-0.5,
        r_hand_pos=(-0.34, 0.965, -0.02), r_elbow_pole=(-0.58, 1.08, -0.38),
        blade_dir=(-0.34, -0.16, -0.93), blade_edge=(0.0, 1.0, 0.0),
        l_hand_pos=(0.25, 1.125, 0.22), l_elbow_pole=(0.55, 1.03, -0.14),
        l_hand_dir=(-0.26, 0.18, 0.95), l_hand_up=(0.15, 0.98, 0.0),
        l_foot_pos=(0.13, 0.080, 0.01), l_knee_pole=(0.15, 0.50, 0.62), l_foot_yaw=2.0, l_foot_pitch=0.0,
        r_foot_pos=(-0.13, 0.170, 0.02), r_knee_pole=(-0.16, 0.50, 0.76), r_foot_yaw=-3.0, r_foot_pitch=-5.0,
    )
    p2 = PoseSpec(
        **base, hips_y=0.900, hips_yaw=-6.5, torso_yaw=5.5, hips_roll=1.8, torso_roll=-2.0, head_roll=0.5,
        r_hand_pos=(-0.35, 0.930, -0.08), r_elbow_pole=(-0.57, 1.06, -0.42),
        blade_dir=(-0.36, -0.18, -0.92), blade_edge=(0.0, 1.0, 0.0),
        l_hand_pos=(0.22, 1.165, 0.28), l_elbow_pole=(0.54, 1.02, -0.08),
        l_hand_dir=(-0.32, 0.24, 0.92), l_hand_up=(0.15, 0.98, 0.0),
        l_foot_pos=(0.13, 0.095, -0.23), l_knee_pole=(0.16, 0.44, 0.42), l_foot_yaw=4.0, l_foot_pitch=-16.0,
        r_foot_pos=(-0.13, 0.085, 0.25), r_knee_pole=(-0.16, 0.48, 0.72), r_foot_yaw=-4.0, r_foot_pitch=10.0,
    )
    p3 = PoseSpec(
        **base, hips_y=0.934, hips_yaw=-0.5, torso_yaw=0.5, hips_roll=2.4, torso_roll=-2.4, head_roll=0.5,
        r_hand_pos=(-0.34, 0.965, -0.02), r_elbow_pole=(-0.58, 1.08, -0.38),
        blade_dir=(-0.34, -0.16, -0.93), blade_edge=(0.0, 1.0, 0.0),
        l_hand_pos=(0.26, 1.110, 0.20), l_elbow_pole=(0.56, 1.03, -0.16),
        l_hand_dir=(-0.24, 0.16, 0.96), l_hand_up=(0.15, 0.98, 0.0),
        l_foot_pos=(0.13, 0.170, 0.02), l_knee_pole=(0.16, 0.50, 0.76), l_foot_yaw=3.0, l_foot_pitch=-5.0,
        r_foot_pos=(-0.13, 0.080, 0.01), r_knee_pole=(-0.15, 0.50, 0.62), r_foot_yaw=-2.0, r_foot_pitch=0.0,
    )
    return AnimationClip(
        name="wuxia_sword_walk", slot="sword_walk", duration=1.05, loop=True,
        tags=["sword", "locomotion", "walk", "wuxia"],
        description="Graceful martial walk with trailing blade and poised left Kiếm Quyết.",
        keyframes=[
            Keyframe(0.00, p0, "flow"), Keyframe(0.2625, p1, "flow"),
            Keyframe(0.525, p2, "flow"), Keyframe(0.7875, p3, "flow"), Keyframe(1.05, p0, "flow"),
        ],
    )


def make_sword_run() -> AnimationClip:
    """Ngự Phong Tật Hành: Buoyant Xianxia Khinh Công sprint — trailing blade right, forward Kiếm Quyết slicing wind."""
    base = dict(hips_pitch=16.0, torso_pitch=16.0, head_yaw=0.0, head_pitch=-3.0,
                r_shoulder_lift=-4.0, r_shoulder_fwd=-16.0, l_shoulder_lift=0.0, l_shoulder_fwd=16.0)
    p0 = PoseSpec(
        **base, hips_y=0.840, hips_yaw=11.0, torso_yaw=-14.0, hips_roll=-2.5, torso_roll=3.0, head_roll=-1.0,
        r_hand_pos=(-0.42, 0.96, -0.22), r_elbow_pole=(-0.62, 1.12, -0.50),
        blade_dir=(-0.50, 0.22, -0.84), blade_edge=(0.0, 1.0, 0.0),
        l_hand_pos=(0.24, 1.12, 0.24), l_elbow_pole=(0.56, 1.02, -0.12),
        l_hand_dir=(-0.22, 0.20, 0.95), l_hand_up=(0.15, 0.98, 0.0),
        l_foot_pos=(0.13, 0.085, 0.34), l_knee_pole=(0.16, 0.48, 0.86), l_foot_yaw=3.0, l_foot_pitch=14.0,
        r_foot_pos=(-0.13, 0.265, -0.34), r_knee_pole=(-0.16, 0.42, 0.24), r_foot_yaw=-4.0, r_foot_pitch=-34.0,
    )
    p1 = PoseSpec(
        **base, hips_y=0.910, hips_yaw=0.0, torso_yaw=-6.0, hips_roll=-3.2, torso_roll=3.2, head_roll=-0.5,
        r_hand_pos=(-0.44, 1.005, -0.26), r_elbow_pole=(-0.64, 1.16, -0.54),
        blade_dir=(-0.52, 0.26, -0.81), blade_edge=(0.0, 1.0, 0.0),
        l_hand_pos=(0.18, 1.185, 0.36), l_elbow_pole=(0.52, 1.05, -0.02),
        l_hand_dir=(-0.28, 0.24, 0.93), l_hand_up=(0.15, 0.98, 0.0),
        l_foot_pos=(0.13, 0.100, -0.08), l_knee_pole=(0.16, 0.46, 0.56), l_foot_yaw=2.0, l_foot_pitch=-12.0,
        r_foot_pos=(-0.13, 0.315, 0.12), r_knee_pole=(-0.16, 0.62, 0.90), r_foot_yaw=-3.0, r_foot_pitch=-10.0,
    )
    p2 = PoseSpec(
        **base, hips_y=0.840, hips_yaw=-11.0, torso_yaw=6.0, hips_roll=2.5, torso_roll=-2.5, head_roll=1.0,
        r_hand_pos=(-0.44, 0.950, -0.28), r_elbow_pole=(-0.64, 1.12, -0.56),
        blade_dir=(-0.52, 0.24, -0.82), blade_edge=(0.0, 1.0, 0.0),
        l_hand_pos=(0.15, 1.205, 0.42), l_elbow_pole=(0.50, 1.05, 0.04),
        l_hand_dir=(-0.30, 0.26, 0.92), l_hand_up=(0.15, 0.98, 0.0),
        l_foot_pos=(0.13, 0.265, -0.34), l_knee_pole=(0.16, 0.42, 0.24), l_foot_yaw=4.0, l_foot_pitch=-34.0,
        r_foot_pos=(-0.13, 0.085, 0.34), r_knee_pole=(-0.16, 0.48, 0.86), r_foot_yaw=-3.0, r_foot_pitch=14.0,
    )
    p3 = PoseSpec(
        **base, hips_y=0.910, hips_yaw=0.0, torso_yaw=-6.0, hips_roll=3.2, torso_roll=-3.0, head_roll=0.5,
        r_hand_pos=(-0.43, 1.005, -0.24), r_elbow_pole=(-0.63, 1.16, -0.52),
        blade_dir=(-0.50, 0.26, -0.83), blade_edge=(0.0, 1.0, 0.0),
        l_hand_pos=(0.21, 1.165, 0.32), l_elbow_pole=(0.54, 1.04, -0.06),
        l_hand_dir=(-0.25, 0.22, 0.94), l_hand_up=(0.15, 0.98, 0.0),
        l_foot_pos=(0.13, 0.315, 0.12), l_knee_pole=(0.16, 0.62, 0.90), l_foot_yaw=3.0, l_foot_pitch=-10.0,
        r_foot_pos=(-0.13, 0.100, -0.08), r_knee_pole=(-0.16, 0.46, 0.56), r_foot_yaw=-2.0, r_foot_pitch=-12.0,
    )
    return AnimationClip(
        name="wuxia_sword_run", slot="sword_run", duration=0.72, loop=True,
        tags=["sword", "locomotion", "run", "wuxia", "khinh_cong"],
        description="Buoyant Wuxia sprint (Ngự Phong Tật Hành) with trailing sword and forward Kiếm Quyết.",
        keyframes=[
            Keyframe(0.00, p0, "flow"), Keyframe(0.18, p1, "flow"),
            Keyframe(0.36, p2, "flow"), Keyframe(0.54, p3, "flow"), Keyframe(0.72, p0, "flow"),
        ],
    )


def make_sword_strafe_left() -> AnimationClip:
    """Kiếm Bộ Dạt Trái: Gliding lock-on sidestep left (+X) with crisp heel/toe contact & supple spine counter-tilt."""
    g0 = _guard(hips_y=0.890, hips_yaw=-20.0, torso_yaw=10.0)
    lift_l = replace(
        g0, hips_y=0.875, hips_yaw=-18.0, hips_roll=-3.0, torso_yaw=9.0, torso_roll=2.5,
        r_hand_pos=(-0.17, 1.075, 0.37), blade_dir=(0.26, 0.45, 0.85),
        l_hand_pos=(0.27, 1.245, 0.27), l_elbow_pole=(0.64, 1.06, -0.08), l_hand_dir=(-0.10, 0.32, 0.94),
        l_foot_pos=(0.27, 0.145, 0.16), l_knee_pole=(0.34, 0.48, 0.74), l_foot_yaw=16.0, l_foot_pitch=8.0,
        r_foot_pos=(-0.18, 0.080, -0.16), r_knee_pole=(-0.24, 0.46, 0.48), r_foot_yaw=-22.0,
    )
    plant_l = replace(
        g0, hips_y=0.848, hips_yaw=-15.0, hips_roll=-6.0, torso_yaw=7.5, torso_roll=5.5, head_roll=-2.0,
        r_hand_pos=(-0.14, 1.045, 0.38), blade_dir=(0.30, 0.42, 0.86),
        l_hand_pos=(0.32, 1.235, 0.23), l_elbow_pole=(0.67, 1.06, -0.12), l_hand_dir=(0.14, 0.34, 0.93),
        l_foot_pos=(0.37, 0.080, 0.15), l_knee_pole=(0.44, 0.46, 0.74), l_foot_yaw=20.0, l_foot_pitch=0.0,
        r_foot_pos=(-0.16, 0.100, -0.15), r_knee_pole=(-0.22, 0.46, 0.48), r_foot_yaw=-20.0, r_foot_pitch=-10.0,
    )
    gather = replace(
        g0, hips_y=0.880, hips_yaw=-22.0, hips_roll=3.5, torso_yaw=11.0, torso_roll=-3.0, head_roll=1.0,
        r_hand_pos=(-0.21, 1.095, 0.35), blade_dir=(0.19, 0.51, 0.84),
        l_hand_pos=(0.20, 1.225, 0.32), l_elbow_pole=(0.58, 1.04, -0.04), l_hand_dir=(-0.38, 0.28, 0.88),
        l_foot_pos=(0.23, 0.080, 0.16), l_knee_pole=(0.30, 0.48, 0.72), l_foot_yaw=14.0,
        r_foot_pos=(-0.05, 0.148, -0.14), r_knee_pole=(-0.12, 0.48, 0.52), r_foot_yaw=-18.0, r_foot_pitch=-8.0,
    )
    return AnimationClip(
        name="wuxia_sword_strafe_left", slot="sword_walk", duration=0.85, loop=True,
        tags=["sword", "locomotion", "strafe", "left"],
        description="Gliding lock-on strafe left with poised sword & balanced left Kiếm Quyết.",
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.18, lift_l, "flow"),
            Keyframe(0.38, plant_l, "flow"),
            Keyframe(0.62, gather, "flow"),
            Keyframe(0.85, g0, "flow"),
        ],
    )


def make_sword_strafe_right() -> AnimationClip:
    """Kiếm Bộ Dạt Phải: Gliding lock-on sidestep right (-X) with crisp heel/toe contact & supple spine counter-tilt."""
    g0 = _guard(hips_y=0.890, hips_yaw=-20.0, torso_yaw=10.0)
    lift_r = replace(
        g0, hips_y=0.875, hips_yaw=-22.0, hips_roll=3.0, torso_yaw=11.0, torso_roll=-2.5,
        r_hand_pos=(-0.22, 1.075, 0.36), blade_dir=(0.20, 0.47, 0.86),
        l_hand_pos=(0.20, 1.245, 0.32), l_elbow_pole=(0.58, 1.05, -0.04), l_hand_dir=(-0.40, 0.31, 0.86),
        l_foot_pos=(0.16, 0.080, 0.16), l_knee_pole=(0.22, 0.46, 0.70), l_foot_yaw=12.0,
        r_foot_pos=(-0.28, 0.145, -0.14), r_knee_pole=(-0.36, 0.48, 0.46), r_foot_yaw=-26.0, r_foot_pitch=8.0,
    )
    plant_r = replace(
        g0, hips_y=0.848, hips_yaw=-25.0, hips_roll=6.0, torso_yaw=12.5, torso_roll=-5.5, head_roll=2.0,
        r_hand_pos=(-0.25, 1.045, 0.35), blade_dir=(0.15, 0.44, 0.88),
        l_hand_pos=(0.17, 1.250, 0.35), l_elbow_pole=(0.55, 1.06, -0.01), l_hand_dir=(-0.46, 0.33, 0.82),
        l_foot_pos=(0.15, 0.100, 0.16), l_knee_pole=(0.22, 0.46, 0.70), l_foot_yaw=12.0, l_foot_pitch=-10.0,
        r_foot_pos=(-0.37, 0.080, -0.14), r_knee_pole=(-0.45, 0.46, 0.46), r_foot_yaw=-32.0, r_foot_pitch=0.0,
    )
    gather = replace(
        g0, hips_y=0.880, hips_yaw=-18.0, hips_roll=-3.5, torso_yaw=9.0, torso_roll=3.0, head_roll=-1.0,
        r_hand_pos=(-0.17, 1.095, 0.37), blade_dir=(0.26, 0.51, 0.82),
        l_hand_pos=(0.28, 1.235, 0.25), l_elbow_pole=(0.64, 1.06, -0.08), l_hand_dir=(-0.10, 0.30, 0.95),
        l_foot_pos=(0.05, 0.148, 0.15), l_knee_pole=(0.12, 0.48, 0.72), l_foot_yaw=10.0, l_foot_pitch=-8.0,
        r_foot_pos=(-0.24, 0.080, -0.16), r_knee_pole=(-0.32, 0.48, 0.46), r_foot_yaw=-24.0,
    )
    return AnimationClip(
        name="wuxia_sword_strafe_right", slot="sword_walk", duration=0.85, loop=True,
        tags=["sword", "locomotion", "strafe", "right"],
        description="Gliding lock-on strafe right with poised sword & balanced left Kiếm Quyết.",
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.18, lift_r, "flow"),
            Keyframe(0.38, plant_r, "flow"),
            Keyframe(0.62, gather, "flow"),
            Keyframe(0.85, g0, "flow"),
        ],
    )


def make_sword_draw() -> AnimationClip:
    """Bạt Kiếm Kiếm Hoa: Unsheathe from back scabbard -> 360-deg Kiếm Hoa wrist twirl -> graceful guard settle."""
    g0 = _guard()
    reach = _guard(
        hips_y=0.925, hips_yaw=-14.0, hips_roll=2.0, torso_yaw=-14.0, torso_pitch=2.0, torso_roll=-2.0,
        head_yaw=-4.0, r_shoulder_lift=16.0, r_shoulder_fwd=-10.0,
        r_hand_pos=(-0.22, 1.54, -0.10), r_elbow_pole=(-0.56, 1.46, 0.12),
        blade_dir=(0.40, -0.82, -0.41), blade_edge=(0.0, 0.45, -0.89), blade_spin_deg=0.0,
        l_hand_pos=(0.12, 1.24, 0.28), l_elbow_pole=(0.52, 1.04, 0.02),
        l_hand_dir=(-0.62, 0.42, 0.66),
    )
    grip_coil = _guard(
        hips_y=0.898, hips_yaw=-22.0, hips_pitch=4.5, hips_roll=4.0,
        torso_yaw=-16.0, torso_pitch=3.0, torso_roll=-3.5,
        r_shoulder_lift=18.0, r_shoulder_fwd=-6.0,
        r_hand_pos=(-0.30, 1.57, -0.02), r_elbow_pole=(-0.60, 1.42, 0.04),
        blade_dir=(0.10, -0.86, -0.50), blade_edge=(0.0, 0.50, -0.86), blade_spin_deg=35.0,
        l_hand_pos=(0.18, 1.28, 0.30), l_elbow_pole=(0.55, 1.06, 0.00),
        l_hand_dir=(-0.40, 0.46, 0.79),
    )
    pull_twirl = _guard(
        hips_y=0.938, hips_yaw=-34.0, hips_pitch=-2.0, hips_roll=5.5,
        torso_yaw=-12.0, torso_pitch=-3.5, torso_roll=-4.5, head_roll=2.0,
        r_shoulder_lift=14.0, r_shoulder_fwd=2.0, l_shoulder_lift=6.0,
        r_hand_pos=(-0.46, 1.48, 0.18), r_elbow_pole=(-0.68, 1.18, -0.10),
        blade_dir=(-0.50, 0.68, 0.54), blade_edge=(0.0, 0.62, -0.78), blade_spin_deg=180.0,
        l_hand_pos=(0.44, 1.38, 0.14), l_elbow_pole=(0.66, 1.12, -0.12),
        l_hand_dir=(0.64, 0.52, 0.56),
        l_foot_pos=(0.18, 0.125, 0.22), l_foot_pitch=6.0,
    )
    flourish_arc = _guard(
        hips_y=0.885, hips_yaw=-29.0, hips_pitch=4.5, hips_roll=-2.0,
        torso_yaw=6.0, torso_pitch=2.5, torso_roll=2.0,
        r_shoulder_lift=1.0, r_shoulder_fwd=10.0,
        r_hand_pos=(-0.34, 1.16, 0.36), r_elbow_pole=(-0.62, 0.92, -0.04),
        blade_dir=(-0.18, 0.44, 0.88), blade_edge=(0.0, 0.89, -0.45), blade_spin_deg=330.0,
        l_hand_pos=(0.32, 1.30, 0.24), l_elbow_pole=(0.63, 1.09, -0.08),
        l_hand_dir=(-0.10, 0.40, 0.91),
        l_foot_pos=(0.17, 0.080, 0.20), r_foot_pos=(-0.20, 0.080, -0.19),
    )
    return AnimationClip(
        name="wuxia_sword_draw", slot="sword_idle", duration=0.85, loop=False,
        tags=["sword", "draw", "kiem_hoa", "wuxia"],
        description="Ethereal unsheathe from back scabbard with 360-deg Kiếm Hoa twirl and flowing left Kiếm Quyết.",
        keyframes=[
            Keyframe(0.00, reach, "flow"),
            Keyframe(0.14, grip_coil, "flow"),
            Keyframe(0.32, pull_twirl, "snap"),
            Keyframe(0.58, flourish_arc, "flow"),
            Keyframe(0.85, replace(g0, blade_spin_deg=360.0), "flow"),
        ],
    )


def make_sword_sheath() -> AnimationClip:
    """Thu Kiếm Kiếm Hoa: Graceful sagittal-plane 360-deg Kiếm Hoa twirl + serene slide into back scabbard."""
    g0 = _guard()
    gather_flick = _guard(
        hips_y=0.900, hips_yaw=-18.0, hips_roll=-2.5, torso_yaw=8.0, torso_roll=2.5,
        r_shoulder_lift=2.0, r_shoulder_fwd=12.0,
        r_hand_pos=(-0.22, 1.18, 0.36), r_elbow_pole=(-0.56, 0.96, 0.02),
        blade_dir=(0.14, 0.62, 0.77), blade_edge=(0.0, 0.78, -0.63), blade_spin_deg=-35.0,
        l_hand_pos=(0.18, 1.26, 0.32), l_elbow_pole=(0.56, 1.06, 0.00),
        l_hand_dir=(-0.46, 0.34, 0.82),
    )
    twirl_out = _guard(
        hips_y=0.925, hips_yaw=-40.0, hips_roll=4.5, torso_yaw=-12.0, torso_pitch=-2.0, torso_roll=-4.0,
        r_shoulder_lift=4.0, r_shoulder_fwd=4.0,
        r_hand_pos=(-0.50, 1.18, 0.24), r_elbow_pole=(-0.68, 1.00, -0.14),
        blade_dir=(-0.12, -0.22, 0.97), blade_edge=(0.0, 0.97, 0.22), blade_spin_deg=-180.0,
        l_hand_pos=(0.14, 1.26, 0.32), l_elbow_pole=(0.54, 1.06, 0.04),
        l_hand_dir=(-0.58, 0.36, 0.73),
    )
    arc_over = _guard(
        hips_y=0.942, hips_yaw=-20.0, hips_roll=2.5, torso_yaw=-14.0, torso_pitch=-3.0, torso_roll=-2.0,
        head_yaw=-3.0, r_shoulder_lift=16.0, r_shoulder_fwd=0.0,
        r_hand_pos=(-0.48, 1.56, 0.14), r_elbow_pole=(-0.68, 1.32, -0.02),
        blade_dir=(-0.22, 0.84, -0.50), blade_edge=(0.0, 0.50, 0.86), blade_spin_deg=-360.0,
        l_hand_pos=(0.34, 1.22, 0.22), l_elbow_pole=(0.62, 1.04, -0.10),
        l_hand_dir=(0.24, 0.22, 0.94),
    )
    align_mouth = _guard(
        hips_y=0.938, hips_yaw=-12.0, torso_yaw=-10.0, torso_pitch=0.5,
        r_shoulder_lift=15.5, r_shoulder_fwd=-7.0,
        r_hand_pos=(-0.30, 1.57, -0.03), r_elbow_pole=(-0.60, 1.42, 0.06),
        blade_dir=(0.18, -0.82, -0.54), blade_edge=(0.0, 0.55, -0.83), blade_spin_deg=-360.0,
        l_hand_pos=(0.30, 1.12, 0.17), l_elbow_pole=(0.58, 1.04, -0.16),
        l_hand_dir=(0.16, -0.10, 0.98),
    )
    seated = _guard(
        hips_y=0.935, hips_yaw=-8.0, torso_yaw=-8.0, torso_pitch=2.0,
        r_shoulder_lift=14.0, r_shoulder_fwd=-10.0,
        r_hand_pos=(-0.22, 1.54, -0.10), r_elbow_pole=(-0.56, 1.46, 0.12),
        blade_dir=(0.40, -0.82, -0.41), blade_edge=(0.0, 0.45, -0.89), blade_spin_deg=-360.0,
        l_hand_pos=(0.28, 1.04, 0.14), l_elbow_pole=(0.56, 1.04, -0.20),
        l_hand_dir=(0.10, -0.35, 0.93),
    )
    return AnimationClip(
        name="wuxia_sword_sheath", slot="sword_idle", duration=0.95, loop=False,
        tags=["sword", "sheath", "kiem_hoa", "wuxia"],
        description="Graceful Kiếm Hoa sword-flower twirl followed by serene sheathing into back scabbard.",
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.14, gather_flick, "flow"),
            Keyframe(0.32, twirl_out, "whip"),
            Keyframe(0.58, arc_over, "flow"),
            Keyframe(0.78, align_mouth, "flow"),
            Keyframe(0.95, seated, "flow"),
        ],
    )


# ==============================================================================
# 2. FLUID WUXIA COMBO, SPIN-CUTS, AERIAL LEAPS & FLURRIES (11 CLIPS)
# ==============================================================================

def make_sword_slash_r2l() -> AnimationClip:
    """Beat 1 — Thanh Phong Phất Liễu: Flowing stepping right-to-left crescent slash + soaring left crane-wing unfurl."""
    g0 = _guard()
    windup = _guard(
        hips_y=0.865, hips_yaw=-48.0, hips_pitch=6.0, hips_roll=5.0,
        torso_yaw=-24.0, torso_pitch=5.0, torso_roll=-4.5, head_roll=2.0,
        r_shoulder_lift=6.0, r_shoulder_fwd=-14.0, l_shoulder_lift=2.0, l_shoulder_fwd=14.0,
        r_hand_pos=(-0.46, 1.20, -0.04), r_elbow_pole=(-0.64, 0.98, -0.44),
        blade_dir=(-0.86, 0.18, -0.48), blade_edge=(0.0, 0.22, 0.98),
        l_hand_pos=(0.10, 1.24, 0.40), l_elbow_pole=(0.52, 1.02, 0.10),
        l_hand_dir=(-0.22, 0.16, 0.96),
        l_foot_pos=(0.18, 0.115, 0.22), l_foot_pitch=8.0,
        r_foot_pos=(-0.22, 0.080, -0.22),
    )
    mid_cut = _guard(
        hips_y=0.835, hips_yaw=14.0, hips_pitch=10.0, hips_roll=-3.5,
        torso_yaw=14.0, torso_pitch=8.0, torso_roll=3.5,
        r_shoulder_lift=0.0, r_shoulder_fwd=22.0, l_shoulder_lift=4.0, l_shoulder_fwd=-6.0,
        r_hand_pos=(0.04, 1.14, 0.51), r_elbow_pole=(-0.44, 0.92, 0.14),
        blade_dir=(0.20, 0.04, 0.98), blade_edge=(0.98, 0.0, -0.20),
        l_hand_pos=(0.44, 1.28, 0.04), l_elbow_pole=(0.66, 1.08, -0.24),
        l_hand_dir=(0.82, 0.28, 0.50),
        l_foot_pos=(0.18, 0.080, 0.28), r_foot_pos=(-0.22, 0.100, -0.22), r_foot_pitch=-14.0,
    )
    follow = _guard(
        hips_y=0.860, hips_yaw=42.0, hips_pitch=6.0, hips_roll=-6.0,
        torso_yaw=23.0, torso_pitch=5.0, torso_roll=5.5, head_roll=-2.0,
        r_shoulder_lift=-2.0, r_shoulder_fwd=24.0, l_shoulder_lift=8.0, l_shoulder_fwd=-16.0,
        r_hand_pos=(0.34, 1.08, 0.34), r_elbow_pole=(-0.08, 0.88, 0.38),
        blade_dir=(0.90, -0.04, 0.43), blade_edge=(0.43, 0.0, -0.90),
        l_hand_pos=(0.48, 1.38, -0.22), l_elbow_pole=(0.66, 1.14, -0.42),
        l_hand_dir=(0.76, 0.44, -0.48),
        l_foot_pos=(0.18, 0.080, 0.28), r_foot_pos=(-0.20, 0.115, -0.18), r_foot_pitch=-16.0,
    )
    follow_drift = _guard(
        hips_y=0.882, hips_yaw=34.0, hips_pitch=5.0, hips_roll=-3.0,
        torso_yaw=18.0, torso_pitch=4.0, torso_roll=2.5,
        r_hand_pos=(0.24, 1.12, 0.40), r_elbow_pole=(-0.22, 0.90, 0.26),
        blade_dir=(0.74, 0.16, 0.65), blade_edge=(0.65, 0.15, -0.74),
        l_hand_pos=(0.42, 1.36, -0.06), l_elbow_pole=(0.65, 1.12, -0.28),
        l_hand_dir=(0.50, 0.42, 0.76),
    )
    return AnimationClip(
        name="wuxia_sword_slash_r2l", slot="sword_slash", duration=0.75, loop=False,
        tags=["sword", "combat", "slash", "combo1"],
        description="Beat 1 graceful stepping horizontal crescent cleave with sweeping left crane-wing Kiếm Quyết.",
        hit_window=(0.20, 0.38),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.15, windup, "flow"),
            Keyframe(0.27, mid_cut, "snap"),
            Keyframe(0.41, follow, "flow"),
            Keyframe(0.56, follow_drift, "flow"),
            Keyframe(0.75, g0, "flow"),
        ],
    )


def make_sword_slash_l2r() -> AnimationClip:
    """Beat 2 — Hồi Phong Toàn Kiếm: Silky 360-deg pivot spin-cut across +Z with centrifugal lean & left-arm spiral."""
    def _sp(yaw: float, hy: float, r_pos, r_el, b_dir, b_edge, l_pos, l_el, l_dir,
            lf_pos, rf_pos, pitch: float = 6.0, hroll: float = 0.0, tyaw: float = 12.0,
            troll: float = 0.0, rf_pitch: float = 0.0) -> PoseSpec:
        return PoseSpec(
            hips_y=hy, hips_yaw=yaw, hips_pitch=pitch, hips_roll=hroll,
            torso_yaw=tyaw, torso_pitch=pitch, torso_roll=troll,
            head_yaw=0.0, head_pitch=1.0, head_roll=-troll * 0.4, head_world_space=False,
            l_shoulder_lift=5.0, l_shoulder_fwd=4.0, r_shoulder_lift=2.0, r_shoulder_fwd=12.0,
            r_hand_pos=r_pos, r_elbow_pole=r_el, blade_dir=b_dir, blade_edge=b_edge,
            l_hand_pos=l_pos, l_elbow_pole=l_el, l_hand_dir=l_dir, l_hand_up=(0.1, 0.98, 0.0),
            l_foot_pos=lf_pos, l_knee_pole=(0.22, 0.48, 0.72), l_foot_yaw=10.0,
            r_foot_pos=rf_pos, r_knee_pole=(-0.28, 0.48, 0.44), r_foot_yaw=-20.0, r_foot_pitch=rf_pitch,
            local_body_space=True, feet_in_body_space=True,
        )

    p0 = _sp(-24.0, 0.905, (-0.18, 1.08, 0.36), (-0.58, 0.88, -0.08), (0.24, 0.46, 0.85), (0.0, 1.0, 0.0),
             (0.22, 1.24, 0.30), (0.60, 1.04, -0.06), (-0.38, 0.30, 0.87),
             (0.16, 0.08, 0.18), (-0.19, 0.08, -0.18), pitch=4.0, hroll=0.0, tyaw=14.0, troll=-1.5)
    p1 = _sp(-108.0, 0.935, (-0.30, 1.12, 0.34), (-0.60, 0.94, -0.10), (-0.42, 0.14, 0.90), (0.90, 0.0, 0.42),
             (0.42, 1.36, 0.12), (0.66, 1.14, -0.18), (0.68, 0.42, 0.60),
             (0.14, 0.095, 0.14), (-0.21, 0.21, -0.20), pitch=7.0, hroll=6.0, tyaw=-12.0, troll=-4.5, rf_pitch=-18.0)
    p2 = _sp(-206.0, 0.950, (-0.36, 1.16, 0.30), (-0.62, 0.96, -0.14), (-0.62, 0.08, 0.78), (0.78, 0.0, 0.62),
             (0.48, 1.40, 0.00), (0.68, 1.18, -0.26), (0.82, 0.44, 0.36),
             (0.14, 0.110, 0.12), (-0.22, 0.25, -0.22), pitch=9.0, hroll=8.0, tyaw=-18.0, troll=-6.0, rf_pitch=-24.0)
    p3 = _sp(-306.0, 0.892, (-0.20, 1.14, 0.44), (-0.56, 0.92, 0.04), (-0.42, 0.18, 0.89), (-0.89, 0.15, -0.42),
             (0.42, 1.34, -0.14), (0.66, 1.12, -0.36), (0.82, 0.36, -0.44),
             (0.16, 0.110, 0.22), (-0.20, 0.09, -0.18), pitch=10.0, hroll=5.0, tyaw=-22.0, troll=-4.0, rf_pitch=-8.0)
    p4 = _sp(-396.0, 0.878, (-0.44, 1.28, 0.24), (-0.66, 1.00, -0.18), (-0.80, 0.34, 0.49), (-0.49, 0.20, -0.85),
             (0.24, 1.26, 0.32), (0.58, 1.06, -0.02), (-0.34, 0.36, 0.87),
             (0.16, 0.080, 0.20), (-0.20, 0.08, -0.20), pitch=5.0, hroll=2.0, tyaw=-12.0, troll=-1.5, rf_pitch=0.0)
    p5 = _sp(-384.0, 0.905, (-0.18, 1.08, 0.36), (-0.58, 0.88, -0.08), (0.24, 0.46, 0.85), (0.0, 1.0, 0.0),
             (0.22, 1.24, 0.30), (0.60, 1.04, -0.06), (-0.38, 0.30, 0.87),
             (0.16, 0.080, 0.18), (-0.19, 0.08, -0.18), pitch=4.0, hroll=0.0, tyaw=14.0, troll=-1.5)
    return AnimationClip(
        name="wuxia_sword_slash_l2r", slot="sword_slash", duration=0.82, loop=False,
        tags=["sword", "combat", "slash", "spin", "combo2"],
        description="Beat 2 ethereal 360-deg pivot reverse spin-cut (Hồi Phong Toàn Kiếm) with left-hand spiral.",
        hit_window=(0.30, 0.56),
        keyframes=[
            Keyframe(0.00, p0, "flow"),
            Keyframe(0.16, p1, "flow"),
            Keyframe(0.32, p2, "flow"),
            Keyframe(0.48, p3, "snap"),
            Keyframe(0.65, p4, "flow"),
            Keyframe(0.82, p5, "flow"),
        ],
    )


def make_sword_thrust() -> AnimationClip:
    """Beat 3 — Phi Yến Xuyên Vân: Weightless Khinh Công skimming swallow-glide lunge with soaring left Kiếm Quyết."""
    g0 = _guard()
    coil = _guard(
        hips_y=0.835, hips_yaw=-44.0, hips_pitch=10.0, hips_roll=4.5,
        torso_yaw=-14.0, torso_pitch=8.0, torso_roll=-3.5,
        r_shoulder_lift=-4.0, r_shoulder_fwd=-14.0, l_shoulder_lift=2.0, l_shoulder_fwd=14.0,
        r_hand_pos=(-0.28, 1.04, 0.06), r_elbow_pole=(-0.54, 0.92, -0.40),
        blade_dir=(-0.02, 0.05, 0.998), blade_edge=(0.0, 1.0, 0.0),
        l_hand_pos=(0.06, 1.18, 0.42), l_elbow_pole=(0.48, 0.98, 0.12),
        l_hand_dir=(-0.10, 0.12, 0.99),
        l_foot_pos=(0.16, 0.080, 0.22), r_foot_pos=(-0.22, 0.095, -0.24), r_foot_pitch=-10.0,
    )
    glide_lunge = _guard(
        hips_y=0.828, hips_yaw=18.0, hips_pitch=18.0, hips_roll=-5.0,
        torso_yaw=16.0, torso_pitch=16.0, torso_roll=4.5, head_roll=-1.5,
        head_pitch=-5.0, r_shoulder_lift=2.0, r_shoulder_fwd=26.0, l_shoulder_lift=8.0, l_shoulder_fwd=-16.0,
        r_hand_pos=(-0.06, 1.14, 0.57), r_elbow_pole=(-0.40, 0.98, 0.18),
        blade_dir=(0.00, 0.02, 1.00), blade_edge=(0.0, 1.0, 0.0),
        l_hand_pos=(0.48, 1.40, -0.24), l_elbow_pole=(0.66, 1.18, -0.40),
        l_hand_dir=(0.74, 0.46, -0.49),
        l_foot_pos=(0.14, 0.080, 0.34), l_knee_pole=(0.18, 0.44, 0.92), l_foot_yaw=6.0,
        r_foot_pos=(-0.20, 0.260, -0.36), r_knee_pole=(-0.26, 0.44, 0.16), r_foot_yaw=-26.0, r_foot_pitch=-32.0,
    )
    lunge_drift = _guard(
        hips_y=0.845, hips_yaw=12.0, hips_pitch=14.0, hips_roll=-3.0,
        torso_yaw=13.0, torso_pitch=12.0, torso_roll=2.5,
        r_shoulder_fwd=22.0, l_shoulder_lift=6.0,
        r_hand_pos=(-0.09, 1.15, 0.53), r_elbow_pole=(-0.44, 0.96, 0.14),
        blade_dir=(0.04, 0.10, 0.994), blade_edge=(0.0, 0.995, -0.10),
        l_hand_pos=(0.44, 1.38, -0.14), l_elbow_pole=(0.66, 1.16, -0.34),
        l_hand_dir=(0.60, 0.44, -0.66),
        l_foot_pos=(0.15, 0.080, 0.30), r_foot_pos=(-0.20, 0.140, -0.28), r_foot_pitch=-18.0,
    )
    settle = _guard(
        hips_y=0.878, hips_yaw=-10.0, hips_pitch=6.0, hips_roll=-1.0,
        torso_yaw=11.0, torso_pitch=5.0, torso_roll=0.0,
        r_hand_pos=(-0.15, 1.10, 0.40), blade_dir=(0.16, 0.32, 0.93),
        l_hand_pos=(0.30, 1.28, 0.16), l_elbow_pole=(0.62, 1.08, -0.14), l_hand_dir=(-0.08, 0.34, 0.94),
        l_foot_pos=(0.16, 0.080, 0.22), r_foot_pos=(-0.19, 0.080, -0.20),
    )
    return AnimationClip(
        name="wuxia_sword_thrust", slot="sword_thrust", duration=0.78, loop=False,
        tags=["sword", "combat", "thrust", "khinh_cong", "combo3"],
        description="Beat 3 weightless Khinh Công swallow-glide lunge thrust (Phi Yến Xuyên Vân).",
        hit_window=(0.22, 0.42),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.15, coil, "flow"),
            Keyframe(0.32, glide_lunge, "snap"),
            Keyframe(0.45, lunge_drift, "flow"),
            Keyframe(0.60, settle, "flow"),
            Keyframe(0.78, g0, "flow"),
        ],
    )


def make_sword_slash_diag() -> AnimationClip:
    """Du Long Đằng Không: Buoyant soaring leap-cut rising along a wide arc into crane-tucked sky crescent."""
    g0 = _guard()
    coil = _guard(
        hips_y=0.785, hips_yaw=36.0, hips_pitch=16.0, hips_roll=-5.5,
        torso_yaw=22.0, torso_pitch=14.0, torso_roll=5.0,
        r_shoulder_lift=-8.0, r_shoulder_fwd=20.0, l_shoulder_lift=4.0, l_shoulder_fwd=-12.0,
        r_hand_pos=(0.24, 0.85, 0.32), r_elbow_pole=(-0.10, 0.84, 0.36),
        blade_dir=(0.74, -0.42, 0.52), blade_edge=(-0.42, 0.72, 0.55),
        l_hand_pos=(0.44, 1.18, -0.18), l_elbow_pole=(0.66, 1.04, -0.40), l_hand_dir=(0.82, 0.24, -0.52),
        l_foot_pos=(0.18, 0.080, 0.22), r_foot_pos=(-0.22, 0.080, -0.22),
    )
    rise_arc = _guard(
        hips_y=0.915, hips_yaw=14.0, hips_pitch=9.0, hips_roll=-2.0,
        torso_yaw=8.0, torso_pitch=6.0, torso_roll=2.0,
        r_shoulder_lift=0.0, r_shoulder_fwd=20.0,
        r_hand_pos=(0.12, 1.06, 0.46), r_elbow_pole=(-0.28, 0.90, 0.30),
        blade_dir=(0.34, -0.04, 0.94), blade_edge=(-0.62, 0.75, 0.23),
        l_hand_pos=(0.38, 1.32, 0.04), l_elbow_pole=(0.65, 1.12, -0.24), l_hand_dir=(0.56, 0.46, 0.69),
        l_foot_pos=(0.17, 0.160, 0.22), l_foot_pitch=-12.0,
        r_foot_pos=(-0.21, 0.110, -0.22), r_foot_pitch=-16.0,
    )
    soar_mid = _guard(
        hips_y=1.095, hips_yaw=-10.0, hips_pitch=1.0, hips_roll=3.0,
        torso_yaw=-8.0, torso_pitch=-5.0, torso_roll=-2.5,
        r_shoulder_lift=11.0, r_shoulder_fwd=16.0, l_shoulder_lift=8.0,
        r_hand_pos=(-0.04, 1.42, 0.49), r_elbow_pole=(-0.44, 1.12, 0.16),
        blade_dir=(-0.16, 0.44, 0.88), blade_edge=(-0.70, 0.68, -0.22),
        l_hand_pos=(0.30, 1.50, 0.24), l_elbow_pole=(0.64, 1.26, -0.06), l_hand_dir=(0.24, 0.64, 0.73),
        l_foot_pos=(0.16, 0.430, 0.20), l_knee_pole=(0.24, 0.82, 0.84), l_foot_pitch=-22.0,
        r_foot_pos=(-0.20, 0.240, -0.22), r_knee_pole=(-0.28, 0.64, 0.36), r_foot_pitch=-28.0,
    )
    soar_apex = _guard(
        hips_y=1.155, hips_yaw=-38.0, hips_pitch=-4.0, hips_roll=5.5,
        torso_yaw=-18.0, torso_pitch=-8.0, torso_roll=-4.5, head_roll=2.0,
        head_pitch=-6.0, r_shoulder_lift=18.0, r_shoulder_fwd=4.0, l_shoulder_lift=10.0,
        r_hand_pos=(-0.38, 1.72, 0.24), r_elbow_pole=(-0.64, 1.40, -0.08),
        blade_dir=(-0.56, 0.76, 0.33), blade_edge=(-0.56, -0.60, 0.57),
        l_hand_pos=(0.36, 1.52, 0.28), l_elbow_pole=(0.64, 1.28, -0.02), l_hand_dir=(-0.10, 0.52, 0.85),
        l_foot_pos=(0.18, 0.480, 0.22), l_knee_pole=(0.26, 0.88, 0.86), l_foot_pitch=-24.0,
        r_foot_pos=(-0.20, 0.300, -0.24), r_knee_pole=(-0.28, 0.70, 0.34), r_foot_pitch=-30.0,
    )
    land_absorb = _guard(
        hips_y=0.845, hips_yaw=-30.0, hips_pitch=8.0, hips_roll=2.0,
        torso_yaw=4.0, torso_pitch=7.0, torso_roll=-1.5,
        r_hand_pos=(-0.26, 1.22, 0.34), r_elbow_pole=(-0.60, 0.98, -0.08),
        blade_dir=(-0.10, 0.60, 0.79),
        l_hand_pos=(0.26, 1.26, 0.30), l_elbow_pole=(0.60, 1.06, -0.04), l_hand_dir=(-0.26, 0.38, 0.89),
        l_foot_pos=(0.17, 0.080, 0.20), r_foot_pos=(-0.20, 0.080, -0.20),
    )
    return AnimationClip(
        name="wuxia_sword_slash_diag", slot="sword_slash", duration=0.82, loop=False,
        tags=["sword", "combat", "slash", "rising", "leap"],
        description="Buoyant soaring rising diagonal cleave (Du Long Đằng Không) with crane knee tuck & soft landing.",
        hit_window=(0.24, 0.48),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.16, coil, "flow"),
            Keyframe(0.26, rise_arc, "snap"),
            Keyframe(0.39, soar_mid, "flow"),
            Keyframe(0.53, soar_apex, "flow"),
            Keyframe(0.68, land_absorb, "heavy_drop"),
            Keyframe(0.82, g0, "flow"),
        ],
    )


def make_sword_heavy_cleave() -> AnimationClip:
    """Beat 4 Finisher — Thiên Ngoại Phi Tiên: Soaring Khinh Công leap overhead cleave + organic Zanshin landing."""
    g0 = _guard()
    crouch = _guard(
        hips_y=0.765, hips_yaw=-26.0, hips_pitch=15.0, hips_roll=3.0,
        torso_yaw=-12.0, torso_pitch=11.0, torso_roll=-2.5,
        r_shoulder_lift=-6.0, l_shoulder_lift=-4.0,
        r_hand_pos=(-0.34, 0.94, 0.20), r_elbow_pole=(-0.60, 0.86, -0.14),
        blade_dir=(-0.36, 0.20, 0.91),
        l_hand_pos=(0.32, 1.04, 0.20), l_elbow_pole=(0.62, 0.94, -0.14), l_hand_dir=(-0.15, 0.10, 0.98),
    )
    leap_rise = _guard(
        hips_y=1.100, hips_yaw=-34.0, hips_pitch=2.0, hips_roll=4.5,
        torso_yaw=-16.0, torso_pitch=-4.0, torso_roll=-3.5,
        r_shoulder_lift=14.0, r_shoulder_fwd=4.0, l_shoulder_lift=10.0,
        r_hand_pos=(-0.38, 1.66, 0.22), r_elbow_pole=(-0.64, 1.40, 0.06),
        blade_dir=(-0.44, 0.72, -0.54), blade_edge=(0.0, 0.60, 0.80),
        l_hand_pos=(0.38, 1.54, 0.26), l_elbow_pole=(0.66, 1.30, -0.04), l_hand_dir=(0.20, 0.64, 0.74),
        l_foot_pos=(0.16, 0.38, 0.18), l_knee_pole=(0.24, 0.80, 0.78), l_foot_pitch=-20.0,
        r_foot_pos=(-0.20, 0.28, -0.22), r_knee_pole=(-0.28, 0.68, 0.38), r_foot_pitch=-26.0,
    )
    leap_raise = _guard(
        hips_y=1.335, hips_yaw=-38.0, hips_pitch=-6.0, hips_roll=5.5,
        torso_yaw=-18.0, torso_pitch=-14.0, torso_roll=-4.5, head_roll=2.0,
        head_pitch=6.0, r_shoulder_lift=22.0, r_shoulder_fwd=6.0, l_shoulder_lift=14.0,
        r_hand_pos=(-0.24, 2.09, 0.14), r_elbow_pole=(-0.56, 1.87, 0.22),
        blade_dir=(-0.24, 0.78, -0.58), blade_edge=(0.0, 0.59, 0.81),
        l_hand_pos=(0.40, 1.85, 0.26), l_elbow_pole=(0.68, 1.57, -0.04), l_hand_dir=(0.28, 0.74, 0.61),
        l_foot_pos=(0.16, 0.65, 0.18), l_knee_pole=(0.24, 1.07, 0.80), l_foot_pitch=-26.0,
        r_foot_pos=(-0.20, 0.51, -0.24), r_knee_pole=(-0.28, 0.91, 0.36), r_foot_pitch=-32.0,
    )
    descend_cut = _guard(
        hips_y=0.980, hips_yaw=12.0, hips_pitch=14.0, hips_roll=-3.5,
        torso_yaw=12.0, torso_pitch=16.0, torso_roll=3.0,
        r_shoulder_lift=4.0, r_shoulder_fwd=24.0, l_shoulder_lift=8.0, l_shoulder_fwd=-8.0,
        r_hand_pos=(0.00, 1.38, 0.51), r_elbow_pole=(-0.38, 1.24, 0.24),
        blade_dir=(0.10, 0.20, 0.97), blade_edge=(0.12, -0.97, 0.20),
        l_hand_pos=(0.44, 1.34, -0.06), l_elbow_pole=(0.68, 1.18, -0.32), l_hand_dir=(0.76, 0.34, -0.55),
        l_foot_pos=(0.18, 0.20, 0.26), l_knee_pole=(0.24, 0.60, 0.86), l_foot_pitch=-8.0,
        r_foot_pos=(-0.22, 0.18, -0.24), r_knee_pole=(-0.30, 0.56, 0.34), r_foot_pitch=-14.0,
    )
    impact_low = _guard(
        hips_y=0.750, hips_yaw=38.0, hips_pitch=19.0, hips_roll=-6.5,
        torso_yaw=22.0, torso_pitch=19.0, torso_roll=5.5, head_roll=-2.0,
        head_pitch=-6.0, r_shoulder_lift=-6.0, r_shoulder_fwd=24.0, l_shoulder_lift=8.0, l_shoulder_fwd=-16.0,
        r_hand_pos=(0.28, 0.85, 0.38), r_elbow_pole=(-0.08, 0.88, 0.38),
        blade_dir=(0.56, -0.38, 0.74), blade_edge=(0.22, -0.84, -0.49),
        l_hand_pos=(0.48, 1.30, -0.28), l_elbow_pole=(0.66, 1.10, -0.48), l_hand_dir=(0.72, 0.42, -0.55),
        l_foot_pos=(0.18, 0.080, 0.30), l_knee_pole=(0.24, 0.44, 0.88),
        r_foot_pos=(-0.22, 0.080, -0.26), r_knee_pole=(-0.32, 0.42, 0.30),
    )
    impact_drift = _guard(
        hips_y=0.775, hips_yaw=34.0, hips_pitch=16.0, hips_roll=-4.5,
        torso_yaw=19.0, torso_pitch=15.0, torso_roll=4.0,
        head_pitch=-4.0, r_shoulder_lift=-4.0, r_shoulder_fwd=22.0, l_shoulder_lift=6.0,
        # Moving Zanshin hold: blade glides subtly outward as knees absorb landing shock
        r_hand_pos=(0.30, 0.89, 0.39), r_elbow_pole=(-0.06, 0.90, 0.36),
        blade_dir=(0.60, -0.28, 0.75), blade_edge=(0.22, -0.88, -0.42),
        l_hand_pos=(0.49, 1.34, -0.24), l_elbow_pole=(0.66, 1.13, -0.44), l_hand_dir=(0.74, 0.46, -0.49),
        l_foot_pos=(0.18, 0.080, 0.29), r_foot_pos=(-0.22, 0.080, -0.25),
    )
    return AnimationClip(
        name="wuxia_sword_heavy_cleave", slot="sword_slash", duration=1.05, loop=False,
        tags=["sword", "combat", "slash", "heavy", "leap", "finisher", "combo4"],
        description="Beat 4 Finisher soaring Khinh Công overhead cleave (Thiên Ngoại Phi Tiên) with Zanshin landing.",
        hit_window=(0.44, 0.64),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.13, crouch, "flow"),
            Keyframe(0.25, leap_rise, "snap"),
            Keyframe(0.39, leap_raise, "flow"),
            Keyframe(0.52, descend_cut, "heavy_drop"),
            Keyframe(0.65, impact_low, "snap", tension=0.25),
            Keyframe(0.81, impact_drift, "flow"),
            Keyframe(1.05, g0, "flow"),
        ],
    )


def make_sword_cross_x_slash() -> AnimationClip:
    """Song Yến Thập Tự: Buoyant hop-step weaving continuous figure-8 X-cross slashes with wide outer arcs."""
    g0 = _guard()
    hop_hi = _guard(
        hips_y=1.020, hips_yaw=-40.0, hips_roll=5.0, torso_yaw=-18.0, torso_pitch=-4.0, torso_roll=-4.0,
        r_shoulder_lift=16.0, r_shoulder_fwd=-2.0, l_shoulder_lift=6.0,
        r_hand_pos=(-0.36, 1.64, 0.16), r_elbow_pole=(-0.64, 1.36, -0.04),
        blade_dir=(-0.54, 0.72, -0.44), blade_edge=(0.0, 0.52, 0.85),
        l_hand_pos=(0.24, 1.44, 0.32), l_elbow_pole=(0.60, 1.20, 0.00), l_hand_dir=(-0.24, 0.48, 0.84),
        l_foot_pos=(0.16, 0.24, 0.20), r_foot_pos=(-0.20, 0.18, -0.18), l_foot_pitch=-14.0, r_foot_pitch=-18.0,
    )
    cut1_mid = _guard(
        hips_y=0.880, hips_yaw=4.0, hips_pitch=8.0, hips_roll=-2.5,
        torso_yaw=6.0, torso_pitch=8.0, torso_roll=2.5,
        r_shoulder_lift=3.0, r_shoulder_fwd=22.0,
        # Forward outer crescent breakdown prevents linear arm-across-chest shortcut
        r_hand_pos=(-0.02, 1.22, 0.50), r_elbow_pole=(-0.42, 0.98, 0.20),
        blade_dir=(0.22, 0.08, 0.97), blade_edge=(0.68, -0.71, -0.17),
        l_hand_pos=(0.38, 1.36, 0.02), l_elbow_pole=(0.65, 1.14, -0.24), l_hand_dir=(0.64, 0.36, 0.68),
        l_foot_pos=(0.17, 0.10, 0.24), r_foot_pos=(-0.21, 0.09, -0.20),
    )
    cut1 = _guard(
        hips_y=0.810, hips_yaw=34.0, hips_pitch=13.0, hips_roll=-6.5,
        torso_yaw=20.0, torso_pitch=14.0, torso_roll=5.5,
        r_shoulder_lift=-6.0, r_shoulder_fwd=24.0, l_shoulder_lift=8.0, l_shoulder_fwd=-14.0,
        r_hand_pos=(0.28, 0.88, 0.38), r_elbow_pole=(-0.08, 0.88, 0.38),
        blade_dir=(0.68, -0.40, 0.61), blade_edge=(0.28, -0.62, -0.73),
        l_hand_pos=(0.48, 1.32, -0.22), l_elbow_pole=(0.66, 1.10, -0.44), l_hand_dir=(0.76, 0.40, -0.51),
        l_foot_pos=(0.18, 0.080, 0.26), r_foot_pos=(-0.22, 0.080, -0.22),
    )
    scoop_turn = _guard(
        hips_y=0.835, hips_yaw=16.0, hips_pitch=10.0, hips_roll=-2.5,
        torso_yaw=8.0, torso_pitch=9.0, torso_roll=2.0,
        r_shoulder_lift=-3.0, r_shoulder_fwd=20.0,
        # Silky bottom loop of figure-8 reverses blade edge fluidly
        r_hand_pos=(0.18, 0.94, 0.46), r_elbow_pole=(-0.22, 0.86, 0.32),
        blade_dir=(0.40, -0.16, 0.90), blade_edge=(-0.62, 0.68, 0.39),
        l_hand_pos=(0.38, 1.26, 0.04), l_elbow_pole=(0.64, 1.06, -0.22), l_hand_dir=(0.36, 0.34, 0.87),
        l_foot_pos=(0.17, 0.085, 0.24), r_foot_pos=(-0.21, 0.080, -0.21),
    )
    cut2_mid = _guard(
        hips_y=0.905, hips_yaw=-10.0, hips_pitch=5.0, hips_roll=3.0,
        torso_yaw=-8.0, torso_pitch=3.0, torso_roll=-2.5,
        r_shoulder_lift=6.0, r_shoulder_fwd=18.0,
        r_hand_pos=(-0.06, 1.24, 0.50), r_elbow_pole=(-0.46, 0.96, 0.14),
        blade_dir=(-0.18, 0.28, 0.94), blade_edge=(-0.68, 0.68, -0.26),
        l_hand_pos=(0.26, 1.28, 0.24), l_elbow_pole=(0.60, 1.06, -0.12), l_hand_dir=(-0.24, 0.36, 0.90),
        l_foot_pos=(0.16, 0.140, 0.22), r_foot_pos=(-0.20, 0.080, -0.20),
    )
    cut2_hi = _guard(
        hips_y=0.960, hips_yaw=-40.0, hips_pitch=0.0, hips_roll=6.0,
        torso_yaw=-20.0, torso_pitch=-4.0, torso_roll=-5.0, head_roll=2.0,
        r_shoulder_lift=16.0, r_shoulder_fwd=-6.0,
        r_hand_pos=(-0.44, 1.54, 0.20), r_elbow_pole=(-0.68, 1.22, -0.14),
        blade_dir=(-0.68, 0.62, 0.39), blade_edge=(-0.50, -0.78, 0.38),
        l_hand_pos=(0.20, 1.30, 0.34), l_elbow_pole=(0.56, 1.08, 0.02), l_hand_dir=(-0.38, 0.34, 0.86),
    )
    return AnimationClip(
        name="wuxia_sword_cross_x_slash", slot="sword_slash", duration=0.90, loop=False,
        tags=["sword", "combat", "slash", "double", "x_slash"],
        description="Buoyant hop-step twin figure-8 X-cross slashes (Song Yến Thập Tự) with full 3D arcs.",
        hit_window=(0.20, 0.64),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.16, hop_hi, "flow"),
            Keyframe(0.26, cut1_mid, "snap"),
            Keyframe(0.35, cut1, "flow"),
            Keyframe(0.44, scoop_turn, "flow"),
            Keyframe(0.54, cut2_mid, "whip"),
            Keyframe(0.67, cut2_hi, "flow"),
            Keyframe(0.90, g0, "flow"),
        ],
    )


def make_sword_flurry_thrust() -> AnimationClip:
    """Liên Hoàn Tam Kiếm: Fluid stepping 3-hit piercing flurry with spiral retracts & Kiếm Quyết counter-sweeps."""
    g0 = _guard()
    c1 = _guard(
        hips_y=0.845, hips_yaw=-36.0, hips_pitch=10.0, hips_roll=4.0,
        torso_yaw=-13.0, torso_pitch=8.0, torso_roll=-3.0,
        r_shoulder_fwd=-10.0,
        r_hand_pos=(-0.26, 1.03, 0.10), r_elbow_pole=(-0.53, 0.90, -0.36),
        blade_dir=(-0.02, 0.04, 0.999), blade_edge=(0.0, 1.0, 0.0),
        l_hand_pos=(0.14, 1.20, 0.36), l_elbow_pole=(0.54, 1.00, 0.06), l_hand_dir=(-0.20, 0.18, 0.96),
        r_foot_pos=(-0.22, 0.095, -0.22), r_foot_pitch=-10.0,
    )
    s1 = _guard(
        hips_y=0.822, hips_yaw=11.0, hips_pitch=12.0, hips_roll=-3.5,
        torso_yaw=10.0, torso_pitch=12.0, torso_roll=3.0,
        r_shoulder_fwd=23.0, l_shoulder_lift=5.0, l_shoulder_fwd=-10.0,
        r_hand_pos=(-0.08, 1.06, 0.53), r_elbow_pole=(-0.40, 0.92, 0.14),
        blade_dir=(0.04, -0.02, 0.999), blade_edge=(0.0, 1.0, 0.0),
        l_hand_pos=(0.40, 1.28, -0.08), l_elbow_pole=(0.66, 1.08, -0.32), l_hand_dir=(0.72, 0.34, -0.60),
        l_foot_pos=(0.16, 0.080, 0.26), r_foot_pos=(-0.22, 0.080, -0.24),
    )
    c2 = replace(
        c1, hips_y=0.870, hips_yaw=-26.0, hips_roll=3.5, torso_yaw=-10.0, torso_roll=-3.0,
        r_hand_pos=(-0.24, 1.19, 0.16), r_elbow_pole=(-0.54, 1.00, -0.32),
        blade_dir=(0.08, 0.15, 0.985),
        l_hand_pos=(0.16, 1.24, 0.34), l_hand_dir=(-0.22, 0.24, 0.94),
    )
    s2 = replace(
        s1, hips_y=0.855, hips_yaw=16.0, hips_roll=-4.5, torso_yaw=13.0, torso_roll=4.0,
        r_shoulder_lift=4.0, r_shoulder_fwd=24.0,
        r_hand_pos=(0.02, 1.26, 0.53), r_elbow_pole=(-0.34, 1.04, 0.16),
        blade_dir=(0.12, 0.16, 0.980),
        l_hand_pos=(0.44, 1.34, -0.14), l_elbow_pole=(0.66, 1.12, -0.36), l_hand_dir=(0.74, 0.42, -0.52),
        l_foot_pos=(0.15, 0.080, 0.29), r_foot_pos=(-0.22, 0.095, -0.26), r_foot_pitch=-12.0,
    )
    c3 = replace(
        c1, hips_y=0.812, hips_yaw=-42.0, hips_pitch=12.0, hips_roll=5.5,
        torso_yaw=-18.0, torso_pitch=10.0, torso_roll=-4.5,
        r_shoulder_fwd=-14.0,
        r_hand_pos=(-0.28, 0.99, 0.06), r_elbow_pole=(-0.56, 0.86, -0.42),
        blade_dir=(0.0, 0.02, 1.0),
        l_hand_pos=(0.12, 1.18, 0.38),
        r_foot_pos=(-0.22, 0.110, -0.28), r_foot_pitch=-14.0,
    )
    s3 = _guard(
        hips_y=0.800, hips_yaw=22.0, hips_pitch=16.0, hips_roll=-5.5,
        torso_yaw=17.0, torso_pitch=16.0, torso_roll=5.0,
        r_shoulder_fwd=26.0, l_shoulder_lift=8.0, l_shoulder_fwd=-16.0,
        r_hand_pos=(-0.05, 1.12, 0.57), r_elbow_pole=(-0.38, 0.96, 0.18),
        blade_dir=(0.0, 0.02, 1.0), blade_edge=(0.0, 1.0, 0.0),
        l_hand_pos=(0.48, 1.40, -0.24), l_elbow_pole=(0.66, 1.18, -0.40), l_hand_dir=(0.74, 0.46, -0.49),
        l_foot_pos=(0.14, 0.080, 0.34), r_foot_pos=(-0.20, 0.225, -0.34), r_foot_pitch=-28.0,
    )
    s3_drift = replace(
        s3, hips_y=0.825, hips_yaw=14.0, hips_pitch=12.0, hips_roll=-3.0,
        torso_yaw=12.0, torso_pitch=12.0, torso_roll=2.5,
        r_hand_pos=(-0.08, 1.13, 0.53), blade_dir=(0.05, 0.12, 0.991),
        l_hand_pos=(0.43, 1.38, -0.14),
        r_foot_pos=(-0.20, 0.120, -0.26), r_foot_pitch=-16.0,
    )
    return AnimationClip(
        name="wuxia_sword_flurry_thrust", slot="sword_thrust", duration=0.98, loop=False,
        tags=["sword", "combat", "thrust", "flurry", "multi_hit"],
        description="Fluid 3-hit piercing thrust flurry (Liên Hoàn Tam Kiếm) with spiral retracts & Zanshin drift.",
        hit_window=(0.18, 0.76),
        keyframes=[
            Keyframe(0.00, g0, "flow"), Keyframe(0.11, c1, "flow"), Keyframe(0.22, s1, "snap"),
            Keyframe(0.33, c2, "flow"), Keyframe(0.45, s2, "snap"), Keyframe(0.56, c3, "flow"),
            Keyframe(0.70, s3, "snap"), Keyframe(0.81, s3_drift, "flow"), Keyframe(0.98, g0, "flow"),
        ],
    )


def make_sword_uppercut_launch() -> AnimationClip:
    """Long Đằng Tứ Hải: Low crouched coil sweeping through a wide rising arc into a crane-leap uppercut."""
    g0 = _guard()
    crouch = _guard(
        hips_y=0.735, hips_yaw=-40.0, hips_pitch=18.0, hips_roll=6.0,
        torso_yaw=-16.0, torso_pitch=16.0, torso_roll=-5.0,
        head_pitch=-6.0, r_shoulder_lift=-10.0, r_shoulder_fwd=4.0,
        r_hand_pos=(-0.32, 0.73, 0.20), r_elbow_pole=(-0.59, 0.86, -0.14),
        blade_dir=(-0.56, -0.44, 0.70), blade_edge=(0.0, 0.84, 0.54),
        l_hand_pos=(0.24, 0.98, 0.30), l_elbow_pole=(0.58, 0.88, -0.06), l_hand_dir=(-0.22, 0.14, 0.96),
        l_foot_pos=(0.18, 0.080, 0.24), r_foot_pos=(-0.22, 0.080, -0.24),
    )
    rise_arc = _guard(
        hips_y=0.875, hips_yaw=-14.0, hips_pitch=10.0, hips_roll=1.5,
        torso_yaw=-4.0, torso_pitch=6.0, torso_roll=-1.0,
        r_shoulder_lift=2.0, r_shoulder_fwd=18.0,
        r_hand_pos=(-0.16, 1.04, 0.46), r_elbow_pole=(-0.52, 0.88, 0.12),
        blade_dir=(-0.18, -0.02, 0.98), blade_edge=(0.0, 1.0, 0.02),
        l_hand_pos=(0.34, 1.18, 0.16), l_elbow_pole=(0.62, 1.02, -0.14), l_hand_dir=(0.34, 0.34, 0.88),
        l_foot_pos=(0.17, 0.140, 0.23), l_foot_pitch=-12.0,
        r_foot_pos=(-0.21, 0.110, -0.22), r_foot_pitch=-16.0,
    )
    surge = _guard(
        hips_y=1.080, hips_yaw=10.0, hips_pitch=0.0, hips_roll=-3.0,
        torso_yaw=10.0, torso_pitch=-6.0, torso_roll=2.5,
        r_shoulder_lift=14.0, r_shoulder_fwd=22.0, l_shoulder_lift=8.0, l_shoulder_fwd=-8.0,
        r_hand_pos=(-0.02, 1.46, 0.49), r_elbow_pole=(-0.40, 1.16, 0.22),
        blade_dir=(0.06, 0.56, 0.82), blade_edge=(0.0, 0.82, -0.56),
        l_hand_pos=(0.42, 1.46, 0.02), l_elbow_pole=(0.66, 1.24, -0.24), l_hand_dir=(0.68, 0.52, -0.52),
        l_foot_pos=(0.16, 0.390, 0.22), l_knee_pole=(0.22, 0.79, 0.84), l_foot_pitch=-22.0,
        r_foot_pos=(-0.20, 0.230, -0.20), r_knee_pole=(-0.28, 0.63, 0.36), r_foot_pitch=-28.0,
    )
    sky_apex = _guard(
        hips_y=1.245, hips_yaw=28.0, hips_pitch=-6.0, hips_roll=-5.0,
        torso_yaw=16.0, torso_pitch=-12.0, torso_roll=4.5, head_roll=-2.0,
        head_pitch=-10.0, r_shoulder_lift=24.0, r_shoulder_fwd=16.0, l_shoulder_lift=12.0, l_shoulder_fwd=-14.0,
        r_hand_pos=(0.08, 1.94, 0.34), r_elbow_pole=(-0.28, 1.64, 0.28),
        blade_dir=(0.14, 0.90, 0.41), blade_edge=(0.0, 0.41, -0.91),
        l_hand_pos=(0.48, 1.62, -0.20), l_elbow_pole=(0.68, 1.38, -0.38), l_hand_dir=(0.72, 0.52, -0.46),
        l_foot_pos=(0.16, 0.540, 0.20), l_knee_pole=(0.22, 0.94, 0.84), l_foot_pitch=-26.0,
        r_foot_pos=(-0.20, 0.360, -0.22), r_knee_pole=(-0.28, 0.76, 0.34), r_foot_pitch=-32.0,
    )
    land_absorb = _guard(
        hips_y=0.835, hips_yaw=10.0, hips_pitch=9.0, hips_roll=-2.0,
        torso_yaw=10.0, torso_pitch=8.0, torso_roll=1.5,
        r_hand_pos=(-0.06, 1.30, 0.38), r_elbow_pole=(-0.44, 1.02, 0.12),
        blade_dir=(0.18, 0.68, 0.71),
        l_hand_pos=(0.34, 1.28, 0.10), l_elbow_pole=(0.62, 1.08, -0.18), l_hand_dir=(0.22, 0.40, 0.89),
        l_foot_pos=(0.17, 0.080, 0.20), r_foot_pos=(-0.20, 0.080, -0.19),
    )
    return AnimationClip(
        name="wuxia_sword_uppercut_launch", slot="sword_slash", duration=0.88, loop=False,
        tags=["sword", "combat", "slash", "uppercut", "leap", "launcher"],
        description="Soaring crane-leap uppercut launcher (Long Đằng Tứ Hải) with wide arc and soft landing.",
        hit_window=(0.24, 0.50),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.17, crouch, "flow"),
            Keyframe(0.27, rise_arc, "snap"),
            Keyframe(0.39, surge, "flow"),
            Keyframe(0.54, sky_apex, "flow"),
            Keyframe(0.71, land_absorb, "heavy_drop"),
            Keyframe(0.88, g0, "flow"),
        ],
    )


def make_sword_whirlwind_360() -> AnimationClip:
    """Thanh Liên Kiếm Vũ: Buoyant AIRBORNE 360-deg Khinh Công lotus cyclone with centrifugal lean & flying wing."""
    def _lotus(yaw: float, hy: float, b_ax: float, b_ay: float, r_z: float,
               lf_y: float, rf_y: float, lf_z: float, rf_z: float,
               pitch: float = 8.0, hroll: float = 0.0, tyaw: float = 16.0, troll: float = 0.0) -> PoseSpec:
        bz = max(0.22, (max(0.01, 1.0 - b_ax * b_ax - b_ay * b_ay)) ** 0.5)
        return PoseSpec(
            hips_y=hy, hips_yaw=yaw, hips_pitch=pitch, hips_roll=hroll,
            torso_yaw=tyaw, torso_pitch=pitch, torso_roll=troll,
            head_yaw=0.0, head_pitch=1.0, head_roll=-troll * 0.4, head_world_space=False,
            l_shoulder_lift=8.0, l_shoulder_fwd=-6.0, r_shoulder_lift=4.0, r_shoulder_fwd=10.0,
            r_hand_pos=(-0.40, hy + 0.22, r_z), r_elbow_pole=(-0.64, hy + 0.08, -0.08),
            blade_dir=(b_ax, b_ay, bz), blade_edge=(bz, 0.0, -b_ax),
            l_hand_pos=(0.50, hy + 0.34, -0.10), l_elbow_pole=(0.66, hy + 0.14, -0.28),
            l_hand_dir=(0.88, 0.36, -0.30), l_hand_up=(0.0, 0.98, 0.20),
            l_foot_pos=(0.16, lf_y, lf_z), l_knee_pole=(0.24, lf_y + 0.40, lf_z + 0.56),
            l_foot_yaw=12.0, l_foot_pitch=-22.0 if lf_y > 0.14 else 0.0,
            r_foot_pos=(-0.18, rf_y, rf_z), r_knee_pole=(-0.26, rf_y + 0.38, rf_z + 0.46),
            r_foot_yaw=-20.0, r_foot_pitch=-28.0 if rf_y > 0.14 else 0.0,
            local_body_space=True, feet_in_body_space=True,
        )

    p0 = _lotus(-24.0, 0.905, 0.24, 0.38, 0.36, 0.08, 0.08, 0.18, -0.18, pitch=4.0, tyaw=14.0)
    p_coil = _lotus(-64.0, 0.780, -0.84, -0.06, 0.18, 0.08, 0.08, 0.18, -0.18, pitch=12.0, hroll=6.0, tyaw=-14.0, troll=-4.5)
    p_air1 = _lotus(35.0, 1.150, -0.70, 0.02, 0.38, 0.42, 0.26, 0.22, -0.24, pitch=10.0, hroll=-7.0, tyaw=18.0, troll=5.5)
    p_air2 = _lotus(145.0, 1.280, -0.66, 0.10, 0.38, 0.58, 0.38, 0.22, -0.24, pitch=10.0, hroll=-8.5, tyaw=20.0, troll=6.5)
    p_air3 = _lotus(252.0, 1.190, -0.64, 0.00, 0.38, 0.46, 0.28, 0.22, -0.24, pitch=10.0, hroll=-6.5, tyaw=17.0, troll=5.0)
    p_land = _lotus(350.0, 0.835, 0.34, 0.12, 0.37, 0.08, 0.08, 0.20, -0.18, pitch=9.0, hroll=-2.0, tyaw=12.0, troll=1.5)
    p_end = _lotus(336.0, 0.905, 0.24, 0.38, 0.36, 0.08, 0.08, 0.18, -0.18, pitch=4.0, tyaw=14.0)
    return AnimationClip(
        name="wuxia_sword_whirlwind_360", slot="sword_slash", duration=0.92, loop=False,
        tags=["sword", "combat", "slash", "spin", "aerial", "aoe", "360"],
        description="Airborne 360-deg Khinh Công lotus cyclone (Thanh Liên Kiếm Vũ) with silky centrifugal arc.",
        hit_window=(0.20, 0.68),
        keyframes=[
            Keyframe(0.00, p0, "flow"),
            Keyframe(0.14, p_coil, "flow"),
            Keyframe(0.28, p_air1, "snap"),
            Keyframe(0.43, p_air2, "flow"),
            Keyframe(0.58, p_air3, "flow"),
            Keyframe(0.75, p_land, "heavy_drop"),
            Keyframe(0.92, p_end, "flow"),
        ],
    )


def make_sword_dash_slash() -> AnimationClip:
    """Lăng Ba Lưu Tinh: Weightless low cloud-skimming glide bursting into a wide sweeping crescent & Zanshin drift."""
    g0 = _guard()
    skim = _guard(
        hips_y=0.770, hips_yaw=-46.0, hips_pitch=22.0, hips_roll=6.0,
        torso_yaw=-16.0, torso_pitch=20.0, torso_roll=-5.0,
        head_pitch=-8.0, r_shoulder_lift=-4.0, r_shoulder_fwd=-18.0, l_shoulder_lift=2.0, l_shoulder_fwd=16.0,
        r_hand_pos=(-0.40, 0.90, -0.18), r_elbow_pole=(-0.62, 1.06, -0.48),
        blade_dir=(-0.46, 0.08, -0.88), blade_edge=(0.0, 1.0, 0.0),
        l_hand_pos=(0.14, 1.08, 0.42), l_elbow_pole=(0.52, 0.94, 0.10), l_hand_dir=(-0.14, 0.12, 0.98),
        l_foot_pos=(0.14, 0.080, 0.30), l_knee_pole=(0.18, 0.42, 0.90),
        r_foot_pos=(-0.20, 0.245, -0.36), r_knee_pole=(-0.26, 0.42, 0.16), r_foot_pitch=-32.0,
    )
    flash_mid = _guard(
        hips_y=0.780, hips_yaw=-6.0, hips_pitch=20.0, hips_roll=-2.0,
        torso_yaw=4.0, torso_pitch=18.0, torso_roll=2.0,
        head_pitch=-7.0, r_shoulder_lift=-2.0, r_shoulder_fwd=20.0, l_shoulder_lift=4.0, l_shoulder_fwd=-4.0,
        # Outer sweeping mid-arc clears torso gracefully
        r_hand_pos=(-0.08, 0.98, 0.50), r_elbow_pole=(-0.48, 0.88, 0.16),
        blade_dir=(0.10, 0.06, 0.99), blade_edge=(0.99, 0.0, -0.10),
        l_hand_pos=(0.36, 1.18, 0.06), l_elbow_pole=(0.64, 1.02, -0.22), l_hand_dir=(0.60, 0.28, 0.75),
        l_foot_pos=(0.14, 0.080, 0.33), r_foot_pos=(-0.21, 0.160, -0.34), r_foot_pitch=-24.0,
    )
    flash = _guard(
        hips_y=0.795, hips_yaw=34.0, hips_pitch=18.0, hips_roll=-6.5,
        torso_yaw=22.0, torso_pitch=16.0, torso_roll=5.5, head_roll=-2.0,
        head_pitch=-6.0, r_shoulder_lift=0.0, r_shoulder_fwd=24.0, l_shoulder_lift=8.0, l_shoulder_fwd=-16.0,
        r_hand_pos=(0.25, 1.00, 0.46), r_elbow_pole=(-0.18, 0.90, 0.36),
        blade_dir=(0.76, 0.08, 0.65), blade_edge=(0.65, 0.0, -0.76),
        l_hand_pos=(0.48, 1.28, -0.22), l_elbow_pole=(0.66, 1.08, -0.44), l_hand_dir=(0.76, 0.40, -0.51),
        l_foot_pos=(0.14, 0.080, 0.34), r_foot_pos=(-0.22, 0.120, -0.32), r_foot_pitch=-18.0,
    )
    flash_drift = _guard(
        hips_y=0.825, hips_yaw=28.0, hips_pitch=13.0, hips_roll=-4.0,
        torso_yaw=16.0, torso_pitch=11.0, torso_roll=3.0,
        r_hand_pos=(0.20, 1.05, 0.43), r_elbow_pole=(-0.24, 0.92, 0.30),
        blade_dir=(0.68, 0.20, 0.71), blade_edge=(0.71, 0.12, -0.69),
        l_hand_pos=(0.44, 1.30, -0.12), l_elbow_pole=(0.65, 1.10, -0.34), l_hand_dir=(0.62, 0.42, -0.66),
        l_foot_pos=(0.15, 0.080, 0.29), r_foot_pos=(-0.21, 0.085, -0.26), r_foot_pitch=-8.0,
    )
    return AnimationClip(
        name="wuxia_sword_dash_slash", slot="sword_slash", duration=0.78, loop=False,
        tags=["sword", "combat", "dash", "slash", "khinh_cong"],
        description="Weightless cloud-skimming flash dash-slash (Lăng Ba Lưu Tinh) with wide outer crescent.",
        hit_window=(0.20, 0.42),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.17, skim, "flow"),
            Keyframe(0.28, flash_mid, "snap"),
            Keyframe(0.40, flash, "flow"),
            Keyframe(0.56, flash_drift, "flow"),
            Keyframe(0.78, g0, "flow"),
        ],
    )


def make_sword_jump_slash() -> AnimationClip:
    """Phượng Vũ Cửu Thiên: High Khinh Công vault + silky 360-deg corkscrew spin + descending radiant cleave."""
    def _air(yaw: float, hy: float, r_pos, r_el, b_dir, b_edge, l_pos, l_el, l_dir,
             lf_y: float, rf_y: float, pitch: float = 6.0, hroll: float = 0.0,
             tyaw: float = 0.0, troll: float = 0.0) -> PoseSpec:
        return PoseSpec(
            hips_y=hy, hips_yaw=yaw, hips_pitch=pitch, hips_roll=hroll,
            torso_yaw=tyaw, torso_pitch=pitch, torso_roll=troll,
            head_yaw=0.0, head_pitch=4.0, head_roll=-troll * 0.4, head_world_space=False,
            l_shoulder_lift=10.0, l_shoulder_fwd=4.0, r_shoulder_lift=14.0, r_shoulder_fwd=10.0,
            r_hand_pos=r_pos, r_elbow_pole=r_el, blade_dir=b_dir, blade_edge=b_edge,
            l_hand_pos=l_pos, l_elbow_pole=l_el, l_hand_dir=l_dir, l_hand_up=(0.1, 0.98, 0.0),
            l_foot_pos=(0.16, lf_y, 0.22), l_knee_pole=(0.22, lf_y + 0.42, 0.80),
            l_foot_yaw=10.0, l_foot_pitch=-24.0 if lf_y > 0.14 else 0.0,
            r_foot_pos=(-0.20, rf_y, -0.22), r_knee_pole=(-0.28, rf_y + 0.38, 0.36),
            r_foot_yaw=-24.0, r_foot_pitch=-30.0 if rf_y > 0.14 else 0.0,
            local_body_space=True, feet_in_body_space=True,
        )

    p0 = _air(-24.0, 0.905, (-0.18, 1.08, 0.36), (-0.58, 0.88, -0.08), (0.24, 0.46, 0.85), (0.0, 1.0, 0.0),
              (0.22, 1.24, 0.30), (0.60, 1.04, -0.06), (-0.38, 0.30, 0.87), 0.08, 0.08, pitch=4.0, tyaw=14.0)
    p_coil = _air(-32.0, 0.740, (-0.32, 0.92, 0.22), (-0.60, 0.84, -0.14), (-0.30, 0.26, 0.92), (0.0, 1.0, 0.0),
                  (0.32, 1.04, 0.18), (0.60, 0.94, -0.16), (-0.10, 0.15, 0.98), 0.08, 0.08, pitch=16.0, hroll=4.0, tyaw=6.0, troll=-3.0)
    p_spin1 = _air(72.0, 1.260, (-0.40, 1.66, 0.22), (-0.66, 1.42, -0.06), (-0.52, 0.56, 0.64), (0.64, 0.0, 0.52),
                   (0.46, 1.62, -0.02), (0.68, 1.40, -0.24), (0.78, 0.46, 0.42), 0.52, 0.38, pitch=2.0, hroll=-7.0, tyaw=14.0, troll=5.5)
    p_spin2 = _air(198.0, 1.455, (-0.34, 2.02, 0.18), (-0.62, 1.76, -0.02), (-0.38, 0.78, -0.50), (0.0, 0.54, 0.84),
                   (0.46, 1.88, -0.08), (0.68, 1.64, -0.28), (0.76, 0.52, -0.39), 0.74, 0.58, pitch=-6.0, hroll=-6.0, tyaw=-14.0, troll=4.5)
    p_drop = _air(342.0, 1.170, (-0.04, 1.56, 0.50), (-0.42, 1.40, 0.24), (0.08, 0.24, 0.97), (0.0, -0.97, 0.24),
                  (0.44, 1.54, -0.08), (0.66, 1.36, -0.32), (0.76, 0.36, -0.54), 0.38, 0.28, pitch=14.0, hroll=-3.0, tyaw=14.0, troll=2.5)
    p_land = _air(378.0, 0.745, (0.28, 0.85, 0.38), (-0.08, 0.88, 0.38), (0.56, -0.36, 0.75), (0.20, -0.86, -0.47),
                  (0.48, 1.30, -0.26), (0.66, 1.10, -0.46), (0.74, 0.42, -0.52), 0.08, 0.08, pitch=18.0, hroll=-5.0, tyaw=20.0, troll=4.5)
    p_settle = _air(366.0, 0.790, (0.24, 0.93, 0.40), (-0.14, 0.91, 0.32), (0.52, -0.18, 0.83), (0.18, -0.92, -0.34),
                    (0.42, 1.30, -0.14), (0.64, 1.10, -0.34), (0.56, 0.42, -0.71), 0.08, 0.08, pitch=13.0, hroll=-2.5, tyaw=16.0, troll=2.0)
    p_end = _air(336.0, 0.905, (-0.18, 1.08, 0.36), (-0.58, 0.88, -0.08), (0.24, 0.46, 0.85), (0.0, 1.0, 0.0),
                 (0.22, 1.24, 0.30), (0.60, 1.04, -0.06), (-0.38, 0.30, 0.87), 0.08, 0.08, pitch=4.0, tyaw=14.0)
    return AnimationClip(
        name="wuxia_sword_jump_slash", slot="sword_jump_slash", duration=1.00, loop=False,
        tags=["sword", "combat", "aerial", "spin", "jump_slash"],
        description="High Khinh Công vault + silky 360-deg corkscrew spin + descending cleave (Phượng Vũ Cửu Thiên).",
        hit_window=(0.48, 0.72),
        keyframes=[
            Keyframe(0.00, p0, "flow"),
            Keyframe(0.13, p_coil, "flow"),
            Keyframe(0.27, p_spin1, "snap"),
            Keyframe(0.42, p_spin2, "flow"),
            Keyframe(0.56, p_drop, "heavy_drop"),
            Keyframe(0.69, p_land, "snap", tension=0.25),
            Keyframe(0.82, p_settle, "flow"),
            Keyframe(1.00, p_end, "flow"),
        ],
    )


# ==============================================================================
# 3. SIGNATURE WUXIA SWORD SKILLS (5 CLIPS: CAST, IAIDO CHARGE/SLASH, PLUNGE, SKY SUMMON)
# ==============================================================================

def make_sword_cast() -> AnimationClip:
    """Kiếm Quyết Ngự Kiếm: Vertical sword seal -> left Kiếm Quyết sweeps Qi up blade -> twin forward launch."""
    g0 = _guard()
    seal = _guard(
        hips_y=0.922, hips_yaw=-10.0, hips_pitch=1.0, hips_roll=-1.5,
        torso_yaw=6.0, torso_pitch=-1.0, torso_roll=1.5,
        r_shoulder_lift=2.0, r_shoulder_fwd=16.0, l_shoulder_lift=2.0, l_shoulder_fwd=14.0,
        r_hand_pos=(-0.05, 1.12, 0.37), r_elbow_pole=(-0.44, 0.94, 0.14),
        blade_dir=(0.00, 0.99, 0.14), blade_edge=(1.0, 0.0, 0.0),
        l_hand_pos=(0.06, 1.20, 0.38), l_elbow_pole=(0.46, 0.98, 0.14), l_hand_dir=(-0.50, 0.72, 0.48),
    )
    trace_up = _guard(
        hips_y=0.960, hips_yaw=-4.0, hips_pitch=-2.5, hips_roll=1.5,
        torso_yaw=4.0, torso_pitch=-6.5, torso_roll=-1.5,
        head_pitch=-4.0, r_shoulder_lift=6.0, r_shoulder_fwd=16.0, l_shoulder_lift=12.0, l_shoulder_fwd=16.0,
        r_hand_pos=(-0.05, 1.16, 0.38), r_elbow_pole=(-0.44, 0.96, 0.16),
        blade_dir=(0.00, 0.99, 0.14), blade_edge=(1.0, 0.0, 0.0),
        l_hand_pos=(0.09, 1.58, 0.39), l_elbow_pole=(0.50, 1.26, 0.16), l_hand_dir=(-0.22, 0.90, 0.38),
    )
    release = _guard(
        hips_y=0.855, hips_yaw=16.0, hips_pitch=10.0, hips_roll=-3.5,
        torso_yaw=12.0, torso_pitch=10.0, torso_roll=3.0,
        r_shoulder_lift=4.0, r_shoulder_fwd=22.0, l_shoulder_lift=6.0, l_shoulder_fwd=22.0,
        r_hand_pos=(-0.24, 1.16, 0.49), r_elbow_pole=(-0.56, 0.94, 0.08),
        blade_dir=(-0.16, 0.18, 0.97), blade_edge=(0.0, 0.98, -0.18),
        l_hand_pos=(0.16, 1.32, 0.53), l_elbow_pole=(0.50, 1.08, 0.16), l_hand_dir=(-0.08, 0.12, 0.99),
        l_foot_pos=(0.16, 0.080, 0.26), r_foot_pos=(-0.22, 0.080, -0.22),
    )
    release_drift = replace(
        release, hips_y=0.870, hips_yaw=12.0, hips_pitch=8.0, hips_roll=-2.0,
        torso_yaw=10.0, torso_pitch=7.0, torso_roll=1.5,
        r_hand_pos=(-0.24, 1.18, 0.47), blade_dir=(-0.12, 0.24, 0.96),
        l_hand_pos=(0.18, 1.34, 0.50), l_hand_dir=(-0.12, 0.18, 0.98),
    )
    return AnimationClip(
        name="wuxia_sword_cast", slot="sword_cast", duration=1.05, loop=False,
        tags=["sword", "combat", "cast", "qi", "skill"],
        description="Xianxia Sword-Qi invocation (Kiếm Quyết Ngự Kiếm) with vertical blade Qi trace & forward launch.",
        hit_window=(0.56, 0.78),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.22, seal, "flow"),
            Keyframe(0.48, trace_up, "flow"),
            Keyframe(0.66, release, "snap"),
            Keyframe(0.83, release_drift, "flow"),
            Keyframe(1.05, g0, "flow"),
        ],
    )


def _iaido_coil(**overrides) -> PoseSpec:
    base = _guard(
        hips_y=0.775, hips_yaw=32.0, hips_pitch=16.0, hips_roll=-3.5,
        torso_yaw=16.0, torso_pitch=16.0, torso_roll=3.0,
        head_yaw=0.0, head_pitch=-4.0, r_shoulder_lift=-8.0, r_shoulder_fwd=20.0,
        r_hand_pos=(0.20, 0.84, 0.24), r_elbow_pole=(-0.18, 0.86, 0.28),
        blade_dir=(0.44, -0.06, -0.89), blade_edge=(0.0, 1.0, 0.0), blade_spin_deg=0.0,
        l_hand_pos=(0.28, 0.86, 0.14), l_elbow_pole=(0.56, 0.92, -0.14), l_hand_dir=(0.35, -0.10, -0.93),
        l_foot_pos=(0.18, 0.080, 0.28), l_knee_pole=(0.24, 0.42, 0.86), l_foot_yaw=16.0,
        r_foot_pos=(-0.22, 0.080, -0.26), r_knee_pole=(-0.32, 0.42, 0.30), r_foot_yaw=-30.0,
    )
    return replace(base, **overrides) if overrides else base


def make_sword_iaido_charge() -> AnimationClip:
    """Bạt Kiếm Tụ Lực: Living 4-phase crouched Iaido Qi-tension breathing loop."""
    c0 = _iaido_coil()
    c_sink = _iaido_coil(
        hips_y=0.762, hips_pitch=17.5, torso_pitch=17.5,
        r_hand_pos=(0.21, 0.830, 0.23), l_hand_pos=(0.285, 0.850, 0.13),
    )
    c_tense = _iaido_coil(
        hips_y=0.752, hips_yaw=34.0, hips_pitch=19.0, torso_yaw=18.0, torso_pitch=19.5,
        r_shoulder_lift=-9.0,
        r_hand_pos=(0.22, 0.818, 0.215), l_hand_pos=(0.295, 0.838, 0.115),
    )
    c_rise = _iaido_coil(
        hips_y=0.766, hips_pitch=17.0, torso_pitch=17.0,
        r_hand_pos=(0.205, 0.834, 0.232), l_hand_pos=(0.282, 0.854, 0.132),
    )
    return AnimationClip(
        name="wuxia_sword_iaido_charge", slot="sword_cast", duration=0.90, loop=True,
        tags=["sword", "skill", "iaido", "charge"],
        description="Coiled crouched Iaido draw-charge stance (Bạt Kiếm Tụ Lực) with living Qi breathing.",
        keyframes=[
            Keyframe(0.00, c0, "flow"),
            Keyframe(0.24, c_sink, "flow"),
            Keyframe(0.48, c_tense, "flow"),
            Keyframe(0.70, c_rise, "flow"),
            Keyframe(0.90, c0, "flow"),
        ],
    )


def make_sword_iaido_draw_slash() -> AnimationClip:
    """Nhất Kiếm Đoạn Thiên: Micro-coil tension -> explosive Saya-biki flash draw -> Zanshin drift -> Kiếm Hoa finish."""
    g0 = _guard()
    c0 = _iaido_coil()
    c_micro = _iaido_coil(
        hips_y=0.754, hips_yaw=34.5, hips_pitch=19.0, torso_yaw=18.5, torso_pitch=19.0,
        r_hand_pos=(0.22, 0.82, 0.22), l_hand_pos=(0.295, 0.84, 0.12),
    )
    flash_mid = _guard(
        hips_y=0.805, hips_yaw=-10.0, hips_pitch=14.0, hips_roll=2.5,
        torso_yaw=-10.0, torso_pitch=12.0, torso_roll=-2.0,
        r_shoulder_lift=0.0, r_shoulder_fwd=18.0, l_shoulder_lift=2.0, l_shoulder_fwd=-14.0,
        r_hand_pos=(-0.06, 1.08, 0.51), r_elbow_pole=(-0.46, 0.92, 0.12),
        blade_dir=(-0.20, 0.04, 0.98), blade_edge=(-0.98, 0.0, -0.20), blade_spin_deg=0.0,
        l_hand_pos=(0.42, 1.00, -0.24), l_elbow_pole=(0.64, 0.98, -0.46), l_hand_dir=(0.74, 0.10, -0.66),
        l_foot_pos=(0.18, 0.080, 0.30), r_foot_pos=(-0.22, 0.120, -0.28), r_foot_pitch=-16.0,
    )
    flash_end = _guard(
        hips_y=0.850, hips_yaw=-46.0, hips_pitch=5.0, hips_roll=6.5,
        torso_yaw=-24.0, torso_pitch=3.0, torso_roll=-5.0, head_roll=2.0,
        r_shoulder_lift=6.0, r_shoulder_fwd=-14.0,
        r_hand_pos=(-0.48, 1.22, 0.12), r_elbow_pole=(-0.66, 1.00, -0.28),
        blade_dir=(-0.86, 0.18, 0.48), blade_edge=(-0.48, 0.0, -0.86), blade_spin_deg=0.0,
        l_hand_pos=(0.24, 1.22, 0.32), l_elbow_pole=(0.56, 1.02, 0.00), l_hand_dir=(-0.34, 0.30, 0.89),
    )
    zanshin_drift = _guard(
        hips_y=0.868, hips_yaw=-49.0, hips_pitch=3.5, hips_roll=5.0,
        torso_yaw=-26.0, torso_pitch=1.5, torso_roll=-3.5,
        r_shoulder_lift=7.0, r_shoulder_fwd=-16.0,
        r_hand_pos=(-0.50, 1.25, 0.08), r_elbow_pole=(-0.68, 1.02, -0.30),
        blade_dir=(-0.90, 0.22, 0.38), blade_edge=(-0.39, 0.0, -0.92), blade_spin_deg=45.0,
        l_hand_pos=(0.22, 1.24, 0.33), l_elbow_pole=(0.56, 1.04, 0.01), l_hand_dir=(-0.36, 0.34, 0.87),
    )
    twirl_finish = _guard(
        hips_y=0.900, hips_yaw=-32.0, hips_roll=1.5, torso_yaw=8.0, torso_roll=-1.0,
        r_hand_pos=(-0.32, 1.14, 0.34), r_elbow_pole=(-0.60, 0.92, -0.06),
        blade_dir=(-0.12, 0.40, 0.91), blade_edge=(0.0, 0.91, -0.40), blade_spin_deg=360.0,
        l_hand_pos=(0.24, 1.25, 0.30), l_elbow_pole=(0.60, 1.06, -0.04), l_hand_dir=(-0.36, 0.32, 0.87),
    )
    return AnimationClip(
        name="wuxia_sword_iaido_draw_slash", slot="sword_slash", duration=0.92, loop=False,
        tags=["sword", "skill", "iaido", "draw_slash", "kiem_hoa", "ultimate"],
        description="Explosive Iaido flash-draw slash (Nhất Kiếm Đoạn Thiên) with Saya-biki pull & Kiếm Hoa Zanshin.",
        hit_window=(0.14, 0.34),
        keyframes=[
            Keyframe(0.00, c0, "flow", tension=0.35),
            Keyframe(0.10, c_micro, "flow", tension=0.35),
            Keyframe(0.22, flash_mid, "snap"),
            Keyframe(0.35, flash_end, "flow"),
            Keyframe(0.50, zanshin_drift, "flow"),
            Keyframe(0.71, twirl_finish, "flow"),
            Keyframe(0.92, replace(g0, blade_spin_deg=360.0), "flow"),
        ],
    )


def make_sword_ground_plunge() -> AnimationClip:
    """Kiếm Trấn Sơn Hà: Clean wrist-inversion leap -> two-handed earth slam -> resonant Qi pulse -> serene rise."""
    g0 = _guard()
    invert_rise = _guard(
        hips_y=1.010, hips_yaw=-16.0, hips_pitch=2.0, torso_yaw=4.0, torso_pitch=-2.0,
        r_shoulder_lift=10.0, r_shoulder_fwd=14.0, l_shoulder_lift=8.0, l_shoulder_fwd=14.0,
        # Intermediate wrist-inversion arc avoids abrupt 150-deg blade flip
        r_hand_pos=(-0.16, 1.48, 0.36), r_elbow_pole=(-0.54, 1.26, 0.16),
        blade_dir=(-0.36, -0.30, 0.88), blade_edge=(0.89, -0.34, 0.30),
        l_hand_pos=(0.10, 1.44, 0.36), l_elbow_pole=(0.48, 1.24, 0.16), l_hand_dir=(-0.56, 0.36, 0.74),
        l_foot_pos=(0.17, 0.220, 0.18), l_foot_pitch=-14.0,
        r_foot_pos=(-0.20, 0.160, -0.18), r_foot_pitch=-18.0,
    )
    leap_inv = _guard(
        hips_y=1.235, hips_yaw=-8.0, hips_pitch=-4.0, torso_yaw=4.0, torso_pitch=-9.0,
        head_pitch=4.0, r_shoulder_lift=19.0, r_shoulder_fwd=16.0, l_shoulder_lift=18.0, l_shoulder_fwd=16.0,
        r_hand_pos=(-0.08, 1.90, 0.34), r_elbow_pole=(-0.48, 1.70, 0.24),
        blade_dir=(0.00, -0.99, 0.12), blade_edge=(1.0, 0.0, 0.0),
        l_hand_pos=(0.04, 1.86, 0.34), l_elbow_pole=(0.44, 1.68, 0.22), l_hand_dir=(-0.60, 0.40, 0.69),
        l_foot_pos=(0.18, 0.470, 0.18), l_knee_pole=(0.24, 0.87, 0.78), l_foot_pitch=-22.0,
        r_foot_pos=(-0.20, 0.370, -0.18), r_knee_pole=(-0.28, 0.75, 0.36), r_foot_pitch=-26.0,
    )
    plunge_in = _guard(
        hips_y=0.685, hips_yaw=4.0, hips_pitch=24.0, torso_yaw=2.0, torso_pitch=22.0,
        head_pitch=8.0, r_shoulder_lift=-10.0, r_shoulder_fwd=20.0, l_shoulder_lift=-10.0, l_shoulder_fwd=20.0,
        r_hand_pos=(-0.05, 0.72, 0.38), r_elbow_pole=(-0.44, 0.90, 0.18),
        blade_dir=(0.00, -0.995, 0.08), blade_edge=(1.0, 0.0, 0.0),
        l_hand_pos=(0.06, 0.75, 0.38), l_elbow_pole=(0.44, 0.92, 0.18), l_hand_dir=(-0.65, -0.25, 0.72),
        l_foot_pos=(0.20, 0.080, 0.28), l_knee_pole=(0.26, 0.42, 0.86),
        r_foot_pos=(-0.22, 0.080, -0.28), r_knee_pole=(-0.30, 0.36, 0.26),
    )
    plunge_pulse = replace(
        plunge_in,
        # Living Domain-Qi resonance instead of frozen duplicate
        hips_y=0.668, hips_pitch=26.0, torso_pitch=24.0, head_pitch=10.0,
        r_hand_pos=(-0.05, 0.705, 0.38), l_hand_pos=(0.06, 0.735, 0.38),
    )
    pull_out = _guard(
        hips_y=0.825, hips_yaw=-8.0, hips_pitch=11.0, torso_yaw=5.0, torso_pitch=10.0,
        r_hand_pos=(-0.10, 1.04, 0.38), r_elbow_pole=(-0.48, 0.96, 0.12),
        blade_dir=(0.05, -0.68, 0.73), blade_edge=(0.99, 0.05, -0.02),
        l_hand_pos=(0.24, 1.18, 0.30), l_elbow_pole=(0.56, 1.02, 0.02), l_hand_dir=(-0.18, 0.24, 0.95),
        l_foot_pos=(0.18, 0.080, 0.22), r_foot_pos=(-0.21, 0.080, -0.21),
    )
    rise = _guard(
        hips_y=0.892, hips_yaw=-18.0, hips_pitch=5.0, torso_yaw=10.0, torso_pitch=5.0,
        r_hand_pos=(-0.15, 1.10, 0.38), blade_dir=(0.16, 0.26, 0.95),
        l_hand_pos=(0.34, 1.28, 0.22), l_elbow_pole=(0.64, 1.08, -0.10), l_hand_dir=(0.18, 0.36, 0.92),
    )
    return AnimationClip(
        name="wuxia_sword_ground_plunge", slot="sword_cast", duration=1.08, loop=False,
        allow_ground_stab=True,
        tags=["sword", "skill", "domain", "plunge", "leap", "aoe"],
        description="Buoyant leap two-hand vertical sword plunge into earth (Kiếm Trấn Sơn Hà) with Qi pulse.",
        hit_window=(0.38, 0.58),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.15, invert_rise, "flow"),
            Keyframe(0.30, leap_inv, "flow"),
            Keyframe(0.45, plunge_in, "heavy_drop", tension=0.25),
            Keyframe(0.64, plunge_pulse, "flow"),
            Keyframe(0.79, pull_out, "flow"),
            Keyframe(0.93, rise, "flow"),
            Keyframe(1.08, g0, "flow"),
        ],
    )


def make_sword_sky_summon() -> AnimationClip:
    """Vạn Kiếm Quy Tông: Levitating Khinh Công hover, Kiếm Hoa skyward summon, heavenly halo & forward command."""
    g0 = _guard()
    gather = _guard(
        hips_y=0.862, hips_yaw=-24.0, hips_pitch=5.0, hips_roll=-2.0,
        torso_yaw=8.0, torso_pitch=5.0, torso_roll=2.0,
        r_hand_pos=(-0.38, 1.12, 0.36), r_elbow_pole=(-0.66, 0.92, 0.00),
        blade_dir=(-0.08, 0.60, 0.80), blade_edge=(0.0, 0.80, -0.60), blade_spin_deg=0.0,
        l_hand_pos=(0.14, 1.18, 0.38), l_elbow_pole=(0.50, 0.98, 0.12), l_hand_dir=(-0.38, 0.42, 0.82),
    )
    rise_twirl = _guard(
        hips_y=1.070, hips_yaw=-18.0, hips_pitch=-1.0, hips_roll=3.0,
        torso_yaw=0.0, torso_pitch=-5.0, torso_roll=-2.5,
        head_pitch=-6.0, r_shoulder_lift=16.0, r_shoulder_fwd=8.0, l_shoulder_lift=8.0,
        # Outer-right rising Kiếm Hoa spiral (180 deg) cleanly clear of head
        r_hand_pos=(-0.44, 1.62, 0.34), r_elbow_pole=(-0.68, 1.34, 0.08),
        blade_dir=(-0.22, 0.88, 0.42), blade_edge=(0.0, -0.43, 0.90), blade_spin_deg=180.0,
        l_hand_pos=(0.30, 1.44, 0.32), l_elbow_pole=(0.62, 1.20, 0.02), l_hand_dir=(0.14, 0.60, 0.79),
        l_foot_pos=(0.16, 0.240, 0.16), l_foot_pitch=-18.0,
        r_foot_pos=(-0.18, 0.180, -0.16), r_foot_pitch=-22.0,
    )
    levitate_halo = _guard(
        hips_y=1.240, hips_yaw=-10.0, hips_pitch=-4.0, hips_roll=2.5,
        torso_yaw=4.0, torso_pitch=-10.0, torso_roll=-2.0,
        head_pitch=-12.0, r_shoulder_lift=24.0, r_shoulder_fwd=12.0, l_shoulder_lift=14.0,
        r_hand_pos=(-0.34, 2.04, 0.32), r_elbow_pole=(-0.60, 1.74, 0.18),
        blade_dir=(-0.06, 0.990, 0.12), blade_edge=(0.0, -0.12, 0.99), blade_spin_deg=360.0,
        l_hand_pos=(0.44, 1.66, 0.24), l_elbow_pole=(0.68, 1.40, -0.06), l_hand_dir=(0.42, 0.68, 0.60),
        l_foot_pos=(0.16, 0.420, 0.14), l_knee_pole=(0.22, 0.82, 0.74), l_foot_pitch=-26.0,
        r_foot_pos=(-0.18, 0.320, -0.14), r_knee_pole=(-0.24, 0.72, 0.42), r_foot_pitch=-30.0,
    )
    levitate_breath = replace(
        levitate_halo,
        # Buoyant celestial hover breath instead of static mid-air pause
        hips_y=1.270, torso_pitch=-12.0, head_pitch=-14.0,
        r_hand_pos=(-0.33, 2.08, 0.31),
        l_hand_pos=(0.47, 1.71, 0.20), l_hand_dir=(0.48, 0.72, 0.50),
        l_foot_pos=(0.16, 0.455, 0.14), r_foot_pos=(-0.18, 0.355, -0.14),
    )
    command_arc = _guard(
        hips_y=1.010, hips_yaw=6.0, hips_pitch=5.0, hips_roll=-2.5,
        torso_yaw=9.0, torso_pitch=5.0, torso_roll=2.0,
        head_pitch=-4.0, r_shoulder_lift=12.0, r_shoulder_fwd=18.0, l_shoulder_lift=10.0, l_shoulder_fwd=18.0,
        r_hand_pos=(-0.26, 1.54, 0.44), r_elbow_pole=(-0.56, 1.24, 0.16),
        blade_dir=(-0.10, 0.62, 0.78), blade_edge=(0.0, 0.78, -0.62), blade_spin_deg=360.0,
        l_hand_pos=(0.24, 1.46, 0.44), l_elbow_pole=(0.56, 1.18, 0.14), l_hand_dir=(-0.06, 0.34, 0.94),
        l_foot_pos=(0.16, 0.180, 0.22), l_foot_pitch=-10.0,
        r_foot_pos=(-0.21, 0.140, -0.20), r_foot_pitch=-14.0,
    )
    command_fwd = _guard(
        hips_y=0.865, hips_yaw=16.0, hips_pitch=10.0, hips_roll=-4.0,
        torso_yaw=12.0, torso_pitch=10.0, torso_roll=3.5,
        head_pitch=0.0, r_shoulder_lift=4.0, r_shoulder_fwd=22.0, l_shoulder_lift=6.0, l_shoulder_fwd=22.0,
        r_hand_pos=(-0.22, 1.18, 0.50), r_elbow_pole=(-0.54, 0.96, 0.12),
        blade_dir=(-0.14, 0.16, 0.98), blade_edge=(0.0, 0.99, -0.16), blade_spin_deg=360.0,
        l_hand_pos=(0.16, 1.36, 0.54), l_elbow_pole=(0.50, 1.10, 0.18), l_hand_dir=(-0.08, 0.14, 0.99),
        l_foot_pos=(0.16, 0.080, 0.26), r_foot_pos=(-0.22, 0.080, -0.22),
    )
    command_drift = replace(
        command_fwd, hips_y=0.885, hips_yaw=10.0, hips_pitch=7.5, hips_roll=-2.0,
        torso_yaw=10.0, torso_pitch=7.0, torso_roll=1.5,
        r_hand_pos=(-0.22, 1.16, 0.47), blade_dir=(-0.06, 0.24, 0.97),
        l_hand_pos=(0.18, 1.34, 0.50),
    )
    return AnimationClip(
        name="wuxia_sword_sky_summon", slot="sword_cast", duration=1.20, loop=False,
        tags=["sword", "skill", "summon", "levitate", "ultimate"],
        description="Levitating Khinh Công skyward sword summon & forward command (Vạn Kiếm Quy Tông).",
        hit_window=(0.68, 0.92),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.16, gather, "flow"),
            Keyframe(0.31, rise_twirl, "flow"),
            Keyframe(0.46, levitate_halo, "flow"),
            Keyframe(0.62, levitate_breath, "flow"),
            Keyframe(0.73, command_arc, "snap"),
            Keyframe(0.85, command_fwd, "flow"),
            Keyframe(0.99, command_drift, "flow"),
            Keyframe(1.20, replace(g0, blade_spin_deg=360.0), "flow"),
        ],
    )


# ==============================================================================
# 4. DEFENSE, RIPOSTE, FLOATING KHINH CÔNG DODGES, HIT & MEDITATE (8 CLIPS)
# ==============================================================================

def make_sword_block() -> AnimationClip:
    """Hoành Kiếm Thủ Môn: Crisp high diagonal cross-guard with visible left palm bracing forearm & spring rebound."""
    g0 = _guard()
    brace = _guard(
        hips_y=0.838, hips_yaw=-14.0, hips_pitch=7.0, hips_roll=3.0,
        torso_yaw=6.0, torso_pitch=6.0, torso_roll=-2.5,
        head_pitch=3.0, r_shoulder_lift=8.0, r_shoulder_fwd=16.0, l_shoulder_lift=6.0, l_shoulder_fwd=16.0,
        r_hand_pos=(-0.22, 1.26, 0.36), r_elbow_pole=(-0.56, 1.02, 0.08),
        blade_dir=(0.76, 0.62, 0.18), blade_edge=(0.62, -0.78, 0.06),
        l_hand_pos=(0.04, 1.26, 0.36), l_elbow_pole=(0.46, 1.00, 0.10), l_hand_dir=(-0.62, 0.34, 0.71),
        l_foot_pos=(0.20, 0.080, 0.18), r_foot_pos=(-0.22, 0.080, -0.20),
    )
    absorb = replace(
        brace, hips_y=0.798, hips_pitch=3.0, hips_roll=4.5, torso_pitch=1.0, torso_roll=-3.5,
        r_hand_pos=(-0.20, 1.235, 0.305), l_hand_pos=(0.04, 1.235, 0.305),
    )
    rebound = replace(
        brace, hips_y=0.855, hips_yaw=-16.0, hips_roll=1.5, torso_roll=-1.5,
        r_hand_pos=(-0.22, 1.250, 0.370), l_hand_pos=(0.06, 1.255, 0.365),
    )
    return AnimationClip(
        name="wuxia_sword_block", slot="sword_block", duration=0.65, loop=False,
        tags=["sword", "combat", "block", "defense"],
        description="High diagonal cross-guard block (Hoành Kiếm Thủ Môn) with left forearm brace & spring absorb.",
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.14, brace, "snap"),
            Keyframe(0.29, absorb, "flow"),
            Keyframe(0.45, rebound, "flow"),
            Keyframe(0.65, g0, "flow"),
        ],
    )


def make_sword_parry() -> AnimationClip:
    """Tá Lực Đả Lực: Fluid Taiji circular sword deflection with harmonizing left Kiếm Quyết sweep."""
    g0 = _guard()
    tuck = _guard(
        hips_y=0.868, hips_yaw=15.0, hips_roll=-3.5, torso_yaw=10.0, torso_roll=3.0,
        r_shoulder_fwd=16.0,
        r_hand_pos=(0.14, 1.14, 0.36), r_elbow_pole=(-0.26, 0.92, 0.24),
        blade_dir=(0.56, 0.72, 0.41), blade_edge=(0.74, -0.62, 0.26),
        l_hand_pos=(0.36, 1.26, 0.08), l_elbow_pole=(0.64, 1.08, -0.22), l_hand_dir=(0.45, 0.40, 0.80),
    )
    deflect = _guard(
        hips_y=0.862, hips_yaw=-36.0, hips_roll=5.5, torso_yaw=-16.0, torso_pitch=3.0, torso_roll=-4.5,
        r_shoulder_lift=10.0, r_shoulder_fwd=8.0,
        r_hand_pos=(-0.34, 1.36, 0.36), r_elbow_pole=(-0.64, 1.06, 0.02),
        blade_dir=(-0.66, 0.62, 0.42), blade_edge=(-0.64, -0.74, 0.20),
        l_hand_pos=(0.14, 1.28, 0.38), l_elbow_pole=(0.52, 1.04, 0.08), l_hand_dir=(-0.24, 0.30, 0.92),
    )
    deflect_flow = _guard(
        hips_y=0.880, hips_yaw=-39.0, hips_roll=4.0, torso_yaw=-14.0, torso_pitch=2.0, torso_roll=-3.0,
        r_shoulder_lift=11.0, r_shoulder_fwd=4.0,
        # Circular Taiji follow-through drift instead of dead pause
        r_hand_pos=(-0.37, 1.39, 0.32), r_elbow_pole=(-0.65, 1.08, -0.02),
        blade_dir=(-0.70, 0.64, 0.32), blade_edge=(-0.62, -0.76, 0.20),
        l_hand_pos=(0.15, 1.30, 0.39), l_elbow_pole=(0.53, 1.06, 0.08), l_hand_dir=(-0.22, 0.34, 0.91),
    )
    return AnimationClip(
        name="wuxia_sword_parry", slot="sword_parry", duration=0.62, loop=False,
        tags=["sword", "combat", "parry", "defense"],
        description="Fluid Taiji circular sword parry (Tá Lực Đả Lực) with harmonizing left Kiếm Quyết.",
        hit_window=(0.14, 0.32),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.12, tuck, "flow"),
            Keyframe(0.27, deflect, "snap"),
            Keyframe(0.42, deflect_flow, "flow"),
            Keyframe(0.62, g0, "flow"),
        ],
    )


def make_sword_riposte() -> AnimationClip:
    """Kim Cang Phản Trảm: Yielding Taiji parry carving a wide outer arc nonstop into a lethal counter-cleave."""
    g0 = _guard()
    deflect = _guard(
        hips_y=0.855, hips_yaw=-34.0, hips_roll=5.5, torso_yaw=-14.0, torso_roll=-4.5,
        r_shoulder_lift=10.0, r_shoulder_fwd=6.0,
        r_hand_pos=(-0.36, 1.36, 0.34), r_elbow_pole=(-0.64, 1.06, -0.02),
        blade_dir=(-0.58, 0.66, 0.47), blade_edge=(-0.64, -0.74, 0.20),
        l_hand_pos=(0.14, 1.26, 0.38), l_elbow_pole=(0.52, 1.02, 0.08), l_hand_dir=(-0.22, 0.28, 0.93),
    )
    counter_mid = _guard(
        hips_y=0.822, hips_yaw=4.0, hips_pitch=10.0, hips_roll=-2.5,
        torso_yaw=8.0, torso_pitch=10.0, torso_roll=2.5,
        r_shoulder_lift=3.0, r_shoulder_fwd=22.0,
        # Mid-slash forward breakdown prevents cutting straight through chest
        r_hand_pos=(-0.04, 1.16, 0.51), r_elbow_pole=(-0.44, 0.94, 0.18),
        blade_dir=(0.14, 0.06, 0.99), blade_edge=(0.99, 0.0, -0.14),
        l_hand_pos=(0.36, 1.32, 0.04), l_elbow_pole=(0.64, 1.12, -0.22), l_hand_dir=(0.62, 0.36, 0.70),
        l_foot_pos=(0.16, 0.080, 0.28), r_foot_pos=(-0.22, 0.100, -0.24), r_foot_pitch=-10.0,
    )
    counter = _guard(
        hips_y=0.810, hips_yaw=32.0, hips_pitch=14.0, hips_roll=-6.0,
        torso_yaw=20.0, torso_pitch=14.0, torso_roll=5.0, head_roll=-2.0,
        r_shoulder_lift=0.0, r_shoulder_fwd=24.0, l_shoulder_lift=8.0, l_shoulder_fwd=-14.0,
        r_hand_pos=(0.26, 1.08, 0.44), r_elbow_pole=(-0.18, 0.90, 0.36),
        blade_dir=(0.72, -0.04, 0.69), blade_edge=(0.69, 0.0, -0.72),
        l_hand_pos=(0.48, 1.36, -0.22), l_elbow_pole=(0.66, 1.14, -0.42), l_hand_dir=(0.76, 0.44, -0.48),
        l_foot_pos=(0.16, 0.080, 0.32), r_foot_pos=(-0.22, 0.120, -0.28), r_foot_pitch=-16.0,
    )
    counter_drift = replace(
        counter, hips_y=0.838, hips_yaw=26.0, hips_pitch=11.0, hips_roll=-3.5,
        torso_yaw=16.0, torso_pitch=10.0, torso_roll=3.0,
        r_hand_pos=(0.21, 1.12, 0.42), blade_dir=(0.64, 0.12, 0.76),
        l_hand_pos=(0.44, 1.36, -0.12),
    )
    return AnimationClip(
        name="wuxia_sword_riposte", slot="sword_parry", duration=0.72, loop=False,
        tags=["sword", "combat", "parry", "counter", "riposte"],
        description="Fluid parry deflection curving nonstop into a lethal stepping counter-cleave (Kim Cang Phản Trảm).",
        hit_window=(0.22, 0.44),
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.15, deflect, "snap"),
            Keyframe(0.26, counter_mid, "snap"),
            Keyframe(0.38, counter, "flow"),
            Keyframe(0.52, counter_drift, "flow"),
            Keyframe(0.72, g0, "flow"),
        ],
    )


def make_sword_dodge_back() -> AnimationClip:
    """Lăng Ba Thối Bộ: Buoyant floating Khinh Công backward skim with feathered touchdown."""
    g0 = _guard()
    push_off = _guard(
        hips_y=0.860, hips_yaw=-20.0, hips_pitch=13.0, hips_roll=2.5,
        torso_yaw=8.0, torso_pitch=10.0, torso_roll=-2.0, head_pitch=-4.0,
        r_hand_pos=(-0.28, 1.08, 0.30), r_elbow_pole=(-0.58, 0.92, -0.10),
        blade_dir=(0.02, 0.38, 0.92),
        l_hand_pos=(0.22, 1.24, 0.35), l_elbow_pole=(0.58, 1.06, 0.02), l_hand_dir=(-0.28, 0.26, 0.92),
        l_foot_pos=(0.16, 0.160, 0.22), l_foot_pitch=-8.0,
        r_foot_pos=(-0.20, 0.095, -0.22), r_foot_pitch=-12.0,
    )
    skim = _guard(
        hips_y=1.030, hips_yaw=-25.0, hips_pitch=14.0, hips_roll=4.5,
        torso_yaw=11.0, torso_pitch=12.0, torso_roll=-3.5, head_pitch=-6.0,
        r_shoulder_lift=0.0, r_shoulder_fwd=-8.0, l_shoulder_lift=4.0,
        r_hand_pos=(-0.38, 1.17, 0.23), r_elbow_pole=(-0.63, 1.03, -0.18),
        blade_dir=(-0.24, 0.32, 0.92), blade_edge=(0.0, 0.95, -0.32),
        l_hand_pos=(0.24, 1.31, 0.39), l_elbow_pole=(0.58, 1.11, 0.04), l_hand_dir=(-0.24, 0.26, 0.93),
        l_foot_pos=(0.16, 0.290, 0.24), l_knee_pole=(0.22, 0.67, 0.82), l_foot_pitch=-16.0,
        r_foot_pos=(-0.20, 0.170, -0.28), r_knee_pole=(-0.28, 0.55, 0.32), r_foot_pitch=-22.0,
    )
    land_soft = _guard(
        hips_y=0.848, hips_yaw=-24.0, hips_pitch=9.0, hips_roll=1.5,
        torso_yaw=12.0, torso_pitch=7.0, torso_roll=-1.5,
        r_hand_pos=(-0.26, 1.06, 0.32), blade_dir=(0.10, 0.40, 0.91),
        l_hand_pos=(0.23, 1.22, 0.32), l_hand_dir=(-0.32, 0.28, 0.90),
        l_foot_pos=(0.16, 0.080, 0.20), r_foot_pos=(-0.20, 0.080, -0.22),
    )
    return AnimationClip(
        name="wuxia_sword_dodge_back", slot="sword_dodge", duration=0.62, loop=False,
        tags=["sword", "combat", "dodge", "back", "khinh_cong"],
        description="Buoyant floating Khinh Công backward evade (Lăng Ba Thối Bộ) with feathered landing.",
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.13, push_off, "snap"),
            Keyframe(0.29, skim, "flow"),
            Keyframe(0.46, land_soft, "heavy_drop"),
            Keyframe(0.62, g0, "flow"),
        ],
    )


def make_sword_dodge_left() -> AnimationClip:
    """Phi Yến Tả Thiểm: Weightless floating Khinh Công sidestep left (+X) with soaring balanced arms."""
    g0 = _guard()
    step_l = _guard(
        hips_y=0.875, hips_yaw=-14.0, hips_pitch=10.0, hips_roll=-7.5,
        torso_yaw=8.0, torso_pitch=7.0, torso_roll=6.0, head_roll=-2.0,
        r_hand_pos=(-0.21, 1.10, 0.37), blade_dir=(0.24, 0.38, 0.89),
        l_hand_pos=(0.36, 1.26, 0.24), l_elbow_pole=(0.66, 1.09, -0.10), l_hand_dir=(0.30, 0.34, 0.89),
        l_foot_pos=(0.32, 0.160, 0.18), l_foot_yaw=18.0, l_foot_pitch=-8.0,
        r_foot_pos=(-0.14, 0.110, -0.19), r_foot_pitch=-12.0,
    )
    glide = _guard(
        hips_y=0.990, hips_yaw=-10.0, hips_pitch=12.0, hips_roll=-12.5,
        torso_yaw=6.0, torso_pitch=8.0, torso_roll=10.5, head_roll=-3.5,
        l_shoulder_lift=6.0,
        r_hand_pos=(-0.24, 1.15, 0.36), r_elbow_pole=(-0.58, 0.99, 0.00),
        blade_dir=(0.22, 0.34, 0.91), blade_edge=(0.0, 0.94, -0.34),
        l_hand_pos=(0.45, 1.29, 0.18), l_elbow_pole=(0.68, 1.13, -0.14), l_hand_dir=(0.56, 0.36, 0.74),
        l_foot_pos=(0.39, 0.250, 0.18), l_knee_pole=(0.47, 0.63, 0.78), l_foot_yaw=22.0, l_foot_pitch=-16.0,
        r_foot_pos=(-0.08, 0.170, -0.20), r_knee_pole=(-0.14, 0.55, 0.44), r_foot_pitch=-22.0,
    )
    land_l = _guard(
        hips_y=0.848, hips_yaw=-18.0, hips_pitch=8.0, hips_roll=-4.5,
        torso_yaw=10.0, torso_pitch=6.0, torso_roll=3.5,
        r_hand_pos=(-0.20, 1.06, 0.36), blade_dir=(0.24, 0.42, 0.87),
        l_hand_pos=(0.29, 1.23, 0.26), l_hand_dir=(-0.10, 0.32, 0.94),
        l_foot_pos=(0.24, 0.080, 0.18), r_foot_pos=(-0.16, 0.080, -0.18),
    )
    return AnimationClip(
        name="wuxia_sword_dodge_left", slot="sword_dodge", duration=0.62, loop=False,
        tags=["sword", "combat", "dodge", "left", "khinh_cong"],
        description="Weightless floating Khinh Công left sidestep evade (Phi Yến Tả Thiểm).",
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.13, step_l, "snap"),
            Keyframe(0.29, glide, "flow"),
            Keyframe(0.46, land_l, "heavy_drop"),
            Keyframe(0.62, g0, "flow"),
        ],
    )


def make_sword_dodge_right() -> AnimationClip:
    """Phi Yến Hữu Thiểm: Weightless floating Khinh Công sidestep right (-X) with sweeping trailing sword."""
    g0 = _guard()
    step_r = _guard(
        hips_y=0.875, hips_yaw=-28.0, hips_pitch=10.0, hips_roll=7.5,
        torso_yaw=13.0, torso_pitch=7.0, torso_roll=-6.0, head_roll=2.0,
        r_hand_pos=(-0.32, 1.10, 0.28), blade_dir=(-0.18, 0.32, 0.93),
        l_hand_pos=(0.21, 1.28, 0.34), l_elbow_pole=(0.58, 1.09, -0.02), l_hand_dir=(-0.38, 0.34, 0.86),
        l_foot_pos=(0.12, 0.110, 0.19), l_foot_pitch=-12.0,
        r_foot_pos=(-0.32, 0.160, -0.18), r_foot_yaw=-30.0, r_foot_pitch=-8.0,
    )
    glide = _guard(
        hips_y=0.990, hips_yaw=-30.0, hips_pitch=12.0, hips_roll=12.5,
        torso_yaw=12.0, torso_pitch=8.0, torso_roll=-10.5, head_roll=3.5,
        l_shoulder_lift=6.0,
        r_hand_pos=(-0.41, 1.13, 0.22), r_elbow_pole=(-0.64, 1.01, -0.20),
        blade_dir=(-0.42, 0.24, 0.87), blade_edge=(0.0, 0.96, -0.26),
        l_hand_pos=(0.20, 1.33, 0.37), l_elbow_pole=(0.56, 1.13, 0.02), l_hand_dir=(-0.38, 0.36, 0.85),
        l_foot_pos=(0.08, 0.170, 0.20), l_knee_pole=(0.14, 0.55, 0.74), l_foot_pitch=-22.0,
        r_foot_pos=(-0.39, 0.250, -0.18), r_knee_pole=(-0.47, 0.63, 0.46), r_foot_yaw=-32.0, r_foot_pitch=-16.0,
    )
    land_r = _guard(
        hips_y=0.848, hips_yaw=-28.0, hips_pitch=8.0, hips_roll=4.5,
        torso_yaw=13.0, torso_pitch=6.0, torso_roll=-3.5,
        r_hand_pos=(-0.26, 1.06, 0.32), blade_dir=(0.02, 0.38, 0.92),
        l_hand_pos=(0.21, 1.25, 0.32), l_hand_dir=(-0.38, 0.32, 0.87),
        l_foot_pos=(0.15, 0.080, 0.18), r_foot_pos=(-0.25, 0.080, -0.18),
    )
    return AnimationClip(
        name="wuxia_sword_dodge_right", slot="sword_dodge", duration=0.62, loop=False,
        tags=["sword", "combat", "dodge", "right", "khinh_cong"],
        description="Weightless floating Khinh Công right sidestep evade (Phi Yến Hữu Thiểm).",
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.13, step_r, "snap"),
            Keyframe(0.29, glide, "flow"),
            Keyframe(0.46, land_r, "heavy_drop"),
            Keyframe(0.62, g0, "flow"),
        ],
    )


def make_sword_hit() -> AnimationClip:
    """Trúng Chiêu Hồi Thế: Organic whip-action stagger (chest snaps first, head & sword follow through)."""
    g0 = _guard()
    impact_snap = _guard(
        hips_y=0.855, hips_yaw=-30.0, hips_pitch=-6.0, hips_roll=3.0,
        torso_yaw=-8.0, torso_pitch=-14.0, torso_roll=5.0,
        # Head lags slightly forward on initial chest impact for organic whip realism
        head_pitch=2.5, head_yaw=-3.0, head_roll=-2.0,
        r_shoulder_lift=6.0, r_shoulder_fwd=-8.0, l_shoulder_lift=6.0,
        r_hand_pos=(-0.34, 1.10, 0.22), r_elbow_pole=(-0.60, 0.94, -0.16),
        blade_dir=(-0.34, 0.56, 0.75), blade_edge=(0.0, 0.80, -0.60),
        l_hand_pos=(0.22, 1.30, 0.26), l_elbow_pole=(0.56, 1.06, -0.10), l_hand_dir=(-0.44, 0.48, 0.76),
        l_foot_pos=(0.18, 0.105, 0.15), l_foot_pitch=6.0,
        r_foot_pos=(-0.22, 0.080, -0.22),
    )
    recoil_follow = _guard(
        hips_y=0.832, hips_yaw=-36.0, hips_pitch=-9.0, hips_roll=5.0,
        torso_yaw=-12.0, torso_pitch=-17.0, torso_roll=6.5,
        head_pitch=-12.0, head_yaw=-7.0, head_roll=3.0,
        r_shoulder_lift=9.0, r_shoulder_fwd=-12.0, l_shoulder_lift=8.0,
        r_hand_pos=(-0.40, 1.14, 0.14), r_elbow_pole=(-0.64, 0.98, -0.24),
        blade_dir=(-0.48, 0.64, 0.60), blade_edge=(0.0, 0.68, -0.73),
        l_hand_pos=(0.26, 1.36, 0.18), l_elbow_pole=(0.58, 1.10, -0.18), l_hand_dir=(-0.50, 0.60, 0.62),
        l_foot_pos=(0.18, 0.120, 0.13), l_foot_pitch=10.0,
        r_foot_pos=(-0.22, 0.080, -0.25),
    )
    brace_recover = _guard(
        hips_y=0.858, hips_yaw=-28.0, hips_pitch=5.0, hips_roll=1.5,
        torso_yaw=6.0, torso_pitch=4.5, torso_roll=-1.0, head_pitch=-1.5,
        r_hand_pos=(-0.26, 1.07, 0.30), r_elbow_pole=(-0.58, 0.90, -0.08),
        blade_dir=(0.04, 0.48, 0.88),
        l_hand_pos=(0.23, 1.24, 0.28), l_hand_dir=(-0.38, 0.34, 0.86),
        l_foot_pos=(0.17, 0.080, 0.17), r_foot_pos=(-0.20, 0.080, -0.20),
    )
    return AnimationClip(
        name="wuxia_sword_hit", slot="sword_hit", duration=0.55, loop=False,
        tags=["sword", "combat", "hit", "reaction"],
        description="Organic whip-action frontal impact stagger with defensive left-arm flinch and sword recovery.",
        keyframes=[
            Keyframe(0.00, g0, "flow"),
            Keyframe(0.11, impact_snap, "snap"),
            Keyframe(0.24, recoil_follow, "flow"),
            Keyframe(0.39, brace_recover, "flow"),
            Keyframe(0.55, g0, "flow"),
        ],
    )


def make_sword_meditate() -> AnimationClip:
    """Bão Kiếm Điều Tức: Serene 4-phase upright-sword Qi meditation with floating wrists & harmonic spine breath."""
    m0 = PoseSpec(
        hips_y=0.920, hips_yaw=0.0, hips_pitch=1.0, torso_yaw=0.0, torso_pitch=1.0,
        head_yaw=0.0, head_pitch=4.0,
        l_shoulder_lift=-4.0, l_shoulder_fwd=10.0, r_shoulder_lift=-4.0, r_shoulder_fwd=10.0,
        r_hand_pos=(-0.03, 1.08, 0.35), r_elbow_pole=(-0.44, 0.92, 0.08),
        blade_dir=(0.00, 0.995, 0.10), blade_edge=(1.0, 0.0, 0.0),
        l_hand_pos=(0.06, 1.15, 0.36), l_elbow_pole=(0.44, 0.96, 0.08), l_hand_dir=(-0.50, 0.74, 0.45),
        l_foot_pos=(0.15, 0.080, 0.00), l_knee_pole=(0.18, 0.48, 0.62), l_foot_yaw=8.0,
        r_foot_pos=(-0.15, 0.080, 0.00), r_knee_pole=(-0.18, 0.48, 0.62), r_foot_yaw=-8.0,
    )
    m_rise = replace(
        m0, hips_y=0.928, hips_pitch=0.0, torso_pitch=-0.5, head_pitch=3.0,
        l_shoulder_lift=-2.5, r_shoulder_lift=-2.5,
        r_hand_pos=(-0.03, 1.102, 0.356), r_elbow_pole=(-0.455, 0.935, 0.07),
        l_hand_pos=(0.06, 1.200, 0.366), l_elbow_pole=(0.455, 0.995, 0.09), l_hand_dir=(-0.47, 0.78, 0.41),
    )
    m_apex = replace(
        m0, hips_y=0.936, hips_pitch=-0.5, torso_pitch=-1.8, head_pitch=1.8,
        l_shoulder_lift=-1.5, l_shoulder_fwd=9.0, r_shoulder_lift=-1.5, r_shoulder_fwd=9.0,
        r_hand_pos=(-0.03, 1.124, 0.362), r_elbow_pole=(-0.470, 0.950, 0.06),
        blade_dir=(0.00, 0.997, 0.075),
        l_hand_pos=(0.06, 1.250, 0.372), l_elbow_pole=(0.470, 1.030, 0.10), l_hand_dir=(-0.43, 0.81, 0.40),
    )
    m_sink = replace(
        m0, hips_y=0.925, hips_pitch=0.5, torso_pitch=0.4, head_pitch=3.5,
        l_shoulder_lift=-3.5, r_shoulder_lift=-3.5,
        r_hand_pos=(-0.03, 1.094, 0.354), r_elbow_pole=(-0.448, 0.928, 0.075),
        l_hand_pos=(0.06, 1.180, 0.364), l_elbow_pole=(0.448, 0.980, 0.085), l_hand_dir=(-0.48, 0.76, 0.43),
    )
    return AnimationClip(
        name="wuxia_sword_meditate", slot="meditate", duration=2.20, loop=True,
        tags=["sword", "qi", "meditate", "wuxia"],
        description="Serene upright-sword Qi meditation (Bão Kiếm Điều Tức) with 4-phase harmonic breathing.",
        keyframes=[
            Keyframe(0.00, m0, "flow"),
            Keyframe(0.55, m_rise, "flow"),
            Keyframe(1.10, m_apex, "flow"),
            Keyframe(1.65, m_sink, "flow"),
            Keyframe(2.20, m0, "flow"),
        ],
    )


# ==============================================================================
# MASTER BUILDER (ALL 31 SWORD CLIPS IN ONE PLACE)
# ==============================================================================

def build_all_sword_clips(fps: int = 60) -> List[AnimationClip]:
    builders = [
        # Locomotion & Handling (7)
        make_sword_idle,
        make_sword_walk,
        make_sword_run,
        make_sword_strafe_left,
        make_sword_strafe_right,
        make_sword_draw,
        make_sword_sheath,
        # Combat Combo, Spins, Aerial & Flurries (11)
        make_sword_slash_r2l,
        make_sword_slash_l2r,
        make_sword_thrust,
        make_sword_slash_diag,
        make_sword_heavy_cleave,
        make_sword_cross_x_slash,
        make_sword_flurry_thrust,
        make_sword_uppercut_launch,
        make_sword_whirlwind_360,
        make_sword_dash_slash,
        make_sword_jump_slash,
        # Signature Wuxia Sword Skills (5)
        make_sword_cast,
        make_sword_iaido_charge,
        make_sword_iaido_draw_slash,
        make_sword_ground_plunge,
        make_sword_sky_summon,
        # Defense, Counter, Floating Khinh Công Evades, Hit & Qi (8)
        make_sword_block,
        make_sword_parry,
        make_sword_riposte,
        make_sword_dodge_back,
        make_sword_dodge_left,
        make_sword_dodge_right,
        make_sword_hit,
        make_sword_meditate,
    ]
    return [sample_clip(fn(), fps=fps) for fn in builders]