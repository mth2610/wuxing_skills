#!/usr/bin/env python3
"""Validate canonical VFX surface profiles before they reach composition."""

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "assets" / "vfx_surface_profiles.json"
INDEX = ROOT / "assets" / "INDEX.md"
PRIMITIVES = {"ribbon", "tube", "puff", "fire_tongue", "decal"}
WRAPS = {"clamp", "repeat"}
TILE_SEAMS = {"tileable_both_axes", "crossfade_runtime"}
ROLES = {"trail", "residue", "scorch", "impact", "rune"}
FILTERS = {"bilinear", "point"}
BLENDS = {"consumer_defined", "alpha", "additive", "multiplied"}
APPROVALS = {"approved", "preview_only", "blocked_visual_owner"}
FALLBACK_POLICIES = {"no_legacy_fallback"}


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
        if profile.get("role") not in ROLES:
            failures += fail(f"{name}: invalid semantic role")
        if profile.get("wrap") not in WRAPS:
            failures += fail(f"{name}: invalid wrap")
        if profile.get("filter") not in FILTERS or profile.get("blend") not in BLENDS:
            failures += fail(f"{name}: invalid filter or blend law")
        if not profile.get("projection") or profile.get("approval") not in APPROVALS:
            failures += fail(f"{name}: projection and approval are required")
        if not profile.get("provenance") or not profile.get("consumers"):
            failures += fail(f"{name}: provenance and consumer are required (no unused profile assets)")
        flipbook = profile.get("flipbook")
        if not isinstance(flipbook, list) or len(flipbook) != 3 or any(not isinstance(v, int) or v < 0 for v in flipbook):
            failures += fail(f"{name}: flipbook must be [columns, rows, frames]")
        if profile.get("wrap") == "repeat" and profile.get("seam") not in TILE_SEAMS:
            failures += fail(f"{name}: repeating surface needs tileable/crossfade seam provenance")
        if (profile.get("wrap") == "clamp" and profile.get("primitive") != "decal" and
                profile.get("seam") != "not_applicable"):
            failures += fail(f"{name}: clamp flipbook must declare seam not_applicable")
        assets = profile.get("assets", {})
        blocked_decal = profile.get("primitive") == "decal" and profile.get("approval") == "blocked_visual_owner"
        if not blocked_decal and "body" not in assets:
            failures += fail(f"{name}: missing body role")
        if blocked_decal and assets:
            failures += fail(f"{name}: blocked decal must not acquire runtime assets before approval")
        fallback_candidates = profile.get("fallback_candidates", [])
        fallback_policy = profile.get("fallback_policy", {})
        if fallback_candidates and fallback_policy:
            failures += fail(f"{name}: choose candidate fallbacks or an explicit no-fallback policy, not both")
        if fallback_policy:
            if (fallback_policy.get("status") not in FALLBACK_POLICIES or
                    not fallback_policy.get("reason")):
                failures += fail(f"{name}: fallback policy needs a recognized decision and reason")
        if blocked_decal and not fallback_candidates and not fallback_policy:
            failures += fail(f"{name}: blocked decal requires a migration/fallback decision")
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
        if "gradient" in assets and "RGBA" not in assets["gradient"].get("channels", ""):
            failures += fail(f"{name}/gradient: gradient must document RGBA material ramp")
        lifecycle = profile.get("lifecycle", {})
        budget = profile.get("budget", {})
        life = lifecycle.get("seconds")
        fade_in, fade_out = lifecycle.get("fade_in"), lifecycle.get("fade_out")
        if not all(isinstance(v, (int, float)) and v >= 0 for v in (life, fade_in, fade_out)):
            failures += fail(f"{name}: lifecycle needs non-negative seconds/fades")
        elif fade_in + fade_out > life and life > 0:
            failures += fail(f"{name}: fades exceed lifetime")
        if not all(isinstance(budget.get(k), int) and budget[k] >= 0 for k in ("max_draw_calls", "max_textures")):
            failures += fail(f"{name}: budget needs non-negative draw/texture limits")
        elif budget["max_textures"] and len(assets) > budget["max_textures"]:
            failures += fail(f"{name}: assets exceed texture cost budget")
        if profile.get("primitive") == "decal":
            if profile.get("role") not in {"residue", "scorch", "impact", "rune"}:
                failures += fail(f"{name}: decal needs a decal semantic role")
            if profile.get("blend") == "consumer_defined":
                failures += fail(f"{name}: decal needs an explicit blend law")
            if "conformal_mesh_stamp" not in profile.get("projection", ""):
                failures += fail(f"{name}: decal needs conformal mesh projection")
            required_seam = ("symbol_boundary_alpha_required" if profile.get("role") == "rune"
                             else "edge_breakup_mask_required")
            if profile.get("seam") != required_seam:
                failures += fail(f"{name}: decal needs role-appropriate seam behavior")
            max_slope = profile.get("max_slope_degrees")
            if not isinstance(max_slope, (int, float)) or not 0.0 <= max_slope <= 90.0:
                failures += fail(f"{name}: decal needs max_slope_degrees in [0, 90]")
        for candidate in fallback_candidates:
            path = candidate.get("path")
            if not path or not (ROOT / path).is_file() or Path(path).name not in index:
                failures += fail(f"{name}: fallback candidate is missing or uncataloged")
            if candidate.get("status") not in {"must_replace", "rejected", "owner_review"} or not candidate.get("reason"):
                failures += fail(f"{name}: fallback candidate lacks review decision/provenance")
    expected = {"VFX_SURFACE_SMOKE_RIBBON", "VFX_SURFACE_ENERGY_RIBBON", "VFX_SURFACE_ENERGY_TUBE", "VFX_SURFACE_SMOKE_PUFF", "VFX_SURFACE_FIRE_TONGUE", "VFX_SURFACE_DECAL_RESIDUE", "VFX_SURFACE_DECAL_SCORCH", "VFX_SURFACE_DECAL_IMPACT", "VFX_SURFACE_DECAL_RUNE"}
    if not expected.issubset(ids):
        failures += fail("profile IDs do not cover every P1 required primary surface")
    if failures:
        return 1
    print(f"VFX surface registry: {len(profiles)} profiles, {len(paths)} semantic assets validated")
    return 0


if __name__ == "__main__":
    sys.exit(main())
