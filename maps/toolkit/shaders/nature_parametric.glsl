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
// between NatureTuftUsesCurrentLod(), NatureEvaluateBlade(), and the vertex shader.
int s_cachedTuftId = -1;
float s_cachedDistToCam = -1.0;
vec4 s_cachedClumpTexel = vec4(0.0);
bool s_hasCachedClumpTexel = false;
vec3 s_cachedTangent = vec3(0.0, 1.0, 0.0);
vec3 s_cachedSide = vec3(1.0, 0.0, 0.0);
float s_cachedHalfWidth = 0.0;

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
    vec3 worldRoot = root + u_worldOffset;
    vec3 diff = worldRoot - u_tuftLodCamera;
    float d2 = dot(diff, diff);

    if (u_tuftFadeRange.y > 0.0 && d2 >= u_tuftFadeRange.y * u_tuftFadeRange.y) return false;

    float d = sqrt(d2);
    s_cachedDistToCam = d;

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
    if (u_canonicalBladeData != 0 && u_geometryLod > 0) {
        // Ghost of Tsushima Seamless LOD Continuity:
        // Low-density LOD blades evaluate the EXACT same cubic Bezier curve (p0, p1, p2, p3)
        // as high-density blades so height and arch posture never collapse across LOD bands.
        float bladeRatio = max(float(max(u_canonicalBlades, 1)) / float(max(u_bladesPerTuft, 1)), 1.0);
        float expPower = (u_geometryLod == 2) ? 0.48 : 0.44;
        p1.w *= pow(bladeRatio, expPower);
    }
}

void NatureEvaluateBlade(out vec3 position, out vec3 normal,
                         out vec4 color, out vec2 windUV, out vec2 surfaceUV)
{
    int localBlade = int(floor(vertexPosition.z));
    int blade = NatureBladeId(localBlade);

    ivec2 baseCoord = ivec2((blade & 255) * 7, blade >> 8);
    vec4 p0 = texelFetch(u_bladeParameters, baseCoord, 0);
    vec4 p1 = texelFetch(u_bladeParameters, ivec2(baseCoord.x + 1, baseCoord.y), 0);
    vec4 p2 = texelFetch(u_bladeParameters, ivec2(baseCoord.x + 2, baseCoord.y), 0);
    vec4 p3 = texelFetch(u_bladeParameters, ivec2(baseCoord.x + 3, baseCoord.y), 0);
    vec4 clump = (s_hasCachedClumpTexel && localBlade == 0)
        ? s_cachedClumpTexel
        : texelFetch(u_bladeParameters, ivec2(baseCoord.x + 4, baseCoord.y), 0);
    s_cachedClumpTexel = vec4(p0.xyz, clump.w);
    s_hasCachedClumpTexel = true;

#ifndef NATURE_BLADE_POSITION_ONLY
    // Colour texels are only read by the lit pass; the depth-only shadow caster evaluates the
    // same silhouette without paying two of its seven atlas gathers per vertex.
    vec3 rootColor = texelFetch(u_bladeParameters, ivec2(baseCoord.x + 5, baseCoord.y), 0).rgb;
    vec3 tipColor  = texelFetch(u_bladeParameters, ivec2(baseCoord.x + 6, baseCoord.y), 0).rgb;
#endif

    NatureApplyCanonicalLod(p0, p1, p2, p3);

    float t = clamp(vertexPosition.y, 0.0, 1.0);
    float sideSign = vertexPosition.x;
    float u = 1.0 - t;
    float u2 = u * u;
    float t2 = t * t;
    float ut = u * t;

    vec3 center = p0.xyz * (u2 * u) + p1.xyz * (3.0 * u2 * t) +
                  p2.xyz * (3.0 * ut * t) + p3.xyz * (t2 * t);

    vec3 rawTangent = u2 * (p1.xyz - p0.xyz) +
                      (2.0 * ut) * (p2.xyz - p1.xyz) +
                      t2 * (p3.xyz - p2.xyz);
    // [BUG FIX]: Chuẩn hóa an toàn cho rawTangent để tránh NaN nếu đường cong suy biến
    float rawTanLenSq = dot(rawTangent, rawTangent);
    vec3 tangent = (rawTanLenSq > 1e-8) ? (rawTangent * inversesqrt(rawTanLenSq)) : vec3(0.0, 1.0, 0.0);

    vec3 side = vec3(-tangent.z, 0.0, tangent.x);
    float sideLenSq = dot(side, side);
    if (sideLenSq > 1e-6) {
        side *= inversesqrt(sideLenSq);
    } else {
        side = vec3(-sin(clump.w), 0.0, cos(clump.w));
    }

#ifdef NATURE_VISIBLE_TUFT_LOD
    // 3D view-assisted ribbon orientation: gently turns edge-on blades toward the 3D camera ray
    // so tilted/bent blades never collapse into 1px dotted wires at any camera pitch or zoom.
    vec3 toCamVec = (p0.xyz + u_worldOffset) - u_tuftLodCamera;
    float distToCam;
    float invDistToCam;
    if (s_cachedDistToCam >= 0.0) {
        distToCam = s_cachedDistToCam;
        invDistToCam = 1.0 / max(distToCam, 0.001);
    } else {
        float tcLenSq = max(dot(toCamVec, toCamVec), 1e-6);
        invDistToCam = inversesqrt(tcLenSq);
        distToCam = tcLenSq * invDistToCam;
        s_cachedDistToCam = distToCam;
    }

    vec3 viewSide3D = cross(tangent, toCamVec);
    float vsLenSq = dot(viewSide3D, viewSide3D);
    if (vsLenSq > 1e-5) {
        viewSide3D *= inversesqrt(vsLenSq);
        if (dot(side, viewSide3D) < 0.0) viewSide3D = -viewSide3D;
        vec3 natCross = cross(side, tangent);
        vec3 naturalNormal = natCross * inversesqrt(max(dot(natCross, natCross), 1e-8));
        vec3 viewDir = toCamVec * invDistToCam;
        float edgeOn = 1.0 - abs(dot(naturalNormal, viewDir));
        float viewAssist = mix(0.36, 0.50, clamp(distToCam * 0.04, 0.0, 1.0));
        viewAssist = mix(viewAssist, 0.82, edgeOn * edgeOn);
        vec3 mixedSide = mix(side, viewSide3D, viewAssist);
        side = mixedSide * inversesqrt(max(dot(mixedSide, mixedSide), 1e-8));
    }
#endif

    vec3 rawGeomN = cross(side, tangent);
    vec3 geometricNormal = rawGeomN * inversesqrt(max(dot(rawGeomN, rawGeomN), 1e-8));
    if (geometricNormal.y < 0.0) geometricNormal = -geometricNormal;
    if (geometricNormal.y < 0.25) {
        geometricNormal.y = 0.25;
        geometricNormal *= inversesqrt(dot(geometricNormal, geometricNormal));
    }

    // Continuous AAA Sword-Leaf Taper Profile:
    // Swells gently in the lower third (t = 0.25) and tapers continuously from t = 0.30 to 1.00.
    // Eliminates the wide "Popsicle-stick" rectangle look and the abrupt elbow at t = 0.86!
    float sinPiT = 4.0 * ut;
    float profile;
    if (p2.w > 0.5) {
        // Reed / broad leaf profile
        profile = (0.78 + 0.36 * sinPiT) * (1.0 - t2 * (0.72 + 0.24 * t));
    } else if (p3.w > 0.5) {
        // Pampas feather plume: slender stalk below, delicate natural taper at tassel tip
        float stalk = min(1.0, 0.50 + 1.4 * t);
        float tasselTaper = (t > 0.75) ? max(0.2, 1.0 - (t - 0.75) * 3.2) : 1.0;
        profile = stalk * tasselTaper;
    } else {
        // Ghost of Tsushima C1-Continuous Katana Blade Taper:
        // Slope before tipStart (t = 0.78, profile = 0.31) matches the tip triangle slope to t = 1.0,
        // eliminating any chisel/pentagon corner where the body quad strip meets the tip triangle!
        float sheath = 0.90 + 0.10 * smoothstep(0.0, 0.15, t);
        float taper = max(0.0, 1.0 - 0.56 * t - 0.38 * t2);
        profile = sheath * taper;
    }
    float halfWidth = p1.w * 0.5 * profile;

    float fadeAlpha = 1.0;
#ifdef NATURE_VISIBLE_TUFT_LOD
    // Distance-only sub-pixel anti-aliasing floor (0 at close range so close-up tips are razor-sharp!)
    float farAntiAlias = clamp((distToCam - 8.0) * 0.04, 0.0, 1.0);
    float minPxHalfWidth = farAntiAlias * distToCam * 0.00038 * (1.0 - 0.75 * t);
    halfWidth = max(halfWidth, minPxHalfWidth);
#endif

    // Smoothly arch the blade spine without creating a step between ring vertices and the tip apex
    float spineArch = p1.w * 0.08 * sinPiT;
    s_cachedTangent = tangent;
    s_cachedSide = side;
    s_cachedHalfWidth = halfWidth;
    position = center + side * (halfWidth * sideSign) + geometricNormal * spineArch;

#ifdef NATURE_VISIBLE_TUFT_LOD
    if (u_tuftFadeRange.y > 0.0 && distToCam > u_tuftFadeRange.x) {
        float perimeterFade = clamp((u_tuftFadeRange.y - distToCam) /
                                    max(u_tuftFadeRange.y - u_tuftFadeRange.x, 0.001), 0.0, 1.0);
        float scaleFade = perimeterFade * (2.0 - perimeterFade);
        position = p0.xyz + (position - p0.xyz) * scaleFade;
        s_cachedHalfWidth *= scaleFade;
        fadeAlpha = perimeterFade;
    }
#endif

#ifdef NATURE_BLADE_POSITION_ONLY
    // Depth-only pass: no shading normal, no palette gradient, no plume tint.
    normal = vec3(0.0, 1.0, 0.0);
    color = vec4(1.0);
    windUV = vec2(p0.w, t);
    surfaceUV = vec2(sideSign, t);
#else
    // Continuous normal interpolation along t (eliminates the sudden role > 2.5 normal jump at the tip!)
    vec3 rawSphere = position - vec3(clump.x, clump.y - 0.04, clump.z);
    vec3 sphereDirection = rawSphere * inversesqrt(max(dot(rawSphere, rawSphere), 1e-8));
    float upBlend = mix(0.42, 0.54, t);
    vec3 rawTarget = mix(sphereDirection, vec3(0.0, 1.0, 0.0), upBlend);
    vec3 target = rawTarget * inversesqrt(max(dot(rawTarget, rawTarget), 1e-8));
    vec3 rawMeshN = geometricNormal + side * (sideSign * (0.34 * (1.0 - 0.35 * t)));
    vec3 meshNormal = rawMeshN * inversesqrt(max(dot(rawMeshN, rawMeshN), 1e-8));
    vec3 rawFinalN = mix(meshNormal, target, mix(0.44, 0.54, t));
    normal = rawFinalN * inversesqrt(max(dot(rawFinalN, rawFinalN), 1e-8));

    // Per-blade botanical identity (0..1) derived from blade index & phase
    float bladeHash = fract(float(localBlade) * 0.6180339 + p0.w * 1.73205);
    // Inner hero blades are fresher/brighter; outer skirt blades are deeper olive/emerald
    float tierShade = (localBlade < 2) ? 1.04 : ((localBlade < 4) ? 0.99 : 0.93);
    float bladeTone = (0.94 + 0.12 * bladeHash) * tierShade;

    // Rich 3-Stop Botanical Color Gradient:
    // Stop 1 (Deep Mossy Sheath Base) -> Stop 2 (Vibrant Viridian Body) -> Stop 3 (Warm Sunlit Chartreuse Tip)
    vec3 rootNorm = rootColor * (1.0 / 255.0);
    vec3 tipNorm  = tipColor  * (1.0 / 255.0);
    // Subtle per-blade hue shift between emerald and warm spring-gold
    vec3 hueVar = mix(vec3(0.95, 1.02, 0.96), vec3(1.06, 1.02, 0.86), bladeHash);
    tipNorm *= hueVar;
    vec3 viridianMid = mix(rootNorm, tipNorm, 0.54) * vec3(0.94, 1.04, 0.90);

    float lowerMix = smoothstep(0.0, 0.40, t);
    float upperMix = smoothstep(0.30, 0.88, t);
    vec3 gradCol = mix(mix(rootNorm, viridianMid, lowerMix), tipNorm, upperMix);

    float baseOcclusion = mix(0.76, 1.0, smoothstep(0.0, 0.32, t));
    vec3 smoothCol = clamp(gradCol * (baseOcclusion * bladeTone), 0.0, 1.0);
    if (p3.w > 0.5 && t > 0.70) {
        float plumeT = smoothstep(0.70, 0.88, t);
        vec3 plumeSilk = mix(tipNorm, vec3(0.82, 0.80, 0.62), 0.55); // Harmonious warm wheat tassel
        smoothCol = mix(smoothCol, plumeSilk * bladeTone, plumeT * 0.65);
    }

    color = vec4(smoothCol, fadeAlpha);
    windUV = vec2(p0.w, t);
    surfaceUV = vec2(sideSign, t);
#endif
}
