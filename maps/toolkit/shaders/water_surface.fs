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
uniform vec4 u_obstacle0;
uniform vec4 u_obstacle1;
uniform vec4 u_obstacle2;
uniform vec4 u_obstacle3;
uniform vec4 u_obstacle4;
uniform vec4 u_obstacle5;
uniform vec4 u_obstacle6;
uniform vec4 u_obstacle7;
uniform vec4 u_obstacle8;
uniform vec4 u_obstacle9;
uniform vec4 u_obstacle10;
uniform vec4 u_obstacle11;

out vec4 finalColor;

void EvaluateObstacleWake(vec4 obs, vec2 worldXZ, vec2 flowDir, float time,
                          inout vec2 ioSlope, inout float ioFoam)
{
    if (obs.z <= 0.01) return;
    vec2 toFrag = worldXZ - obs.xy;
    float d2 = dot(toFrag, toFrag);
    float maxReach = obs.z * 2.85;
    if (d2 >= maxReach * maxReach || d2 < 1e-6) return;

    float invD = inversesqrt(d2);
    float d = d2 * invD;
    vec2 dir = toFrag * invD;

    // Band around stone perimeter (d ~ 0.40 * r .. 2.65 * r)
    float normR = d / obs.z;
    if (normR < 0.38) return;

    // Asymmetric downstream hydraulic wake: bow cushion upstream + Kelvin V-chevron downstream
    float alongFlow = dot(toFrag, flowDir) / obs.z;
    float acrossFlow = abs(toFrag.x * flowDir.y - toFrag.y * flowDir.x) / obs.z;

    // 1. Upstream bow cushion (smooth compression mound, no concentric rings)
    float bowZone = smoothstep(-1.4, -0.65, alongFlow) * (1.0 - smoothstep(-0.65, -0.35, alongFlow));
    float bowCushion = bowZone * exp(-acrossFlow * acrossFlow * 2.5) * 0.20;

    // 2. Downstream parabolic Kelvin wake
    float downReach = max(alongFlow - 0.35, 0.0);
    float vSpread = sqrt(downReach) * 0.62;
    float wakeDist = abs(acrossFlow - vSpread);
    float vWave = cos(downReach * 8.0 - time * 4.8) * 0.5 + 0.5;
    float kelvinWake = exp(-wakeDist * 4.2) * (1.0 - smoothstep(0.5, 3.2, downReach)) * vWave;

    float strength = clamp(obs.w, 0.15, 0.90);
    ioSlope += (dir * 0.06 + flowDir * 0.10) * (kelvinWake + bowCushion) * strength;
    float foam = (bowCushion * 0.32 + kelvinWake * 0.36 * smoothstep(0.0, 1.4, downReach)) * strength;
    ioFoam = max(ioFoam, foam);
}

float EvaluateObstacleWaterDepth(vec4 obs, vec2 worldXZ, float inDepth)
{
    // Natural bathymetry preserved: obstacles are rendered as true 3D meshes with depth.
    // Artificial analytical circular depth clamping removed to eliminate black halos and transparency holes.
    return inDepth;
}

void main()
{
    // [VULKAN / SPIR-V FIX]: Tính toàn bộ đạo hàm không gian màn hình (dFdx/dFdy) ở đầu hàm main()
    // trong luồng đồng nhất (uniform control flow) trước mọi lệnh rẽ nhánh / discard.
    vec2 xzDx = dFdx(fragWorldXZ);
    vec2 xzDy = dFdy(fragWorldXZ);
    vec2 lakeDx = dFdx(fragLakeCoord) * 0.5;
    vec2 lakeDy = dFdy(fragLakeCoord) * 0.5;

    float t = mod(u_time * u_waveSpeed, 6283.185307);

    // Continuous analytical radial bathymetry and shoreline damping
    float radial = 0.0;
    float shoreFade = fragShoreFade;
    float radialEdgeFade = 1.0;
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
        if (radial > 0.84) {
            float shoreNoise = textureGrad(texture0, fragWorldXZ * 0.72, xzDx * 0.72, xzDy * 0.72).r;
            float shoreFine  = textureGrad(texture0, fragWorldXZ * 2.1,  xzDx * 2.1,  xzDy * 2.1).r;
            float shoreBreakup = (shoreNoise - 0.5) * 0.045 + (shoreFine - 0.5) * 0.012;
            radialEdgeFade = 1.0 - smoothstep(0.91 + shoreBreakup, 1.0, radial);
            if (radialEdgeFade < 0.002) discard;
        }
        shoreFade = 1.0 - smoothstep(0.75, 0.98, radial);
    }

    // ── 1. WAVE NORMALS & MULTI-PHASE FLOW (SIMD Vectorized) ────────────────
    const vec2 d0 = vec2(0.707, 0.707);
    const vec2 d1 = vec2(-0.612, 0.791);
    const vec2 d2 = vec2(0.866, -0.500);
    const vec2 d3 = vec2(-0.382, -0.924);

    vec2 effectiveFlow = u_flowVelocity;
    if (u_waterShape == 3 && dot(effectiveFlow, effectiveFlow) < 0.000025) {
        effectiveFlow = vec2(0.14, 0.22);
    }
    float effFlowLenSq = dot(effectiveFlow, effectiveFlow);
    vec2 normFlowDir = (effFlowLenSq > 1e-6) ? (effectiveFlow * inversesqrt(effFlowLenSq)) : vec2(0.55, 0.83);
    vec2 pFlow = fragWorldXZ - effectiveFlow * mod(u_time * 0.45, 4096.0);

    vec2 warp = vec2(sin(pFlow.y * 0.55 + t * 0.35), cos(pFlow.x * 0.55 - t * 0.28)) * 0.45;
    vec2 pw = pFlow + warp;

    vec4 pWave = vec4(
        dot(pw, d0) * u_waveScale + t,
        dot(pw, d1) * (u_waveScale * 1.58) - t * 1.25,
        dot(pw, d2) * (u_waveScale * 2.14) + t * 0.85,
        dot(pw, d3) * (u_waveScale * 2.92) - t * 0.62
    );

    vec4 cosP = cos(pWave);
    vec4 sinP = sin(pWave);

    vec2 slope = (d0 * (cosP.x * 0.38) + d1 * (cosP.y * 0.32) + d2 * (cosP.z * 0.20) + d3 * (cosP.w * 0.15))
               * (u_waveScale * u_waveHeight * 1.75 * shoreFade);

    vec3 viewVec = u_viewPos - fragPosition;
    float distSq = dot(viewVec, viewVec);
    float detailDistFade = 1.0 / (1.0 + distSq * 0.0015);
    float dualDetailFade = 1.0 - smoothstep(784.0, 1225.0, distSq);

    float flowSpeedSq = dot(u_flowVelocity, u_flowVelocity);
    vec2 detailSlope = vec2(0.0);
    const vec2 eps = vec2(0.006, 0.0);
    vec2 detDx0 = xzDx * u_detailScale;
    vec2 detDy0 = xzDy * u_detailScale;

    if (flowSpeedSq > 0.000025) {
        float invFlowSpeed = inversesqrt(flowSpeedSq);
        float flowSpeed = flowSpeedSq * invFlowSpeed;
        vec2 flowDir = u_flowVelocity * invFlowSpeed;
        float flowProgress = u_time * flowSpeed * 0.22;
        float phase0 = fract(flowProgress);
        float phase1 = fract(flowProgress + 0.5);
        float flowWeight = abs(2.0 * phase0 - 1.0);

        vec2 baseUV = fragWorldXZ * u_detailScale;
        vec2 uv0 = baseUV + flowDir * phase0;
        float n0 = textureGrad(texture0, uv0, detDx0, detDy0).r;
        vec2 s0 = vec2(textureGrad(texture0, uv0 + eps.xy, detDx0, detDy0).r,
                       textureGrad(texture0, uv0 + eps.yx, detDx0, detDy0).r) - n0;

        if (distSq < 1225.0) {
            vec2 uv1 = baseUV + flowDir * phase1;
            float n1 = textureGrad(texture0, uv1, detDx0, detDy0).r;
            vec2 s1 = vec2(textureGrad(texture0, uv1 + eps.xy, detDx0, detDy0).r,
                           textureGrad(texture0, uv1 + eps.yx, detDx0, detDy0).r) - n1;
            detailSlope = mix(s0, s1, flowWeight * dualDetailFade);
        } else {
            detailSlope = s0;
        }
    } else {
        vec2 detailUv0 = fragWorldXZ * u_detailScale + fract(vec2(t * 0.011, -t * 0.007));
        float d0_val = textureGrad(texture0, detailUv0, detDx0, detDy0).r;
        vec2 d0_grad = vec2(textureGrad(texture0, detailUv0 + eps.xy, detDx0, detDy0).r,
                            textureGrad(texture0, detailUv0 + eps.yx, detDx0, detDy0).r) - d0_val;

        if (distSq < 1225.0) {
            float scale1 = u_detailScale * 0.63;
            vec2 detDx1 = vec2(-xzDx.y, xzDx.x) * scale1;
            vec2 detDy1 = vec2(-xzDy.y, xzDy.x) * scale1;
            vec2 detailUv1 = vec2(-fragWorldXZ.y, fragWorldXZ.x) * scale1
                           + fract(vec2(-t * 0.006, t * 0.009));
            float d1_val = textureGrad(texture0, detailUv1, detDx1, detDy1).r;
            vec2 d1_grad = vec2(textureGrad(texture0, detailUv1 + eps.xy, detDx1, detDy1).r,
                                textureGrad(texture0, detailUv1 + eps.yx, detDx1, detDy1).r) - d1_val;
            vec2 blendedGrad = d0_grad * 0.72 + vec2(d1_grad.y, -d1_grad.x) * 0.28;
            detailSlope = mix(d0_grad, blendedGrad, dualDetailFade);
        } else {
            detailSlope = d0_grad;
        }
    }

    slope += detailSlope * (u_detailStrength * 2.40 * shoreFade * detailDistFade);

    // Conditional wave-field sampling (2D Shallow-Water PDE wave grid)
    vec2 interactSlope = vec2(0.0);
    float waveCurvature = 0.0;
    float wakeCrestHighlight = 0.0;
    float dynamicWakeFoam = 0.0;

    if (u_waveFieldEnabled > 0) {
        vec2 waveUv = fragLakeCoord * 0.5 + 0.5;
        float gridFade = 1.0;
        if (u_waterShape == 3) {
            vec2 edge = smoothstep(vec2(0.72), vec2(0.96), abs(fragLakeCoord));
            gridFade = (1.0 - edge.x) * (1.0 - edge.y);
        }
        vec4 waveField = textureGrad(u_waveFieldTex, waveUv, lakeDx, lakeDy);
        interactSlope = (waveField.rg - 0.5) * (2.45 * gridFade);
        waveCurvature = (waveField.a - 0.5) * (5.0 * gridFade);
        wakeCrestHighlight = max(0.0, waveField.b - 0.5) * (0.52 * gridFade);
        dynamicWakeFoam = clamp((wakeCrestHighlight * 1.6 + max(0.0, waveCurvature) * 0.22 - 0.12) * shoreFade * gridFade, 0.0, 0.65);
    }

    // Evaluate analytical expanding ripple rings (u_rippleRings[0..3])
    for (int r = 0; r < 4; r++) {
        vec4 ring = u_rippleRings[r];
        vec4 rParam = u_rippleParams[r];
        if (ring.z > 0.05 && rParam.x > 0.001) {
            vec2 toRing = fragWorldXZ - ring.xy;
            float rDistSq = dot(toRing, toRing);
            float maxR = ring.z;
            if (rDistSq < maxR * maxR && rDistSq > 1e-6) {
                float invR = inversesqrt(rDistSq);
                float rDist = rDistSq * invR;
                float band = abs(rDist - ring.w);
                float wl = max(rParam.y, 0.15);
                if (band < wl * 1.8) {
                    float env = (1.0 - band / (wl * 1.8)) * rParam.x;
                    float wavePhase = (rDist - ring.w) * (6.2831853 / wl);
                    interactSlope += (toRing * invR) * (cos(wavePhase) * env * 0.35);
                    dynamicWakeFoam = max(dynamicWakeFoam, max(sin(wavePhase), 0.0) * env * 0.28);
                }
            }
        }
    }

    // Evaluate river stone obstacles (prominent separated boulders within 32m)
    float obstacleFoam = 0.0;
    if (distSq < 1024.0 && u_obstacle0.z > 0.01) {
        EvaluateObstacleWake(u_obstacle0, fragWorldXZ, normFlowDir, t, interactSlope, obstacleFoam);
        EvaluateObstacleWake(u_obstacle1, fragWorldXZ, normFlowDir, t, interactSlope, obstacleFoam);
        EvaluateObstacleWake(u_obstacle2, fragWorldXZ, normFlowDir, t, interactSlope, obstacleFoam);
        EvaluateObstacleWake(u_obstacle3, fragWorldXZ, normFlowDir, t, interactSlope, obstacleFoam);
        EvaluateObstacleWake(u_obstacle4, fragWorldXZ, normFlowDir, t, interactSlope, obstacleFoam);
        EvaluateObstacleWake(u_obstacle5, fragWorldXZ, normFlowDir, t, interactSlope, obstacleFoam);
        if (u_obstacle6.z > 0.01) {
            EvaluateObstacleWake(u_obstacle6,  fragWorldXZ, normFlowDir, t, interactSlope, obstacleFoam);
            EvaluateObstacleWake(u_obstacle7,  fragWorldXZ, normFlowDir, t, interactSlope, obstacleFoam);
            EvaluateObstacleWake(u_obstacle8,  fragWorldXZ, normFlowDir, t, interactSlope, obstacleFoam);
            EvaluateObstacleWake(u_obstacle9,  fragWorldXZ, normFlowDir, t, interactSlope, obstacleFoam);
            EvaluateObstacleWake(u_obstacle10, fragWorldXZ, normFlowDir, t, interactSlope, obstacleFoam);
            EvaluateObstacleWake(u_obstacle11, fragWorldXZ, normFlowDir, t, interactSlope, obstacleFoam);
        }
        dynamicWakeFoam = max(dynamicWakeFoam, obstacleFoam * shoreFade);
    }

    // Direct player/entity water interactor wake
    float submerged = (u_waterSubmerged > 0.0) ? u_waterSubmerged : u_waterInteractor.y;
    if (u_waterRadius > 0.01 && submerged > 0.01) {
        vec2 toInteractor = fragWorldXZ - u_waterInteractor.xz;
        float iDistSq = dot(toInteractor, toInteractor);
        float activeRad = u_waterRadius * 2.8;

        if (iDistSq < activeRad * activeRad && iDistSq > 1e-5) {
            float invIDist = inversesqrt(iDistSq);
            float iDist = iDistSq * invIDist;
            float normDist = iDist / u_waterRadius;
            float speed = length(u_waterVelocity.xz);
            float speedFactor = clamp(speed * 0.35 + 0.15, 0.0, 1.0);

            float outerWakeFade = 1.0 - smoothstep(1.2, 2.6, normDist);
            float innerFoamFade = 1.0 - smoothstep(0.3, 1.4, normDist);
            float bowEnv = exp(-normDist * 1.5) * speedFactor * clamp(submerged, 0.0, 1.5) * outerWakeFade;
            vec2 iDir = toInteractor * invIDist;

            // Alive & dynamic: breathing circular contact ripples & subtle displacement even when stationary
            float standRipple = sin(normDist * 15.0 - t * 4.2) * exp(-normDist * 2.2);
            float contactRing = smoothstep(0.35, 0.65, normDist) * (1.0 - smoothstep(0.65, 1.35, normDist));
            float wakeEnv = (bowEnv * 0.35 + standRipple * 0.22 * clamp(submerged, 0.0, 1.0) * outerWakeFade);
            interactSlope += iDir * wakeEnv;
            dynamicWakeFoam = max(dynamicWakeFoam, (innerFoamFade * bowEnv * 0.35 + contactRing * 0.24 * clamp(submerged, 0.0, 1.0)) * shoreFade);
        }
    }

    slope += interactSlope * shoreFade;
    float slopeLenSq = dot(slope, slope);
    vec3 normal = vec3(-slope.x, 1.0, -slope.y) * inversesqrt(1.0 + slopeLenSq);

    // ── 2. DEPTH & BATHYMETRY ESTIMATION (h <= 1.5m) ─────────────────────────
    float waterDepth = 0.0;
    bool hasValidDepth = false;

    if (u_waterShape == 3) {
        waterDepth = max(0.0, fragWaterDepth);
        hasValidDepth = true;
    } else if (u_waterShape == 0) {
        float rad16 = radial * mix(1.0, radial, 0.6);
        waterDepth = u_maxDepth * (1.0 - rad16) + 0.015;
        hasValidDepth = true;
    }



    // Hardware scene depth buffer refinement (detects submerged stepping stones & exact bank contact)
    if (u_hasDepthTex > 0 && u_resolution.x > 1.0 && u_resolution.y > 1.0) {
        vec2 screenUV = gl_FragCoord.xy / u_resolution;
        float sceneLinear = textureLod(u_cameraDepthTex, screenUV, 0.0).r;
        float ndc = gl_FragCoord.z * 2.0 - 1.0;
        float fragLinear = 2000.0 / max(1001.0 - ndc * 999.0, 0.001);

        if (sceneLinear > 1.01 && sceneLinear >= fragLinear - 0.04) {
            float hwDepth = clamp(sceneLinear - fragLinear, 0.0, u_maxDepth);
            if (hasValidDepth) {
                // Combine vertex bathymetry with hardware depth so submerged rocks & banks shallow the water
                waterDepth = min(waterDepth, hwDepth);
            } else {
                waterDepth = hwDepth;
                hasValidDepth = true;
            }
        }
    }

    if (!hasValidDepth) {
        if (u_waterShape == 1) {
            float edgeDist = min(1.0 - abs(fragLakeCoord.x), 1.0 - abs(fragLakeCoord.y));
            waterDepth = clamp(smoothstep(0.0, 0.35, edgeDist) * u_maxDepth, 0.0, u_maxDepth);
        } else if (u_waterShape == 2) {
            float channelDist = clamp(1.0 - abs(fragLakeCoord.y), 0.0, 1.0);
            waterDepth = clamp(sin(channelDist * 1.5707963) * u_maxDepth, 0.0, u_maxDepth);
        }
    }

    // ── 3. OPTICAL BEER-LAMBERT TRANSMITTANCE & WATER COLUMN ─────────────────
    vec3 viewDir = viewVec * inversesqrt(max(distSq, 1e-8));
    float lLenSq = dot(u_lightDir, u_lightDir);
    vec3 L = (lLenSq > 1e-8) ? (u_lightDir * inversesqrt(lLenSq)) : vec3(0.0, 1.0, 0.0);
    float zenithDamp = 1.0 / (1.0 + max(L.y - 0.36, 0.0) * 0.85);
    vec3 effectiveLight = u_lightColor * zenithDamp;
    float lightPeak = max(max(effectiveLight.r, effectiveLight.g), effectiveLight.b);
    if (lightPeak > 1.35) {
        effectiveLight *= (1.35 + (lightPeak - 1.35) * 0.35) / lightPeak;
    }

    float NdotV = max(dot(normal, viewDir), 0.001);
    float opticalPath = waterDepth / max(NdotV, 0.32);

    // Beer-Lambert spectral attenuation
    vec3 absorptionCoeff = max(u_absorption, vec3(0.52, 0.18, 0.10));
    vec3 transmittance = exp(-absorptionCoeff * opticalPath);

    // ── 4. WATER CAUSTICS (Refracted Dual-Layer Photon Focusing) ─────────────
    float depthFadeCaustic = smoothstep(0.008, 0.12, waterDepth) * (1.0 - smoothstep(0.75, 1.45, waterDepth) * 0.50);
    vec3 causticLight = vec3(0.0);
    if (depthFadeCaustic * u_causticsStrength > 0.001) {
        vec2 causticRefract = normal.xz * clamp(waterDepth * 0.25, 0.015, 0.18);
        vec2 causticBase = fragWorldXZ + causticRefract;
        float cScale0 = u_causticsScale * 0.32;
        float cScale1 = u_causticsScale * 0.45;
        vec2 causticUv0 = causticBase * cScale0 + fract(vec2(t * 0.038, t * 0.024));
        vec2 causticUv1 = causticBase * cScale1 + fract(vec2(-t * 0.029, t * 0.043));
        float c0 = textureGrad(u_causticTex, causticUv0, xzDx * cScale0, xzDy * cScale0).r;
        float c1 = textureGrad(u_causticTex, causticUv1, xzDx * cScale1, xzDy * cScale1).r;
        float causticMin = min(c0, c1) * 2.2;
        float causticWave = causticMin * sqrt(max(causticMin, 0.0));

        float causticIntensity = smoothstep(0.46, 0.86, causticWave) *
                                 u_causticsStrength * 0.095 * depthFadeCaustic;
        causticLight = effectiveLight * (causticIntensity * transmittance);
    }

    // ── 5. SINGLE-LAYER WATER SUBSURFACE SCATTERING (Wave-Crest SSS) ─────────
    float waveFacet = dot(sinP, vec4(0.42, 0.30, 0.18, 0.10));
    vec3 rawScatterDir = L + vec3(normal.x, 0.0, normal.z) * 0.65;
    vec3 scatterLightDir = rawScatterDir * inversesqrt(max(dot(rawScatterDir, rawScatterDir), 1e-8));
    float backlight = max(0.0, dot(viewDir, -vec3(scatterLightDir.x, scatterLightDir.y * 0.25, scatterLightDir.z)));
    float bl2 = backlight * backlight;
    float sssFactor = (bl2 * backlight * 0.78 + 0.14) * u_scatterCoeff;
    float crestThickness = clamp((1.0 - normal.y * 0.88) + max(0.0, waveFacet) * 0.32 + wakeCrestHighlight * 0.75, 0.0, 1.0);
    vec3 sssLight = effectiveLight * u_scatterColor * (sssFactor * (1.0 - exp(-2.0 * waterDepth)) * crestThickness * 0.75);

    // ── 6. HYBRID FRESNEL & ENVIRONMENT REFLECTION (Energy-Conserving) ───────
    const float F0 = 0.04;
    float omv = 1.0 - NdotV;
    float omv2 = omv * omv;
    float fresnel = F0 + (1.0 - F0) * (omv2 * omv2 * omv);

    vec3 reflectDir = reflect(-viewDir, normal);
    float skyAngle = clamp(reflectDir.y * 0.85 + 0.15, 0.0, 1.0);
    float sunAzimuth = clamp(dot(normalize(vec2(reflectDir.x, reflectDir.z) + vec2(1e-5)),
                                 normalize(L.xz + vec2(1e-5))) * 0.5 + 0.5, 0.0, 1.0);
    vec3 skyZenith  = mix(vec3(0.18, 0.42, 0.70), u_ambientColor * 1.18, 0.45);
    vec3 skyHorizon = mix(u_ambientColor * 1.32 + vec3(0.10, 0.12, 0.14),
                          effectiveLight * 0.68, 0.24 + 0.34 * sunAzimuth * sunAzimuth);
    vec3 reflectedSky = mix(skyHorizon, skyZenith, skyAngle);

    // Luminous emerald-moss waterline contact reflection along the riverbank
    vec3 bankCanopyReflect = vec3(0.16, 0.34, 0.18) * (0.82 + u_ambientColor.g * 0.35);
    float bankReflectMask = (1.0 - smoothstep(0.01, 0.22, waterDepth)) * (1.0 - skyAngle * 0.55);
    reflectedSky = mix(reflectedSky, bankCanopyReflect, bankReflectMask * 0.42);

    float sunReflect = max(0.0, dot(reflectDir, L));
    float sr2 = sunReflect * sunReflect;
    float sr4 = sr2 * sr2;
    float sr8 = sr4 * sr4;
    reflectedSky += effectiveLight * (sr8 * sr8 * 0.14);

    float crest = smoothstep(0.48, 0.92, waveFacet) * 0.042;
    vec3 waveHighlight = reflectedSky * (max(waveFacet, 0.0) * 0.035 + crest + wakeCrestHighlight * 0.38);

    vec3 halfSum = L + viewDir;
    vec3 halfDir = halfSum * inversesqrt(max(dot(halfSum, halfSum), 1e-6));
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
    float glintSharp = nh128 * nh128;
    float waveSlopeEnergy = clamp(sqrt(slopeLenSq) * 14.0, 0.14, 1.0);
    float glintMid = (nh96 * 0.24 + nh64 * 0.10) * waveSlopeEnergy;

    // Soft-knee highlight compression so golden-hour water glints sparkle without clipping white
    vec3 rawGlint = effectiveLight * (glintSharp * 2.10 + glintMid * 0.52) * (0.20 + fresnel * 0.80) * detailDistFade;
    vec3 sunGlint = rawGlint / (vec3(1.0) + rawGlint * 0.45);
    float disturbance = clamp(length(interactSlope) * 2.2, 0.0, 1.0);
    vec3 waveReflection = reflectedSky * (nh12 * disturbance * 0.34);

    vec2 rawAzimuth = L.xz + vec2(0.42, -0.34);
    vec2 horizAzimuth = rawAzimuth * inversesqrt(max(dot(rawAzimuth, rawAzimuth), 1e-8));
    float waveSlopeContrast = dot(normal.xz, horizAzimuth) * 2.2;
    vec3 waveLighting = effectiveLight * clamp(waveSlopeContrast + (dot(normal, L) - L.y), -0.20, 0.22) * 0.24;

    // ── 7. SHORELINE & SUBMERGED CONTACT WATERLINE MENISCUS ──────────────────
    float foamDist = clamp(u_foamThreshold, 0.06, 0.13);
    float contactMask = clamp(1.0 - waterDepth / foamDist, 0.0, 1.0);
    float brokenFoam = 0.0;

    if (contactMask > 0.001 || dynamicWakeFoam > 0.001) {
        float meniscusLip = smoothstep(0.002, 0.018, waterDepth) * (1.0 - smoothstep(0.022, 0.085, waterDepth));
        vec2 foamUV = fragWorldXZ * 0.52 - normFlowDir * fract(t * 0.045);
        vec2 foamUV2 = fragWorldXZ * 1.35 + normFlowDir * fract(t * 0.075);
        float foamNoise = textureGrad(texture0, foamUV, xzDx * 0.52, xzDy * 0.52).r;
        float foamFine  = textureGrad(texture0, foamUV2, xzDx * 1.35, xzDy * 1.35).r;
        float foamWeb   = foamNoise * 0.62 + foamFine * 0.38;
        float contactRipple = sin(waterDepth * 36.0 - t * 4.2) * exp(-waterDepth * 7.5);

        float wakeFoamTex = dynamicWakeFoam * smoothstep(0.24, 0.70, foamWeb + dynamicWakeFoam * 0.45);
        if (u_waterShape == 3) {
            // Delicate shoreline meniscus foam, lapping bank ripples, and crisp stone wake lacing
            float shoreLace = meniscusLip * 0.38 * smoothstep(0.44, 0.74, foamWeb)
                            + max(contactRipple, 0.0) * 0.16 * contactMask * smoothstep(0.48, 0.78, foamWeb);
            brokenFoam = clamp(max(shoreLace, wakeFoamTex), 0.0, 0.70);
        } else {
            brokenFoam = smoothstep(0.46, 0.78, contactMask * 0.60 + (foamWeb - 0.5) * 0.72);
            brokenFoam = max(brokenFoam, meniscusLip * 0.38 * smoothstep(0.36, 0.68, foamWeb));
            brokenFoam += max(contactRipple, 0.0) * 0.14 * contactMask;
            brokenFoam = clamp(max(brokenFoam, wakeFoamTex), 0.0, 1.0);
        }
    }

    // ── 8. CRYSTAL CLEAR SEE-THROUGH WATER TRANSPARENCY ──────────────────────
    // Shallow water (0..30cm) stays crystal-clear so riverbed pebbles, sand ripples & caustics
    // read clearly through the water column, while deeper pools transition to rich emerald-jade.
    float depthOpacity = (u_waterShape == 3)
        ? (1.0 - exp(-2.05 * waterDepth))
        : (1.0 - exp(-2.20 * waterDepth));
    float waterColumnAlpha = (u_waterShape == 3)
        ? clamp(depthOpacity * 0.72, 0.0, 0.82)
        : clamp(depthOpacity * 0.75, 0.0, 0.85);
    float glintAlphaBoost = clamp(glintSharp * 0.38, 0.0, 0.26);
    float surfaceAlpha = (fresnel * 0.66) + waterColumnAlpha + glintAlphaBoost;

    float shoreEdgeFade = radialEdgeFade;
    if (u_waterShape == 3) {
        shoreEdgeFade = smoothstep(0.002, 0.125, fragWaterDepth);
    } else if (u_waterShape != 0) {
        shoreEdgeFade = smoothstep(0.002, 0.055, waterDepth);
    }
    surfaceAlpha *= shoreEdgeFade;

    float alpha = clamp(surfaceAlpha + brokenFoam * 0.58 * smoothstep(0.001, 0.035, waterDepth), 0.0, 0.92);
    if (alpha < 0.002) discard;

    // ── 9. ENERGY-CONSERVING COMPOSITION ─────────────────────────────────────
    vec3 shallowTint = mix(u_shallowColor, vec3(0.24, 0.64, 0.58), 0.35);
    vec3 deepTint    = mix(u_deepColor,    vec3(0.06, 0.32, 0.40), 0.50);

    vec3 depthBlend = clamp(mix(vec3(1.0 - smoothstep(0.06, 0.95, waterDepth)), transmittance, 0.68), 0.0, 1.0);
    vec3 waterVolumeColor = mix(deepTint, shallowTint, depthBlend);
    waterVolumeColor *= (0.80 + u_ambientColor * 0.35 + effectiveLight * 0.24);

    float shoreFadeOptical = (u_waterShape == 3) ? smoothstep(0.005, 0.18, fragWaterDepth) : 1.0;

    float reflectWeight = (u_waterShape == 3)
        ? clamp((fresnel * 0.68 + 0.10) * shoreFadeOptical, 0.0, 0.78)
        : clamp(fresnel * 0.72 + 0.12, 0.0, 0.85);
    vec3 underwaterTerms = waterVolumeColor + causticLight * 1.35 + sssLight + waveLighting;
    vec3 color = mix(underwaterTerms, reflectedSky, reflectWeight);

    color += waveHighlight + sunGlint + waveReflection;
    color += effectiveLight * vec3(0.62, 0.76, 0.84) * clamp(waveCurvature, -0.20, 0.20);

    vec3 frothColor = mix(u_foamColor, vec3(0.98, 0.97, 0.94), 0.35) * (0.76 + effectiveLight * 0.24);
    color = mix(color, frothColor, brokenFoam * 0.78);

    // VFX Point Light response
    color += VFXLights_Accumulate(fragPosition, normal, shallowTint) * 0.65;

    finalColor = vec4(color, alpha);
}