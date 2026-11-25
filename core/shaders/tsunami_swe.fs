#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/lighting.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/vfx_composite.glsl"

in vec3 v_worldPos;
in vec4 fragColor;

uniform float u_fade;
uniform vec4  u_deepColor;
uniform vec4  u_crestColor;
uniform vec4  u_foamColor;
uniform int   u_renderMode;
uniform float u_cryoAmount;

void main() {
    float depth01  = clamp(fragTexCoord.x, 0.0, 1.0);
    float foam     = clamp(fragTexCoord.y, 0.0, 1.0);
    float orbAlpha = clamp(fragColor.r, 0.0, 1.0);
    float flowSpd  = clamp(fragColor.g, 0.0, 1.0);
    float vFade    = clamp(fragColor.a, 0.0, 1.0);

    float fade = u_fade * vFade;
    float cryo = clamp(u_cryoAmount, 0.0, 1.0);

    // Ballistic Spray Droplets Pass (0.30 < fragColor.b < 0.70) — Water Drops -> Snow/Ice Motes
    if (fragColor.b > 0.30 && fragColor.b < 0.70) {
        vec2 uv = fragTexCoord - vec2(0.5);
        float taper = mix(mix(1.50, 0.94, clamp(fragTexCoord.y, 0.0, 1.0)), 1.0, cryo);
        float r = length(vec2(uv.x * taper, uv.y)) * 2.0;
        if (r > 1.0) discard;

        float ang = atan(uv.y, uv.x);
        float hexStar = 1.0 - 0.28 * pow(abs(sin(ang * 3.0)), 0.7);
        float effR = mix(r, r / max(hexStar, 0.55), cryo);
        if (effR > 1.0) discard;

        // Crisp surface-tension meniscus edge (never soft/fuzzy like gas)
        float edgeMask = smoothstep(1.0, mix(0.80, 0.55, cryo), effR);
        float sphereZ = sqrt(max(0.0, 1.0 - r * r));
        vec3 N = normalize(vec3(uv.x * 2.0, uv.y * 2.0, sphereZ));
        vec3 V = vec3(0.0, 0.0, 1.0);
        vec3 L1 = normalize(vec3(0.45, 0.82, 0.48));
        vec3 H1 = normalize(L1 + V);

        float ndv = clamp(sphereZ, 0.0, 1.0);
        float fresnel = pow(1.0 - ndv, 3.0);
        float specPinpoint = pow(max(0.0, dot(N, H1)), 96.0) * 3.2;

        // Clear center (refractive lens), darker teal meniscus rim -> glittering snow/ice crystal
        vec3 rimTint = mix(u_deepColor.rgb * 0.35, vec3(0.55, 0.85, 0.96), fresnel);
        vec3 dropCol = rimTint + vec3(0.95, 0.99, 1.0) * specPinpoint;
        vec3 snowCol = vec3(0.78, 0.93, 1.0) + vec3(0.25, 0.30, 0.35) * specPinpoint;
        dropCol = mix(dropCol, snowCol, cryo);

        float alpha = clamp(mix(0.10, 0.65, cryo) + fresnel * 0.62 + min(specPinpoint, 0.85), 0.0, 0.92);
        alpha *= edgeMask * vFade;
        finalColor = VFX_ResolveBody(dropCol, 1.0, alpha);
        return;
    }

    // Terrain-Conforming Ground Wetness -> Crystalline Hoarfrost Pass (fragColor.b > 0.75)
    if (fragColor.b > 0.75) {
        float wet = clamp(fragTexCoord.x, 0.0, 1.0) * fade;
        float dryNoise = fbm2(v_worldPos.xz * 2.2) * 0.65 + fbm2(v_worldPos.xz * 5.4) * 0.35;
        float wetMask = smoothstep(0.08, 0.58, wet * (0.64 + 0.56 * dryNoise));
        if (wetMask <= 0.005) discard;

        vec3 N = normalize(fragNormal);
        vec3 V = normalize(-fragPosition);
        vec3 L1 = normalize(vec3(0.45, 0.82, 0.38));
        float ndv = clamp(abs(dot(N, V)), 0.0, 1.0);
        float fresnel = 0.02 + 0.98 * pow(1.0 - ndv, 4.0);
        float wetSpec = calcSpecular(N, L1, V, 96.0) * 1.15;

        vec3 dampTint = vec3(0.05, 0.04, 0.03);
        vec3 sheenCol = vec3(0.38, 0.60, 0.78) * fresnel * 0.45 + vec3(0.95, 0.96, 0.90) * wetSpec;
        vec3 waterWetCol = dampTint + sheenCol * wetMask;

        float frostNoise = fbm2(v_worldPos.xz * 7.5);
        float frostSparkle = smoothstep(0.68, 0.92, fbm2(v_worldPos.xz * 18.0));
        vec3 frostCol = mix(vec3(0.55, 0.82, 0.95), vec3(0.90, 0.97, 1.0), frostNoise) + vec3(0.35) * frostSparkle;

        vec3 col = mix(waterWetCol, frostCol, cryo);
        float alpha = clamp(wetMask * mix(0.52, 0.74, cryo), 0.0, 0.76);
        finalColor = VFX_ResolveBody(col, 1.0, alpha);
        return;
    }

    float edgeAlpha = max(orbAlpha, smoothstep(0.02, 0.24, depth01));
    if (edgeAlpha * fade <= 0.004) discard;

    // Debug Render Mode 1: Velocity & Foam
    if (u_renderMode == 1) {
        vec3 velCol = vec3(flowSpd * 0.85, 0.25 + flowSpd * 0.65, 0.55 + orbAlpha * 0.45);
        velCol = mix(velCol, vec3(1.0, 0.98, 0.92), clamp(foam * 0.70, 0.0, 1.0));
        finalColor = VFX_ResolveBody(velCol, 1.0, clamp(edgeAlpha * fade * 0.90, 0.0, 0.92));
        return;
    }

    // Debug Render Mode 2: Heightfield + 3D Manifold Elevation
    if (u_renderMode == 2) {
        vec3 hCol = mix(vec3(0.02, 0.22, 0.55), vec3(0.20, 0.95, 1.0), clamp(depth01 + orbAlpha * 0.45, 0.0, 1.0));
        finalColor = VFX_ResolveBody(hCol, 1.0, clamp(edgeAlpha * fade * 0.90, 0.0, 0.92));
        return;
    }

    // World-space dual-scrolling FBM water ripples (dampened cleanly as water freezes into ice)
    float ripTime = u_time * (1.0 - 0.88 * cryo);
    vec2 uv1 = v_worldPos.xz * 1.45 + vec2(ripTime * 1.25, -ripTime * 0.90);
    vec2 uv2 = v_worldPos.xz * 2.85 + vec2(-ripTime * 0.95, ripTime * 1.35);
    float h0 = fbm2(uv1) + 0.5 * fbm2(uv2);
    float hx = fbm2(uv1 + vec2(0.07, 0.0)) + 0.5 * fbm2(uv2 + vec2(0.07, 0.0));
    float hz = fbm2(uv1 + vec2(0.0, 0.07)) + 0.5 * fbm2(uv2 + vec2(0.0, 0.07));
    vec2 rippleGrad = vec2(h0 - hx, h0 - hz) * mix(4.2, 2.2, orbAlpha) * (1.0 - 0.65 * cryo);

    vec3 baseN = normalize(fragNormal);
    if (!gl_FrontFacing) baseN = -baseN;
    vec3 N = perturbNormal(baseN, rippleGrad, mix(0.42, 0.26, orbAlpha));

    // In MyBeginMode3D, fragPosition is in view space (camera at origin)
    vec3 V = normalize(-fragPosition);
    vec3 L1 = (dot(u_lightDir, u_lightDir) > 0.001) ? normalize(u_lightDir) : normalize(vec3(0.45, 0.82, 0.38));
    vec3 L2 = normalize(vec3(-L1.x, max(L1.y * 0.65, 0.30), -L1.z));

    float ndv = clamp(abs(dot(N, V)), 0.0, 1.0);
    float fresnel = 0.03 + 0.97 * pow(1.0 - ndv, 3.6);
    float specSharp = calcSpecular(N, L1, V, 220.0) * 3.2;
    float specSoft  = calcSpecular(N, L2, V, 48.0)  * 0.36;
    float sunLit    = clamp(dot(N, L1) * 0.5 + 0.5, 0.0, 1.0);

    // True Beer-Lambert exponential absorption for rich sapphire/jade depth on bright sand
    float effDepth = max(depth01, orbAlpha * 0.50);
    vec3 beerTrans = exp(-vec3(2.40, 0.85, 0.55) * effDepth * 2.20);
    vec3 shallowCol = u_crestColor.rgb * 0.42;
    vec3 deepCol    = u_deepColor.rgb  * 0.36;
    vec3 abyssCol   = u_deepColor.rgb  * 0.16;

    vec3 waterBody = mix(deepCol, shallowCol, beerTrans);
    waterBody = mix(waterBody, abyssCol, smoothstep(0.42, 1.0, effDepth));

    // Sub-surface caustic web
    vec2 cUv = v_worldPos.xz * 1.85 + v_worldPos.xy * 0.35 - vec2(u_time * 1.35, u_time * 0.95);
    float causticNoise = fbm2(cUv);
    float causticWeb = smoothstep(0.46, 0.78, causticNoise);

    // Translucent sapphire/jade core for 3D detached ATLA manifolds
    vec3 orbCoreCol = mix(deepCol * 0.80, u_crestColor.rgb * 0.78, pow(1.0 - ndv, 1.5));
    orbCoreCol += vec3(0.14, 0.68, 0.88) * causticWeb * 0.28;
    waterBody = mix(waterBody, orbCoreCol, orbAlpha);
    waterBody *= (0.68 + 0.36 * sunLit);

    // Controlled subsurface scattering on thin crests only (no whiteout)
    float crestThickness = clamp(effDepth * 0.85 + flowSpd * 0.30, 0.0, 1.0);
    vec3 sssCol = u_crestColor.rgb * pow(crestThickness, 1.8) * 0.26 * (1.0 - 0.50 * orbAlpha);
    waterBody += sssCol + vec3(0.18, 0.62, 0.72) * causticWeb * (1.0 - foam * 0.80) * 0.22;

    // Sky Fresnel reflection + crisp sun glints (kept moderate so body stays deep)
    vec3 skyReflect = vec3(0.32, 0.62, 0.82);
    vec3 color = mix(waterBody, skyReflect, fresnel * mix(0.28, 0.52, orbAlpha));
    color += vec3(0.92, 0.98, 1.0) * (specSharp + specSoft) * (1.0 - foam * 0.55);

    // Crest-only lacy aerated foam (multi-octave FBM breakup, never a milky blanket)
    float fn1 = fbm2(v_worldPos.xz * 4.2 + vec2(u_time * 1.5, -u_time * 1.1));
    float fn2 = fbm2(v_worldPos.xz * 9.6 - vec2(u_time * 2.1, u_time * 1.4));
    float groundFoam = (foam * 0.92 + fn1 * 0.38 + fn2 * 0.22 - 0.42) * (1.0 - 0.88 * orbAlpha);
    float orbFilaments = orbAlpha * (causticWeb * 0.38 + pow(1.0 - ndv, 4.0) * 0.28);
    float lacyFoam = smoothstep(0.46, 0.86, groundFoam + orbFilaments);
    float crestCore = smoothstep(0.76, 0.98, foam) * (1.0 - 0.78 * orbAlpha);
    float foamMask = clamp(max(lacyFoam * 0.85, crestCore), 0.0, 1.0) * edgeAlpha;

    color = mix(color, u_foamColor.rgb * (0.86 + 0.14 * sunLit), foamMask * mix(0.82, 0.40, orbAlpha));

    // Cryo-Freeze Transition: Crystalline fracture veins + frosted glacial ice sheen
    if (cryo > 0.01) {
        float crackField = abs(fbm2(v_worldPos.xz * 3.2 + v_worldPos.xy * 1.4) - 0.5);
        float iceVeins = 1.0 - smoothstep(0.015, 0.09, crackField);
        vec3 glacialTint = mix(vec3(0.28, 0.68, 0.90), vec3(0.84, 0.95, 1.0), pow(1.0 - ndv, 1.35));
        glacialTint += vec3(0.55, 0.85, 1.0) * iceVeins * 0.55;
        color = mix(color, glacialTint * (0.72 + 0.32 * sunLit), cryo * 0.78);
    }

    // Rich translucent liquid alpha (higher solidity when frozen to glacial ice)
    float bodyAlpha = mix(0.16, 0.56, pow(effDepth, 0.78));
    bodyAlpha = mix(bodyAlpha, 0.68, orbAlpha);
    bodyAlpha = mix(bodyAlpha, 0.84, cryo * 0.75);
    float alpha = mix(bodyAlpha, 0.88, fresnel);
    alpha = max(alpha, foamMask * 0.84);
    alpha *= edgeAlpha * fade;

    finalColor = VFX_ResolveBody(color, 1.0, clamp(alpha, 0.0, 0.92));
}
