#version 330
#include "core/shaders/common/vfx_lights.glsl"
#include "maps/toolkit/shaders/map_shadow.glsl"
#include "maps/toolkit/shaders/nature_surface.glsl"

in vec3 fragPosition;
in vec3 fragNormal;
in vec2 v_uv;
flat in float v_part;
flat in float v_species;
flat in float v_seed;
in float v_ao;
flat in vec3 v_customTint;
in vec4 v_lightSpace;
in vec4 v_staticLightSpace;

out vec4 finalColor;

void GetFlowerSpeciesColors(int sp, float t, float s, float isInner, float seed, vec3 customTint,
                            out vec3 albedo, out vec3 sssColor)
{
    float hueShift = (seed - 0.5) * 0.08;
    float sMid = smoothstep(0.24, 0.90, t);
    float sBase = smoothstep(0.0, 0.34, t);

    if (sp == 0) {
        vec3 core = vec3(0.98, 0.78, 0.18);
        vec3 mid  = vec3(0.97, 0.98, 0.95);
        vec3 tip  = vec3(0.84, 0.94, 0.98);
        albedo = mix(core, mix(mid, tip, sMid), sBase);
        if (isInner > 0.5) albedo = mix(vec3(0.99, 0.72, 0.12), vec3(1.0, 0.95, 0.68), t);
        sssColor = vec3(0.94, 0.98, 0.88);
    } else if (sp == 1) {
        vec3 core = vec3(0.68, 0.34, 0.08);
        vec3 mid  = vec3(0.78, 0.60, 0.18) + vec3(hueShift * 0.7, hueShift * 0.4, 0.0);
        vec3 tip  = vec3(0.90, 0.78, 0.38);
        albedo = mix(core, mix(mid, tip, sMid), sBase);
        if (isInner > 0.5) albedo = mix(vec3(0.52, 0.22, 0.05), vec3(0.82, 0.54, 0.14), t);
        sssColor = vec3(0.92, 0.68, 0.20);
    } else if (sp == 2) {
        vec3 core = vec3(0.34, 0.02, 0.04);
        vec3 mid  = vec3(0.76, 0.15, 0.18) + vec3(hueShift * 0.4, 0.0, 0.0);
        vec3 tip  = vec3(0.88, 0.34, 0.24);
        albedo = mix(core, mix(mid, tip, sMid), sBase);
        if (isInner > 0.5) albedo = mix(vec3(0.78, 0.20, 0.16), vec3(0.88, 0.70, 0.30), smoothstep(0.55, 1.0, t));
        sssColor = vec3(0.92, 0.24, 0.14);
    } else if (sp == 3) {
        vec3 core = vec3(0.18, 0.28, 0.48);
        vec3 mid  = vec3(0.34, 0.48, 0.70) + vec3(0.0, hueShift * 0.6, hueShift * 0.8);
        vec3 tip  = vec3(0.64, 0.76, 0.88);
        albedo = mix(core, mix(mid, tip, sMid), sBase);
        if (isInner > 0.5) albedo = mix(vec3(0.32, 0.56, 0.62), vec3(0.76, 0.84, 0.90), t);
        sssColor = vec3(0.42, 0.66, 0.84);
    } else if (sp == 4) {
        vec3 core = vec3(0.66, 0.20, 0.34);
        vec3 mid  = vec3(0.84, 0.54, 0.64) + vec3(hueShift * 0.4, 0.0, hueShift * 0.4);
        vec3 tip  = vec3(0.93, 0.84, 0.86);
        albedo = mix(core, mix(mid, tip, sMid), sBase);
        if (isInner > 0.5) albedo = mix(vec3(0.80, 0.52, 0.16), vec3(0.90, 0.78, 0.42), t);
        sssColor = vec3(0.92, 0.52, 0.64);
    } else if (sp == 5 || sp == 6) {
        vec3 core = vec3(0.24, 0.12, 0.38);
        vec3 mid  = vec3(0.48, 0.32, 0.68) + vec3(hueShift * 0.8, 0.0, hueShift * 0.8);
        vec3 tip  = vec3(0.76, 0.64, 0.84);
        albedo = mix(core, mix(mid, tip, sMid), sBase);
        if (isInner > 0.5) albedo = mix(vec3(0.84, 0.66, 0.22), vec3(0.92, 0.84, 0.60), t);
        sssColor = vec3(0.74, 0.42, 0.84);
    } else if (sp == 8) {
        vec3 core = vec3(0.46, 0.40, 0.26);
        vec3 mid  = vec3(0.84, 0.85, 0.80) + vec3(hueShift * 0.25);
        vec3 tip  = vec3(0.94, 0.94, 0.90);
        albedo = mix(core, mix(mid, tip, sMid), sBase);
        if (isInner > 0.5) albedo = mix(vec3(0.78, 0.75, 0.68), vec3(0.90, 0.88, 0.82), t);
        sssColor = vec3(0.94, 0.90, 0.82);
    } else {
        vec3 core = vec3(0.86, 0.58, 0.14);
        vec3 mid  = vec3(0.93, 0.89, 0.78);
        vec3 tip  = vec3(0.96, 0.94, 0.88);
        albedo = mix(core, mix(mid, tip, sMid), sBase);
        if (isInner > 0.5) albedo = mix(vec3(0.88, 0.62, 0.16), vec3(0.94, 0.86, 0.50), t);
        sssColor = vec3(0.96, 0.84, 0.58);
    }

    if (dot(customTint, customTint) > 0.05 && isInner < 0.5) {
        vec3 naturalTint = mix(customTint, vec3(dot(customTint, vec3(0.3333))), 0.14);
        albedo = mix(albedo, naturalTint * mix(0.72, 1.04, t), 0.22);
    }
}

void main()
{
    vec3 rawN = gl_FrontFacing ? fragNormal : vec3(-fragNormal.x, max(abs(fragNormal.y), 0.22), -fragNormal.z);
    vec3 N = rawN * inversesqrt(max(dot(rawN, rawN), 1e-8));

    vec3 toCam = u_viewPos - fragPosition;
    float distSq = max(dot(toCam, toCam), 1e-6);
    float invDist = inversesqrt(distSq);
    float distToCam = distSq * invDist;
    vec3 V = toCam * invDist;

    float lLenSq = dot(u_lightDir, u_lightDir);
    vec3 L = (lLenSq > 1e-8) ? (u_lightDir * inversesqrt(lLenSq)) : vec3(0.0, 1.0, 0.0);
    vec3 halfSum = L + V;
    vec3 H = halfSum * inversesqrt(max(dot(halfSum, halfSum), 1e-8));

    int part = int(floor(v_part + 0.5));
    int sp = int(floor(v_species + 0.5));
    float t = v_uv.y;
    float s = abs(v_uv.x - 0.5) * 2.0;

    vec3 albedo;
    vec3 sssTint;
    float membraneThickness = 1.0;

    if (part < 2) {
        vec3 stemBase = vec3(0.11, 0.26, 0.08);
        vec3 stemTip  = vec3(0.24, 0.48, 0.14);
        albedo = mix(stemBase, stemTip, t);
        sssTint = vec3(0.36, 0.68, 0.16);
        membraneThickness = mix(0.85, 0.35, t);
    } else {
        float isInner = (part == 3) ? 1.0 : 0.0;
        GetFlowerSpeciesColors(sp, t, s, isInner, v_seed, v_customTint, albedo, sssTint);

        // Xấp xỉ hữu tỉ cho exp(-s * s * 26.0)
        float s26 = s * s * 26.0;
        float centerRib = (1.0 / (1.0 + s26 + 0.48 * s26 * s26)) * (1.0 - t * 0.55);
        float sideVeins = (distSq < 484.0) ? (sin(s * 26.0 - t * 14.0) * 0.5 + 0.5) : 0.5;
        float veinMask = mix(centerRib, sideVeins * (1.0 - s * 0.40), 0.38) * (1.0 - t * 0.30);
        membraneThickness = clamp(0.16 + veinMask * 0.56 + (1.0 - t) * 0.32, 0.10, 1.0);
        float veinContrast = (u_sssParams.w > 0.01) ? u_sssParams.w : 0.92;
        float throatCup = mix(0.76, 1.08, smoothstep(0.04, 0.62, t));
        float edgeRoll = smoothstep(0.58, 0.96, s) * smoothstep(0.18, 0.85, t);
        albedo *= mix(0.84, 1.06, 1.0 - veinMask * 0.38 * veinContrast) * throatCup * (1.0 + edgeRoll * 0.08);

        // Petal vein micro-normal relief for silky specular catchlights & cupped petal curvature
        if (part == 2 && distSq < 484.0) {
            float veinSlope = cos(s * 26.0 - t * 14.0) * sign(v_uv.x - 0.5) * (1.0 - s * 0.40) * 0.16;
            vec3 sideVec = cross(vec3(0.0, 1.0, 0.0), N);
            float svLenSq = dot(sideVec, sideVec);
            if (svLenSq > 1e-6) {
                N = N + (sideVec * inversesqrt(svLenSq)) * veinSlope;
                N *= inversesqrt(max(dot(N, N), 1e-8));
            }
        }
    }

    float shadowVis = FoliageShadowVisibility(
        v_lightSpace, v_staticLightSpace, N, L, fragPosition, distToCam);
    float cloudVis = 0.92;
    if (shadowVis > 0.001 && distSq < 900.0) {
        float cloudDistFade = FastRamp(576.0, 900.0, distSq);
        cloudVis = mix(Environment_CloudVisibility(u_cloudNoise, fragPosition), 0.92, cloudDistFade);
    }
    shadowVis *= cloudVis;
    float petalShadow = mix(0.62, 1.0, shadowVis);

    float rawNdotL = dot(N, L);
    float wrapDiffuse = max(0.0, (rawNdotL + 0.38) * 0.7246377);

    // Chuẩn hóa transLightDir trước khi tính tích vô hướng với V
    vec3 rawTransL = L + N * max(u_sssParams.y, 0.34);
    vec3 transLightDir = rawTransL * inversesqrt(max(dot(rawTransL, rawTransL), 1e-8));
    float forwardDot = clamp(dot(V, -transLightDir), 0.0, 1.0);
    float backScatter = forwardDot * forwardDot * forwardDot;
    float overheadTranslucency = max(0.0, -rawNdotL) * (0.55 + 0.45 * t);
    float thinness = 1.18 - membraneThickness * 0.52;
    float sssGain = max(u_sssParams.x, 1.25);
    float sssFactor = (backScatter * 1.15 + overheadTranslucency * 0.65) * thinness * sssGain;
    if (part < 2) sssFactor *= 0.42;

    float oneMinusNV = clamp(1.0 - max(dot(N, V), 0.0), 0.0, 1.0);
    float fresnel = (oneMinusNV * oneMinusNV) * (oneMinusNV * sqrt(oneMinusNV));
    float ndh = max(dot(N, H), 0.0);
    float specLobe = (part < 2) ? FastPow16(ndh) : (part == 3 ? FastPow8(ndh) : (FastPow16(ndh) * FastPow8(ndh)));
    float specStrength = (part < 2) ? 0.14 : (part == 3 ? 0.06 : 0.26);
    float spec = specLobe * specStrength * (0.4 + 0.6 * t);

    vec3 skyTint = u_ambientColor * mix(vec3(0.76, 0.82, 0.96), vec3(1.18, 1.18, 1.14), clamp(N.y * 0.5 + 0.5, 0.0, 1.0));
    vec3 groundBounce = vec3(0.14, 0.24, 0.09) * u_ambientColor * (1.0 - v_ao);

    float rimStrength = (u_sssParams.z > 0.01) ? u_sssParams.z : 0.78;
    vec3 directLight = albedo * u_lightColor * (wrapDiffuse * petalShadow);
    vec3 sssLight = sssTint * u_lightColor * (sssFactor * mix(0.45, 1.0, shadowVis));
    vec3 rimLight = mix(albedo, vec3(1.0, 0.97, 0.90), 0.5) * u_lightColor * (fresnel * 0.32 * rimStrength * petalShadow);
    vec3 specLight = u_lightColor * (spec * petalShadow);
    float baseContactAO = (part >= 2) ? mix(0.30, 1.0, smoothstep(0.0, 0.56, t)) : mix(0.52, 1.0, smoothstep(0.0, 0.45, t));
    vec3 ambientLight = (albedo * skyTint + groundBounce) * (v_ao * baseContactAO) * mix(0.68, 1.0, shadowVis);
    directLight *= mix(0.50, 1.0, smoothstep(0.0, 0.46, t));

    vec3 lit = ambientLight + directLight + sssLight + rimLight + specLight;
    if (distSq < 1225.0) {
        lit += VFXLights_Accumulate(fragPosition, N, albedo);
    }

    finalColor = vec4(lit, 1.0);
}