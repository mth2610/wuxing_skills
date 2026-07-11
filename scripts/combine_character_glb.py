#!/usr/bin/env python3
"""
Combine a Mixamo character (downloaded "With Skin") + a folder of separately
downloaded Mixamo animation FBX files (downloaded "Without Skin") into one
game-ready .glb — automates the manual Blender steps described earlier in
this project's chat history (import character, import each animation FBX,
rename+fake-user the Action it creates, delete the temp armature, Decimate
the mesh, export glTF with Draco compression) for
character/character_model.h's CharacterModel_Load.

Must run INSIDE Blender's bundled Python (imports `bpy`), headless:

  blender --background --python scripts/combine_character_glb.py -- \\
      path/to/character_with_skin.fbx path/to/animations_folder \\
      assets/characters/player.glb 0.25

Args after the literal `--` (Blender strips everything before it):
  argv[0]  character FBX, "With Skin" (required)
  argv[1]  folder of animation FBX files, "Without Skin" (required) — every
           *.fbx directly inside it is imported, one Action per file
  argv[2]  output .glb path (default: assets/characters/player.glb)
  argv[3]  optional Decimate ratio 0.0-1.0 (default: 0.25 = keep 25% of
           faces). Pass 1.0 to skip decimation entirely.
  argv[4]  optional max texture size in pixels, e.g. 1024 (default: 1024 —
           Mixamo characters often ship 2048/4096px Diffuse/Normal/Specular/
           Glossiness maps, which is usually THE dominant contributor to
           file size, far more than mesh data). Pass 0 to skip resizing.

IMPORTANT — name your animation FBX files BEFORE running this script so the
resulting Action names match what character/character_model.c looks for
(case-insensitive substring match): name them things like "idle.fbx",
"walking.fbx", "punching.fbx", "kick.fbx", "palm.fbx" — the Action ends up
named after the file (minus ".fbx"). Rename freely and re-run any time; this
script is idempotent (output file is overwritten).

What it does:
  1. Wipes the default empty-file contents (no leftover default cube).
  2. Imports the character FBX (mesh + armature + skin).
  3. For each *.fbx in the animation folder: imports it (temp armature +
     mesh), renames the new Action(s) it creates after the filename, marks
     them Fake User (so they survive after the temp objects are deleted),
     then deletes the temp import.
  4. Adds a Decimate modifier (Collapse, given ratio) to every mesh on the
     main character and applies it — this is usually the single biggest
     lever on file size for AI-generated/photogrammetry meshes, which are
     often far denser than a game character needs (aim for roughly
     5,000-20,000 triangles for a stylized/mobile character).
  5. Purges orphan data left over from the temporary animation imports.
  6. Exports the character + every named Action as one glTF binary (.glb),
     with Draco mesh compression enabled.

Blender's glTF exporter has renamed some animation-related options across
versions — this script tries the current (4.x) name first and falls back to
an older one; if export still fails with a TypeError about an unexpected
keyword, do File > Export > glTF 2.0 once manually with your desired
settings (Draco on, Animation mode "Actions"), click Export, then check
Blender's Info log (Window > Toggle System Console on Windows, or the Info
editor) — it prints the exact bpy.ops.export_scene.gltf(...) call it just
ran; copy those exact kwargs into export_glb() below.
"""

import sys
from pathlib import Path

try:
    import bpy
except ImportError:
    sys.exit(
        "This script must be run inside Blender's Python "
        "(blender --background --python scripts/combine_character_glb.py -- ...). "
        "See the docstring at the top of this file for usage."
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


def wipe_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def import_character(character_fbx):
    bpy.ops.import_scene.fbx(filepath=str(character_fbx))
    armature = None
    meshes = []
    for obj in bpy.context.scene.objects:
        if obj.type == 'ARMATURE':
            armature = obj
        elif obj.type == 'MESH':
            meshes.append(obj)
    if armature is None:
        sys.exit(f"No armature found after importing {character_fbx} — is this really a rigged/skinned FBX?")
    return armature, meshes


def apply_armature_transform(armature):
    """Bake the armature object's rotation/scale (typically +90 X and 0.01
    scale left over from FBX's Z-up/cm-scale conventions) into the bone rest
    pose, leaving the armature node at identity transform. Mixamo characters
    almost always need this: leaving a non-identity transform on the
    armature's own node is a well-known raylib glTF-skinning pitfall — the
    joint hierarchy renders fine in Blender (which composes the extra node
    transform correctly) but explodes into wildly displaced vertices in
    raylib, whose glTF skinning path does not account for a transform on the
    skeleton root above the joints. NOTE: transform_apply does NOT touch
    Action fcurves — pose-bone location keyframes keep their old (pre-apply,
    cm-scale) values, which is one reason strip_root_motion() below must
    delete root location fcurves outright instead of trying to rescale."""
    bpy.ops.object.select_all(action='DESELECT')
    armature.select_set(True)
    bpy.context.view_layer.objects.active = armature
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)


def join_meshes(meshes):
    """Merge every mesh object (Mixamo characters typically ship body/shirt/
    shorts/hair/eyelashes/shoes as separate objects, all skinned to the same
    armature) into one. Each stays a separate skin in the exported glTF
    otherwise, which is another known raylib glTF-skinning pitfall: multiple
    skins referencing the same joint set can get their joint indices
    remapped incorrectly on load, producing the same exploded-vertex
    symptom as an un-applied armature transform. Returns the single
    resulting mesh object (or the input unchanged if there was only one)."""
    if len(meshes) <= 1:
        return meshes
    bpy.ops.object.select_all(action='DESELECT')
    for m in meshes:
        m.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    bpy.ops.object.join()
    joined = bpy.context.view_layer.objects.active
    print(f"  Joined {len(meshes)} mesh objects into '{joined.name}'")
    return [joined]


def strip_root_motion(armature):
    """Delete the root bone's Location fcurves from every imported Action,
    so the root (Mixamo: Hips) stays at its REST position — origin at the
    feet, feet on y=0. Mixamo animations bake forward/vertical translation
    onto the hip bone; left in, that translation stacks on top of the game
    code's own position updates (character rockets out of view when moving).
    Locking to the first-frame value was tried instead and is WRONG: each
    clip's first frame carries a different constant offset (still in the
    pre-transform-apply cm scale, since transform_apply doesn't rescale
    action fcurves), so the model stood displaced from its logical position
    (shadow/mana bar one place, body another; feet sunk below the floor;
    the running clip offset so far only the head showed). Deleting the
    fcurves makes every clip share the rest-pose origin. Hip bob is lost —
    acceptable for basic combat clips."""
    root_bones = [b for b in armature.pose.bones if b.parent is None]
    if not root_bones:
        print("  ! No root bone found, skipping root motion strip")
        return
    root_name = root_bones[0].name
    data_path = f'pose.bones["{root_name}"].location'
    stripped = 0
    for action in bpy.data.actions:
        for fcurve in [fc for fc in action.fcurves if fc.data_path == data_path]:
            action.fcurves.remove(fcurve)
            stripped += 1
    print(f"  Removed root-location fcurves for bone '{root_name}' ({stripped} fcurve(s) across all actions)")


def import_animation(fbx_path):
    """Import one animation FBX, rename the Action(s) it creates after the
    file, mark them Fake User, then delete the temporary imported objects —
    the Action data survives on its own thanks to Fake User."""
    existing_actions = set(bpy.data.actions.keys())

    before_objs = set(bpy.context.scene.objects)
    bpy.ops.import_scene.fbx(filepath=str(fbx_path))
    imported_objs = [o for o in bpy.context.scene.objects if o not in before_objs]

    new_actions = [a for a in bpy.data.actions if a.name not in existing_actions]
    if not new_actions:
        print(f"  ! {fbx_path.name}: no new Action found, skipping")
    for i, action in enumerate(new_actions):
        name = fbx_path.stem if i == 0 else f"{fbx_path.stem}_{i + 1}"
        action.name = name
        action.use_fake_user = True
        print(f"  + {fbx_path.name} -> action '{name}'")

    for obj in imported_objs:
        bpy.data.objects.remove(obj, do_unlink=True)


def resize_textures(max_size):
    if max_size <= 0:
        print("max_texture_size <= 0 - skipping texture resize.")
        return
    for img in bpy.data.images:
        w, h = img.size[0], img.size[1]
        if w <= max_size and h <= max_size:
            continue
        scale = max_size / max(w, h)
        new_w = max(1, int(w * scale))
        new_h = max(1, int(h * scale))
        print(f"  {img.name}: {w}x{h} -> {new_w}x{new_h}")
        img.scale(new_w, new_h)


def decimate_meshes(meshes, ratio):
    if ratio >= 1.0:
        print("Decimate ratio is 1.0 - skipping (no reduction requested).")
        return
    for mesh_obj in meshes:
        bpy.context.view_layer.objects.active = mesh_obj
        before = len(mesh_obj.data.polygons)
        mod = mesh_obj.modifiers.new(name="AutoDecimate", type='DECIMATE')
        mod.ratio = ratio
        mod.decimate_type = 'COLLAPSE'
        # Move to the top of the stack before applying — avoids Blender's
        # "Applied modifier was not first" warning when an Armature modifier
        # (common on rigged imports) is already present.
        bpy.ops.object.modifier_move_to_index(modifier=mod.name, index=0)
        bpy.ops.object.modifier_apply(modifier=mod.name)
        after = len(mesh_obj.data.polygons)
        print(f"  {mesh_obj.name}: {before} -> {after} faces (ratio {ratio})")


def export_glb(output_glb):
    output_glb.parent.mkdir(parents=True, exist_ok=True)
    base_kwargs = dict(
        filepath=str(output_glb),
        export_format='GLB',
        export_animations=True,
        # Draco OFF — this project's raylib build has no Draco decoder
        # compiled in (confirmed: "Failed to load mesh data, Draco
        # compression not supported" at runtime). Meshes are already small
        # post-Decimate, so the size cost of skipping Draco is minor —
        # textures are the dominant factor, handled by resize_textures().
        export_draco_mesh_compression_enable=False,
    )
    # export_animation_mode='ACTIONS' is the Blender 4.x name; older versions
    # use export_nla_strips=False for the equivalent "export raw Actions, not
    # NLA-baked" behavior — try new, then fall back to old.
    try:
        bpy.ops.export_scene.gltf(**base_kwargs, export_animation_mode='ACTIONS')
    except TypeError:
        bpy.ops.export_scene.gltf(**base_kwargs, export_nla_strips=False)


def main():
    character_fbx, anim_folder, output_glb, decimate_ratio, max_texture_size = parse_args()

    if not character_fbx.exists():
        sys.exit(f"Character FBX not found: {character_fbx}")
    if not anim_folder.is_dir():
        sys.exit(f"Animation folder not found: {anim_folder}")

    print(f"Character: {character_fbx}")
    print(f"Animations folder: {anim_folder}")
    print(f"Output: {output_glb}")
    print(f"Decimate ratio: {decimate_ratio}")
    print(f"Max texture size: {max_texture_size}")

    wipe_scene()
    armature, meshes = import_character(character_fbx)
    print(f"Imported character: armature '{armature.name}', {len(meshes)} mesh object(s)")

    # The character FBX itself may carry a bundled action (e.g. the user
    # downloaded an "idle" pose/animation as their base "With Skin" file) —
    # rename it after the character file's own name so it's not lost/
    # unmatched under some generic Mixamo internal name.
    if armature.animation_data and armature.animation_data.action:
        base_action = armature.animation_data.action
        base_action.name = character_fbx.stem
        base_action.use_fake_user = True
        print(f"  Character's own bundled action renamed to '{base_action.name}'")

    print("Applying armature transform (fixes raylib skin-explode bug)...")
    apply_armature_transform(armature)

    character_resolved = character_fbx.resolve()
    anim_files = [p for p in sorted(anim_folder.glob("*.fbx")) if p.resolve() != character_resolved]
    if not anim_files:
        print(f"! No .fbx files found in {anim_folder} (besides the character file)")
    for fbx_path in anim_files:
        import_animation(fbx_path)

    print("Stripping root motion...")
    strip_root_motion(armature)

    print("Decimating meshes...")
    # Decimate each original mesh object separately, BEFORE joining — each
    # part (body/shirt/hair/...) still has its own isolated UV island at
    # this point. Collapsing a single already-joined mesh instead would let
    # Decimate merge triangles across different parts' UV islands, sampling
    # unrelated regions of the texture atlas (this produced a blown-out
    # white-looking character in-game the first time this was tried).
    decimate_meshes(meshes, decimate_ratio)

    print("Joining meshes into one (fixes raylib multi-skin bug)...")
    meshes = join_meshes(meshes)

    print("Resizing textures...")
    resize_textures(max_texture_size)

    print("Purging orphan data...")
    bpy.ops.outliner.orphans_purge(do_local_ids=True, do_linked_ids=True, do_recursive=True)

    print(f"Exporting {output_glb} ...")
    export_glb(output_glb)

    size_mb = output_glb.stat().st_size / (1024 * 1024)
    print(f"Done. {output_glb} = {size_mb:.1f} MB")
    print("Actions embedded:", ", ".join(sorted(a.name for a in bpy.data.actions)))


if __name__ == "__main__":
    main()
