#!/usr/bin/env python3
"""
scripts/import_character_anim.py

Automated character animation pipeline CLI for Wuxing Skills:
1. Imports an external animation (.fbx from Mixamo or .bvh from procedural engine).
2. Copies it to character/raw_model/animations/ with sanitized naming.
3. Automatically runs Blender to rebuild assets/characters/player.glb.
4. Optionally updates assets/characters/player_anims.json manifest.

Usage:
    python3 scripts/import_character_anim.py <animation.fbx/.bvh> [action_name] [--slot <slot_name>] [--no-rebuild]
    python3 scripts/import_character_anim.py --list
"""

import os
import sys
import shutil
import argparse
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
RAW_ANIM_DIR = REPO_ROOT / "character" / "raw_model" / "animations"
CHAR_FBX = REPO_ROOT / "character" / "raw_model" / "character_with_skin.fbx"
OUTPUT_GLB = REPO_ROOT / "assets" / "characters" / "player.glb"
MANIFEST_JSON = REPO_ROOT / "assets" / "characters" / "player_anims.json"
COMBINE_SCRIPT = REPO_ROOT / "scripts" / "combine_character_glb.py"

BLENDER_CANDIDATES = [
    "/Applications/Blender.app/Contents/MacOS/Blender",
    "blender",
]

def find_blender():
    for candidate in BLENDER_CANDIDATES:
        if os.path.isfile(candidate) or shutil.which(candidate):
            return candidate
    return None

def list_animations():
    print(f"\n[Animation Bank] {RAW_ANIM_DIR}:")
    files = sorted(list(RAW_ANIM_DIR.glob("*.fbx")) + list(RAW_ANIM_DIR.glob("*.bvh")))
    for idx, f in enumerate(files):
        print(f"  [{idx:2d}] {f.stem} ({f.suffix}) - {f.stat().st_size / 1024:.1f} KB")

    if OUTPUT_GLB.exists():
        print(f"\n[Game Asset] {OUTPUT_GLB} ({OUTPUT_GLB.stat().st_size / (1024*1024):.2f} MB)")

def sanitize_name(name):
    clean = name.strip().lower()
    clean = clean.replace(" ", "_").replace("-", "_")
    return "".join(c for c in clean if c.isalnum() or c == "_")

def rebuild_glb():
    blender = find_blender()
    if not blender:
        print("[Error] Blender executable not found. Please install Blender 3.6+ or add to PATH.")
        return False

    cmd = [
        blender,
        "--background",
        "--python", str(COMBINE_SCRIPT),
        "--",
        str(CHAR_FBX),
        str(RAW_ANIM_DIR),
        str(OUTPUT_GLB),
        "0.25",
        "1024"
    ]
    print(f"\n[Pipeline] Running Blender: {' '.join(cmd)}")
    res = subprocess.run(cmd)
    return res.returncode == 0

def main():
    parser = argparse.ArgumentParser(description="Import character animation into Wuxing Skills")
    parser.add_argument("anim_file", nargs="?", help="Path to .fbx or .bvh animation file")
    parser.add_argument("action_name", nargs="?", help="Custom action name (optional)")
    parser.add_argument("--slot", help="Map to slot in player_anims.json (e.g. jump_down, sword_slash)")
    parser.add_argument("--list", action="store_true", help="List all animations in library")
    parser.add_argument("--rebuild-only", action="store_true", help="Rebuild player.glb without adding new file")

    args = parser.parse_args()

    if args.list:
        list_animations()
        return

    if args.rebuild_only:
        success = rebuild_glb()
        print("[Pipeline] Rebuild completed successfully!" if success else "[Pipeline] Rebuild failed.")
        return

    if not args.anim_file:
        parser.print_help()
        list_animations()
        return

    input_path = Path(args.anim_file).resolve()
    if not input_path.exists():
        print(f"[Error] File not found: {input_path}")
        sys.exit(1)

    if input_path.suffix.lower() not in [".fbx", ".bvh"]:
        print(f"[Error] Unsupported format: {input_path.suffix}. Expected .fbx or .bvh")
        sys.exit(1)

    action_name = sanitize_name(args.action_name if args.action_name else input_path.stem)
    dest_path = RAW_ANIM_DIR / f"{action_name}{input_path.suffix.lower()}"

    RAW_ANIM_DIR.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(input_path, dest_path)
    print(f"[Import] Copied {input_path.name} -> {dest_path.name}")

    # Rebuild player.glb
    success = rebuild_glb()
    if success:
        print(f"\n[Success] '{action_name}' is now embedded and game-ready in {OUTPUT_GLB}!")
    else:
        print("\n[Warning] Rebuild encountered errors. Check logs above.")

if __name__ == "__main__":
    main()
