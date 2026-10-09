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
        int texel = index / 4;
        vec4 ids = texelFetch(u_visibleTuftIds, ivec2(texel % 256, texel / 256), 0);
        return int(ids[index % 4]);
    }
#endif
    return u_chunkTuftOffset + gl_InstanceID;
}

int NatureBladeId(int localBlade)
{
    int tuft = NatureTuftId();
    if (u_canonicalBladeData != 0) {
        int botanicalBlade = u_bladesPerTuft > 1 && u_bladesPerTuft < u_canonicalBlades
            ? localBlade * (u_canonicalBlades - 1) / (u_bladesPerTuft - 1) : localBlade;
        return tuft * u_canonicalBlades + botanicalBlade;
    }
    return u_bladeOffset + (tuft-u_chunkTuftOffset) * u_bladesPerTuft + localBlade;
}

#ifdef NATURE_VISIBLE_TUFT_LOD
bool NatureTuftUsesCurrentLod()
{
    if (u_compactTuftSubmission != 0) return true;
    if (u_tuftLodBands.y <= 0.0) return u_tuftLodLevel == 0;
    int blade = NatureBladeId(0);
    vec3 root = texelFetch(u_bladeParameters,
        ivec2((blade % 256) * 7 + 4, blade / 256), 0).xyz;
    uint h = floatBitsToUint(root.x) ^ (floatBitsToUint(root.z) * 0x9e3779b9u);
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    float rank = float(h & 0x00ffffffu) * (1.0 / 16777216.0);
    vec3 worldRoot = root + u_worldOffset;
    float d = distance(worldRoot, u_tuftLodCamera);
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
    return texelFetch(u_bladeParameters, ivec2((blade % 256) * 7 + column, blade / 256), 0);
}

void NatureApplyCanonicalLod(vec4 p0, inout vec4 p1, inout vec4 p2, inout vec4 p3)
{
    if (u_canonicalBladeData != 0) {
        float widthScale = u_geometryLod == 1 ? 1.22 : (u_geometryLod == 2 ? 1.65 : (u_geometryLod == 3 ? 2.10 : 1.0));
        p1.w *= widthScale;
        if (u_geometryLod == 2) {
            // Preserve the author's short/wide far silhouette and droop;
            // canonical near geometry remains unchanged for real shadows.
            float height = (p1.y-p0.y) / 0.28;
            float droop = height*0.68 - (p3.y-p0.y);
            p1.x = p0.x + (p1.x-p0.x)*0.67;
            p1.y = p0.y + (p1.y-p0.y)*0.78;
            p1.z = p0.z + (p1.z-p0.z)*0.67;
            p2.x = p0.x + (p2.x-p0.x)*0.67;
            p2.y = p0.y + (p2.y-p0.y)*0.78;
            p2.z = p0.z + (p2.z-p0.z)*0.67;
            p3.x = p0.x + (p3.x-p0.x)*0.67;
            p3.z = p0.z + (p3.z-p0.z)*0.67;
            p3.y = p0.y + max(height*0.78*0.12,height*0.78*0.68-droop*0.65);
            p1.w *= 1.28;
        }
    }
}

void NatureEvaluateBlade(out vec3 position, out vec3 normal,
                         out vec4 color, out vec2 windUV, out vec2 surfaceUV)
{
    int localBlade = int(floor(vertexPosition.z));
    int blade = NatureBladeId(localBlade);
    vec4 p0 = NatureBladeParameter(blade, 0);
    vec4 p1 = NatureBladeParameter(blade, 1);
    vec4 p2 = NatureBladeParameter(blade, 2);
    vec4 p3 = NatureBladeParameter(blade, 3);
    vec4 clump = NatureBladeParameter(blade, 4);
    vec3 rootColor = NatureBladeParameter(blade, 5).rgb;
    vec3 tipColor = NatureBladeParameter(blade, 6).rgb;
    NatureApplyCanonicalLod(p0,p1,p2,p3);
    float t = vertexPosition.y;
    float sideSign = vertexPosition.x;
    float role = fract(vertexPosition.z) * 4.0; // 1 lower, 2 upper, 3 apex
    float u = 1.0 - t;
    vec3 center = p0.xyz*(u*u*u) + p1.xyz*(3.0*u*u*t) +
                  p2.xyz*(3.0*u*t*t) + p3.xyz*(t*t*t);
    vec3 tangent = normalize(3.0*u*u*(p1.xyz-p0.xyz) +
                             6.0*u*t*(p2.xyz-p1.xyz) + 3.0*t*t*(p3.xyz-p2.xyz));
    vec3 side = cross(tangent, vec3(0.0,1.0,0.0));
    float sideLength = length(side);
    side = sideLength > 0.001 ? side/sideLength : vec3(-sin(clump.w),0.0,cos(clump.w));
    vec3 geometricNormal = normalize(cross(side,tangent));
    if (geometricNormal.y < 0.0) geometricNormal = -geometricNormal;
    geometricNormal.y = max(geometricNormal.y, 0.25);
    geometricNormal = normalize(geometricNormal);
    float profile = p2.w > 0.5 ? (0.85 + 0.25*sin(3.141592653589793*t))*(1.0-pow(t,1.8))
                              : (0.72 + 0.50*t)*(1.0-t*t);
    float halfWidth = p1.w * 0.5 * max(profile,role < 1.5 ? 0.08 : 0.03);
#ifdef NATURE_VISIBLE_TUFT_LOD
    // Sub-pixel guard (gf u_pixelSize principle): prevent far blades from collapsing below 0.75 pixel
    // which causes aliasing crawling, flicker and wasted rasterization.
    vec3 worldRoot = p0.xyz + u_worldOffset;
    float distToCam = distance(worldRoot, u_tuftLodCamera);
    float minPxHalfWidth = distToCam * 0.00065 * (1.0 - 0.5 * t);
    halfWidth = max(halfWidth, minPxHalfWidth);
#endif
    position = center + side*halfWidth*sideSign + geometricNormal*halfWidth*0.18;
    if (role > 2.5) position = center;
#ifdef NATURE_VISIBLE_TUFT_LOD
    if (u_tuftFadeRange.y > 0.0 && distToCam > u_tuftFadeRange.x) {
        float perimeterFade = clamp((u_tuftFadeRange.y - distToCam) /
                                   max(u_tuftFadeRange.y - u_tuftFadeRange.x, 0.001), 0.0, 1.0);
        position = p0.xyz + (position - p0.xyz) * perimeterFade;
    }
#endif
    vec3 sphereDirection = normalize(position - (clump.xyz-vec3(0.0,0.04,0.0)));
    vec3 target = normalize(mix(sphereDirection,vec3(0.0,1.0,0.0),role > 2.5 ? 0.55 : 0.45));
    vec3 meshNormal = normalize(geometricNormal + side*sideSign*(role < 1.5 ? 0.35 : 0.32));
    normal = normalize(mix(meshNormal,target,role > 2.5 ? 0.78 : 0.72));
    float occlusion = t < 0.22 ? 0.88 + 0.12*(t/0.22) : 1.0;
    if (role > 2.5) occlusion *= 0.86;
    // Match the two byte quantizations in Nature_LerpColor/Nature_ScaleColor.
    color = vec4(clamp(floor(floor(mix(rootColor,tipColor,t))*occlusion),0.0,255.0)/255.0,1.0);
    windUV = vec2(p0.w,t);
    surfaceUV = vec2(sideSign,t);
}
