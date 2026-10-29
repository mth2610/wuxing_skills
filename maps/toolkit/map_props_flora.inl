// Generalized Flora System (Meadows, Reeds, Wildflower Colonies)
// Included into map_props.c — not a standalone compilation unit.

Color MapProp_GetFlowerSpeciesColor(int variant, bool accent)
{
    // Painterly Ghost of Tsushima & Where Winds Meet botanical palette:
    // Balanced chroma so petals glow naturally under HDR sun without clipping into flat white cones
    static const Color primary[8] = {
        {248, 242, 224, 255}, // 0: warm ivory daisy (Bạch Cúc)
        {250, 204, 48, 255},  // 1: golden amber buttercup (Hoàng Anh)
        {232, 62, 48, 255},   // 2: vermilion crimson poppy (Chu Sa Hồng)
        {96, 158, 242, 255},  // 3: cerulean sapphire cornflower (Lam Điệp)
        {244, 146, 176, 255}, // 4: wild peach-rose blossom (Đào Hoa)
        {214, 96, 164, 255},  // 5: orchid magenta cosmos (Tử Mẫu)
        {164, 134, 234, 255}, // 6: misty lavender violet (Huân Y Thảo)
        {248, 232, 158, 255}, // 7: soft primrose cream (Ngọc Lan)
    };
    static const Color secondary[8] = {
        {238, 228, 202, 255},
        {242, 176, 28, 255},
        {204, 38, 36, 255},
        {72, 128, 224, 255},
        {232, 114, 148, 255},
        {184, 64, 136, 255},
        {138, 104, 216, 255},
        {242, 212, 124, 255},
    };
    variant &= 7;
    return accent ? secondary[variant] : primary[variant];
}

// ============================================================================
// CACHED CLUSTER EPICENTERS (Loại bỏ hàng triệu lệnh sinf/cosf khi sinh 98.000 bụi cỏ)
// ============================================================================
#define FLORA_MAX_CACHED_CLUSTERS 64
static Vector2 s_cachedClusterEpicenters[FLORA_MAX_CACHED_CLUSTERS][3];
static bool s_clusterEpicentersInitialized = false;

static void MapFlora_EnsureEpicenterCache(void)
{
    if (s_clusterEpicentersInitialized)
        return;
    s_clusterEpicentersInitialized = true;
    for (int c = 0; c < FLORA_MAX_CACHED_CLUSTERS; c++)
    {
        float clusterSeedAngle = (float)c * 1.047f + 0.35f;
        for (int e = 0; e < 3; e++)
        {
            float epAngle = clusterSeedAngle + (float)e * 2.094f + 0.35f * sinf((float)(c * 7 + e * 3));
            float epDist = 0.28f + 0.16f * cosf((float)(c * 3 + e * 5));
            s_cachedClusterEpicenters[c][e] = (Vector2){cosf(epAngle) * epDist, sinf(epAngle) * epDist};
        }
    }
}

// Helper: Computes the exact 3 organic colony epicenters in normalized cluster space
// so flower placement, grass understorey depression, and grass density suppression align 100%.
static inline void MapFlora_GetClusterEpicentersNorm(int clusterIndex, Vector2 outEpicenters[3])
{
    MapFlora_EnsureEpicenterCache();
    if (clusterIndex >= 0 && clusterIndex < FLORA_MAX_CACHED_CLUSTERS)
    {
        outEpicenters[0] = s_cachedClusterEpicenters[clusterIndex][0];
        outEpicenters[1] = s_cachedClusterEpicenters[clusterIndex][1];
        outEpicenters[2] = s_cachedClusterEpicenters[clusterIndex][2];
        return;
    }
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

    MapFlora_EnsureEpicenterCache();
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
                float rVal = uRand * sqrtf(uRand);
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

                // [TỐI ƯU POISSON]: Kiểm tra nhanh theo từng trục trước khi tính bình phương khoảng cách
                bool tooClose = false;
                int searchStart = (placementCursor - clusterStartIndex > 512)
                                      ? (placementCursor - 512)
                                      : clusterStartIndex;
                for (int prev = placementCursor - 1; prev >= searchStart; prev--)
                {
                    float pdx = x - outPlacements[prev].position.x;
                    if (pdx >= 0.068f || pdx <= -0.068f)
                        continue;
                    float pdz = z - outPlacements[prev].position.z;
                    if (pdz >= 0.068f || pdz <= -0.068f)
                        continue;
                    if (pdx * pdx + pdz * pdz < 0.068f * 0.068f)
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
            outPlacements[outIdx].position = (Vector3){x, gy + 0.015f, z};
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
            // AAA Wildflower Height: elevated above the grass understorey cushion (0.44m - 0.56m)
            // so blooms catch full sunlight and never get drowned or pierced by grass stalks.
            float coreLift = isColony ? (1.0f - 0.12f * radialNorm) : 0.88f;
            float driftHeightBase = tallAccent ? 0.52f : 0.45f;
            outPlacements[outIdx].height = (driftHeightBase + (Nature_Random01(&rng) * 0.08f - 0.03f)) * coreLift;

            // Painterly bloom proportions with rich surface presence across all camera zoom levels
            float baseBloomRadius = tallAccent ? 0.098f : 0.086f;
            outPlacements[outIdx].bloomRadius = baseBloomRadius * (0.92f + Nature_Random01(&rng) * 0.18f) * (0.90f + 0.12f * coreLift);
            outPlacements[outIdx].petalColor = MapProp_GetFlowerSpeciesColor(variant, Nature_Random01(&rng) > 0.74f);
            outPlacements[outIdx].petalCount = (unsigned char)(5 + (variant % 3));
            outPlacements[outIdx].bloomVariant = (unsigned char)variant;
            outPlacements[outIdx].petalLengthScale = 0.98f + Nature_Random01(&rng) * 0.22f;
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

    MapFlora_EnsureEpicenterCache();
    float minScale = 1.0f;
    for (int c = 0; c < clusterCount; c++)
    {
        const MapFloraFlowerCluster *cl = &clusters[c];
        float rx = (cl->radius.x > 0.1f) ? cl->radius.x : 10.0f;
        float rz = (cl->radius.y > 0.1f) ? cl->radius.y : rx;
        float rawDx = x - cl->center.x;
        if (rawDx > rx * 1.2f || rawDx < -rx * 1.2f)
            continue;
        float rawDz = z - cl->center.z;
        if (rawDz > rz * 1.2f || rawDz < -rz * 1.2f)
            continue;

        float dx = rawDx / rx;
        float dz = rawDz / rz;
        float distSq = dx * dx + dz * dz;
        if (distSq > 1.44f) // outside 1.2x cluster radius
            continue;

        Vector2 epicenters[3];
        MapFlora_GetClusterEpicentersNorm(c, epicenters);

        float minEpiDistSq = 999.0f;
        for (int e = 0; e < 3; e++)
        {
            float edx = dx - epicenters[e].x;
            float edz = dz - epicenters[e].y;
            float d2 = edx * edx + edz * edz;
            if (d2 < minEpiDistSq)
                minEpiDistSq = d2;
        }

        if (minEpiDistSq >= 0.45f * 0.45f)
            continue;

        float epiDist = sqrtf(minEpiDistSq);
        // Core understorey cushion: scale grass height down to 0.42x (from ~0.52m to ~0.22m)
        // Transition back to 1.0x at distance 0.45 in normalized cluster space
        float weight = 1.0f - MapMath_SmoothStep01(0.04f, 0.45f, epiDist);
        float scale = 1.0f - 0.58f * weight; // 1.0 -> 0.42
        if (scale < minScale)
            minScale = scale;
    }
    return minScale;
}

float MapFlora_EvaluateFlowerSuppression(
    float x, float z,
    const MapFloraFlowerCluster *clusters,
    int clusterCount)
{
    if (!clusters || clusterCount <= 0)
        return 1.0f;

    MapFlora_EnsureEpicenterCache();
    float minDensity = 1.0f;
    for (int c = 0; c < clusterCount; c++)
    {
        const MapFloraFlowerCluster *cl = &clusters[c];
        float rx = (cl->radius.x > 0.1f) ? cl->radius.x : 10.0f;
        float rz = (cl->radius.y > 0.1f) ? cl->radius.y : rx;
        float rawDx = x - cl->center.x;
        if (rawDx > rx * 1.2f || rawDx < -rx * 1.2f)
            continue;
        float rawDz = z - cl->center.z;
        if (rawDz > rz * 1.2f || rawDz < -rz * 1.2f)
            continue;

        float dx = rawDx / rx;
        float dz = rawDz / rz;
        float distSq = dx * dx + dz * dz;
        if (distSq > 1.44f)
            continue;

        Vector2 epicenters[3];
        MapFlora_GetClusterEpicentersNorm(c, epicenters);

        float minEpiDistSq = 999.0f;
        for (int e = 0; e < 3; e++)
        {
            float edx = dx - epicenters[e].x;
            float edz = dz - epicenters[e].y;
            float d2 = edx * edx + edz * edz;
            if (d2 < minEpiDistSq)
                minEpiDistSq = d2;
        }

        if (minEpiDistSq >= 0.38f * 0.38f)
            continue;

        float epiDist = sqrtf(minEpiDistSq);
        // In the dense center of flower colonies, thin grass tufts by ~52% (density 0.48)
        // so blades have natural breathing room and do not crowd flower blossoms
        float weight = 1.0f - MapMath_SmoothStep01(0.02f, 0.38f, epiDist);
        float density = 1.0f - 0.52f * weight;
        if (density < minDensity)
            minDensity = density;
    }
    return minDensity;
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

    float spacing = (config.spacing > 0.0f) ? config.spacing : 0.26f;
    unsigned int seed = config.seed ? config.seed : 0x51a7c3u;

    int count = MapProp_GenerateMeadowPlacements(
        outPlacements, maxCount, ground, groundCenter,
        (MapMeadowDistribution){
            .minBounds = config.minBounds,
            .maxBounds = config.maxBounds,
            .spacing = spacing,
            .jitter = 0.92f,
            .minRadius = 0.24f,
            .maxRadius = 0.32f,
            .minHeight = 0.34f,
            .maxHeight = 0.54f,
            .yOffset = 0.016f,
            .seed = seed,
        },
        config.densityFn, config.densityUserData);

    for (int i = 0; i < count; i++)
    {
        MapMeadowPlacement *clump = &outPlacements[i];
        float cx = clump->position.x;
        float cz = clump->position.z;

        // Ghost of Tsushima Voronoi Patch Clumping + Wind Streamline Orientation:
        // Partition the meadow into ~2.1m Voronoi patches so neighboring tufts share a coherent
        // wind-combed facing, bend posture, and high/low grass tier instead of 240-deg spiky chaos.
        const float voronoiScale = 0.48f; // ~2.08m patch size
        float gx = cx * voronoiScale;
        float gz = cz * voronoiScale;
        int cellX = (int)floorf(gx);
        int cellZ = (int)floorf(gz);
        float bestDistSq = 99.0f;
        float secondDistSq = 99.0f;
        float patchHash = 0.5f;
        float patchHash2 = 0.5f;
        for (int dz = -1; dz <= 1; dz++)
        {
            for (int dx = -1; dx <= 1; dx++)
            {
                int nx = cellX + dx;
                int nz = cellZ + dz;
                unsigned int ch = (unsigned int)(nx * 73856093) ^ (unsigned int)(nz * 19349663) ^ seed;
                ch ^= ch >> 16;
                ch *= 0x7feb352du;
                ch ^= ch >> 15;
                float jx = (float)(ch & 0xffffu) * (1.0f / 65535.0f);
                float jz = (float)((ch >> 16) & 0xffffu) * (1.0f / 65535.0f);
                float px = ((float)nx + 0.15f + 0.70f * jx) - gx;
                float pz = ((float)nz + 0.15f + 0.70f * jz) - gz;
                float d2 = px * px + pz * pz;
                if (d2 < bestDistSq)
                {
                    secondDistSq = bestDistSq;
                    bestDistSq = d2;
                    patchHash = jx;
                    patchHash2 = jz;
                }
                else if (d2 < secondDistSq)
                {
                    secondDistSq = d2;
                }
            }
        }
        // Smooth edge factor (0 at patch border, 1 at Voronoi clump core)
        float edgeDist = sqrtf(fmaxf(0.0f, secondDistSq)) - sqrtf(fmaxf(0.0f, bestDistSq));
        float clumpCore = fminf(1.0f, edgeDist * 1.65f);

        float wave1 = sinf(cx * 0.07f + cz * 0.05f) * 0.60f + sinf(cx * -0.05f + cz * 0.11f + 1.2f) * 0.40f;
        float eddy = sinf(cx * 0.20f - cz * 0.16f + 0.8f) * 0.55f + sinf(cx * 0.12f + cz * 0.22f + 2.3f) * 0.45f;
        float patchAngleOffset = (patchHash - 0.5f) * 28.0f * (0.45f + 0.55f * clumpCore);
        // Multi-frequency wind deflection across hill contours and swales
        float macroSwirl = sinf(cx * 0.026f + cz * 0.018f) * 32.0f + cosf(cz * 0.038f - cx * 0.022f) * 20.0f;
        float flowAngle = -16.0f + macroSwirl + wave1 * 22.0f + eddy * 14.0f + patchAngleOffset;

        float hash = sinf((float)(i * 47) + cx * 12.9898f + cz * 78.233f) * 43758.5453f;
        hash -= floorf(hash);
        // Natural ±18 deg tuft yaw variation around the Voronoi patch streamline so blades comb organically
        clump->rotationDeg = flowAngle + (hash - 0.5f) * 36.0f;

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

        // Ghost of Tsushima Dynamic Multi-Tier Grass Landscape:
        // Tier 1: Low fine turf (0.28m - 0.36m) on crests, walkways and rocky shoulders.
        // Tier 2: Rolling meadow (0.40m - 0.52m) across open hillside slopes.
        // Tier 3: Tall prairie drifts (0.60m - 0.74m) in hollows, valleys, and river swales.
        float macroTierWave = sinf(cx * 0.042f + cz * 0.032f) * 0.55f +
                              cosf(cz * 0.060f - cx * 0.035f + 1.2f) * 0.30f +
                              sinf(cx * 0.11f + cz * 0.08f) * 0.15f;
        float tierFactor = 0.5f + 0.5f * macroTierWave;
        float tieredBase = 0.30f + 0.28f * (tierFactor * tierFactor * (3.0f - 2.0f * tierFactor));
        float highTypePatch = (patchHash2 > 0.58f) ? (0.12f * clumpCore) : (-0.05f * clumpCore);
        float heightNorm = (localHeight - 0.34f) / 0.20f;
        clump->height = (tieredBase + heightNorm * 0.14f + highTypePatch) * (0.88f + 0.20f * growth);
        clump->height *= 0.84f + 0.16f * coverage;

        if (config.flowerClusters && config.flowerClusterCount > 0)
        {
            float understorey = MapFlora_EvaluateFlowerUnderstorey(
                cx, cz, config.flowerClusters, config.flowerClusterCount);
            clump->height *= understorey;
        }

        // Broader tuft collar radius (0.26m..0.34m) so adjacent clumps interlock into a gapless cushion
        clump->radius = 0.26f + biome * 0.08f;
        clump->radius *= 0.94f + 0.16f * (localRadius - 0.24f) / 0.08f;

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

int MapFlora_GenerateReedPlacements(
    MapMeadowPlacement *outPlacements, int maxCount,
    const MapGroundSurface *ground, Vector3 groundCenter,
    MapFloraReedConfig config)
{
    if (!outPlacements || maxCount <= 0 || !ground || !ground->ready)
        return 0;

    float spanX = config.span.x > 0.0f ? config.span.x : (ground->boundary.rect.z > 0.0f ? ground->boundary.rect.z * 0.38f : 70.0f);
    float spanZ = config.span.y > 0.0f ? config.span.y : (ground->boundary.rect.w > 0.0f ? ground->boundary.rect.w * 0.32f : 96.0f);
    float minClearance = config.minWaterClearance > 0.0f ? config.minWaterClearance : 0.01f;
    float maxClearance = config.maxWaterClearance > 0.0f ? config.maxWaterClearance : 0.42f;
    unsigned char minWetAlpha = config.minWetSplatAlpha > 0 ? config.minWetSplatAlpha : 95;
    float maxWetClearance = config.maxWetClearance > 0.0f ? config.maxWetClearance : 0.85f;
    float minSlopeNormalY = config.minSlopeNormalY > 0.0f ? config.minSlopeNormalY : 0.70f;
    float minGroundY = config.minGroundY != 0.0f ? config.minGroundY : -2.40f;
    float clusterDist = config.minClusterDistance != 0.0f ? config.minClusterDistance : -0.15f;
    float minRad = config.minRadius > 0.0f ? config.minRadius : 0.24f;
    float maxRad = config.maxRadius > 0.0f ? config.maxRadius : 0.32f;
    float minH = config.minHeight > 0.0f ? config.minHeight : 0.44f;
    float maxH = config.maxHeight > 0.0f ? config.maxHeight : 0.62f;

    unsigned int rng = config.seed ? config.seed : 0x9371a5u;
    int count = 0;
    int maxAttempts = maxCount * 32;

    for (int attempts = 0; attempts < maxAttempts && count < maxCount; attempts++)
    {
        float rx = groundCenter.x + (Nature_Random01(&rng) * 2.0f - 1.0f) * spanX * 0.92f;
        float rz = groundCenter.z + (Nature_Random01(&rng) * 2.0f - 1.0f) * spanZ * 0.92f;
        float rnx = (rx - groundCenter.x) / (spanX * 0.92f);
        float rnz = (rz - groundCenter.z) / (spanZ * 0.92f);
        if (rnx * rnx + rnz * rnz > 1.0f)
            continue;

        Vector3 pos, normal;
        if (!MapProp_SampleGroundSurface(ground, groundCenter, rx, rz, &pos, &normal))
            continue;
        if (normal.y < minSlopeNormalY || pos.y < minGroundY)
            continue;

        float waterY = -100.0f;
        if (config.dem)
            waterY = MapDem_SampleWaterHeight(config.dem, rx, rz);
        else if (config.sampleWaterHeight)
            waterY = config.sampleWaterHeight(rx, rz, config.ctx);

        float clearance = pos.y - waterY;
        Color splat = {0, 0, 0, 0};
        if (config.dem)
            splat = MapDem_SampleSplat(config.dem, rx, rz);
        else if (config.sampleSplat)
            splat = config.sampleSplat(rx, rz, config.ctx);

        float cliffFloor = config.dem ? -config.dem->cliffDepth + 0.5f : -7.5f;
        bool nearShore = (waterY > cliffFloor) && (clearance >= minClearance && clearance <= maxClearance);
        bool wetMargin = (splat.a > minWetAlpha) && (clearance >= 0.02f && clearance <= maxWetClearance);

        if (nearShore || wetMargin)
        {
            float reedCluster = sinf(rx * 0.35f + rz * 0.28f) * cosf(rz * 0.42f - rx * 0.19f);
            if (reedCluster < clusterDist)
                continue;

            float rad = minRad + (maxRad - minRad) * Nature_Random01(&rng);
            float h = minH + (maxH - minH) * Nature_Random01(&rng);
            outPlacements[count++] = (MapMeadowPlacement){
                .position = pos,
                .radius = rad,
                .height = h,
                .rotationDeg = Nature_Random01(&rng) * 360.0f,
                .phase = Nature_Random01(&rng)};
        }
    }
    return count;
}

int MapFlora_GeneratePampasPlacements(
    MapMeadowPlacement *outPlacements, int maxCount,
    const MapGroundSurface *ground, Vector3 groundCenter,
    MapFloraPampasConfig config)
{
    if (!outPlacements || maxCount <= 0 || !ground || !ground->ready)
        return 0;

    float spanX = config.span.x > 0.0f ? config.span.x : (ground->boundary.rect.z > 0.0f ? ground->boundary.rect.z * 0.38f : 70.0f);
    float spanZ = config.span.y > 0.0f ? config.span.y : (ground->boundary.rect.w > 0.0f ? ground->boundary.rect.w * 0.32f : 96.0f);
    float clearingRadius = config.clearingRadius > 0.0f ? config.clearingRadius : 11.5f;
    float clearingSq = clearingRadius * clearingRadius;
    float minSlopeNormalY = config.minSlopeNormalY > 0.0f ? config.minSlopeNormalY : 0.54f;
    float minGroundY = config.minGroundY != 0.0f ? config.minGroundY : -1.20f;
    float minClearance = config.minWaterClearance > 0.0f ? config.minWaterClearance : 0.85f;
    unsigned char maxRockG = config.maxRockSplatGreen > 0 ? config.maxRockSplatGreen : 190;
    float driftThreshold = config.driftThreshold > 0.0f ? config.driftThreshold : 0.30f;
    float minRad = config.minRadius > 0.0f ? config.minRadius : 0.30f;
    float maxRad = config.maxRadius > 0.0f ? config.maxRadius : 0.40f;
    float baseH = config.baseHeight > 0.0f ? config.baseHeight : 0.48f;

    unsigned int rng = config.seed ? config.seed : 0x9371a5u;
    int count = 0;
    int maxAttempts = maxCount * 14;

    for (int attempts = 0; attempts < maxAttempts && count < maxCount; attempts++)
    {
        float rx = groundCenter.x + (Nature_Random01(&rng) * 2.0f - 1.0f) * spanX * 0.86f;
        float rz = groundCenter.z + (Nature_Random01(&rng) * 2.0f - 1.0f) * spanZ * 0.86f;
        float pnx = (rx - groundCenter.x) / (spanX * 0.86f);
        float pnz = (rz - groundCenter.z) / (spanZ * 0.86f);
        if (pnx * pnx + pnz * pnz > 1.0f)
            continue;

        float cdx = rx - groundCenter.x;
        float cdz = rz - groundCenter.z;
        if (cdx * cdx + cdz * cdz < clearingSq)
            continue;

        float driftMask = sinf(rx * 0.17f + sinf(rz * 0.09f) * 1.8f) *
                          cosf(rz * 0.15f - rx * 0.08f);
        if (driftMask < driftThreshold)
            continue;

        Vector3 pos, normal;
        if (!MapProp_SampleGroundSurface(ground, groundCenter, rx, rz, &pos, &normal))
            continue;
        if (normal.y < minSlopeNormalY || pos.y < minGroundY)
            continue;

        float waterY = -100.0f;
        if (config.dem)
            waterY = MapDem_SampleWaterHeight(config.dem, rx, rz);
        else if (config.sampleWaterHeight)
            waterY = config.sampleWaterHeight(rx, rz, config.ctx);

        if (pos.y - waterY < minClearance)
            continue;

        Color splat = {0, 0, 0, 0};
        if (config.dem)
            splat = MapDem_SampleSplat(config.dem, rx, rz);
        else if (config.sampleSplat)
            splat = config.sampleSplat(rx, rz, config.ctx);

        if (splat.g > maxRockG)
            continue;

        if (config.flowerClusters && config.flowerClusterCount > 0)
        {
            float flowerSupp = MapFlora_EvaluateFlowerSuppression(
                rx, rz, config.flowerClusters, config.flowerClusterCount);
            if (flowerSupp < 0.75f)
                continue;
        }

        float colonyWeight = MapMath_SmoothStep01(driftThreshold, 0.75f, driftMask);
        float rad = minRad + (maxRad - minRad) * Nature_Random01(&rng);
        float h = (baseH + 0.14f * colonyWeight) * (0.90f + 0.18f * Nature_Random01(&rng));
        outPlacements[count++] = (MapMeadowPlacement){
            .position = pos,
            .radius = rad,
            .height = h,
            .rotationDeg = Nature_Random01(&rng) * 360.0f,
            .phase = Nature_Random01(&rng)};
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
    flora.windDirection = (Vector2){0.95f, -0.31f};
    flora.windStrength = 0.036f;

    // 1. Valley / Meadow Grass (18m chunk size cuts draw calls & frustum checks while keeping tight culling)
    if (grassPlacements && grassCount > 0)
    {
        float lodDist = (style.lodDistance > 0.0f) ? style.lodDistance : 20.0f;
        float midDist = (style.midLodDistance > 0.0f) ? style.midLodDistance : (lodDist * 0.52f);
        flora.valleyMeadow = MapProp_CreateMeadow(grassPlacements, grassCount,
                                                  (MapMeadowStyle){
                                                      .rootColor = (style.grassRootColor.a > 0) ? style.grassRootColor : (Color){24, 38, 16, 255},
                                                      .tipColor = (style.grassTipColor.a > 0) ? style.grassTipColor : (Color){142, 192, 64, 255},
                                                      .bladesPerClump = (style.bladesPerClump > 0) ? style.bladesPerClump : 5,
                                                      .bladeSegments = (style.bladeSegments > 0) ? style.bladeSegments : 3,
                                                      .bladeWidthScale = (style.bladeWidthScale > 0.0f) ? style.bladeWidthScale : 0.128f,
                                                      .chunkSize = 18.0f,
                                                      .lodDistance = lodDist,
                                                      .midLodDistance = midDist,
                                                      .drawDistance = (style.drawDistance > 0.0f) ? style.drawDistance : 36.0f,
                                                      .shadowDistance = (style.shadowDistance < 0.0f) ? 0.0f : ((style.shadowDistance > 0.0f) ? style.shadowDistance : 10.0f),
                                                      .texturePath = NULL,
                                                      .botanicalVariation = style.botanicalVariation,
                                                      .growthForm = MAP_MEADOW_GROWTH_GRASS,
                                                      .grassState = style.grassState,
                                                      .dryness = style.dryness,
                                                      .scorch = style.scorch,
                                                  });
        flora.valleyGrassCount = grassCount;
    }

    // 2. Shoreline & River Reeds
    if (reedPlacements && reedCount > 0)
    {
        flora.reedMeadow = MapProp_CreateMeadow(reedPlacements, reedCount,
                                                (MapMeadowStyle){
                                                    .rootColor = (style.reedRootColor.a > 0) ? style.reedRootColor : (Color){24, 38, 18, 255},
                                                    .tipColor = (style.reedTipColor.a > 0) ? style.reedTipColor : (Color){126, 168, 72, 255},
                                                    .bladesPerClump = 5,
                                                    .bladeSegments = 3,
                                                    .bladeWidthScale = 0.132f,
                                                    .chunkSize = 20.0f,
                                                    .lodDistance = 13.5f,
                                                    .midLodDistance = 7.0f,
                                                    .drawDistance = 24.0f,
                                                    .shadowDistance = (style.shadowDistance < 0.0f) ? 0.0f : ((style.shadowDistance > 0.0f) ? fminf(style.shadowDistance, 7.5f) : 7.5f),
                                                    .texturePath = NULL,
                                                    .hasPlumes = false,
                                                    .growthForm = MAP_MEADOW_GROWTH_REED,
                                                });
        flora.reedCount = reedCount;
    }

    // 3. Hillside Pampas Grass (Ghost of Tsushima Susuki Plumes)
    if (style.pampasPlacements && style.pampasCount > 0)
    {
        flora.pampasMeadow = MapProp_CreateMeadow(style.pampasPlacements, style.pampasCount,
                                                  (MapMeadowStyle){
                                                      .rootColor = (style.pampasRootColor.a > 0) ? style.pampasRootColor : (Color){36, 46, 22, 255},
                                                      .tipColor = (style.pampasTipColor.a > 0) ? style.pampasTipColor : (Color){194, 188, 134, 255},
                                                      .bladesPerClump = 5,
                                                      .bladeSegments = 3,
                                                      .bladeWidthScale = 0.105f,
                                                      .chunkSize = 20.0f,
                                                      .lodDistance = 13.5f,
                                                      .midLodDistance = 7.0f,
                                                      .drawDistance = 22.0f,
                                                      .shadowDistance = (style.shadowDistance < 0.0f) ? 0.0f : ((style.shadowDistance > 0.0f) ? fminf(style.shadowDistance, 7.5f) : 7.5f),
                                                      .texturePath = NULL,
                                                      .growthForm = MAP_MEADOW_GROWTH_PAMPAS,
                                                  });
        flora.pampasCount = style.pampasCount;
    }

    // 4. Clustered Wildflower Fields: 100% Procedural 3D Petals
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

            Color stemColor = (Color){68, 98, 44, 255};
            Color centerColor = (cl->discalCenterColor.a > 0) ? cl->discalCenterColor
                                                              : (Color){248, 196, 38, 255};

            flora.flowerFields[i] = MapProp_CreateFlowerField(
                &flowerPlacements[placementOffset], count,
                stemColor, centerColor,
                NULL, 0.0f, 1, 1);

            // High-fidelity LOD: near 3D petals to 24m, far drift to 68m, shadow casters to 16m
            MapProp_SetFlowerFieldLod(&flora.flowerFields[i], 24.0f, 16.0f);
            MapProp_SetFlowerFieldDrawDistance(&flora.flowerFields[i], 68.0f);
            placementOffset += count;
        }
    }

    flora.time = 0.0f;
    flora.ready = (flora.valleyMeadow.ready || flora.reedMeadow.ready || flora.pampasMeadow.ready || flora.flowerClusterCount > 0);
    return flora;
}

void MapProp_UpdateFloraSystem(MapFloraSystem *flora, Vector3 cameraTarget, float dt)
{
    if (!flora || !flora->ready)
        return;
    flora->time += dt;

    if (flora->valleyGrassCount > 0 && flora->valleyMeadow.parametric)
    {
        MapProp_PrepareMeadow(&flora->valleyMeadow, (Vector3){0});
    }
    if (flora->reedCount > 0 && flora->reedMeadow.parametric)
    {
        MapProp_PrepareMeadow(&flora->reedMeadow, (Vector3){0});
    }
    if (flora->pampasCount > 0 && flora->pampasMeadow.parametric)
    {
        MapProp_PrepareMeadow(&flora->pampasMeadow, (Vector3){0});
    }

    // Only run default interaction pass if the map hasn't already opened a custom interaction frame
    if (!s_natureInteractionOpen)
    {
        MapProp_BeginNatureInteraction(cameraTarget, dt);
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
        // Keep windStrength <= 0.06f so reeds use the fast 32x32 GPU wind cache
        MapProp_DrawMeadow(&flora->reedMeadow, (Vector3){0}, flora->time,
                           flora->windDirection, 0.058f);
    }
    if (flora->pampasCount > 0)
    {
        MapProp_DrawMeadow(&flora->pampasMeadow, (Vector3){0}, flora->time,
                           flora->windDirection, 0.052f);
    }
    if (flora->flowerClusterCount > 0 && flora->flowerFields)
    {
        MapProp_DrawFlowerFieldBatch(flora->flowerFields, flora->flowerClusterCount,
                                     (Vector3){0}, flora->time, flora->windDirection, 0.032f);
    }
}

void MapProp_DrawFloraShadowCasters(MapFloraSystem *flora)
{
    if (!flora || !flora->ready)
        return;

    Vector3 offset = {0};
    if (Nature_ShadowCasterTypeEnabled(false))
    {
        if (flora->valleyGrassCount > 0)
        {
            MapProp_DrawMeadowShadowCasters(&flora->valleyMeadow, offset, flora->time,
                                            flora->windDirection, flora->windStrength);
        }
        if (flora->reedCount > 0)
        {
            MapProp_DrawMeadowShadowCasters(&flora->reedMeadow, offset, flora->time,
                                            flora->windDirection, 0.058f);
        }
        if (flora->pampasCount > 0)
        {
            MapProp_DrawMeadowShadowCasters(&flora->pampasMeadow, offset, flora->time,
                                            flora->windDirection, 0.052f);
        }
    }
    if (Nature_ShadowCasterTypeEnabled(true) && flora->flowerFields)
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

    if (flora->pampasCount > 0)
    {
        MapProp_UnloadMeadow(&flora->pampasMeadow);
        flora->pampasCount = 0;
    }
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