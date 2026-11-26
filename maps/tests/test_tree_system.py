#!/usr/bin/env python3
"""Validates the 3-stage procedural tree system, 8-cell dilated atlas, GLSL contracts, and 24-bit vitality packing."""
import math
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent


def pack_state(phase: float, dryness: float, scorch: float, frost: float) -> float:
    p_frac = phase - math.floor(phase)
    p_q = min(255, max(0, int(math.floor(p_frac * 255.0 + 0.5))))
    d_q = min(255, max(0, int(math.floor(dryness * 255.0 + 0.5))))
    s_q = min(15, max(0, int(math.floor(scorch * 15.0 + 0.5))))
    f_q = min(15, max(0, int(math.floor(frost * 15.0 + 0.5))))
    packed = p_q | (d_q << 8) | (s_q << 16) | (f_q << 20)
    # Round-trip through IEEE-754 float32
    return struct.unpack("<f", struct.pack("<f", float(packed)))[0]


def unpack_state(packed_f32: float):
    v = int(math.floor(packed_f32 + 0.5))
    return (
        (v & 255) / 255.0,
        ((v >> 8) & 255) / 255.0,
        ((v >> 16) & 15) / 15.0,
        ((v >> 20) & 15) / 15.0,
    )


def check_file_contains(rel_path: str, needles: list[str], errors: list[str]):
    p = ROOT / rel_path
    if not p.exists():
        errors.append(f"Missing file: {rel_path}")
        return
    txt = p.read_text(encoding="utf-8")
    for n in needles:
        if n not in txt:
            errors.append(f"{rel_path}: missing '{n}'")


def main():
    errors: list[str] = []

    # 1. Public API symbols in map_props.h
    check_file_contains("maps/toolkit/map_props.h", [
        "MAP_TREE_STATE_LUSH",
        "MAP_TREE_STATE_AUTUMN",
        "MAP_TREE_STATE_WITHERED",
        "MAP_TREE_STATE_DRY_BARE",
        "MAP_TREE_STATE_SCORCHED",
        "MapProp_SetTreeCondition",
        "MapProp_SetTreeState",
        "float dryness;",
        "float scorch;",
    ], errors)

    # 2. 3-stage procedural architecture & 8-cell dilated atlas in map_props_trees.inl
    check_file_contains("maps/toolkit/map_props_trees.inl", [
        "TREE_CELL_SPRUCE",
        "TREE_CELL_CEDAR",
        "TREE_CELL_PINE_ROSETTE",
        "TREE_CELL_PEACH_BLOSSOM",
        "TREE_CELL_OAK_BROADLEAF",
        "TREE_CELL_BIRCH_SPRIG",
        "TREE_CELL_BAMBOO_FAN",
        "TREE_CELL_WILLOW_WHIP",
        "TreeAtlas_DilateTransparentRGB",
        "Tree_TransportFrame",
        "Tree_SweepBranchSpline",
        "TreeSkeleton",
        "TreeCanopyLobe",
        "Tree_EmitSprigCard",
        "Tree_PackInstanceState",
        "MapProp_SetTreeCondition",
        "MapProp_SetTreeState",
    ], errors)

    # 3. Shader contracts: zero 'f' float suffixes and zero '#include' inside comments
    shader_files = (
        "maps/toolkit/shaders/tree_instanced.vs",
        "maps/toolkit/shaders/tree_foliage.fs",
        "maps/toolkit/shaders/tree_shadow_instanced.vs",
        "maps/toolkit/shaders/tree_shadow.fs",
    )
    for rel in shader_files:
        p = ROOT / rel
        if not p.exists():
            errors.append(f"Missing shader: {rel}")
            continue
        src = p.read_text(encoding="utf-8")
        for lineno, line in enumerate(src.splitlines(), 1):
            parts = line.split("//", 1)
            code = parts[0]
            comment = parts[1] if len(parts) > 1 else ""
            if re.search(r"\b\d+\.\d*f\b", code):
                errors.append(f"{rel}:{lineno}: float literal with 'f' suffix forbidden in GLSL")
            if "#include" in comment:
                errors.append(f"{rel}:{lineno}: '#include' inside comment forbidden (ENGINE_LANDMINES #20)")

    # 4. Matching defoliation logic across visible and shadow vertex shaders
    for vs in ("maps/toolkit/shaders/tree_instanced.vs", "maps/toolkit/shaders/tree_shadow_instanced.vs"):
        check_file_contains(vs, [
            "u_treeGlobalDryness",
            "u_treeGlobalScorch",
            "leafRank > retention",
            "rankBucket",
        ], errors)

    # 5. Exact 24-bit float32 state packing round-trip test
    samples = [
        (0.0, 0.0, 0.0, 0.0),
        (0.999, 1.0, 1.0, 1.0),
        (0.37, 0.42, 0.60, 0.25),
        (0.73, 0.88, 0.93, 0.50),
    ]
    for ph, dr, sc, fr in samples:
        packed = pack_state(ph, dr, sc, fr)
        uph, udr, usc, ufr = unpack_state(packed)
        if abs(uph - (ph - math.floor(ph))) > 0.005:
            errors.append(f"phase round-trip error: {ph} -> {uph}")
        if abs(udr - dr) > 0.005:
            errors.append(f"dryness round-trip error: {dr} -> {udr}")
        if abs(usc - sc) > 0.07:
            errors.append(f"scorch round-trip error: {sc} -> {usc}")
        if abs(ufr - fr) > 0.07:
            errors.append(f"frost round-trip error: {fr} -> {ufr}")

    # 6. Biome worlds integrate MapTreeSystem cleanly
    for world_rel in (
        "maps/worlds/song_quao/song_quao.c",
        "maps/worlds/mau_son/mau_son.c",
        "maps/worlds/ninh_thuan/ninh_thuan.c",
        "maps/worlds/ly_son/ly_son.c",
        "maps/worlds/phu_quoc/phu_quoc.c",
    ):
        check_file_contains(world_rel, [
            "MapProp_CreateTreeSystem",
            "MapProp_PrepareTreeSystem",
            "MapProp_DrawTreeSystem",
            "MapProp_UnloadTreeSystem",
        ], errors)

    if errors:
        for e in errors:
            print(f"FAIL: {e}", file=sys.stderr)
        sys.exit(1)

    print("Tree system contract & packing test: PASSED")


if __name__ == "__main__":
    main()
