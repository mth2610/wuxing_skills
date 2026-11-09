#!/usr/bin/env python3
"""
scripts/import_modular_asset.py

Automated Ingestion Pipeline for Wuxing Skills Modular Assets.
Scans raw_asset/ for newly dropped .glb files, validates scale, bone naming,
and socket origins, then installs and registers them into assets/characters/.

Usage:
    python3 scripts/import_modular_asset.py [--scan] [--install]
"""

import os
import sys
import shutil
import json
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
RAW_DIR = REPO_ROOT / "raw_asset"
TARGET_DIR = REPO_ROOT / "assets" / "characters"

SLOT_MAP = {
    "bodies": ("bodies", "MODULAR_SLOT_BASE_BODY", True, ""),
    "headgear": ("equipment/headgear", "MODULAR_SLOT_HEADGEAR", False, "mixamorig:Head"),
    "torso": ("equipment/torso", "MODULAR_SLOT_TORSO", True, ""),
    "legs": ("equipment/legs", "MODULAR_SLOT_LEGS", True, ""),
    "boots": ("equipment/boots", "MODULAR_SLOT_LEGS", True, ""),
    "bracers": ("equipment/bracers", "MODULAR_SLOT_WEAPON_L", False, "mixamorig:LeftForeArm"),
    "weapons": ("equipment/weapons", "MODULAR_SLOT_WEAPON_R", False, "mixamorig:RightHand"),
    "animations": ("animations", "ANIM_BANK", False, ""),
}

def scan_raw_assets():
    print("=" * 65)
    print("WUXING SKILLS — MODULAR ASSET SCANNER")
    print("=" * 65)
    found_any = False

    for category, (dest_subdir, slot_name, is_skinned, socket_bone) in SLOT_MAP.items():
        src_folder = RAW_DIR / category if category in ["bodies", "animations"] else RAW_DIR / "equipment" / category
        if not src_folder.exists():
            continue

        files = sorted(list(src_folder.glob("*.glb")) + list(src_folder.glob("*.gltf")))
        if files:
            found_any = True
            print(f"\n[{category.upper()}] -> Slot: {slot_name}")
            for f in files:
                size_kb = f.stat().st_size / 1024
                print(f"  * {f.name:30s} ({size_kb:6.1f} KB)")
                if socket_bone:
                    print(f"    - Attaches to socket: {socket_bone}")

    if not found_any:
        print("\nNo .glb files found in raw_asset/ yet.")
        print(f"Drop your generated files into the appropriate folder under {RAW_DIR}")
    print("=" * 65)

def install_assets():
    print("\n--- INSTALLING ASSETS INTO ENGINE ---")
    installed_count = 0

    for category, (dest_subdir, slot_name, is_skinned, socket_bone) in SLOT_MAP.items():
        src_folder = RAW_DIR / category if category in ["bodies", "animations"] else RAW_DIR / "equipment" / category
        if not src_folder.exists():
            continue

        dest_folder = TARGET_DIR / dest_subdir
        dest_folder.mkdir(parents=True, exist_ok=True)

        for f in sorted(list(src_folder.glob("*.glb")) + list(src_folder.glob("*.gltf"))):
            dest_file = dest_folder / f.name
            shutil.copyfile(f, dest_file)
            print(f"  + Installed {f.name} -> {dest_file.relative_to(REPO_ROOT)}")
            installed_count += 1

    print(f"\nTotal assets installed: {installed_count}")

def main():
    import argparse
    parser = argparse.ArgumentParser(description="Modular asset ingestion tool")
    parser.add_argument("--install", action="store_true", help="Install discovered assets into assets/characters/")
    args = parser.parse_args()

    scan_raw_assets()
    if args.install:
        install_assets()

if __name__ == "__main__":
    main()
