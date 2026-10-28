#!/usr/bin/env python3
"""
Master Rock Pipeline CLI Runner.
Usage:
    python3 maps/pipeline/generate_rocks.py --theme song_quao
Executes:
    Stage 0: Load Theme Profile JSON
    Stage 1: Terrain Derivatives (terrain_fields.py)
    Stage 2 & 3: Geological & Lithological Inference (geology.py)
    Stage 4: Hero Boulder Placement (hero_placement.py)
    Stage 5: Japanese Ishigumi Arrangement (ishigumi.py)
    Stage 6: Gravel & Grounding Fields + C Header Export (gravel_fields.py)
"""

import os
import sys
import argparse

PIPELINE_DIR = os.path.dirname(os.path.abspath(__file__))
if PIPELINE_DIR not in sys.path:
    sys.path.insert(0, PIPELINE_DIR)

from rocks.terrain_fields import compute_terrain_fields
from rocks.geology import compute_geology_fields
from rocks.hero_placement import place_hero_boulders
from rocks.ishigumi import generate_ishigumi_arrangements
from rocks.gravel_fields import generate_gravel_and_grounding


def run_pipeline(theme_name):
    print("=" * 72)
    print(f"  WUXING SKILLS ROCK PIPELINE: Theme '{theme_name}'")
    print("  Deterministic Geomorphology, Hydrodynamics & Japanese Ishigumi")
    print("=" * 72)

    theme_path = os.path.join(PIPELINE_DIR, "rocks", "themes", f"{theme_name}.json")
    if not os.path.exists(theme_path):
        print(f"Error: Theme file not found: {theme_path}")
        return False

    analysis_path = f"assets/maps/{theme_name}/{theme_name}_analysis.npz"
    if not os.path.exists(analysis_path):
        print(f"Error: Analysis file not found: {analysis_path}")
        return False

    out_dir = f"assets/maps/{theme_name}/rock_pipeline"
    c_header_path = f"assets/maps/{theme_name}/{theme_name}_rock_data.h"

    print("\n>>> Stage 1: Computing Topographic Derivatives...")
    compute_terrain_fields(analysis_path, theme_path, out_dir)

    terrain_npz = os.path.join(out_dir, "rock_terrain_fields.npz")
    print("\n>>> Stage 2 & 3: Inferring Geological Substrate & Outcrop Probability...")
    compute_geology_fields(terrain_npz, theme_path, out_dir)

    geology_npz = os.path.join(out_dir, "rock_geology_fields.npz")
    print("\n>>> Stage 4: Placing Monumental Hero Boulders (Đá Chủ)...")
    place_hero_boulders(terrain_npz, geology_npz, theme_path, out_dir)

    heroes_json = os.path.join(out_dir, "rock_heroes.json")
    print("\n>>> Stage 5: Executing Japanese Ishigumi Arrangement Grammar...")
    generate_ishigumi_arrangements(terrain_npz, geology_npz, heroes_json, theme_path, out_dir)

    placements_json = os.path.join(out_dir, "rock_ishigumi_placements.json")
    print("\n>>> Stage 6: Baking Grounding Masks & Exporting Production C Assets...")
    generate_gravel_and_grounding(terrain_npz, placements_json, theme_path, out_dir, c_header_path)

    print("\n" + "=" * 72)
    print("  PIPELINE COMPLETE: Assets successfully generated and exported!")
    print("=" * 72)
    return True


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Wuxing Skills Rock Pipeline")
    parser.add_argument("--theme", default="song_quao", help="Theme profile name (default: song_quao)")
    args = parser.parse_args()

    success = run_pipeline(args.theme)
    if not success:
        sys.exit(1)
