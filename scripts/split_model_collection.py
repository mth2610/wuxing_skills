#!/usr/bin/env python3
"""
Split a combined glTF/GLB "collection" file (a nature kit, prop pack, etc.
with many objects in one scene) into one .glb per top-level object, matching
this project's convention of one model file per prop (see assets/models/
bamboo.glb and MAP_API.md §11 — LoadModel once per prop type, drawn many
times in a loop).

Must run INSIDE Blender's bundled Python, not a normal python3 interpreter —
it imports `bpy`. Invoke it via Blender's headless CLI:

  blender --background --python scripts/split_model_collection.py -- \\
      assets/raw_models/nature_kit.glb assets/models

Args after the literal `--` are this script's own argv (Blender strips
everything before it):
  argv[0]  input .glb/.gltf collection file (required)
  argv[1]  output directory for split .glb files (default: assets/models)
  argv[2]  optional name prefix for output files, e.g. "bush_" (default: none)

What it does:
  1. Wipes Blender's default empty-file contents (no leftover default cube).
  2. Imports the collection file.
  3. Finds every ROOT object (no parent) that has at least one mesh in its
     own hierarchy — skips stray Cameras/Lights some kits ship with.
  4. For each root, selects it + all descendants and exports just that
     selection as its own <name>.glb into the output dir.
  5. Sanitizes object names into safe snake_case filenames.

Re-run safely any time a new collection file needs splitting; existing
output files with the same name are overwritten.
"""

import re
import sys
from pathlib import Path

try:
    import bpy
except ImportError:
    sys.exit(
        "This script must run inside Blender, not a plain python3 interpreter.\n"
        "Usage: blender --background --python scripts/split_model_collection.py -- "
        "<input.glb> [output_dir] [prefix]"
    )


def sanitize_name(name: str) -> str:
    name = name.strip().lower()
    name = re.sub(r"[^a-z0-9_]+", "_", name)
    name = re.sub(r"_+", "_", name).strip("_")
    return name or "object"


def mesh_count_in_hierarchy(obj) -> int:
    count = 1 if obj.type == "MESH" else 0
    for child in obj.children:
        count += mesh_count_in_hierarchy(child)
    return count


def select_hierarchy(obj):
    obj.select_set(True)
    for child in obj.children:
        select_hierarchy(child)


def main() -> int:
    argv = sys.argv
    if "--" in argv:
        argv = argv[argv.index("--") + 1:]
    else:
        argv = []

    if not argv:
        sys.exit("Missing input file. Usage: ... -- <input.glb> [output_dir] [prefix]")

    input_path = Path(argv[0]).resolve()
    output_dir = Path(argv[1]).resolve() if len(argv) > 1 else Path("assets/models").resolve()
    prefix = argv[2] if len(argv) > 2 else ""

    if not input_path.is_file():
        sys.exit(f"Input file not found: {input_path}")

    output_dir.mkdir(parents=True, exist_ok=True)

    # Start from a clean empty scene so the default cube/camera/light don't
    # get exported as spurious extra files.
    bpy.ops.wm.read_factory_settings(use_empty=True)

    if input_path.suffix.lower() in (".glb", ".gltf"):
        bpy.ops.import_scene.gltf(filepath=str(input_path))
    elif input_path.suffix.lower() == ".fbx":
        bpy.ops.import_scene.fbx(filepath=str(input_path))
    else:
        sys.exit(f"Unsupported input format: {input_path.suffix}")

    roots = [obj for obj in bpy.context.scene.objects if obj.parent is None]

    exported = []
    used_names = set()
    for root in roots:
        if mesh_count_in_hierarchy(root) == 0:
            continue  # skip stray Camera/Light/Empty-only roots

        base_name = sanitize_name(root.name)
        out_name = f"{prefix}{base_name}" if prefix else base_name
        if out_name in used_names:
            out_name = f"{out_name}_{len(used_names)}"
        used_names.add(out_name)

        bpy.ops.object.select_all(action="DESELECT")
        select_hierarchy(root)
        bpy.context.view_layer.objects.active = root

        out_path = output_dir / f"{out_name}.glb"
        bpy.ops.export_scene.gltf(
            filepath=str(out_path),
            use_selection=True,
            export_format="GLB",
            export_apply=True,
        )
        exported.append(out_path)
        print(f"  {root.name:<28} -> {out_path}")

    if not exported:
        print("No mesh-bearing root objects found — nothing exported.")
        return 1

    print(f"\nDone: {len(exported)} model(s) split from {input_path.name} -> {output_dir}/")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
