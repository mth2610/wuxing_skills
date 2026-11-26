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
        // QI_SPIRIT: Talisman Luminous Spirit + Mesh Surface Aura look.
        // Clean smoked-glass translucent interior + razor-sharp incandescent
        // Fresnel silhouette + glowing scale ridges & flowing longitudinal qi wave.
        float rim       = pow(1.0 - ndv, 2.05);
        float rimBand   = smoothstep(0.08, 0.92, rim);
        float scaleLuma = dot(fragColor.rgb, vec3(0.30, 0.59, 0.11));
        float ridgeMask = smoothstep(0.42, 0.92, scaleLuma);
        float qiWave    = 0.78 + 0.22 * sin(fragLocalPos.x * 2.4 + u_time * 5.2);
        float qiPulse   = 0.92 + 0.08 * sin(u_time * 3.1 + fragLocalPos.y * 2.0);

        // Smoked-glass spirit core (mostly see-through at center, deeper tint for daylight readability)
        vec3 mysticCore = mix(albedo * 0.22, u_glowColor * 0.18, 0.50);
        float bodyAlpha = clamp(u_opacity * (0.24 + 0.34 * rimBand) * texel.a * colDiffuse.a, 0.0, 0.72);

        // Two-stop incandescent energy ramp (talisman.fs style): rich elemental primary -> white-hot peak
        float energyMask = clamp(rimBand * 1.15 + ridgeMask * 0.28 + edgeGlow * 1.35, 0.0, 1.5);
        vec3 whiteHot    = mix(u_glowColor, vec3(1.0, 0.97, 0.84), 0.74);
        vec3 lineCol     = mix(u_glowColor, whiteHot, smoothstep(0.32, 0.92, rimBand + edgeGlow * 0.6));
        vec3 safeGlow    = VFX_TonemapSafeEmission(lineCol, u_glowIntensity * qiWave * qiPulse, energyMask);

        float strokeAlpha = clamp(rimBand * 0.88 + edgeGlow * 0.95, 0.0, 0.96);
        float coverage    = max(bodyAlpha, strokeAlpha);
        finalColor = VFX_ResolvePremultiplied(mysticCore, 1.0, coverage, safeGlow, 1.0, 1.0);
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
