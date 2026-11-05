// ============================================================================
// GENERALIZED MULTI-STATE GRASS BLADE OPTICS & MORPHOLOGY
// Supports seamless, zero-rebuild transitions between:
//   0: MAP_GRASS_STATE_LUSH     (Cỏ xanh tươi — Emerald-Jade & Sunlit Chartreuse)
//   1: MAP_GRASS_STATE_WITHERED (Cỏ úa vàng   — Autumnal Golden-Ochre Senescence)
//   2: MAP_GRASS_STATE_DRY      (Cỏ khô       — Parched Savannah Wheat/Straw)
//   3: MAP_GRASS_STATE_SCORCHED (Cỏ cháy đen  — Fire-Charred Carbon, Ash & Embers)
// ============================================================================

// .x = dryness (0.0 = lush, 0.5 = yellowing/withered, 1.0 = parched dry straw)
// .y = scorch  (0.0 = unburned, 1.0 = charred black by fire)
// .z = emberIntensity (0.0 = cold char, >0.0 = smoldering orange-red fire embers)
// .w = patchVariation (0.0..1.0 organic spatial patchiness)
uniform vec4 u_grassStateParams;

// Up to 8 localized fire burn zones:
// C API (MapProp_AddGrassBurnZone) packs: .xy = worldXZ, .z = radius, .w = scorchStrength
// Legacy fallback supported when .w < 0.0: .xz = worldXZ, abs(.w) = radius
uniform int u_grassBurnZoneCount;
uniform vec4 u_grassBurnZones[8];

// Fast spatial organic noise for patch-coherent yellowing / drying
float GrassStatePatchNoise(vec2 worldXZ)
{
    float n0 = sin(worldXZ.x * 0.14 + worldXZ.y * 0.11 + 0.7) * 0.5 + 0.5;
    float n1 = sin(worldXZ.x * -0.23 + worldXZ.y * 0.19 + 2.1) * 0.5 + 0.5;
    return n0 * 0.62 + n1 * 0.38;
}

// Evaluates local (dryness, scorch) at a world position.
// Fast-paths to vec2(0.0) with zero loop cost when in default Lush Green state.
vec2 GrassEvaluateLocalState(vec3 worldPos)
{
    float baseDry = u_grassStateParams.x;
    float baseScorch = u_grassStateParams.y;
    int zoneCount = clamp(u_grassBurnZoneCount, 0, 8);

    if (baseDry <= 0.0001 && baseScorch <= 0.0001 && zoneCount <= 0) {
        return vec2(0.0);
    }

    float patchVar = u_grassStateParams.w;
    float patchNoise = (patchVar > 0.001 || zoneCount > 0) ? GrassStatePatchNoise(worldPos.xz) : 0.5;

    float dryness = baseDry;
    if (dryness > 0.001 && dryness < 0.999 && patchVar > 0.001) {
        dryness = clamp(dryness + (patchNoise - 0.5) * 0.32 * patchVar, 0.0, 1.0);
    }

    float scorch = baseScorch;
    float raggedScale = 0.86 + 0.28 * patchNoise;

    for (int i = 0; i < 8; i++) {
        if (i >= zoneCount) break;
        vec4 zone = u_grassBurnZones[i];

        // Hỗ trợ đồng thời chuẩn đóng gói từ C API (.xy = centerXZ, .z = radius, .w = strength)
        // và quy ước dự phòng khi .w < 0 (.xz = centerXZ, abs(.w) = radius)
        bool useLegacyLayout = (zone.w < 0.0 || zone.z <= 0.001);
        vec2 zoneCenterXZ = useLegacyLayout ? zone.xz : zone.xy;
        float radius = useLegacyLayout ? abs(zone.w) : zone.z;
        if (radius <= 0.001) continue;

        vec2 dXZ = worldPos.xz - zoneCenterXZ;
        float distSq = dot(dXZ, dXZ);

        // Culling sớm bằng bình phương khoảng cách
        float raggedRadius = radius * raggedScale;
        float maxHaloReach = raggedRadius * 1.40 + 0.08;
        if (distSq >= maxHaloReach * maxHaloReach) continue;

        float dist = distSq * inversesqrt(max(distSq, 1e-8));
        float strengthScale = useLegacyLayout ? ((zone.w < 0.0) ? 0.78 : 1.0) : clamp(zone.w, 0.0, 1.5);
        float invRagged = 1.0 / max(raggedRadius, 0.09);

        float zoneScorch = 1.0 - clamp((dist - raggedRadius * 0.45) * (invRagged * 1.8181818), 0.0, 1.0);
        zoneScorch = zoneScorch * zoneScorch * (3.0 - 2.0 * zoneScorch) * min(strengthScale, 1.0);

        // Heat halo around the fire zone also dries & yellows adjacent grass
        float heatHalo = 1.0 - clamp((dist - raggedRadius * 0.75) * (invRagged * 1.5384615), 0.0, 1.0);
        dryness = max(dryness, min(heatHalo * 0.85 * strengthScale, 1.0));
        scorch = max(scorch, zoneScorch);
    }

    return vec2(dryness, scorch);
}

// Applies morphological posture deformation to a grass blade vertex in the Vertex Shader:
// - Withered/Yellowing grass droops heavier under reduced turgor pressure.
// - Dry grass narrows/curls inward along its margins.
// - Fire-scorched grass shrivels, shortens into stunted charred stalks, and resists wind sway.
void GrassApplyVertexMorphology(inout vec3 localPos, vec3 rootPos, float h,
                                vec2 grassState, inout float windCompliance)
{
    float dryness = grassState.x;
    float scorch = grassState.y;
    if (dryness <= 0.001 && scorch <= 0.001) return;

    // 1. Withered / Dry droop & inward margin curl:
    float h2 = h * h;
    float wiltDroop = dryness * (1.0 - 0.35 * dryness) * 0.14 * h2;
    localPos.y = max(localPos.y - wiltDroop, rootPos.y + 0.005 * h);

    if (dryness > 0.001) {
        float dryCurlXZ = 1.0 - dryness * 0.10 * h;
        localPos.xz = rootPos.xz + (localPos.xz - rootPos.xz) * dryCurlXZ;
        windCompliance *= (1.0 - dryness * 0.18);
    }

    // 2. Fire-scorched shrinkage: heat shrivels the thin upper blade into stunted char stalks
    if (scorch > 0.001) {
        float shrinkXZ = 1.0 - scorch * (0.18 + 0.22 * h);
        float shrinkY  = 1.0 - scorch * (0.38 + 0.16 * h);
        vec3 rel = localPos - rootPos;
        localPos = rootPos + rel * vec3(shrinkXZ, shrinkY, shrinkXZ);
        windCompliance *= (1.0 - scorch * 0.82);
    }
}