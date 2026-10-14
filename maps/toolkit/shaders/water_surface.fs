#version 330
#include "core/shaders/common/vfx_lights.glsl"

in vec3 fragPosition;
in vec2 fragLakeCoord;
in vec2 fragWorldXZ;
in float fragShoreFade;
in float fragWaterDepth;

uniform sampler2D texture0;         // Noise / microdetail texture
uniform sampler2D u_causticTex;     // Dual-phase caustics texture
uniform sampler2D u_cameraDepthTex; // Linear scene depth texture (if available)
uniform sampler2D u_waveFieldTex;
uniform int u_waveFieldEnabled;

uniform int u_hasDepthTex;          // Flag: 1 if hardware scene depth is bound
uniform vec2 u_resolution;          // Viewport resolution for depth sampling
uniform float u_time;
uniform float u_waveHeight;
uniform float u_waveScale;
uniform float u_waveSpeed;
uniform float u_detailScale;
uniform float u_detailStrength;
uniform vec2 u_flowVelocity;        // Directional stream flow (x, z)
uniform int u_waterShape;           // 0 = Radial, 1 = Rect, 2 = Strip
uniform vec3 u_modelPos;
uniform vec2 u_lakeRadii;
uniform int u_lakeSeed;

// Lighting uniforms
uniform vec3 u_lightDir;
uniform vec3 u_lightColor;
uniform vec3 u_ambientColor;
uniform vec3 u_viewPos;

// Shallow Water Optical Parameters (Depth <= 1.3m)
uniform float u_maxDepth;           // Max optical depth in meters (<= 1.3m)
uniform vec3 u_absorption;          // Beer-Lambert beta_e(R, G, B) (m^-1)
uniform vec3 u_deepColor;
uniform vec3 u_shallowColor;
uniform vec3 u_foamColor;
uniform vec3 u_scatterColor;        // Single-layer water SSS tint
uniform float u_scatterCoeff;       // SSS intensity
uniform float u_causticsStrength;   // Caustic intensity
uniform float u_causticsScale;      // Caustic spatial frequency
uniform float u_foamThreshold;      // Depth threshold for shoreline foam

// Dynamic Water Interaction Uniforms
uniform vec3 u_waterInteractor;
uniform vec3 u_waterVelocity;
uniform float u_waterRadius;
uniform float u_waterSubmerged;

uniform vec4 u_rippleRings[4];
uniform vec4 u_rippleParams[4];

out vec4 finalColor;

void main()
{
    float t = u_time * u_waveSpeed;

    // Continuous analytical radial bathymetry and shoreline damping
    float radial = 0.0;
    float shoreFade = fragShoreFade;
    if (u_waterShape == 0) {
        vec2 delta = (fragWorldXZ - u_modelPos.xz) / max(u_lakeRadii, vec2(0.001));
        float angle = atan(delta.y, delta.x);
        float seedPhase = float(u_lakeSeed & 1023) * 0.0173;
        float shorelineNoise = sin(angle * 2.0 + seedPhase) * 0.095
                             + cos(angle * 3.0 - seedPhase * 0.70) * 0.065
                             + sin(angle * 5.0 + 1.2) * 0.038
                             + sin(angle * 11.0 - seedPhase * 0.40) * 0.018;
        float edge = 1.0 + shorelineNoise;
        radial = clamp(length(delta) / max(edge, 0.001), 0.0, 1.0);
        shoreFade = 1.0 - smoothstep(0.75, 0.98, radial);
    }

    // ── 1. WAVE NORMALS & DUAL-PHASE FLOW (SIMD Vectorized) ──────────────────
    const vec2 d0 = vec2(0.82, 0.57);
    const vec2 d1 = vec2(-0.31, 0.95);
    const vec2 d2 = vec2(0.96, -0.18);
    vec2 pFlow = fragWorldXZ - u_flowVelocity * (u_time * 0.45);

    vec3 pWave = vec3(
        dot(pFlow, d0) * u_waveScale + t,
        dot(pFlow, d1) * (u_waveScale * 1.73) - t * 1.31,
        dot(pFlow, d2) * (u_waveScale * 2.61) + t * 0.73
    );

    vec3 cosP = cos(pWave);
    vec3 sinP = sin(pWave);

    vec2 slope = (d0 * (cosP.x * 0.50) + d1 * (cosP.y * 0.5017) + d2 * (cosP.z * 0.4176))
               * (u_waveScale * u_waveHeight * 1.85 * shoreFade);

    // Distance attenuation factor for micro-details to prevent far-field aliasing
    vec3 viewVec = u_viewPos - fragPosition;
    float distSq = dot(viewVec, viewVec);
    float detailDistFade = 1.0 / (1.0 + distSq * 0.0015);

    // Microdetail sampling
    float flowSpeedSq = dot(u_flowVelocity, u_flowVelocity);
    vec2 detailSlope = vec2(0.0);
    const vec2 eps = vec2(0.006, 0.0);

    if (flowSpeedSq > 0.000025) {
        float flowSpeed = sqrt(flowSpeedSq);
        vec2 flowDir = u_flowVelocity / flowSpeed;
        float flowProgress = u_time * flowSpeed * 0.22;
        float phase0 = fract(flowProgress);
        float phase1 = fract(flowProgress + 0.5);
        float flowWeight = abs(2.0 * phase0 - 1.0);

        vec2 baseUV = fragWorldXZ * u_detailScale;
        vec2 uv0 = baseUV + flowDir * phase0;
        vec2 uv1 = baseUV + flowDir * phase1;

        float n0 = texture(texture0, uv0).r;
        float n1 = texture(texture0, uv1).r;
        vec2 s0 = vec2(texture(texture0, uv0 + eps.xy).r, texture(texture0, uv0 + eps.yx).r) - n0;
        vec2 s1 = vec2(texture(texture0, uv1 + eps.xy).r, texture(texture0, uv1 + eps.yx).r) - n1;
        detailSlope = mix(s0, s1, flowWeight);
    } else {
        vec2 detailUv0 = fragWorldXZ * u_detailScale + vec2(t * 0.011, -t * 0.007);
        vec2 detailUv1 = vec2(-fragWorldXZ.y, fragWorldXZ.x) * (u_detailScale * 0.63)
                       + vec2(-t * 0.006, t * 0.009);
        float d0_val = texture(texture0, detailUv0).r;
        float d1_val = texture(texture0, detailUv1).r;
        vec2 d0_grad = vec2(texture(texture0, detailUv0 + eps.xy).r, texture(texture0, detailUv0 + eps.yx).r) - d0_val;
        vec2 d1_grad = vec2(texture(texture0, detailUv1 + eps.xy).r, texture(texture0, detailUv1 + eps.yx).r) - d1_val;
        detailSlope = d0_grad * 0.72 + vec2(d1_grad.y, -d1_grad.x) * 0.28;
    }

    slope += detailSlope * (u_detailStrength * 2.40 * shoreFade * detailDistFade);

    // Conditional wave-field sampling (skips texture fetch when disabled)
    vec2 interactSlope = vec2(0.0);
    float waveCurvature = 0.0;
    float wakeCrestHighlight = 0.0;
    float ringCrestHighlight = 0.0;
    float dynamicWakeFoam = 0.0;

    if (u_waveFieldEnabled > 0) {
        vec4 waveField = texture(u_waveFieldTex, fragLakeCoord * 0.5 + 0.5);
        interactSlope = (waveField.rg - 0.5) * 2.45;
        waveCurvature = (waveField.a - 0.5) * 5.0;
        wakeCrestHighlight = max(0.0, waveField.b - 0.5) * 0.52;
        // Generate subtle interactive foam on sharp wakes & high curvature crests
        dynamicWakeFoam = clamp((wakeCrestHighlight * 1.6 + max(0.0, waveCurvature) * 0.22 - 0.12) * shoreFade, 0.0, 0.65);
    }

    slope += interactSlope * shoreFade;
    vec3 normal = normalize(vec3(-slope.x, 1.0, -slope.y));

    // ── 2. DEPTH & BATHYMETRY ESTIMATION (h <= 1.3m) ─────────────────────────
    float waterDepth = 0.0;
    bool hasValidDepth = (u_waterShape == 3);
    if (hasValidDepth) waterDepth = max(0.0, fragWaterDepth);

    if (u_waterShape == 0) {
        waterDepth = u_maxDepth * (1.0 - pow(radial, 1.6)) + 0.015;
        hasValidDepth = true;
    }

    if (!hasValidDepth && u_hasDepthTex > 0 && u_resolution.x > 1.0) {
        vec2 screenUV = gl_FragCoord.xy / u_resolution;
        float sceneLinear = texture(u_cameraDepthTex, screenUV).r;
        float ndc = gl_FragCoord.z * 2.0 - 1.0;
        float fragLinear = 2000.0 / (1001.0 - ndc * 999.0);

        if (sceneLinear > 1.01 && sceneLinear >= fragLinear - 0.05) {
            waterDepth = clamp(sceneLinear - fragLinear, 0.0, u_maxDepth);
            hasValidDepth = true;
        }
    }

    if (!hasValidDepth) {
        if (u_waterShape == 1) {
            float edgeDist = min(1.0 - abs(fragLakeCoord.x), 1.0 - abs(fragLakeCoord.y));
            waterDepth = clamp(smoothstep(0.0, 0.35, edgeDist) * u_maxDepth, 0.0, u_maxDepth);
        } else if (u_waterShape == 2) {
            float channelDist = 1.0 - abs(fragLakeCoord.y);
            waterDepth = clamp(sin(channelDist * 1.5708) * u_maxDepth, 0.0, u_maxDepth);
        }
    }

    // ── 3. OPTICAL BEER-LAMBERT TRANSMITTANCE & WATER COLUMN ─────────────────
    vec3 viewDir = viewVec * inversesqrt(max(distSq, 1e-8));
    vec3 L = normalize(u_lightDir);
    float NdotV = max(dot(normal, viewDir), 0.001);
    float opticalPath = waterDepth / max(NdotV, 0.25);

    // Beer-Lambert spectral attenuation (ensure positive fallback if u_absorption is unset)
    vec3 absorptionCoeff = max(u_absorption, vec3(0.45, 0.12, 0.08));
    vec3 transmittance = exp(-absorptionCoeff * opticalPath);

    // ── 4. WATER CAUSTICS (Refracted Dual-Layer Photon Focusing) ─────────────
    // Refract caustic coordinates using surface normal and water depth
    vec2 causticRefract = normal.xz * clamp(waterDepth * 0.25, 0.015, 0.18);
    vec2 causticBase = fragWorldXZ + causticRefract;
    vec2 causticUv0 = causticBase * (u_causticsScale * 0.32) + vec2(t * 0.038, t * 0.024);
    vec2 causticUv1 = causticBase * (u_causticsScale * 0.45) + vec2(-t * 0.029, t * 0.043);
    float c0 = texture(u_causticTex, causticUv0).r;
    float c1 = texture(u_causticTex, causticUv1).r;
    float causticMin = min(c0, c1) * 2.2;
    float causticWave = causticMin * sqrt(max(causticMin, 0.0)); // Fast approximation of pow(x, 1.5~1.8)

    float depthFadeCaustic = smoothstep(0.01, 0.18, waterDepth);
    float causticIntensity = smoothstep(0.50, 0.88, causticWave) *
                             u_causticsStrength * 0.065 * depthFadeCaustic;
    vec3 causticLight = u_lightColor * causticIntensity * transmittance;

    // ── 5. SINGLE-LAYER WATER SUBSURFACE SCATTERING (Wave-Crest SSS) ─────────
    float waveFacet = dot(sinP, vec3(0.52, 0.31, 0.17));
    vec3 scatterLightDir = normalize(L + vec3(normal.x, 0.0, normal.z) * 0.65);
    float backlight = max(0.0, dot( viewDir, -vec3(scatterLightDir.x, scatterLightDir.y * 0.25, scatterLightDir.z) ));
    float bl2 = backlight * backlight;
    float sssFactor = (bl2 * backlight * 0.75 + 0.25) * u_scatterCoeff;
    float crestThickness = clamp((1.0 - normal.y * 0.85) + max(0.0, waveFacet) * 0.35 + wakeCrestHighlight * 0.8, 0.0, 1.0);
    vec3 sssLight = u_lightColor * u_scatterColor * sssFactor * (1.0 - exp(-2.4 * waterDepth)) * crestThickness;

    // ── 6. HYBRID FRESNEL & ENVIRONMENT REFLECTION (Fast Polynomials) ────────
    const float F0 = 0.02;
    float omv = 1.0 - NdotV;
    float omv2 = omv * omv;
    float fresnel = F0 + (1.0 - F0) * (omv2 * omv2 * omv);

    vec3 reflectDir = reflect(-viewDir, normal);
    float skyAngle = clamp(reflectDir.y * 0.85 + 0.15, 0.0, 1.0);
    vec3 skyZenith = vec3(0.20, 0.40, 0.72) * (0.80 + u_ambientColor.b * 0.4);
    vec3 skyHorizon = vec3(0.46, 0.58, 0.68) * (0.70 + u_ambientColor.g * 0.3);
    vec3 reflectedSky = mix(skyHorizon, skyZenith, skyAngle);

    // Subtle atmospheric sun-glow on horizon reflection
    float sunReflect = max(0.0, dot(reflectDir, L));
    float sr2 = sunReflect * sunReflect;
    float sr4 = sr2 * sr2;
    reflectedSky += u_lightColor * (sr4 * sr4 * 0.18);

    // Facet wave glints
    float crest = smoothstep(0.48, 0.95, waveFacet) * 0.035;
    vec3 waveHighlight = reflectedSky * (waveFacet * 0.030 + crest + wakeCrestHighlight * 0.45 + ringCrestHighlight * 0.55);

    // Multi-scale specular sun glint via fast squaring (replaces 3 pow() calls)
    vec3 halfDir = normalize(L + viewDir);
    float NdotH = max(dot(normal, halfDir), 0.0);
    float nh2   = NdotH * NdotH;
    float nh4   = nh2 * nh2;
    float nh8   = nh4 * nh4;
    float nh12  = nh8 * nh4;
    float nh16  = nh8 * nh8;
    float nh32  = nh16 * nh16;
    float nh48  = nh32 * nh16;
    float nh64  = nh32 * nh32;
    float nh128 = nh64 * nh64;
    float glintSharp = nh128 * nh128; // NdotH^256
    float glintMid   = nh48 * 0.20;   // NdotH^48 * 0.20

    vec3 sunGlint = u_lightColor * (glintSharp * 3.5 + glintMid * 0.8) * (0.15 + fresnel * 0.85) * detailDistFade;
    float disturbance = clamp(length(interactSlope) * 2.2, 0.0, 1.0);
    vec3 waveReflection = reflectedSky * (nh12 * disturbance * 0.43);
    vec3 waveLighting = u_lightColor * clamp(dot(normal, L) - L.y, -0.20, 0.20) * 0.28;

    // ── 7. SHORELINE & SUBMERGED CONTACT WATERLINE MENISCUS ──────────────────
    float foamDist = max(u_foamThreshold, 0.16);
    float contactMask = clamp(1.0 - waterDepth / foamDist, 0.0, 1.0);
    float brokenFoam = 0.0;

    // Only sample foam texture when near shoreline or active dynamic wake
    if (contactMask > 0.001 || dynamicWakeFoam > 0.001) {
        float meniscusLip = smoothstep(0.0, 0.022, waterDepth) * (1.0 - smoothstep(0.022, 0.075, waterDepth));
        float foamNoise = texture(texture0, fragWorldXZ * 0.45 + vec2(t * 0.022, -t * 0.016)).r;
        float contactRipple = sin(waterDepth * 36.0 - t * 4.8) * exp(-waterDepth * 6.5);

        brokenFoam = smoothstep(0.47, 0.78, contactMask * 0.59 + (foamNoise - 0.5) * 0.74);
        brokenFoam = max(brokenFoam, meniscusLip * 0.12 * smoothstep(0.48, 0.72, foamNoise));
        brokenFoam += max(contactRipple, 0.0) * 0.10 * contactMask;

        float wakeFoamTex = dynamicWakeFoam * smoothstep(0.32, 0.78, foamNoise + dynamicWakeFoam * 0.35);
        brokenFoam = clamp(max(brokenFoam, wakeFoamTex), 0.0, 1.0);
    }

    // ── 8. CRYSTAL CLEAR SEE-THROUGH WATER TRANSPARENCY ──────────────────────
    float waterColumnAlpha = 0.12 + 0.34 * (1.0 - exp(-1.8 * waterDepth));
    float surfaceAlpha = fresnel * 0.72 + waterColumnAlpha + min(length(sunGlint) * 0.22, 0.26);

    // Only sample shore edge noise when near the radial shoreline
    float shoreEdgeFade = 1.0;
    if (u_waterShape == 0) {
        if (radial > 0.84) {
            float shoreNoise = texture(texture0, fragWorldXZ * 0.72).r;
            float shoreFine  = texture(texture0, fragWorldXZ * 2.1).r;
            float shoreBreakup = (shoreNoise - 0.5) * 0.045 + (shoreFine - 0.5) * 0.012;
            shoreEdgeFade = 1.0 - smoothstep(0.91 + shoreBreakup, 1.0, radial);
        }
    } else {
        shoreEdgeFade = smoothstep(0.0005, 0.012, waterDepth);
    }
    surfaceAlpha *= shoreEdgeFade;

    float alpha = clamp(surfaceAlpha + brokenFoam * 0.28 * shoreEdgeFade, 0.0, 0.88);

    // ── 9. COMPOSITION ───────────────────────────────────────────────────────
    vec3 shallowTint = (u_waterShape == 3) ? u_shallowColor : mix(u_shallowColor, vec3(0.16, 0.64, 0.60), 0.50);
    vec3 deepTint    = (u_waterShape == 3) ? u_deepColor    : mix(u_deepColor,    vec3(0.05, 0.34, 0.42), 0.50);

    // Combine smooth depth transition with physical Beer-Lambert spectral transmittance
    vec3 depthBlend = clamp(mix(vec3(1.0 - smoothstep(0.05, 0.75, waterDepth)), transmittance, 0.55), 0.0, 1.0);
    vec3 waterVolumeColor = mix(deepTint, shallowTint, depthBlend);
    waterVolumeColor *= (0.75 + u_ambientColor * 0.30 + u_lightColor * 0.20);

    vec3 color = mix(waterVolumeColor, reflectedSky, clamp(fresnel * 0.68 + 0.10, 0.0, 1.0));
    color += causticLight;
    color += sssLight;
    color += waveHighlight;
    color += sunGlint;
    color += waveReflection + waveLighting;
    color += u_lightColor * vec3(0.66, 0.78, 0.88) * clamp(waveCurvature, -0.24, 0.24);

    vec3 frothColor = mix(u_foamColor, vec3(1.0), 0.18) * (0.82 + u_lightColor * 0.18);
    color = mix(color, frothColor, brokenFoam);

    // VFX Point Light response
    color += VFXLights_Accumulate(fragPosition, normal, u_shallowColor) * 0.65;

    finalColor = vec4(color, alpha);
}