#!/usr/bin/env python3
"""
scripts/rebuild_martial_anims.py

Rebuilds assets/characters/animations/humanoid_anims.glb around a clean,
non-elemental Martial Arts (Võ Học) taxonomy:
  - Kept basic locomotion + casting:
      idle, walk, running, jumping_down, loco_idle_sword.001, casting
  - Hand techniques (Đòn Tay):
      fist_straight_punch, fist_hook_uppercut, palm_twin_thrust, elbow_horizontal_smash
  - Leg / evasion techniques (Đòn Chân):
      kick_snap_front, kick_roundhouse, kick_axe_stomp, dodge_evade_step
  - Sword techniques (Chiêu Thức Với Kiếm):
      sword_draw_gather, sword_horizontal_cleave, sword_diagonal_backhand,
      sword_lunge_thrust, sword_parry_guard

Starts every pose from the actual frame-0 pose of `idle` (or `loco_idle_sword.001`)
and applies local/bone delta rotations so Mixamo shoulder/upleg frames
(character/docs/LANDMINES.md §7) are always preserved.

Run:
  /Applications/Blender.app/Contents/MacOS/Blender --background --python scripts/rebuild_martial_anims.py
"""

import math
from pathlib import Path

import bpy
from mathutils import Euler, Quaternion, Vector

REPO_ROOT = Path(__file__).resolve().parent.parent
ANIM_BANK_GLB = REPO_ROOT / "assets/characters/animations/humanoid_anims.glb"
CLIPS_DIR = REPO_ROOT / "assets/characters/animations/clips"

KEEP_BASE_CLIPS = {
    "idle",
    "walk",
    "running",
    "jumping_down",
    "loco_idle_sword.001",
    "casting",
}


def capture_action_frame0(armature, action_name):
    act = bpy.data.actions.get(action_name)
    if act is None:
        raise RuntimeError(f"Missing required base action: {action_name}")
    armature.animation_data_create()
    armature.animation_data.action = act
    bpy.context.scene.frame_set(0)
    bpy.context.view_layer.update()
    pose = {}
    for pb in armature.pose.bones:
        pose[pb.name] = (
            pb.location.copy(),
            pb.rotation_quaternion.copy(),
        )
    return pose


def apply_pose_delta(armature, base_pose, frame, hip_offset=(0.0, 0.0, 0.0), bone_deltas_deg=None):
    bpy.context.scene.frame_set(frame)
    for pb in armature.pose.bones:
        if pb.name not in base_pose:
            continue
        pb.rotation_mode = "QUATERNION"
        base_loc, base_q = base_pose[pb.name]
        if pb.name == "mixamorig:Hips":
            pb.location = base_loc + Vector(hip_offset)
            pb.keyframe_insert(data_path="location", frame=frame)
        else:
            pb.location = base_loc.copy()

        q = base_q.copy()
        if bone_deltas_deg and pb.name in bone_deltas_deg:
            dx, dy, dz = bone_deltas_deg[pb.name]
            dq = Euler((math.radians(dx), math.radians(dy), math.radians(dz)), "XYZ").to_quaternion()
            q = q @ dq
        pb.rotation_quaternion = q
        if pb.name.startswith("mixamorig:"):
            pb.keyframe_insert(data_path="rotation_quaternion", frame=frame)


def apply_bezier_easing(action):
    for fc in action.fcurves:
        for kp in fc.keyframe_points:
            kp.interpolation = "BEZIER"
            kp.handle_left_type = "AUTO_CLAMPED"
            kp.handle_right_type = "AUTO_CLAMPED"


def make_clip(armature, base_pose, name, keyframes):
    """keyframes: list of (frame, hip_offset, bone_deltas_deg)."""
    act = bpy.data.actions.new(name)
    armature.animation_data_create()
    armature.animation_data.action = act
    for frame, hip_off, deltas in keyframes:
        apply_pose_delta(armature, base_pose, frame, hip_off, deltas)
    apply_bezier_easing(act)
    act.use_fake_user = True
    return act


def export_single_clip(armature, action, out_path):
    armature.animation_data.action = action
    saved = {}
    for a in bpy.data.actions:
        saved[a.name] = a.use_fake_user
        a.use_fake_user = (a == action)

    bpy.ops.object.select_all(action="DESELECT")
    armature.select_set(True)
    bpy.context.view_layer.objects.active = armature

    bpy.ops.export_scene.gltf(
        filepath=str(out_path),
        export_format="GLB",
        use_selection=True,
        export_animations=True,
        export_nla_strips=False,
        export_draco_mesh_compression_enable=False,
        export_current_frame=False,
        export_yup=True,
    )
    for name, val in saved.items():
        if name in bpy.data.actions:
            bpy.data.actions[name].use_fake_user = val
    print(f"  [CLIP] {out_path.name} ({int(action.frame_range[1])} frames)")


def build_all_martial_clips(armature, idle_pose, sword_pose):
    clips = []

    # =========================================================================
    # 1. ĐÒN TAY (Hand / Arm Techniques)
    # =========================================================================
    clips.append(make_clip(armature, idle_pose, "fist_straight_punch", [
        (0, (0, 0, 0), {}),
        (4, (0, -0.04, -0.02), {
            "mixamorig:Hips": (0, -22, 0),
            "mixamorig:Spine1": (6, -18, 0),
            "mixamorig:RightArm": (20, -20, -15),
            "mixamorig:RightForeArm": (0, 0, 75),
            "mixamorig:LeftArm": (-28, 12, 20),
            "mixamorig:LeftForeArm": (0, 0, -65),
        }),
        (9, (0, 0.08, -0.04), {
            "mixamorig:Hips": (8, 28, 0),
            "mixamorig:Spine1": (10, 26, 0),
            "mixamorig:RightArm": (-65, 10, 12),
            "mixamorig:RightForeArm": (0, 0, 10),
            "mixamorig:LeftArm": (-18, -10, 35),
            "mixamorig:LeftForeArm": (0, 0, -85),
            "mixamorig:LeftUpLeg": (-22, 0, 0),
            "mixamorig:LeftLeg": (32, 0, 0),
        }),
        (13, (0, 0.06, -0.03), {
            "mixamorig:Hips": (8, 24, 0),
            "mixamorig:Spine1": (8, 22, 0),
            "mixamorig:RightArm": (-62, 10, 12),
            "mixamorig:RightForeArm": (0, 0, 14),
            "mixamorig:LeftArm": (-18, -10, 35),
            "mixamorig:LeftForeArm": (0, 0, -85),
        }),
        (20, (0, 0, 0), {}),
    ]))

    clips.append(make_clip(armature, idle_pose, "fist_hook_uppercut", [
        (0, (0, 0, 0), {}),
        (5, (0, -0.03, -0.08), {
            "mixamorig:Hips": (12, -26, 0),
            "mixamorig:Spine1": (14, -18, -8),
            "mixamorig:RightArm": (25, -18, -28),
            "mixamorig:RightForeArm": (0, 0, 70),
            "mixamorig:LeftUpLeg": (-28, 0, 0),
            "mixamorig:LeftLeg": (42, 0, 0),
            "mixamorig:RightUpLeg": (-22, 0, 0),
            "mixamorig:RightLeg": (36, 0, 0),
        }),
        (11, (0, 0.05, 0.02), {
            "mixamorig:Hips": (-4, 30, 0),
            "mixamorig:Spine1": (-6, 28, 8),
            "mixamorig:RightArm": (-82, 22, -16),
            "mixamorig:RightForeArm": (0, 0, 68),
            "mixamorig:LeftArm": (-24, -12, 32),
            "mixamorig:LeftForeArm": (0, 0, -80),
        }),
        (18, (0, 0, 0), {}),
    ]))

    clips.append(make_clip(armature, idle_pose, "palm_twin_thrust", [
        (0, (0, 0, 0), {}),
        (6, (0, -0.04, -0.10), {
            "mixamorig:Hips": (10, 0, 0),
            "mixamorig:Spine1": (8, 0, 0),
            "mixamorig:LeftArm": (18, 12, 32),
            "mixamorig:LeftForeArm": (0, 0, -82),
            "mixamorig:RightArm": (18, -12, -32),
            "mixamorig:RightForeArm": (0, 0, 82),
            "mixamorig:LeftUpLeg": (-32, 0, -10),
            "mixamorig:LeftLeg": (46, 0, 0),
            "mixamorig:RightUpLeg": (-26, 0, 10),
            "mixamorig:RightLeg": (40, 0, 0),
        }),
        (12, (0, 0.09, -0.06), {
            "mixamorig:Hips": (12, 0, 0),
            "mixamorig:Spine1": (14, 0, 0),
            "mixamorig:LeftArm": (-62, -10, -14),
            "mixamorig:LeftForeArm": (0, 0, -10),
            "mixamorig:LeftHand": (-48, 0, 0),
            "mixamorig:RightArm": (-62, 10, 14),
            "mixamorig:RightForeArm": (0, 0, 10),
            "mixamorig:RightHand": (-48, 0, 0),
            "mixamorig:LeftUpLeg": (-34, 0, -10),
            "mixamorig:LeftLeg": (48, 0, 0),
        }),
        (17, (0, 0.07, -0.05), {
            "mixamorig:Spine1": (12, 0, 0),
            "mixamorig:LeftArm": (-58, -10, -14),
            "mixamorig:LeftHand": (-42, 0, 0),
            "mixamorig:RightArm": (-58, 10, 14),
            "mixamorig:RightHand": (-42, 0, 0),
        }),
        (24, (0, 0, 0), {}),
    ]))

    clips.append(make_clip(armature, idle_pose, "elbow_horizontal_smash", [
        (0, (0, 0, 0), {}),
        (5, (0, -0.02, -0.05), {
            "mixamorig:Hips": (6, -32, 0),
            "mixamorig:Spine1": (6, -24, 0),
            "mixamorig:RightArm": (-28, -28, -25),
            "mixamorig:RightForeArm": (0, 0, 105),
        }),
        (10, (0, 0.07, -0.04), {
            "mixamorig:Hips": (8, 36, 0),
            "mixamorig:Spine1": (10, 32, 0),
            "mixamorig:RightArm": (-58, 42, -18),
            "mixamorig:RightForeArm": (0, 0, 112),
            "mixamorig:LeftArm": (-20, -12, 34),
            "mixamorig:LeftForeArm": (0, 0, -82),
        }),
        (18, (0, 0, 0), {}),
    ]))

    # =========================================================================
    # 2. ĐÒN CHÂN (Leg / Foot / Evasion Techniques)
    # =========================================================================
    clips.append(make_clip(armature, idle_pose, "kick_snap_front", [
        (0, (0, 0, 0), {}),
        (5, (0, -0.02, -0.04), {
            "mixamorig:Hips": (-8, 12, 0),
            "mixamorig:Spine1": (-8, -8, 0),
            "mixamorig:RightUpLeg": (-82, 0, 0),
            "mixamorig:RightLeg": (98, 0, 0),
            "mixamorig:LeftArm": (-36, 0, 18),
            "mixamorig:LeftForeArm": (0, 0, -76),
            "mixamorig:RightArm": (-24, 0, -18),
            "mixamorig:RightForeArm": (0, 0, 72),
        }),
        (10, (0, 0.04, 0.02), {
            "mixamorig:Hips": (-16, 16, 0),
            "mixamorig:Spine1": (-14, -10, 0),
            "mixamorig:RightUpLeg": (-88, 0, 0),
            "mixamorig:RightLeg": (8, 0, 0),
            "mixamorig:RightFoot": (32, 0, 0),
            "mixamorig:LeftArm": (-42, 0, 22),
            "mixamorig:LeftForeArm": (0, 0, -72),
            "mixamorig:RightArm": (22, 0, -24),
        }),
        (15, (0, 0, -0.02), {
            "mixamorig:Hips": (-6, 8, 0),
            "mixamorig:RightUpLeg": (-62, 0, 0),
            "mixamorig:RightLeg": (86, 0, 0),
        }),
        (22, (0, 0, 0), {}),
    ]))

    clips.append(make_clip(armature, idle_pose, "kick_roundhouse", [
        (0, (0, 0, 0), {}),
        (5, (0, 0.02, -0.04), {
            "mixamorig:Hips": (0, 48, -14),
            "mixamorig:Spine1": (-8, -22, 12),
            "mixamorig:RightUpLeg": (-68, -24, 28),
            "mixamorig:RightLeg": (92, 0, 0),
            "mixamorig:LeftArm": (-38, 0, 22),
            "mixamorig:LeftForeArm": (0, 0, -78),
        }),
        (11, (0, 0.05, 0.01), {
            "mixamorig:Hips": (-8, 68, -24),
            "mixamorig:Spine1": (-12, -30, 18),
            "mixamorig:RightUpLeg": (-78, -26, 36),
            "mixamorig:RightLeg": (6, 0, 0),
            "mixamorig:RightFoot": (28, 0, 0),
            "mixamorig:LeftArm": (-42, 0, 26),
            "mixamorig:RightArm": (28, 0, -26),
        }),
        (20, (0, 0, 0), {}),
    ]))

    clips.append(make_clip(armature, idle_pose, "kick_axe_stomp", [
        (0, (0, 0, 0), {}),
        (6, (0, 0, 0.04), {
            "mixamorig:Hips": (-12, 8, 0),
            "mixamorig:Spine1": (-10, 0, 0),
            "mixamorig:RightUpLeg": (-102, 0, 6),
            "mixamorig:RightLeg": (28, 0, 0),
            "mixamorig:LeftArm": (-48, 0, 18),
            "mixamorig:RightArm": (-48, 0, -18),
        }),
        (12, (0, 0.05, -0.10), {
            "mixamorig:Hips": (14, 10, 0),
            "mixamorig:Spine1": (16, 0, 0),
            "mixamorig:RightUpLeg": (-36, 0, 6),
            "mixamorig:RightLeg": (42, 0, 0),
            "mixamorig:LeftUpLeg": (-28, 0, -6),
            "mixamorig:LeftLeg": (44, 0, 0),
            "mixamorig:LeftArm": (22, 0, 24),
            "mixamorig:RightArm": (22, 0, -24),
        }),
        (20, (0, 0, 0), {}),
    ]))

    clips.append(make_clip(armature, idle_pose, "dodge_evade_step", [
        (0, (0, 0, 0), {}),
        (5, (0, -0.12, -0.12), {
            "mixamorig:Hips": (16, -24, 0),
            "mixamorig:Spine1": (18, 14, 0),
            "mixamorig:LeftUpLeg": (-42, 0, -12),
            "mixamorig:LeftLeg": (66, 0, 0),
            "mixamorig:RightUpLeg": (-34, 0, 12),
            "mixamorig:RightLeg": (58, 0, 0),
            "mixamorig:LeftArm": (-48, 0, 28),
            "mixamorig:LeftForeArm": (0, 0, -88),
            "mixamorig:RightArm": (-42, 0, -24),
            "mixamorig:RightForeArm": (0, 0, 82),
        }),
        (12, (0, -0.06, -0.06), {
            "mixamorig:Hips": (10, -12, 0),
            "mixamorig:Spine1": (10, 8, 0),
        }),
        (18, (0, 0, 0), {}),
    ]))

    # =========================================================================
    # 3. CHIÊU THỨC VỚI KIẾM (Sword Arts / Kiếm Pháp)
    # =========================================================================
    clips.append(make_clip(armature, sword_pose, "sword_draw_gather", [
        (0, (0, 0, 0), {}),
        (6, (0, -0.03, -0.06), {
            "mixamorig:Hips": (8, -24, 0),
            "mixamorig:Spine1": (8, -16, 0),
            "mixamorig:RightArm": (-20, -22, 34),
            "mixamorig:RightForeArm": (0, 0, 48),
            "mixamorig:LeftArm": (-28, 14, -22),
            "mixamorig:LeftForeArm": (0, 0, -72),
            "mixamorig:LeftUpLeg": (-22, 0, -8),
            "mixamorig:LeftLeg": (34, 0, 0),
        }),
        (13, (0, 0.02, -0.05), {
            "mixamorig:Hips": (4, 14, 0),
            "mixamorig:Spine1": (4, 16, 0),
            "mixamorig:RightArm": (28, -32, -28),
            "mixamorig:RightForeArm": (0, 0, 18),
            "mixamorig:LeftArm": (-68, 12, 10),
            "mixamorig:LeftForeArm": (0, 0, -14),
            "mixamorig:LeftHand": (-18, 0, 0),
        }),
        (20, (0, 0.02, -0.04), {
            "mixamorig:Hips": (4, 14, 0),
            "mixamorig:Spine1": (4, 16, 0),
            "mixamorig:RightArm": (28, -32, -28),
            "mixamorig:RightForeArm": (0, 0, 18),
            "mixamorig:LeftArm": (-68, 12, 10),
            "mixamorig:LeftForeArm": (0, 0, -14),
        }),
        (26, (0, 0, 0), {}),
    ]))

    clips.append(make_clip(armature, sword_pose, "sword_horizontal_cleave", [
        (0, (0, 0, 0), {}),
        (5, (0, -0.03, -0.05), {
            "mixamorig:Hips": (6, -34, 0),
            "mixamorig:Spine1": (6, -26, 0),
            "mixamorig:RightArm": (14, -36, -38),
            "mixamorig:RightForeArm": (0, 0, 32),
            "mixamorig:LeftArm": (-26, 0, 24),
        }),
        (10, (0, 0.08, -0.04), {
            "mixamorig:Hips": (8, 42, 0),
            "mixamorig:Spine1": (10, 36, 0),
            "mixamorig:RightArm": (-56, 46, 22),
            "mixamorig:RightForeArm": (0, 0, 12),
            "mixamorig:RightHand": (0, 0, 22),
            "mixamorig:LeftArm": (18, 0, 28),
            "mixamorig:LeftUpLeg": (-28, 0, -8),
            "mixamorig:LeftLeg": (40, 0, 0),
        }),
        (18, (0, 0, 0), {}),
    ]))

    clips.append(make_clip(armature, sword_pose, "sword_diagonal_backhand", [
        (0, (0, 0, 0), {}),
        (5, (0, -0.02, -0.05), {
            "mixamorig:Hips": (8, 32, 0),
            "mixamorig:Spine1": (8, 24, 0),
            "mixamorig:RightArm": (-48, 38, 28),
            "mixamorig:RightForeArm": (0, 0, 42),
        }),
        (11, (0, 0.07, -0.03), {
            "mixamorig:Hips": (4, -36, 0),
            "mixamorig:Spine1": (4, -30, -8),
            "mixamorig:RightArm": (-74, -32, -26),
            "mixamorig:RightForeArm": (0, 0, 10),
            "mixamorig:LeftArm": (-22, 16, 24),
        }),
        (18, (0, 0, 0), {}),
    ]))

    clips.append(make_clip(armature, sword_pose, "sword_lunge_thrust", [
        (0, (0, 0, 0), {}),
        (4, (0, -0.04, -0.07), {
            "mixamorig:Hips": (10, -24, 0),
            "mixamorig:Spine1": (10, -18, 0),
            "mixamorig:RightArm": (18, -18, -22),
            "mixamorig:RightForeArm": (0, 0, 68),
            "mixamorig:LeftUpLeg": (-26, 0, -8),
            "mixamorig:LeftLeg": (42, 0, 0),
        }),
        (10, (0, 0.14, -0.08), {
            "mixamorig:Hips": (14, 24, 0),
            "mixamorig:Spine1": (16, 20, 0),
            "mixamorig:RightArm": (-76, 8, 8),
            "mixamorig:RightForeArm": (0, 0, 6),
            "mixamorig:RightHand": (18, 0, 0),
            "mixamorig:LeftArm": (28, -16, 28),
            "mixamorig:LeftForeArm": (0, 0, -24),
            "mixamorig:LeftUpLeg": (-44, 0, -10),
            "mixamorig:LeftLeg": (58, 0, 0),
            "mixamorig:RightUpLeg": (24, 0, 10),
            "mixamorig:RightLeg": (18, 0, 0),
        }),
        (15, (0, 0.10, -0.06), {
            "mixamorig:Hips": (12, 20, 0),
            "mixamorig:RightArm": (-70, 8, 8),
        }),
        (22, (0, 0, 0), {}),
    ]))

    clips.append(make_clip(armature, sword_pose, "sword_parry_guard", [
        (0, (0, 0, 0), {}),
        (4, (0, -0.02, -0.05), {
            "mixamorig:Hips": (8, -12, 0),
            "mixamorig:Spine1": (6, -8, 0),
            "mixamorig:RightArm": (-62, 26, 22),
            "mixamorig:RightForeArm": (0, 0, 78),
            "mixamorig:LeftArm": (-48, -16, 22),
            "mixamorig:LeftForeArm": (0, 0, -78),
            "mixamorig:LeftUpLeg": (-22, 0, -8),
            "mixamorig:LeftLeg": (34, 0, 0),
        }),
        (12, (0, -0.02, -0.04), {
            "mixamorig:Hips": (6, -10, 0),
            "mixamorig:RightArm": (-60, 24, 22),
            "mixamorig:RightForeArm": (0, 0, 74),
            "mixamorig:LeftArm": (-46, -14, 22),
            "mixamorig:LeftForeArm": (0, 0, -74),
        }),
        (18, (0, 0, 0), {}),
    ]))

    return clips


def main():
    CLIPS_DIR.mkdir(parents=True, exist_ok=True)
    for old_glb in CLIPS_DIR.glob("*.glb"):
        old_glb.unlink()

    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(ANIM_BANK_GLB))
    armature = next(o for o in bpy.data.objects if o.type == "ARMATURE")

    # Strip Blender's glTF-import armature suffix (e.g. "_Humanoid_Armature")
    suffix = f"_{armature.name}"
    for act in bpy.data.actions:
        if act.name.endswith(suffix):
            act.name = act.name[: -len(suffix)]
    if armature.animation_data and armature.animation_data.nla_tracks:
        for trk in list(armature.animation_data.nla_tracks):
            armature.animation_data.nla_tracks.remove(trk)

    # Capture natural base poses BEFORE removing any actions
    idle_pose = capture_action_frame0(armature, "idle")
    sword_pose = capture_action_frame0(armature, "loco_idle_sword.001")

    # Remove all old combat actions, keeping ONLY basic locomotion + casting
    removed = []
    for act in list(bpy.data.actions):
        if act.name not in KEEP_BASE_CLIPS:
            removed.append(act.name)
            bpy.data.actions.remove(act)
    print(f"Removed {len(removed)} old combat actions: {sorted(removed)}")
    print(f"Kept {len(bpy.data.actions)} base actions: {sorted(a.name for a in bpy.data.actions)}")

    # Build new non-elemental Martial Arts suite
    new_clips = build_all_martial_clips(armature, idle_pose, sword_pose)

    for act in new_clips:
        export_single_clip(armature, act, CLIPS_DIR / f"{act.name}.glb")

    for act in bpy.data.actions:
        act.use_fake_user = True

    bpy.ops.object.select_all(action="DESELECT")
    armature.select_set(True)
    bpy.context.view_layer.objects.active = armature

    bpy.ops.export_scene.gltf(
        filepath=str(ANIM_BANK_GLB),
        export_format="GLB",
        use_selection=True,
        export_animations=True,
        export_nla_strips=False,
        export_draco_mesh_compression_enable=False,
        export_yup=True,
    )
    print(f"[REBUILT BANK] {ANIM_BANK_GLB} total actions={len(bpy.data.actions)}")
    for i, a in enumerate(sorted(bpy.data.actions, key=lambda x: x.name)):
        print(f"  [{i:2d}] {a.name}")


if __name__ == "__main__":
    main()
