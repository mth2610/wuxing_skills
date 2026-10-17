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

// Up to 8 localized fire burn zones: .xyz = world center, .w = radius (negative encodes <1 strength)
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
    int zoneCount = u_grassBurnZoneCount;

    if (baseDry <= 0.0001 && baseScorch <= 0.0001 && zoneCount <= 0) {
        return vec2(0.0);
    }

    float patchVar = u_grassStateParams.w;
    float patchNoise = (patchVar > 0.001) ? GrassStatePatchNoise(worldPos.xz) : 0.5;

    float dryness = baseDry;
    if (dryness > 0.001 && dryness < 0.999 && patchVar > 0.001) {
        dryness = clamp(dryness + (patchNoise - 0.5) * 0.32 * patchVar, 0.0, 1.0);
    }

    float scorch = baseScorch;
    for (int i = 0; i < 8; i++) {
        if (i >= zoneCount) break;
        vec4 zone = u_grassBurnZones[i];
        float radius = abs(zone.w);
        if (radius <= 0.001) continue;
        vec2 dXZ = worldPos.xz - zone.xz;
        float dist = sqrt(dot(dXZ, dXZ));
        // Organic ragged fire perimeter using patchNoise
        float raggedRadius = radius * (0.86 + 0.28 * patchNoise);
        float zoneScorch = 1.0 - clamp((dist - raggedRadius * 0.45) / max(raggedRadius * 0.55, 0.05), 0.0, 1.0);
        zoneScorch = zoneScorch * zoneScorch * (3.0 - 2.0 * zoneScorch);
        // Heat halo around the fire zone also dries & yellows adjacent grass
        float heatHalo = 1.0 - clamp((dist - raggedRadius * 0.75) / max(raggedRadius * 0.65, 0.08), 0.0, 1.0);
        dryness = max(dryness, heatHalo * 0.85);
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

    // 1. Withered / Dry droop: loss of turgor sags the upper arch downward
    float wiltDroop = dryness * (1.0 - 0.35 * dryness) * 0.14 * h * h;
    localPos.y -= wiltDroop;

    // 2. Fire-scorched shrinkage: heat shrivels the thin upper blade into stunted char stalks
    if (scorch > 0.001) {
        float shrinkXZ = 1.0 - scorch * (0.18 + 0.22 * h);
        float shrinkY  = 1.0 - scorch * (0.38 + 0.16 * h);
        vec3 rel = localPos - rootPos;
        localPos = rootPos + rel * vec3(shrinkXZ, shrinkY, shrinkXZ);
        // Charred stalks are brittle and stiff
        windCompliance *= (1.0 - scorch * 0.82);
    }
}
