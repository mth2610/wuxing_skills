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
uniform int u_waterShape;           // 0 = Radial, 1 = Rect, 2 = Strip, 3 = Terrain
uniform vec3 u_modelPos;
uniform vec2 u_lakeRadii;
uniform int u_lakeSeed;

// Lighting uniforms
uniform vec3 u_lightDir;
uniform vec3 u_lightColor;
uniform vec3 u_ambientColor;
uniform vec3 u_viewPos;

// Shallow Water Optical Parameters (Depth <= 1.5m)
uniform float u_maxDepth;           // Max optical depth in meters
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

    // Subtle natural downstream drift for terrain rivers when u_flowVelocity is unset
    vec2 effectiveFlow = u_flowVelocity;
    if (u_waterShape == 3 && dot(effectiveFlow, effectiveFlow) < 0.000025) {
        effectiveFlow = vec2(0.14, 0.22);
    }
    vec2 pFlow = fragWorldXZ - effectiveFlow * (u_time * 0.45);

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

    // Distance-gated Microdetail sampling (saves 3 texture fetches per pixel beyond 35m)
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
        float n0 = texture(texture0, uv0).r;
        vec2 s0 = vec2(texture(texture0, uv0 + eps.xy).r, texture(texture0, uv0 + eps.yx).r) - n0;

        if (distSq < 1225.0) {
            vec2 uv1 = baseUV + flowDir * phase1;
            float n1 = texture(texture0, uv1).r;
            vec2 s1 = vec2(texture(texture0, uv1 + eps.xy).r, texture(texture0, uv1 + eps.yx).r) - n1;
            detailSlope = mix(s0, s1, flowWeight);
        } else {
            detailSlope = s0;
        }
    } else {
        vec2 detailUv0 = fragWorldXZ * u_detailScale + vec2(t * 0.011, -t * 0.007);
        float d0_val = texture(texture0, detailUv0).r;
        vec2 d0_grad = vec2(texture(texture0, detailUv0 + eps.xy).r, texture(texture0, detailUv0 + eps.yx).r) - d0_val;

        if (distSq < 1225.0) {
            vec2 detailUv1 = vec2(-fragWorldXZ.y, fragWorldXZ.x) * (u_detailScale * 0.63)
                           + vec2(-t * 0.006, t * 0.009);
            float d1_val = texture(texture0, detailUv1).r;
            vec2 d1_grad = vec2(texture(texture0, detailUv1 + eps.xy).r, texture(texture0, detailUv1 + eps.yx).r) - d1_val;
            detailSlope = d0_grad * 0.72 + vec2(d1_grad.y, -d1_grad.x) * 0.28;
        } else {
            detailSlope = d0_grad;
        }
    }

    slope += detailSlope * (u_detailStrength * 2.40 * shoreFade * detailDistFade);

    // Conditional wave-field sampling + Analytical Ripple Rings & Interactor Wake
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
        dynamicWakeFoam = clamp((wakeCrestHighlight * 1.6 + max(0.0, waveCurvature) * 0.22 - 0.12) * shoreFade, 0.0, 0.65);
    }

    // Analytical expanding ripple rings (supports WATER_SHAPE_TERRAIN where wake grid is absent)
    for (int i = 0; i < 4; i++) {
        float ringAmp = u_rippleParams[i].x;
        float ringMaxRad = u_rippleRings[i].z;
        if (ringAmp > 0.001 && ringMaxRad > 0.01) {
            vec2 toFrag = fragWorldXZ - u_rippleRings[i].xy;
            float rDistSq = dot(toFrag, toFrag);
            float maxExtent = ringMaxRad + 1.25;
            if (rDistSq < maxExtent * maxExtent && rDistSq > 1e-5) {
                float rDist = sqrt(rDistSq);
                float curRad = u_rippleRings[i].w;
                float bandWidth = max(u_rippleParams[i].y, 0.35);
                float deltaR = rDist - curRad;
                float env = exp(-(deltaR * deltaR) / (bandWidth * bandWidth)) * ringAmp;
                if (env > 0.001) {
                    float phase = deltaR * 18.0 - u_time * 8.0;
                    float sWave = sin(phase);
                    float cWave = cos(phase);
                    vec2 rDir = toFrag / rDist;
                    interactSlope += rDir * (cWave * env * 0.85);
                    ringCrestHighlight += max(0.0, sWave) * env * 0.55;
                    dynamicWakeFoam = max(dynamicWakeFoam, max(0.0, sWave) * env * 0.38 * shoreFade);
                }
            }
        }
    }

    // Direct player/entity water interactor wake
    if (u_waterRadius > 0.01) {
        vec2 toInteractor = fragWorldXZ - u_waterInteractor.xz;
        float iDistSq = dot(toInteractor, toInteractor);
        float activeRad = u_waterRadius * 3.2;
        if (iDistSq < activeRad * activeRad && iDistSq > 1e-5) {
            float iDist = sqrt(iDistSq);
            float normDist = iDist / u_waterRadius;
            float speed = length(u_waterVelocity.xz);
            float wakeEnv = exp(-normDist * 0.85) * clamp(speed * 0.25 + 0.15, 0.0, 1.0);
            float wakeWave = sin(normDist * 10.0 - u_time * 9.0);
            float wakeDeriv = cos(normDist * 10.0 - u_time * 9.0);
            vec2 iDir = toInteractor / iDist;
            interactSlope += iDir * (wakeDeriv * wakeEnv * 0.65);
            wakeCrestHighlight += max(0.0, wakeWave) * wakeEnv * 0.45;
            dynamicWakeFoam = max(dynamicWakeFoam, smoothstep(1.3, 0.4, normDist) * wakeEnv * 0.42 * shoreFade);
        }
    }

    slope += interactSlope * shoreFade;
    vec3 normal = normalize(vec3(-slope.x, 1.0, -slope.y));

    // ── 2. DEPTH & BATHYMETRY ESTIMATION (h <= 1.5m) ─────────────────────────
    float waterDepth = 0.0;
    bool hasValidDepth = (u_waterShape == 3);
    if (hasValidDepth) waterDepth = max(0.0, fragWaterDepth);

    if (u_waterShape == 0) {
        // Fast polynomial for 1.0 - pow(radial, 1.6)
        float rad16 = radial * mix(1.0, radial, 0.6);
        waterDepth = u_maxDepth * (1.0 - rad16) + 0.015;
        hasValidDepth = true;
    }

    // Hardware scene depth buffer refinement (used only for analytic rect/strip shapes)
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
    // Zenith irradiance compensation + HDR soft-knee so water never washes out
    float zenithDamp = 1.0 / (1.0 + max(L.y - 0.36, 0.0) * 0.85);
    vec3 effectiveLight = u_lightColor * zenithDamp;
    float lightPeak = max(max(effectiveLight.r, effectiveLight.g), effectiveLight.b);
    if (lightPeak > 1.35) {
        effectiveLight *= (1.35 + (lightPeak - 1.35) * 0.35) / lightPeak;
    }

    float NdotV = max(dot(normal, viewDir), 0.001);
    float opticalPath = waterDepth / max(NdotV, 0.28);

    // Beer-Lambert spectral attenuation
    vec3 absorptionCoeff = max(u_absorption, vec3(0.48, 0.16, 0.10));
    vec3 transmittance = exp(-absorptionCoeff * opticalPath);

    // ── 4. WATER CAUSTICS (Refracted Dual-Layer Photon Focusing) ─────────────
    vec2 causticRefract = normal.xz * clamp(waterDepth * 0.25, 0.015, 0.18);
    vec2 causticBase = fragWorldXZ + causticRefract;
    vec2 causticUv0 = causticBase * (u_causticsScale * 0.32) + vec2(t * 0.038, t * 0.024);
    vec2 causticUv1 = causticBase * (u_causticsScale * 0.45) + vec2(-t * 0.029, t * 0.043);
    float c0 = texture(u_causticTex, causticUv0).r;
    float c1 = texture(u_causticTex, causticUv1).r;
    float causticMin = min(c0, c1) * 2.2;
    float causticWave = causticMin * sqrt(max(causticMin, 0.0));

    float depthFadeCaustic = smoothstep(0.015, 0.20, waterDepth) * (1.0 - smoothstep(0.85, 1.45, waterDepth) * 0.45);
    float causticIntensity = smoothstep(0.50, 0.88, causticWave) *
                             u_causticsStrength * 0.068 * depthFadeCaustic;
    vec3 causticLight = effectiveLight * causticIntensity * transmittance;

    // ── 5. SINGLE-LAYER WATER SUBSURFACE SCATTERING (Wave-Crest SSS) ─────────
    float waveFacet = dot(sinP, vec3(0.52, 0.31, 0.17));
    vec3 scatterLightDir = normalize(L + vec3(normal.x, 0.0, normal.z) * 0.65);
    float backlight = max(0.0, dot(viewDir, -vec3(scatterLightDir.x, scatterLightDir.y * 0.25, scatterLightDir.z)));
    float bl2 = backlight * backlight;
    float sssFactor = (bl2 * backlight * 0.78 + 0.18) * u_scatterCoeff;
    float crestThickness = clamp((1.0 - normal.y * 0.88) + max(0.0, waveFacet) * 0.32 + wakeCrestHighlight * 0.75, 0.0, 1.0);
    vec3 sssLight = effectiveLight * u_scatterColor * sssFactor * (1.0 - exp(-2.2 * waterDepth)) * crestThickness;

    // ── 6. HYBRID FRESNEL & ENVIRONMENT REFLECTION (Energy-Conserving) ───────
    const float F0 = 0.02;
    float omv = 1.0 - NdotV;
    float omv2 = omv * omv;
    float fresnel = F0 + (1.0 - F0) * (omv2 * omv2 * omv);

    vec3 reflectDir = reflect(-viewDir, normal);
    float skyAngle = clamp(reflectDir.y * 0.85 + 0.15, 0.0, 1.0);
    // Natural mountain-valley sky & riparian foliage reflection (avoids artificial neon-cyan wash)
    vec3 skyZenith  = vec3(0.14, 0.30, 0.52) * (0.72 + u_ambientColor.b * 0.34);
    vec3 skyHorizon = vec3(0.32, 0.44, 0.50) * (0.66 + u_ambientColor.g * 0.26);
    vec3 reflectedSky = mix(skyHorizon, skyZenith, skyAngle);

    // Subtle green-emerald riverbank canopy reflection near shallow margins
    vec3 bankCanopyReflect = vec3(0.11, 0.20, 0.09) * (0.65 + u_ambientColor.g * 0.45);
    float bankReflectMask = (1.0 - smoothstep(0.05, 0.65, waterDepth)) * (1.0 - skyAngle * 0.65);
    reflectedSky = mix(reflectedSky, bankCanopyReflect, bankReflectMask * 0.48);

    // Restrained atmospheric sun-glow on horizon reflection
    float sunReflect = max(0.0, dot(reflectDir, L));
    float sr2 = sunReflect * sunReflect;
    float sr4 = sr2 * sr2;
    float sr8 = sr4 * sr4;
    reflectedSky += effectiveLight * (sr8 * sr8 * 0.10);

    // Facet wave glints
    float crest = smoothstep(0.48, 0.92, waveFacet) * 0.045;
    vec3 waveHighlight = reflectedSky * (max(waveFacet, 0.0) * 0.038 + crest + wakeCrestHighlight * 0.42 + ringCrestHighlight * 0.52);

    // Crisp multi-scale specular sun glint (eliminates wide nh16 milky glare patch)
    vec3 halfDir = normalize(L + viewDir);
    float NdotH = max(dot(normal, halfDir), 0.0);
    float nh2   = NdotH * NdotH;
    float nh4   = nh2 * nh2;
    float nh8   = nh4 * nh4;
    float nh12  = nh8 * nh4;
    float nh16  = nh8 * nh8;
    float nh32  = nh16 * nh16;
    float nh64  = nh32 * nh32;
    float nh96  = nh64 * nh32;
    float nh128 = nh64 * nh64;
    float glintSharp = nh128 * nh128; // NdotH^256 crisp sun diamond
    // Modulate mid-glint by wave slope energy so flat water never turns into a bright white sheet
    float waveSlopeEnergy = clamp(length(slope) * 14.0, 0.12, 1.0);
    float glintMid = (nh96 * 0.28 + nh64 * 0.12) * waveSlopeEnergy;

    vec3 sunGlint = effectiveLight * (glintSharp * 2.8 + glintMid * 0.65) * (0.20 + fresnel * 0.80) * detailDistFade;
    float disturbance = clamp(length(interactSlope) * 2.2, 0.0, 1.0);
    vec3 waveReflection = reflectedSky * (nh12 * disturbance * 0.38);

    // Crisp wave-slope contrast across all sun angles
    vec2 horizAzimuth = normalize(L.xz + vec2(0.42, -0.34));
    float waveSlopeContrast = dot(normal.xz, horizAzimuth) * 2.2;
    vec3 waveLighting = effectiveLight * clamp(waveSlopeContrast + (dot(normal, L) - L.y), -0.22, 0.24) * 0.28;

    // ── 7. SHORELINE & SUBMERGED CONTACT WATERLINE MENISCUS ──────────────────
    float foamDist = max(u_foamThreshold, 0.15);
    float contactMask = clamp(1.0 - waterDepth / foamDist, 0.0, 1.0);
    float brokenFoam = 0.0;

    if (contactMask > 0.001 || dynamicWakeFoam > 0.001) {
        float meniscusLip = smoothstep(0.0, 0.022, waterDepth) * (1.0 - smoothstep(0.022, 0.075, waterDepth));
        float foamNoise = texture(texture0, fragWorldXZ * 0.45 + vec2(t * 0.022, -t * 0.016)).r;
        float contactRipple = sin(waterDepth * 36.0 - t * 4.8) * exp(-waterDepth * 6.5);

        brokenFoam = smoothstep(0.49, 0.80, contactMask * 0.58 + (foamNoise - 0.5) * 0.72);
        brokenFoam = max(brokenFoam, meniscusLip * 0.11 * smoothstep(0.48, 0.72, foamNoise));
        brokenFoam += max(contactRipple, 0.0) * 0.09 * contactMask;

        float wakeFoamTex = dynamicWakeFoam * smoothstep(0.32, 0.78, foamNoise + dynamicWakeFoam * 0.35);
        brokenFoam = clamp(max(brokenFoam, wakeFoamTex), 0.0, 1.0);
        if (u_waterShape == 3) {
            // Fade foam cleanly at shallow shoreline lip & river stones so submerged rocks aren't ringed in white
            brokenFoam *= smoothstep(0.045, 0.16, waterDepth) * 0.52;
        }
    }

    // ── 8. CRYSTAL CLEAR SEE-THROUGH WATER TRANSPARENCY ──────────────────────
    float waterColumnAlpha = (u_waterShape == 3)
        ? (0.05 + 0.68 * smoothstep(0.02, 0.75, waterDepth))
        : (0.12 + 0.36 * (1.0 - exp(-1.8 * waterDepth)));
    // Only crisp pinpoint glintSharp contributes to alpha, preventing broad milky opacity blobs
    float glintAlphaBoost = clamp(glintSharp * 0.35, 0.0, 0.22);
    float surfaceAlpha = fresnel * 0.65 + waterColumnAlpha + glintAlphaBoost;

    // Smooth analytical shoreline edge feathering (clean, antialiased contact lip)
    float shoreEdgeFade = 1.0;
    if (u_waterShape == 0) {
        if (radial > 0.84) {
            float shoreNoise = texture(texture0, fragWorldXZ * 0.72).r;
            float shoreFine  = texture(texture0, fragWorldXZ * 2.1).r;
            float shoreBreakup = (shoreNoise - 0.5) * 0.045 + (shoreFine - 0.5) * 0.012;
            shoreEdgeFade = 1.0 - smoothstep(0.91 + shoreBreakup, 1.0, radial);
        }
    } else if (u_waterShape == 3) {
        // Clean linear feathering from depth = 0.002m to 0.12m eliminates jagged pixel steps
        shoreEdgeFade = smoothstep(0.002, 0.12, waterDepth);
    } else {
        shoreEdgeFade = smoothstep(0.002, 0.035, waterDepth);
    }
    surfaceAlpha *= shoreEdgeFade;

    float alpha = clamp(surfaceAlpha + brokenFoam * 0.26 * shoreEdgeFade, 0.0, 0.90);

    // ── 9. ENERGY-CONSERVING COMPOSITION ─────────────────────────────────────
    vec3 shallowTint = (u_waterShape == 3) ? u_shallowColor : mix(u_shallowColor, vec3(0.14, 0.52, 0.50), 0.45);
    vec3 deepTint    = (u_waterShape == 3) ? u_deepColor    : mix(u_deepColor,    vec3(0.04, 0.26, 0.34), 0.52);

    vec3 depthBlend = clamp(mix(vec3(1.0 - smoothstep(0.04, 0.82, waterDepth)), transmittance, 0.60), 0.0, 1.0);
    vec3 waterVolumeColor = mix(deepTint, shallowTint, depthBlend);
    waterVolumeColor *= (0.58 + u_ambientColor * 0.24 + effectiveLight * 0.11);

    // Energy-conserving Fresnel blend: transmitted volume + subsurface terms attenuate as reflection rises
    float reflectWeight = clamp(fresnel * 0.62 + 0.05, 0.0, 0.82);
    vec3 underwaterTerms = waterVolumeColor + causticLight + sssLight + waveLighting;
    vec3 color = mix(underwaterTerms, reflectedSky, reflectWeight);

    color += waveHighlight + sunGlint + waveReflection;
    color += effectiveLight * vec3(0.62, 0.76, 0.84) * clamp(waveCurvature, -0.20, 0.20);

    vec3 frothColor = mix(u_foamColor, vec3(1.0), 0.14) * (0.72 + effectiveLight * 0.14);
    color = mix(color, frothColor, brokenFoam * 0.85);

    // VFX Point Light response
    color += VFXLights_Accumulate(fragPosition, normal, u_shallowColor) * 0.65;

    finalColor = vec4(color, alpha);
}