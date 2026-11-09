#!/usr/bin/env python3
"""
scripts/pipeline_humanoid_assets.py

Automated Humanoid Asset & Animation Pipeline for Wuxing Skills:
1. Accepts two input folders:
   - models_dir: Raw 3D character/boss/minion models (.fbx, .glb, .obj)
   - anims_dir:  Raw animation clips (.fbx, .bvh, .glb)
2. Automatically detects Humanoid vs Non-Humanoid armatures:
   - Humanoids: Normalizes bone naming, retargets limb channels, strips limb translations,
     and registers them into the shared humanoid asset ecosystem.
   - Non-Humanoids: Exports as bundled all-in-one GLBs with their own animations intact.
3. Compiles the master shared animation GLB (assets/characters/player.glb).
4. Generates/updates the semantic action slot manifest (assets/characters/player_anims.json).

Usage:
  python3 scripts/pipeline_humanoid_assets.py --models <models_dir> --anims <anims_dir>
  python3 scripts/pipeline_humanoid_assets.py --demo-setup
"""

import os
import sys
import json
import shutil
import argparse
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

BLENDER_CANDIDATES = [
    "/Applications/Blender.app/Contents/MacOS/Blender",
    "blender",
]

DEFAULT_MODELS_DIR = REPO_ROOT / "raw_assets" / "models"
DEFAULT_ANIMS_DIR = REPO_ROOT / "raw_assets" / "animations"
FALLBACK_ANIMS_DIR = REPO_ROOT / "character" / "raw_model" / "animations"
FALLBACK_MODEL_FBX = REPO_ROOT / "character" / "raw_model" / "character_with_skin.fbx"

DEFAULT_HUMANOID_OUT = REPO_ROOT / "assets" / "characters" / "models"
DEFAULT_NONHUMANOID_OUT = REPO_ROOT / "assets" / "models"
DEFAULT_SHARED_GLB = REPO_ROOT / "assets" / "characters" / "player.glb"
DEFAULT_MANIFEST = REPO_ROOT / "assets" / "characters" / "player_anims.json"

SLOT_KEYWORDS = {
    "idle": ["idle", "stand"],
    "walk": ["walk", "jog"],
    "run": ["run", "sprint"],
    "jump": ["jump", "leap"],
    "jump_down": ["jumping_down", "jump_down", "land"],
    "dodge": ["dodge", "evade", "roll", "dash"],
    "punch": ["punch", "jab", "hook", "cross"],
    "kick": ["kick"],
    "palm": ["palm", "push", "slap"],
    "sword_idle": ["loco_idle_sword", "sword_idle"],
    "sword_slash": ["sword_slash", "slash", "cleave", "whirlwind"],
    "sword_parry": ["sword_parry", "parry", "deflect"],
    "sword_block": ["sword_block", "block", "guard"],
    "cast": ["casting", "cast", "spell", "flourish"],
    "hit": ["hit", "stagger", "react"],
    "knockdown": ["knockdown", "fall"]
}

def find_blender(custom_path=None):
    if custom_path and (os.path.isfile(custom_path) or shutil.which(custom_path)):
        return custom_path
    for c in BLENDER_CANDIDATES:
        if os.path.isfile(c) or shutil.which(c):
            return c
    return None

def generate_manifest(animation_names, manifest_path: Path):
    slots = {k: [] for k in SLOT_KEYWORDS}

    for anim in sorted(animation_names):
        lower = anim.lower()
        matched = False
        for slot, keywords in SLOT_KEYWORDS.items():
            if any(k in lower for k in keywords):
                if anim not in slots[slot]:
                    slots[slot].append(anim)
                matched = True
        # If unmatched action, assign as fallback variant if appropriate
        if not matched and "attack" in lower:
            slots["punch"].append(anim)

    manifest_data = {
        "description": "Wuxing Skills - Automated Humanoid Animation Manifest",
        "slots": slots
    }

    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest_data, f, indent=2)

    return slots

def main():
    parser = argparse.ArgumentParser(description="Wuxing Skills - Automated Humanoid Asset & Animation Pipeline")
    parser.add_argument("--models", type=Path, default=DEFAULT_MODELS_DIR, help="Folder containing raw 3D models")
    parser.add_argument("--anims", type=Path, default=DEFAULT_ANIMS_DIR, help="Folder containing raw animation clips")
    parser.add_argument("--out-humanoid", type=Path, default=DEFAULT_HUMANOID_OUT, help="Output folder for clean humanoid models")
    parser.add_argument("--out-nonhumanoid", type=Path, default=DEFAULT_NONHUMANOID_OUT, help="Output folder for bundled non-humanoids")
    parser.add_argument("--out-shared", type=Path, default=DEFAULT_SHARED_GLB, help="Master humanoid shared animation GLB")
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST, help="Output path for animation manifest JSON")
    parser.add_argument("--decimate", type=float, default=1.0, help="Optional polygon decimation ratio (0.1 to 1.0)")
    parser.add_argument("--blender", type=str, default=None, help="Custom path to Blender executable")
    parser.add_argument("--demo-setup", action="store_true", help="Set up sample directory structure with existing raw assets")

    args = parser.parse_args()

    blender_bin = find_blender(args.blender)
    if not blender_bin:
        sys.exit("[Error] Blender 3.6+ was not found. Please install Blender or pass --blender <path>.")

    if args.demo_setup:
        print("[Setup] Initializing sample raw asset directories...")
        args.models.mkdir(parents=True, exist_ok=True)
        args.anims.mkdir(parents=True, exist_ok=True)
        if FALLBACK_MODEL_FBX.exists() and not (args.models / FALLBACK_MODEL_FBX.name).exists():
            shutil.copy(FALLBACK_MODEL_FBX, args.models / FALLBACK_MODEL_FBX.name)
            print(f"  + Copied sample character model to {args.models}")
        if FALLBACK_ANIMS_DIR.exists():
            for f in FALLBACK_ANIMS_DIR.glob("*.*"):
                dst = args.anims / f.name
                if not dst.exists():
                    shutil.copy(f, dst)
            print(f"  + Populated sample animations into {args.anims}")

    # Fallback if directories are empty but repo raw assets exist
    effective_anims = args.anims if args.anims.exists() and any(args.anims.iterdir()) else FALLBACK_ANIMS_DIR
    effective_models = args.models if args.models.exists() and any(args.models.iterdir()) else FALLBACK_MODEL_FBX.parent

    print("====================================================================")
    print("      WUXING SKILLS — HUMANOID ASSET & ANIMATION PIPELINE           ")
    print("====================================================================")
    print(f"Blender executable : {blender_bin}")
    print(f"Raw Models input   : {effective_models}")
    print(f"Raw Anims input    : {effective_anims}")
    print(f"Humanoid models out: {args.out_humanoid}")
    print(f"Non-Humanoids out  : {args.out_nonhumanoid}")
    print(f"Shared GLB output  : {args.out_shared}")
    print(f"Manifest output    : {args.manifest}")
    print("--------------------------------------------------------------------")

    temp_cfg = REPO_ROOT / ".pipeline_cfg.json"
    temp_report = REPO_ROOT / ".pipeline_report.json"

    cfg_payload = {
        "models_dir": str(effective_models),
        "anims_dir": str(effective_anims),
        "output_humanoid_dir": str(args.out_humanoid),
        "output_nonhumanoid_dir": str(args.out_nonhumanoid),
        "shared_glb_output": str(args.out_shared),
        "report_output": str(temp_report),
        "decimate_ratio": args.decimate
    }

    with open(temp_cfg, "w", encoding="utf-8") as f:
        json.dump(cfg_payload, f, indent=2)

    worker_script = REPO_ROOT / "scripts" / "pipeline_worker_blender.py"

    cmd = [
        blender_bin,
        "--background",
        "--python",
        str(worker_script),
        "--",
        str(temp_cfg)
    ]

    try:
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=True)
        # Filter and print relevant logs
        for line in res.stdout.splitlines():
            if any(tag in line for tag in ["[Model", "[Animation", "[Master Humanoid", "-> Extracted", "Error"]):
                print("  " + line)
    except subprocess.CalledProcessError as e:
        print("[Error] Blender worker execution failed:")
        print(e.stdout)
        sys.exit(1)
    finally:
        if temp_cfg.exists():
            temp_cfg.unlink()

    # Read worker report
    report = {}
    if temp_report.exists():
        with open(temp_report, "r", encoding="utf-8") as f:
            report = json.load(f)
        temp_report.unlink()

    extracted = report.get("extracted_from_models", [])
    if extracted:
        print(f"\n[Extraction] Extracted & validated {len(extracted)} bundled animation(s) from raw models:")
        for e in extracted:
            print(f"  + Model '{e['model']}' -> Action '{e['action']}'")

    imported_anims = [a["action"] for a in report.get("imported_animations", [])]
    if imported_anims:
        slots = generate_manifest(imported_anims, args.manifest)
        print(f"\n[Manifest] Generated action slot bindings at {args.manifest}:")
        for slot, actions in slots.items():
            if actions:
                print(f"  - {slot:<14}: {', '.join(actions)}")

    print("\n--------------------------------------------------------------------")
    print(f"[Done] Pipeline processed {len(report.get('humanoid_models', []))} Humanoids, "
          f"{len(report.get('nonhumanoid_models', []))} Non-Humanoids, "
          f"{len(report.get('imported_animations', []))} animation clips.")
    print("====================================================================")

if __name__ == "__main__":
    main()
