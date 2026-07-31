#!/usr/bin/env python3
"""Validate canonical VFX surface profiles before they reach composition."""

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "assets" / "vfx_surface_profiles.json"
INDEX = ROOT / "assets" / "INDEX.md"
PRIMITIVES = {"ribbon", "tube", "puff", "fire_tongue"}
WRAPS = {"clamp", "repeat"}
TILE_SEAMS = {"tileable_both_axes", "crossfade_runtime"}


def fail(message):
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def main():
    doc = json.loads(MANIFEST.read_text(encoding="utf-8"))
    index = INDEX.read_text(encoding="utf-8")
    profiles = doc.get("profiles", [])
    ids, names, paths = set(), set(), set()
    failures = 0
    for profile in profiles:
        name = profile.get("name", "<unnamed>")
        for key, values in (("id", ids), ("name", names)):
            value = profile.get(key)
            if not value or value in values:
                failures += fail(f"{name}: missing or duplicate {key}")
            values.add(value)
        if profile.get("primitive") not in PRIMITIVES:
            failures += fail(f"{name}: invalid primitive")
        if profile.get("wrap") not in WRAPS:
            failures += fail(f"{name}: invalid wrap")
        if not profile.get("provenance") or not profile.get("consumers"):
            failures += fail(f"{name}: provenance and consumer are required (no unused profile assets)")
        flipbook = profile.get("flipbook")
        if not isinstance(flipbook, list) or len(flipbook) != 3 or any(not isinstance(v, int) or v < 0 for v in flipbook):
            failures += fail(f"{name}: flipbook must be [columns, rows, frames]")
        if profile.get("wrap") == "repeat" and profile.get("seam") not in TILE_SEAMS:
            failures += fail(f"{name}: repeating surface needs tileable/crossfade seam provenance")
        if profile.get("wrap") == "clamp" and profile.get("seam") != "not_applicable":
            failures += fail(f"{name}: clamp flipbook must declare seam not_applicable")
        assets = profile.get("assets", {})
        if "body" not in assets:
            failures += fail(f"{name}: missing body role")
        for role, asset in assets.items():
            path, channels = asset.get("path"), asset.get("channels")
            if role not in {"body", "flow", "mask", "gradient", "fallback_body"}:
                failures += fail(f"{name}: unknown asset role {role}")
            if not path or not path.startswith("assets/textures/"):
                failures += fail(f"{name}/{role}: invalid runtime path")
                continue
            if not (ROOT / path).is_file():
                failures += fail(f"{name}/{role}: missing file {path}")
            if not channels:
                failures += fail(f"{name}/{role}: missing channel semantics")
            if path in paths:
                failures += fail(f"{name}/{role}: duplicate registered asset {path}")
            paths.add(path)
            if Path(path).name not in index:
                failures += fail(f"{name}/{role}: {path} is not cataloged in assets/INDEX.md")
        if "flow" in assets and "RG" not in assets["flow"].get("channels", ""):
            failures += fail(f"{name}/flow: flow map must document RG direction channels")
        if "mask" in assets and "R" not in assets["mask"].get("channels", ""):
            failures += fail(f"{name}/mask: mask must document scalar R channel")
    expected = {"VFX_SURFACE_SMOKE_RIBBON", "VFX_SURFACE_ENERGY_RIBBON", "VFX_SURFACE_ENERGY_TUBE", "VFX_SURFACE_SMOKE_PUFF", "VFX_SURFACE_FIRE_TONGUE"}
    if not expected.issubset(ids):
        failures += fail("profile IDs do not cover every P1 required primary surface")
    if failures:
        return 1
    print(f"VFX surface registry: {len(profiles)} profiles, {len(paths)} semantic assets validated")
    return 0


if __name__ == "__main__":
    sys.exit(main())
