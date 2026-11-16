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

void main() {
    float depth01  = clamp(fragTexCoord.x, 0.0, 1.0);
    float foam     = clamp(fragTexCoord.y, 0.0, 1.0);
    float orbAlpha = clamp(fragColor.r, 0.0, 1.0);
    float flowSpd  = clamp(fragColor.g, 0.0, 1.0);
    float vFade    = clamp(fragColor.a, 0.0, 1.0);

    float fade = u_fade * vFade;

    // Terrain-Conforming Ground Wetness Pass (fragColor.b > 0.5)
    if (fragColor.b > 0.5) {
        float wet = clamp(fragTexCoord.x, 0.0, 1.0) * fade;
        float dryNoise = fbm2(v_worldPos.xz * 2.2);
        float wetMask = smoothstep(0.18, 0.72, wet * (0.68 + 0.52 * dryNoise));
        if (wetMask <= 0.005) discard;

        vec3 N = normalize(fragNormal);
        vec3 V = normalize(-fragPosition);
        vec3 L1 = normalize(vec3(0.45, 0.82, 0.38));
        float ndv = clamp(abs(dot(N, V)), 0.0, 1.0);
        float fresnel = 0.02 + 0.98 * pow(1.0 - ndv, 4.0);
        float wetSpec = calcSpecular(N, L1, V, 96.0) * 1.15;

        vec3 dampTint = vec3(0.05, 0.04, 0.03);
        vec3 sheenCol = vec3(0.38, 0.60, 0.78) * fresnel * 0.45 + vec3(0.95, 0.96, 0.90) * wetSpec;
        vec3 col = dampTint + sheenCol * wetMask;
        float alpha = clamp(wetMask * 0.52, 0.0, 0.58);
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

    // World-space dual-scrolling FBM water ripples (locked to world, never slides with camera)
    vec2 uv1 = v_worldPos.xz * 1.45 + vec2(u_time * 1.25, -u_time * 0.90);
    vec2 uv2 = v_worldPos.xz * 2.85 + vec2(-u_time * 0.95, u_time * 1.35);
    float h0 = fbm2(uv1) + 0.5 * fbm2(uv2);
    float hx = fbm2(uv1 + vec2(0.07, 0.0)) + 0.5 * fbm2(uv2 + vec2(0.07, 0.0));
    float hz = fbm2(uv1 + vec2(0.0, 0.07)) + 0.5 * fbm2(uv2 + vec2(0.0, 0.07));
    vec2 rippleGrad = vec2(h0 - hx, h0 - hz) * mix(4.2, 2.2, orbAlpha);

    vec3 baseN = normalize(fragNormal);
    if (!gl_FrontFacing) baseN = -baseN;
    vec3 N = perturbNormal(baseN, rippleGrad, mix(0.42, 0.26, orbAlpha));

    // In MyBeginMode3D, fragPosition is in view space (camera at origin)
    vec3 V = normalize(-fragPosition);
    vec3 L1 = normalize(vec3(0.45, 0.82, 0.38));
    vec3 L2 = normalize(vec3(-0.60, 0.55, -0.45));

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

    // Rich translucent liquid alpha
    float bodyAlpha = mix(0.16, 0.56, pow(effDepth, 0.78));
    bodyAlpha = mix(bodyAlpha, 0.68, orbAlpha);
    float alpha = mix(bodyAlpha, 0.84, fresnel);
    alpha = max(alpha, foamMask * 0.84);
    alpha *= edgeAlpha * fade;

    finalColor = VFX_ResolveBody(color, 1.0, clamp(alpha, 0.0, 0.92));
}
