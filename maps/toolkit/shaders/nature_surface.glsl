#include "environment/shaders/cloud_shadow.glsl"
#include "environment/shaders/hemisphere_lighting.glsl"

uniform sampler2D u_cloudNoise;
uniform vec3 u_lightDir;
uniform vec3 u_lightColor;
uniform vec3 u_ambientColor;
uniform vec3 u_viewPos;
uniform vec4 colDiffuse;

// ============================================================================
// FAST MATH HELPERS (ALU Optimized for Foliage Shading)
// ============================================================================
float FastPow2(float x) { return x * x; }
float FastPow4(float x) { float x2 = x * x; return x2 * x2; }
float FastPow8(float x) { float x2 = x * x; float x4 = x2 * x2; return x4 * x4; }
float FastPow16(float x) { float x4 = FastPow4(x); return x4 * x4 * x4 * x4; }
float FastPow32(float x) { float x16 = FastPow16(x); return x16 * x16; }

// Rational approximation for pow(x, 1.4) on [0, 1]: x / (1.4 - 0.4 * x)
float FastPow1_4(float x) { return x / (1.4 - 0.4 * x); }

// ============================================================================
// SHADOW MAPPING (Zero Redundant Texture Fetches)
// ============================================================================
// Avoids calling both 1-tap and 4-tap lookups during transition.
// Scales the bilinear kernel footprint down smoothly and snaps to 1-tap in the distance.
float NatureShadowCompare(sampler2D mapTexture, vec2 uv, float depth,
                          float texelSize, float filterWeight)
{
    if (filterWeight <= 0.05)
        return MapShadowCompare(mapTexture, uv, depth);
    // Smoothly contract kernel radius near the LOD boundary to avoid popping
    float effectiveTexel = texelSize * smoothstep(0.0, 0.35, filterWeight);
    return MapShadowCompareBilinear(mapTexture, uv, depth, effectiveTexel);
}

float FoliageShadowVisibility(vec4 lightSpace, vec4 staticLightSpace,
                              vec3 normal, vec3 lightDir, vec3 worldPosition,
                              float distToCam)
{
    // Shading LOD: Bypass shadow lookups beyond near/mid gameplay range (28m).
    float shadowFade = 1.0 - smoothstep(22.0, 28.0, distToCam);
    if (shadowFade <= 0.0)
        return 1.0;

    float slope = 1.0 - max(dot(normal, lightDir), 0.0);
    // Maintain smooth bilinear PCF across the player focus area (up to 22m)
    float filterWeight = (u_shadowFilterQuality > 1.5)
        ? (1.0 - smoothstep(14.0, 22.0, distToCam)) : 0.0;

    float dynamicShadow = 1.0;
    if (u_shadowEnabled > 0.5) {
        vec3 projected = (lightSpace.xyz / max(lightSpace.w, 1e-5)) * 0.5 + 0.5;
        if (all(greaterThan(projected, vec3(0.0))) && all(lessThan(projected, vec3(1.0)))) {
            float bias = clamp(0.0018 + slope * 0.0035, 0.0015, 0.0055);
            dynamicShadow = NatureShadowCompare(shadowMap, projected.xy,
                projected.z - bias, u_shadowTexel, filterWeight);
            dynamicShadow = mix(1.0, dynamicShadow, MapShadowCoverageFade(projected.xy));
        }
    }

    // Early-out optimization: Only sample static shadow map if dynamic shadow isn't already fully occluded
    float combined = dynamicShadow;
    if (u_staticShadowEnabled > 0.5 && dynamicShadow > 0.01) {
        vec3 projected = (staticLightSpace.xyz / max(staticLightSpace.w, 1e-5)) * 0.5 + 0.5;
        if (all(greaterThan(projected, vec3(0.0))) && all(lessThan(projected, vec3(1.0)))) {
            float bias = clamp(0.0025 + slope * 0.0040, 0.0020, 0.0070);
            float staticShadow = NatureShadowCompare(staticShadowMap, projected.xy,
                projected.z - bias, u_staticShadowTexel, filterWeight);
            staticShadow = mix(1.0, staticShadow, MapShadowCoverageFade(projected.xy));
            combined = min(dynamicShadow, staticShadow);
        }
    }

    return mix(1.0, combined, shadowFade);
}

// ============================================================================
// GENERAL FOLIAGE, STEMS, REEDS & BLOOMS SHADING
// ============================================================================
vec3 NatureShade(vec3 baseColor, vec3 worldPosition, vec3 worldNormal,
                 float heightAlongPlant, float uTransverse,
                 vec4 lightSpace, vec4 staticLightSpace)
{
    // Combined distance & view direction calculation (saves 1 sqrt)
    vec3 toCam = u_viewPos - worldPosition;
    float distSq = dot(toCam, toCam);
    float invDist = inversesqrt(max(distSq, 1e-6));
    vec3 viewDir = toCam * invDist;
    float distToCam = distSq * invDist;

    float hNorm = clamp(heightAlongPlant, 0.0, 1.0);
    // Distinguish upper bloom/petals (height > 0.65) from lower stems and grass blades
    float bloomMask = smoothstep(0.65, 0.90, hNorm);

    vec3 geometricNormal = normalize(worldNormal);
    vec3 faceNormal = gl_FrontFacing ? geometricNormal : -geometricNormal;

    // Smooth continuous upper-hemisphere bias (prevents normal-jump flickering in wind)
    faceNormal.y = max(faceNormal.y, -0.15) + 0.24;
    faceNormal = normalize(faceNormal);

    // Ghost of Tsushima: Curved Normals & Cylindrical Volume Illusion
    // cross(faceNormal, vec3(0, 1, 0)) simplified directly to vec3(-faceNormal.z, 0.0, faceNormal.x)
    vec3 bladeSide = vec3(-faceNormal.z, 0.0, faceNormal.x);
    float sideLenSq = dot(bladeSide, bladeSide);
    if (sideLenSq > 1e-6) {
        bladeSide *= inversesqrt(sideLenSq);
        float u = clamp(uTransverse, -1.0, 1.0);
        faceNormal = normalize(faceNormal + bladeSide * (u * 0.60 * (1.0 - bloomMask)));
    }

    // Distant Normal Flattening (Anti-Shimmering)
    float antiShimmer = smoothstep(14.0, 42.0, distToCam) * (1.0 - bloomMask);
    float flattenFactor = antiShimmer * 0.85;
    float groundBlend = (1.0 - smoothstep(0.0, 0.25, hNorm)) * 0.35;
    float totalTerrainBlend = 1.0 - (1.0 - flattenFactor) * (1.0 - groundBlend);
    vec3 n = normalize(mix(faceNormal, vec3(0.0, 1.0, 0.0), totalTerrainBlend));

    // Root-to-ground color blending: softly ground lower stems and reeds into soil
    const vec3 soilTone = vec3(0.18, 0.15, 0.09);
    float stemRootBlend = (1.0 - smoothstep(0.0, 0.22, hNorm)) * (1.0 - bloomMask) * 0.65;
    baseColor = mix(baseColor, soilTone, stemRootBlend);

    // Ghost of Tsushima Wrapped Diffuse Lighting: ((N · L + w) / (1 + w))^2
    float wrap = mix(0.32, 0.42, bloomMask);
    float wrapped = clamp((dot(n, u_lightDir) + wrap) / (1.0 + wrap), 0.0, 1.0);
    float directDiffuse = wrapped * wrapped;

    // Two-sided Subsurface Scattering (SSS) Transmission
    float backfaceSun = max(-dot(faceNormal, u_lightDir), 0.0);
    float viewSunAlign = max(dot(-u_lightDir, viewDir), 0.0);
    float transmissionAngle = FastPow1_4(backfaceSun) * 0.70 + FastPow2(viewSunAlign) * 0.60;

    // Petals have omnidirectional membrane diffusion
    float petalDiffusion = 0.28 + 0.72 * wrapped;
    transmissionAngle = mix(transmissionAngle, max(transmissionAngle, petalDiffusion * 0.75), bloomMask);
    float thinness = mix(0.20, 1.0, smoothstep(0.06, 0.65, hNorm));
    float transmission = transmissionAngle * thinness;

    // Chromatic Subsurface Scattering Color: richer chlorophyll gold shift at high thinness
    vec3 foliageSSS = baseColor * vec3(1.65, 1.82, 0.52) + vec3(0.05, 0.08, 0.015) * thinness;
    vec3 petalSSS   = baseColor * vec3(1.48, 1.28, 0.92) + vec3(0.08, 0.04, 0.02);
    vec3 subsurfaceColor = mix(foliageSSS, petalSSS, bloomMask);

    // Fast foliage shadow lookup + Cloud shadow
    float shadow = FoliageShadowVisibility(lightSpace, staticLightSpace, n, u_lightDir, worldPosition, distToCam);
    float cloudVis = Environment_CloudVisibility(u_cloudNoise, worldPosition);

    // Intra-Canopy Self-Shadowing (Fast rational approximation of exp(-1.2 * (1 - hNorm)))
    float invH = 1.0 - hNorm;
    float canopyExtinction = clamp(1.0 / (1.0 + 1.2 * invH + 0.72 * invH * invH), 0.58, 1.0);

    // Hemispheric Ambient Lighting
    float skyWeight = n.y * 0.5 + 0.5;
    vec3 skyAmbient = Environment_HemisphereIrradiance(vec3(0.0, 1.0, 0.0), u_ambientColor);
    vec3 groundBounce = Environment_HemisphereIrradiance(vec3(0.0, -1.0, 0.0), u_ambientColor);

    // Soft radial ambient occlusion from the blossom center outwards to petal tips
    float cupCavity = mix(0.58, 1.0, smoothstep(0.82, 0.98, hNorm));
    vec3 ambientFloor = mix(groundBounce, skyAmbient, skyWeight) * mix(1.0, cupCavity, bloomMask);

    float horizon = 0.75 + 0.25 * max(n.y, 0.0);
    vec3 lit = baseColor * ambientFloor * horizon;

    // Specular and Rim terms (ALU-optimized power functions + Micro-Occlusion)
    vec3 halfDir = normalize(u_lightDir + viewDir);
    float NdotH = max(dot(faceNormal, halfDir), 0.0);
    float specLobeStem = FastPow32(NdotH);
    float specLobeBloom = FastPow16(NdotH);
    float specIntensity = mix(0.75 * (1.0 - antiShimmer * 0.75), 0.45, bloomMask) * canopyExtinction;
    float spec = mix(specLobeStem, specLobeBloom, bloomMask) * specIntensity;

    // Rim light (Fast approximation of (1 - NdotV)^2.8)
    float oneMinusNV = 1.0 - max(dot(faceNormal, viewDir), 0.0);
    float rim = (oneMinusNV * oneMinusNV * sqrt(oneMinusNV)) * mix(0.22, 0.30, bloomMask);

    // Direct sun, SSS, specular, and rim strictly governed by physical shadow visibility
    vec3 sunScale = u_lightColor * (canopyExtinction * cloudVis * shadow);
    vec3 directSunTerms = baseColor * (directDiffuse * 1.10 + rim)
                        + subsurfaceColor * (transmission * mix(0.75, 0.92, bloomMask))
                        + vec3(spec * 1.0, spec * 1.0, spec * 0.94);
    lit += directSunTerms * sunScale;

    // Diffuse sky dome transmission (skylight passing through translucent petals)
    lit += subsurfaceColor * (transmission * mix(0.18, 0.24, bloomMask)) * skyAmbient;

    // Root Contact AO: Fast curve replacing pow(hNorm, 0.70)
    float hCurve = hNorm / (0.70 + 0.30 * hNorm);
    float rootAO = clamp(0.74 + 0.26 * hCurve, 0.74, 1.0);
    lit *= rootAO;

    lit += VFXLights_Accumulate(worldPosition, n, baseColor);
    return lit;
}

// ============================================================================
// HIGH-DENSITY BLADE GRASS SHADING (Dual-Lobe Anisotropic + Anti-Aliased)
// ============================================================================
vec3 GrassShade(vec3 baseColor, vec3 worldPosition, vec3 worldNormal,
                float heightAlongPlant, float uTransverse,
                vec4 lightSpace, vec4 staticLightSpace)
{
    float h = clamp(heightAlongPlant, 0.0, 1.0);

    // Combined distance & view direction calculation (saves 1 sqrt)
    vec3 toCam = u_viewPos - worldPosition;
    float distSq = dot(toCam, toCam);
    float invDist = inversesqrt(max(distSq, 1e-6));
    vec3 viewDir = toCam * invDist;
    float distToCam = distSq * invDist;

    // Two-sided grass retains upward canopy normal on either face
    vec3 bladeNormal = normalize(worldNormal);
    vec3 faceNormal = vec3(bladeNormal.x, abs(bladeNormal.y), bladeNormal.z);

    float absU = abs(uTransverse);
    float widthPixels = 2.0 / max(fwidth(uTransverse), 1e-4);
    float unresolved = 1.0 - smoothstep(1.0, 3.0, widthPixels);
    float antiShimmer = max(unresolved, smoothstep(10.0, 32.0, distToCam));

    float groundBlend = (1.0 - smoothstep(0.0, 0.30, h)) * 0.30;
    float terrainBlend = 1.0 - (1.0 - antiShimmer * 0.80) * (1.0 - groundBlend);
    vec3 n = normalize(mix(faceNormal, vec3(0.0, 1.0, 0.0), terrainBlend));

    // Soft central rib & subtle edge relief (filtered away when unresolved)
    float rib = 1.0 - smoothstep(0.0, 0.32, absU);
    baseColor *= 1.0 + (rib - 0.35) * 0.09 * (1.0 - unresolved);

    // AAA Technique: Root-to-Ground Color Blending
    const vec3 soilTone = vec3(0.18, 0.15, 0.09);
    float rootBlend = (1.0 - smoothstep(0.0, 0.24, h)) * 0.68;
    baseColor = mix(baseColor, soilTone, rootBlend);

    float wrapped = clamp((dot(n, u_lightDir) + 0.42) * 0.704225, 0.0, 1.0); // 1.0 / 1.42 = 0.704225
    float shadow = FoliageShadowVisibility(lightSpace, staticLightSpace, n, u_lightDir, worldPosition, distToCam);

    // Beer-Lambert attenuation through the unresolved canopy
    float canopyDepth = 0.45 * (1.0 - h);
    float opticalDepth = canopyDepth * 0.9 / max(u_lightDir.y, 0.10);
    // Rational approximation of exp(-opticalDepth)
    float expCanopy = 1.0 / (1.0 + opticalDepth + 0.48 * opticalDepth * opticalDepth);
    float canopy = 0.12 + 0.88 * expCanopy;

    vec3 skyAmbient = Environment_HemisphereIrradiance(vec3(0.0, 1.0, 0.0), u_ambientColor);
    vec3 groundBounce = Environment_HemisphereIrradiance(vec3(0.0, -1.0, 0.0), u_ambientColor);
    vec3 ambient = mix(groundBounce, skyAmbient, n.y * 0.5 + 0.5);

    // Deep ground contact ambient occlusion at blade base
    float rootAO = mix(0.18, 1.0, smoothstep(0.0, 0.55, h));
    vec3 lit = baseColor * ambient * rootAO;

    // Ghost of Tsushima: Analytical Tangent & Dual-Lobe Kajiya-Kay Anisotropic Specular
    // Directly projects UP(0,1,0) onto the blade plane without needing two cross() products
    vec3 rawTangent = vec3(-faceNormal.x * faceNormal.y, 1.0 - faceNormal.y * faceNormal.y, -faceNormal.z * faceNormal.y);
    vec3 bladeTangent = normalize(rawTangent + vec3(1e-4, 0.0, 0.0));
    vec3 halfDir = normalize(u_lightDir + viewDir);

    // Normal variance Toksvig-style anti-aliasing
    vec3 normalDx = dFdx(faceNormal);
    vec3 normalDy = dFdy(faceNormal);
    float variance = dot(normalDx, normalDx) + dot(normalDy, normalDy);
    float specAA = (1.0 - antiShimmer) / (1.0 + 8.0 * variance);

    float dotTH = dot(bladeTangent, halfDir);
    float sinTHSq = max(0.0, 1.0 - dotTH * dotTH);
    // Since sinTH = sqrt(sinTHSq), sinTH^36 = sinTHSq^18, and sinTH^8 = sinTHSq^4 (Saves 1 sqrt + 1 pow!)
    float sinTH4  = sinTHSq * sinTHSq;
    float sinTH8  = sinTH4 * sinTH4;
    float sinTH16 = sinTH8 * sinTH8;
    float sinTH36 = sinTH16 * sinTHSq; // Exactly sinTH^36!

    // Dual-Lobe Specular: Primary broad cuticle sheen (tinted by leaf color) + Secondary sharp sun glint
    float tipMask = smoothstep(0.20, 0.85, h);
    vec3 primarySheen = mix(baseColor, vec3(1.0, 1.02, 0.95), 0.65) * (sinTH8 * 0.08);
    vec3 secondaryGlint = vec3(1.0, 1.02, 0.95) * (sinTH36 * 0.22 * tipMask);
    vec3 anisoSpec = (primarySheen + secondaryGlint) * specAA;

    // Waxy cuticle grazing sheen (Fresnel reflecting sky)
    float nv = max(dot(faceNormal, viewDir), 0.0);
    float waxFresnel = FastPow4(1.0 - nv);
    vec3 waxSheen = skyAmbient * (waxFresnel * 0.32 * smoothstep(0.25, 0.90, h) * (1.0 - antiShimmer * 0.5));

    // Chlorophyll translucency: radiant emerald-gold backlight through leaf membrane
    float forwardScatter = max(dot(-u_lightDir, viewDir), 0.0);
    float backLight = max(-dot(faceNormal, u_lightDir), 0.0);
    float forwardLobe = FastPow2(forwardScatter);
    float backLobe = FastPow1_4(backLight);
    float edgeThinness = 0.75 + 0.50 * absU;
    float transmission = (backLobe * 0.65 + forwardLobe * 0.60)
                        * smoothstep(0.10, 0.75, h) * edgeThinness;
    vec3 translucentColor = baseColor * vec3(1.15, 1.22, 0.75) + vec3(0.03, 0.05, 0.0) * edgeThinness;

    // Physical daylight optics
    float sunVis = shadow * Environment_CloudVisibility(u_cloudNoise, worldPosition);
    vec3 directSunTerms = baseColor * (wrapped * wrapped * 1.05)
                        + anisoSpec
                        + translucentColor * (transmission * 1.10);
    vec3 directSun = directSunTerms * u_lightColor * (canopy * sunVis);

    // Sky reflection follows canopy exposure independently of solar visibility
    lit += directSun + waxSheen * rootAO;
    lit += VFXLights_Accumulate(worldPosition, n, baseColor);
    return lit;
}