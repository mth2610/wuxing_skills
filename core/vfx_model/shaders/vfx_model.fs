#version 330

#ifdef GL_ES
precision highp float;
#endif

#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/vfx_composite.glsl"

in vec2  fragTexCoord;
in vec4  fragColor;
in vec3  fragWorldPos;
in vec3  fragWorldNormal;
in vec3  fragLocalPos;
in float fragSpineT;

uniform sampler2D texture0;
uniform vec4      colDiffuse;

uniform float u_time;
uniform int   u_style;         // 0 = SOLID_LIT, 1 = QI_SPIRIT, 2 = SOLID_AURA
uniform float u_dissolve;      // 0 = fully visible, 1 = dissolved
uniform vec2  u_revealRange;   // x = start (0=head), y = end (1=tail)
uniform float u_opacity;       // base coverage for QI_SPIRIT
uniform vec3  u_glowColor;
uniform float u_glowIntensity;

uniform vec3  u_sunToLight;
uniform vec3  u_sunColor;
uniform vec3  u_skyColor;
uniform vec3  u_groundColor;
uniform vec3  u_viewPos;

out vec4 finalColor;

void main() {
    vec4 texel = texture(texture0, fragTexCoord);
    vec3 albedo = texel.rgb * fragColor.rgb * colDiffuse.rgb;

    vec3 N = normalize(fragWorldNormal);
    if (!gl_FrontFacing) N = -N;
    vec3 V = normalize(u_viewPos - fragWorldPos);
    float ndv = clamp(abs(dot(N, V)), 0.0, 1.0);
    float fresnel = pow(1.0 - ndv, 2.6);

    // 3D noise field in local space so dissolve & qi veins stick to the animated body
    float nDissolve = vnoise3(fragLocalPos * 1.45 + vec3(0.0, -u_time * 0.45, 0.0));
    float edgeGlow = 0.0;
    if (u_dissolve > 0.001) {
        if (nDissolve < u_dissolve) discard;
        edgeGlow = 1.0 - smoothstep(u_dissolve, u_dissolve + 0.12, nDissolve);
    }

    // Longitudinal head-to-tail spine reveal/wipe (0 = head, 1 = tail)
    float wipeNoise = (nDissolve - 0.5) * 0.08;
    float sCoord = fragSpineT + wipeNoise;
    if (u_revealRange.x > 0.001) {
        if (sCoord < u_revealRange.x) discard;
        edgeGlow = max(edgeGlow, 1.0 - smoothstep(u_revealRange.x, u_revealRange.x + 0.09, sCoord));
    }
    if (u_revealRange.y < 0.999) {
        if (sCoord > u_revealRange.y) discard;
        edgeGlow = max(edgeGlow, 1.0 - smoothstep(u_revealRange.y - 0.09, u_revealRange.y, sCoord));
    }

    // Half-Lambert stylized lighting
    float ndl = dot(N, normalize(u_sunToLight));
    float halfLambert = ndl * 0.5 + 0.5;
    vec3 hemi = mix(u_groundColor, u_skyColor, N.y * 0.5 + 0.5);
    vec3 lit = albedo * (hemi + u_sunColor * (halfLambert * halfLambert * 0.85));

    // Scrolling qi vein pattern (Mali-safe single-octave vnoise3)
    float veinWave = vnoise3(fragLocalPos * 2.2 + vec3(0.0, -u_time * 1.6, u_time * 0.9));
    float veins = smoothstep(0.62, 0.88, veinWave);

    if (u_style == 1) {
        // QI_SPIRIT: Gaseous / Vaporous Celestial Qi Dragon ("Khí Hóa").
        // Truly hollow see-through mist interior -> thin swirling Qi silk threads
        // & sparkling golden motes -> blazing thin wispy Fresnel energy outline.
        float faceWeight = gl_FrontFacing ? 1.0 : 0.20;
        float rim        = pow(1.0 - ndv, 2.85);
        float thinRim    = smoothstep(0.14, 0.92, rim);
        float coreRim    = smoothstep(0.48, 0.96, rim + edgeGlow * 0.65);

        // Head stays slightly more coherent for iconic silhouette; tail dissolves into mist
        float headMask   = 1.0 - smoothstep(0.05, 0.24, fragSpineT);
        float tailVapor  = 1.0 - 0.50 * smoothstep(0.62, 1.0, fragSpineT);

        // Longitudinal flowing Qi vapor & thin swirling silk filaments (head -> tail)
        vec3 flowCoord1  = vec3(fragSpineT * 4.6 - u_time * 3.0,
                                fragLocalPos.y * 2.8 + sin(fragSpineT * 7.5 - u_time * 3.4) * 0.40,
                                fragLocalPos.z * 2.8 + cos(fragSpineT * 6.5 - u_time * 2.8) * 0.40);
        float mistField  = vnoise3(flowCoord1);
        float vaporWisp  = smoothstep(0.32, 0.84, mistField);

        vec3 flowCoord2  = vec3(fragSpineT * 9.0 - u_time * 4.8,
                                fragLocalPos.y * 5.2 + mistField * 1.6,
                                fragLocalPos.z * 5.2 - mistField * 1.6);
        float silkField  = vnoise3(flowCoord2);
        // Sharp ridged silk threads with wide transparent gaps between them
        float qiThread   = pow(clamp(1.0 - abs(silkField * 2.0 - 1.0), 0.0, 1.0), 4.6);
        float qiSparkle  = smoothstep(0.89, 0.985, silkField) * (0.40 + 0.60 * vaporWisp);

        // Gaseous hollow density: center is translucent vapor (0.04..0.16) so
        // ground/character shows right through; only rim + silk threads condense.
        float wispMod    = mix(0.25, 1.0, vaporWisp);
        float rimShell   = thinRim * mix(0.42, 1.15, vaporWisp);
        float interior   = (0.04 + 0.34 * qiThread + 0.10 * vaporWisp + 0.14 * headMask) * (1.0 - thinRim);
        float gasDensity = clamp((interior + rimShell * 0.92 + edgeGlow * 0.90) * tailVapor * faceWeight, 0.0, 1.0);

        vec3 vaporTint   = mix(colDiffuse.rgb, u_glowColor, 0.65);
        vec3 mysticCore  = vaporTint * (0.28 + 0.55 * qiThread + 0.25 * headMask);
        float coverage   = clamp(u_opacity * gasDensity * 0.64 * texel.a * colDiffuse.a, 0.0, 0.72);

        // Keep interior emission low so premultiplied additive doesn't blow out
        // the hollow center into solid yellow; concentrate HDR strictly in rim,
        // thin Qi threads, and pin-point sparkles.
        float interiorE  = (0.03 * vaporWisp + 0.58 * qiThread * wispMod + 0.22 * headMask * wispMod) * (1.0 - thinRim);
        float rimE       = rimShell * 1.75 + qiSparkle * 1.85 + edgeGlow * 1.50;
        float energyMask = clamp((interiorE + rimE) * tailVapor * faceWeight, 0.0, 2.6);

        vec3 coronaCol   = mix(vaporTint, u_glowColor, 0.84);
        vec3 whiteHot    = mix(u_glowColor, vec3(1.0, 0.99, 0.92), 0.86);
        float hotMix     = clamp(coreRim * 0.88 + qiThread * 0.55 + qiSparkle * 0.90, 0.0, 1.0);
        vec3 lineCol     = mix(coronaCol, whiteHot, hotMix);
        float qiPulse    = 0.92 + 0.08 * sin(u_time * 3.8 - fragSpineT * 6.0);
        vec3 safeGlow    = VFX_TonemapSafeEmission(lineCol, max(u_glowIntensity, 2.5) * 1.45 * qiPulse, energyMask);

        vec4 res = VFX_ResolvePremultiplied(mysticCore, 1.0, coverage, safeGlow, 1.0, 1.0);
        res.a = min(res.a, clamp(gasDensity * 0.82 + coreRim * 0.14, 0.0, 0.86));
        finalColor = res;
        return;
    }

    // SOLID_LIT (0) or SOLID_AURA (2)
    vec3 outRgb = lit + u_glowColor * (fresnel * 0.25);
    if (u_style == 2) {
        float auraMask = clamp(fresnel * 0.72 + veins * 0.35 + edgeGlow * 1.2, 0.0, 1.5);
        outRgb += VFX_TonemapSafeEmission(u_glowColor, u_glowIntensity * 0.75, auraMask);
    } else if (edgeGlow > 0.001) {
        outRgb += u_glowColor * (edgeGlow * u_glowIntensity);
    }

    finalColor = vec4(VFX_Finite3(outRgb), 1.0);
}
