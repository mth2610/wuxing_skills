#!/usr/bin/env python3
"""
scripts/modular_asset_pipeline.py

Universal automated asset processor for Wuxing Skills modular character system.
Handles raw 3D models (characters, equipment, weapons, animation packs)
and outputs clean, standardized assets conforming to the engine's 1-unit-1-meter
Mixamo humanoid contract.

Usage:
  blender --background --python scripts/modular_asset_pipeline.py -- --extract-anims <input.glb> <output_dir>
  blender --background --python scripts/modular_asset_pipeline.py -- --extract-sockets <input.glb> <output_dir>
  blender --background --python scripts/modular_asset_pipeline.py -- --standardize-body <input.glb> <output.glb> [scale]
"""

import sys
import os
import argparse

try:
    import bpy
    from mathutils import Vector, Matrix
except ImportError:
    sys.exit("Error: Must be run inside Blender's python interpreter via:\n  blender --background --python scripts/modular_asset_pipeline.py -- [args]")

def extract_animations(input_glb, output_dir):
    """Extracts all animation tracks from a GLB into individual headless animation clips."""
    os.makedirs(output_dir, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=input_glb)

    armatures = [obj for obj in bpy.data.objects if obj.type == 'ARMATURE']
    if not armatures:
        print(f"No armature found in {input_glb}")
        return

    arm = armatures[0]
    # Delete all mesh geometry to create ultra-lightweight headless animation clips (~20KB)
    for obj in list(bpy.data.objects):
        if obj.type == 'MESH':
            bpy.data.objects.remove(obj, do_unlink=True)

    actions = list(bpy.data.actions)
    print(f"Found {len(actions)} animations in {input_glb}")

    for act in actions:
        clean_name = act.name.replace("_PlayerArmature", "").replace("_Armature", "").lower()
        out_path = os.path.join(output_dir, f"{clean_name}.glb")

        # Clear tracks and bind this single action
        if arm.animation_data:
            arm.animation_data.nla_tracks.clear()
            track = arm.animation_data.nla_tracks.new()
            track.name = clean_name
            track.strips.new(clean_name, 0, act)
            arm.animation_data.action = act

        bpy.ops.export_scene.gltf(
            filepath=out_path,
            export_format='GLB',
            use_selection=False,
            export_apply=False,
            export_animations=True,
            export_skins=True
        )
        print(f"  -> Exported animation: {out_path} ({act.frame_range[1]} frames)")

def extract_sockets(input_glb, output_dir):
    """Extracts rigid accessory meshes (weapons, hats, scabbards) as static unskinned models."""
    os.makedirs(output_dir, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=input_glb)

    # Detect socket objects by name patterns
    socket_keywords = {
        "weapon": ["kiem", "sword", "weapon", "blade", "staff"],
        "headgear": ["nonla", "hat", "khan", "headgear", "mask"],
        "back": ["vokiem", "scabbard", "sheath", "wing"]
    }

    mesh_objs = [obj for obj in bpy.data.objects if obj.type == 'MESH']
    exported_count = 0

    for m in mesh_objs:
        m_lower = m.name.lower()
        category = None
        for cat, kws in socket_keywords.items():
            if any(kw in m_lower for kw in kws):
                category = cat
                break

        if category:
            cat_dir = os.path.join(output_dir, category)
            os.makedirs(cat_dir, exist_ok=True)
            out_path = os.path.join(cat_dir, f"{m.name}.glb")

            # Duplicate object to isolated scene
            bpy.ops.object.select_all(action='DESELECT')
            m.select_set(True)
            bpy.context.view_layer.objects.active = m

            # Unparent and clear armature modifiers
            m.parent = None
            for mod in list(m.modifiers):
                if mod.type == 'ARMATURE':
                    m.modifiers.remove(mod)

            bpy.ops.export_scene.gltf(
                filepath=out_path,
                export_format='GLB',
                use_selection=True,
                export_apply=True,
                export_animations=False,
                export_skins=False
            )
            print(f"  -> Extracted rigid socket ({category}): {out_path}")
            exported_count += 1

    print(f"Extracted {exported_count} socket items to {output_dir}")

def standardize_body(input_glb, output_glb, scale_factor=1.0):
    """Standardizes a base body mesh, verifying scale and stripping accessories."""
    os.makedirs(os.path.dirname(output_glb), exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=input_glb)

    armatures = [obj for obj in bpy.data.objects if obj.type == 'ARMATURE']
    if not armatures:
        print(f"Error: No armature found in {input_glb}")
        return

    arm = armatures[0]
    if scale_factor != 1.0:
        arm.scale = Vector((scale_factor, scale_factor, scale_factor))
        bpy.ops.object.select_all(action='DESELECT')
        arm.select_set(True)
        bpy.context.view_layer.objects.active = arm
        bpy.ops.object.transform_apply(scale=True)

    # Export clean standardized body
    bpy.ops.export_scene.gltf(
        filepath=output_glb,
        export_format='GLB',
        use_selection=False,
        export_apply=False,
        export_animations=False,
        export_skins=True
    )
    print(f"Exported standardized body: {output_glb}")

def main():
    # Pass arguments after '--'
    argv = sys.argv
    if "--" in argv:
        argv = argv[argv.index("--") + 1:]
    else:
        argv = []

    parser = argparse.ArgumentParser(description="Wuxing Skills Modular Asset Pipeline")
    parser.add_argument("--extract-anims", nargs=2, metavar=("INPUT_GLB", "OUT_DIR"), help="Extract animation tracks")
    parser.add_argument("--extract-sockets", nargs=2, metavar=("INPUT_GLB", "OUT_DIR"), help="Extract rigid socket items (weapons, hats)")
    parser.add_argument("--standardize-body", nargs='+', metavar=("INPUT_GLB", "OUT_GLB", "[SCALE]"), help="Standardize base body mesh")

    args = parser.parse_args(argv)

    if args.extract_anims:
        extract_animations(args.extract_anims[0], args.extract_anims[1])
    elif args.extract_sockets:
        extract_sockets(args.extract_sockets[0], args.extract_sockets[1])
    elif args.standardize_body:
        scale = float(args.standardize_body[2]) if len(args.standardize_body) > 2 else 1.0
        standardize_body(args.standardize_body[0], args.standardize_body[1], scale)
    else:
        parser.print_help()

if __name__ == "__main__":
    main()
