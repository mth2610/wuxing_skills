#!/usr/bin/env python3
"""
scripts/convert_nhanvat1.py

Converts unskinned hierarchical node-based model (nhanvat1.glb) into a
game-ready Humanoid Skinned Model with Armature + Skinned Mesh + 7 custom wuxia animations
+ shared standard animations.
"""

import sys
import os
import re
from mathutils import Vector, Matrix

try:
    import bpy
except ImportError:
    sys.exit("Error: Must be run inside Blender's Python interpreter.")

INPUT_GLB = "/Users/mth2610/Desktop/nhanvat1.glb"
OUTPUT_GLB = "assets/characters/models/nhanvat1.glb"
OUTPUT_PLAYER_GLB = "assets/characters/player.glb"
ORIG_GLB = "assets/characters/player_original.glb"
SCALE_FACTOR = 0.45  # Standardizes height to 1.78m human scale

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=INPUT_GLB)

print("1. Scaling entire hierarchy via root node...")
root = bpy.data.objects.get('LacHongKiemTien_IsometricMixamo')
if root:
    root.scale = Vector((SCALE_FACTOR, SCALE_FACTOR, SCALE_FACTOR))

bone_nodes = {obj.name: obj for obj in bpy.context.scene.objects if obj.type == 'EMPTY' and 'mixamorig' in obj.name}
mesh_objs = [obj for obj in bpy.context.scene.objects if obj.type == 'MESH']
print(f"Found {len(bone_nodes)} bone nodes and {len(mesh_objs)} submeshes.")

# BindTPose at frame 0
for name, node in bone_nodes.items():
    act = bpy.data.actions.get(f'KiemTien_Iso_BindTPose_{name}')
    if act and node.animation_data:
        node.animation_data.action = act

bpy.context.scene.frame_set(0)
bpy.context.view_layer.update()

# Trace bone ancestor for each mesh
mesh_bone_map = {}
for m in mesh_objs:
    curr = m
    bone_ancestor = None
    while curr.parent:
        if 'mixamorig' in curr.parent.name:
            bone_ancestor = curr.parent.name
            break
        curr = curr.parent
    mesh_bone_map[m] = bone_ancestor or 'mixamorig:Hips'

print(f"Mapped {len(mesh_bone_map)}/{len(mesh_objs)} meshes to bone ancestors.")

# 2. Build Armature from BindTPose
print("2. Building Armature from BindTPose...")
arm_data = bpy.data.armatures.new("PlayerHumanoid")
arm_obj = bpy.data.objects.new("PlayerArmature", arm_data)
bpy.context.scene.collection.objects.link(arm_obj)
bpy.context.view_layer.objects.active = arm_obj
bpy.ops.object.mode_set(mode='EDIT')

edit_bones = {}
for name, node in bone_nodes.items():
    eb = arm_data.edit_bones.new(name)
    eb.head = node.matrix_world.translation
    edit_bones[name] = eb

for name, node in bone_nodes.items():
    eb = edit_bones[name]
    if node.parent and node.parent.name in edit_bones:
        eb.parent = edit_bones[node.parent.name]
    children = [edit_bones[c.name] for c in node.children if c.name in edit_bones]
    if children:
        eb.tail = sum((c.head for c in children), Vector((0,0,0))) / len(children)
        if (eb.tail - eb.head).length < 0.001:
            eb.tail = eb.head + Vector((0, 0, 0.05 * SCALE_FACTOR))
    else:
        if eb.parent:
            dir_vec = (eb.head - eb.parent.head)
            if dir_vec.length > 0.001:
                eb.tail = eb.head + dir_vec.normalized() * (0.05 * SCALE_FACTOR)
            else:
                eb.tail = eb.head + Vector((0, 0, 0.05 * SCALE_FACTOR))
        else:
            eb.tail = eb.head + Vector((0, 0, 0.05 * SCALE_FACTOR))

bpy.ops.object.mode_set(mode='OBJECT')
print(f"Created Armature with {len(arm_data.bones)} bones.")

# 3. Make meshes single-user, apply world transforms and assign vertex groups
print("3. Skinning and joining submeshes...")
bpy.ops.object.select_all(action='DESELECT')
for m in mesh_objs:
    m.select_set(True)
bpy.ops.object.make_single_user(type='SELECTED_OBJECTS', object=True, obdata=True)
bpy.ops.object.select_all(action='DESELECT')

for m, bone_name in mesh_bone_map.items():
    mat = m.matrix_world.copy()
    m.parent = None
    m.matrix_world = mat
    bpy.context.view_layer.objects.active = m
    m.select_set(True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    m.select_set(False)

    m.vertex_groups.clear()
    vg = m.vertex_groups.new(name=bone_name)
    vg.add(list(range(len(m.data.vertices))), 1.0, 'REPLACE')

    mod = m.modifiers.new(name="Armature", type='ARMATURE')
    mod.object = arm_obj

# Join into single unified mesh
bpy.ops.object.select_all(action='DESELECT')
for m in mesh_objs:
    m.select_set(True)
bpy.context.view_layer.objects.active = mesh_objs[0]
bpy.ops.object.join()
joined_mesh = bpy.context.view_layer.objects.active
joined_mesh.name = "nhanvat1_mesh"
joined_mesh.parent = arm_obj
print(f"Joined mesh into '{joined_mesh.name}' ({len(joined_mesh.data.vertices)} vertices, {len(joined_mesh.material_slots)} materials).")

# 4. Bake the 7 custom animations
print("4. Baking custom wuxia animations...")
ANIM_DEFS = [
    ("KiemTien_Iso_IdleGuard", "idle"),
    ("KiemTien_Iso_CloudStepRun", "run"),
    ("KiemTien_Iso_ThuanThien3Hit", "attack_combo"),
    ("KiemTien_Iso_LacHongTram", "skill_slash"),
    ("KiemTien_Iso_NguKiemTuKhi", "skill_cast"),
    ("KiemTien_Iso_PhiThanHoiToan", "skill_spin"),
    ("KiemTien_Iso_BindTPose", "tpose"),
]

valid_action_names = set()

bpy.context.view_layer.objects.active = arm_obj
bpy.ops.object.mode_set(mode='POSE')

for src_prefix, clean_anim_name in ANIM_DEFS:
    for name, node in bone_nodes.items():
        pb = arm_obj.pose.bones.get(name)
        if pb:
            c = pb.constraints.new('COPY_TRANSFORMS')
            c.target = node

    max_frame = 0
    for name, node in bone_nodes.items():
        act = bpy.data.actions.get(f"{src_prefix}_{name}")
        if act and node.animation_data:
            node.animation_data.action = act
            max_frame = max(max_frame, int(act.frame_range[1]))

    if max_frame == 0:
        max_frame = 1

    bpy.ops.pose.select_all(action='SELECT')
    bpy.ops.nla.bake(
        frame_start=0,
        frame_end=max_frame,
        only_selected=True,
        visual_keying=True,
        clear_constraints=True,
        bake_types={'POSE'}
    )

    action = arm_obj.animation_data.action if arm_obj.animation_data else None
    if action:
        action.name = clean_anim_name
        valid_action_names.add(clean_anim_name)
        track = arm_obj.animation_data.nla_tracks.new()
        track.name = clean_anim_name
        track.strips.new(clean_anim_name, 0, action)
        print(f"  -> Baked '{clean_anim_name}' ({max_frame} frames).")

bpy.ops.object.mode_set(mode='OBJECT')

# 5. Import standard animations from player_original.glb
if os.path.exists(ORIG_GLB):
    print("5. Importing standard shared animations...")
    bpy.ops.import_scene.gltf(filepath=ORIG_GLB)
    for act in list(bpy.data.actions):
        if "_Armature" in act.name:
            cname = act.name.replace("_Armature", "")
            if cname in valid_action_names:
                cname = f"std_{cname}"
            act.name = cname
            valid_action_names.add(cname)
            track = arm_obj.animation_data.nla_tracks.new()
            track.name = cname
            track.strips.new(cname, 0, act)
            print(f"  -> Added shared '{cname}'.")

# 6. Purge unneeded actions and trim missing bone fcurves
arm_bone_names = {b.name for b in arm_obj.data.bones}
for act in list(bpy.data.actions):
    if act.name not in valid_action_names:
        bpy.data.actions.remove(act)
    else:
        for fc in list(act.fcurves):
            m = re.match(r'pose\.bones\["([^"]+)"\]', fc.data_path)
            if m and m.group(1) not in arm_bone_names:
                act.fcurves.remove(fc)

print(f"Retained {len(bpy.data.actions)} clean actions.")

# 7. Clean up all temporary objects
for node in list(bone_nodes.values()):
    bpy.data.objects.remove(node, do_unlink=True)

for obj in list(bpy.context.scene.objects):
    if obj != arm_obj and obj != joined_mesh:
        bpy.data.objects.remove(obj, do_unlink=True)

# 8. Export game-ready models
os.makedirs(os.path.dirname(OUTPUT_GLB), exist_ok=True)
print(f"8. Exporting to {OUTPUT_GLB}...")
bpy.ops.export_scene.gltf(
    filepath=OUTPUT_GLB,
    export_format='GLB',
    use_selection=False,
    export_apply=False,
    export_animations=True,
    export_skins=True
)

print(f"9. Updating {OUTPUT_PLAYER_GLB}...")
bpy.ops.export_scene.gltf(
    filepath=OUTPUT_PLAYER_GLB,
    export_format='GLB',
    use_selection=False,
    export_apply=False,
    export_animations=True,
    export_skins=True
)

print("All conversions completed successfully!")
