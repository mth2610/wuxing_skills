#!/usr/bin/env python3
"""
tools/animations/generate_animations.py — Unified Wuxia Weapon Animation Generator CLI

Generates 60 FPS GAME_MIXAMO_20 skeletal animations for Wuxing Skills weapon archetypes
(sword now; extensible to staff/spear and unarmed), audits FK/anatomy/grounding/blade clearance,
exports standalone .glb clips, merges cleanly into assets/characters/animations/humanoid_anims.glb,
and optionally renders 3D skinned contact-sheets with male_body.glb + weapon GLB.

EXAMPLES:
    python3 tools/animations/generate_animations.py --weapon sword --install
    python3 tools/animations/generate_animations.py --weapon sword --clip wuxia_sword_slash_r2l --preview /tmp/preview.png
    python3 tools/animations/generate_animations.py --weapon sword --install --preview /tmp/sword_sheet.png
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
import numpy as np


_ANIM_DIR = Path(__file__).resolve().parent
_REPO_ROOT = _ANIM_DIR.parent.parent
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from tools.animations.rig_core import (
    export_standalone_glb,
    merge_clips_into_bank,
    render_clip_strip,
    verify_clip,
    write_png,
)
from tools.animations.sword_anims import build_all_sword_clips
from tools.animations.unarmed_anims import build_all_unarmed_clips
from tools.animations.taiji_anims import build_all_taiji_clips
from tools.animations.jianshu_32_anims import build_all_jianshu_clips



def main() -> int:
    parser = argparse.ArgumentParser(description="Generate GAME_MIXAMO_20 weapon animations for Wuxing Skills.")
    parser.add_argument("--weapon", choices=["sword", "unarmed", "taiji", "jianshu"], default="sword", help="Weapon archetype to generate")
    parser.add_argument("--clip", default="", help="Optional single clip name filter (or comma-separated list)")
    parser.add_argument("--fps", type=int, default=60, help="Sampling framerate (engine ANIM_FPS = 60)")
    parser.add_argument("--out-dir", type=Path, default=_ANIM_DIR / "out", help="Standalone GLB output dir")
    parser.add_argument("--install", action="store_true", help="Merge clips into assets/characters/animations/humanoid_anims.glb")
    parser.add_argument("--preview", type=Path, default=None, help="Optional PNG path to render 3D skinned contact-sheet")
    args = parser.parse_args()

    weapon_glb = _REPO_ROOT / "assets/characters/equipment/weapons/kiem_thuan_thien.glb"
    if args.weapon == "sword":
        clips = build_all_sword_clips(fps=args.fps)
    elif args.weapon == "unarmed":
        clips = build_all_unarmed_clips(fps=args.fps)
    elif args.weapon == "taiji":
        clips = build_all_taiji_clips(fps=args.fps)
    elif args.weapon == "jianshu":
        clips = build_all_jianshu_clips(fps=args.fps)
    else:
        raise ValueError(f"Unsupported weapon: {args.weapon}")

    if args.clip:
        wanted = {s.strip() for s in args.clip.split(",") if s.strip()}
        clips = [c for c in clips if c.name in wanted]
        if not clips:
            print(f"ERROR: clip(s) '{args.clip}' not found for weapon '{args.weapon}'", file=sys.stderr)
            return 1

    all_ok = True
    out_weapon_dir = args.out_dir / args.weapon
    print(f"=== Generating {len(clips)} '{args.weapon}' clips @ {args.fps} FPS (GAME_MIXAMO_20) ===")
    for clip in clips:
        audit = verify_clip(clip)
        status = "PASS" if audit["ok"] else "FAIL"
        print(
            f"  [{status}] {clip.name:26s} slot={clip.slot:12s} dur={clip.duration:.2f}s "
            f"f={audit['frames']:3d} footY={audit['stance_y']} tipMinY={audit['blade_tip_min_y']:.2f}m "
            f"tipMaxZ={audit['blade_tip_max_z']:+.2f}m maxStep={audit['max_step_deg']:4.1f}deg"
        )
        if not audit["ok"]:
            all_ok = False
            for err in audit["errors"][:5]:
                print(f"         ERR: {err}")

        export_standalone_glb([clip], out_weapon_dir / f"{clip.name}.glb")

    if not args.clip:
        pack_path = out_weapon_dir / f"wuxia_{args.weapon}_pack.glb"
        export_standalone_glb(clips, pack_path)
        print(f"Exported {len(clips)} standalone GLBs + combined pack -> {pack_path}")
    else:
        print(f"Exported {len(clips)} standalone GLB(s) -> {out_weapon_dir}")

    if not all_ok:
        print("ABORTING install due to FK/anatomy verification errors.", file=sys.stderr)
        return 2

    if args.install:
        bank_path = _REPO_ROOT / "assets/characters/animations/humanoid_anims.glb"
        tool_path = _REPO_ROOT / "tools/animation_manager/humanoid_anims.glb"
        rep, add, total = merge_clips_into_bank(bank_path, clips)
        merge_clips_into_bank(tool_path, clips)
        print(f"Installed into {bank_path}: replaced={rep}, added={add}, total_clips={total}")

    if args.preview is not None:
        body_glb = _REPO_ROOT / "assets/characters/bodies/male_body.glb"
        rows = []
        for clip in clips:
            rows.append(render_clip_strip(clip, body_glb, weapon_glb, n_panels=6, panel_w=180, panel_h=220))
        sheet = np.concatenate(rows, axis=0)
        write_png(args.preview, sheet)
        print(f"Rendered 3D skinned preview sheet ({sheet.shape[1]}x{sheet.shape[0]}) -> {args.preview}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
