#version 330

#include "maps/toolkit/shaders/map_shadow.glsl"
#include "environment/shaders/cloud_shadow.glsl"
#include "environment/shaders/hemisphere_lighting.glsl"

in vec3 fragWorldPos;
in float fragIceDepth;
in float fragShoreFade;

uniform sampler2D texture0;      // cloud_noise.png
uniform sampler2D u_plateTex;    // glacial_ice_plates.png (R: sheet thickness, G: fracture net, B: bubbles, A: snow crust)
uniform sampler2D u_iceFlowTex;  // mau_son_ice_flow.png (RG: D8 flow dir, B: freeze weight, A: lake mask)
uniform sampler2D u_cloudNoise;  // cloud shadow noise (bound on slot 3 by Environment_BindCloudShadowShader)
uniform int u_iceFlowEnabled;
uniform vec2 u_iceBounds;

uniform float u_time;
uniform float u_driftSpeed;
uniform vec2 u_driftDirection;
uniform float u_floeCoverage;
uniform float u_crackIntensity;
uniform float u_snowCrustAmount;
uniform vec3 u_deepIceColor;
uniform vec3 u_shallowIceColor;
uniform vec3 u_snowRimeColor;
uniform vec3 u_viewPos;
uniform vec3 u_ambientColor;
uniform vec3 u_lightDir;
uniform vec3 u_lightColor;

out vec4 finalColor;

void main()
{
    vec2 xzDx = dFdx(fragWorldPos.xz);
    vec2 xzDy = dFdy(fragWorldPos.xz);

    // Reconstruct geometric surface normal & slope for cascade / icefall detection
    vec3 dPosX = dFdx(fragWorldPos);
    vec3 dPosY = dFdy(fragWorldPos);
    vec3 rawGeomN = cross(dPosX, dPosY);
    float gLenSq = dot(rawGeomN, rawGeomN);
    vec3 geomN = (gLenSq > 1e-10) ? (rawGeomN * inversesqrt(gLenSq)) : vec3(0.0, 1.0, 0.0);
    if (geomN.y < 0.0) geomN = -geomN;
    float surfaceSlope = 1.0 - clamp(geomN.y, 0.0, 1.0);
    float cascadeMask = smoothstep(0.06, 0.22, surfaceSlope);

    // Organic shoreline feathering matching water_surface.fs
    float edgeNoiseA = textureGrad(texture0, fragWorldPos.xz * 0.45 + vec2(1.7, 2.3), xzDx * 0.45, xzDy * 0.45).r;
    float edgeNoiseB = textureGrad(texture0, fragWorldPos.xz * 1.35 - vec2(2.8, 1.1), xzDx * 1.35, xzDy * 1.35).r;
    float shoreWarp = (edgeNoiseA - 0.5) * 0.024 + (edgeNoiseB - 0.5) * 0.012 + 0.008;
    float warpedDepth = max(fragIceDepth - shoreWarp, 0.0);
    if (warpedDepth < 0.018 && cascadeMask < 0.18) discard;

    float t = mod(u_time, 6283.185307);

    // Sample baked D8 flowmap & freeze potential
    vec2 flowDir = (dot(u_driftDirection, u_driftDirection) > 1e-5) ? normalize(u_driftDirection) : vec2(0.65, 0.55);
    float freezeWeight = 0.65;
    float lakeMask = 0.50;
    if (u_iceFlowEnabled != 0) {
        vec2 flowUV = clamp(fragWorldPos.xz / max(u_iceBounds, vec2(1.0)), vec2(0.0), vec2(1.0));
        vec4 fSample = textureLod(u_iceFlowTex, flowUV, 0.0);
        vec2 bakedDir = fSample.rg * 2.0 - 1.0;
        if (dot(bakedDir, bakedDir) > 0.02) {
            flowDir = normalize(bakedDir);
        }
        freezeWeight = fSample.b;
        lakeMask = smoothstep(0.20, 0.75, fSample.a);
    }
    vec2 flowPerp = vec2(-flowDir.y, flowDir.x);

    // Shore-fast ice shelf + semi-continuous fractured glacial pack-ice (no cartoon cobblestones)
    float shoreShelf = 1.0 - smoothstep(0.08, 0.34, warpedDepth);
    float slowDrift = u_driftSpeed * (1.0 - shoreShelf * 0.95) * (1.0 - lakeMask * 0.65);
    vec2 driftOffset = flowDir * (sin(t * slowDrift * 0.45) * 0.18 + t * slowDrift * 0.06);

    float plateScale = mix(0.085, 0.055, lakeMask);
    vec2 baseUV = (fragWorldPos.xz - driftOffset) * plateScale;
    vec4 plates = textureGrad(u_plateTex, baseUV, xzDx * plateScale, xzDy * plateScale);

    vec2 rotUV = vec2(fragWorldPos.x * 0.809 - fragWorldPos.z * 0.588,
                      fragWorldPos.x * 0.588 + fragWorldPos.z * 0.809) * 0.12;
    vec4 subPlates = textureGrad(u_plateTex, rotUV, xzDx * 0.12, xzDy * 0.12);

    float floeSignal = plates.r * 0.78 + subPlates.r * 0.22;
    float shelfBias = shoreShelf * 0.26 + freezeWeight * 0.16 + lakeMask * 0.12;
    float floeThreshold = clamp(1.0 - u_floeCoverage - shelfBias, 0.08, 0.75);
    float floeAlpha = smoothstep(floeThreshold - 0.05, floeThreshold + 0.07, floeSignal + (edgeNoiseA - 0.5) * 0.12);
    // Keep steep cascades continuous (whitewater + frozen icicle chute, never holes!)
    floeAlpha = max(floeAlpha, cascadeMask);
    if (floeAlpha < 0.12) discard;

    // Reconstruct crisp bevelled ice-sheet normal from shelf height + geometric normal
    vec2 dOff = vec2(0.010, 0.0);
    float hR = textureGrad(u_plateTex, baseUV + dOff.xy, xzDx * plateScale, xzDy * plateScale).r;
    float hU = textureGrad(u_plateTex, baseUV + dOff.yx, xzDx * plateScale, xzDy * plateScale).r;
    vec3 sheetPerturb = vec3((plates.r - hR) * 1.85, 0.0, (plates.r - hU) * 1.85) * (1.0 - cascadeMask * 0.7);
    vec3 iceNormal = normalize(geomN + sheetPerturb);

    vec3 viewVec = u_viewPos - fragWorldPos;
    float camDistSq = max(dot(viewVec, viewVec), 1e-6);
    float camDist = sqrt(camDistSq);
    vec3 viewDir = viewVec / camDist;
    vec3 lDir = (dot(u_lightDir, u_lightDir) > 0.001) ? normalize(u_lightDir) : normalize(vec3(0.6, 0.5, -0.5));

    // 2-Layer Parallax Internal Fracture & Trapped Air Bubble Refraction (n_ice = 1.31)
    vec3 R_ice = refract(-viewDir, iceNormal, 0.7633588);
    if (dot(R_ice, R_ice) < 0.01) R_ice = -viewDir;
    vec2 parallaxUV = baseUV + R_ice.xz * 0.075;
    vec4 deepPlates = textureGrad(u_plateTex, parallaxUV, xzDx * plateScale, xzDy * plateScale);

    float surfaceCrack = plates.g;
    float deepCrack = deepPlates.g;
    float fractureGlow = clamp(surfaceCrack * 0.65 + deepCrack * 0.85, 0.0, 1.0) * u_crackIntensity;
    float bubbles = deepPlates.b;

    // Deep sapphire glacial ice body with crisp white internal pressure cracks
    vec3 iceBody = mix(u_shallowIceColor * 0.78, u_deepIceColor, smoothstep(0.12, 0.88, fragIceDepth * 0.68 + bubbles * 0.32));
    iceBody += vec3(0.42, 0.68, 0.86) * (fractureGlow * 0.42);
    iceBody = mix(iceBody, u_shallowIceColor * 1.08, bubbles * 0.22);

    // Wind-blown snow crust & shoreline snow-slush transition
    float snowCrustMask = clamp(plates.a * u_snowCrustAmount * smoothstep(0.20, 0.75, floeAlpha) + shoreShelf * 0.48, 0.0, 0.90);
    vec3 albedo = mix(iceBody, u_snowRimeColor, snowCrustMask);

    // Slope-aware glacial cascade: flow-aligned churning whitewater foam & hanging icicle striations (zero XZ stretch!)
    if (cascadeMask > 0.01) {
        float across = dot(fragWorldPos.xz, flowPerp);
        vec2 casUV1 = vec2(across * 0.55, fragWorldPos.y * 0.48 + t * 0.95);
        vec2 casUV2 = vec2(across * 1.25 + 0.37, fragWorldPos.y * 0.85 + t * 1.45);
        float foam1 = texture(texture0, casUV1).r;
        float foam2 = texture(texture0, casUV2).r;
        float streak = smoothstep(0.28, 0.78, foam1 * 0.60 + foam2 * 0.40);
        // Frozen icicle ribs along cascade edges + aerated whitewater froth in center
        vec3 cascadeIce = mix(u_shallowIceColor * 0.95, u_snowRimeColor * 1.03, streak * 0.85 + 0.15);
        albedo = mix(albedo, cascadeIce, cascadeMask);
        snowCrustMask = mix(snowCrustMask, 0.52 + 0.35 * streak, cascadeMask);
    }

    // Real directional shadows + cloud shadows + alpine snow-bounce hemisphere irradiance
    float rawShadow = MapShadowVisibility(fragWorldPos, iceNormal, lDir);
    float shadow = mix(0.32, 1.0, rawShadow);
    if (shadow > 0.001) {
        shadow *= Environment_CloudVisibility(u_cloudNoise, fragWorldPos);
    }

    float NdotL = max((dot(iceNormal, lDir) + 0.22) * 0.82, 0.0);
    float NdotV = max(dot(iceNormal, viewDir), 0.001);
    float omv = 1.0 - NdotV;
    float omv2 = omv * omv;
    float fresnel = 0.04 + 0.96 * (omv2 * omv2 * omv);

    vec3 halfVec = normalize(lDir + viewDir);
    float NdotH = max(dot(iceNormal, halfVec), 0.0);
    float specPower = mix(112.0, 18.0, snowCrustMask);
    float spec = pow(NdotH, specPower) * mix(0.85, 0.12, snowCrustMask) * shadow;

    float backLight = pow(max(dot(viewDir, -lDir), 0.0), 3.0);
    vec3 iceSSS = u_shallowIceColor * (backLight * 0.38 * (1.0 - snowCrustMask * 0.68)) * shadow;

    vec3 hemi = Environment_HemisphereIrradianceTinted(iceNormal, u_ambientColor, vec3(0.84, 0.90, 1.02));
    vec3 skyReflect = mix(u_ambientColor * 1.32 + vec3(0.10, 0.16, 0.28), u_lightColor * 0.58, 0.22);
    vec3 lit = albedo * (hemi + u_lightColor * (NdotL * shadow * 0.88))
             + iceSSS * u_lightColor
             + u_lightColor * spec;
    lit = mix(lit, skyReflect, fresnel * (1.0 - snowCrustMask * 0.78) * 0.58);

    // Crisp frosted ice-edge bevel highlight along narrow dark meltwater leads
    float edgeBevel = smoothstep(0.12, 0.42, floeAlpha) * (1.0 - smoothstep(0.42, 0.90, floeAlpha)) * (1.0 - cascadeMask);
    lit += u_snowRimeColor * (edgeBevel * 0.28 * (hemi + u_lightColor * (0.42 * shadow)));

    // Alpine aerial perspective matching ground_splat.fs
    if (camDist > 35.0) {
        float aerial = smoothstep(35.0, 145.0, camDist) * 0.34;
        vec3 alpineAerial = u_ambientColor * vec3(0.94, 1.02, 1.18) + u_lightColor * vec3(0.24, 0.25, 0.27);
        lit = mix(lit, alpineAerial, aerial);
    }

    finalColor = vec4(lit, 1.0);
}
