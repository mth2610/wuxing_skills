#!/usr/bin/env python3
"""Validates that dry grass state is not clobbered by secondary meadows and is gated in GLSL."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
FLORA = ROOT / "maps/toolkit/map_props_flora.inl"
MEADOW = ROOT / "maps/toolkit/map_props_meadow.inl"
SURFACE = ROOT / "maps/toolkit/shaders/nature_surface.glsl"


def main():
    flora_src = FLORA.read_text(encoding="utf-8")
    meadow_src = MEADOW.read_text(encoding="utf-8")
    surf_src = SURFACE.read_text(encoding="utf-8")

    errors = []

    if ".growthForm = MAP_MEADOW_GROWTH_REED" not in flora_src:
        errors.append("Missing reed meadow definition in map_props_flora.inl")
    else:
        reed_block = flora_src.split(".growthForm = MAP_MEADOW_GROWTH_REED", 1)[1].split("});", 1)[0]
        if ".dryness = style.dryness" not in reed_block or ".grassState = style.grassState" not in reed_block:
            errors.append("reedMeadow must forward style.grassState and style.dryness")

    if "style.growthForm == MAP_MEADOW_GROWTH_GRASS" not in meadow_src:
        errors.append("MapProp_CreateMeadow must guard MapProp_SetGrassState so secondary meadows do not clobber DRY state")

    for sym in ("distantCanopyTone", "bounceOffset", "sssTint"):
        if sym not in surf_src:
            errors.append(f"nature_surface.glsl missing dryness-gated {sym}")

    if errors:
        for e in errors:
            print(f"FAIL: {e}", file=sys.stderr)
        sys.exit(1)

    print("Dry grass gate check: PASSED")


if __name__ == "__main__":
    main()
