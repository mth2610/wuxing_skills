#version 330
#include "core/shaders/common/vfx_lights.glsl"
#include "maps/toolkit/shaders/map_shadow.glsl"
#include "maps/toolkit/shaders/nature_surface.glsl"

in vec3 fragPosition;
in vec3 fragNormal;
in vec2 v_uv;
in float v_part;
in float v_species;
in float v_seed;
in float v_ao;
in vec3 v_customTint;
in vec4 v_lightSpace;
in vec4 v_staticLightSpace;

uniform vec4 u_sssParams;

out vec4 finalColor;

void GetFlowerSpeciesColors(int sp, float t, float s, float isInner, float seed, vec3 customTint,
                            out vec3 albedo, out vec3 sssColor)
{
    float hueShift = (seed - 0.5) * 0.08;
    if (sp == 0) {
        vec3 core = vec3(0.98, 0.78, 0.18);
        vec3 mid  = vec3(0.97, 0.98, 0.95);
        vec3 tip  = vec3(0.84, 0.94, 0.98);
        albedo = mix(core, mix(mid, tip, smoothstep(0.22, 0.92, t)), smoothstep(0.0, 0.34, t));
        if (isInner > 0.5) albedo = mix(vec3(0.99, 0.72, 0.12), vec3(1.0, 0.95, 0.68), t);
        sssColor = vec3(0.94, 0.98, 0.88);
    } else if (sp == 1) {
        vec3 core = vec3(0.78, 0.34, 0.04);
        vec3 mid  = vec3(0.99, 0.78, 0.10) + vec3(hueShift, hueShift * 0.5, 0.0);
        vec3 tip  = vec3(1.00, 0.94, 0.36);
        albedo = mix(core, mix(mid, tip, smoothstep(0.20, 0.90, t)), smoothstep(0.0, 0.30, t));
        if (isInner > 0.5) albedo = mix(vec3(0.72, 0.28, 0.03), vec3(0.98, 0.68, 0.10), t);
        sssColor = vec3(1.00, 0.76, 0.14);
    } else if (sp == 2) {
        vec3 core = vec3(0.38, 0.02, 0.05);
        vec3 mid  = vec3(0.94, 0.09, 0.14) + vec3(hueShift * 0.5, 0.0, 0.0);
        vec3 tip  = vec3(1.00, 0.34, 0.18);
        albedo = mix(core, mix(mid, tip, smoothstep(0.28, 0.92, t)), smoothstep(0.0, 0.32, t));
        if (isInner > 0.5) albedo = mix(vec3(0.95, 0.18, 0.16), vec3(1.00, 0.82, 0.32), smoothstep(0.55, 1.0, t));
        sssColor = vec3(1.00, 0.20, 0.08);
    } else if (sp == 3) {
        vec3 core = vec3(0.24, 0.95, 0.84);
        vec3 mid  = vec3(0.16, 0.54, 0.96) + vec3(0.0, hueShift, hueShift);
        vec3 tip  = vec3(0.64, 0.92, 1.00);
        albedo = mix(core, mix(mid, tip, smoothstep(0.25, 0.88, t)), smoothstep(0.0, 0.36, t));
        if (isInner > 0.5) albedo = mix(vec3(0.32, 0.98, 0.88), vec3(0.84, 0.99, 1.00), t);
        sssColor = vec3(0.24, 0.88, 1.00);
    } else if (sp == 4) {
        vec3 core = vec3(0.88, 0.26, 0.42);
        vec3 mid  = vec3(0.98, 0.64, 0.76) + vec3(hueShift * 0.5, 0.0, hueShift * 0.5);
        vec3 tip  = vec3(1.00, 0.92, 0.95);
        albedo = mix(core, mix(mid, tip, smoothstep(0.22, 0.90, t)), smoothstep(0.0, 0.32, t));
        if (isInner > 0.5) albedo = mix(vec3(0.96, 0.62, 0.16), vec3(1.00, 0.88, 0.46), t);
        sssColor = vec3(1.00, 0.54, 0.70);
    } else if (sp == 5 || sp == 6) {
        vec3 core = vec3(0.26, 0.08, 0.48);
        vec3 mid  = vec3(0.58, 0.28, 0.92) + vec3(hueShift, 0.0, hueShift);
        vec3 tip  = vec3(0.88, 0.66, 1.00);
        albedo = mix(core, mix(mid, tip, smoothstep(0.28, 0.94, t)), smoothstep(0.0, 0.38, t));
        if (isInner > 0.5) albedo = mix(vec3(0.98, 0.80, 0.20), vec3(1.00, 0.96, 0.72), t);
        sssColor = vec3(0.82, 0.38, 1.00);
    } else {
        vec3 core = vec3(0.95, 0.68, 0.16);
        vec3 mid  = vec3(0.99, 0.94, 0.82);
        vec3 tip  = vec3(1.00, 0.98, 0.92);
        albedo = mix(core, mix(mid, tip, smoothstep(0.24, 0.90, t)), smoothstep(0.0, 0.34, t));
        if (isInner > 0.5) albedo = mix(vec3(0.96, 0.70, 0.18), vec3(1.00, 0.92, 0.55), t);
        sssColor = vec3(1.00, 0.88, 0.62);
    }

    if (dot(customTint, customTint) > 0.05 && isInner < 0.5) {
        albedo = mix(albedo, customTint * mix(0.76, 1.12, t), 0.32);
    }
}

void main()
{
    vec3 N = normalize(fragNormal);
    if (!gl_FrontFacing) N = -N;

    vec3 V = normalize(u_viewPos - fragPosition);
    vec3 L = normalize(u_lightDir);
    vec3 H = normalize(L + V);

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

        float centerRib = exp(-s * s * 26.0) * (1.0 - t * 0.55);
        float sideVeins = sin(s * 24.0 - t * 12.0) * 0.5 + 0.5;
        float veinMask = mix(centerRib, sideVeins * (1.0 - s * 0.45), 0.32) * (1.0 - t * 0.35);
        membraneThickness = clamp(0.22 + veinMask * 0.55 + (1.0 - t) * 0.28, 0.15, 1.0);
        albedo *= mix(0.90, 1.06, 1.0 - veinMask * 0.32 * u_sssParams.w);
    }

    vec3 toCam = u_viewPos - fragPosition;
    float distSq = dot(toCam, toCam);
    float distToCam = sqrt(max(distSq, 1e-6));
    float shadowVis = FoliageShadowVisibility(
        v_lightSpace, v_staticLightSpace, N, L, fragPosition, distToCam);
    float cloudVis = 0.92;
    if (shadowVis > 0.001 && distSq < 900.0) {
        float cloudDistFade = FastRamp(576.0, 900.0, distSq);
        cloudVis = mix(Environment_CloudVisibility(u_cloudNoise, fragPosition), 0.92, cloudDistFade);
    }
    shadowVis *= cloudVis;
    float petalShadow = mix(0.68, 1.0, shadowVis);

    float rawNdotL = dot(N, L);
    float wrapDiffuse = max(0.0, (rawNdotL + 0.38) / 1.38);

    vec3 transLightDir = L + N * max(u_sssParams.y, 0.34);
    float backScatter = pow(clamp(dot(V, -transLightDir), 0.0, 1.0), 3.0);
    float overheadTranslucency = max(0.0, -rawNdotL) * (0.55 + 0.45 * t);
    float thinness = 1.18 - membraneThickness * 0.52;
    float sssFactor = (backScatter * 1.15 + overheadTranslucency * 0.65) * thinness * max(u_sssParams.x, 1.25);
    if (part < 2) sssFactor *= 0.42;

    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 3.2);
    float specPower = (part < 2) ? 18.0 : 28.0;
    float specStrength = (part < 2) ? 0.14 : 0.24;
    float spec = pow(max(dot(N, H), 0.0), specPower) * specStrength * (0.4 + 0.6 * t);

    vec3 skyTint = u_ambientColor * mix(0.78, 1.24, clamp(N.y * 0.5 + 0.5, 0.0, 1.0));
    vec3 groundBounce = vec3(0.14, 0.24, 0.09) * u_ambientColor * (1.0 - v_ao);

    vec3 directLight = albedo * u_lightColor * wrapDiffuse * petalShadow;
    vec3 sssLight = sssTint * u_lightColor * sssFactor * mix(0.65, 1.0, shadowVis);
    vec3 rimLight = mix(albedo, vec3(1.0, 0.97, 0.90), 0.5) * u_lightColor * (fresnel * 0.32 * u_sssParams.z * petalShadow);
    vec3 specLight = u_lightColor * (spec * petalShadow);
    vec3 ambientLight = (albedo * skyTint + groundBounce) * v_ao;

    vec3 lit = ambientLight + directLight + sssLight + rimLight + specLight;
    lit += VFXLights_Accumulate(fragPosition, N, albedo);

    finalColor = vec4(lit, 1.0);
}
