#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/vfx_composite.glsl"

// ── VFX_ComposeRuneCircle — Cổ Ấn Vạn Pháp (6 Ancient Seal Variants) ────────
// Ported from the WebGL single-pass blackbody rune circle architecture, adapted
// to the engine's Bright-Background VFX Contract (separate BODY alpha pass +
// EMISSION premultiplied pass) with chroma-preserving linear pigment so all 6
// seals stay vivid on both dark and bright daylight backgrounds.
//
// Layer modes (u_style.w):
//   0 = Rune Texture Layer (Layer 1 inner core/script or Layer 2 outer script)
//       texture0 RGB: R = razor calligraphy/rings, G = tight aura, B = outer curls
//   1 = Energy Pulse Layer (Layer 3 polar rim pulse & sharp perimeter filaments)
//   2 = Pinpoint Ember Sparks (camera-facing sharp exponential spark quads)

in vec4 fragColor;

uniform sampler2D texture0;     // Layer 1 (innerTex) or Layer 2 (outerTex)
uniform sampler2D u_noiseTex;   // 256x256 seamless periodic RGBA noise
uniform vec4 u_deepColor;       // dark elemental shadow / ember base
uniform vec4 u_midColor;        // saturated elemental mid-tone
uniform vec4 u_coreColor;       // hot white-tinted peak core
uniform vec4 u_params;          // x: fade, y: glow * layerGain, z: ignite (0..1), w: igniteSpike
uniform vec4 u_style;           // x: timeWrapped, y: seed, z: passMode (0=BODY,1=EMIT), w: layerMode
uniform float u_layerRot;       // pre-wrapped layer rotation (radians)

const float PI_VAL  = 3.14159265;
const float TAU_VAL = 6.28318530;

vec2 Rot2(vec2 p, float a)
{
    float c = cos(a);
    float s = sin(a);
    return vec2(c * p.x - s * p.y, s * p.x + c * p.y);
}

// Suppress minor channels slightly so sRGB-authored palette colors survive
// ACES per-channel shoulder compression without washing into pastel on bright
// daylight backgrounds.
vec3 VividPigment(vec3 c, float power, float gain)
{
    return pow(max(c, vec3(0.0)), vec3(power)) * gain;
}

// Physical blackbody heat ramp matching the WebGL reference:
//   0.00 -> 0.26: black -> deep elemental ember (cDeep)
//   0.24 -> 0.66: deep -> saturated elemental flame (cMid)
//   0.64 -> 0.88: mid -> hot gold/elemental highlight
//   0.88 -> 1.24: white-hot core peak (cCore * 1.45)
vec3 physicalHeatRamp(float h, vec3 cDeep, vec3 cMid, vec3 cCore)
{
    vec3 col = mix(vec3(0.0), cDeep, smoothstep(0.02, 0.26, h));
    col = mix(col, cMid, smoothstep(0.24, 0.66, h));
    vec3 hotGold = mix(cMid * 1.45, cCore, 0.42);
    col = mix(col, hotGold, smoothstep(0.64, 0.88, h));
    col = mix(col, cCore * 1.45, smoothstep(0.88, 1.26, h));
    return col;
}

void main()
{
    float fade        = u_params.x;
    float glowGain    = u_params.y;
    float ignite      = u_params.z;
    float igniteSpike = u_params.w;

    float t         = u_style.x;
    float seed      = u_style.y;
    float passMode  = u_style.z;
    float layerMode = u_style.w;

    vec3 richDeep = VividPigment(u_deepColor.rgb, 1.10, 1.05);
    vec3 vividMid = VividPigment(u_midColor.rgb,  1.12, 1.12);
    vec3 hotCore  = u_coreColor.rgb;

    // ── Mode 2: Pinpoint Ember Sparks ────────────────────────────────────────
    if (layerMode > 1.5)
    {
        vec2 c = fragTexCoord - vec2(0.5);
        float d2 = dot(c, c);
        if (d2 > 0.25) discard;

        float spark   = exp(-d2 * 28.0);
        float corePin = exp(-d2 * 85.0);

        float vHeat  = fragColor.r * 1.3;
        float vAlpha = fragColor.a * fade;

        vec3 col = mix(richDeep, vividMid, smoothstep(0.15, 0.72, vHeat));
        col = mix(col, hotCore * 1.35, clamp(corePin * 1.10, 0.0, 1.0));
        col *= (0.9 + glowGain * 0.35);

        float alpha = spark * vAlpha;
        if (alpha < 0.01) discard;

        finalColor = VFX_ResolvePremultiplied(
            vividMid, 0.85, clamp(alpha * 0.70, 0.0, 0.90),
            VFX_TonemapSafeHDR(col), alpha, 1.0);
        return;
    }

    vec2 pUnrot = fragTexCoord * 2.0 - 1.0;
    float r = length(pUnrot);

    // Symmetric two-arc ignite sweep from bottom (0) to top (1)
    float symA = abs(atan(pUnrot.x, -pUnrot.y)) / PI_VAL;
    float sweepEdge = ignite * 1.08;

    // ── Mode 1: Layer 3 — Energy Pulse & Sharp Perimeter Filaments ───────────
    if (layerMode > 0.5)
    {
        if (r < 0.515 || r > 0.875) discard;

        vec2 p = Rot2(pUnrot, u_layerRot);
        float angle = atan(p.y, p.x);
        float normA = (angle + PI_VAL) / TAU_VAL;

        vec2 polar1 = vec2(normA * 4.0 - t * 0.06, r * 3.2 - t * 0.04) + seed;
        vec2 polar2 = vec2(normA * 8.0 + t * 0.09, r * 6.0 - t * 0.07) + seed * 2.1;
        vec4 n1 = texture(u_noiseTex, polar1);
        vec4 n2 = texture(u_noiseTex, polar2);

        float rimBand  = exp(-pow((r - 0.535) * 44.0, 2.0));
        float rimPulse = smoothstep(0.32, 0.86, n1.g * 0.65 + n2.r * 0.35);

        float outerBand = smoothstep(0.67, 0.75, r) * smoothstep(0.875, 0.77, r);
        float sharpFilament = pow(1.0 - abs(n1.r * 2.0 - 1.0), 4.0) * smoothstep(0.42, 0.82, n2.g);

        float reveal = smoothstep(sweepEdge, sweepEdge - 0.12, symA);
        float sweepHot = exp(-pow((symA - sweepEdge) * 16.0, 2.0))
                       * step(0.01, ignite) * step(ignite, 0.995) * rimBand;

        float heat = (rimBand * rimPulse * 0.92 + outerBand * sharpFilament * 0.68 + sweepHot * 1.15);
        heat *= (1.0 + igniteSpike * 0.38) * glowGain * mix(0.02, 1.0, reveal) * fade;

        if (heat < 0.02) discard;

        vec3 col = physicalHeatRamp(heat, richDeep, vividMid, hotCore);
        float cov = clamp(heat * 0.45, 0.0, 0.72);
        finalColor = VFX_ResolvePremultiplied(
            vividMid, 0.70, cov,
            VFX_TonemapSafeHDR(col), 1.0, 1.0);
        return;
    }

    // ── Mode 0: Layer 1 (Inner Core & Script) / Layer 2 (Outer Script & Motifs)
    float rim = 1.0 - smoothstep(0.955, 0.985, r);
    if (r > 0.985 || r < 0.495) discard;

    vec2 p = Rot2(pUnrot, u_layerRot);
    vec2 uv = p * 0.5 + 0.5;

    vec3 ch = texture(texture0, uv).rgb;
    float razorCore = ch.r;
    float tightAura = ch.g;
    float outerCurl = ch.b;

    if (razorCore + tightAura + outerCurl < 0.008) discard;

    float voidCut = smoothstep(0.495, 0.525, r);
    float reveal = smoothstep(sweepEdge, sweepEdge - 0.11, symA);
    float leadingArc = exp(-pow((symA - sweepEdge) * 18.0, 2.0))
                     * step(0.01, ignite) * step(ignite, 0.995);
    float activeMask = mix(0.03, 1.0, reveal) * voidCut * rim * fade;

    // 2 seamless noise lookups for organic erosion & living flicker
    vec4 n1 = texture(u_noiseTex, uv * 2.6 + vec2(t * 0.05, -t * 0.04) + seed);
    vec4 n2 = texture(u_noiseTex, uv * 5.2 - vec2(t * 0.11,  t * 0.08) + seed * 1.7);

    float erosion = smoothstep(0.18, 0.82, n1.r * 0.72 + n2.g * 0.28 + 0.12);
    float flicker = 0.82 + 0.34 * n2.g + 0.12 * n1.b;

    // ── BODY Pass (BLEND_ALPHA): vivid stroke pigment (no muddy grey plate) ─
    if (passMode < 0.5)
    {
        float strokeCov = clamp(
            max(razorCore * (0.98 + 0.15 * erosion),
                max(tightAura * 0.82, outerCurl * 0.72)),
            0.0, 1.0);
        float cov = strokeCov * activeMask;
        if (cov < 0.008) discard;

        // Keep outer curls & glyph strokes vividly pigmented instead of dark grey mud
        float midWeight = clamp(razorCore * 1.10 + outerCurl * 0.95 + tightAura * 0.35, 0.0, 1.0);
        vec3 bodyInk = mix(richDeep, vividMid, midWeight);
        finalColor = VFX_ResolveBody(bodyInk, 1.0, cov);
        return;
    }

    // ── EMISSION Pass (BLEND_ALPHA_PREMULTIPLY): blackbody luminance ramp ───
    float coreHeat = razorCore * (0.86 + 0.34 * erosion) * flicker;
    float auraHeat = tightAura * (0.44 + 0.26 * n1.r);
    float curlHeat = outerCurl * (0.55 + 0.38 * n2.g) * smoothstep(0.98, 0.78, r);

    float heat = max(coreHeat * 1.12, max(auraHeat * 0.80, curlHeat * 0.78));
    heat += leadingArc * (razorCore * 0.65 + tightAura * 0.45);
    heat *= (1.0 + igniteSpike * 0.42) * glowGain;

    vec3 emitCol = physicalHeatRamp(heat * 1.18, richDeep, vividMid, hotCore) * activeMask * 1.25;
    float cov = clamp(max(razorCore * 0.94, max(tightAura * 0.45, outerCurl * 0.25)) * activeMask, 0.0, 0.94);
    float peak = max(emitCol.r, max(emitCol.g, emitCol.b));
    if (peak < 0.005 && cov < 0.005) discard;

    vec3 bodyTint = mix(richDeep, vividMid, clamp(razorCore + outerCurl, 0.0, 1.0));
    finalColor = VFX_ResolvePremultiplied(
        bodyTint, 0.65, cov,
        VFX_TonemapSafeHDR(emitCol), 1.0, 1.0);
}
