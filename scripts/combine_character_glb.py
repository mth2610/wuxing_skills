#!/usr/bin/env python3
"""
Pipeline script to combine a Mixamo character with separately downloaded 
Mixamo animations into a single game-ready .glb file.

Pipeline Order:
Import -> Mesh -> Skeleton -> Animation -> Material -> Texture -> Optimization -> Export GLB
"""

import sys
import re  # <--- BẮT BUỘC THÊM THƯ VIỆN NÀY ĐỂ XỬ LÝ CHUỖI TÊN XƯƠNG
from pathlib import Path

try:
    import bpy
except ImportError:
    sys.exit(
        "This script must be run inside Blender's Python "
        "(blender --background --python scripts/combine_character_glb.py -- ...)."
    )

def parse_args():
    argv = sys.argv
    if "--" in argv:
        argv = argv[argv.index("--") + 1:]
    else:
        argv = []
    if len(argv) < 2:
        sys.exit(
            "Usage: blender --background --python scripts/combine_character_glb.py -- "
            "<character.fbx> <animations_folder> [output.glb] [decimate_ratio]"
        )
    character_fbx = Path(argv[0])
    anim_folder = Path(argv[1])
    output_glb = Path(argv[2]) if len(argv) > 2 else Path("assets/characters/player.glb")
    decimate_ratio = float(argv[3]) if len(argv) > 3 else 0.25
    max_texture_size = int(argv[4]) if len(argv) > 4 else 1024
    return character_fbx, anim_folder, output_glb, decimate_ratio, max_texture_size


# -------------------------------------------------------------------------
# 1. IMPORT PIPELINE
# -------------------------------------------------------------------------
def pipeline_import(character_fbx, anim_folder):
    # Wipe scene
    bpy.ops.wm.read_factory_settings(use_empty=True)

    # Import Main Character
    bpy.ops.import_scene.fbx(filepath=str(character_fbx))
    armature = None
    meshes = []
    for obj in bpy.context.scene.objects:
        if obj.type == 'ARMATURE':
            armature = obj
        elif obj.type == 'MESH':
            meshes.append(obj)
            
    if armature is None:
        sys.exit(f"No armature found after importing {character_fbx} — is this a rigged FBX?")

    # --- BẮT ĐẦU FIX: BỘ LỌC ĐỔI TÊN XƯƠNG (RETARGETING) ---
    # Tạo bản đồ ánh xạ từ tên gốc (ví dụ: RightFoot) sang tên thực tế trên armature (mixamorig:RightFoot)
    target_bone_map = {b.name.split(':')[-1]: b.name for b in armature.data.bones}

    def retarget_action_bones(action):
        fixed_count = 0
        for fcurve in action.fcurves:
            # Tìm các chuỗi kiểu pose.bones["mixamorig5:RightFoot"].location
            match = re.match(r'pose\.bones\["([^"]+)"\]\.(.+)', fcurve.data_path)
            if match:
                anim_bone = match.group(1)
                prop = match.group(2)
                
                # Nếu xương trong action không tồn tại trên skeleton chính
                if anim_bone not in armature.data.bones:
                    base_anim_bone = anim_bone.split(':')[-1] # Lấy "RightFoot"
                    # Kiểm tra xem "RightFoot" có trên skeleton gốc không
                    if base_anim_bone in target_bone_map:
                        correct_bone = target_bone_map[base_anim_bone]
                        # Đổi lại đường dẫn cho khớp 100%
                        fcurve.data_path = f'pose.bones["{correct_bone}"].{prop}'
                        fixed_count += 1
        return fixed_count
    # --- KẾT THÚC FIX ---

    # Extract & rename bundled action in character file (if any)
    if armature.animation_data and armature.animation_data.action:
        base_action = armature.animation_data.action
        base_action.name = character_fbx.stem
        base_action.use_fake_user = True
        retarget_action_bones(base_action)
        print(f"  [Import] Character bundled action renamed to '{base_action.name}'")

    # Import Animation Files (extract action, delete mesh/armature)
    character_resolved = character_fbx.resolve()
    anim_files = [p for p in sorted(anim_folder.glob("*.fbx")) if p.resolve() != character_resolved]
    
    existing_actions = set(bpy.data.actions.keys())
    
    for fbx_path in anim_files:
        before_objs = set(bpy.context.scene.objects)
        bpy.ops.import_scene.fbx(filepath=str(fbx_path))
        imported_objs = [o for o in bpy.context.scene.objects if o not in before_objs]

        new_actions = [a for a in bpy.data.actions if a.name not in existing_actions]
        if not new_actions:
            print(f"  [Import] ! {fbx_path.name}: no new Action found, deleted model.")
            
        for i, action in enumerate(new_actions):
            # Strict naming based on filename (e.g., run.fbx -> "run")
            name = fbx_path.stem if i == 0 else f"{fbx_path.stem}_{i + 1}"
            action.name = name
            action.use_fake_user = True
            
            # SỬA LỖI T-POSE: Fix lỗi sai tên xương mixamorig5 -> mixamorig
            fixed = retarget_action_bones(action)
            
            existing_actions.add(name)
            print(f"  [Import] + {fbx_path.name} -> extracted action '{name}' (Retargeted {fixed} bone tracks)")

        # Delete temp imported models/armatures, keeping only the Fake User actions
        for obj in imported_objs:
            bpy.data.objects.remove(obj, do_unlink=True)

    return armature, meshes


# -------------------------------------------------------------------------
# 2. MESH PIPELINE
# -------------------------------------------------------------------------
def pipeline_mesh(meshes):
    if len(meshes) <= 1:
        return meshes
    bpy.ops.object.select_all(action='DESELECT')
    for m in meshes:
        m.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    bpy.ops.object.join()
    joined = bpy.context.view_layer.objects.active
    print(f"  [Mesh] Joined {len(meshes)} mesh objects into '{joined.name}'")
    return [joined]


# -------------------------------------------------------------------------
# 3. SKELETON PIPELINE
# -------------------------------------------------------------------------
def pipeline_skeleton(armature):
    bpy.ops.object.select_all(action='DESELECT')
    armature.select_set(True)
    bpy.context.view_layer.objects.active = armature
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    print("  [Skeleton] Armature transforms applied.")


# -------------------------------------------------------------------------
# 4. ANIMATION PIPELINE
# -------------------------------------------------------------------------
def pipeline_animation(armature):
    root_bones = [b for b in armature.pose.bones if b.parent is None]
    if not root_bones:
        print("  [Animation] ! No root bone found, skipping root motion strip.")
        return
    root_name = root_bones[0].name
    data_path = f'pose.bones["{root_name}"].location'
    stripped = 0
    for action in bpy.data.actions:
        for fcurve in [fc for fc in action.fcurves if fc.data_path == data_path]:
            action.fcurves.remove(fcurve)
            stripped += 1
    print(f"  [Animation] Stripped {stripped} root motion fcurves from '{root_name}'.")


# -------------------------------------------------------------------------
# 5. MATERIAL PIPELINE
# -------------------------------------------------------------------------
def pipeline_material():
    # Deduplicate redundant materials (e.g. Mat and Mat.001) generated by FBX importer
    mats = bpy.data.materials
    dedup_count = 0
    for mat in mats:
        if "." in mat.name:
            base_name = mat.name.rsplit(".", 1)[0]
            if base_name in mats:
                mat.user_remap(mats[base_name])
                mats.remove(mat)
                dedup_count += 1
    if dedup_count > 0:
        print(f"  [Material] Deduplicated {dedup_count} redundant materials.")
    else:
        print("  [Material] Material pipeline processed.")


# -------------------------------------------------------------------------
# 6. TEXTURE PIPELINE
# -------------------------------------------------------------------------
def pipeline_texture(max_size):
    if max_size <= 0:
        print("  [Texture] Max size <= 0, skipping resize.")
        return
    count = 0
    for img in bpy.data.images:
        w, h = img.size[0], img.size[1]
        if w <= max_size and h <= max_size:
            continue
        scale = max_size / max(w, h)
        new_w, new_h = max(1, int(w * scale)), max(1, int(h * scale))
        img.scale(new_w, new_h)
        count += 1
    print(f"  [Texture] Resized {count} texture(s) to max {max_size}px.")


# -------------------------------------------------------------------------
# 7. OPTIMIZATION PIPELINE
# -------------------------------------------------------------------------
def pipeline_optimization(meshes, ratio):
    if ratio < 1.0:
        for mesh_obj in meshes:
            bpy.context.view_layer.objects.active = mesh_obj
            before = len(mesh_obj.data.polygons)
            mod = mesh_obj.modifiers.new(name="AutoDecimate", type='DECIMATE')
            mod.ratio = ratio
            mod.decimate_type = 'COLLAPSE'
            bpy.ops.object.modifier_move_to_index(modifier=mod.name, index=0)
            bpy.ops.object.modifier_apply(modifier=mod.name)
            after = len(mesh_obj.data.polygons)
            print(f"  [Optimization] Decimated {mesh_obj.name}: {before} -> {after} faces.")
            
    bpy.ops.outliner.orphans_purge(do_local_ids=True, do_linked_ids=True, do_recursive=True)
    print("  [Optimization] Orphan data purged.")


# -------------------------------------------------------------------------
# 8. EXPORT GLB
# -------------------------------------------------------------------------
def pipeline_export(output_glb):
    output_glb.parent.mkdir(parents=True, exist_ok=True)
    base_kwargs = dict(
        filepath=str(output_glb),
        export_format='GLB',
        export_animations=True,
        export_draco_mesh_compression_enable=False,
    )
    try:
        bpy.ops.export_scene.gltf(**base_kwargs, export_animation_mode='ACTIONS')
    except TypeError:
        bpy.ops.export_scene.gltf(**base_kwargs, export_nla_strips=False)
    
    size_mb = output_glb.stat().st_size / (1024 * 1024)
    print(f"  [Export] Saved to {output_glb} ({size_mb:.1f} MB)")
    print("  [Export] Embedded Actions:", ", ".join(sorted(a.name for a in bpy.data.actions)))


# -------------------------------------------------------------------------
# MAIN EXECUTION
# -------------------------------------------------------------------------
def main():
    character_fbx, anim_folder, output_glb, decimate_ratio, max_texture_size = parse_args()
    
    print("\n--- STARTING ASSET PIPELINE ---")
    
    print("\n[1/8] IMPORTING...")
    armature, meshes = pipeline_import(character_fbx, anim_folder)

    print("\n[2/8] MESH PIPELINE...")
    meshes = pipeline_mesh(meshes)

    print("\n[3/8] SKELETON PIPELINE...")
    pipeline_skeleton(armature)

    print("\n[4/8] ANIMATION PIPELINE...")
    pipeline_animation(armature)
    
    print("\n[5/8] MATERIAL PIPELINE...")
    pipeline_material()

    print("\n[6/8] TEXTURE PIPELINE...")
    pipeline_texture(max_texture_size)

    print("\n[7/8] OPTIMIZATION PIPELINE...")
    pipeline_optimization(meshes, decimate_ratio)

    print("\n[8/8] EXPORT GLB...")
    pipeline_export(output_glb)
    
    print("\n--- PIPELINE COMPLETE ---")

if __name__ == "__main__":
    main()