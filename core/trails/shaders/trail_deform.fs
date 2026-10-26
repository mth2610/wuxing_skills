#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/fx.glsl"
#include "core/shaders/common/vfx_contrast.glsl"
#include "core/shaders/common/vfx_composite.glsl"
#include "core/shaders/common/lighting.glsl"
#include "core/uv/shaders/uv_deform.glsl"
#include "core/uv/shaders/surface_flow.glsl"

// u_uvField/u_uvMeta ONLY — declared directly, NOT pulled in from the
// uv_field GLSL module.
//
// LANDMINE, hit twice on a real build, 05/08/2026 — read this before
// changing anything above this comment, and see core/shader_preprocessor.c
// for the mechanism: that loader finds its target by bare text search, with
// NO awareness of GLSL comments and NO per-path dedup (unlike a real C
// preprocessor). The uv_field module re-pulls in the uv_deform and
// surface_flow modules itself, and this file already pulls both in
// directly two lines up, so naming that third module by writing the
// directive keyword immediately followed by its quoted path — ANYWHERE in
// this file, even inside a prose comment explaining not to — makes the
// loader pull it in for real, duplicating the uv_deform/surface_flow
// bodies in the flattened output and breaking compilation.
#define UV_DEFORM_MAX_LAYERS 4
uniform vec4 u_uvField[UV_DEFORM_MAX_LAYERS * 3];
uniform vec4 u_uvMeta;

uniform sampler2D texture0;
uniform float u_matMode;        // 0 = passthrough, 1 = packed 4-ch wisp, 2 = sin-wave strands
uniform float u_wispMix;        // coarse(R) -> fine(G)
uniform float u_dissolve;       // B-channel dissolve threshold
uniform float u_dissolveSoft;   // dissolve edge softness
uniform vec2  u_tiling;         // x = tiles along path, y = tiles across width
uniform vec4  u_panSpeed;       // x = coarse pan, y = fine pan (UV units/sec)
uniform float u_tailFadeA;      // segment fade start (0 = head, 1 = tail)
uniform float u_tailFadeB;      // segment fade end — tail dissolves to zero here

uniform vec4  u_bandShape;      // x = bundle half-width, y = edge softness, z = HDR gain, w = strand gain
uniform vec4  u_strandFlow;     // x = B-channel flow strength, y = bundle 3 weight, z = stretchUV
uniform vec4  u_tailShape;      // x = stagger, y = extra tail dissolve, z = tail narrow
uniform vec2  u_pathArc;        // x = metres at HEAD node, y = laid length (m)
uniform vec3  u_colHot;         // HDR core colour
uniform vec3  u_colTail;        // colour at the TAIL; vColor.rgb is the head
uniform float u_wavePhase;      // per-spawn random phase
uniform vec2  u_waveEnv;        // x = head fade fraction
uniform float u_renderPass;     // 0 = BODY (alpha), 1 = EMISSION additive, 2 = EMISSION premultiplied
uniform float u_bodyOpacity;    // coverage scale for the body pass, 0..1
uniform float u_tonemapSafe;    // 1 only for migrated MAGIC trail radiance
uniform vec4  u_contrastParams; // enabled, edgeSharpness, coreSize, coreIntensity
uniform vec2  u_coreShape;      // x = coreHalfWidth, y = coreDensityGate

// ── WebGL Style & 4-Color Ramp Extensions ───────────────────────────────────
uniform float u_style;          // 0 = standard strand, 1..15 = specialized WebGL styles
uniform vec3  u_colRamp[4];     // 4-stop gradient ramp (C0, C1, C2, C3)
uniform vec4  u_p0;
uniform vec4  u_p1;
uniform vec4  u_p2;
uniform vec2  u_uvScale;
uniform float u_seed;
uniform float u_maxDist;
uniform float u_add;
uniform float u_alpha;
uniform vec4  u_wave;

uniform int u_nodeArc;
in float vPathMetres;
in vec2 vSegUV;
in vec4 vColor;

const float TAU = 6.2831853;

float Hash11(float p)
{
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

float Hash21(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Vectorized 4-corner SIMD 2D value noise (4x fewer hash calls, bit-identical)
float VNoise2D(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    vec4 h = vec4(i.x, i.x + 1.0, i.x, i.x + 1.0);
    vec4 hy = vec4(i.y, i.y, i.y + 1.0, i.y + 1.0);
    vec4 p1 = fract(h * 0.1031);
    vec4 p2 = fract(hy * 0.1031);
    vec4 d = p1 * (p2 + 33.33) + p2 * (p1 + 33.33) + p1 * (p1 + 33.33);
    p1 += d;
    p2 += d;
    vec4 r = fract((p1 + p2) * p1);
    return mix(mix(r.x, r.y, f.x), mix(r.z, r.w, f.x), f.y);
}

float Fbm2D(vec2 p)
{
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 4; i++) {
        s += a * VNoise2D(p);
        p = p * 2.03 + vec2(17.1, 9.3);
        a *= 0.5;
    }
    return s;
}

float FlowFbm2D(vec2 uv, vec2 dir, float strength, float t)
{
    vec2 fv = (vec2(VNoise2D(uv * 1.3 + 7.7), VNoise2D(uv * 1.3 + 1.9)) - 0.5) * 2.0 * strength + dir;
    float p0 = fract(t);
    float p1 = fract(t + 0.5);
    float w = abs(1.0 - 2.0 * p0);
    return mix(Fbm2D(uv - fv * p0 * 0.7), Fbm2D(uv - fv * p1 * 0.7 + 0.37), w);
}

vec3 Ramp(float t)
{
    t = clamp(t, 0.0, 1.0);
    if (t < 0.3333) return mix(u_colRamp[0], u_colRamp[1], t * 3.0);
    if (t < 0.6666) return mix(u_colRamp[1], u_colRamp[2], (t - 0.3333) * 3.0);
    return mix(u_colRamp[2], u_colRamp[3], (t - 0.6666) * 3.0);
}

vec3 SampleRamp(float t)
{
    return Ramp(t);
}

vec3 Voronoi2D(vec2 x)
{
    vec2 n = floor(x);
    vec2 f = fract(x);
    float d1 = 8.0, d2 = 8.0;
    vec2 bestCell = n;
    for (int j = -1; j <= 1; j++) {
        for (int i = -1; i <= 1; i++) {
            vec2 g = vec2(float(i), float(j));
            vec2 cell = n + g;
            vec2 of = vec2(Hash21(cell), Hash21(cell + 37.7));
            vec2 r = g + of - f;
            float d = dot(r, r);
            if (d < d1) {
                d2 = d1;
                d1 = d;
                bestCell = cell;
            } else if (d < d2) {
                d2 = d;
            }
        }
    }
    float s1 = sqrt(d1);
    return vec3(s1, sqrt(d2) - s1, Hash21(bestCell + 11.1));
}

float SegDist(vec2 p, vec2 a, vec2 b, float w)
{
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-4), 0.0, 1.0);
    return 1.0 - smoothstep(w, w + 0.04, length(pa - ba * h));
}

float Glyph3x3(vec2 p, float id)
{
    float r = 0.0;
    for (int i = 0; i < 4; i++) {
        float fi = float(i);
        vec2 a = floor(vec2(Hash11(id + fi * 1.7), Hash11(id + fi * 3.1 + 5.0)) * 3.0) / 2.0;
        vec2 b = floor(vec2(Hash11(id + fi * 5.3 + 9.0), Hash11(id + fi * 2.3 + 13.0)) * 3.0) / 2.0;
        r = max(r, SegDist(p, a * 0.7 + 0.15, b * 0.7 + 0.15, 0.045));
    }
    return r;
}

vec3 TrailToneMapSafeRadiance(vec3 radiance)
{
    float peak = max(radiance.r, max(radiance.g, radiance.b));
    float safeWeight = clamp((peak - 1.25) / max(peak, 0.00001), 0.0, 1.0);
    return mix(radiance, VFX_TonemapSafeHDR(radiance), safeWeight);
}

vec4 ResolvePass(vec3 colour, float inten, float vAlpha, float gain)
{
    if (u_renderPass < 0.5)
    {
        if (u_bodyOpacity <= 0.001) discard;
        float bodyMask = VFXContrast_BodyMask(inten, u_contrastParams);
        float coverage = clamp(bodyMask * vAlpha * u_bodyOpacity, 0.0, 1.0);
        if (coverage < 1e-4) discard;
        return VFX_ResolveBody(colour, 1.0, coverage);
    }
    if (u_renderPass < 1.5)
    {
        return VFX_ResolveEmission(colour, gain, 1.0, inten * vAlpha);
    }
    float cover = clamp(inten * vAlpha, 0.0, 1.0);
    vec3 radiance = VFX_Finite3(colour * max(gain, 0.0) * cover);
    if (u_tonemapSafe > 0.5)
        radiance = TrailToneMapSafeRadiance(radiance);
    return vec4(radiance, cover);
}

vec4 ResolvePassEx(vec3 bodyCol, vec3 emissiveCol, float inten, float vAlpha, float gain, bool isAdditive)
{
    float a = clamp(inten * vAlpha, 0.0, 1.0);
    if (u_renderPass < 0.5)
    {
        if (u_bodyOpacity <= 0.001 || isAdditive) discard;
        float coverage = clamp(a * u_bodyOpacity, 0.0, 1.0);
        if (coverage < 1e-4) discard;
        return VFX_ResolveBody(bodyCol, 1.0, coverage);
    }
    float outAlpha = isAdditive ? 0.0 : a;
    vec3 radiance = VFX_Finite3(emissiveCol * max(gain, 0.0) * a);
    if (u_tonemapSafe > 0.5)
        radiance = TrailToneMapSafeRadiance(radiance);
    return vec4(radiance, outAlpha);
}

void main()
{
    // Early-out before any texture fetch or procedural noise if this is a Body
    // pass for a pure-emissive trail or a transparent vertex.
    if (vColor.a <= 0.001) discard;
    if (u_renderPass < 0.5 && u_bodyOpacity <= 0.001) discard;

    // ── MODE 0: Passthrough texture with vertex deform ──────────────────────
    if (u_matMode < 0.5)
    {
        vec4 texel = texture(texture0, vSegUV);
        if (texel.a * vColor.a < 0.003) discard;
        finalColor = ResolvePass(texel.rgb * vColor.rgb, texel.a, vColor.a, max(u_bandShape.z, 1.0));
        return;
    }

    float along = vSegUV.y;                   // 0 = head .. 1 = tail
    float across = (vSegUV.x - 0.5) * 2.0;    // -1 .. 1 across the width
    float metres = (u_nodeArc != 0) ? vPathMetres : (u_pathArc.x - along * u_pathArc.y);

    // ── MODE 1: Packed 4-channel Wisp Material (R=coarse, G=fine, B=dissolve, A=turbulence)
    if (u_matMode < 1.5)
    {
        vec2 tile = max(u_tiling, vec2(0.01));
        float panC = SurfaceFlow_Pan(u_time, u_panSpeed.x);
        float panF = SurfaceFlow_Pan(u_time, u_panSpeed.y);
        vec2 uvCoarse = vec2(vSegUV.x * tile.y, fract(along * tile.x - panC));
        vec2 uvFine   = vec2(vSegUV.x * tile.y, fract(along * tile.x * 1.35 - panF));

        vec4 sCoarse = texture(texture0, uvCoarse);
        vec4 sFine   = texture(texture0, uvFine);

        float turb = (sFine.a - 0.5) * 0.35;
        float mixF = clamp(u_wispMix + turb, 0.0, 1.0);
        float wisp = mix(sCoarse.r, sFine.g, mixF);

        float edgeFade = 1.0 - smoothstep(0.75, 1.0, abs(across));
        float thr = u_dissolve * along + (1.0 - edgeFade) * 0.25;
        float disMask = smoothstep(thr, thr + max(u_dissolveSoft, 0.01), sCoarse.b);
        float tailBand = (u_tailFadeA >= u_tailFadeB)
                             ? 1.0
                             : 1.0 - smoothstep(u_tailFadeA, u_tailFadeB, along);

        float inten = wisp * edgeFade * disMask * tailBand;
        if (inten < 0.004) discard;

        vec3 col = mix(vColor.rgb, u_colTail, along);
        if (dot(u_colTail, u_colTail) < 1e-4) col = vColor.rgb;
        finalColor = ResolvePass(col, inten, vColor.a, max(u_bandShape.z, 1.0));
        return;
    }

    // ── PROCEDURAL PRESET STYLES (WebGL Trail Lab Pipeline) ─────────────────
    if (u_style > 0.5)
    {
        if (u_renderPass < 0.5 && u_add > 0.5) discard;

        float v = vSegUV.x;                   // 0..1 across width (vUV.y in WebGL)
        float e = 1.0 - abs(2.0 * v - 1.0);   // edge falloff (1 - abs(2v - 1))
        float a = vSegUV.y;                   // age 0 (head) .. 1 (tail) (vAge in WebGL)
        float vDist = a * max(u_pathArc.y, 1.0);
        if (u_maxDist > 0.0 && vDist > u_maxDist) discard;

        float vS = metres;                    // stationary world arc-length
        float u = -vS * (u_uvScale.x > 0.001 ? u_uvScale.x : 1.0);
        float vShade = sin(vS * u_wave.y + u_time * u_wave.z + u_seed * 6.2831853);
        float vSpeed = 0.85;

        vec4 c = vec4(0.0);
        int st = int(u_style + 0.5);

        if (st == 1) // Kiếm Khí (Blade / Sword Qi)
        {
            float vb = (v - 0.5) / 1.25 + 0.5;
            float n = Fbm2D(vec2(u * u_p0.x - u_time * u_p0.y, vb * u_p0.z));
            float edge = pow(clamp(vb, 0.0, 1.0), u_p0.w);
            float f = edge * 0.8 + n * 0.45;
            float thr = a * 1.2 - 0.08;
            float m = smoothstep(thr - 0.05, thr + 0.22, f);
            float core = smoothstep(thr + 0.15, thr + 0.7, f);
            float fade = pow(clamp(1.0 - a, 0.0, 1.0), u_p1.x);
            float tip = pow(clamp(vb, 0.0, 1.0), 10.0) * (1.0 - a);
            float outside = exp(-max(vb - 1.0, 0.0) * 9.0) * smoothstep(1.2, 0.98, vb);
            float inner = smoothstep(-0.02, 0.35, vb);
            c = vec4(Ramp(core * 0.7 + tip * 0.5 + 0.08) * (1.0 + tip * 1.2), m * fade * inner * outside);
        }
        else if (st == 2) // Phi Kiếm (Flying Sword Comet / Streak)
        {
            float n = Fbm2D(vec2(u * u_p0.x - u_time * u_p0.y, v * u_p0.z));
            float core = pow(e, u_p1.x);
            float glow = pow(e, u_p1.y);
            float sPulse = 0.5 + 0.5 * sin(u * u_p0.w - u_time * u_p2.x);
            float pulse = sPulse * sPulse * sPulse;
            float fade = pow(clamp(1.0 - a, 0.0, 1.0), u_p1.z);
            float hh = exp(-vDist * 8.0);
            float body = glow * (0.35 + 0.9 * n) + pulse * glow * 0.5;
            c = vec4(Ramp(clamp(body * 0.7 + core * 0.8 + hh, 0.0, 1.0)) * (1.0 + core * 1.5), (body * 0.8 + core + hh * 0.6) * fade);
        }
        else if (st == 3) // Tiên Đới (Celestial Silk)
        {
            float sh = 0.65 + 0.35 * vShade;
            float nSilk = Fbm2D(vec2(u * 0.8, v * 2.0));
            float lines = 0.5 + 0.5 * sin(v * u_p0.x + nSilk * 3.0);
            float band = smoothstep(0.7, 1.0, sin(u * u_p0.y - u_time * u_p0.z));
            float border = (1.0 - smoothstep(0.0, 0.035, abs(v - 0.13))) + (1.0 - smoothstep(0.0, 0.035, abs(v - 0.87)));
            vec3 base = Ramp(v * 0.9 + nSilk * 0.25);
            vec3 col = base * sh * (0.85 + 0.15 * lines) + band * 0.3 * u_colRamp[3] + border * vec3(1.0, 0.85, 0.5) * 0.7;

            float headHot = exp(-vDist * 6.5) * pow(e, 0.65);
            col += vec3(1.0, 0.9, 0.8) * headHot * 0.75;

            c = vec4(col, smoothstep(0.0, 0.1, e) * pow(clamp(1.0 - a, 0.0, 1.0), u_p1.x) * 0.95);
        }
        else if (st == 4) // Linh Toàn (Dash / Wood Spiral Qi)
        {
            float fr = fract(u * u_p0.x - u_time * u_p0.y);
            float dash = smoothstep(0.15, 0.35, fr) * (1.0 - smoothstep(0.65, 0.85, fr));
            float core = pow(e, u_p1.x);
            float glow = e * sqrt(max(e, 0.0));
            float fade = pow(clamp(1.0 - a, 0.0, 1.0), u_p1.y) * smoothstep(0.0, 0.1, vDist);
            c = vec4(Ramp(0.3 + 0.7 * core) * (1.0 + core), (glow * 0.6 * (0.4 + 0.6 * dash) + core * 0.9) * fade);
        }
        else if (st == 5) // Lôi Điện (Lightning Strike)
        {
            float flick = 0.55 + 0.45 * Hash21(vec2(floor(u_time * u_p0.x), u_seed));
            float core = pow(e, u_p0.y);
            float glow = pow(e, u_p0.z);
            float fade = pow(clamp(1.0 - a, 0.0, 1.0), u_p1.x);
            float tw = 0.6 + 0.4 * sin(u * 30.0 - u_time * 60.0);
            c = vec4(mix(Ramp(0.5), vec3(1.0), core) * (1.0 + 2.0 * core), (glow * 0.55 * tw + core) * fade * flick);
        }
        else if (st == 6) // Hỏa Diễm (Rising Fire Jet)
        {
            vec2 q = vec2(v * u_p0.z, u);
            float f = FlowFbm2D(q, vec2(0.0, u_p0.y), u_p0.w, u_time * u_p0.x);
            float heat = e * (1.0 - a * u_p1.x) + (f - 0.5) * u_p1.y + exp(-vDist * 5.0) * 0.5;
            float m = smoothstep(0.12, 0.4, heat);
            c = vec4(Ramp(heat * 1.3), m * (1.0 - a * a) * 1.2);
        }
        else if (st == 7) // Long Khí (Dragon Qi Scales / Tube)
        {
            float fres = pow(1.0 - e, u_p1.x);
            vec2 g = vec2(u * u_p0.x - u_time * u_p0.y, v * u_p0.z);
            g.x += mod(floor(g.y), 2.0) * 0.5;
            float dd = length(fract(g) - 0.5);
            float scale = smoothstep(0.62, 0.28, dd);
            float edgeL = smoothstep(0.28, 0.5, dd);
            const float lam = 0.4 + 0.6 * 0.78446454; // precomputed dot((0,1,0), normalize(0.4,0.8,0.5))
            vec3 col = Ramp(0.25 + 0.5 * scale + 0.25 * lam) * (0.55 + 0.7 * scale) * lam + fres * Ramp(0.95) * 1.2 - edgeL * 0.15;
            float fade = pow(clamp(1.0 - a, 0.0, 1.0), u_p1.y);
            c = vec4(col + exp(-vDist * u_p1.z) * Ramp(1.0), (0.35 + 0.5 * scale + fres) * fade);
        }
        else if (st == 8) // Mặc Tích (Calligraphy Ink Wash)
        {
            float n = Fbm2D(vec2(u * 0.7, v * 22.0));
            float rag = (VNoise2D(vec2(u * 5.0, v * 9.0)) - 0.5) * 0.35;
            float dry = smoothstep(0.25, 0.6, n * 0.9 + (1.0 - vSpeed) * 0.8);
            float body = smoothstep(0.12, 0.4, e + rag) * dry;
            float bleed = smoothstep(0.0, 0.4, a) * smoothstep(0.0, 0.6, e) * 0.25;
            float fade = 1.0 - smoothstep(0.6, 1.0, a);
            c = vec4(mix(u_colRamp[0], u_colRamp[1], n * 0.7), (body + bleed * (1.0 - body)) * fade);
        }
        else if (st == 9) // Phù Văn (Talisman Runes)
        {
            vec2 g = vec2(vS * u_p0.x - u_time * u_p0.y, clamp((v - 0.12) / 0.76, 0.0, 1.0) * u_p0.z);
            vec2 id = floor(g);
            float gid = Hash21(id + u_seed);
            float gl = Glyph3x3(fract(g), gid * 100.0) * step(0.12, v) * step(v, 0.88);
            float pulse = 0.6 + 0.4 * sin(u_time * 2.0 + gid * 20.0);
            float line = (1.0 - smoothstep(0.0, 0.03, abs(v - 0.06))) + (1.0 - smoothstep(0.0, 0.03, abs(v - 0.94)));
            float fade = pow(clamp(1.0 - a, 0.0, 1.0), u_p1.x);
            c = vec4(Ramp(0.4 + 0.6 * gl) * (1.0 + gl), (gl * pulse + line * 0.8 + 0.12 * e) * fade);
        }
        else if (st == 10) // Hàn Băng (Ice Crystal Shards)
        {
            vec3 vo = Voronoi2D(vec2(u * u_p0.x, v * u_p0.y));
            float facet = 0.55 + 0.45 * vo.z;
            float edgeL = 1.0 - smoothstep(0.0, 0.08, vo.y);
            float hSpark = Hash11(vo.z * 91.0 + floor(u_time * 4.0 + vo.z * 10.0));
            float h2 = hSpark * hSpark;
            float h4 = h2 * h2;
            float spark = h4 * h4 * h4; // hSpark^12 without pow()
            float fade = pow(clamp(1.0 - a, 0.0, 1.0), u_p1.x);
            c = vec4(Ramp(facet * 0.7 + edgeL * 0.3) * (1.0 + edgeL * 0.8 + spark * 4.0), smoothstep(0.0, 0.35, e) * fade * (0.5 + 0.5 * facet));
        }
        else if (st == 11) // Phong Nhận (Wind Blade / Slashes)
        {
            float sp = u_p0.y * (1.0 + 0.6 * Hash11(u_seed * 7.0));
            float s = VNoise2D(vec2(u * u_p0.x - u_time * sp, u_seed * 10.0));
            float streak = smoothstep(0.35, 0.9, s);
            float fade = pow(clamp(1.0 - a, 0.0, 1.0), u_p1.x) * smoothstep(0.0, 0.08, vDist);
            c = vec4(Ramp(0.4 + 0.6 * streak), pow(e, u_p0.z) * (0.45 + 1.1 * streak) * fade);
        }
        else if (st == 12) // Thập Tự Quang (Cross Radiant Beam)
        {
            float n = Fbm2D(vec2(u * u_p0.x - u_time * u_p0.y, v * 3.0));
            float core = pow(e, u_p1.x);
            float glow = pow(e, u_p1.y);
            float pulse = 0.5 + 0.5 * sin(u * u_p0.z - u_time * u_p0.w);
            float fade = pow(clamp(1.0 - a, 0.0, 1.0), u_p1.z);
            c = vec4(Ramp(clamp(glow * 0.5 + core * 0.6 + n * 0.3, 0.0, 1.0)) * (1.0 + core), (glow * (0.4 + 0.6 * n) * 0.8 + core * (0.6 + 0.4 * pulse)) * fade);
        }
        else if (st == 13) // Yên Vụ & Ma Khí (Smoke / Mist / Demonic Contour)
        {
            vec2 q = vec2(v * u_p0.z + u_seed * 3.1, u * u_p0.x);
            float f = FlowFbm2D(q, vec2(0.0, u_p0.y), u_p0.w, u_time * u_p2.x + u_seed);
            float f2 = Fbm2D(vec2(q.x * 2.1 + 3.0, q.y * 2.3 - u_time * u_p0.y * 1.4));
            float dens = f * 0.75 + f2 * 0.35;
            float s = pow(e, u_p1.z) * (0.35 + dens);
            float fade = pow(clamp(1.0 - a, 0.0, 1.0), u_p1.y) * smoothstep(0.0, 0.1, vDist);
            if (u_p2.w > 0.5)
            {
                float band = smoothstep(u_p1.x, u_p1.x + 0.1, s) * (1.0 - smoothstep(u_p1.x + 0.1, u_p1.x + 0.35, s));
                c = vec4(Ramp(0.35 + 0.65 * band) * 1.4, band * fade * 1.3);
            }
            else
            {
                float t_col = dens * u_p2.y + (1.0 - e) * u_p2.z + (1.0 - a) * u_p1.w;
                c = vec4(Ramp(t_col), smoothstep(u_p1.x, u_p1.x + 0.45, s) * fade);
            }
        }
        else if (st == 14) // Tinh Hà (Nebula / Star River)
        {
            float n = Fbm2D(vec2(u * u_p0.x - u_time * u_p0.y, v * u_p0.z));
            float n2 = Fbm2D(vec2(u * u_p0.x * 2.1 + 7.0 - u_time * u_p0.y * 1.6, v * u_p0.z * 2.3 + 3.0));
            float body = pow(e, u_p1.x) * (0.4 + 0.9 * n);
            vec2 sg = vec2(vS * u_p0.w, v * 8.0);
            vec2 id = floor(sg);
            vec2 lp = fract(sg) - 0.5;
            float r_star = Hash21(id + u_seed);
            vec2 off = (vec2(Hash21(id + 3.1), Hash21(id + 7.7)) - 0.5) * 0.5;
            float star = step(0.8, r_star) * (1.0 - smoothstep(0.0, 0.14, length(lp - off))) *
                         (0.5 + 0.5 * sin(u_time * 3.0 + r_star * 40.0)) * smoothstep(0.0, 0.5, e);
            float fade = pow(clamp(1.0 - a, 0.0, 1.0), u_p1.y) * smoothstep(0.0, 0.08, vDist);

            float headHot = exp(-vDist * 7.0) * pow(e, 0.4);
            vec3 starCol = vec3(2.5, 2.3, 3.0) * star;
            vec3 col = Ramp(clamp(body * 0.9 + n2 * 0.25, 0.0, 1.0)) * (1.2 + 0.8 * body) + starCol + vec3(1.8, 1.5, 2.2) * headHot * 3.0;

            c = vec4(col, (body * 0.9 + star + headHot * 0.5) * fade);
        }
        else if (st == 15) // Thái Cực (Twin Yin-Yang Helix)
        {
            float fr = fract(u * u_p0.x - u_time * u_p0.y);
            float flow = 0.78 + 0.22 * smoothstep(0.1, 0.5, fr) * (1.0 - smoothstep(0.6, 0.9, fr));
            float core = smoothstep(0.0, 0.7, e);
            float fade = pow(clamp(1.0 - a, 0.0, 1.0), u_p1.x) * smoothstep(0.0, 0.1, vDist);
            c = vec4(mix(u_colRamp[1], u_colRamp[0], core) * flow, sqrt(max(e, 0.0)) * fade);
        }

        float pAlpha = c.a * u_alpha;
        float alphaFootprint = max(fwidth(pAlpha), 1e-5);
        pAlpha *= smoothstep(0.003 - alphaFootprint,
                             0.003 + alphaFootprint, pAlpha);
        if (pAlpha < 1e-5) discard;

        bool isAdditive = (u_add > 0.5);
        vec3 emissiveCol = c.rgb;
        vec3 bodyCol = clamp(c.rgb, 0.0, 1.0);

        finalColor = ResolvePassEx(bodyCol, emissiveCol, pAlpha, vColor.a, max(u_bandShape.z, 1.0), isAdditive);
        return;
    }

    // ── MODE 2: Sin-Wave Strand Trail ────────────────────────────────────────
    float edgeMask = 1.0 - smoothstep(1.0 - max(u_bandShape.y, 0.01), 1.0, abs(across));
    if (edgeMask <= 0.001) discard;

    float tailBand = (u_tailFadeA >= u_tailFadeB)
                         ? 1.0
                         : 1.0 - smoothstep(u_tailFadeA, u_tailFadeB, along);
    if (tailBand <= 0.001) discard;

    float ramp = UVDeform_Envelope(UV_ENV_HEAD_WELD, along, 0.0,
                                   max(u_waveEnv.x, 0.001));

    float panA = SurfaceFlow_Pan(u_time, u_panSpeed.x);
    float panB = SurfaceFlow_Pan(u_time, u_panSpeed.y);

    // ── Phase 1/2: three independent wave fields (Sin01/02/03) ───────────────
    vec2 driveMat = vec2(metres, along);
    float w0 = UVDeform_LayerOffset(u_uvField[0], u_uvField[1], u_uvField[2],
                                    vSegUV, driveMat, u_time, vec2(0.5)).x;
    float w1 = UVDeform_LayerOffset(u_uvField[3], u_uvField[4], u_uvField[5],
                                    vSegUV, driveMat, u_time, vec2(0.5)).x;
    float w2 = UVDeform_LayerOffset(u_uvField[6], u_uvField[7], u_uvField[8],
                                    vSegUV, driveMat, u_time, vec2(0.5)).x;

    // ── Phase 3: flow distortion from the B channel (only sampled if active) ─
    if (u_strandFlow.x > 1e-4)
    {
        vec2 fuv = vec2(vSegUV.x, fract(metres * u_tiling.x * 0.5));
        float b1 = texture(texture0, fuv + vec2(0.0, -panA)).b - 0.5;
        float b2 = texture(texture0, fuv * 1.37 + vec2(0.29, -panB * 0.7)).b - 0.5;
        float flowScale = u_strandFlow.x * ramp;
        w0 += (b1 + b2) * flowScale;
        w1 += (b1 - b2) * flowScale * 0.90;
        w2 -= (b1 + b2 * 0.6) * flowScale * 0.80;
    }

    float endMid = clamp(u_tailFadeA, 0.0, 1.0);
    float endLate = clamp(u_tailFadeA + u_tailShape.x, 0.0, 1.0);
    float endEarly = clamp(u_tailFadeA - u_tailShape.x, 0.0, 1.0);
    float dying = smoothstep(endEarly, 1.0, along);

    float bundleHW = max(u_bandShape.x, 0.02) *
                     mix(1.0, clamp(u_tailShape.z, 0.05, 1.0), dying);

    float vBase = metres * u_tiling.x;
    bool stretch = u_strandFlow.z > 0.5;
    float v0 = SurfaceFlow_AlongV(along, vBase, 1.00, panA, stretch);
    float v1 = SurfaceFlow_AlongV(along, vBase, 1.60, panB, stretch);
    float v2 = SurfaceFlow_AlongV(along, vBase, 0.70, panA * 0.55, stretch);
    float s0 = texture(texture0, vec2(SurfaceFlow_AcrossU(across, w0, bundleHW), v0)).r;
    float s1 = texture(texture0, vec2(SurfaceFlow_AcrossU(across, w1, bundleHW * 0.72), v1)).g;
    float s2 = texture(texture0, vec2(SurfaceFlow_AcrossU(across, w2, bundleHW * 1.35), v2)).r;

    float d0 = abs(across - w0) / bundleHW;
    float d1 = abs(across - w1) / (bundleHW * 0.72);
    float d2 = abs(across - w2) / (bundleHW * 1.35);
    float env0 = 1.0 - smoothstep(0.04, 1.0, d0);
    float env1 = 1.0 - smoothstep(0.04, 1.0, d1);
    float env2 = 1.0 - smoothstep(0.04, 1.0, d2);
    s0 *= env0;
    s1 *= env1;
    s2 *= env2;

    float end0 = 1.0 - smoothstep(endMid, 1.0, along);
    float end1 = 1.0 - smoothstep(endLate, 1.0, along);
    float end2 = 1.0 - smoothstep(endEarly, 1.0, along);
    s0 *= end0;
    s1 *= end1;
    s2 *= end2;

    float coreOut = max(u_coreShape.x, 0.004);
    float coreIn  = coreOut * 0.1111;
    float wispMixClamp = clamp(u_wispMix, 0.0, 1.0);
    float b2WeightClamp = clamp(u_strandFlow.y, 0.0, 1.0);
    float core0 = (1.0 - smoothstep(coreIn, coreOut, d0)) * end0;
    float core1 = (1.0 - smoothstep(coreIn, coreOut, d1)) * end1 * wispMixClamp;
    float core2 = (1.0 - smoothstep(coreIn, coreOut, d2)) * end2 * b2WeightClamp;
    float centreCore = max(max(core0, core1), core2);

    float strand = max(max(s0, s1 * wispMixClamp), s2 * b2WeightClamp);
    strand = pow(clamp(strand, 0.0, 1.0), max(u_bandShape.w, 0.05));

    // ── Phase 5: dissolve (A channel) ────────────────────────────────────────
    float dis = texture(texture0, vec2(vSegUV.x * 0.83 + 0.17,
                                       SurfaceFlow_AlongV(along, vBase, 0.45,
                                                          panB * 0.35, false))).a;
    float thr = u_dissolve * ramp + u_tailShape.y * dying;
    float dissolveMask2 = smoothstep(thr, thr + max(u_dissolveSoft, 0.01), dis);

    float commonMask = edgeMask * dissolveMask2 * tailBand;
    float inten = strand * commonMask;
    float hotSignal = centreCore * commonMask;
    if (max(inten, hotSignal) < 0.004)
        discard;

    // ── Phase 6: colour ──────────────────────────────────────────────────────
    vec3 lengthCol = mix(vColor.rgb, u_colTail, along);
    if (dot(u_colRamp[1], u_colRamp[1]) + dot(u_colRamp[2], u_colRamp[2]) > 0.0001) {
        lengthCol = SampleRamp(along);
    }

    vec3 hot;
    float passIntensity;
    if (u_renderPass < 0.5)
    {
        hot = mix(lengthCol, u_colHot, hotSignal * 0.65);
        passIntensity = max(inten, hotSignal * 0.65);
    }
    else
    {
        float densGate = clamp(u_coreShape.y, 0.05, 0.98);
        float hotMix = max(smoothstep(densGate, 1.0, inten),
                           smoothstep(0.08, 0.45, hotSignal));
        float coreTrans = smoothstep(0.60, 1.0, hotMix);
        vec3 elemCol = mix(lengthCol * (0.8 + inten * 0.4), vec3(3.2), coreTrans);

        float isFire = step(lengthCol.b, lengthCol.r * 0.65) * step(0.35, lengthCol.r);
        if (isFire > 0.5)
        {
            float flameTemp = clamp(mix(inten * 0.55, 1.0, hotSignal * hotSignal) * (1.0 - along * 0.30), 0.0, 1.0);
            hot = calcBlackbodyNormalized(flameTemp);
        }
        else
        {
            hot = elemCol;
        }
        passIntensity = max(inten, hotSignal);
    }

    finalColor = ResolvePass(hot, passIntensity, vColor.a, u_bandShape.z);
}
