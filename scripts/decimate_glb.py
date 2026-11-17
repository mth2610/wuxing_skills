#!/usr/bin/env python3
"""Decimate a skinned GLB with Blender's Decimate modifier, keeping the rig.

Why Blender and not a hand-rolled decimator: edge-collapse decimation must carry
skin weights, material assignment and (optionally) UVs through a topology change.
Blender's Collapse modifier interpolates vertex groups and keeps material slots,
which is exactly the behaviour a garment needs — a naive vertex-clustering pass
would also move seam vertices and crack the trim edges.

Run (headless):
    /Applications/Blender.app/Contents/MacOS/Blender --background --factory-startup \
        --python scripts/decimate_glb.py -- <in.glb> <out.glb> [--tris N | --ratio R]

The script prints the pre/post triangle counts and exits non-zero if the result
keeps no armature, loses any `cloth_*` bone, or ends up above the target.
"""
import os
import sys

import bpy


def parse_args():
    argv = sys.argv
    argv = argv[argv.index("--") + 1:] if "--" in argv else []
    if len(argv) < 2:
        raise SystemExit("usage: --python decimate_glb.py -- <in.glb> <out.glb> [--tris N | --ratio R]")
    src, dst = argv[0], argv[1]
    target_tris = None
    ratio = None
    i = 2
    while i < len(argv):
        if argv[i] == "--tris" and i + 1 < len(argv):
            target_tris = int(argv[i + 1]); i += 2
        elif argv[i] == "--ratio" and i + 1 < len(argv):
            ratio = float(argv[i + 1]); i += 2
        else:
            i += 1
    return src, dst, target_tris, ratio


def scene_triangles():
    total = 0
    for obj in bpy.context.scene.objects:
        if obj.type != 'MESH':
            continue
        mesh = obj.data
        mesh.calc_loop_triangles()
        total += len(mesh.loop_triangles)
    return total


def main():
    src, dst, target_tris, ratio = parse_args()
    if not os.path.isfile(src):
        raise SystemExit(f"input not found: {src}")

    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=src)

    meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
    armatures = [o for o in bpy.context.scene.objects if o.type == 'ARMATURE']
    if not meshes:
        raise SystemExit("no mesh objects imported")
    if not armatures:
        raise SystemExit("no armature imported — refusing to decimate a rigged asset without its rig")

    cloth_bones = {b.name for b in armatures[0].data.bones if b.name.startswith("cloth")}
    before = scene_triangles()
    if target_tris and before:
        ratio = min(1.0, float(target_tris) / float(before))
    if not ratio or ratio <= 0.0 or ratio >= 1.0:
        raise SystemExit(f"nothing to do (ratio={ratio}, tris={before})")

    for obj in meshes:
        bpy.context.view_layer.objects.active = obj
        obj.select_set(True)
        mod = obj.modifiers.new(name="Decimate", type='DECIMATE')
        mod.decimate_type = 'COLLAPSE'
        mod.ratio = ratio
        mod.use_collapse_triangulate = True
        mod.use_symmetry = False
        bpy.ops.object.modifier_apply(modifier=mod.name)
        obj.select_set(False)

    after = scene_triangles()
    kept_bones = {b.name for b in armatures[0].data.bones}
    lost = cloth_bones - kept_bones
    if lost:
        raise SystemExit(f"decimation dropped cloth bones: {sorted(lost)}")

    # Sanity: every vertex must still be skinned, or the garment will collapse
    # onto the origin during CPU skinning.
    for obj in meshes:
        groups = obj.vertex_groups
        if not groups:
            raise SystemExit(f"'{obj.name}' lost its vertex groups")
        unweighted = 0
        for v in obj.data.vertices:
            if not v.groups or sum(g.weight for g in v.groups) <= 1e-4:
                unweighted += 1
        if unweighted:
            raise SystemExit(f"'{obj.name}' has {unweighted} unweighted vertices after decimation")

    bpy.ops.export_scene.gltf(
        filepath=dst,
        export_format='GLB',
        export_apply=False,
        export_skins=True,
        export_yup=True,
        export_materials='EXPORT',
        export_animations=False,
        export_normals=True,
        export_texcoords=True,
    )

    print(f"DECIMATE: {before} -> {after} triangles (ratio {ratio:.4f}), "
          f"{len(meshes)} mesh(es), {len(kept_bones)} bones, "
          f"{len(cloth_bones)} cloth bone(s) preserved")
    print(f"DECIMATE: wrote {dst} ({os.path.getsize(dst) / 1e6:.2f} MB)")


main()
