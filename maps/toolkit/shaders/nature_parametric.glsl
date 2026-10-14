// One immutable atlas shared by all LODs. Seven RGBA32F texels per blade,
// 256 descriptors per row; template.z = local blade id + row role / 4.
uniform sampler2D u_bladeParameters;
uniform int u_bladeOffset;
uniform int u_bladesPerTuft;

uniform int u_canonicalBladeData;
uniform int u_canonicalBlades;
uniform int u_geometryLod;
uniform int u_chunkTuftOffset;

#ifdef NATURE_VISIBLE_TUFT_LOD
uniform vec4 u_tuftLodBands;
uniform int u_tuftLodLevel;
uniform vec3 u_tuftLodCamera;
uniform vec2 u_tuftFadeRange;
uniform int u_compactTuftSubmission;
uniform sampler2D u_visibleTuftIds;
uniform int u_visibleTuftOffset;
#endif

int NatureTuftId()
{
#ifdef NATURE_VISIBLE_TUFT_LOD
    if (u_compactTuftSubmission != 0) {
        int index = u_visibleTuftOffset + gl_InstanceID;
        int texel = index >> 2;
        vec4 ids = texelFetch(u_visibleTuftIds, ivec2(texel & 255, texel >> 8), 0);
        return int(ids[index & 3]);
    }
#endif
    return u_chunkTuftOffset + gl_InstanceID;
}

int NatureBladeId(int localBlade)
{
    int tuft = NatureTuftId();
    if (u_canonicalBladeData != 0) {
        int botanicalBlade = (u_bladesPerTuft > 1 && u_bladesPerTuft < u_canonicalBlades)
            ? (localBlade * (u_canonicalBlades - 1)) / (u_bladesPerTuft - 1) : localBlade;
        return tuft * u_canonicalBlades + botanicalBlade;
    }
    return u_bladeOffset + (tuft - u_chunkTuftOffset) * u_bladesPerTuft + localBlade;
}

#ifdef NATURE_VISIBLE_TUFT_LOD
bool NatureTuftUsesCurrentLod()
{
    if (u_compactTuftSubmission != 0) return true;
    if (u_tuftLodBands.y <= 0.0) return u_tuftLodLevel == 0;
    int blade = NatureBladeId(0);
    vec3 root = texelFetch(u_bladeParameters,
        ivec2((blade & 255) * 7 + 4, blade >> 8), 0).xyz;
    vec3 worldRoot = root + u_worldOffset;
    float d = distance(worldRoot, u_tuftLodCamera);

    // Early reject if beyond maximum fade range
    if (u_tuftFadeRange.y > 0.0 && d >= u_tuftFadeRange.y) return false;

    uint h = floatBitsToUint(root.x) ^ (floatBitsToUint(root.z) * 0x9e3779b9u);
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    float rank = float(h & 0x00ffffffu) * (1.0 / 16777216.0);
    float farWeight = smoothstep(u_tuftLodBands.y - u_tuftLodBands.w,
                                 u_tuftLodBands.y + u_tuftLodBands.w, d);
    float midWeight = u_tuftLodBands.x > 0.0
        ? smoothstep(u_tuftLodBands.x - u_tuftLodBands.z,
                     u_tuftLodBands.x + u_tuftLodBands.z, d) : 0.0;
    int selected = rank < farWeight ? 2 : (rank < midWeight ? 1 : 0);
    return selected == u_tuftLodLevel;
}
#endif

vec4 NatureBladeParameter(int blade, int column)
{
    return texelFetch(u_bladeParameters, ivec2((blade & 255) * 7 + column, blade >> 8), 0);
}

void NatureApplyCanonicalLod(vec4 p0, inout vec4 p1, inout vec4 p2, inout vec4 p3)
{
    if (u_canonicalBladeData != 0) {
        float widthScale = u_geometryLod == 1 ? 1.22 : (u_geometryLod == 2 ? 1.65 : (u_geometryLod == 3 ? 2.10 : 1.0));
        p1.w *= widthScale;
        if (u_geometryLod == 2) {
            float height = (p1.y - p0.y) * 3.5714286; // 1.0 / 0.28
            float droop = height * 0.68 - (p3.y - p0.y);
            p1.xz = p0.xz + (p1.xz - p0.xz) * 0.67;
            p1.y  = p0.y  + (p1.y  - p0.y)  * 0.78;
            p2.xz = p0.xz + (p2.xz - p0.xz) * 0.67;
            p2.y  = p0.y  + (p2.y  - p0.y)  * 0.78;
            p3.xz = p0.xz + (p3.xz - p0.xz) * 0.67;
            p3.y  = p0.y  + max(height * 0.0936, height * 0.5304 - droop * 0.65);
            p1.w *= 1.28;
        }
    }
}

void NatureEvaluateBlade(out vec3 position, out vec3 normal,
                         out vec4 color, out vec2 windUV, out vec2 surfaceUV)
{
    int localBlade = int(floor(vertexPosition.z));
    int blade = NatureBladeId(localBlade);

    // Compute atlas base coordinate once for all 7 texel fetches
    ivec2 baseCoord = ivec2((blade & 255) * 7, blade >> 8);
    vec4 p0 = texelFetch(u_bladeParameters, baseCoord, 0);
    vec4 p1 = texelFetch(u_bladeParameters, ivec2(baseCoord.x + 1, baseCoord.y), 0);
    vec4 p2 = texelFetch(u_bladeParameters, ivec2(baseCoord.x + 2, baseCoord.y), 0);
    vec4 p3 = texelFetch(u_bladeParameters, ivec2(baseCoord.x + 3, baseCoord.y), 0);
    vec4 clump = texelFetch(u_bladeParameters, ivec2(baseCoord.x + 4, baseCoord.y), 0);
    vec3 rootColor = texelFetch(u_bladeParameters, ivec2(baseCoord.x + 5, baseCoord.y), 0).rgb;
    vec3 tipColor  = texelFetch(u_bladeParameters, ivec2(baseCoord.x + 6, baseCoord.y), 0).rgb;

    NatureApplyCanonicalLod(p0, p1, p2, p3);

    float t = vertexPosition.y;
    float sideSign = vertexPosition.x;
    float role = fract(vertexPosition.z) * 4.0;
    float u = 1.0 - t;
    float u2 = u * u;
    float t2 = t * t;
    float ut = u * t;

    vec3 center = p0.xyz * (u2 * u) + p1.xyz * (3.0 * u2 * t) +
                  p2.xyz * (3.0 * ut * t) + p3.xyz * (t2 * t);

    // Factor out scalar 3.0 before normalize() to save 3 multiplies
    vec3 tangent = normalize(u2 * (p1.xyz - p0.xyz) +
                             (2.0 * ut) * (p2.xyz - p1.xyz) +
                             t2 * (p3.xyz - p2.xyz));

    // cross(tangent, vec3(0, 1, 0)) simplifies directly to vec3(-tangent.z, 0.0, tangent.x)
    vec3 side = vec3(-tangent.z, 0.0, tangent.x);
    float sideLenSq = dot(side, side);
    side = (sideLenSq > 1e-6) ? (side * inversesqrt(sideLenSq)) : vec3(-sin(clump.w), 0.0, cos(clump.w));

    // cross(side, tangent) is already unit-length because side and tangent are unit & orthogonal
    vec3 geometricNormal = cross(side, tangent);
    if (geometricNormal.y < 0.0) geometricNormal = -geometricNormal;
    if (geometricNormal.y < 0.25) {
        geometricNormal.y = 0.25;
        geometricNormal = normalize(geometricNormal);
    }

    // Fast polynomial replacements for sin(pi * t) and pow(t, 1.8)
    float sinPiT = 4.0 * ut;
    float powT18 = t2 * (0.82 + 0.18 * t);
    float profile = (p2.w > 0.5) ? (0.85 + 0.25 * sinPiT) * (1.0 - powT18)
                                 : (0.72 + 0.50 * t) * (1.0 - t2);
    float halfWidth = p1.w * 0.5 * max(profile, role < 1.5 ? 0.08 : 0.03);

    float fadeAlpha = 1.0;
#ifdef NATURE_VISIBLE_TUFT_LOD
    vec3 worldRoot = p0.xyz + u_worldOffset;
    float distToCam = distance(worldRoot, u_tuftLodCamera);
    float minPxHalfWidth = distToCam * 0.00065 * (1.0 - 0.5 * t);
    halfWidth = max(halfWidth, minPxHalfWidth);
#endif

    position = (role > 2.5) ? center : (center + side * (halfWidth * sideSign) + geometricNormal * (halfWidth * 0.18));

#ifdef NATURE_VISIBLE_TUFT_LOD
    if (u_tuftFadeRange.y > 0.0 && distToCam > u_tuftFadeRange.x) {
        float perimeterFade = clamp((u_tuftFadeRange.y - distToCam) /
                                    max(u_tuftFadeRange.y - u_tuftFadeRange.x, 0.001), 0.0, 1.0);
        position = p0.xyz + (position - p0.xyz) * perimeterFade;
        fadeAlpha = perimeterFade;
    }
#endif

    vec3 sphereDirection = normalize(position - vec3(clump.x, clump.y - 0.04, clump.z));
    vec3 target = mix(sphereDirection, vec3(0.0, 1.0, 0.0), role > 2.5 ? 0.55 : 0.45);
    vec3 meshNormal = geometricNormal + side * (sideSign * (role < 1.5 ? 0.35 : 0.32));
    // Single normalization happens in main() after wind tilt is applied
    normal = mix(meshNormal, target, role > 2.5 ? 0.78 : 0.72);

    float occlusion = (t < 0.22) ? (0.88 + 0.5454545 * t) : 1.0;
    if (role > 2.5) occlusion *= 0.86;

    // Smooth gradient without double-floor Mach banding while preserving [0..255] color scale
    vec3 smoothCol = clamp(mix(rootColor, tipColor, t) * occlusion * (1.0 / 255.0), 0.0, 1.0);
    color = vec4(smoothCol, fadeAlpha);
    windUV = vec2(p0.w, t);
    surfaceUV = vec2(sideSign, t);
}