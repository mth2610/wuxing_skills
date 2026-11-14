#!/usr/bin/env python3
"""
scripts/build_earth_unarmed_anims.py

Creates the 4 Earth (Tho / Hung Gar) unarmed combat animations inspired by
Avatar: The Last Airbender:
  1. earth_pillar_punch  (CHAR_ANIM_PUNCH)
  2. earth_stomp_slam    (CHAR_ANIM_PALM / CHAR_ANIM_CAST)
  3. earth_heavy_kick    (CHAR_ANIM_KICK)
  4. earth_brace_dodge   (CHAR_ANIM_DODGE)

Exports:
  - Individual standalone GLB clips in assets/characters/animations/clips/
  - Merged animation bank in assets/characters/animations/humanoid_anims.glb
    (preserving all existing animations)
  - Verification contact-sheet PNGs in scratch/

Run:
  /Applications/Blender.app/Contents/MacOS/Blender --background --python scripts/build_earth_unarmed_anims.py
"""

import math
from pathlib import Path

import bpy
from mathutils import Euler, Vector

REPO_ROOT = Path(__file__).resolve().parent.parent
ANIM_BANK_GLB = REPO_ROOT / "assets/characters/animations/humanoid_anims.glb"
CLIPS_DIR = REPO_ROOT / "assets/characters/animations/clips"
REF_BODY_GLB = REPO_ROOT / "assets/characters/bodies/human_body.glb"
PREVIEW_DIR = Path(
    "/Users/mth2610/.gemini/antigravity-cli/brain/d53dbc6b-8a92-40ed-b8a4-852145fe0fe5/scratch"
)
CLIPS_DIR.mkdir(parents=True, exist_ok=True)
PREVIEW_DIR.mkdir(parents=True, exist_ok=True)


def clear_pose(armature):
    for pb in armature.pose.bones:
        pb.rotation_mode = "QUATERNION"
        pb.location = (0.0, 0.0, 0.0)
        pb.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
        pb.scale = (1.0, 1.0, 1.0)


def set_pose_key(armature, frame, hip_loc=None, bone_rots_deg=None):
    """
    Set pose at `frame` with Euler XYZ (degrees) converted to QUATERNION
    (matching Mixamo/glTF standard and eliminating rotation_mode mismatches).
    """
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

    # Always key all 20 bones at every keyframe so glTF export has clean curves
    for pb in armature.pose.bones:
        if pb.name.startswith("mixamorig:"):
            pb.keyframe_insert(data_path="rotation_quaternion", frame=frame)


def apply_bezier_easing(action):
    for fc in action.fcurves:
        for kp in fc.keyframe_points:
            kp.interpolation = "BEZIER"
            kp.handle_left_type = "AUTO_CLAMPED"
            kp.handle_right_type = "AUTO_CLAMPED"


def horse_stance_base():
    """Hung Gar wide rooted horse stance (Trung Binh Tan)."""
    return {
        "mixamorig:Hips": (6, 0, 0),
        "mixamorig:Spine": (6, 0, 0),
        "mixamorig:Spine1": (5, 0, 0),
        "mixamorig:LeftUpLeg": (-42, 0, -24),
        "mixamorig:LeftLeg": (62, 0, 0),
        "mixamorig:LeftFoot": (-15, 0, 8),
        "mixamorig:RightUpLeg": (-42, 0, 24),
        "mixamorig:RightLeg": (62, 0, 0),
        "mixamorig:RightFoot": (-15, 0, -8),
    }


def guard_stance_base():
    """Ready martial guard stance."""
    return {
        "mixamorig:Hips": (5, -12, 0),
        "mixamorig:Spine": (5, 8, 0),
        "mixamorig:Spine1": (4, 6, 0),
        "mixamorig:LeftArm": (-62, 0, -32),
        "mixamorig:LeftForeArm": (0, 0, -88),
        "mixamorig:RightArm": (-68, 0, 26),
        "mixamorig:RightForeArm": (0, 0, 95),
        "mixamorig:LeftUpLeg": (-22, 0, -10),
        "mixamorig:LeftLeg": (34, 0, 0),
        "mixamorig:LeftFoot": (-10, 0, 0),
        "mixamorig:RightUpLeg": (-12, 0, 10),
        "mixamorig:RightLeg": (28, 0, 0),
        "mixamorig:RightFoot": (-14, 0, 0),
    }


def build_earth_pillar_punch(armature):
    """
    earth_pillar_punch (frames 0..34):
    Drop into wide Hung Gar horse stance, chamber right fist at hip,
    drive explosive torso-twisted straight punch forward, hold impact, recover.
    """
    act = bpy.data.actions.new("earth_pillar_punch")
    armature.animation_data_create()
    armature.animation_data.action = act

    # Frame 0: Ready guard
    set_pose_key(armature, 0, (0, 0, -0.04), guard_stance_base())

    # Frame 8: Wind-up — drop deep into horse stance, right fist chambered at hip
    p_wind = horse_stance_base()
    p_wind.update({
        "mixamorig:Hips": (-6, -18, 0),
        "mixamorig:Spine1": (-6, -14, 0),
        "mixamorig:LeftArm": (-58, 0, -48),
        "mixamorig:LeftForeArm": (0, 0, -68),
        "mixamorig:RightArm": (-65, 0, -24),     # Pulled back
        "mixamorig:RightForeArm": (0, 0, 105),   # Chambered tight
    })
    set_pose_key(armature, 8, (0, 0, -0.18), p_wind)

    # Frame 14: Impact! Explosive torso twist + right straight punch snap at chest height
    p_hit = horse_stance_base()
    p_hit.update({
        "mixamorig:Hips": (10, 24, 0),
        "mixamorig:Spine": (8, 18, 0),
        "mixamorig:Spine1": (8, 20, 0),
        "mixamorig:Spine2": (6, 14, 0),
        "mixamorig:Head": (-14, -42, 0),         # Eyes locked forward
        "mixamorig:LeftArm": (-76, 0, -14),      # Left fist pulled back to ribs
        "mixamorig:LeftForeArm": (0, 0, -110),
        "mixamorig:RightShoulder": (0, 0, 12),
        "mixamorig:RightArm": (-12, 0, 86),      # Horizontal chest-height straight punch forward
        "mixamorig:RightForeArm": (0, 0, 10),    # Nearly straight punch
    })
    set_pose_key(armature, 14, (0, -0.06, -0.20), p_hit)

    # Frame 21: Hold power impact
    set_pose_key(armature, 21, (0, -0.06, -0.19), p_hit)

    # Frame 34: Recover to guard
    set_pose_key(armature, 34, (0, 0, -0.04), guard_stance_base())

    apply_bezier_easing(act)
    act.use_fake_user = True
    return act


def build_earth_stomp_slam(armature):
    """
    earth_stomp_slam (frames 0..40):
    Signature ATLA Earthbending move: lift right knee high & gather fists at chest,
    stomp right foot down into deep horse stance + double downward slam!
    """
    act = bpy.data.actions.new("earth_stomp_slam")
    armature.animation_data_create()
    armature.animation_data.action = act

    # Frame 0: Guard
    set_pose_key(armature, 0, (0, 0, -0.04), guard_stance_base())

    # Frame 11: Chamber — raise right knee high, both fists drawn up to chest/shoulders
    p_lift = {
        "mixamorig:Hips": (4, 0, -4),
        "mixamorig:Spine": (6, 0, 0),
        "mixamorig:Spine1": (6, 0, 0),
        "mixamorig:Head": (-8, 0, 0),
        "mixamorig:LeftArm": (-28, 0, -52),
        "mixamorig:LeftForeArm": (0, 0, -108),
        "mixamorig:RightArm": (-28, 0, 52),
        "mixamorig:RightForeArm": (0, 0, 108),
        "mixamorig:LeftUpLeg": (-16, 0, -4),
        "mixamorig:LeftLeg": (28, 0, 0),
        "mixamorig:LeftFoot": (-10, 0, 0),
        "mixamorig:RightUpLeg": (-92, 0, 8),     # High knee chamber
        "mixamorig:RightLeg": (105, 0, 0),
        "mixamorig:RightFoot": (-12, 0, 0),
    }
    set_pose_key(armature, 11, (0, 0.02, -0.02), p_lift)

    # Frame 18: STOMP + Double Ground Slam! (lean forward powerfully)
    p_slam = horse_stance_base()
    p_slam.update({
        "mixamorig:Hips": (16, 0, 0),
        "mixamorig:Spine": (18, 0, 0),
        "mixamorig:Spine1": (16, 0, 0),
        "mixamorig:Head": (-22, 0, 0),
        "mixamorig:LeftArm": (-64, 0, -48),      # Driven downward/forward
        "mixamorig:LeftForeArm": (0, 0, -25),
        "mixamorig:RightArm": (-64, 0, 48),
        "mixamorig:RightForeArm": (0, 0, 25),
    })
    set_pose_key(armature, 18, (0, -0.05, -0.24), p_slam)

    # Frame 26: Ground tremor follow-through (slight bounce)
    set_pose_key(armature, 26, (0, -0.05, -0.21), p_slam)

    # Frame 40: Recover
    set_pose_key(armature, 40, (0, 0, -0.04), guard_stance_base())

    apply_bezier_easing(act)
    act.use_fake_user = True
    return act


def build_earth_heavy_kick(armature):
    """
    earth_heavy_kick (frames 0..36):
    Hung Gar heavy front thrust kick (Dap Chan).
    """
    act = bpy.data.actions.new("earth_heavy_kick")
    armature.animation_data_create()
    armature.animation_data.action = act

    set_pose_key(armature, 0, (0, 0, -0.04), guard_stance_base())

    # Frame 9: Chamber right knee high, lean back slightly
    p_chamber = {
        "mixamorig:Hips": (8, -10, -4),
        "mixamorig:Spine": (6, 8, 0),
        "mixamorig:LeftArm": (-58, 0, -45),
        "mixamorig:LeftForeArm": (0, 0, -90),
        "mixamorig:RightArm": (-72, 0, 20),
        "mixamorig:RightForeArm": (0, 0, 95),
        "mixamorig:LeftUpLeg": (-10, 0, -4),
        "mixamorig:LeftLeg": (18, 0, 0),
        "mixamorig:RightUpLeg": (-95, 0, 6),
        "mixamorig:RightLeg": (108, 0, 0),
    }
    set_pose_key(armature, 9, (0, 0.02, -0.02), p_chamber)

    # Frame 16: Full forward heel-thrust kick
    p_kick = {
        "mixamorig:Hips": (16, -8, -4),
        "mixamorig:Spine": (10, 8, 0),
        "mixamorig:Head": (-12, 0, 0),
        "mixamorig:LeftArm": (-55, 0, -48),
        "mixamorig:LeftForeArm": (0, 0, -90),
        "mixamorig:RightArm": (-75, 0, -10),
        "mixamorig:RightForeArm": (0, 0, 85),
        "mixamorig:LeftUpLeg": (-8, 0, -4),
        "mixamorig:LeftLeg": (14, 0, 0),
        "mixamorig:RightUpLeg": (-96, 0, 4),
        "mixamorig:RightLeg": (8, 0, 0),         # Extended leg
        "mixamorig:RightFoot": (-20, 0, 0),      # Heel thrust
    }
    set_pose_key(armature, 16, (0, -0.03, -0.02), p_kick)

    # Frame 24: Re-chamber
    set_pose_key(armature, 24, (0, 0.0, -0.03), p_chamber)

    # Frame 36: Recover
    set_pose_key(armature, 36, (0, 0, -0.04), guard_stance_base())

    apply_bezier_easing(act)
    act.use_fake_user = True
    return act


def build_earth_brace_dodge(armature):
    """
    earth_brace_dodge (frames 0..26):
    Low grounded lateral evade step with tight Hung Gar forearm guard.
    """
    act = bpy.data.actions.new("earth_brace_dodge")
    armature.animation_data_create()
    armature.animation_data.action = act

    set_pose_key(armature, 0, (0, 0, -0.04), guard_stance_base())

    # Frame 10: Low sidestep with crossed iron-bridge guard (lean slightly forward)
    p_evade = horse_stance_base()
    p_evade.update({
        "mixamorig:Hips": (12, -18, 6),
        "mixamorig:Spine": (12, 12, 4),
        "mixamorig:Head": (-12, 6, -4),
        "mixamorig:LeftArm": (-56, 0, -56),
        "mixamorig:LeftForeArm": (0, 0, -112),
        "mixamorig:RightArm": (-52, 0, 62),
        "mixamorig:RightForeArm": (0, 0, 118),
    })
    set_pose_key(armature, 10, (-0.18, 0.04, -0.20), p_evade)

    # Frame 16: Hold low brace
    set_pose_key(armature, 16, (-0.16, 0.03, -0.18), p_evade)

    # Frame 26: Recover
    set_pose_key(armature, 26, (0, 0, -0.04), guard_stance_base())

    apply_bezier_easing(act)
    act.use_fake_user = True
    return act


def export_single_clip(armature, action, out_path):
    """Export a single standalone animation GLB (skeleton + 1 action)."""
    armature.animation_data.action = action
    # Temporarily stash only this action in NLA or export active action
    saved = {}
    for a in bpy.data.actions:
        saved[a.name] = a.use_fake_user
        a.use_fake_user = (a == action)

    # Remove other actions temporarily? No, export_nla_strips=False exports active action only!
    bpy.ops.object.select_all(action="DESELECT")
    armature.select_set(True)
    bpy.context.view_layer.objects.active = armature

    bpy.ops.export_scene.gltf(
        filepath=str(out_path),
        export_format="GLB",
        use_selection=True,
        export_animations=True,
        export_nla_strips=False,
        export_current_frame=False,
        export_yup=True,
    )
    for name, val in saved.items():
        if name in bpy.data.actions:
            bpy.data.actions[name].use_fake_user = val
    print(f"  [CLIP] {out_path.name} ({int(action.frame_range[1])} frames)")


def render_pose_preview(armature, action, frame, out_png):
    """Import human_body mesh temporarily, pose it at `frame`, and render 3/4 view."""
    bpy.ops.import_scene.gltf(filepath=str(REF_BODY_GLB))
    ref_arm = next(o for o in bpy.data.objects if o.type == "ARMATURE" and o != armature)
    ref_mesh = next(o for o in bpy.data.objects if o.type == "MESH")

    ref_arm.animation_data_create()
    ref_arm.animation_data.action = action

    scene = bpy.context.scene
    scene.frame_set(frame)
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = 720
    scene.render.resolution_y = 960
    scene.render.image_settings.file_format = "PNG"

    world = bpy.data.worlds.get("PreviewWorld") or bpy.data.worlds.new("PreviewWorld")
    scene.world = world
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.78, 0.80, 0.84, 1.0)
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.7

    if "PreviewCam" not in bpy.data.objects:
        ldata = bpy.data.lights.new("Key", type="AREA")
        ldata.energy = 180.0
        lobj = bpy.data.objects.new("Key", ldata)
        lobj.location = (1.8, -2.6, 2.3)
        lobj.rotation_euler = (Vector((0, 0, 0.9)) - lobj.location).to_track_quat("-Z", "Y").to_euler()
        bpy.context.collection.objects.link(lobj)

        cdata = bpy.data.cameras.new("PreviewCam")
        cdata.lens = 45.0
        cam = bpy.data.objects.new("PreviewCam", cdata)
        cam.location = (2.1, -2.5, 1.15)
        cam.rotation_euler = (Vector((0, 0, 0.88)) - cam.location).to_track_quat("-Z", "Y").to_euler()
        bpy.context.collection.objects.link(cam)
        scene.camera = cam

    scene.render.filepath = str(out_png)
    bpy.ops.render.render(write_still=True)

    bpy.data.objects.remove(ref_mesh, do_unlink=True)
    bpy.data.objects.remove(ref_arm, do_unlink=True)


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(ANIM_BANK_GLB))
    armature = next(o for o in bpy.data.objects if o.type == "ARMATURE")

    # Normalize existing action names if needed, keep all existing actions
    existing = [a.name for a in bpy.data.actions]
    print(f"Loaded {len(existing)} existing actions from {ANIM_BANK_GLB.name}")

    # Remove old versions of our 4 new actions if re-running
    new_names = [
        "earth_pillar_punch",
        "earth_stomp_slam",
        "earth_heavy_kick",
        "earth_brace_dodge",
    ]
    for n in new_names:
        if n in bpy.data.actions:
            bpy.data.actions.remove(bpy.data.actions[n])

    a_punch = build_earth_pillar_punch(armature)
    a_slam  = build_earth_stomp_slam(armature)
    a_kick  = build_earth_heavy_kick(armature)
    a_dodge = build_earth_brace_dodge(armature)

    # Export individual standalone GLB clips
    for act in [a_punch, a_slam, a_kick, a_dodge]:
        export_single_clip(armature, act, CLIPS_DIR / f"{act.name}.glb")

    # Render key impact pose of each animation for visual check
    render_pose_preview(armature, a_punch, 14, PREVIEW_DIR / "anim_earth_punch.png")
    render_pose_preview(armature, a_slam,  18, PREVIEW_DIR / "anim_earth_slam.png")
    render_pose_preview(armature, a_kick,  16, PREVIEW_DIR / "anim_earth_kick.png")
    render_pose_preview(armature, a_dodge, 10, PREVIEW_DIR / "anim_earth_dodge.png")

    # Export all actions cleanly WITHOUT NLA duplication (matching retarget_all_animations.py)
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
