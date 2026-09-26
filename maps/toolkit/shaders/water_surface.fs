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

    // The shallow-water solver provides actual height derivatives and
    // curvature. All interaction shading comes from this propagated field.
    vec4 waveField = texture(u_waveFieldTex, fragLakeCoord * 0.5 + 0.5);
    vec2 interactSlope = u_waveFieldEnabled > 0 ?
        (waveField.rg - vec2(0.5)) * 2.45 : vec2(0.0);
    float waveCurvature = u_waveFieldEnabled > 0 ?
        (waveField.a - 0.5) * 5.0 : 0.0;
    float wakeCrestHighlight = u_waveFieldEnabled > 0 ?
        max(0.0, waveField.b - 0.5) * 0.52 : 0.0;
    float ringCrestHighlight = 0.0;
    float dynamicWakeFoam = 0.0;
    slope += interactSlope * fragShoreFade;
    vec3 normal = normalize(vec3(-slope.x, 1.0, -slope.y));

    // ── 2. DEPTH & BATHYMETRY ESTIMATION (h <= 1.3m) ─────────────────────────
    float waterDepth = 0.0;
    bool hasValidDepth = false;

    // The radial lake has a known bed profile. Screen-space ray distance to
    // that bed changes with camera pitch and used to move a visible contour
    // across the lake whenever the camera zoomed or jumped.
    if (u_waterShape == 0) {
        float radial = clamp(length(fragLakeCoord), 0.0, 1.0);
        waterDepth = u_maxDepth * (1.0 - pow(radial, 1.6)) + 0.015;
        hasValidDepth = true;
    }

    if (!hasValidDepth && u_hasDepthTex > 0 && u_resolution.x > 1.0) {
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
            waterDepth = clamp((1.0 - pow(radial, 1.6)) * u_maxDepth + 0.015, 0.0, u_maxDepth);
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
    float causticIntensity = smoothstep(0.55, 0.88, causticWave) *
                             u_causticsStrength * 0.055 * (1.0 - exp(-2.0 * waterDepth));
    vec3 causticLight = u_lightColor * causticIntensity;

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
    float disturbance = clamp(length(interactSlope) * 2.2, 0.0, 1.0);
    vec3 waveReflection = reflectedSky * pow(NdotH, 12.0) * disturbance * 0.43;
    vec3 waveLighting = u_lightColor *
        clamp(dot(normal, normalize(u_lightDir)) - u_lightDir.y, -0.20, 0.20) * 0.28;

    // ── 7. SHORELINE & SUBMERGED CONTACT WATERLINE MENISCUS ───────────────────
    // Contact waterline: surface tension meniscus where water meets body/bank
    float foamDist = max(u_foamThreshold, 0.16);
    float contactMask = clamp(1.0 - waterDepth / foamDist, 0.0, 1.0);
    float meniscusLip = smoothstep(0.0, 0.022, waterDepth) * (1.0 - smoothstep(0.022, 0.075, waterDepth));

    float foamNoise = texture(texture0, fragWorldXZ * 0.45 + vec2(t * 0.022, -t * 0.016)).r;
    float contactRipple = sin(waterDepth * 36.0 - t * 4.8) * exp(-waterDepth * 6.5);
    float brokenFoam = smoothstep(0.47, 0.78, contactMask * 0.59 + (foamNoise - 0.5) * 0.74);
    brokenFoam = max(brokenFoam, meniscusLip * 0.28);
    brokenFoam += max(contactRipple, 0.0) * 0.10 * contactMask;
    brokenFoam = max(brokenFoam, dynamicWakeFoam);
    brokenFoam = clamp(brokenFoam, 0.0, 1.0);

    // ── 8. CRYSTAL CLEAR SEE-THROUGH WATER TRANSPARENCY ──────────────────────
    // Natural shallow water alpha:
    // Transparent near shore, building smoothly down the water column
    // Submerged geometry (character legs, lake bed) is visible yet unmistakably underwater
    float waterColumnAlpha = 0.12 + 0.34 * (1.0 - exp(-1.8 * waterDepth));
    float surfaceAlpha = fresnel * 0.72 + waterColumnAlpha + min(length(sunGlint) * 0.22, 0.26);

    // Smoothly reveal the sandy bed over the outer edge of a radial lake.
    float shoreEdgeFade = u_waterShape == 0
        ? 1.0 - smoothstep(0.93, 1.0, length(fragLakeCoord))
        : smoothstep(0.0005, 0.012, waterDepth);
    surfaceAlpha *= shoreEdgeFade;

    // Meniscus and contact foam sit firmly ON the surface — never dissolved!
    float alpha = clamp(surfaceAlpha + brokenFoam * 0.28 * shoreEdgeFade, 0.0, 0.88);

    // ── 9. COMPOSITION ───────────────────────────────────────────────────────
    // Aquatic water column color (Beer-Lambert volumetric tint)
    vec3 shallowTint = mix(u_shallowColor, vec3(0.16, 0.64, 0.60), 0.50);
    vec3 deepTint = mix(u_deepColor, vec3(0.05, 0.34, 0.42), 0.50);
    vec3 waterVolumeColor = mix(shallowTint, deepTint, smoothstep(0.05, 0.75, waterDepth));
    waterVolumeColor *= (0.75 + u_ambientColor * 0.30 + u_lightColor * 0.20);

    // Mix surface reflection (sky) and water volume color based on Fresnel
    vec3 color = mix(waterVolumeColor, reflectedSky, fresnel * 0.65 + 0.10);
    color += causticLight;
    color += sssLight;
    color += waveHighlight;
    color += sunGlint;
    color += waveReflection + waveLighting;
    color += u_lightColor * vec3(0.66, 0.78, 0.88) *
             clamp(waveCurvature, -0.24, 0.24);
    vec3 frothColor = mix(u_foamColor, vec3(1.0, 1.0, 1.0), 0.18);
    color = mix(color, frothColor, brokenFoam);

    // VFX Point Light response
    color += VFXLights_Accumulate(fragPosition, normal, u_shallowColor) * 0.65;

    finalColor = vec4(color, alpha);
}
