#!/usr/bin/env python3
"""
scripts/build_metal_sword_anims.py

Creates 3 Wuxia Metal-element (Kiem Phap + Am Khi) animations on the standard
20-bone Mixamo rig:
  1. metal_sword_gather   (Phase 1: Sword draw & Kiếm Quyết summon stance)
  2. metal_blade_barrage  (Phase 2A: Rapid sword-finger / dagger volley)
  3. metal_dash_slash     (Phase 2B: Lunging horizontal-diagonal cleave)

Exports:
  - Individual standalone GLB clips in assets/characters/animations/clips/
  - Merged animation bank in assets/characters/animations/humanoid_anims.glb
    (preserving all existing animations, export_nla_strips=False, no Draco)

Run:
  /Applications/Blender.app/Contents/MacOS/Blender --background --python scripts/build_metal_sword_anims.py
"""

import math
from pathlib import Path

import bpy
from mathutils import Euler, Vector

REPO_ROOT = Path(__file__).resolve().parent.parent
ANIM_BANK_GLB = REPO_ROOT / "assets/characters/animations/humanoid_anims.glb"
CLIPS_DIR = REPO_ROOT / "assets/characters/animations/clips"
CLIPS_DIR.mkdir(parents=True, exist_ok=True)


def clear_pose(armature):
    for pb in armature.pose.bones:
        pb.rotation_mode = "QUATERNION"
        pb.location = (0.0, 0.0, 0.0)
        pb.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
        pb.scale = (1.0, 1.0, 1.0)


def set_pose_key(armature, frame, hip_loc=None, bone_rots_deg=None):
    bpy.context.scene.frame_set(frame)
    clear_pose(armature)

    if hip_loc is not None:
        hips = armature.pose.bones["mixamorig:Hips"]
        hips.location = Vector(hip_loc)
        hips.keyframe_insert(data_path="location", frame=frame)

    if bone_rots_deg:
        for bname, deg_xyz in bone_rots_deg.items():
            pb = armature.pose.bones.get(bname)
            if not pb:
                continue
            pb.rotation_mode = "QUATERNION"
            pb.rotation_quaternion = Euler(
                (math.radians(deg_xyz[0]), math.radians(deg_xyz[1]), math.radians(deg_xyz[2])),
                "XYZ",
            ).to_quaternion()

    for pb in armature.pose.bones:
        if pb.name.startswith("mixamorig:"):
            pb.keyframe_insert(data_path="rotation_quaternion", frame=frame)


def apply_bezier_easing(action):
    for fc in action.fcurves:
        for kp in fc.keyframe_points:
            kp.interpolation = "BEZIER"
            kp.handle_left_type = "AUTO_CLAMPED"
            kp.handle_right_type = "AUTO_CLAMPED"


def sword_stance_base():
    """Poised wuxia sword stance."""
    return {
        "mixamorig:Hips": (4, -18, 0),
        "mixamorig:Spine": (6, 10, 0),
        "mixamorig:Spine1": (5, 8, 0),
        "mixamorig:Neck": (-4, 0, 0),
        "mixamorig:LeftArm": (-52, 0, -28),
        "mixamorig:LeftForeArm": (0, 0, -76),
        "mixamorig:RightArm": (-58, 0, 30),
        "mixamorig:RightForeArm": (0, 0, 68),
        "mixamorig:LeftUpLeg": (-24, 0, -12),
        "mixamorig:LeftLeg": (36, 0, 0),
        "mixamorig:LeftFoot": (-12, 0, 0),
        "mixamorig:RightUpLeg": (-14, 0, 12),
        "mixamorig:RightLeg": (28, 0, 0),
        "mixamorig:RightFoot": (-14, 0, 0),
    }


def build_metal_sword_gather(armature):
    """Phase 1: Draw sword across chest and raise left Kiếm Quyết seal to summon blades."""
    act = bpy.data.actions.new("metal_sword_gather")
    armature.animation_data_create()
    armature.animation_data.action = act

    # Frame 0: poised start
    set_pose_key(armature, 0, (0, 0, -0.04), sword_stance_base())

    # Frame 6: coil inward, sword held diagonally across chest
    coil = sword_stance_base()
    coil.update({
        "mixamorig:Hips": (10, -28, 0),
        "mixamorig:Spine": (8, -10, 0),
        "mixamorig:Spine1": (8, -12, 0),
        "mixamorig:RightArm": (-38, 22, 48),
        "mixamorig:RightForeArm": (0, 0, 108),
        "mixamorig:LeftArm": (-42, -15, -40),
        "mixamorig:LeftForeArm": (0, 0, -96),
        "mixamorig:LeftUpLeg": (-34, 0, -16),
        "mixamorig:LeftLeg": (52, 0, 0),
        "mixamorig:RightUpLeg": (-24, 0, 16),
        "mixamorig:RightLeg": (44, 0, 0),
    })
    set_pose_key(armature, 6, (0, 0, -0.10), coil)

    # Frame 14: sweep sword out to right-back, left sword-finger raised forward
    summon = sword_stance_base()
    summon.update({
        "mixamorig:Hips": (6, 12, 0),
        "mixamorig:Spine": (4, 14, -4),
        "mixamorig:Spine1": (2, 16, -4),
        "mixamorig:Head": (-4, -18, 0),
        "mixamorig:RightArm": (-76, -24, -18),
        "mixamorig:RightForeArm": (0, 0, 26),
        "mixamorig:RightHand": (0, 0, 12),
        "mixamorig:LeftArm": (-18, 12, -72),
        "mixamorig:LeftForeArm": (0, 0, -22),
        "mixamorig:LeftHand": (-10, 0, 0),
        "mixamorig:LeftUpLeg": (-30, 0, -14),
        "mixamorig:LeftLeg": (45, 0, 0),
        "mixamorig:RightUpLeg": (-18, 0, 14),
        "mixamorig:RightLeg": (34, 0, 0),
    })
    set_pose_key(armature, 14, (0, 0, -0.07), summon)

    # Frame 20: hold crisp summon apex
    set_pose_key(armature, 20, (0, 0, -0.06), summon)

    apply_bezier_easing(act)
    act.use_fake_user = True
    return act


def build_metal_blade_barrage(armature):
    """Phase 2A: Rapid alternating sword-finger & blade flicks launching flying daggers."""
    act = bpy.data.actions.new("metal_blade_barrage")
    armature.animation_data_create()
    armature.animation_data.action = act

    # Frame 0: ready volley
    set_pose_key(armature, 0, (0, 0, -0.06), sword_stance_base())

    # Frame 5: Left hand flicks forward
    left_flick = sword_stance_base()
    left_flick.update({
        "mixamorig:Hips": (6, 16, 0),
        "mixamorig:Spine": (6, 18, 0),
        "mixamorig:Spine1": (6, 16, 0),
        "mixamorig:LeftArm": (-12, 18, -82),
        "mixamorig:LeftForeArm": (0, 0, -12),
        "mixamorig:RightArm": (-62, -16, 22),
        "mixamorig:RightForeArm": (0, 0, 82),
    })
    set_pose_key(armature, 5, (0, 0.04, -0.06), left_flick)

    # Frame 11: Right sword hand snaps forward
    right_flick = sword_stance_base()
    right_flick.update({
        "mixamorig:Hips": (6, -24, 0),
        "mixamorig:Spine": (6, -16, 0),
        "mixamorig:Spine1": (6, -14, 0),
        "mixamorig:RightArm": (-14, -18, 80),
        "mixamorig:RightForeArm": (0, 0, 14),
        "mixamorig:LeftArm": (-58, 12, -24),
        "mixamorig:LeftForeArm": (0, 0, -86),
    })
    set_pose_key(armature, 11, (0, 0.06, -0.06), right_flick)

    # Frame 18: return to poised stance
    set_pose_key(armature, 18, (0, 0, -0.05), sword_stance_base())

    apply_bezier_easing(act)
    act.use_fake_user = True
    return act


def build_metal_dash_slash(armature):
    """Phase 2B: Forward lunging cleave with wide sweeping right-hand sword arc."""
    act = bpy.data.actions.new("metal_dash_slash")
    armature.animation_data_create()
    armature.animation_data.action = act

    # Frame 0: wind-up back
    windup = sword_stance_base()
    windup.update({
        "mixamorig:Hips": (12, -38, 0),
        "mixamorig:Spine": (10, -22, 0),
        "mixamorig:Spine1": (8, -18, 0),
        "mixamorig:RightArm": (-48, -32, 10),
        "mixamorig:RightForeArm": (0, 0, 42),
        "mixamorig:LeftArm": (-36, 10, -52),
        "mixamorig:LeftForeArm": (0, 0, -64),
        "mixamorig:LeftUpLeg": (-36, 0, -14),
        "mixamorig:LeftLeg": (56, 0, 0),
    })
    set_pose_key(armature, 0, (0, -0.04, -0.10), windup)

    # Frame 8: explosive forward lunge + wide horizontal cleave
    cleave = sword_stance_base()
    cleave.update({
        "mixamorig:Hips": (14, 42, 0),
        "mixamorig:Spine": (12, 26, 4),
        "mixamorig:Spine1": (10, 22, 4),
        "mixamorig:Head": (-8, -32, 0),
        "mixamorig:RightArm": (-16, 48, 76),
        "mixamorig:RightForeArm": (0, 0, 12),
        "mixamorig:RightHand": (0, 0, -18),
        "mixamorig:LeftArm": (-66, -24, -22),
        "mixamorig:LeftForeArm": (0, 0, -34),
        "mixamorig:LeftUpLeg": (-46, 0, -10),
        "mixamorig:LeftLeg": (64, 0, 0),
        "mixamorig:RightUpLeg": (18, 0, 12),
        "mixamorig:RightLeg": (18, 0, 0),
    })
    set_pose_key(armature, 8, (0, 0.14, -0.12), cleave)

    # Frame 14: follow-through hold
    set_pose_key(armature, 14, (0, 0.12, -0.10), cleave)

    # Frame 20: recover to stance
    set_pose_key(armature, 20, (0, 0, -0.04), sword_stance_base())

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


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(ANIM_BANK_GLB))
    armature = next(o for o in bpy.data.objects if o.type == "ARMATURE")

    existing = [a.name for a in bpy.data.actions]
    print(f"Loaded {len(existing)} existing actions from {ANIM_BANK_GLB.name}")

    new_names = [
        "metal_sword_gather",
        "metal_blade_barrage",
        "metal_dash_slash",
    ]
    for n in new_names:
        if n in bpy.data.actions:
            bpy.data.actions.remove(bpy.data.actions[n])

    a_gather  = build_metal_sword_gather(armature)
    a_barrage = build_metal_blade_barrage(armature)
    a_slash   = build_metal_dash_slash(armature)

    for act in [a_gather, a_barrage, a_slash]:
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
    print(f"[MERGED BANK] {ANIM_BANK_GLB} total actions={len(bpy.data.actions)}")


if __name__ == "__main__":
    main()
