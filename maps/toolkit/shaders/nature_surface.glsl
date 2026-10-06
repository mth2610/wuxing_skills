#include "environment/shaders/cloud_shadow.glsl"
uniform sampler2D u_cloudNoise;
uniform vec3 u_lightDir;
uniform vec3 u_lightColor;
uniform vec3 u_ambientColor;
uniform vec3 u_viewPos;
uniform vec4 colDiffuse;

// Four comparison taps interpolate visibility rather than depth. Limit the
// kernel to resolved nearby foliage; distant/LOW vegetation keeps one tap.
float NatureShadowCompare(sampler2D mapTexture, vec2 uv, float depth,
                          float texelSize, float filterWeight)
{
    if (filterWeight <= 0.0)
        return MapShadowCompare(mapTexture, uv, depth);
    float filtered = MapShadowCompareBilinear(mapTexture, uv, depth, texelSize);
    if (filterWeight >= 1.0)
        return filtered;
    return mix(MapShadowCompare(mapTexture, uv, depth), filtered, filterWeight);
}

float FoliageShadowVisibility(vec4 lightSpace, vec4 staticLightSpace,
                              vec3 normal, vec3 lightDir, vec3 worldPosition,
                              float distToCam)
{
    // Shading LOD: Bypass shadow lookups beyond near/mid gameplay range (28m).
    // Eliminates ~60% of all texture fetches with seamless distance fade.
    float shadowFade = 1.0 - smoothstep(22.0, 28.0, distToCam);
    if (shadowFade <= 0.0)
        return 1.0;

    float slope = 1.0 - max(dot(normal, lightDir), 0.0);
    // Maintain smooth bilinear PCF across the entire player focus area (up to 22m),
    // eliminating pixelated stair-stepping on foliage around the character.
    float filterWeight = (u_shadowFilterQuality > 1.5)
        ? (1.0 - smoothstep(14.0, 22.0, distToCam)) : 0.0;
    float dynamicShadow = 1.0;
    if (u_shadowEnabled > 0.5) {
        vec3 projected = lightSpace.xyz / max(lightSpace.w, 0.00001) * 0.5 + 0.5;
        if (projected.z > 0.0 && projected.z < 1.0 &&
            projected.x > 0.0 && projected.x < 1.0 &&
            projected.y > 0.0 && projected.y < 1.0) {
            float bias = clamp(0.0018 + slope * 0.0035, 0.0015, 0.0055);
            dynamicShadow = NatureShadowCompare(shadowMap, projected.xy,
                projected.z - bias, u_shadowTexel, filterWeight);
            dynamicShadow = mix(1.0, dynamicShadow, MapShadowCoverageFade(projected.xy));
        }
    }
    float staticShadow = 1.0;
    if (u_staticShadowEnabled > 0.5) {
        vec3 projected = staticLightSpace.xyz / max(staticLightSpace.w, 0.00001) * 0.5 + 0.5;
        if (projected.z > 0.0 && projected.z < 1.0 &&
            projected.x > 0.0 && projected.x < 1.0 &&
            projected.y > 0.0 && projected.y < 1.0) {
            float bias = clamp(0.0025 + slope * 0.0040, 0.0020, 0.0070);
            staticShadow = NatureShadowCompare(staticShadowMap, projected.xy,
                projected.z - bias, u_staticShadowTexel, filterWeight);
            staticShadow = mix(1.0, staticShadow, MapShadowCoverageFade(projected.xy));
        }
    }
    float combined = min(dynamicShadow, staticShadow);
    return mix(1.0, combined, shadowFade);
}

vec3 NatureShade(vec3 baseColor, vec3 worldPosition, vec3 worldNormal,
                 float heightAlongPlant, float uTransverse,
                 vec4 lightSpace, vec4 staticLightSpace)
{
    vec3 viewDir = normalize(u_viewPos - worldPosition);
    vec3 geometricNormal = normalize(worldNormal);
    vec3 faceNormal = gl_FrontFacing ? geometricNormal : -geometricNormal;
    // Ensure two-sided foliage always faces into upper hemisphere for sky ambient & sun reception
    if (faceNormal.y < 0.12) {
        faceNormal.y = abs(faceNormal.y) + 0.22;
        faceNormal = normalize(faceNormal);
    }

    // Distinguish upper bloom/petals (height > 0.65) from lower stems and grass blades
    float bloomMask = smoothstep(0.65, 0.90, heightAlongPlant);

    // Ghost of Tsushima: Curved Normals & Cylindrical Volume Illusion
    // N_curved = normalize(N_surface + u * S * k_curve)
    // uTransverse is in [-1.0, 1.0] across blade width (0.0 at spine/tip).
    // Spreads specular highlight smoothly across the blade like a 3D cylindrical reed.
    vec3 bladeSide = cross(faceNormal, vec3(0.0, 1.0, 0.0));
    if (length(bladeSide) > 0.001) {
        bladeSide = normalize(bladeSide);
        float u = clamp(uTransverse, -1.0, 1.0);
        faceNormal = normalize(faceNormal + bladeSide * (u * 0.60 * (1.0 - bloomMask)));
    }

    // Ghost of Tsushima: Distant Normal Flattening (Anti-Shimmering)
    // At close range (< 14m), blades retain 100% of their 3D transverse-rounded cylinder normals
    // for rich directional sunlight contrast and sculptural depth.
    // As distance increases (14m -> 42m), smoothly interpolate toward terrain normal (0, 1, 0)
    // to eliminate distant specular noise, aliasing, and crawling sparkle.
    float distToCam = length(u_viewPos - worldPosition);
    float antiShimmer = smoothstep(14.0, 42.0, distToCam) * (1.0 - bloomMask);
    vec3 terrainNormal = vec3(0.0, 1.0, 0.0);
    float flattenFactor = mix(0.0, 0.85, antiShimmer);
    float groundBlend = (1.0 - smoothstep(0.0, 0.25, heightAlongPlant)) * 0.35;
    float totalTerrainBlend = 1.0 - (1.0 - flattenFactor) * (1.0 - groundBlend);
    vec3 n = normalize(mix(faceNormal, terrainNormal, totalTerrainBlend));

    // Ghost of Tsushima Wrapped Diffuse Lighting:
    // I_diffuse = ((N · L + w) / (1 + w))^2
    // Quadratic falloff wraps light gently around curved foliage without harsh terminators
    float wrap = mix(0.32, 0.14, bloomMask);
    float wrapped = clamp((dot(n, u_lightDir) + wrap) / (1.0 + wrap), 0.0, 1.0);
    float directDiffuse = wrapped * wrapped;

    // Two-sided Subsurface Scattering (SSS) Transmission:
    // Light penetrates thin foliage membranes both via direct back-face sun illumination
    // and forward-scatter when looking toward the sun (Ghost of Tsushima / CryEngine foliage).
    float backfaceSun = max(-dot(faceNormal, u_lightDir), 0.0);
    float viewSunAlign = max(dot(-u_lightDir, viewDir), 0.0);
    float transmissionAngle = pow(backfaceSun, 1.5) * 0.70 + pow(viewSunAlign, 2.0) * 0.50;
    float thinness = mix(0.20, 1.0, smoothstep(0.06, 0.65, heightAlongPlant));
    float transmission = transmissionAngle * thinness;

    // Subsurface scattering color: radiant emerald-gold transmission
    vec3 subsurfaceColor = mix(
        baseColor * vec3(1.48, 1.40, 0.62), // Grass: radiant emerald-gold backlit transmission
        baseColor * vec3(1.42, 1.22, 0.88) + vec3(0.08, 0.04, 0.02), // Petals: warm translucent glow
        bloomMask
    );

    // Fast foliage shadow lookup (linear lightSpace coordinates interpolated from vertex shader)
    float shadow = FoliageShadowVisibility(lightSpace, staticLightSpace, n, u_lightDir, worldPosition, distToCam);

    // Intra-Canopy Self-Shadowing:
    // Soft, dappled attenuation as light filters through foliage canopy
    // Tips catch 100% direct sun; lower canopy stays pleasantly verdant (never pitch black)
    float hNorm = clamp(heightAlongPlant, 0.0, 1.0);
    float canopyExtinction = clamp(exp(-1.2 * (1.0 - hNorm)), 0.58, 1.0);

    // Hemispheric Ambient Lighting:
    // Upper surface catches cool sky ambient; lower surface catches warm chlorophyll grass bounce
    float skyWeight = n.y * 0.5 + 0.5;
    vec3 baseAmbient = max(u_ambientColor, vec3(0.28, 0.32, 0.24));
    vec3 skyAmbient = baseAmbient * vec3(1.06, 1.10, 1.16);
    vec3 groundBounce = baseAmbient * vec3(0.65, 0.74, 0.44);
    float cupCavity = mix(0.65, 1.0, smoothstep(0.70, 0.98, heightAlongPlant));
    vec3 ambientFloor = mix(groundBounce, skyAmbient, skyWeight) * mix(1.0, cupCavity, bloomMask);

    float horizon = 0.75 + 0.25 * max(n.y, 0.0);
    vec3 lit = baseColor * ambientFloor * horizon;

    // Specular and Rim terms
    vec3 halfDir = normalize(u_lightDir + viewDir);
    float NdotH = max(dot(faceNormal, halfDir), 0.0);
    float specPower = mix(28.0, 16.0, bloomMask);
    float specIntensity = mix(0.75 * (1.0 - antiShimmer * 0.75), 0.45, bloomMask);
    float spec = pow(NdotH, specPower) * specIntensity;

    float NdotV = max(dot(faceNormal, viewDir), 0.0);
    float rim = pow(1.0 - NdotV, 2.8) * mix(0.22, 0.30, bloomMask);

    // Direct sun, SSS, specular, and rim strictly governed by physical shadow visibility
    float cloudVis = Environment_CloudVisibility(u_cloudNoise, worldPosition);
    vec3 sunScale = u_lightColor * canopyExtinction * cloudVis;
    vec3 directSunTerms = baseColor * (directDiffuse * 1.10 + rim)
                        + subsurfaceColor * (transmission * mix(0.75, 0.92, bloomMask))
                        + vec3(spec);
    lit += directSunTerms * sunScale * shadow;

    // Diffuse sky dome transmission (skylight passing through translucent petals)
    lit += subsurfaceColor * (transmission * mix(0.18, 0.24, bloomMask)) * skyAmbient;

    // Root Contact AO: grounds foliage naturally into the soil without harsh pitch-black ink spots
    float rootAO = clamp(0.74 + 0.26 * pow(hNorm, 0.70), 0.74, 1.0);
    lit *= rootAO;

    lit += VFXLights_Accumulate(worldPosition, n, baseColor);
    return lit;
}

// Opaque grass uses a broad lighting response; preserve silhouette contrast
// without amplifying a one-pixel leaf into a bright specular line.
vec3 GrassShade(vec3 baseColor, vec3 worldPosition, vec3 worldNormal,
                float heightAlongPlant, float uTransverse,
                vec4 lightSpace, vec4 staticLightSpace)
{
    float h = clamp(heightAlongPlant, 0.0, 1.0);
    vec3 viewDir = normalize(u_viewPos - worldPosition);
    // Mesh normals already include the rounded blade and clump response.
    // Two-sided grass retains that upward canopy normal on either face.
    vec3 bladeNormal = normalize(worldNormal);
    vec3 faceNormal = vec3(bladeNormal.x, abs(bladeNormal.y), bladeNormal.z);
    float widthPixels = 2.0 / max(fwidth(uTransverse), 0.0001);
    float unresolved = 1.0 - smoothstep(1.0, 3.0, widthPixels);
    float distToCam = length(u_viewPos - worldPosition);
    float antiShimmer = max(unresolved, smoothstep(10.0, 32.0, distToCam));
    float groundBlend = (1.0 - smoothstep(0.0, 0.30, h)) * 0.30;
    float terrainBlend = 1.0 - (1.0 - antiShimmer * 0.80) * (1.0 - groundBlend);
    vec3 n = normalize(mix(faceNormal, vec3(0.0, 1.0, 0.0), terrainBlend));

    // A soft central rib is filtered away when the blade cannot resolve it.
    // No textures, alpha discard, or additional geometry are required.
    float rib = 1.0 - smoothstep(0.0, 0.32, abs(uTransverse));
    baseColor *= 1.0 + (rib - 0.35) * 0.09 * (1.0 - unresolved);
    float wrapped = clamp((dot(n, u_lightDir) + 0.42) / 1.42, 0.0, 1.0);
    float shadow = FoliageShadowVisibility(lightSpace, staticLightSpace, n, u_lightDir, worldPosition, distToCam);
    float canopy = mix(0.48, 1.0, h * (2.0 - h));

    vec3 baseAmbient = max(u_ambientColor, vec3(0.28, 0.32, 0.24));
    vec3 skyAmbient = baseAmbient * vec3(1.06, 1.10, 1.16);
    vec3 groundBounce = baseAmbient * vec3(0.65, 0.74, 0.44);
    vec3 ambient = mix(groundBounce, skyAmbient, n.y * 0.5 + 0.5);
    // Deep ground contact ambient occlusion at blade base (Ghost of Tsushima deep canopy shadow)
    float rootAO = clamp(0.24 + 0.76 * pow(h, 1.25), 0.24, 1.0);
    vec3 lit = baseColor * ambient * rootAO;

    // Ghost of Tsushima: Anisotropic fiber specular along blade length
    // Blades have longitudinal veins; highlights form soft sheen bands across width
    vec3 sideVec = normalize(cross(faceNormal, vec3(0.0, 1.0, 0.0)) + vec3(0.001, 0.0, 0.0));
    vec3 bladeTangent = normalize(cross(sideVec, faceNormal));
    vec3 halfDir = normalize(u_lightDir + viewDir);
    vec3 normalDx = dFdx(faceNormal);
    vec3 normalDy = dFdy(faceNormal);
    float variance = dot(normalDx, normalDx) + dot(normalDy, normalDy);

    float dotTH = dot(bladeTangent, halfDir);
    float sinTH = sqrt(max(0.0, 1.0 - dotTH * dotTH));
    float anisoSpec = pow(sinTH, 36.0) * 0.20 * (1.0 - antiShimmer) / (1.0 + 8.0 * variance);

    // Waxy cuticle grazing sheen (Fresnel sheen reflecting sky)
    float nv = max(dot(faceNormal, viewDir), 0.0);
    float waxFresnel = pow(1.0 - nv, 4.0);
    vec3 waxSheen = skyAmbient * waxFresnel * 0.32 * smoothstep(0.25, 0.90, h) * (1.0 - antiShimmer * 0.5);

    // Velvet tip glint and wind wave crest highlights
    float tipGlint = pow(sinTH, 12.0) * smoothstep(0.40, 1.0, h) * (1.0 - antiShimmer * 0.65);
    vec3 velvetGlint = vec3(1.25, 1.20, 0.80) * tipGlint * 0.14;

    // Chlorophyll translucency: warm emerald-gold backlight through leaf membrane
    float forwardScatter = max(dot(-u_lightDir, viewDir), 0.0);
    float backLight = max(-dot(faceNormal, u_lightDir), 0.0);
    float transmission = (pow(backLight, 1.5) * 0.58
                        + pow(forwardScatter, 2.8) * 0.42)
                        * smoothstep(0.18, 0.95, h);
    vec3 translucentColor = baseColor * vec3(1.60, 1.52, 0.42) + vec3(0.05, 0.08, 0.01);

    // Physical daylight optics:
    // Direct solar irradiance (diffuse wrap, anisotropic specular, velvet tip glint,
    // and backlight solar transmission) is occluded by shadow map visibility.
    // Hemispheric sky ambient and waxy sky reflection provide natural illumination in shade.
    float sunVis = shadow * Environment_CloudVisibility(u_cloudNoise, worldPosition);
    vec3 directSunTerms = baseColor * (wrapped * wrapped * 1.05)
                        + anisoSpec * vec3(1.0, 1.02, 0.95)
                        + velvetGlint
                        + translucentColor * (transmission * 0.92);
    vec3 directSun = directSunTerms * u_lightColor * canopy * sunVis;

    // Diffuse skylight transmission through leaf membrane (soft emerald ambient glow)
    vec3 skyTrans = translucentColor * (transmission * 0.22) * skyAmbient;

    lit += directSun + skyTrans + waxSheen;
    lit += VFXLights_Accumulate(worldPosition, n, baseColor);
    return lit;
}
