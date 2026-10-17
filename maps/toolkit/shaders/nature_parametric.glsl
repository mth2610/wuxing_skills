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

// Per-vertex cache to avoid duplicate texture fetches and distance sqrt() calls
// between NatureTuftUsesCurrentLod() and NatureEvaluateBlade().
int s_cachedTuftId = -1;
float s_cachedDistToCam = -1.0;
vec4 s_cachedClumpTexel = vec4(0.0);
bool s_hasCachedClumpTexel = false;

int NatureTuftId()
{
    if (s_cachedTuftId >= 0) return s_cachedTuftId;
#ifdef NATURE_VISIBLE_TUFT_LOD
    if (u_compactTuftSubmission != 0) {
        int index = u_visibleTuftOffset + gl_InstanceID;
        int texel = index >> 2;
        vec4 ids = texelFetch(u_visibleTuftIds, ivec2(texel & 255, texel >> 8), 0);
        s_cachedTuftId = int(ids[index & 3]);
        return s_cachedTuftId;
    }
#endif
    s_cachedTuftId = u_chunkTuftOffset + gl_InstanceID;
    return s_cachedTuftId;
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
    s_cachedTuftId = -1;
    s_cachedDistToCam = -1.0;
    s_hasCachedClumpTexel = false;

    if (u_compactTuftSubmission != 0) return true;
    if (u_tuftLodBands.y <= 0.0) return u_tuftLodLevel == 0;

    int blade = NatureBladeId(0);
    s_cachedClumpTexel = texelFetch(u_bladeParameters,
        ivec2((blade & 255) * 7 + 4, blade >> 8), 0);
    s_hasCachedClumpTexel = true;
    vec3 root = s_cachedClumpTexel.xyz;
    vec3 diff = (root + u_worldOffset) - u_tuftLodCamera;
    float d2 = dot(diff, diff);

    // Fast squared-distance reject before computing sqrt()
    if (u_tuftFadeRange.y > 0.0 && d2 >= u_tuftFadeRange.y * u_tuftFadeRange.y) return false;

    float d = sqrt(d2);
    s_cachedDistToCam = d;

    // Synchronized spatial hash + Interleaved Gradient Noise (matches CPU rank)
    uint x = floatBitsToUint(root.x);
    uint z = floatBitsToUint(root.z);
    uint h = (x * 0x8da6b343u) ^ (z * 0xd8163841u);
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    float hashRank = float(h & 0x00ffffffu) * (1.0 / 16777216.0);
    float ign = fract(52.9829189 * fract(0.06711056 * root.x + 0.00583715 * root.z));
    float rank = fract(hashRank * 0.65 + ign * 0.35);

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
        // Dynamic optical density compensation matching CPU blade-reduction ratio
        float bladeRatio = float(max(u_canonicalBlades, 1)) / float(max(u_bladesPerTuft, 1));
        float expPower = (u_geometryLod == 2) ? 0.68 : ((u_geometryLod == 1) ? 0.62 : 0.55);
        float widthScale = (u_geometryLod > 0) ? pow(bladeRatio, expPower) : 1.0;
        p1.w *= widthScale;

        if (u_geometryLod == 2) {
            float height = (p1.y - p0.y) * 3.5714286; // 1.0 / 0.28
            float droop = height * 0.68 - (p3.y - p0.y);
            p1.xz = p0.xz + (p1.xz - p0.xz) * 0.78;
            p1.y  = p0.y  + (p1.y  - p0.y)  * 0.85;
            p2.xz = p0.xz + (p2.xz - p0.xz) * 0.78;
            p2.y  = p0.y  + (p2.y  - p0.y)  * 0.85;
            p3.xz = p0.xz + (p3.xz - p0.xz) * 0.78;
            p3.y  = p0.y  + max(height * 0.14, height * 0.60 - droop * 0.55);
            p1.w *= 1.16; // Preserve lush far-LOD canopy coverage
        }
    }
}

void NatureEvaluateBlade(out vec3 position, out vec3 normal,
                         out vec4 color, out vec2 windUV, out vec2 surfaceUV)
{
    int localBlade = int(floor(vertexPosition.z));
    int blade = NatureBladeId(localBlade);

    // Compute atlas base coordinate once; reuse cached clump texel when localBlade == 0
    ivec2 baseCoord = ivec2((blade & 255) * 7, blade >> 8);
    vec4 p0 = texelFetch(u_bladeParameters, baseCoord, 0);
    vec4 p1 = texelFetch(u_bladeParameters, ivec2(baseCoord.x + 1, baseCoord.y), 0);
    vec4 p2 = texelFetch(u_bladeParameters, ivec2(baseCoord.x + 2, baseCoord.y), 0);
    vec4 p3 = texelFetch(u_bladeParameters, ivec2(baseCoord.x + 3, baseCoord.y), 0);
    vec4 clump = (s_hasCachedClumpTexel && localBlade == 0)
        ? s_cachedClumpTexel
        : texelFetch(u_bladeParameters, ivec2(baseCoord.x + 4, baseCoord.y), 0);
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

    vec3 side = vec3(-tangent.z, 0.0, tangent.x);
    float sideLenSq = dot(side, side);
    if (sideLenSq > 1e-6) {
        side *= inversesqrt(sideLenSq);
    } else {
        side = vec3(-sin(clump.w), 0.0, cos(clump.w));
    }

    // cross(side, tangent) is already unit-length because side and tangent are unit & orthogonal
    vec3 geometricNormal = cross(side, tangent);
    if (geometricNormal.y < 0.0) geometricNormal = -geometricNormal;
    if (geometricNormal.y < 0.25) {
        geometricNormal.y = 0.25;
        geometricNormal = normalize(geometricNormal);
    }

    // Slender botanical spear profile: fuller mid-body for coverage, graceful tapering tip
    float sinPiT = 4.0 * ut;
    float powT18 = t2 * (0.82 + 0.18 * t);
    float profile = (p2.w > 0.5) ? (0.85 + 0.25 * sinPiT) * (1.0 - powT18)
                                 : (0.72 + 0.50 * t) * (1.0 - t2);
    float halfWidth = p1.w * 0.5 * max(profile, role < 1.5 ? 0.08 : 0.035);

    float fadeAlpha = 1.0;
#ifdef NATURE_VISIBLE_TUFT_LOD
    float distToCam = (s_cachedDistToCam >= 0.0)
        ? s_cachedDistToCam
        : distance(p0.xyz + u_worldOffset, u_tuftLodCamera);
    // Subtle sub-pixel minimum width prevents distant blades from breaking into 1px dots
    // while keeping close-range blades slender and naturally tapered
    float minPxHalfWidth = distToCam * 0.00052 * (1.0 - 0.58 * t);
    halfWidth = max(halfWidth, minPxHalfWidth);
#endif

    // Taper midrib V-crease smoothly to 0 at the tip so the apex joins cleanly
    float creaseOffset = halfWidth * 0.18 * u;
    position = (role > 2.5)
        ? center
        : (center + side * (halfWidth * sideSign) + geometricNormal * creaseOffset);

#ifdef NATURE_VISIBLE_TUFT_LOD
    if (u_tuftFadeRange.y > 0.0 && distToCam > u_tuftFadeRange.x) {
        float perimeterFade = clamp((u_tuftFadeRange.y - distToCam) /
                                    max(u_tuftFadeRange.y - u_tuftFadeRange.x, 0.001), 0.0, 1.0);
        // Smooth ease-out scaling so distant tufts settle into the ground organically
        float scaleFade = perimeterFade * (2.0 - perimeterFade);
        position = p0.xyz + (position - p0.xyz) * scaleFade;
        fadeAlpha = perimeterFade;
    }
#endif

    vec3 sphereDirection = normalize(position - vec3(clump.x, clump.y - 0.04, clump.z));
    vec3 target = mix(sphereDirection, vec3(0.0, 1.0, 0.0), role > 2.5 ? 0.58 : 0.46);
    vec3 meshNormal = geometricNormal + side * (sideSign * (role < 1.5 ? 0.34 : 0.30));
    // Balanced spherical volume fill + sharp blade midrib crease highlight
    normal = mix(meshNormal, target, role > 2.5 ? 0.58 : 0.46);

    float occlusion = (t < 0.25) ? (0.88 + 0.48 * t) : 1.0;

    // Monotonic, natural root-to-tip botanical color gradient + subtle per-blade phase tone
    float bladeTone = 0.96 + 0.08 * fract(p0.w * 0.3183099);
    float colorT = t * (0.70 + 0.30 * t);
    vec3 smoothCol = clamp(mix(rootColor, tipColor, colorT) * (occlusion * bladeTone * (1.0 / 255.0)), 0.0, 1.0);

    color = vec4(smoothCol, fadeAlpha);
    windUV = vec2(p0.w, t);
    surfaceUV = vec2(sideSign, t);
}