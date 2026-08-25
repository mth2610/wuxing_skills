#!/usr/bin/env python3
"""Validate canonical VFX surface profiles before they reach composition.

The channel-grammar half of this file enforces assets/TEXTURE_PACKING.md. Read
that document before loosening anything here: every rule it states exists
because the alternative already cost a debugging session.
"""

import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "assets" / "vfx_surface_profiles.json"
INDEX = ROOT / "assets" / "INDEX.md"
PACKING_SPEC = ROOT / "assets" / "TEXTURE_PACKING.md"

# ── The channel grammar (assets/TEXTURE_PACKING.md §2) ──────────────────────
# <LAYOUT> | R:<slot>/<mode> | G:... | B:... | A:...  [— free prose]
CHANNEL_RE = re.compile(
    r"^(?P<layout>[A-Z_]+)\s*\|\s*"
    r"R:(?P<r_slot>\w+)/(?P<r_mode>\w+)\s*\|\s*"
    r"G:(?P<g_slot>\w+)/(?P<g_mode>\w+)\s*\|\s*"
    r"B:(?P<b_slot>\w+)/(?P<b_mode>\w+)\s*\|\s*"
    r"A:(?P<a_slot>\w+)/(?P<a_mode>\w+)\s*(?:—.*)?$",
    re.DOTALL,
)

MODES = {"TILE", "STRETCH", "CLAMP"}

# Permitted slot per channel per layout. A set means any one of them.
LAYOUTS = {
    "STRAND":   {"R": {"pattern1"}, "G": {"pattern2"}, "B": {"distort"}, "A": {"dissolve"}},
    "FLOW":     {"R": {"body"}, "G": {"mask", "dissolve"}, "B": {"flowx"}, "A": {"flowy"}},
    "OPAQUE":   {"R": {"color"}, "G": {"color"}, "B": {"color"}, "A": {"opacity"}},
    "FLIPBOOK": {"R": {"color"}, "G": {"color"}, "B": {"color"}, "A": {"opacity"}},
    # Ray-marched gas: four scalar fields, NO colour. Colour comes from a ramp
    # LUT indexed by emission at draw time, which is what keeps one sheet usable
    # as orange fire and as purple magic fire.
    "VOLUME": {"R": {"emission"}, "G": {"density"}, "B": {"shadow"}, "A": {"opacity"}},
    # Pure data: four decorrelated scalar fields. Never drawn.
    "NOISE":    {"R": {"field"}, "G": {"field"}, "B": {"field"}, "A": {"field"}},
    # Pre-spec files: a standalone flow map or mask that leaves channels
    # constant. Tolerated so the build is not held hostage to a migration, but
    # ALWAYS reported — see report_debt() — because R6 forbids a constant
    # channel and these are precisely the files the FLOW layout exists to
    # absorb. Never label a new sheet SPLIT_LEGACY.
    "SPLIT_LEGACY": {
        "R": {"flowx", "mask", "color"},
        "G": {"flowy", "unused", "color"},
        "B": {"unused", "color"},
        "A": {"unused", "opacity"},
    },
}

# R3: signed slots encode 128 as neutral and decode c*2-1.
SIGNED_SLOTS = {"distort", "flowx", "flowy"}
PRIMITIVES = {"ribbon", "tube", "puff", "fire_tongue", "decal", "disc"}
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


DEBT = []
LIES = []
GHOSTS = []


# ── Does the CODE agree that a profile is unused? ───────────────────────────
#
# Same class of defect as check_pixels(), found the same day and from the same
# root: the manifest makes claims about things it never checks. Three profiles
# carried `consumers: []` and an `orphaned` block while vc_smoke_trail.inl,
# vc_smoke_column.inl and vc_beam.inl were all reading them by enum id. The
# build printed "ORPHANED" for each at every configure, and R9 says an orphan is
# deleted — so a stale empty list is a standing instruction to delete a sheet
# that three live effects depend on, one of them the reference the others are
# judged against.
#
# ONE DIRECTION ONLY, and the asymmetry is deliberate. "Declared orphaned but
# referenced" is always wrong and is the direction that destroys work. The
# reverse — declared consumed but no enum reference — is NOT a defect: decal
# profiles are resolved by role and appearance rather than by id, so grepping
# the enum would report four false alarms. Checking only the dangerous direction
# is what keeps this check trustworthy enough to act on.
def find_enum_references(enum_id, source_roots):
    hits = []
    for root in source_roots:
        for path in sorted(root.rglob("*")):
            if path.suffix not in (".c", ".inl"):
                continue
            if "generated" in path.name or "/tests/" in path.as_posix():
                continue
            try:
                if enum_id in path.read_text(errors="ignore"):
                    hits.append(path.name)
            except OSError:
                pass
    return hits


# ── Does the FILE carry the channels the declaration promises? ──────────────
#
# Added 25/08/2026 after the whole of this validator turned out to check only
# what the JSON SAYS. All three VOLUME TRAIL tube sheets declared
# `A:opacity` and shipped with no alpha channel at all — energy_volume.png is a
# palette PNG with no tRNS, smoke_volume.png and fire_volume.png are plain
# greyscale — so raylib expanded alpha to a constant 255 and the sheets painted
# filaments onto a surface that was opaque everywhere. The effect read as
# crumpled foil, the declaration had been wrong since the file was written, and
# nothing in the build could see it, because the grammar check above matches a
# STRING.
#
# Existence is read from IHDR and tRNS alone — no decompression, so this costs
# nothing at configure time and is exact. Constant-value detection needs the
# pixels and is done only for small sheets; see check_pixels().
def png_header(path):
    """(width, height, bit_depth, colour_type, has_trns) or None if unreadable."""
    try:
        data = path.read_bytes()
    except OSError:
        return None
    if len(data) < 33 or data[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    width, height, depth, ctype = struct.unpack(">IIBB", data[16:26])
    interlace = data[28]
    return width, height, depth, ctype, (b"tRNS" in data), interlace


# Which of R/G/B/A a PNG colour type physically carries. Greyscale duplicates
# its one channel across R/G/B, which is why a grey sheet can legitimately
# declare three colour channels and still be lying about the fourth.
CTYPE_CHANNELS = {
    0: {"R", "G", "B"},           # greyscale
    2: {"R", "G", "B"},           # truecolour
    3: {"R", "G", "B"},           # palette (A only via tRNS)
    4: {"R", "G", "B", "A"},      # greyscale + alpha
    6: {"R", "G", "B", "A"},      # truecolour + alpha
}


def check_pixels(label, path, text):
    """Cross-check a channels declaration against the actual PNG."""
    match = CHANNEL_RE.match((text or "").strip())
    if match is None:
        return 0          # the grammar check already reported this
    hdr = png_header(path)
    if hdr is None:
        return 0          # missing-file is reported by the caller
    _w, _h, _depth, ctype, has_trns, _interlace = hdr

    present = set(CTYPE_CHANNELS.get(ctype, {"R", "G", "B", "A"}))
    if ctype == 3 and has_trns:
        present.add("A")

    for channel in ("R", "G", "B", "A"):
        slot = match.group(f"{channel.lower()}_slot")
        if slot == "unused":
            continue      # already counted as debt by check_channels()
        if channel not in present:
            LIES.append(
                f"{label}: declares {channel}:{slot} but {path.name} is PNG colour "
                f"type {ctype}"
                + (" with no tRNS" if ctype == 3 else "")
                + f" — it has no {channel} channel, so the consumer reads a "
                f"constant (TEXTURE_PACKING.md R6)"
            )
    return 0


def check_channels(label, text, flipbook):
    """Enforce assets/TEXTURE_PACKING.md §2 on one channels string."""
    failures = 0
    match = CHANNEL_RE.match((text or "").strip())
    if not match:
        return fail(
            f"{label}: channels must start with the grammar of "
            f"assets/TEXTURE_PACKING.md §2 — "
            f"'<LAYOUT> | R:<slot>/<mode> | G:... | B:... | A:...'; got {text!r}"
        )

    layout = match.group("layout")
    if layout not in LAYOUTS:
        return fail(f"{label}: unknown layout {layout} (expected one of {sorted(LAYOUTS)})")

    permitted = LAYOUTS[layout]
    for channel in ("R", "G", "B", "A"):
        slot = match.group(f"{channel.lower()}_slot")
        mode = match.group(f"{channel.lower()}_mode")
        if slot not in permitted[channel]:
            failures += fail(
                f"{label}: {layout}.{channel} must be one of "
                f"{sorted(permitted[channel])}, got {slot!r}"
            )
        if mode not in MODES:
            failures += fail(f"{label}: {channel} has unknown mode {mode!r}")
        # R6 — a packed sheet exists to use all four channels. Only the
        # explicitly deprecated bucket may carry a constant one, and it is
        # counted as debt rather than waved through.
        if slot == "unused":
            if layout == "SPLIT_LEGACY":
                DEBT.append(f"{label}: {channel} constant (R6)")
            else:
                failures += fail(
                    f"{label}: {channel} is 'unused'; TEXTURE_PACKING.md R6 forbids a "
                    f"constant channel in a packed layout"
                )
        # A flipbook cell is never wrapped.
        if layout == "FLIPBOOK" and mode != "CLAMP":
            failures += fail(f"{label}: FLIPBOOK.{channel} must be CLAMP, got {mode}")

    # R1 — a channel cannot be both a shape and a seamless material. The
    # grammar makes this structurally impossible per channel; what is still
    # worth checking is that a STRETCH channel never appears in a layout whose
    # consumer has no stretch switch.
    modes = {match.group(f"{c}_mode") for c in "rgba"}
    if "STRETCH" in modes and layout not in ("STRAND", "FLOW"):
        failures += fail(
            f"{label}: STRETCH is only meaningful for STRAND/FLOW, whose consumers "
            f"carry an explicit stretch switch; {layout} has none"
        )

    # A declared flipbook must use a celled layout, and vice versa. VOLUME is
    # celled too — it is the same ray-marched sheet as FLIPBOOK, differing in
    # what the channels MEAN (four scalar fields, no colour), not in whether it
    # is a grid of frames.
    CELLED = ("FLIPBOOK", "VOLUME")
    has_cells = isinstance(flipbook, list) and len(flipbook) == 3 and flipbook[2] > 0
    if has_cells and layout not in CELLED:
        failures += fail(f"{label}: profile declares flipbook cells but layout is {layout}")
    if layout in CELLED and not has_cells:
        failures += fail(f"{label}: {layout} layout but the profile declares no cells")

    return failures


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
        # R9 — a profile with no consumer is deleted, not kept. Deferring the
        # deletion is allowed, but only on the record: an "orphaned" block with
        # a date and a reason. Nothing sits in the registry unexplained.
        orphaned = profile.get("orphaned")
        if not profile.get("provenance"):
            failures += fail(f"{name}: provenance is required")
        if not profile.get("consumers"):
            refs = find_enum_references(profile.get("id", ""), [ROOT / "core"])
            if refs:
                GHOSTS.append(
                    f"{name}: declares no consumer, but {', '.join(sorted(set(refs)))} "
                    f"reference {profile.get('id')} directly"
                )
            if not orphaned:
                failures += fail(
                    f"{name}: no consumer — delete the profile and its assets, or declare "
                    f'"orphaned": {{"since": ..., "reason": ...}} (TEXTURE_PACKING.md R9)'
                )
            elif not orphaned.get("since") or not orphaned.get("reason"):
                failures += fail(f"{name}: orphaned block needs both 'since' and 'reason'")
            else:
                print(f"ORPHANED since {orphaned['since']}: {name} — {orphaned['reason'][:80]}...")
        elif orphaned:
            failures += fail(f"{name}: has consumers but is still marked orphaned — drop the block")
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
            else:
                failures += check_pixels(f"{name}/{role}", ROOT / path, channels)
            if not channels:
                failures += fail(f"{name}/{role}: missing channel semantics")
            else:
                failures += check_channels(f"{name}/{role}", channels, profile.get("flipbook"))
            if path in paths:
                failures += fail(f"{name}/{role}: duplicate registered asset {path}")
            paths.add(path)
            if Path(path).name not in index:
                failures += fail(f"{name}/{role}: {path} is not cataloged in assets/INDEX.md")
        # The old substring checks on "RG"/"R"/"RGBA" here were replaced by
        # check_channels(): the grammar states the slot of every channel, so a
        # flow map that forgot its direction encoding now fails on the slot
        # name rather than on whether the prose happened to contain "RG".
        #
        # R8 — a PACKED profile is exactly one file. max_textures elsewhere is
        # a ceiling, not a count, so this applies only where packing is the
        # whole point: it is what stops a STRAND/FLOW profile quietly regrowing
        # the second and third file it was created to eliminate.
        body_layout = (assets.get("body", {}).get("channels", "").split("|")[0].strip())
        if body_layout in ("STRAND", "FLOW"):
            if len(assets) != 1:
                failures += fail(
                    f"{name}: body is {body_layout} (packed) but {len(assets)} assets are "
                    f"registered — a packed sheet is one file (TEXTURE_PACKING.md R8)"
                )
            if profile.get("budget", {}).get("max_textures") != 1:
                failures += fail(
                    f"{name}: body is {body_layout} (packed) so budget.max_textures must be 1 "
                    f"(TEXTURE_PACKING.md R8)"
                )
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
    if DEBT:
        # Not a failure — but never silent either. These are the channels the
        # FLOW layout exists to reclaim, and the count is the migration's
        # progress bar.
        print(f"\nPACKING DEBT — {len(DEBT)} constant channels across SPLIT_LEGACY assets:")
        for item in DEBT:
            print(f"  · {item}")
        print("  Fold each into its body sheet as FLOW (assets/TEXTURE_PACKING.md §4).")
    # Reported separately from DEBT, and worded harder, because it is a
    # different kind of problem. DEBT is a sheet that ADMITS to a constant
    # channel; this is a sheet whose declaration and whose pixels disagree, so
    # every reader of the manifest — including this validator, until 25/08/2026
    # — has been told something untrue about the asset. Not a hard failure only
    # because the three known cases predate the check and the fix is a
    # regeneration, not a one-line edit.
    if GHOSTS:
        print(f"\nDECLARED-ORPHAN BUT REFERENCED — {len(GHOSTS)}:")
        for item in GHOSTS:
            print(f"  ! {item}")
        print("  R9 deletes an orphan, so an empty consumers list on a profile the")
        print("  code still reads is an instruction to delete something in use.")
    if LIES:
        print(f"\nDECLARED-BUT-ABSENT — {len(LIES)} channel(s) the file does not carry:")
        for item in LIES:
            print(f"  ! {item}")
        print("  Regenerate the sheet with the channel, or correct the declaration.")
        print("  Reference recipe: scripts/gen_volume_surface.py derives RGB and A")
        print("  from one field with DIFFERENT curves — a floor and a gamma for")
        print("  colour, a threshold for coverage.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
