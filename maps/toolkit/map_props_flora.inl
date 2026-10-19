// Generalized Flora System (Meadows, Reeds, Wildflower Colonies)
// Included into map_props.c — not a standalone compilation unit.

Color MapProp_GetFlowerSpeciesColor(int variant, bool accent)
{
    // Painterly high-luminance Xianxia & Tsushima botanical palette
    static const Color primary[8] = {
        {255, 252, 240, 255}, // 0: ivory white daisy (Bạch Cúc)
        {255, 218, 56, 255},  // 1: warm golden buttercup (Hoàng Anh)
        {244, 68, 52, 255},   // 2: vermilion crimson poppy (Chu Sa Hồng)
        {104, 168, 252, 255}, // 3: heavenly sapphire cornflower (Lam Điệp)
        {252, 154, 186, 255}, // 4: wild peach-rose pink (Đào Hoa)
        {224, 102, 174, 255}, // 5: orchid magenta cosmos (Tử Mẫu)
        {174, 142, 244, 255}, // 6: gentle lavender violet (Huân Y Thảo)
        {255, 242, 172, 255}, // 7: pale primrose cream (Ngọc Lan)
    };
    static const Color secondary[8] = {
        {255, 244, 220, 255},
        {255, 190, 32, 255},
        {214, 42, 40, 255},
        {76, 136, 236, 255},
        {244, 120, 158, 255},
        {192, 68, 144, 255},
        {146, 110, 228, 255},
        {255, 224, 136, 255},
    };
    variant &= 7;
    return accent ? secondary[variant] : primary[variant];
}

// Helper: Computes the exact 3 organic colony epicenters in normalized cluster space
// so flower placement, grass understorey depression, and grass density suppression align 100%.
static void MapFlora_GetClusterEpicentersNorm(int clusterIndex, Vector2 outEpicenters[3])
{
    float clusterSeedAngle = (float)clusterIndex * 1.047f + 0.35f;
    for (int e = 0; e < 3; e++)
    {
        float epAngle = clusterSeedAngle + (float)e * 2.094f + 0.35f * sinf((float)(clusterIndex * 7 + e * 3));
        float epDist = 0.28f + 0.16f * cosf((float)(clusterIndex * 3 + e * 5));
        outEpicenters[e] = (Vector2){cosf(epAngle) * epDist, sinf(epAngle) * epDist};
    }
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

    for (int c = 0; c < clusterCount && placementCursor < totalFlowerCount; c++)
    {
        const MapFloraFlowerCluster *cl = &clusters[c];
        int count = cl->flowerCount;
        if (count <= 0)
            continue;
        if (placementCursor + count > totalFlowerCount)
            count = totalFlowerCount - placementCursor;

        int clusterStartIndex = placementCursor;
        float rx = (cl->radius.x > 0.1f) ? cl->radius.x : 10.0f;
        float rz = (cl->radius.y > 0.1f) ? cl->radius.y : rx;

        // 3 organic colony epicenters per cluster along landscape flow
        Vector2 epicenters[3];
        MapFlora_GetClusterEpicentersNorm(c, epicenters);

        float flowAngle = 0.785f + 0.30f * sinf(cl->center.x * 0.12f + (float)c);
        float cosFlow = cosf(flowAngle);
        float sinFlow = sinf(flowAngle);

        for (int i = 0; i < count; i++)
        {
            int localIndex = i;
            int colony = localIndex % 3;
            if (Nature_Random01(&rng) < 0.18f)
                colony = (colony + 1) % 3;
            // Cohesive painterly drifts (88% clustered in ribbons, 12% soft satellite blooms)
            bool isColony = Nature_Random01(&rng) < 0.88f;

            float x = cl->center.x;
            float z = cl->center.z;
            float radialNorm = 0.5f;
            for (int attempt = 0; attempt < 24; attempt++)
            {
                float angle = Nature_Random01(&rng) * (2.0f * PI);
                float uRand = Nature_Random01(&rng);
                float rVal = uRand * sqrtf(uRand); // Fast & exact replacement for powf(u, 1.5)
                radialNorm = rVal;
                if (isColony)
                {
                    float along = cosf(angle) * rVal * 0.38f;
                    float across = sinf(angle) * rVal * 0.20f;
                    float dxNorm = epicenters[colony].x + (cosFlow * along - sinFlow * across);
                    float dzNorm = epicenters[colony].y + (sinFlow * along + cosFlow * across);
                    x = cl->center.x + dxNorm * rx;
                    z = cl->center.z + dzNorm * rz;
                }
                else
                {
                    x = cl->center.x + cosf(angle) * rx * (0.25f + 0.55f * rVal);
                    z = cl->center.z + sinf(angle) * rz * (0.25f + 0.55f * rVal);
                }

                if (isExcluded && isExcluded(x, z, ctx))
                    continue;

                // Poisson minimum distance enforcement
                bool tooClose = false;
                for (int prev = clusterStartIndex; prev < placementCursor; prev++)
                {
                    float pdx = x - outPlacements[prev].position.x;
                    float pdz = z - outPlacements[prev].position.z;
                    if (pdx * pdx + pdz * pdz < 0.062f * 0.062f)
                    {
                        tooClose = true;
                        break;
                    }
                }
                if (!tooClose || attempt >= 23)
                    break;
            }

            float gy = sampleHeight ? sampleHeight(x, z, ctx) : 0.0f;
            int outIdx = placementCursor++;
            outPlacements[outIdx].position = (Vector3){x, gy + 0.018f, z};
            outPlacements[outIdx].rotationDeg = Nature_Random01(&rng) * 360.0f;
            outPlacements[outIdx].phase = Nature_Random01(&rng);

            int variant;
            if (isColony)
            {
                variant = cl->species[colony];
                if (Nature_Random01(&rng) < 0.15f)
                    variant = cl->species[3];
            }
            else
            {
                variant = cl->species[(int)(Nature_Random01(&rng) * 4.0f) % 4];
            }

            bool tallAccent = (variant == 2 || variant == 4 || variant == 6);
            // Proud hero posture in colony core, tapering gracefully toward drift edges
            float coreLift = isColony ? (1.0f - 0.22f * radialNorm) : 0.85f;
            float driftHeightBase = tallAccent ? 0.60f : 0.52f;
            outPlacements[outIdx].height = (driftHeightBase + (Nature_Random01(&rng) * 0.10f - 0.02f)) * coreLift;

            // Fuller 3D bloom silhouettes so flowers read as lush drifts at both close and wide zoom
            float baseBloomRadius = tallAccent ? 0.148f : 0.122f;
            outPlacements[outIdx].bloomRadius = baseBloomRadius * (0.94f + Nature_Random01(&rng) * 0.24f) * (0.90f + 0.15f * coreLift);
            outPlacements[outIdx].petalColor = MapProp_GetFlowerSpeciesColor(variant, Nature_Random01(&rng) > 0.78f);
            outPlacements[outIdx].petalCount = (unsigned char)(5 + (variant % 3));
            outPlacements[outIdx].bloomVariant = (unsigned char)variant;
            outPlacements[outIdx].petalLengthScale = 1.02f + Nature_Random01(&rng) * 0.24f;
        }
    }
}

float MapFlora_EvaluateFlowerUnderstorey(
    float x, float z,
    const MapFloraFlowerCluster *clusters,
    int clusterCount)
{
    if (!clusters || clusterCount <= 0)
        return 1.0f;
    float understorey = 1.0f;
    for (int c = 0; c < clusterCount; c++)
    {
        float rx = (clusters[c].radius.x > 0.1f) ? clusters[c].radius.x : 10.0f;
        float rz = (clusters[c].radius.y > 0.1f) ? clusters[c].radius.y : rx;
        float dx = (x - clusters[c].center.x) / rx;
        float dz = (z - clusters[c].center.z) / rz;
        float centerD2 = dx * dx + dz * dz;
        if (centerD2 > 1.65f)
            continue;

        Vector2 epicenters[3];
        MapFlora_GetClusterEpicentersNorm(c, epicenters);
        float minColonyD2 = centerD2 * 0.85f;
        for (int e = 0; e < 3; e++)
        {
            float edx = dx - epicenters[e].x;
            float edz = dz - epicenters[e].y;
            float ed2 = edx * edx + edz * edz;
            if (ed2 < minColonyD2)
                minColonyD2 = ed2;
        }

        // Depress grass height down to 42% right under the flower colony epicenters
        // so 0.52m..0.66m blooms rise proudly above a soft 0.22m..0.28m grass bed.
        if (minColonyD2 < 0.48f)
        {
            float blend = fminf(1.0f, fmaxf(0.0f, (minColonyD2 - 0.06f) / 0.42f));
            float smooth = blend * blend * (3.0f - 2.0f * blend);
            understorey = fminf(understorey, 0.42f + 0.58f * smooth);
        }
    }
    return understorey;
}

float MapFlora_EvaluateFlowerSuppression(
    float x, float z,
    const MapFloraFlowerCluster *clusters,
    int clusterCount)
{
    if (!clusters || clusterCount <= 0)
        return 1.0f;
    float flowerSuppression = 1.0f;
    for (int c = 0; c < clusterCount; c++)
    {
        float rx = (clusters[c].radius.x > 0.1f) ? clusters[c].radius.x : 10.0f;
        float rz = (clusters[c].radius.y > 0.1f) ? clusters[c].radius.y : rx;
        float dx = (x - clusters[c].center.x) / rx;
        float dz = (z - clusters[c].center.z) / rz;
        float centerD2 = dx * dx + dz * dz;
        if (centerD2 > 1.65f)
            continue;

        Vector2 epicenters[3];
        MapFlora_GetClusterEpicentersNorm(c, epicenters);
        float minColonyD2 = centerD2 * 0.85f;
        for (int e = 0; e < 3; e++)
        {
            float edx = dx - epicenters[e].x;
            float edz = dz - epicenters[e].y;
            float ed2 = edx * edx + edz * edz;
            if (ed2 < minColonyD2)
                minColonyD2 = ed2;
        }

        // Thin grass tuft density to 48% inside flower colony cores so petals aren't pierced by blades
        if (minColonyD2 < 0.45f)
        {
            float blend = fminf(1.0f, fmaxf(0.0f, (minColonyD2 - 0.05f) / 0.40f));
            float smooth = blend * blend * (3.0f - 2.0f * blend);
            flowerSuppression = fminf(flowerSuppression, 0.48f + 0.52f * smooth);
        }
    }
    return flowerSuppression;
}

int MapFlora_GenerateMeadowPlacements(
    MapMeadowPlacement *outPlacements,
    int maxCount,
    const MapGroundSurface *ground,
    Vector3 groundCenter,
    MapFloraMeadowConfig config)
{
    if (!outPlacements || maxCount <= 0 || !ground)
        return 0;

    float spacing = (config.spacing > 0.0f) ? config.spacing : 0.24f;
    unsigned int seed = config.seed ? config.seed : 0x51a7c3u;

    int count = MapProp_GenerateMeadowPlacements(
        outPlacements, maxCount, ground, groundCenter,
        (MapMeadowDistribution){
            .minBounds = config.minBounds,
            .maxBounds = config.maxBounds,
            .spacing = spacing,
            .jitter = 0.90f,
            .minRadius = 0.21f,
            .maxRadius = 0.28f,
            .minHeight = 0.36f,
            .maxHeight = 0.58f,
            .yOffset = 0.018f,
            .seed = seed,
        },
        config.densityFn, config.densityUserData);

    for (int i = 0; i < count; i++)
    {
        MapMeadowPlacement *clump = &outPlacements[i];
        float cx = clump->position.x;
        float cz = clump->position.z;

        // Unified 3-wave landscape streamline orientation + full 240-degree yaw jitter
        // so neighboring tufts interlock in 360 degrees instead of combing into flat rows
        float wave1 = sinf(cx * 0.07f + cz * 0.05f) * 0.60f + sinf(cx * -0.05f + cz * 0.11f + 1.2f) * 0.40f;
        float eddy = sinf(cx * 0.22f - cz * 0.18f + 0.8f) * 0.55f + sinf(cx * 0.14f + cz * 0.26f + 2.3f) * 0.45f;
        float micro = sinf(cx * 0.55f + cz * 0.45f + 3.1f) * 0.50f + sinf(cx * -0.42f + cz * 0.62f) * 0.50f;
        float flowAngle = 45.0f + wave1 * 32.0f + eddy * 24.0f + micro * 16.0f;

        float hash = sinf((float)(i * 47) + cx * 12.9898f + cz * 78.233f) * 43758.5453f;
        hash -= floorf(hash);
        clump->rotationDeg = flowAngle + (hash - 0.5f) * 240.0f;

        float localHeight = clump->height;
        float localRadius = clump->radius;

        float habitatAnalytic = MeadowHabitat(cx, cz);
        float biome = habitatAnalytic;
        float growth = habitatAnalytic;
        float coverage = 1.0f;
        if (config.ecology && config.ecology->ready)
        {
            MapEcologySample sample = MapEcology_Sample(config.ecology, cx, cz);
            if (sample.coverage > 0.01f || sample.growth > 0.01f || sample.habitat > 0.01f)
            {
                biome = sample.habitat;
                growth = sample.growth;
                coverage = sample.coverage;
            }
        }

        // Organic billowing meadow rhythm: alternates lush tall drifts with softer low-turf pockets
        float canopyWave = 0.86f + 0.18f * (wave1 * 0.65f + eddy * 0.35f);
        float heightNorm = (localHeight - 0.36f) / 0.22f;
        clump->height = (0.36f + heightNorm * 0.18f) * (0.88f + 0.22f * growth) * canopyWave;
        clump->height *= 0.80f + 0.20f * coverage;

        if (config.flowerClusters && config.flowerClusterCount > 0)
        {
            float understorey = MapFlora_EvaluateFlowerUnderstorey(
                cx, cz, config.flowerClusters, config.flowerClusterCount);
            clump->height *= understorey;
        }

        // Refined tuft collar radius (0.21m..0.28m) prevents overly wide/spiky blade spread
        clump->radius = 0.21f + biome * 0.06f;
        clump->radius *= 0.92f + 0.16f * (localRadius - 0.21f) / 0.07f;

        if (config.edgeTaperFn)
        {
            float taper = config.edgeTaperFn(cx, cz, config.edgeTaperUserData);
            taper = fminf(1.0f, fmaxf(0.48f, taper));
            clump->height *= taper;
            clump->radius *= (1.16f - 0.16f * taper);
        }
    }

    return count;
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

    // 1. Valley / Meadow Grass (Smooth 4-segment Bézier clumps with unified botanical palette)
    if (grassPlacements && grassCount > 0)
    {
        float lodDist = (style.lodDistance > 0.0f) ? style.lodDistance : 26.0f;
        float midDist = (style.midLodDistance > 0.0f) ? style.midLodDistance : (lodDist * 0.54f);
        flora.valleyMeadow = MapProp_CreateMeadow(grassPlacements, grassCount,
                                                  (MapMeadowStyle){
                                                      .rootColor = (style.grassRootColor.a > 0) ? style.grassRootColor : (Color){28, 46, 20, 255},
                                                      .tipColor = (style.grassTipColor.a > 0) ? style.grassTipColor : (Color){114, 172, 58, 255},
                                                      .bladesPerClump = (style.bladesPerClump > 0) ? style.bladesPerClump : 6,
                                                      .bladeSegments = (style.bladeSegments > 0) ? style.bladeSegments : 4,
                                                      .bladeWidthScale = (style.bladeWidthScale > 0.0f) ? style.bladeWidthScale : 0.14f,
                                                      .chunkSize = 12.0f,
                                                      .lodDistance = lodDist,
                                                      .midLodDistance = midDist,
                                                      .drawDistance = (style.drawDistance > 0.0f) ? style.drawDistance : 60.0f,
                                                      .shadowDistance = (style.shadowDistance < 0.0f) ? 0.0f : ((style.shadowDistance > 0.0f) ? style.shadowDistance : 11.0f),
                                                      .texturePath = NULL,
                                                      .botanicalVariation = style.botanicalVariation,
                                                      .growthForm = MAP_MEADOW_GROWTH_GRASS,
                                                      .grassState = style.grassState,
                                                      .dryness = style.dryness,
                                                      .scorch = style.scorch,
                                                  });
        flora.valleyGrassCount = grassCount;
    }

    // 2. Shoreline & River Reeds (No plumes by default to prevent flat white cards)
    if (reedPlacements && reedCount > 0)
    {
        flora.reedMeadow = MapProp_CreateMeadow(reedPlacements, reedCount,
                                                (MapMeadowStyle){
                                                    .rootColor = (style.reedRootColor.a > 0) ? style.reedRootColor : (Color){28, 46, 22, 255},
                                                    .tipColor = (style.reedTipColor.a > 0) ? style.reedTipColor : (Color){126, 168, 76, 255},
                                                    .bladesPerClump = 6,
                                                    .bladeSegments = 4,
                                                    .bladeWidthScale = 0.135f,
                                                    .chunkSize = 18.0f,
                                                    .lodDistance = 28.0f,
                                                    .midLodDistance = 15.0f,
                                                    .drawDistance = 58.0f,
                                                    .shadowDistance = (style.shadowDistance > 0.0f) ? fminf(style.shadowDistance, 10.5f) : 10.5f,
                                                    .texturePath = NULL,
                                                    .hasPlumes = false,
                                                    .growthForm = MAP_MEADOW_GROWTH_REED,
                                                });
        flora.reedCount = reedCount;
    }

    // 3. Clustered Wildflower Fields: 100% Procedural 3D Petals (NO texture atlas!)
    if (flowerPlacements && clusters && clusterCount > 0)
    {
        flora.flowerFields = (MapFlowerField *)RL_MALLOC(clusterCount * sizeof(MapFlowerField));
        flora.flowerClusterCount = clusterCount;

        int placementOffset = 0;
        for (int i = 0; i < clusterCount; i++)
        {
            const MapFloraFlowerCluster *cl = &clusters[i];
            int count = cl->flowerCount;
            if (count <= 0)
            {
                flora.flowerFields[i] = (MapFlowerField){0};
                continue;
            }

            Color stemColor = (Color){82, 114, 54, 255};
            Color centerColor = (cl->discalCenterColor.a > 0) ? cl->discalCenterColor
                                                              : (Color){255, 208, 46, 255};

            // Passing NULL as petalTexturePath enables high-fidelity 3D procedural petals
            flora.flowerFields[i] = MapProp_CreateFlowerField(
                &flowerPlacements[placementOffset], count,
                stemColor, centerColor,
                NULL, 0.0f, 1, 1);

            // Tuned LOD & contact-shadow distances to eliminate distant sub-pixel overdraw
            MapProp_SetFlowerFieldLod(&flora.flowerFields[i], 22.0f, 10.5f);
            MapProp_SetFlowerFieldDrawDistance(&flora.flowerFields[i], 56.0f);
            placementOffset += count;
        }
    }

    flora.time = 0.0f;
    flora.ready = (flora.valleyMeadow.ready || flora.reedMeadow.ready || flora.flowerClusterCount > 0);
    return flora;
}

void MapProp_UpdateFloraSystem(MapFloraSystem *flora, Vector3 cameraTarget, float dt)
{
    if (!flora || !flora->ready)
        return;
    flora->time += dt;

    if (flora->valleyGrassCount > 0)
    {
        MapProp_PrepareMeadow(&flora->valleyMeadow, (Vector3){0});
    }
    if (flora->reedCount > 0)
    {
        MapProp_PrepareMeadow(&flora->reedMeadow, (Vector3){0});
    }

    // Only run default interaction pass if the map hasn't already opened a custom interaction frame
    if (!s_natureInteractionOpen)
    {
        MapProp_BeginNatureInteraction(cameraTarget, dt);
        MapProp_AddNatureInteractor(cameraTarget, 1.25f, 0.34f);
        MapProp_AddNatureWindVorticles(flora->time);
        MapProp_EndNatureInteraction();
    }
}

void MapProp_DrawFloraSystem(MapFloraSystem *flora)
{
    if (!flora || !flora->ready)
        return;

    if (flora->valleyGrassCount > 0)
    {
        MapProp_DrawMeadow(&flora->valleyMeadow, (Vector3){0}, flora->time,
                           flora->windDirection, flora->windStrength);
    }
    if (flora->reedCount > 0)
    {
        MapProp_DrawMeadow(&flora->reedMeadow, (Vector3){0}, flora->time,
                           flora->windDirection, 0.11f);
    }
    for (int i = 0; i < flora->flowerClusterCount; i++)
    {
        if (flora->flowerFields[i].ready)
        {
            MapProp_DrawFlowerField(&flora->flowerFields[i], (Vector3){0}, flora->time,
                                    flora->windDirection, 0.032f);
        }
    }
}

void MapProp_DrawFloraShadowCasters(MapFloraSystem *flora)
{
    if (!flora || !flora->ready)
        return;

    Vector3 offset = {0};
    if (flora->valleyGrassCount > 0)
    {
        MapProp_DrawMeadowShadowCasters(&flora->valleyMeadow, offset, flora->time,
                                        flora->windDirection, flora->windStrength);
    }
    if (flora->reedCount > 0)
    {
        MapProp_DrawMeadowShadowCasters(&flora->reedMeadow, offset, flora->time,
                                        flora->windDirection, 0.11f);
    }

    // Optimization: 3D procedural flowers sit nested inside grass and already draw batched
    // contact shadows. Only rasterize flower geometry into the R32F directional shadow map
    // when explicitly requested by diagnostic filter WUXING_NATURE_SHADOW_CASTERS.
    const char *casterFilter = getenv("WUXING_NATURE_SHADOW_CASTERS");
    if (casterFilter && casterFilter[0] != '\0')
    {
        for (int i = 0; i < flora->flowerClusterCount; i++)
        {
            if (flora->flowerFields[i].ready)
            {
                MapProp_DrawFlowerFieldShadowCaster(&flora->flowerFields[i], offset, flora->time,
                                                    flora->windDirection, 0.032f);
            }
        }
    }
}

void MapProp_UnloadFloraSystem(MapFloraSystem *flora)
{
    if (!flora || !flora->ready)
        return;

    for (int i = 0; i < flora->flowerClusterCount; i++)
    {
        if (flora->flowerFields[i].ready)
        {
            MapProp_UnloadFlowerField(&flora->flowerFields[i]);
        }
    }
    if (flora->flowerFields)
    {
        RL_FREE(flora->flowerFields);
        flora->flowerFields = NULL;
    }
    flora->flowerClusterCount = 0;

    if (flora->reedCount > 0)
    {
        MapProp_UnloadMeadow(&flora->reedMeadow);
        flora->reedCount = 0;
    }
    if (flora->valleyGrassCount > 0)
    {
        MapProp_UnloadMeadow(&flora->valleyMeadow);
        flora->valleyGrassCount = 0;
    }
    flora->ready = false;
}