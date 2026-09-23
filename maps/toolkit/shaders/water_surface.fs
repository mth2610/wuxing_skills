#version 330
#include "core/shaders/common/vfx_lights.glsl"

in vec3 fragPosition;
in vec2 fragLakeCoord;
in vec2 fragWorldXZ;
in vec4 fragScreenPos;
in float fragShoreFade;

uniform sampler2D texture0;         // Noise / microdetail texture
uniform sampler2D u_causticTex;     // Dual-phase caustics texture
uniform sampler2D u_cameraDepthTex; // Linear scene depth texture (if available)

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
uniform vec3 u_waterInteractor;  // (x, y, z) position of the submerged body
uniform vec3 u_waterVelocity;    // (vx, vy, vz) movement velocity
uniform float u_waterRadius;     // Interactor body radius (m)
uniform float u_waterSubmerged;  // Submerged fraction [0.0 -> 1.0]

uniform vec4 u_rippleRings[4];   // (x, z, spawnTime, maxRadius)
uniform vec4 u_rippleParams[4];  // (amplitude, speed, wavelength, decay)

out vec4 finalColor;

void main()
{
    float t = u_time * u_waveSpeed;

    // ── 1. WAVE NORMALS & DUAL-PHASE FLOW ────────────────────────────────────
    vec2 d0 = vec2(0.82, 0.57);
    vec2 d1 = vec2(-0.31, 0.95);
    vec2 d2 = vec2(0.96, -0.18);
    vec2 flowOffset = u_flowVelocity * (u_time * 0.45);
    vec2 pFlow = fragWorldXZ - flowOffset;

    float p0 = dot(pFlow, d0) * u_waveScale + t;
    float p1 = dot(pFlow, d1) * u_waveScale * 1.73 - t * 1.31;
    float p2 = dot(pFlow, d2) * u_waveScale * 2.61 + t * 0.73;

    vec2 slope = cos(p0) * d0 * u_waveScale * 0.50;
    slope += cos(p1) * d1 * u_waveScale * 1.73 * 0.29;
    slope += cos(p2) * d2 * u_waveScale * 2.61 * 0.16;
    slope *= u_waveHeight * 1.85 * fragShoreFade;

    // Microdetail sampling: Dual-phase flow if stream, or dual-rotated if calm
    float flowSpeed = length(u_flowVelocity);
    vec2 detailSlope = vec2(0.0);

    if (flowSpeed > 0.005) {
        vec2 flowDir = normalize(u_flowVelocity);
        float flowProgress = u_time * flowSpeed * 0.22;
        float phase0 = fract(flowProgress);
        float phase1 = fract(flowProgress + 0.5);
        float flowWeight = abs(2.0 * phase0 - 1.0);

        vec2 uv0 = fragWorldXZ * u_detailScale + flowDir * phase0;
        vec2 uv1 = fragWorldXZ * u_detailScale + flowDir * phase1;

        float n0 = texture(texture0, uv0).r;
        float n1 = texture(texture0, uv1).r;
        float n0_dx = texture(texture0, uv0 + vec2(0.006, 0.0)).r - n0;
        float n0_dz = texture(texture0, uv0 + vec2(0.0, 0.006)).r - n0;
        float n1_dx = texture(texture0, uv1 + vec2(0.006, 0.0)).r - n1;
        float n1_dz = texture(texture0, uv1 + vec2(0.0, 0.006)).r - n1;

        vec2 s0 = vec2(n0_dx, n0_dz);
        vec2 s1 = vec2(n1_dx, n1_dz);
        detailSlope = mix(s0, s1, flowWeight);
    } else {
        vec2 detailUv0 = fragWorldXZ * u_detailScale + vec2(t * 0.011, -t * 0.007);
        vec2 detailUv1 = vec2(-fragWorldXZ.y, fragWorldXZ.x) * u_detailScale * 0.63
                       + vec2(-t * 0.006, t * 0.009);
        float d0_val = texture(texture0, detailUv0).r;
        float d1_val = texture(texture0, detailUv1).r;
        float dX0 = texture(texture0, detailUv0 + vec2(0.006, 0.0)).r - d0_val;
        float dZ0 = texture(texture0, detailUv0 + vec2(0.0, 0.006)).r - d0_val;
        float dX1 = texture(texture0, detailUv1 + vec2(0.006, 0.0)).r - d1_val;
        float dZ1 = texture(texture0, detailUv1 + vec2(0.0, 0.006)).r - d1_val;
        detailSlope = vec2(dX0, dZ0) * 0.72 + vec2(dZ1, -dX1) * 0.28;
    }

    slope += detailSlope * u_detailStrength * 2.40 * fragShoreFade;

    // ── 1.1 DYNAMIC SURFACE INTERACTION (Kelvin Wake & Ripples) ──────────────
    vec2 interactSlope = vec2(0.0);
    float dynamicWakeFoam = 0.0;
    float wakeCrestHighlight = 0.0;
    float ringCrestHighlight = 0.0;

    if (u_waterSubmerged > 0.01) {
        vec2 toFrag = fragWorldXZ - u_waterInteractor.xz;
        float r = length(toFrag);
        vec2 rDir = (r > 0.0001) ? (toFrag / r) : vec2(0.0, 1.0);
        float bodyRadius = max(u_waterRadius, 0.35);

        // A. Breathing / idle concentric ripples around submerged body
        if (r < 6.0) {
            float idleK = 12.0;
            float idlePhase = r * idleK - u_time * 5.0;
            float idleFalloff = exp(-max(0.0, r - bodyRadius) * 2.2) * u_waterSubmerged;
            interactSlope += rDir * cos(idlePhase) * idleFalloff * 0.28;
            wakeCrestHighlight += max(0.0, cos(idlePhase)) * idleFalloff * 0.35;

            // Contact waterline disturbance & meniscus foam around the body
            float contactDist = abs(r - bodyRadius);
            if (contactDist < 0.28) {
                float contactLip = smoothstep(0.28, 0.02, contactDist);
                dynamicWakeFoam = max(dynamicWakeFoam, contactLip * 0.95 * u_waterSubmerged);
            }
        }

        // B. Kelvin / V-shaped wake & bow wave when moving
        float speed = length(u_waterVelocity.xz);
        if (speed > 0.15) {
            vec2 moveDir = u_waterVelocity.xz / speed;
            vec2 sideDir = vec2(-moveDir.y, moveDir.x);

            float forward = dot(toFrag, moveDir);
            float side = dot(toFrag, sideDir);
            float absSide = abs(side);

            // 1. Bow wave in front of the moving body
            if (forward > -bodyRadius * 0.4 && forward < 3.2 && absSide < 2.5) {
                vec2 bowDelta = vec2(forward - bodyRadius * 0.5, absSide * 1.15);
                float bowDist = length(bowDelta);
                float bowEnv = exp(-bowDist * 2.0) * clamp(speed * 0.35, 0.0, 1.0) * u_waterSubmerged;
                float bowPhase = bowDist * 15.0 - u_time * (speed * 3.2 + 6.0);
                vec2 bowDir = normalize(toFrag + moveDir * 0.3);
                interactSlope += bowDir * (-sin(bowPhase)) * bowEnv * 0.42;

                float bowCrest = cos(bowPhase);
                wakeCrestHighlight += max(0.0, bowCrest) * bowEnv * 0.60;
                float bowFoam = smoothstep(0.15, 0.70, bowCrest) * bowEnv * 1.35;
                dynamicWakeFoam = max(dynamicWakeFoam, bowFoam);
            }

            // 2. Stern Kelvin V-wake arms behind the moving body
            if (forward < bodyRadius * 0.6 && forward > -12.0) {
                float trailDist = bodyRadius * 0.6 - forward; // trailDist >= 0
                float kelvinHalfWidth = trailDist * 0.38 + bodyRadius * 0.65; // ~19.5 degree half angle
                float armDist = abs(absSide - kelvinHalfWidth);

                float armWidth = 0.30 + trailDist * 0.07;
                float armProfile = exp(-(armDist * armDist) / (2.0 * armWidth * armWidth)) * exp(-trailDist * 0.16);
                float wakeAmp = armProfile * clamp(speed * 0.35, 0.0, 1.0) * u_waterSubmerged;

                float wakePhase = trailDist * 10.5 - absSide * 4.0 - u_time * (speed * 2.5 + 5.0);
                float wakeCrest = cos(wakePhase);

                vec2 wakeWaveDir = normalize(sideDir * sign(side) * 0.85 - moveDir * 0.55);
                interactSlope += wakeWaveDir * (-sin(wakePhase)) * wakeAmp * 0.48;
                wakeCrestHighlight += max(0.0, wakeCrest) * wakeAmp * 0.75;

                // Foam along the wake arm crests (breaking aerated foam)
                float crestFoam = smoothstep(0.05, 0.65, wakeCrest) * wakeAmp * 1.45;
                dynamicWakeFoam = max(dynamicWakeFoam, crestFoam);

                // 3. Transverse waves (chevrons inside the V)
                if (absSide < kelvinHalfWidth) {
                    float insideV = clamp(1.0 - absSide / max(kelvinHalfWidth, 0.1), 0.0, 1.0);
                    float transPhase = trailDist * 8.0 - u_time * (speed * 2.0 + 4.0);
                    float transAmp = exp(-trailDist * 0.22) * insideV * clamp(speed * 0.30, 0.0, 1.0) * u_waterSubmerged;
                    float transCrest = cos(transPhase);

                    interactSlope += (-moveDir) * (-sin(transPhase)) * transAmp * 0.30;
                    wakeCrestHighlight += max(0.0, transCrest) * transAmp * 0.40;

                    // Frothy churn trail in the center
                    float trailNoise = texture(texture0, fragWorldXZ * 0.55 + vec2(u_time * 0.03, -u_time * 0.02)).r;
                    float churn = exp(-absSide * 3.2) * exp(-trailDist * 0.25) * smoothstep(0.30, 0.65, trailNoise) * clamp(speed * 0.40, 0.0, 1.0) * u_waterSubmerged;
                    dynamicWakeFoam = max(dynamicWakeFoam, churn * 1.25);
                }
            }
        }
    }

    // ── 1.2 EXPANDING SHOCKWAVE RIPPLE RINGS ─────────────────────────────────
    for (int i = 0; i < 4; i++) {
        float spawnTime = u_rippleRings[i].z;
        if (spawnTime <= 0.0) continue;

        float age = u_time - spawnTime;
        float maxRadius = u_rippleRings[i].w;
        float waveSpeed = u_rippleParams[i].y;
        float maxLife = (waveSpeed > 0.01) ? (maxRadius / waveSpeed) : 3.5;

        if (age >= 0.0 && age < maxLife) {
            vec2 ringToFrag = fragWorldXZ - u_rippleRings[i].xy;
            float ringDist = length(ringToFrag);
            vec2 ringDir = (ringDist > 0.0001) ? (ringToFrag / ringDist) : vec2(0.0, 1.0);

            float currentRadius = waveSpeed * age;
            float deltaR = ringDist - currentRadius;
            float wavelength = max(u_rippleParams[i].z, 0.35);
            float amplitude = u_rippleParams[i].x;
            float decayCoeff = u_rippleParams[i].w;

            // Gaussian wave packet envelope
            float packetWidth = wavelength * 2.2;
            float packet = exp(-(deltaR * deltaR) / (2.0 * packetWidth * packetWidth));

            // Temporal & distance attenuation
            float lifeFade = 1.0 - (age / maxLife);
            float distFade = exp(-ringDist * decayCoeff * 0.20);
            float strength = amplitude * packet * lifeFade * distFade;

            if (strength > 0.001) {
                float waveK = 6.2831853 / wavelength;
                float phase = deltaR * waveK;
                float dWave = -sin(phase);
                interactSlope += ringDir * dWave * strength * 0.45;

                float ringCrest = cos(phase);
                ringCrestHighlight += max(0.0, ringCrest) * strength * 0.65;

                // Foam on expanding ring crest
                float ringFoam = smoothstep(0.10, 0.70, ringCrest) * strength * 1.50;
                dynamicWakeFoam = max(dynamicWakeFoam, ringFoam);
            }
        }
    }

    slope += interactSlope * fragShoreFade;
    vec3 normal = normalize(vec3(-slope.x, 1.0, -slope.y));

    // ── 2. DEPTH & BATHYMETRY ESTIMATION (h <= 1.3m) ─────────────────────────
    float waterDepth = 0.0;
    bool hasValidDepth = false;

    if (u_hasDepthTex > 0 && u_resolution.x > 1.0) {
        vec2 screenUV = gl_FragCoord.xy / u_resolution;
        float sceneLinear = texture(u_cameraDepthTex, screenUV).r;
        
        // Linearize gl_FragCoord.z matching MyBeginMode3D & depth_copy.fs (near=1.0, far=1000.0)
        float ndc = gl_FragCoord.z * 2.0 - 1.0;
        float fragLinear = (2000.0) / (1001.0 - ndc * 999.0);
        
        if (sceneLinear > 1.01 && sceneLinear >= fragLinear - 0.05) {
            waterDepth = clamp(sceneLinear - fragLinear, 0.0, u_maxDepth);
            hasValidDepth = true;
        }
    }

    if (!hasValidDepth) {
        // Analytical shape-based bathymetry fallback
        if (u_waterShape == 0) {
            float radial = length(fragLakeCoord);
            waterDepth = clamp((1.0 - radial * radial) * u_maxDepth, 0.0, u_maxDepth);
        } else if (u_waterShape == 1) {
            float edgeDist = min(1.0 - abs(fragLakeCoord.x), 1.0 - abs(fragLakeCoord.y));
            waterDepth = clamp(smoothstep(0.0, 0.35, edgeDist) * u_maxDepth, 0.0, u_maxDepth);
        } else if (u_waterShape == 2) {
            float channelDist = 1.0 - abs(fragLakeCoord.y);
            waterDepth = clamp(sin(channelDist * 1.5708) * u_maxDepth, 0.0, u_maxDepth);
        }
    }

    // ── 3. OPTICAL BEER-LAMBERT TRANSMITTANCE & WATER COLUMN ────────────────
    vec3 viewDir = normalize(u_viewPos - fragPosition);
    float NdotV = max(dot(normal, viewDir), 0.001);
    float opticalPath = waterDepth / max(NdotV, 0.25);
    vec3 transmittance = exp(-u_absorption * opticalPath);

    // ── 4. WATER CAUSTICS (Dual-Layer Photon Focusing) ───────────────────────
    vec2 causticUv0 = fragWorldXZ * u_causticsScale * 0.32 + vec2(t * 0.038, t * 0.024);
    vec2 causticUv1 = fragWorldXZ * u_causticsScale * 0.45 + vec2(-t * 0.029, t * 0.043);
    float c0 = texture(u_causticTex, causticUv0).r;
    float c1 = texture(u_causticTex, causticUv1).r;
    float causticWave = pow(min(c0, c1) * 2.2, 1.80);

    // Submerged photon focusing: caustics illuminate submerged geometry (legs, rocks, bed)
    float causticIntensity = causticWave * u_causticsStrength * (1.0 - exp(-3.2 * waterDepth));
    vec3 causticLight = u_lightColor * causticIntensity * 1.10;

    // ── 5. SINGLE-LAYER WATER SUBSURFACE SCATTERING (Backlight SSS) ──────────
    float backlight = max(0.0, dot(-u_lightDir, viewDir));
    float sssFactor = pow(backlight, 3.2) * u_scatterCoeff;
    float waveScatter = clamp(1.0 - normal.y * 0.8, 0.0, 1.0);
    vec3 sssLight = u_lightColor * u_scatterColor * sssFactor * (1.0 - exp(-2.4 * waterDepth)) * waveScatter;

    // ── 6. HYBRID FRESNEL & ENVIRONMENT REFLECTION ───────────────────────────
    float F0 = 0.02;
    float fresnel = F0 + (1.0 - F0) * pow(1.0 - NdotV, 5.0);

    vec3 reflectDir = reflect(-viewDir, normal);
    float skyAngle = clamp(reflectDir.y * 0.85 + 0.15, 0.0, 1.0);
    vec3 skyZenith = vec3(0.20, 0.40, 0.72) * (0.80 + u_ambientColor.b * 0.4);
    vec3 skyHorizon = vec3(0.46, 0.58, 0.68) * (0.70 + u_ambientColor.g * 0.3);
    vec3 reflectedSky = mix(skyHorizon, skyZenith, skyAngle);

    // Facet wave glints
    float waveFacet = sin(p0) * 0.52 + sin(p1) * 0.31 + sin(p2) * 0.17;
    float crest = smoothstep(0.48, 0.95, waveFacet) * 0.035;
    vec3 waveHighlight = reflectedSky * (waveFacet * 0.030 + crest);
    waveHighlight += reflectedSky * (wakeCrestHighlight * 0.45 + ringCrestHighlight * 0.55);

    // Multi-scale specular sun glint
    vec3 halfDir = normalize(u_lightDir + viewDir);
    float NdotH = max(dot(normal, halfDir), 0.0);
    float glintSharp = pow(NdotH, 256.0);
    float glintMid = pow(NdotH, 48.0) * 0.20;
    vec3 sunGlint = u_lightColor * (glintSharp * 3.5 + glintMid * 0.8) * (0.15 + fresnel * 0.85);

    // ── 7. SHORELINE & SUBMERGED CONTACT WATERLINE MENISCUS ───────────────────
    // Contact waterline: surface tension meniscus where water meets body/bank
    float foamDist = max(u_foamThreshold, 0.16);
    float contactMask = clamp(1.0 - waterDepth / foamDist, 0.0, 1.0);
    float meniscusLip = smoothstep(0.0, 0.035, waterDepth) * (1.0 - smoothstep(0.035, 0.12, waterDepth));

    float foamNoise = texture(texture0, fragWorldXZ * 0.45 + vec2(t * 0.022, -t * 0.016)).r;
    float contactRipple = sin(waterDepth * 36.0 - t * 4.8) * exp(-waterDepth * 6.5);
    float brokenFoam = smoothstep(0.26, 0.78, contactMask * 1.35 + (foamNoise - 0.5) * 0.65);
    brokenFoam = max(brokenFoam, meniscusLip * 0.90);
    brokenFoam += max(contactRipple, 0.0) * 0.35 * contactMask;
    brokenFoam = max(brokenFoam, dynamicWakeFoam);
    brokenFoam = clamp(brokenFoam, 0.0, 1.0);

    // ── 8. CRYSTAL CLEAR SEE-THROUGH WATER TRANSPARENCY ──────────────────────
    // Natural shallow water alpha:
    // Transparent near shore, building smoothly down the water column
    // Submerged geometry (character legs, lake bed) is visible yet unmistakably underwater
    float waterColumnAlpha = 0.32 + 0.42 * (1.0 - exp(-2.0 * waterDepth));
    float surfaceAlpha = fresnel * 0.82 + waterColumnAlpha + min(length(sunGlint) * 0.35, 0.60);

    // Soft boundary fade right at shore edge (< 1.2cm)
    float shoreEdgeFade = smoothstep(0.0005, 0.012, waterDepth);
    surfaceAlpha *= shoreEdgeFade;

    // Meniscus and contact foam sit firmly ON the surface — never dissolved!
    float alpha = clamp(surfaceAlpha + brokenFoam * 0.96, 0.0, 0.98);

    // ── 9. COMPOSITION ───────────────────────────────────────────────────────
    // Aquatic water column color (Beer-Lambert volumetric tint)
    vec3 shallowTint = mix(u_shallowColor, vec3(0.16, 0.64, 0.60), 0.50);
    vec3 deepTint = mix(u_deepColor, vec3(0.05, 0.34, 0.42), 0.50);
    vec3 waterVolumeColor = mix(shallowTint, deepTint, smoothstep(0.05, 0.75, waterDepth));
    waterVolumeColor *= (0.75 + u_ambientColor * 0.30 + u_lightColor * 0.20);

    // Mix surface reflection (sky) and water volume color based on Fresnel
    vec3 color = mix(waterVolumeColor, reflectedSky, fresnel * 0.68 + 0.24);
    color += causticLight;
    color += sssLight;
    color += waveHighlight;
    color += sunGlint;
    vec3 frothColor = mix(u_foamColor, vec3(1.0, 1.0, 1.0), 0.72);
    color = mix(color, frothColor, brokenFoam);

    // VFX Point Light response
    color += VFXLights_Accumulate(fragPosition, normal, u_shallowColor) * 0.65;

    finalColor = vec4(color, alpha);
}
