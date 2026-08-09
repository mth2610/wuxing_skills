#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

#include "core/shaders/common/noise.glsl"

uniform int u_semanticPass; // 0=alpha body/detail, 1=additive HDR emission
uniform int u_debugLayer;   // 0=normal, 1..5=Mass/Structure/Edge/Accent/Emission
uniform float u_innerRadius;
uniform float u_outerRadius;
uniform float u_fieldTime;
uniform float u_phase;
uniform float u_opacity;
uniform float u_emissionStrength;
uniform vec4 u_bodyColor;
uniform vec4 u_edgeColor;
uniform vec4 u_accentColor;
uniform vec4 u_emissionColor;
uniform vec4 u_lobeCenters;
uniform vec4 u_lobeWidths;
uniform int u_lobeCount;

const float TAU = 6.28318530718;

float periodicDistance(float a, float b)
{
    float d = abs(a - b);
    return min(d, 1.0 - d);
}

void main()
{
    // This UV covers the WHOLE authored field. It must never be reused as a
    // generic per-particle hotspot function: one field = one composition.
    vec2 p = fragTexCoord * 2.0 - 1.0;
    float radius = length(p);
    if (radius > 1.02) discard;

    float angle01 = fract(atan(p.y, p.x) / TAU + 1.0 + u_phase);
    vec2 drift = vec2(u_fieldTime * 0.08, -u_fieldTime * 0.055);
    float macroNoise = fbm2(p * 2.3 + drift + vec2(u_phase * 7.0));
    float structureNoise = fbm2(p * 5.8 - drift * 1.7 + vec2(3.1, 7.7));
    float detailNoise = fbm2N(p * 12.0 + drift * 2.1 + vec2(11.0, 2.0), 2);

    float warp = (macroNoise - 0.5) * 0.075;
    float inner = u_innerRadius + warp * 0.55;
    float outer = u_outerRadius + warp;
    float innerGate = smoothstep(inner - 0.045, inner + 0.035, radius);
    float outerGate = 1.0 - smoothstep(outer - 0.055, outer + 0.035, radius);
    float mass = clamp(innerGate * outerGate, 0.0, 1.0);

    // Structure and edge are resolved from the field density itself. This is
    // legal: they describe matter. Radiance below uses another authored mask.
    float structure = mass * smoothstep(0.22, 0.78, structureNoise + macroNoise * 0.34);
    float noiseContour = 1.0 - smoothstep(0.025, 0.115, abs(detailNoise - 0.54));
    float boundary = max(1.0 - smoothstep(0.0, 0.055, abs(radius - inner)),
                         1.0 - smoothstep(0.0, 0.060, abs(radius - outer)));
    float edge = mass * max(noiseContour * (0.35 + 0.65 * structure), boundary * 0.58);

    // Explicit authored lobe mask. It is independent of body alpha and spans
    // the aggregate annulus, so accents form coherent regions instead of one
    // radial core per puff or a chain of emissive billboards.
    float lobes = 0.0;
    for (int i = 0; i < 4; ++i)
    {
        if (i >= u_lobeCount) break;
        float d = periodicDistance(angle01, u_lobeCenters[i]);
        lobes = max(lobes, 1.0 - smoothstep(u_lobeWidths[i] * 0.38,
                                           u_lobeWidths[i], d));
    }
    float ringWidth = max(outer - inner, 0.001);
    float radial01 = clamp((radius - inner) / ringWidth, 0.0, 1.0);
    float innerRidge = 1.0 - smoothstep(0.16, 0.58, abs(radial01 - 0.28));
    float filaments = smoothstep(0.52, 0.82,
                                 detailNoise * 0.72 + structureNoise * 0.45);
    float accent = mass * lobes * innerRidge * filaments;

    // Separate authored emission field: broad low-energy fire colour plus the
    // lobe-constrained accent. It never samples `mass` from VFXBody; both masks
    // merely share this effect-level coordinate domain.
    float emissionTexture = smoothstep(0.38, 0.82,
                                       structureNoise * 0.62 + detailNoise * 0.50);
    float emission = mass * emissionTexture * (0.18 + 0.34 * lobes) + accent;

    if (u_debugLayer != 0)
    {
        float debugValue = (u_debugLayer == 1) ? mass :
                           (u_debugLayer == 2) ? structure :
                           (u_debugLayer == 3) ? edge :
                           (u_debugLayer == 4) ? accent : emission;
        if (u_semanticPass == 1) discard;
        finalColor = vec4(vec3(debugValue), debugValue);
        return;
    }

    if (u_semanticPass == 0)
    {
        float bodyAlpha = clamp((structure * 0.34 + edge * 0.30) * u_opacity,
                                0.0, 0.72);
        vec3 bodyRgb = mix(u_bodyColor.rgb, u_edgeColor.rgb,
                           clamp(edge * 0.72 + structure * 0.20, 0.0, 1.0));
        finalColor = vec4(bodyRgb * fragColor.rgb, bodyAlpha * fragColor.a);
    }
    else
    {
        if (emission < 0.004) discard;
        vec3 radianceColor = mix(u_emissionColor.rgb, u_accentColor.rgb,
                                 clamp(accent * 1.8, 0.0, 1.0));
        // BLEND_ADDITIVE applies source alpha. RGB remains uncompressed HDR;
        // bloom and the single final tone-map own highlight roll-off.
        finalColor = vec4(radianceColor * u_emissionStrength,
                          clamp(emission * u_opacity, 0.0, 1.0));
    }
}
