#!/usr/bin/env python3
"""Validates biome-specific grass growth forms, new wildflower species, unified flower atlas, and MapShrubSystem."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent


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
    errors = []

    check_file_contains("maps/toolkit/map_props.h", [
        "MAP_MEADOW_GROWTH_XERIC_TUSSOCK",
        "MAP_MEADOW_GROWTH_ALPINE_MAT",
        "MAP_MEADOW_GROWTH_METALLIC_SEDGE",
        "MAP_FLOWER_SPECIES_SA_LIEN",
        "MAP_FLOWER_SPECIES_HOANG_THU",
        "MAP_FLOWER_SPECIES_HOA_LIEN",
        "MAP_FLOWER_SPECIES_KIM_NGAN",
        "MAP_SHRUB_XERIC_THORN",
        "MAP_SHRUB_DESERT_SAGE",
        "MAP_SHRUB_ARID_AGAVE",
        "MAP_SHRUB_GNARLED_DEADWOOD",
        "MAP_SHRUB_LUSH_AZALEA",
        "MAP_SHRUB_ALPINE_JUNIPER",
        "MAP_SHRUB_PRICKLY_PEAR",
        "MAP_SHRUB_MEADOW_HAWTHORN",
        "MAP_SHRUB_MEADOW_LILAC",
        "MAP_SHRUB_MEADOW_DOG_ROSE",
        "MAP_SHRUB_MEADOW_ELDERBERRY",
        "MAP_SHRUB_MEADOW_ROWAN",
        "MAP_TREE_MEADOW_OAK",
        "MAP_TREE_MEADOW_BIRCH",
        "MAP_TREE_ALPINE_SNOW_SPRUCE",
        "MAP_TREE_GNARLED_MOUNTAIN_PINE",
        "MAP_TREE_FROSTED_BIRCH",
        "MAP_TREE_COLUMNAR_TAIGA_SPRUCE",
        "MAP_TREE_OLD_GROWTH_FIR",
        "MAP_TREE_FROZEN_DEADWOOD",
        "MapProp_CreateShrubSystem",
        "MapProp_PrepareShrubSystem",
        "MapProp_DrawShrubSystem",
    ], errors)

    check_file_contains("maps/toolkit/map_props_shrubs.inl", [
        "Shrub_BuildArchetypeMesh",
        "MapFlora_GenerateShrubPlacements",
        "MapFlora_EvaluateShrubUnderstorey",
        "Nature_IsChunkVisibleFast",
        "UpdateTexture",
    ], errors)

    check_file_contains("maps/toolkit/shaders/shrub_instanced.vs", [
        "u_shrubInstanceTex",
        "NatureVegetationWindBend",
        "NatureDominantWindImpact",
        "u_interactionMap",
    ], errors)

    check_file_contains("maps/toolkit/shaders/shrub_foliage.fs", [
        "MapShadowVisibilityCovLS",
        "Environment_CloudVisibility",
        "Environment_HemisphereIrradiance",
        "VFXLights_Accumulate(fragPosition, N, albedo)",
    ], errors)

    # Verify zero 'f' float suffixes in new GLSL shaders (Android GLES rule)
    for glsl in ("maps/toolkit/shaders/shrub_instanced.vs", "maps/toolkit/shaders/shrub_foliage.fs"):
        src = (ROOT / glsl).read_text(encoding="utf-8")
        for lineno, line in enumerate(src.splitlines(), 1):
            code = line.split("//", 1)[0]
            import re
            if re.search(r"\b\d+\.\d*f\b", code):
                errors.append(f"{glsl}:{lineno}: float literal with 'f' suffix forbidden in GLSL")

    check_file_contains("maps/toolkit/map_props_flowers.inl", [
        "FlowerParametric_CreateSharedAtlas",
        "s_flowerBaseOffsetLoc",
        "ownsInstanceTexture",
    ], errors)

    check_file_contains("maps/toolkit/shaders/flower_parametric.vs", [
        "u_instanceBaseOffset",
        "sp == 9",
        "sp == 10",
        "sp == 11",
        "sp == 12",
    ], errors)

    check_file_contains("maps/toolkit/shaders/flower_parametric.fs", [
        "sp == 9",
        "sp == 10",
        "sp == 11",
        "sp == 12",
        "distSq < 256.0",
    ], errors)

    check_file_contains("maps/worlds/ninh_thuan/ninh_thuan.c", [
        "MAP_MEADOW_GROWTH_XERIC_TUSSOCK",
        "MAP_FLOWER_SPECIES_SA_LIEN",
        "MAP_FLOWER_SPECIES_HOANG_THU",
        "MapFlora_GenerateShrubPlacements",
        "MapFlora_EvaluateShrubUnderstorey",
        "s_rocks.items",
        "MapFlora_GenerateOpenWorldFlowerClusters",
        "focalPoints",
    ], errors)

    check_file_contains("maps/worlds/ly_son/ly_son.c", [
        "MAP_MEADOW_GROWTH_XERIC_TUSSOCK",
        "MAP_FLOWER_SPECIES_HOA_LIEN",
        "MapFlora_GenerateShrubPlacements",
        "s_rocks.items",
        "MapFlora_GenerateOpenWorldFlowerClusters",
    ], errors)

    check_file_contains("maps/worlds/mau_son/mau_son.c", [
        "MAP_MEADOW_GROWTH_ALPINE_MAT",
        "MAP_FLOWER_SPECIES_KIM_NGAN",
        "MapFlora_GenerateShrubPlacements",
        "s_rocks.items",
        "MapFlora_GenerateOpenWorldFlowerClusters",
    ], errors)

    check_file_contains("maps/worlds/phu_quoc/phu_quoc.c", [
        "MAP_MEADOW_GROWTH_METALLIC_SEDGE",
        "MAP_FLOWER_SPECIES_KIM_NGAN",
        "MapFlora_GenerateShrubPlacements",
        "s_rocks.items",
        "MapFlora_GenerateOpenWorldFlowerClusters",
    ], errors)

    check_file_contains("maps/worlds/song_quao/song_quao.c", [
        "MapFlora_GenerateShrubPlacements",
        "s_songQuaoShrubs",
        "focalPoints",
    ], errors)

    # Ensure no map passes unscaled *_ROCK_ITEMS or scales shrub minSpacing by s_scaleXZ
    for map_rel in (
        "maps/worlds/ninh_thuan/ninh_thuan.c",
        "maps/worlds/ly_son/ly_son.c",
        "maps/worlds/mau_son/mau_son.c",
        "maps/worlds/phu_quoc/phu_quoc.c",
        "maps/worlds/song_quao/song_quao.c",
    ):
        txt = (ROOT / map_rel).read_text(encoding="utf-8")
        if ".minSpacing =" in txt and "* s_scaleXZ" in txt.split(".minSpacing =", 1)[1].splitlines()[0]:
            errors.append(f"{map_rel}: shrub minSpacing must not be multiplied by s_scaleXZ")

    # Ensure MapProp_UnloadShrubSystem is called ONLY inside MapProp_UnloadFloraSystem (never in Draw)
    flora_txt = (ROOT / "maps/toolkit/map_props_flora.inl").read_text(encoding="utf-8")
    if flora_txt.count("MapProp_UnloadShrubSystem") != 1:
        errors.append(
            f"maps/toolkit/map_props_flora.inl: expected MapProp_UnloadShrubSystem once (in Unload), found {flora_txt.count('MapProp_UnloadShrubSystem')}"
        )

    if errors:
        for e in errors:
            print(f"FAIL: {e}", file=sys.stderr)
        sys.exit(1)

    print("Biome flora and shrubs check: PASSED")


if __name__ == "__main__":
    main()
