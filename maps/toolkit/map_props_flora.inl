// Generalized Flora System (Meadows, Reeds, Wildflower Colonies)
// Included into map_props.c — not a standalone compilation unit.

Color MapProp_GetFlowerSpeciesColor(int variant, bool accent)
{
    static const Color primary[8] = {
        {252, 250, 242, 255}, // 0: ivory white daisy (Bạch Cúc)
        {255, 212, 48, 255},  // 1: warm golden buttercup (Hoàng Anh)
        {232, 54, 42, 255},   // 2: vermilion crimson poppy (Chu Sa Hồng)
        {88, 148, 242, 255},  // 3: heavenly sapphire cornflower (Lam Điệp)
        {248, 142, 175, 255}, // 4: wild peach-rose pink (Đào Hoa)
        {208, 88, 156, 255},  // 5: orchid magenta cosmos (Tử Mẫu)
        {158, 128, 232, 255}, // 6: gentle lavender violet (Huân Y Thảo)
        {255, 240, 168, 255}, // 7: pale primrose cream (Ngọc Lan)
    };
    static const Color secondary[8] = {
        {255, 246, 228, 255}, {255, 185, 28, 255},  {198, 32, 36, 255},
        {64, 118, 224, 255},  {242, 112, 148, 255}, {178, 52, 126, 255},
        {134, 98, 216, 255},  {255, 220, 130, 255},
    };
    variant &= 7;
    return accent ? secondary[variant] : primary[variant];
}

void MapFlora_GenerateFlowerPlacements(
    MapFlowerPlacement *outPlacements,
    int totalFlowerCount,
    const MapFloraFlowerCluster *clusters,
    int clusterCount,
    float (*sampleHeight)(float x, float z, void *ctx),
    bool (*isExcluded)(float x, float z, void *ctx),
    void *ctx,
    unsigned int seed)
{
    if (!outPlacements || totalFlowerCount <= 0 || !clusters || clusterCount <= 0)
        return;

    unsigned int rng = seed ? seed : 0x7A9B3C1Du;
    int placementCursor = 0;

    for (int c = 0; c < clusterCount && placementCursor < totalFlowerCount; c++) {
        const MapFloraFlowerCluster *cl = &clusters[c];
        int count = cl->flowerCount;
        if (count <= 0) continue;
        if (placementCursor + count > totalFlowerCount)
            count = totalFlowerCount - placementCursor;

        int clusterStartIndex = placementCursor;
        float rx = (cl->radius.x > 0.1f) ? cl->radius.x : 10.0f;
        float rz = (cl->radius.y > 0.1f) ? cl->radius.y : rx;

        // 3 organic colony epicenters per cluster along landscape flow
        float clusterSeedAngle = (float)c * 1.047f + 0.35f;
        Vector2 epicenters[3];
        for (int e = 0; e < 3; e++) {
            float epAngle = clusterSeedAngle + (float)e * 2.094f + 0.35f * sinf((float)(c * 7 + e * 3));
            float epDist = 0.32f + 0.20f * cosf((float)(c * 3 + e * 5));
            epicenters[e] = (Vector2){cosf(epAngle) * epDist, sinf(epAngle) * epDist};
        }

        for (int i = 0; i < count; i++) {
            int localIndex = i;
            int colony = localIndex % 3;
            if (Nature_Random01(&rng) < 0.20f) colony = (colony + 1) % 3;
            bool isColony = Nature_Random01(&rng) < 0.80f;

            float x = cl->center.x;
            float z = cl->center.z;
            for (int attempt = 0; attempt < 24; attempt++) {
                float angle = Nature_Random01(&rng) * (2.0f * PI);
                float rVal = powf(Nature_Random01(&rng), 1.35f);
                if (isColony) {
                    float flowAngle = 0.785f + 0.30f * sinf(cl->center.x * 0.12f + (float)c);
                    float along = cosf(angle) * rVal * 0.46f;
                    float across = sinf(angle) * rVal * 0.26f;
                    float dxNorm = epicenters[colony].x + (cosf(flowAngle) * along - sinf(flowAngle) * across);
                    float dzNorm = epicenters[colony].y + (sinf(flowAngle) * along + cosf(flowAngle) * across);
                    x = cl->center.x + dxNorm * rx;
                    z = cl->center.z + dzNorm * rz;
                } else {
                    x = cl->center.x + cosf(angle) * rx * (0.35f + 0.65f * rVal);
                    z = cl->center.z + sinf(angle) * rz * (0.35f + 0.65f * rVal);
                }

                if (isExcluded && isExcluded(x, z, ctx))
                    continue;

                // Poisson minimum distance enforcement
                bool tooClose = false;
                for (int prev = clusterStartIndex; prev < placementCursor; prev++) {
                    float pdx = x - outPlacements[prev].position.x;
                    float pdz = z - outPlacements[prev].position.z;
                    if (pdx * pdx + pdz * pdz < 0.055f * 0.055f) {
                        tooClose = true;
                        break;
                    }
                }
                if (!tooClose || attempt >= 23)
                    break;
            }

            float gy = sampleHeight ? sampleHeight(x, z, ctx) : 0.0f;
            int outIdx = placementCursor++;
            outPlacements[outIdx].position = (Vector3){x, gy + 0.014f, z};
            outPlacements[outIdx].rotationDeg = Nature_Random01(&rng) * 360.0f;
            outPlacements[outIdx].phase = Nature_Random01(&rng);

            int variant;
            if (isColony) {
                variant = cl->species[colony];
                if (Nature_Random01(&rng) < 0.16f)
                    variant = cl->species[3];
            } else {
                variant = cl->species[(int)(Nature_Random01(&rng) * 4.0f) % 4];
            }

            bool tallAccent = (variant == 2 || variant == 4 || variant == 6);
            float driftHeightBase = tallAccent ? 0.44f : 0.36f;
            outPlacements[outIdx].height = driftHeightBase + (Nature_Random01(&rng) * 0.07f - 0.02f);
            outPlacements[outIdx].bloomRadius = (tallAccent ? 0.105f : 0.082f) * (0.95f + Nature_Random01(&rng) * 0.20f);
            outPlacements[outIdx].petalColor = MapProp_GetFlowerSpeciesColor(variant, Nature_Random01(&rng) > 0.85f);
            outPlacements[outIdx].petalCount = (unsigned char)(4 + (variant % 3));
            outPlacements[outIdx].bloomVariant = (unsigned char)variant;
            outPlacements[outIdx].petalLengthScale = 0.92f + Nature_Random01(&rng) * 0.23f;
        }
    }
}

float MapFlora_EvaluateFlowerUnderstorey(
    float x, float z,
    const MapFloraFlowerCluster *clusters,
    int clusterCount)
{
    if (!clusters || clusterCount <= 0) return 1.0f;
    float understorey = 1.0f;
    for (int c = 0; c < clusterCount; c++) {
        float rx = (clusters[c].radius.x > 0.1f) ? clusters[c].radius.x : 10.0f;
        float rz = (clusters[c].radius.y > 0.1f) ? clusters[c].radius.y : rx;
        float dx = (x - clusters[c].center.x) / rx;
        float dz = (z - clusters[c].center.z) / rz;
        float blend = fminf(1.0f, fmaxf(0.0f, (dx * dx + dz * dz - 0.30f) / 0.95f));
        understorey = fminf(understorey, 0.58f + 0.42f * blend * blend * (3.0f - 2.0f * blend));
    }
    return understorey;
}

float MapFlora_EvaluateFlowerSuppression(
    float x, float z,
    const MapFloraFlowerCluster *clusters,
    int clusterCount)
{
    if (!clusters || clusterCount <= 0) return 1.0f;
    float flowerSuppression = 1.0f;
    for (int c = 0; c < clusterCount; c++) {
        float rx = (clusters[c].radius.x > 0.1f) ? clusters[c].radius.x : 10.0f;
        float rz = (clusters[c].radius.y > 0.1f) ? clusters[c].radius.y : rx;
        float dx = (x - clusters[c].center.x) / rx;
        float dz = (z - clusters[c].center.z) / rz;
        float d2 = dx * dx + dz * dz;
        if (d2 < 1.25f) {
            float localFactor = fmaxf(0.82f, (d2 - 0.30f) / 0.95f);
            flowerSuppression = fminf(flowerSuppression, localFactor);
        }
    }
    return flowerSuppression;
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
                .shadowDistance = (style.shadowDistance > 0.0f) ? style.shadowDistance : 16.0f,
                .texturePath = NULL,
                .botanicalVariation = 1.0f,
                .growthForm = MAP_MEADOW_GROWTH_GRASS,
            });
        flora.valleyGrassCount = grassCount;
    }

    // 2. Shoreline & River Reeds (No plumes by default to prevent flat white cards)
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
                .shadowDistance = (style.shadowDistance > 0.0f) ? style.shadowDistance : 14.0f,
                .texturePath = NULL,
                .hasPlumes = false, // Strictly false: reeds are elegant curved blades, no white paper rectangles
                .growthForm = MAP_MEADOW_GROWTH_REED,
            });
        flora.reedCount = reedCount;
    }

    // 3. Clustered Wildflower Fields: 100% Procedural 3D Petals (NO texture atlas!)
    if (flowerPlacements && clusters && clusterCount > 0) {
        flora.flowerFields = (MapFlowerField *)RL_MALLOC(clusterCount * sizeof(MapFlowerField));
        flora.flowerClusterCount = clusterCount;

        int placementOffset = 0;
        for (int i = 0; i < clusterCount; i++) {
            const MapFloraFlowerCluster *cl = &clusters[i];
            int count = cl->flowerCount;
            if (count <= 0) {
                flora.flowerFields[i] = (MapFlowerField){0};
                continue;
            }

            Color stemColor = (Color){91, 120, 65, 255};
            Color centerColor = (cl->discalCenterColor.a > 0) ? cl->discalCenterColor
                : MapProp_GetFlowerSpeciesColor(cl->species[0], true);

            // Passing NULL as petalTexturePath enables high-fidelity 3D procedural petals
            flora.flowerFields[i] = MapProp_CreateFlowerField(
                &flowerPlacements[placementOffset], count,
                stemColor, centerColor,
                NULL, 0.0f, 1, 1);

            MapProp_SetFlowerFieldLod(&flora.flowerFields[i], 34.0f, 30.0f);
            MapProp_SetFlowerFieldDrawDistance(&flora.flowerFields[i], 78.0f);
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
