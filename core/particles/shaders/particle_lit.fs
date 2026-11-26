#version 330
// The rlvk sampler resolver maps the draw-call texture by reflected name, not a
// presumed descriptor binding, so this shader may safely use texture0 plus the
// soft-particle scene-depth sampler (HANDOFF §7.30).

// Đợt E / F1 — lit CPU particles. See core/docs/ELDEN_VFX_SPEC.md §0.1b:
// flat-shaded smoke can only ever look like a decal OF smoke. Volume reads
// almost entirely from lighting — a bright rim toward the light, a dark
// occluded core, and a glow when backlit.
//
// Everything is opt-in: at u_lightingStrength = 0 this returns the legacy
// `texelColor * fragColor` result, so nothing already shipped changes look.

#ifdef GL_ES
precision highp float;
#endif

in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragPosition;

out vec4 finalColor;

uniform sampler2D texture0;
uniform vec2 u_resolution;

#include "core/shaders/common/soft_particle.glsl"
#include "core/shaders/common/vfx_composite.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/lighting.glsl"

uniform float u_time;
uniform int   u_surfaceMode; // 0 = ALPHA, 1 = ADDITIVE, 2 = PREMULTIPLIED

/* BACKGROUND ADAPTATION — how bright is what this particle is in front of. */
uniform sampler2D u_bgLuma;
uniform float u_bgAdapt;

uniform float u_softFade;
uniform float u_softDebug;

// Environment (set per frame by DrawParticles from environment_system)
uniform vec3 u_sunToLight;   // normalized, surface → sun
uniform vec3 u_sunColor;     // linear-ish 0..1
uniform vec3 u_ambient;      // flat ambient fill
uniform vec3 viewPos;

uniform float u_lightingStrength; // 0 = legacy unlit path (DEFAULT)
uniform float u_scatterStrength;  // forward-scatter / backlit glow
uniform float u_debugNormal;      // 0 = off, 1 = screen normal, 2 = pure wrap, 3 = light dir
uniform float u_normalBulge;      // 1 = true hemisphere; >1 exaggerates the dome
uniform float u_emissiveBoost;    // >1 = HDR headroom for glowing cores
uniform float u_analyticUV;       // 1 = quad-local UV (default), 0 = derivative fallback
uniform vec2  u_atlasGrid;        // (cols, rows); (1,1) = not an atlas

// ── PACKED VOLUME SHEET ─────────────────────────────────────────────────────
uniform float u_volumeSheet;
uniform sampler2D u_rampLUT;
uniform float u_heatGain;         // exposure on emission before the ramp lookup
uniform vec3  u_smokeTint;        // body colour of the soot half
uniform float u_smokeGain;

// ── OPTICAL FLOW MOTION VECTOR WARPING (Flipbook Subframe Advection) ─────
uniform float u_useMotionVectors;
uniform sampler2D u_motionTex;
uniform float u_motionWarp;

uniform float u_lightAzimuth;     // <0 = use the real sun; >=0 = debug override
uniform float u_sunGain;          // scales the directional term
uniform float u_ambientGain;      // scales the flat fill (LOWER = more contrast)

#define MAX_VFX_LIGHTS 4
uniform int   u_vfxLightCount;
uniform vec3  u_vfxLightPos[MAX_VFX_LIGHTS];
uniform vec3  u_vfxLightColor[MAX_VFX_LIGHTS];
uniform float u_vfxLightRadius[MAX_VFX_LIGHTS];

// ── 6-WAY VOLUMETRIC LIGHTING (Unity VFX Graph technique) ────────────────────
uniform float u_sixWayLighting;
uniform sampler2D u_sixWayTexB;
uniform float u_sixWayScattering;  // forward-scatter / backlit multiplier
uniform float u_sixWayAbsorption;  // multi-axis extinction factor
uniform vec3  u_ambientGround;     // multi-directional ambient ground bounce (-Y)
uniform vec3  u_ambientHorizon;    // multi-directional ambient horizon fill (sides)

uniform sampler2D u_normalTex;

// ── Fast Math Helpers (Avoid SFU pow(x, 1.5) & NaN on zero vectors) ─────────
float Pow15Safe(float x)
{
    float c = max(x, 0.0);
    return c * sqrt(c);
}

vec3 SafeNormalize(vec3 v, vec3 fallback)
{
    float lenSq = dot(v, v);
    return (lenSq > 1e-8) ? (v * inversesqrt(lenSq)) : fallback;
}

// Recover quad-local UV [0, 1] without seam wrapping at the 1.0 boundary.
vec2 GetQuadLocalUV(vec2 uv)
{
    if (u_atlasGrid.x > 1.5 || u_atlasGrid.y > 1.5)
    {
        vec2 clampedUV = clamp(uv, vec2(0.0), vec2(0.99999));
        return fract(clampedUV * u_atlasGrid);
    }
    return uv;
}

// ── Shared shading pieces ────────────────────────────────────────────────────

// Screen-space hemisphere normal. `texA` is the sprite alpha, read only by the
// derivative fallback and micro-surface erosion perturbation.
vec3 ParticleNormalLocal(vec2 luv, float texA)
{
    vec2  q = luv * 2.0 - 1.0; // [-1,1] across the quad
    float rrSq = dot(q, q);
    vec3  n;
    if (u_analyticUV > 0.5)
    {
        float rcSq = min(rrSq, 1.0);
        float invRr = (rrSq > 1.0) ? inversesqrt(rrSq) : 1.0;
        vec2  xyDome = (q * invRr) * u_normalBulge;

        // Alpha erosion gradient perturbation (Ghost of Tsushima n ~ ∇ρ):
        // Micro-surface billow perturbation along the density falloff edge.
        vec2  gErosion = vec2(dFdx(texA), dFdy(texA));
        float gLenSq = dot(gErosion, gErosion);
        float gLen = (gLenSq > 1e-10) ? (gLenSq * inversesqrt(gLenSq)) : 0.0;
        vec2  gDir = (gLenSq > 1e-10) ? (gErosion * inversesqrt(gLenSq)) : vec2(0.0);
        float rc = sqrt(rcSq);
        vec2  xy = mix(xyDome, -gDir * (rc * u_normalBulge), 0.35 * clamp(gLen * 8.0, 0.0, 1.0));
        // Clamp minimum z to 1e-3 so outer corners (rc == 1) with zero bulge never normalize(vec3(0)).
        float z = max(sqrt(max(1.0 - rcSq, 0.0)), 1e-3);
        n = SafeNormalize(vec3(xy, z), vec3(0.0, 0.0, 1.0));
    }
    else
    {
        // Derivative fallback for atlas UVs.
        float a = clamp(texA, 0.0, 1.0);
        vec2  g = vec2(dFdx(a), dFdy(a));
        float gLenSq = dot(g, g);
        vec2  dir = (gLenSq > 1e-12) ? (g * inversesqrt(gLenSq)) : vec2(0.0);
        float r = sqrt(max(1.0 - a, 0.0));
        float z = max(sqrt(a), 1e-3);
        n = SafeNormalize(vec3(-dir * (r * u_normalBulge), z), vec3(0.0, 0.0, 1.0));
    }
    return n;
}

vec3 ParticleNormalXY(vec4 normalSample)
{
    vec2 xy = normalSample.rg * 2.0 - 1.0;
    float z = max(sqrt(max(1.0 - dot(xy, xy), 0.0)), 1e-3);
    return SafeNormalize(vec3(xy, z), vec3(0.0, 0.0, 1.0));
}

// Compute the tangent basis (T, B) of the quad in world space from screen derivatives.
// Numerically stabilized: uses sign(det) instead of dividing by tiny det before normalize().
void ParticleTangentBasis(vec3 V, out vec3 quadT, out vec3 quadB)
{
    vec3 upRef = abs(V.y) > 0.99 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 R = SafeNormalize(cross(upRef, V), vec3(1.0, 0.0, 0.0));
    vec3 U = cross(V, R);

    vec3 dP1 = dFdx(fragPosition);
    vec3 dP2 = dFdy(fragPosition);
    vec2 dU1 = dFdx(fragTexCoord);
    vec2 dU2 = dFdy(fragTexCoord);

    float det = dU1.x * dU2.y - dU1.y * dU2.x;
    if (abs(det) > 1e-9)
    {
        float invSign = (det >= 0.0) ? 1.0 : -1.0;
        vec3 rawT = (dP1 * dU2.y - dP2 * dU1.y) * invSign;
        vec3 rawB = (-dP1 * dU2.x + dP2 * dU1.x) * invSign;
        quadT = SafeNormalize(rawT, R);
        quadB = SafeNormalize(rawB, -U);
    }
    else
    {
        quadT = R;
        quadB = -U; // v runs downward
    }
}

vec3 ParticleNormalWorldFromBasis(vec3 n, vec3 V, vec3 quadT, vec3 quadB)
{
    return SafeNormalize(n.x * quadT - n.y * quadB + n.z * V, V);
}

vec3 ParticleLightDir()
{
    if (u_lightAzimuth >= 0.0)
    {
        float rad = radians(u_lightAzimuth);
        return normalize(vec3(cos(rad), 0.25, sin(rad)));
    }
    return u_sunToLight;
}

// Scene light arriving at a texel with normal N and precomputed view vector V.
vec3 ParticleLightTerm(vec3 N, vec3 V, vec3 L, out float wrapOut)
{
    float ndl  = dot(N, L);
    float wrap = Pow15Safe(ndl * 0.5 + 0.5);
    wrapOut = wrap;

    vec3 lit = u_ambient * u_ambientGain + u_sunColor * (u_sunGain * wrap);

    // ── Forward scatter — the backlit glow (Henyey-Greenstein Two-Lobe Phase) ───
    if (u_scatterStrength > 0.0)
    {
        float cosTheta = dot(-V, L);
        float d1 = max(1.3025 - 1.10 * cosTheta, 1e-3); // g1 = 0.55
        float d2 = max(1.0625 + 0.50 * cosTheta, 1e-3); // g2 = -0.25
        float hgForward  = 0.6975 * inversesqrt(d1) / d1;
        float hgBackward = 0.9375 * inversesqrt(d2) / d2;
        float hgPhase = mix(hgBackward, hgForward, 0.72);
        lit += u_sunColor * (hgPhase * u_scatterStrength * 0.35);
    }

    // ── VFX point lights — early distance-squared culling skips sqrt/div ────
    int lightCount = clamp(u_vfxLightCount, 0, MAX_VFX_LIGHTS);
    for (int i = 0; i < lightCount; i++)
    {
        vec3  toL    = u_vfxLightPos[i] - fragPosition;
        float distSq = dot(toL, toL);
        float rad    = max(u_vfxLightRadius[i], 0.001);
        if (distSq >= rad * rad) continue;

        float invDist = inversesqrt(max(distSq, 1e-6));
        float dist    = distSq * invDist;
        float att     = 1.0 - dist / rad;
        att *= att;

        vec3  Lpt = toL * invDist;
        float w   = Pow15Safe(dot(N, Lpt) * 0.5 + 0.5);
        lit += u_vfxLightColor[i] * (w * att);
    }
    return lit;
}

// ── 6-WAY VOLUMETRIC LIGHTING ────────────────────────────────────────────────
vec3 ParticleLightTerm6Way(vec2 luv, vec3 V, vec3 quadT, vec3 quadB,
                           vec3 sampledMapA, vec3 sampledMapB, float sampledMapBAux,
                           float soot, float selfShadow, float opac, vec3 L,
                           out float wrapOut)
{
    vec3 mapA;
    vec3 mapB;
    float ao = 1.0;

    float scFactor  = (u_sixWayScattering > 0.0 ? u_sixWayScattering : 1.0);
    float absFactor = (u_sixWayAbsorption > 0.0 ? u_sixWayAbsorption : 1.0);

    // Two-lobe Henyey-Greenstein forward scattering using fast rsq
    float cosTheta = dot(L, -V);
    float d1 = max(1.3364 - 1.16 * cosTheta, 1e-3); // g1 = 0.58
    float d2 = max(1.0400 + 0.40 * cosTheta, 1e-3); // g2 = -0.20
    float hgForward  = 0.6636 * inversesqrt(d1) / d1;
    float hgBackward = 0.9600 * inversesqrt(d2) / d2;
    float hgPhase = mix(hgBackward, hgForward, 0.75);
    float fwdScatter = clamp(1.0 + scFactor * 2.2 * hgPhase, 1.0, 8.0);

    if (u_sixWayLighting > 1.5)
    {
        mapA = sampledMapA;
        mapB = sampledMapB;
        ao = (u_volumeSheet < 0.5)
           ? clamp(sampledMapBAux, 0.05, 1.0)
           : clamp(dot(mapA + mapB, vec3(1.0 / 6.0)), 0.05, 1.0);

        mapA = clamp((mapA - 0.04) * (1.0 / 0.96), 0.0, 1.0);
        mapB = clamp((mapB - 0.04) * (1.0 / 0.96), 0.0, 1.0);
        float contrast = max(absFactor, 1.0);
        if (contrast > 1.001)
        {
            mapA = pow(mapA, vec3(contrast));
            mapB = pow(mapB, vec3(contrast));
        }

        mapA.b *= fwdScatter;
    }
    else
    {
        vec2 q = luv * 2.0 - 1.0;
        float rQuad = length(q);
        float dens = clamp(max(soot, opac), 0.0, 1.0);
        float bulge = max(u_normalBulge, 0.2);
        float pX_pos = clamp(0.5 - 0.5 * q.x * bulge, 0.0, 1.0);
        float pX_neg = clamp(0.5 + 0.5 * q.x * bulge, 0.0, 1.0);
        float pY_pos = clamp(0.5 - 0.5 * q.y * bulge, 0.0, 1.0);
        float pY_neg = clamp(0.5 + 0.5 * q.y * bulge, 0.0, 1.0);

        float ext = clamp(absFactor * 1.6, 0.2, 5.0);
        float densExt = dens * ext;
        float tX_pos = Pow15Safe(1.0 - pX_pos * densExt);
        float tX_neg = Pow15Safe(1.0 - pX_neg * densExt);
        float tY_pos = Pow15Safe(1.0 - pY_pos * densExt);
        float tY_neg = Pow15Safe(1.0 - pY_neg * densExt);

        float tZ_back  = pow(clamp(selfShadow, 1e-4, 1.0), 0.6) * fwdScatter;
        float tZ_front = clamp(1.0 - dens * 0.45, 0.15, 1.0) * clamp(1.0 - rQuad * 0.35, 0.1, 1.0);

        mapA = vec3(tX_pos, tY_pos, tZ_back);
        mapB = vec3(tX_neg, tY_neg, tZ_front);
    }

    vec3 L_local = vec3(dot(L, quadT), -dot(L, quadB), cosTheta);
    vec3 L_pos = max(vec3(0.0), L_local);
    vec3 L_neg = max(vec3(0.0), -L_local);
    L_pos *= L_pos;
    L_neg *= L_neg;
    float dirLit = dot(L_pos, mapA) + dot(L_neg, mapB);
    wrapOut = dirLit;

    float effectiveSunGain = max(u_sunGain * 2.2, 2.2);
    vec3 lit = u_sunColor * (effectiveSunGain * dirLit);

    vec3 ambGround  = (dot(u_ambientGround, u_ambientGround) > 1e-8) ? u_ambientGround : (u_ambient * 0.35);
    vec3 ambHorizon = (dot(u_ambientHorizon, u_ambientHorizon) > 1e-8) ? u_ambientHorizon : (u_ambient * 0.65);

    vec3 ambLit = (u_ambient * (u_ambientGain * 0.65) * mapA.g
                + ambGround * (u_ambientGain * 0.45) * mapB.g
                + ambHorizon * (u_ambientGain * 0.35) * ((mapA.r + mapB.r + mapA.b + mapB.b) * 0.25)) * ao;
    lit += ambLit;

    int lightCount = clamp(u_vfxLightCount, 0, MAX_VFX_LIGHTS);
    for (int i = 0; i < lightCount; i++)
    {
        vec3  toL    = u_vfxLightPos[i] - fragPosition;
        float distSq = dot(toL, toL);
        float rad    = max(u_vfxLightRadius[i], 0.001);
        if (distSq >= rad * rad) continue;

        float invDist = inversesqrt(max(distSq, 1e-6));
        float dist    = distSq * invDist;
        float att     = 1.0 - dist / rad;
        att *= att;

        vec3 Lpt = toL * invDist;
        vec3 Lpt_local = vec3(dot(Lpt, quadT), -dot(Lpt, quadB), dot(Lpt, -V));
        vec3 Lpt_pos = max(vec3(0.0), Lpt_local);
        vec3 Lpt_neg = max(vec3(0.0), -Lpt_local);
        Lpt_pos *= Lpt_pos;
        Lpt_neg *= Lpt_neg;
        float ptTransmission = dot(Lpt_pos, mapA) + dot(Lpt_neg, mapB);
        lit += u_vfxLightColor[i] * (ptTransmission * att);
    }

    if (u_sixWayLighting > 1.5 && u_volumeSheet < 0.5)
    {
        float smokeLightY = dot(lit, vec3(0.2126, 0.7152, 0.0722));
        lit = vec3(smokeLightY);
    }
    return lit;
}

// Ghost of Tsushima: Chromaticity-preserving Planck radiance compression.
vec3 CompressFlameRadiance(vec3 col, float heat)
{
    float maxC = max(col.r, max(col.g, col.b));
    if (maxC > 1.0)
    {
        float excess = maxC - 1.0;
        float compressed = 1.0 + excess / (1.0 + excess * 0.40);
        vec3 chrom = col * (1.0 / maxC);
        float coreIncandescence = smoothstep(0.96, 1.0, heat);
        vec3 coreColor = mix(chrom, vec3(1.0), coreIncandescence * 0.40);
        return coreColor * compressed;
    }
    return col;
}

// Unified clean translucent fire shading shared across Shader Tongue (Mode 5),
// Shader Puff (Mode 6), and Flipbook Flame Volume (Path A). Zero black soot;
// gentle u_bgLuma-adaptive chromatic opacity prevents blue-snow washout without
// turning low-heat wisps into opaque blood-red paint.
vec4 ShadeTranslucentFire(vec3 rampCol, float heat, float mask, float heatBoost, float flicker)
{
    float bg = (u_resolution.x > 0.0) ? texture(u_bgLuma, gl_FragCoord.xy / u_resolution).r : 0.0;
    float brightBg = smoothstep(0.12, 0.50, bg);

    // Keep outer flame wisps rich vermilion-tangerine on snow without muddy soot
    vec3 warmRamp = mix(rampCol, vec3(1.0, 0.42, 0.05), brightBg * (1.0 - heat) * 0.35);
    vec3 baseFlame = warmRamp * (0.64 + heatBoost * 0.85) * flicker * (0.30 + heat * 1.18);

    // Narrow incandescent core so the flame corona radiates vivid orange bloom
    float corePop = pow(smoothstep(0.66, 1.0, heat), 2.0);
    vec3 hotCore  = mix(vec3(1.0, 0.78, 0.22), vec3(1.0, 0.96, 0.84), corePop)
                  * (corePop * (1.85 + brightBg * 1.45));

    vec3 col = CompressFlameRadiance((baseFlame + hotCore) * u_emissiveBoost, heat);

    // Monotonic soft opacity: airy on dark arenas, deeper chromatic carrier on snow
    float alphaDark   = clamp(mask * 0.32, 0.0, 0.38);
    float alphaBright = clamp(mask * mix(0.50, 0.64, smoothstep(0.15, 0.75, heat)), 0.0, 0.64);
    float flameAlpha  = mix(alphaDark, alphaBright, brightBg);

    vec3 outRgb = col * mix(mask * 0.78, mask * 0.98, brightBg);
    float peak = max(outRgb.r, max(outRgb.g, outRgb.b));
    if (peak > 8.0) outRgb *= (8.0 + (peak - 8.0) / (1.0 + (peak - 8.0) * 0.35)) / peak;
    return vec4(VFX_Finite3(max(outRgb, vec3(0.0))), flameAlpha);
}

void main()
{
    // ── MODE 5: PROCEDURAL SHADER TONGUE (Zero-flipbook, slender rising tongue) ──
    if (u_volumeSheet > 4.5 && u_volumeSheet < 5.5)
    {
        if (fragColor.a < 0.004) discard;
        float age = fragColor.g;
        float rnd = fragColor.b;
        float t = u_time * (u_heatGain > 0.0 ? u_heatGain : 2.5);
        float x = (fragTexCoord.x - 0.5) * 2.0;
        float y = 1.0 - fragTexCoord.y; // Raylib top-to-bottom UV correction
        float distort = (u_motionWarp > 0.0) ? u_motionWarp : 0.40;

        float w1 = texture(texture0, vec2(x * 0.175 + rnd * 1.25, y * 0.225 - t * 0.069)).r - 0.5;
        x += w1 * distort * 2.2 * smoothstep(0.0, 0.8, y);
        float n = texture(texture0, vec2(x * 0.41 + rnd * 0.9 + w1 * 0.5, y * 0.275 - t * 0.125 + w1 * 0.25)).r;
        float d = texture(texture0, vec2(x * 0.69 + rnd * 0.4 + 1.1, y * 0.5 - t * 0.1625)).r;
        x += (n - 0.5) * distort * 1.3 * y;

        float by = clamp(y * (1.0 / 0.24), 0.0, 1.0);
        float oneMinusBy = 1.0 - by;
        float rb = sqrt(max(1.0 - oneMinusBy * oneMinusBy, 0.0));
        float width = 0.8 * pow(max(1.0 - y, 0.0), 0.8) * rb;
        float field = max(1.0 - abs(x) / max(width, 1e-3), -1.0) * 0.9 +
                      (n - 0.5) * 1.6 * (0.3 + y) + w1 * 0.6 * y + (d - 0.5) * 0.35;

        if (field < -0.4) discard;

        float soft = (u_softFade > 0.0) ? SoftParticle_Factor(u_softFade) : 1.0;
        // Unwarped quad-border fade prevents domain-warped noise from clipping at square UV edges
        float edgeMask = smoothstep(0.0, 0.18, fragTexCoord.x) * smoothstep(1.0, 0.82, fragTexCoord.x) *
                         smoothstep(0.0, 0.16, 1.0 - y);
        float fade = smoothstep(0.0, 0.08, age) * (1.0 - smoothstep(0.75, 1.0, age)) *
                     smoothstep(0.0, 0.06, y) * edgeMask;
        float softVal = (u_smokeGain > 0.0) ? u_smokeGain : 0.5;
        float density = smoothstep(0.05, 0.05 + softVal * 0.5, field);
        float glow = smoothstep(-0.4, 0.1, field) * 0.18;
        float mask = clamp(density + glow, 0.0, 1.0) * fade * 0.58 * fragColor.a * soft;
        if (mask < 0.004) discard;

        float hf = smoothstep(0.05, 0.96, field) * (1.0 - y * 0.72) + (d - 0.5) * 0.36 - age * 0.20;
        float heat = clamp(hf, 0.0, 1.0) * smoothstep(-0.08, 0.45, field);
        float fl = 0.88 + 0.12 * sin(u_time * 14.0 + rnd * 60.0);

        vec3 rampCol = texture(u_rampLUT, vec2(clamp(heat, 0.0, 1.0), 0.5)).rgb;
        vec3 lutProbe = texture(u_rampLUT, vec2(0.75, 0.5)).rgb;
        if (dot(lutProbe, lutProbe) < 0.001) rampCol = calcBlackbodyNormalized(heat);

        finalColor = ShadeTranslucentFire(rampCol, heat, mask, fragColor.r, fl);
        return;
    }

    // ── MODE 6: PROCEDURAL SHADER PUFF (Zero-flipbook, rolling radial billow) ─────
    if (u_volumeSheet > 5.5 && u_volumeSheet < 6.5)
    {
        if (fragColor.a < 0.004) discard;
        float age = fragColor.g;
        float rnd = fragColor.b;
        float distort = (u_motionWarp > 0.0) ? u_motionWarp : 0.40;
        vec2 p = (fragTexCoord - 0.5) * 2.0;
        vec2 off = vec2(0.0, -age * 0.35);

        float w = texture(texture0, p * 0.22 + rnd * 1.3 + off).r - 0.5;
        p += w * distort * 1.2 * (0.4 + length(p));
        float r = length(p);
        float n = texture(texture0, p * 0.45 + rnd * 0.9 + vec2(1.7, 0.0) + off * 1.5).r;
        float d = texture(texture0, p * 0.9 + rnd * 0.4 + 3.1 + off * 2.0).r;

        float field = (1.0 - r) * 1.15 + (n - 0.5) * 1.4 * (0.3 + r) + (d - 0.5) * 0.35 - age * 0.55;
        if (field < -0.4) discard;

        float soft = (u_softFade > 0.0) ? SoftParticle_Factor(u_softFade) : 1.0;
        // Unwarped radial quad-border fade prevents warped billow from clipping at square UV edges
        float rawR = length((fragTexCoord - 0.5) * 2.0);
        float edgeMask = smoothstep(1.0, 0.62, rawR);
        float fade = smoothstep(0.0, 0.05, age) * (1.0 - smoothstep(0.55, 1.0, age)) * edgeMask;
        float softVal = (u_smokeGain > 0.0) ? u_smokeGain : 0.5;
        float density = smoothstep(0.05, 0.05 + softVal * 0.5, field);
        float glow = smoothstep(-0.4, 0.1, field) * 0.18;
        float mask = clamp(density + glow, 0.0, 1.0) * fade * 0.62 * fragColor.a * soft;
        if (mask < 0.004) discard;

        float hf = smoothstep(0.06, 0.96, field) * (1.0 - age * 0.72) + (d - 0.5) * 0.36;
        float heat = clamp(hf, 0.0, 1.0) * smoothstep(-0.08, 0.45, field);
        float fl = 0.88 + 0.12 * sin(u_time * 14.0 + rnd * 60.0);

        vec3 rampCol = texture(u_rampLUT, vec2(clamp(heat, 0.0, 1.0), 0.5)).rgb;
        vec3 lutProbe = texture(u_rampLUT, vec2(0.75, 0.5)).rgb;
        if (dot(lutProbe, lutProbe) < 0.001) rampCol = calcBlackbodyNormalized(heat);

        finalColor = ShadeTranslucentFire(rampCol, heat, mask, fragColor.r, fl);
        return;
    }

    // ── MODE 7: PROCEDURAL SHADER FEATHERED DUST CLOUD (Zero-texture ALPHA BODY) ─
    // Smooth domain-warped FBM x Gaussian radial falloff (zero Worley/cauliflower
    // blobs, zero hard edges) with macro directional lighting + backlit Mie rim.
    if (u_volumeSheet > 6.5)
    {
        if (fragColor.a < 0.004) discard;
        vec2 p = (fragTexCoord - 0.5) * 2.0;
        float dist = length(p);
        if (dist >= 1.0) discard;

        // Deep Gaussian-like radial feathering guarantees 0.0 well before quad edge
        float radial = pow(max(0.0, 1.0 - dist), 2.15);
        if (radial < 0.003) discard;

        // Continuous world-coupled domain-warped FBM (1x fbm2 + 2x vnoise for fast fillrate)
        vec2 drift = vec2(u_time * 0.16, -u_time * 0.24) + fragPosition.xz * 0.26;
        vec2 baseUV = fragTexCoord * 2.45 + drift;
        float wx = (vnoise(baseUV * 1.15 + vec2(0.4, 1.2)) - 0.44) * 0.42;
        float wy = (vnoise(baseUV * 1.15 + vec2(3.1, 2.7)) - 0.44) * 0.42;
        float fbmMain = fbm2(baseUV + vec2(wx, wy));
        float fbmSoft = vnoise(baseUV * 1.45 - vec2(wx, -wy) * 0.65 + vec2(4.3, 2.1));

        float cloudMask = clamp(0.48 + (fbmMain * 0.65 + fbmSoft * 0.35 - 0.38) * 1.42, 0.0, 1.0);
        float soft = (u_softFade > 0.0) ? SoftParticle_Factor(u_softFade) : 1.0;
        float alpha = clamp(radial * smoothstep(0.06, 0.92, cloudMask) * 1.22, 0.0, 1.0) * fragColor.a * soft;
        if (alpha < 0.005) discard;

        // Analytic view-aligned macro dome normal (zero derivative seam artifacts)
        vec3 V = SafeNormalize(viewPos - fragPosition, vec3(0.0, 0.0, 1.0));
        vec3 upRef = abs(V.y) > 0.99 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
        vec3 camR = SafeNormalize(cross(upRef, V), vec3(1.0, 0.0, 0.0));
        vec3 camU = cross(V, camR);
        float domeZ = sqrt(max(1.0 - min(dist * dist, 1.0), 0.04));
        vec3 N = SafeNormalize(camR * (p.x * 0.78) + camU * (p.y * 0.78) + V * domeZ, V);

        vec3 L = ParticleLightDir();
        float ndotl = dot(N, L);
        float shade = clamp(ndotl * 0.5 + 0.5 + (fbmMain - 0.44) * 0.22, 0.0, 1.0);

        // 3-tone macro earth dust ramp (warm shadow core -> golden sand midtone -> sunlit rim)
        vec3 shadowCol = fragColor.rgb * vec3(0.46, 0.36, 0.24);
        vec3 midCol    = fragColor.rgb * 0.94;
        vec3 litCol    = min(fragColor.rgb * vec3(1.18, 1.08, 0.92), vec3(1.25));
        vec3 dustCol   = mix(shadowCol, midCol, smoothstep(0.08, 0.54, shade));
        dustCol        = mix(dustCol, litCol, smoothstep(0.45, 0.94, shade));

        // Gentle scene luminance coupling without washing sand into cool night blue-grey
        float wrap;
        vec3 envLit = ParticleLightTerm(N, V, L, wrap);
        float envY = clamp(dot(envLit, vec3(0.2126, 0.7152, 0.0722)), 0.58, 1.25);
        dustCol *= mix(1.0, envY, 0.18);

        // Subtle warm backlit Mie rim on wispy edges
        float fwdScatter = pow(max(dot(-V, L), 0.0), 3.4);
        float thinWisp = 1.0 - smoothstep(0.22, 0.84, cloudMask * radial);
        vec3 rimTint = mix(litCol, vec3(0.95, 0.78, 0.50), 0.65);
        dustCol += rimTint * fwdScatter * (0.08 + 0.14 * thinWisp);

        finalColor = VFX_ResolveBody(dustCol, 1.0, clamp(alpha, 0.0, 0.78));
        return;
    }

    // ── Precompute derivatives BEFORE any non-uniform discard in Modes 0..4 ──
    vec3 V = SafeNormalize(viewPos - fragPosition, vec3(0.0, 0.0, 1.0));
    vec3 quadT, quadB;
    ParticleTangentBasis(V, quadT, quadB);
    vec2 quadLocalUV = GetQuadLocalUV(fragTexCoord);

    vec2 sampleUV = fragTexCoord;
    vec2 sampleUV_B = fragTexCoord;
    float subframeT = 0.0;
    vec4 texelColor;
    vec4 texBColor = vec4(0.0);

    // When u_useMotionVectors > 0.5, CPU packs subframe blend factor into fragColor.b.
    // Reconstruct vertex colour so non-volume smoke/sprites do not pulse yellow->blue!
    vec4 vtxColor = fragColor;

    if (u_useMotionVectors > 0.5)
    {
        vec2 grid = max(u_atlasGrid, vec2(1.0));
        vec2 safeUV = clamp(fragTexCoord, vec2(0.0), vec2(0.99999));
        vec2 scaledUV = safeUV * grid;
        vec2 cellBaseA = floor(scaledUV);
        vec2 localUV = scaledUV - cellBaseA;
        subframeT = clamp(fragColor.b, 0.0, 1.0);
        vtxColor.b = fragColor.g;

        float frameIdx = cellBaseA.y * grid.x + cellBaseA.x;
        float nextIdx = min(frameIdx + 1.0, grid.x * grid.y - 1.0);
        float nextY = floor(nextIdx / grid.x);
        vec2 cellBaseB = vec2(nextIdx - nextY * grid.x, nextY);

        vec4 flowSamp = texture(u_motionTex, fragTexCoord);
        vec2 flow = (flowSamp.rg - 0.5) * 2.0;
        vec2 flowUV = vec2(flow.x, -flow.y);
        float warpScale = (u_motionWarp > 0.001) ? u_motionWarp * 0.08 : 0.06;

        vec2 invGrid = 1.0 / grid;
        sampleUV   = (cellBaseA + clamp(localUV - flowUV * (subframeT * warpScale), 0.005, 0.995)) * invGrid;
        sampleUV_B = (cellBaseB + clamp(localUV + flowUV * ((1.0 - subframeT) * warpScale), 0.005, 0.995)) * invGrid;

        vec4 colA = texture(texture0, sampleUV);
        vec4 colB = texture(texture0, sampleUV_B);
        texelColor = mix(colA, colB, subframeT);

        if (u_sixWayLighting > 1.5)
        {
            vec4 bColA = texture(u_sixWayTexB, sampleUV);
            vec4 bColB = texture(u_sixWayTexB, sampleUV_B);
            texBColor = mix(bColA, bColB, subframeT);
        }
    }
    else
    {
        if (u_volumeSheet > 1.5 && u_volumeSheet < 2.5)
        {
            vec2 grid = max(u_atlasGrid, vec2(1.0));
            vec2 safeUV = clamp(fragTexCoord, vec2(0.0), vec2(0.99999));
            vec2 scaledUV = safeUV * grid;
            vec2 cellBase = floor(scaledUV);
            vec2 localUV = (grid.x > 1.5 || grid.y > 1.5) ? (scaledUV - cellBase) : fragTexCoord;

            vec2 pNoise = localUV * 2.8 + vec2(fragPosition.x * 0.9, fragPosition.y * 1.6 - u_time * 3.0);
            float dX = vnoise(pNoise) - 0.5;
            float dY = vnoise(pNoise + vec2(17.3, 31.7)) - 0.5;

            vec2 uvWarp = vec2(dX, dY) * (0.12 * (0.10 + localUV.y * 0.90));
            vec2 warpedLocalUV = clamp(localUV + uvWarp, 0.01, 0.99);
            sampleUV = (cellBase + warpedLocalUV) / grid;
        }

        texelColor = texture(texture0, sampleUV);
        if (u_sixWayLighting > 1.5)
        {
            texBColor = texture(u_sixWayTexB, sampleUV);
        }
    }

    // Compute analytic/derivative local normal BEFORE any discard so dFdx(texA)
    // is always evaluated in uniform control flow across the 2x2 pixel quad.
    float normalSourceAlpha = (u_volumeSheet > 0.5) ? texelColor.a : (texelColor.a * vtxColor.a);
    vec3 localHemisphereN = ParticleNormalLocal(quadLocalUV, normalSourceAlpha);

    float soft = (u_softFade > 0.0) ? SoftParticle_Factor(u_softFade) : 1.0;

    // ── EXTRACTED FIRE EOO + NORMAL ATLAS (MODE 4) ───────────────────────
    if (u_volumeSheet > 3.5 && u_volumeSheet < 4.5)
    {
        float coverage = texelColor.a * vtxColor.a * soft;
        if (coverage < 0.005) discard;

        vec4 normalSample = (u_useMotionVectors > 0.5)
            ? mix(texture(u_normalTex, sampleUV), texture(u_normalTex, sampleUV_B), subframeT)
            : texture(u_normalTex, sampleUV);
        vec3 fireN = ParticleNormalWorldFromBasis(ParticleNormalXY(normalSample), V, quadT, quadB);
        float wrap;
        vec3 envLit = ParticleLightTerm(fireN, V, ParticleLightDir(), wrap);

        float density = clamp(1.0 - texelColor.g, 0.0, 1.0);
        float heat = clamp(texelColor.r * u_heatGain * fragColor.r, 0.0, 1.0);
        float flameMask = smoothstep(0.015, 0.55, texelColor.r);
        vec3 rampCol = texture(u_rampLUT, vec2(heat, 0.5)).rgb;
        vec3 flame = rampCol * (flameMask * coverage * u_emissiveBoost);

        float bodyAlpha = clamp(coverage * density, 0.0, 1.0);
        vec3 smoke = u_smokeTint * envLit * bodyAlpha;
        vec3 mode4Out = flame + smoke;
        if (any(isnan(mode4Out)) || any(isinf(mode4Out))) mode4Out = vec3(0.0);
        finalColor = vec4(clamp(mode4Out, vec3(0.0), vec3(32.0)), bodyAlpha);
        return;
    }

    // ── EXTRACTED EOO SMOKE + NORMAL ATLAS (MODE 3) ──────────────────────
    if (u_volumeSheet > 2.5)
    {
        float alpha = texelColor.a * vtxColor.a * soft;
        if (alpha < 0.01) discard;

        vec4 normalSample = (u_useMotionVectors > 0.5)
            ? mix(texture(u_normalTex, sampleUV), texture(u_normalTex, sampleUV_B), subframeT)
            : texture(u_normalTex, sampleUV);
        vec3 smokeN = ParticleNormalWorldFromBasis(ParticleNormalXY(normalSample), V, quadT, quadB);

        float wrap;
        vec3 envLit = ParticleLightTerm(smokeN, V, ParticleLightDir(), wrap);
        float lightY = dot(envLit, vec3(0.2126, 0.7152, 0.0722));
        float ao = mix(0.35, 1.0, texelColor.g);
        float bakedLight = 0.75 + 2.0 * texelColor.r;
        vec3 carrier = vtxColor.rgb * (ao * bakedLight);
        float strength = (u_lightingStrength > 0.0) ? u_lightingStrength : 1.0;
        vec3 shaded = carrier * mix(1.0, lightY, strength);
        finalColor = VFX_ResolveBody(shaded, 1.0, alpha);
        return;
    }

    // ── PACKED VOLUME SHEET (MODES 1 & 2) ────────────────────────────────────
    if (u_volumeSheet > 0.5)
    {
        if (u_softDebug > 1.5)
        {
            finalColor = vec4(texelColor.r, texelColor.a, 0.0, 1.0);
            return;
        }
        if (u_softDebug > 0.5) { finalColor = vec4(vec3(soft), 1.0); return; }

        float emis;
        float rawSoot;
        float soot;
        float shad;
        float opac;

        if (u_sixWayLighting > 1.5)
        {
            emis = texBColor.a;
            opac = texelColor.a;
            rawSoot = clamp(opac * clamp(1.0 - emis * 0.45, 0.0, 1.0), 0.0, 1.0);
            soot = clamp(rawSoot * u_smokeGain, 0.0, 1.0);
            shad = soot;
        }
        else
        {
            emis = texelColor.r;
            rawSoot = texelColor.g;
            soot = clamp(rawSoot * u_smokeGain, 0.0, 1.0);
            shad = texelColor.b;
            opac = texelColor.a;
        }

        // PATH A: FLAME VOLUME (u_volumeSheet > 1.5 — Flipbook Tongue & Flipbook Puff)
        if (u_volumeSheet > 1.5)
        {
            float flameMask = smoothstep(0.02, 0.14, emis);
            float fade = vtxColor.a * soft * flameMask;
            if (fade < 0.005) discard;

            float heat = clamp(emis * u_heatGain * fragColor.r, 0.0, 1.0);
            vec3 rampCol = texture(u_rampLUT, vec2(heat, 0.5)).rgb;
            vec3 lutProbe = texture(u_rampLUT, vec2(0.75, 0.5)).rgb;
            if (dot(lutProbe, lutProbe) < 0.001) rampCol = calcBlackbodyNormalized(heat);

            float mask = clamp(smoothstep(0.04, 0.72, emis) * fade, 0.0, 1.0);
            finalColor = ShadeTranslucentFire(rampCol, heat, mask, fragColor.r, 1.0);
            return;
        }

        // PATH B: STANDARD VOLUME SHEET (u_volumeSheet <= 1.5, e.g. EnergyBurst)
        float fade = vtxColor.a * soft;
        if (fade < 0.004 || (emis < 0.004 && soot * opac < 0.004)) discard;

        float heat  = clamp(emis * u_heatGain * fragColor.r, 0.0, 1.0);
        vec3  rampCol = texture(u_rampLUT, vec2(heat, 0.5)).rgb;
        vec3  bbCol = calcBlackbodyNormalized(heat);
        vec3  flame = mix(rampCol, bbCol, 0.45) * (emis * opac * u_emissiveBoost);
        flame = CompressFlameRadiance(flame, heat);

        if (u_smokeGain <= 0.0)
        {
            finalColor = vec4(flame * fade, 1.0);
            return;
        }

        float selfShadow = (soot > 0.004) ? clamp(shad / soot, 0.0, 1.0) : 1.0;
        float wrap;
        vec3 L = ParticleLightDir();
        vec3 lit;
        if (u_sixWayLighting > 0.5)
        {
            lit = ParticleLightTerm6Way(quadLocalUV, V, quadT, quadB,
                                        texelColor.rgb, texBColor.rgb, texBColor.a,
                                        soot, selfShadow, opac, L, wrap);
        }
        else
        {
            vec3 N = ParticleNormalWorldFromBasis(localHemisphereN, V, quadT, quadB);
            lit = ParticleLightTerm(N, V, L, wrap);
        }
        vec3 smoke = u_smokeTint * lit * (u_sixWayLighting > 0.5 ? 1.0 : selfShadow);

        float matNow  = soot + emis;
        float matOrig = max(rawSoot + emis, 1e-4);
        float alpha = clamp(opac * clamp(matNow / matOrig, 0.0, 1.0) * fade, 0.0, 1.0);
        float sootFrac = clamp(soot / max(matNow, 1e-4), 0.0, 1.0);
        float blendAlpha = alpha * mix(0.18, 1.0, sootFrac);

        finalColor = vec4(flame * fade + smoke * (alpha * sootFrac), blendAlpha);
        return;
    }

    // ── STANDARD SPRITE / SIX-WAY SMOKE PATH ─────────────────────────────────
    vec4 base = texelColor * vtxColor;
    if (base.a < 0.01) discard;

    if (u_softDebug > 0.5)
    {
        finalColor = vec4(vec3(soft), 1.0);
        return;
    }

    float finalAlpha = base.a * soft;
    if (finalAlpha < 0.004 && u_debugNormal < 0.5) discard;

    float effLightingStrength = (u_sixWayLighting > 0.5)
        ? (u_lightingStrength > 0.0 ? u_lightingStrength : 1.0)
        : u_lightingStrength;

    if (effLightingStrength <= 0.0)
    {
        vec3 unlitRgb = (u_sixWayLighting > 1.5) ? vtxColor.rgb : base.rgb;
        float rQuad = length(quadLocalUV * 2.0 - 1.0);
        // Spatial core pin (<=20% center area): keeps the sprite's corona at
        // 100% elemental saturation so bloom receives vivid color, while only
        // the tight center warms toward an incandescent peak.
        float corePin = smoothstep(0.62, 0.96, texelColor.a) * (1.0 - smoothstep(0.0, 0.52, rQuad));
        vec3 coreTint = mix(unlitRgb, vec3(1.0, 0.97, 0.90), 0.72);
        vec3 radiantRgb = mix(unlitRgb, coreTint * (1.25 + corePin * 0.95), corePin * 0.65);

        if (u_surfaceMode == 2)
        {
            // Premultiplied Carrier + Chromatic Corona + Incandescent Core:
            // Deep chromatic carrier holds silhouette on snow; HDR glow drives bloom.
            vec3 carrierRgb = unlitRgb * 0.58;
            float carrierCov = clamp(finalAlpha * 0.52, 0.0, 0.78);
            vec3 glowRgb = radiantRgb * (u_emissiveBoost * finalAlpha);
            finalColor = VFX_ResolvePremultiplied(carrierRgb, 1.0, carrierCov,
                                                  glowRgb, 1.0, 1.0);
            return;
        }
        if (u_surfaceMode == 0)
        {
            finalColor = VFX_ResolveBody(radiantRgb, u_emissiveBoost, finalAlpha);
            return;
        }
        finalColor = VFX_ResolveEmission(radiantRgb, u_emissiveBoost, 1.0, finalAlpha);
        return;
    }

    if (u_debugNormal > 0.5 && u_debugNormal < 1.5)
    {
        finalColor = vec4(localHemisphereN * 0.5 + 0.5, base.a);
        return;
    }

    vec3 L = ParticleLightDir();

    if (u_debugNormal > 2.5)
    {
        finalColor = vec4(L * 0.5 + 0.5, 1.0);
        return;
    }

    float wrap;
    vec3 lit;
    if (u_sixWayLighting > 0.5)
    {
        lit = ParticleLightTerm6Way(quadLocalUV, V, quadT, quadB,
                                    texelColor.rgb, texBColor.rgb, texBColor.a,
                                    base.a, 1.0 - base.a * 0.5, base.a, L, wrap);
    }
    else
    {
        vec3 N = ParticleNormalWorldFromBasis(localHemisphereN, V, quadT, quadB);
        lit = ParticleLightTerm(N, V, L, wrap);
    }

    if (u_debugNormal > 1.5)
    {
        finalColor = vec4(vec3(wrap), 1.0);
        return;
    }

    vec3 albedo = (u_sixWayLighting > 1.5) ? vtxColor.rgb : base.rgb;
    vec3 shaded = albedo * lit;
    finalColor = VFX_ResolveBody(mix(albedo, shaded, effLightingStrength),
                                 u_emissiveBoost, finalAlpha);
}