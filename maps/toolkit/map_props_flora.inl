// Generalized Flora System (Meadows, Reeds, Wildflower Colonies)
// Included into map_props.c — not a standalone compilation unit.

Color MapProp_GetFlowerSpeciesColor(int variant, bool accent)
{
    static const Color primary[8] = {
        {252, 250, 242, 255}, // 0: ivory white daisy
        {255, 212, 48, 255},  // 1: warm golden buttercup
        {232, 54, 42, 255},   // 2: vermilion crimson poppy
        {88, 148, 242, 255},  // 3: heavenly sapphire cornflower
        {248, 142, 175, 255}, // 4: wild peach-rose pink
        {208, 88, 156, 255},  // 5: orchid magenta cosmos
        {158, 128, 232, 255}, // 6: gentle lavender violet
        {255, 240, 168, 255}, // 7: pale primrose cream
    };
    static const Color secondary[8] = {
        {255, 246, 228, 255}, {255, 185, 28, 255},  {198, 32, 36, 255},
        {64, 118, 224, 255},  {242, 112, 148, 255}, {178, 52, 126, 255},
        {134, 98, 216, 255},  {255, 220, 130, 255},
    };
    variant &= 7;
    return accent ? secondary[variant] : primary[variant];
}

MapFloraSystem MapProp_CreateFloraSystem(
    const MapMeadowPlacement *grassPlacements, int grassCount,
    const MapMeadowPlacement *reedPlacements, int reedCount,
    const MapFlowerPlacement *flowerPlacements, const MapFloraFlowerCluster *clusters, int clusterCount,
    MapFloraStyle style)
{
    MapFloraSystem flora = {0};
    flora.windDirection = (Vector2){0.86f, 0.51f};
    flora.windStrength = 0.035f;

    // 1. Valley / Meadow Grass (Smooth 4-segment Bézier clumps)
    if (grassPlacements && grassCount > 0) {
        flora.valleyMeadow = MapProp_CreateMeadow(grassPlacements, grassCount,
            (MapMeadowStyle){
                .rootColor = (style.grassRootColor.a > 0) ? style.grassRootColor : (Color){18, 34, 16, 255},
                .tipColor  = (style.grassTipColor.a > 0)  ? style.grassTipColor  : (Color){136, 186, 54, 255},
                .bladesPerClump = (style.bladesPerClump > 0) ? style.bladesPerClump : 6,
                .bladeSegments  = (style.bladeSegments > 0)  ? style.bladeSegments  : 4,
                .bladeWidthScale = (style.bladeWidthScale > 0.0f) ? style.bladeWidthScale : 0.16f,
                .chunkSize = 12.0f,
                .lodDistance = (style.lodDistance > 0.0f) ? style.lodDistance : 32.0f,
                .midLodDistance = 0.0f, // 0.0f prevents near-camera grass flattening
                .drawDistance = (style.drawDistance > 0.0f) ? style.drawDistance : 60.0f,
                .shadowDistance = 14.0f,
                .texturePath = NULL,
                .botanicalVariation = 1.0f,
                .growthForm = MAP_MEADOW_GROWTH_GRASS,
            });
        flora.valleyGrassCount = grassCount;
    }

    // 2. Shoreline & River Reeds
    if (reedPlacements && reedCount > 0) {
        flora.reedMeadow = MapProp_CreateMeadow(reedPlacements, reedCount,
            (MapMeadowStyle){
                .rootColor = (style.reedRootColor.a > 0) ? style.reedRootColor : (Color){26, 42, 20, 255},
                .tipColor  = (style.reedTipColor.a > 0)  ? style.reedTipColor  : (Color){136, 172, 82, 255},
                .bladesPerClump = 7,
                .bladeSegments  = 4,
                .bladeWidthScale = 0.14f,
                .chunkSize = 18.0f,
                .lodDistance = 36.0f,
                .midLodDistance = 18.0f,
                .drawDistance = 65.0f,
                .shadowDistance = 12.0f,
                .texturePath = NULL,
                .hasPlumes = style.hasReedPlumes,
                .growthForm = MAP_MEADOW_GROWTH_REED,
            });
        flora.reedCount = reedCount;
    }

    // 3. Clustered Wildflower Fields
    if (flowerPlacements && clusters && clusterCount > 0) {
        flora.flowerFields = (MapFlowerField *)RL_MALLOC(clusterCount * sizeof(MapFlowerField));
        flora.flowerClusterCount = clusterCount;

        const char *atlasPath = style.flowerAtlasPath ? style.flowerAtlasPath
            : "maps/toolkit/textures/wildflower_bloom_atlas_v2.png";

        int placementOffset = 0;
        for (int i = 0; i < clusterCount; i++) {
            const MapFloraFlowerCluster *cl = &clusters[i];
            int count = cl->flowerCount;
            if (count <= 0) {
                flora.flowerFields[i] = (MapFlowerField){0};
                continue;
            }

            Color primary = (cl->primaryColor.a > 0) ? cl->primaryColor
                : MapProp_GetFlowerSpeciesColor(cl->primarySpecies, false);
            Color accent = (cl->accentColor.a > 0) ? cl->accentColor
                : MapProp_GetFlowerSpeciesColor(cl->primarySpecies, true);

            flora.flowerFields[i] = MapProp_CreateFlowerField(
                &flowerPlacements[placementOffset], count,
                primary, accent, atlasPath, 0.42f, 4, 2);

            MapProp_SetFlowerFieldDrawDistance(&flora.flowerFields[i], 75.0f);
            placementOffset += count;
        }
    }

    flora.time = 0.0f;
    flora.ready = (flora.valleyMeadow.ready || flora.reedMeadow.ready || flora.flowerClusterCount > 0);
    return flora;
}

void MapProp_UpdateFloraSystem(MapFloraSystem *flora, Vector3 cameraTarget, float dt)
{
    if (!flora || !flora->ready) return;
    flora->time += dt;

    if (flora->valleyGrassCount > 0) {
        MapProp_PrepareMeadow(&flora->valleyMeadow, (Vector3){0});
    }
    if (flora->reedCount > 0) {
        MapProp_PrepareMeadow(&flora->reedMeadow, (Vector3){0});
    }

    MapProp_BeginNatureInteraction(cameraTarget, dt);
    MapProp_AddNatureInteractor(cameraTarget, 1.25f, 0.34f);
    MapProp_AddNatureWindVorticles(flora->time);
    MapProp_EndNatureInteraction();
}

void MapProp_DrawFloraSystem(MapFloraSystem *flora)
{
    if (!flora || !flora->ready) return;

    if (flora->valleyGrassCount > 0) {
        MapProp_DrawMeadow(&flora->valleyMeadow, (Vector3){0}, flora->time,
                           flora->windDirection, flora->windStrength);
    }
    if (flora->reedCount > 0) {
        MapProp_DrawMeadow(&flora->reedMeadow, (Vector3){0}, flora->time,
                           flora->windDirection, 0.11f);
    }
    for (int i = 0; i < flora->flowerClusterCount; i++) {
        if (flora->flowerFields[i].ready) {
            MapProp_DrawFlowerField(&flora->flowerFields[i], (Vector3){0}, flora->time,
                                    flora->windDirection, 0.032f);
        }
    }
}

void MapProp_DrawFloraShadowCasters(MapFloraSystem *flora)
{
    if (!flora || !flora->ready) return;

    Vector3 offset = {0};
    if (flora->valleyGrassCount > 0) {
        MapProp_DrawMeadowShadowCasters(&flora->valleyMeadow, offset, flora->time,
                                         flora->windDirection, flora->windStrength);
    }
    if (flora->reedCount > 0) {
        MapProp_DrawMeadowShadowCasters(&flora->reedMeadow, offset, flora->time,
                                         flora->windDirection, 0.11f);
    }
    for (int i = 0; i < flora->flowerClusterCount; i++) {
        if (flora->flowerFields[i].ready) {
            MapProp_DrawFlowerFieldShadowCaster(&flora->flowerFields[i], offset, flora->time,
                                                flora->windDirection, 0.032f);
        }
    }
}

void MapProp_UnloadFloraSystem(MapFloraSystem *flora)
{
    if (!flora || !flora->ready) return;

    for (int i = 0; i < flora->flowerClusterCount; i++) {
        if (flora->flowerFields[i].ready) {
            MapProp_UnloadFlowerField(&flora->flowerFields[i]);
        }
    }
    if (flora->flowerFields) {
        RL_FREE(flora->flowerFields);
        flora->flowerFields = NULL;
    }
    flora->flowerClusterCount = 0;

    if (flora->reedCount > 0) {
        MapProp_UnloadMeadow(&flora->reedMeadow);
        flora->reedCount = 0;
    }
    if (flora->valleyGrassCount > 0) {
        MapProp_UnloadMeadow(&flora->valleyMeadow);
        flora->valleyGrassCount = 0;
    }
    flora->ready = false;
}
