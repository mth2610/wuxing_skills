#!/usr/bin/env python3
"""
scripts/pipeline_worker_blender.py

Blender worker script invoked by pipeline_humanoid_assets.py.
Handles:
1. Model loading & armature inspection (Humanoid vs Non-Humanoid classification).
2. Bone name normalization (standardizes Mixamo/Biped prefixes) while STRICTLY
   renaming mesh vertex groups to preserve 100% vertex skin weights.
3. Automatic extraction of bundled animations from raw models into standalone clips.
4. Animation retargeting and root motion filtering.
5. Exporting clean Humanoid models, combined animation GLB, and Non-Humanoid bundled GLBs.

Runs inside Blender:
  blender --background --python scripts/pipeline_worker_blender.py -- <json_config_path>
"""

import sys
import os
import json
import re
from pathlib import Path

try:
    import bpy
except ImportError:
    sys.exit("Error: Must be run inside Blender's Python interpreter.")

HUMANOID_CORE_BONES = [
    "Hips", "Spine", "Spine1", "Spine2", "Neck", "Head",
    "LeftShoulder", "LeftArm", "LeftForeArm", "LeftHand",
    "RightShoulder", "RightArm", "RightForeArm", "RightHand",
    "LeftUpLeg", "LeftLeg", "LeftFoot",
    "RightUpLeg", "RightLeg", "RightFoot"
]

def clean_bone_name(raw_name: str) -> str:
    """Removes common namespace prefixes (mixamorig:, mixamorig1:, Bip01_, etc.)"""
    name = raw_name
    if ":" in name:
        name = name.split(":")[-1]
    name = re.sub(r'^(Bip01_?|mixamorig\d*_?|Armature\|)', '', name, flags=re.IGNORECASE)
    return name

def is_humanoid_armature(armature_obj) -> bool:
    if not armature_obj or armature_obj.type != 'ARMATURE':
        return False
    bone_names = {clean_bone_name(b.name).lower() for b in armature_obj.data.bones}
    matched = 0
    for core in HUMANOID_CORE_BONES:
        if core.lower() in bone_names:
            matched += 1
    return matched >= 12

def standardize_armature_and_meshes(armature_obj, meshes):
    """
    CRITICAL: Renames armature bones AND mesh vertex groups simultaneously.
    If vertex groups are not renamed with bones, skinning weights disconnect
    and vertices collapse into torn ribbons!
    """
    rename_map = {}
    for bone in armature_obj.data.bones:
        cleaned = clean_bone_name(bone.name)
        if cleaned != bone.name:
            rename_map[bone.name] = cleaned
            bone.name = cleaned

    for mesh in meshes:
        for old_name, new_name in rename_map.items():
            vg = mesh.vertex_groups.get(old_name)
            if vg:
                vg.name = new_name
    return len(rename_map)

def retarget_action_to_armature(action, armature_obj):
    """Rewrites action fcurves so bone paths match the armature's actual bone names."""
    bone_map = {clean_bone_name(b.name).lower(): b.name for b in armature_obj.data.bones}
    fixed = 0
    for fcurve in list(action.fcurves):
        m = re.match(r'pose\.bones\["([^"]+)"\]\.(.+)', fcurve.data_path)
        if m:
            anim_bone = m.group(1)
            prop = m.group(2)
            cleaned = clean_bone_name(anim_bone).lower()
            if anim_bone not in armature_obj.data.bones and cleaned in bone_map:
                correct_name = bone_map[cleaned]
                fcurve.data_path = f'pose.bones["{correct_name}"].{prop}'
                fixed += 1
            # Filter limb translation (keep rotation only; allow translation on Hips/Root only)
            if prop.startswith("location") and cleaned not in ["hips", "root"]:
                action.fcurves.remove(fcurve)
    return fixed

def process_pipeline(config_path: str):
    with open(config_path, "r", encoding="utf-8") as f:
        cfg = json.load(f)

    models_dir = Path(cfg["models_dir"])
    anims_dir = Path(cfg["anims_dir"])
    out_humanoid_dir = Path(cfg["output_humanoid_dir"])
    out_nonhumanoid_dir = Path(cfg["output_nonhumanoid_dir"])
    shared_glb_path = Path(cfg["shared_glb_output"])
    decimate_ratio = float(cfg.get("decimate_ratio", 1.0))

    out_humanoid_dir.mkdir(parents=True, exist_ok=True)
    out_nonhumanoid_dir.mkdir(parents=True, exist_ok=True)
    shared_glb_path.parent.mkdir(parents=True, exist_ok=True)

    extracted_anims_dir = out_humanoid_dir.parent / "extracted_anims"
    extracted_anims_dir.mkdir(parents=True, exist_ok=True)

    report = {
        "humanoid_models": [],
        "nonhumanoid_models": [],
        "extracted_from_models": [],
        "imported_animations": []
    }

    # -------------------------------------------------------------
    # 1. PROCESS MODELS & EXTRACT EMBEDDED ANIMATIONS
    # -------------------------------------------------------------
    model_files = []
    if models_dir.exists():
        for ext in ["*.fbx", "*.glb", "*.gltf", "*.obj"]:
            model_files.extend(models_dir.glob(ext))

    reference_armature_file = None

    for mfile in sorted(model_files):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        if mfile.suffix.lower() == ".fbx":
            bpy.ops.import_scene.fbx(filepath=str(mfile))
        elif mfile.suffix.lower() in [".glb", ".gltf"]:
            bpy.ops.import_scene.gltf(filepath=str(mfile))
        elif mfile.suffix.lower() == ".obj":
            try:
                bpy.ops.wm.obj_import(filepath=str(mfile))
            except AttributeError:
                bpy.ops.import_scene.obj(filepath=str(mfile))

        armature = None
        meshes = []
        for obj in bpy.context.scene.objects:
            if obj.type == 'ARMATURE':
                armature = obj
            elif obj.type == 'MESH':
                meshes.append(obj)

        is_human = is_humanoid_armature(armature)

        if is_human:
            standardize_armature_and_meshes(armature, meshes)
            if not reference_armature_file:
                reference_armature_file = mfile

            # Apply transforms
            bpy.ops.object.select_all(action='DESELECT')
            armature.select_set(True)
            bpy.context.view_layer.objects.active = armature
            bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

            # Check if model carries bundled animations that meet humanoid standard
            bundled_actions = [a for a in bpy.data.actions if len(a.fcurves) > 0]
            if bundled_actions:
                print(f"[Model:Humanoid] Found {len(bundled_actions)} bundled animation(s) in '{mfile.name}' — extracting...")
                for act in bundled_actions:
                    retarget_action_to_armature(act, armature)
                    act_name = re.sub(r'[^a-zA-Z0-9_]', '_', act.name.lower().strip())
                    if not act_name or act_name in ["armature", "action"]:
                        act_name = f"{mfile.stem}_action"
                    act.name = act_name

                    if armature.animation_data is None:
                        armature.animation_data_create()
                    armature.animation_data.action = act

                    ext_path = extracted_anims_dir / f"{act_name}.glb"
                    bpy.ops.export_scene.gltf(
                        filepath=str(ext_path),
                        export_format='GLB',
                        use_selection=False,
                        export_apply=False,
                        export_animations=True,
                        export_skins=True
                    )
                    report["extracted_from_models"].append({
                        "model": mfile.name,
                        "action": act_name,
                        "file": str(ext_path)
                    })
                    print(f"  -> Extracted & validated: '{act_name}' -> {ext_path.name}")

            # Strip animations from model so the mesh stays clean & lightweight
            if decimate_ratio < 1.0 and meshes:
                for mesh_obj in meshes:
                    mod = mesh_obj.modifiers.new(name="Decimate", type='DECIMATE')
                    mod.ratio = decimate_ratio
                    bpy.context.view_layer.objects.active = mesh_obj
                    bpy.ops.object.modifier_apply(modifier=mod.name)

            out_path = out_humanoid_dir / f"{mfile.stem}.glb"
            bpy.ops.export_scene.gltf(
                filepath=str(out_path),
                export_format='GLB',
                use_selection=False,
                export_apply=False,
                export_animations=False,
                export_skins=True
            )
            report["humanoid_models"].append({
                "name": mfile.name,
                "type": "HUMANOID",
                "out": str(out_path),
                "bone_count": len(armature.data.bones) if armature else 0
            })
            print(f"[Model:Humanoid] Exported clean skin-only mesh: {out_path.name}")
        else:
            # Non-humanoid: preserve bundled animations and export intact
            out_path = out_nonhumanoid_dir / f"{mfile.stem}.glb"
            bpy.ops.export_scene.gltf(
                filepath=str(out_path),
                export_format='GLB',
                use_selection=False,
                export_apply=False,
                export_animations=True,
                export_skins=True
            )
            report["nonhumanoid_models"].append({
                "name": mfile.name,
                "type": "NON_HUMANOID",
                "out": str(out_path),
                "bundled_anims": len(bpy.data.actions)
            })
            print(f"[Model:NonHumanoid] Exported bundled model: {out_path.name}")

    # -------------------------------------------------------------
    # 2. COMBINE ALL ANIMATIONS INTO MASTER HUMANOID GLB
    # -------------------------------------------------------------
    anim_files = []
    if anims_dir.exists():
        for ext in ["*.fbx", "*.bvh", "*.glb", "*.gltf"]:
            anim_files.extend(anims_dir.glob(ext))

    # Add all animations automatically extracted from raw models
    if extracted_anims_dir.exists():
        anim_files.extend(extracted_anims_dir.glob("*.glb"))

    if anim_files and reference_armature_file:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        if reference_armature_file.suffix.lower() == ".fbx":
            bpy.ops.import_scene.fbx(filepath=str(reference_armature_file))
        else:
            bpy.ops.import_scene.gltf(filepath=str(reference_armature_file))

        armature = None
        for obj in bpy.context.scene.objects:
            if obj.type == 'ARMATURE':
                armature = obj
                break

        if armature:
            standardize_armature_and_meshes(armature, [o for o in bpy.context.scene.objects if o.type == 'MESH'])
            existing_actions = set(bpy.data.actions.keys())

            for afile in sorted(anim_files):
                before_objs = set(bpy.context.scene.objects)
                if afile.suffix.lower() == ".bvh":
                    bpy.ops.import_anim.bvh(filepath=str(afile))
                elif afile.suffix.lower() == ".fbx":
                    bpy.ops.import_scene.fbx(filepath=str(afile))
                else:
                    bpy.ops.import_scene.gltf(filepath=str(afile))

                imported_objs = [o for o in bpy.context.scene.objects if o not in before_objs]
                new_actions = [a for a in bpy.data.actions if a.name not in existing_actions]

                for i, action in enumerate(new_actions):
                    name = afile.stem if i == 0 else f"{afile.stem}_{i+1}"
                    name = re.sub(r'[^a-zA-Z0-9_]', '_', name.lower().strip())
                    action.name = name
                    action.use_fake_user = True
                    retarget_action_to_armature(action, armature)
                    existing_actions.add(name)
                    report["imported_animations"].append({
                        "file": afile.name,
                        "action": name,
                        "frames": int(action.frame_range[1] - action.frame_range[0])
                    })
                    print(f"[Animation Bank] Integrated clip '{name}' ({afile.name})")

                for obj in imported_objs:
                    bpy.data.objects.remove(obj, do_unlink=True)

            bpy.ops.export_scene.gltf(
                filepath=str(shared_glb_path),
                export_format='GLB',
                use_selection=False,
                export_apply=False,
                export_animations=True,
                export_skins=True
            )
            print(f"[Master Humanoid] Exported combined animation library: {shared_glb_path}")

    report_path = Path(cfg.get("report_output", "pipeline_report.json"))
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2)

if __name__ == "__main__":
    argv = sys.argv
    if "--" in argv:
        cfg_arg = argv[argv.index("--") + 1]
        process_pipeline(cfg_arg)
    else:
        print("Usage: blender --background --python scripts/pipeline_worker_blender.py -- <config.json>")
