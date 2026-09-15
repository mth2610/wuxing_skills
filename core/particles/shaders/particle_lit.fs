#version 330
// The rlvk sampler resolver maps the draw-call texture by reflected name, not a
// presumed descriptor binding, so this shader may safely use texture0 plus the
// soft-particle scene-depth sampler (HANDOFF §7.30).

// Đợt E / F1 — lit CPU particles. See core/docs/ELDEN_VFX_SPEC.md §0.1b:
// flat-shaded smoke can only ever look like a decal OF smoke. Volume reads
// almost entirely from lighting — a bright rim toward the light, a dark
// occluded core, and a glow when backlit.
//
// NORMAL SOURCE — an analytic hemisphere built from the quad-local UV. The
// first version derived it from dFdx/dFdy of the sprite alpha, which failed in
// the worst possible way: that gradient is ~0 across the flat CORE of a soft
// particle, so the tilt direction collapsed to zero and the normal snapped to
// (0,0,1) exactly where the sprite is brightest and largest. The result read as
// a uniformly slightly-brighter blob, plus quantisation spokes from a 64px
// texture magnified many times. The derivative path survives behind
// u_analyticUV = 0 for SpriteAnim atlases, where fragTexCoord is a sub-rect
// rather than quad-local UV.
//
// Everything is opt-in: at u_lightingStrength = 0 this returns exactly the old
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

/* BACKGROUND ADAPTATION — how bright is what this particle is in front of.
 *
 * Filled by SceneTargets_CaptureBackgroundLuma, taken after the world is drawn
 * and BEFORE any VFX, so a particle is never told about its own light. Sampling
 * a finished frame would close that loop and the effect would oscillate.
 *
 * Why emission has to fall as the background rises, measured on REF PARTICLES:
 * against a dark backdrop more emission reads better, monotonically; against a
 * white one it reads WORSE, because the added light fills back in the silhouette
 * the particle cut out of the background. The two slopes point opposite ways, so
 * no fixed emissive value serves both (BRIGHT_BACKGROUND_VFX_SPEC.md §7.6c).
 *
 * 0 = off, and that is the default: nothing changes until a caller asks. */
uniform sampler2D u_bgLuma;
uniform float u_bgAdapt;

uniform float u_softFade;
uniform float u_softDebug;

// NOTE: deliberately does NOT use `colDiffuse`. rlgl only guarantees to push
// that uniform for its own shape/texture draw paths — through a raw
// rlBegin(RL_QUADS) run with a custom shader bound it may never be written, and
// an unwritten GLSL uniform is ZERO, which would multiply the whole batch to
// black. The per-vertex colour already carries the particle tint.

// Environment (set per frame by DrawParticles from environment_system)
uniform vec3 u_sunToLight;   // normalized, surface → sun
uniform vec3 u_sunColor;     // linear-ish 0..1
uniform vec3 u_ambient;      // flat ambient fill
uniform vec3 viewPos;

uniform float u_lightingStrength; // 0 = legacy unlit path (DEFAULT)
uniform float u_scatterStrength;  // forward-scatter / backlit glow
// Debug modes. 0 = off, 1 = screen-space normal, 2 = pure lighting term on an
// OPAQUE quad, 3 = the light vector L. Mode 2 is the one that settles "is this
// shading or just alpha stacking": it throws away both the texture colour AND
// the alpha falloff, so what is left can only be the lighting.
uniform float u_debugNormal;
uniform float u_normalBulge;      // 1 = true hemisphere; >1 exaggerates the dome
// Đợt E — EMISSIVE HDR BOOST. The reason particles had no blown-out core.
//
// Everything upstream is capped at 1.0: the vertex colour is rlColor4ub (8-bit),
// emissiveCurve is applied CPU-side and clamped at 255, and the texture is [0,1].
// So a single emissive sprite could never write more than 1.0 — while the scene
// buffer is R16F and happily holds 10.0. ACES then maps 1.0 to ~0.8, which is
// exactly the bloom threshold, so nothing ever blew out and nothing bloomed.
// The HDR pipeline existed and particles never used its headroom.
//
// Pushing the emissive population above 1.0 is what gives the white-hot core
// with a coloured rim: the tonemapper rolls the excess off to white and bloom
// picks up what is over threshold. Set per BATCH (>1 only for unlit/emissive
// particles); smoke must stay at 1.0 or it would emit light it should occlude.
uniform float u_emissiveBoost;
uniform float u_analyticUV;       // 1 = quad-local UV (default), 0 = derivative fallback
// Đợt E / E4 — atlas grid (cols, rows); (1,1) = not an atlas.
//
// With a SpriteAnim atlas, fragTexCoord is the ATLAS sub-rect (e.g. 0.25..0.375),
// not the quad's 0..1. The analytic hemisphere below reads it as if it were
// quad-local, so it shades from a small off-centre patch of the sphere — and
// that patch JUMPS to a different region every time the animation steps to the
// next cell, which reads as the sprite popping frame to frame. Handing the grid
// over lets the local UV be recovered exactly, so the analytic path (which
// exists because the derivative fallback has a dead core) keeps working.
uniform vec2 u_atlasGrid;

// ── PACKED VOLUME SHEET ─────────────────────────────────────────────────────
//
// 1 = texture0 is a 4-channel volume flipbook from scripts/flipbook/ rather
// than a colour sprite:
//
//   R = flame emission   → indexes u_rampLUT; THIS is where white-core/orange-
//                          rim zoning comes from. Legacy mode cannot express it
//                          at all: one vertex colour tints the whole quad.
//   G = smoke density    → the occluding half, lit by the code below.
//   B = self-shadowed smoke; B/G is the fraction of light that survived to
//       each texel, which is what stops stacked puffs reading as flat cards.
//   A = true opacity (1 - transmittance), NOT a luminance guess.
//
// Output is PREMULTIPLIED (see VFX_BLEND_PREMULTIPLIED): emission adds light
// without occluding, smoke both occludes and is lit, from ONE draw. That is why
// this exists — the alternative is an additive core plus an alpha body, two
// populations that interleave in the depth sort and cost a batch flush at every
// alternation.
//
// The sheet carries NO hue. Colour is entirely u_rampLUT, so the same greyscale
// fire becomes purple or blue magic fire by swapping the ramp.
uniform float u_volumeSheet;
uniform sampler2D u_rampLUT;
uniform float u_heatGain;         // exposure on emission before the ramp lookup
uniform vec3  u_smokeTint;        // body colour of the soot half
// Gain on the sheet's soot channel, the mirror of u_heatGain on emission.
//
// THE SHEET IS DIRECTIONLESS AND SO IS ITS SMOKINESS. R:G was the last thing
// still baked into the asset, which would have meant a second sim for "fire
// with little smoke" — and a second sheet is the wrong unit, because the same
// greyscale puff has to serve a petrol fire (black, heavy) and burning leaves
// (white, light) and a clean flame. Scaling G here makes that a composition
// decision like every other: heat gain, ramp, tint, force field, spawn spread.
// 1.0 is exactly today's look.
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
// 0 = standard hemisphere lighting (default)
// 1 = synthetic 6-way directional scattering (evaluated from volume sheet / quad)
// 2 = dual-texture 6-way lightmap pair (texture0 = Map A, u_sixWayTexB = Map B)
uniform float u_sixWayLighting;
uniform sampler2D u_sixWayTexB;
uniform float u_sixWayScattering;  // forward-scatter / backlit multiplier
uniform float u_sixWayAbsorption;  // multi-axis extinction factor
uniform vec3  u_ambientGround;     // multi-directional ambient ground bounce (-Y)
uniform vec3  u_ambientHorizon;    // multi-directional ambient horizon fill (sides)

// ── Shared shading pieces ────────────────────────────────────────────────────
// Extracted so the legacy path and the packed-volume path cannot drift apart.
// A second copy of this maths is exactly how a mirror rots into fiction
// (core/CLAUDE.md, debugging workflow §3) — there must be ONE hemisphere normal
// and ONE light term in this file.

// Screen-space hemisphere normal. `texA` is the sprite alpha, read only by the
// derivative fallback.
vec3 ParticleNormalLocal(float texA)
{
    // Recover the quad-local UV. fract() of the cell-scaled coord is exactly the
    // position within the cell; guarded so a non-atlas particle (grid 1,1) is
    // untouched — fract(1.0) is 0.0 and would fold the quad's far edge.
    vec2 luv = (u_atlasGrid.x > 1.5 || u_atlasGrid.y > 1.5)
                 ? fract(fragTexCoord * u_atlasGrid)
                 : fragTexCoord;
    vec2  q = luv * 2.0 - 1.0;               // [-1,1] across the quad
    float rr = length(q);
    vec3  n;
    if (u_analyticUV > 0.5)
    {
        // Exact hemisphere: xy is the in-plane offset, z closes it to unit
        // length. Zero texture dependence, so no spokes and no dead core.
        float rc = min(rr, 1.0);
        vec2  xyDome = (rr > 1e-5) ? (q / rr) * rc * u_normalBulge : vec2(0.0);
        // Alpha erosion gradient perturbation (Ghost of Tsushima n ~ ∇ρ):
        // Micro-surface billow perturbation along the density falloff edge.
        vec2  gErosion = vec2(dFdx(texA), dFdy(texA));
        float gLen = length(gErosion);
        vec2  gDir = gLen > 1e-5 ? (gErosion / gLen) : vec2(0.0);
        vec2  xy = mix(xyDome, -gDir * rc * u_normalBulge, 0.35 * clamp(gLen * 8.0, 0.0, 1.0));
        n = normalize(vec3(xy, sqrt(max(1.0 - rc * rc, 0.0))));
    }
    else
    {
        // Derivative fallback for atlas UVs. Reads alpha as a paraboloid height
        // a = 1 - r^2, so r = sqrt(1-a) and the normal is (r*dir, sqrt(a)).
        float a = texA;
        vec2  g = vec2(dFdx(a), dFdy(a));
        float glen = length(g);
        vec2  dir = glen > 1e-6 ? g / glen : vec2(0.0);
        float r = sqrt(clamp(1.0 - a, 0.0, 1.0));
        n = normalize(vec3(-dir * r * u_normalBulge, sqrt(max(a, 0.0))));
    }
    return n;
}

// Compute the tangent basis (T, B) of the quad in world space from screen derivatives.
// Tangent T points along increasing u (+X Right in texture space).
// Bitangent B points along increasing v (+Y Bottom in texture space).
void ParticleTangentBasis(vec3 V, out vec3 quadT, out vec3 quadB)
{
    vec3 upRef = abs(V.y) > 0.99 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 R = normalize(cross(upRef, V));
    vec3 U = cross(V, R);

    vec3 dP1 = dFdx(fragPosition);
    vec3 dP2 = dFdy(fragPosition);
    vec2 dU1 = dFdx(fragTexCoord);
    vec2 dU2 = dFdy(fragTexCoord);

    float det = dU1.x * dU2.y - dU1.y * dU2.x;
    if (abs(det) > 1e-9)
    {
        quadT = normalize((dP1 * dU2.y - dP2 * dU1.y) / det);
        quadB = normalize((-dP1 * dU2.x + dP2 * dU1.x) / det);
    }
    else
    {
        quadT = R;
        quadB = -U; // v runs downward
    }
}

// Rebuild a world-space normal from the quad-local normal.
// Tangent basis aligns n.x with +u (quadT) and n.y with -v (-quadB),
// ensuring the normal dome is invariant to particle rotation on CPU.
vec3 ParticleNormalWorld(vec3 n)
{
    vec3 V = normalize(viewPos - fragPosition);
    vec3 quadT, quadB;
    ParticleTangentBasis(V, quadT, quadB);
    return normalize(n.x * quadT - n.y * quadB + n.z * V);
}

// Debug: force a horizontal light at a chosen azimuth. Sweeping it MUST sweep
// the bright side across the sprite. If the bright spot instead stays pinned to
// the centre, the shading is radially symmetric — which is what happens when the
// real sun points nearly along the view vector, since then dot(N, L) collapses
// to n.z and n.z is radially symmetric by construction. That failure looks
// identical to "lighting is broken" but is not.
vec3 ParticleLightDir()
{
    if (u_lightAzimuth >= 0.0)
    {
        float rad = radians(u_lightAzimuth);
        return normalize(vec3(cos(rad), 0.25, sin(rad)));
    }
    return u_sunToLight;
}

// Scene light arriving at a texel with normal N. `wrapOut` hands back the raw
// half-Lambert term for debug mode 2.
//
// GAINS, and they are not optional. Raw `ambient + sunColor * wrap` is a
// MULTIPLIER on the body colour, and in a night arena both terms sit around
// 0.15-0.45 — so physically-correct maths dims the smoke to a third of its
// authored brightness and stains it moonlight-blue, instead of shaping it. Light
// is here to sculpt the puff, not to darken it: gain the sun term up so the lit
// side lands near 1.0, and pull ambient DOWN to open up contrast.
vec3 ParticleLightTerm(vec3 N, vec3 L, out float wrapOut)
{
    // Half-Lambert wrap, same convention as surface_lit.fs so particles and
    // characters agree about where the light is. No hard terminator — smoke has
    // none.
    float ndl  = dot(N, L);
    float wrap = pow(ndl * 0.5 + 0.5, 1.5);
    wrapOut = wrap;

    vec3 lit = u_ambient * u_ambientGain + u_sunColor * u_sunGain * wrap;

    // ── Forward scatter — the backlit glow (Henyey-Greenstein Two-Lobe Phase) ───
    // The single most convincing volumetric cue: light coming from BEHIND the
    // puff bleeds through it. Two-lobe Henyey-Greenstein produces the characteristic
    // silver lining edge without flat exponent blow-out.
    if (u_scatterStrength > 0.0)
    {
        vec3  V = normalize(viewPos - fragPosition);
        float cosTheta = dot(-V, L);
        float hgForward = (1.0 - 0.3025) / max(pow(1.3025 - 1.10 * cosTheta, 1.5), 1e-3); // g1 = 0.55
        float hgBackward = (1.0 - 0.0625) / max(pow(1.0625 + 0.50 * cosTheta, 1.5), 1e-3); // g2 = -0.25
        float hgPhase = mix(hgBackward, hgForward, 0.72);
        lit += u_sunColor * hgPhase * (u_scatterStrength * 0.35);
    }

    // ── VFX point lights — a fireball lighting its own smoke ─────────────────
    for (int i = 0; i < MAX_VFX_LIGHTS; i++)
    {
        if (i >= u_vfxLightCount) break;
        vec3  toL  = u_vfxLightPos[i] - fragPosition;
        float dist = length(toL);
        float att  = clamp(1.0 - dist / max(u_vfxLightRadius[i], 0.001), 0.0, 1.0);
        att *= att;
        if (att <= 0.0) continue;
        float w = pow(dot(N, toL / max(dist, 0.001)) * 0.5 + 0.5, 1.5);
        lit += u_vfxLightColor[i] * w * att;
    }
    return lit;
}

// ── 6-WAY VOLUMETRIC LIGHTING ────────────────────────────────────────────────
// Unity VFX Graph / Ghost of Tsushima 6-way directional transmission & scattering model.
// Evaluates light along 6 cardinal directions in particle billboard texture space:
//   Map A: (+X Right, +Y Top,    +Z Back / Transmitted through volume)
//   Map B: (-X Left,  -Y Bottom, -Z Front / Camera-facing reflection)
// Invariant to particle 2D rotation via screen derivative tangent frame reconstruction.
vec3 ParticleLightTerm6Way(vec2 luv, vec3 sampledMapA, vec3 sampledMapB, float sampledMapBAux,
                           float soot, float selfShadow, float opac, vec3 L,
                           out float wrapOut)
{
    vec3 V = normalize(viewPos - fragPosition);
    vec3 quadT, quadB;
    ParticleTangentBasis(V, quadT, quadB);

    vec3 mapA;
    vec3 mapB;
    float ao = 1.0;

    float scFactor = (u_sixWayScattering > 0.0 ? u_sixWayScattering : 1.0);
    float absFactor = (u_sixWayAbsorption > 0.0 ? u_sixWayAbsorption : 1.0);

    // Two-lobe Henyey-Greenstein forward scattering (silver lining when looking towards the sun)
    float cosTheta = dot(L, -V);
    float hgForward = (1.0 - 0.3364) / max(pow(1.3364 - 1.16 * cosTheta, 1.5), 1e-3); // g1 = 0.58
    float hgBackward = (1.0 - 0.04) / max(pow(1.04 + 0.40 * cosTheta, 1.5), 1e-3);     // g2 = -0.20
    float hgPhase = mix(hgBackward, hgForward, 0.75);
    float fwdScatter = clamp(1.0 + scFactor * 2.2 * hgPhase, 1.0, 8.0);

    if (u_sixWayLighting > 1.5)
    {
        // Dual-texture 6-way lightmap pair (Unity standard):
        // texture0 = Map A (+X Right, +Y Top, +Z Backlight, Alpha: Opacity)
        // u_sixWayTexB = Map B (-X Left, -Y Bottom, -Z Front, Alpha: Opacity)
        // Use the samples selected in main(). They may be optical-flow warped
        // and cross-faded between two frames; sampling fragTexCoord again here
        // would make lighting lag behind the advected density silhouette.
        mapA = sampledMapA;
        mapB = sampledMapB;
        // LIGHT6 smoke stores authored AO in Map B.a. A future six-way VOLUME
        // fire uses that same slot for emission, so retain the RGB-derived
        // fallback there; emission itself must never be darkened as smoke AO.
        ao = (u_volumeSheet < 0.5)
           ? clamp(sampledMapBAux, 0.05, 1.0)
           : clamp(dot(mapA + mapB, vec3(1.0 / 6.0)), 0.05, 1.0);

        // Extinction / Contrast shaping: mapA and mapB remapped to expand dynamic range
        mapA = clamp((mapA - 0.04) / 0.96, 0.0, 1.0);
        mapB = clamp((mapB - 0.04) / 0.96, 0.0, 1.0);
        float contrast = max(absFactor, 1.0);
        mapA = pow(mapA, vec3(contrast));
        mapB = pow(mapB, vec3(contrast));

        // +Z is forward-scattered backlight modulated by Henyey-Greenstein
        mapA.b *= fwdScatter;
    }
    else
    {
        // Synthetic 6-way directional scattering from single sheet or procedural puff
        vec2 q = luv * 2.0 - 1.0;
        float rQuad = length(q);
        float dens = clamp(max(soot, opac), 0.0, 1.0);
        float bulge = max(u_normalBulge, 0.2);
        float pX_pos = clamp(0.5 - 0.5 * q.x * bulge, 0.0, 1.0);
        float pX_neg = clamp(0.5 + 0.5 * q.x * bulge, 0.0, 1.0);
        float pY_pos = clamp(0.5 - 0.5 * q.y * bulge, 0.0, 1.0);
        float pY_neg = clamp(0.5 + 0.5 * q.y * bulge, 0.0, 1.0);

        float ext = clamp(absFactor * 1.6, 0.2, 5.0);
        float tX_pos = pow(clamp(1.0 - pX_pos * dens * ext, 0.0, 1.0), 1.6);
        float tX_neg = pow(clamp(1.0 - pX_neg * dens * ext, 0.0, 1.0), 1.6);
        float tY_pos = pow(clamp(1.0 - pY_pos * dens * ext, 0.0, 1.0), 1.6);
        float tY_neg = pow(clamp(1.0 - pY_neg * dens * ext, 0.0, 1.0), 1.6);

        float tZ_back = pow(clamp(selfShadow, 0.0, 1.0), 0.6) * fwdScatter;
        float tZ_front = clamp(1.0 - dens * 0.45, 0.15, 1.0) * clamp(1.0 - rQuad * 0.35, 0.1, 1.0);

        mapA = vec3(tX_pos, tY_pos, tZ_back);
        mapB = vec3(tX_neg, tY_neg, tZ_front);
    }

    // Direct directional light (Sun) projected into the quad's local texture frame:
    // quadT = +X (Right), -quadB = +Y (Top), -V = +Z (Backlight)
    vec3 L_local = vec3(dot(L, quadT), -dot(L, quadB), dot(L, -V));
    vec3 L_pos = max(vec3(0.0), L_local);
    vec3 L_neg = max(vec3(0.0), -L_local);
    // Squared directional weights conserve energy for a normalized light
    // vector: sum(L_local^2) == 1. Linear positive/negative weights made a
    // diagonal light up to sqrt(3) brighter than an axis-aligned one.
    L_pos *= L_pos;
    L_neg *= L_neg;
    float dirLit = dot(L_pos, mapA) + dot(L_neg, mapB);
    wrapOut = dirLit;

    float effectiveSunGain = max(u_sunGain * 2.2, 2.2);
    vec3 lit = u_sunColor * (effectiveSunGain * dirLit);

    // Multi-directional ambient environment lighting with controlled contrast
    vec3 ambGround = (length(u_ambientGround) > 1e-4) ? u_ambientGround : (u_ambient * 0.35);
    vec3 ambHorizon = (length(u_ambientHorizon) > 1e-4) ? u_ambientHorizon : (u_ambient * 0.65);

    vec3 ambLit = (u_ambient * (u_ambientGain * 0.65) * mapA.g
                + ambGround * (u_ambientGain * 0.45) * mapB.g
                + ambHorizon * (u_ambientGain * 0.35) * ((mapA.r + mapB.r + mapA.b + mapB.b) * 0.25)) * ao;
    lit += ambLit;

    // VFX point lights evaluated in 6-way texture space
    for (int i = 0; i < MAX_VFX_LIGHTS; i++)
    {
        if (i >= u_vfxLightCount) break;
        vec3 toL = u_vfxLightPos[i] - fragPosition;
        float dist = length(toL);
        float att = clamp(1.0 - dist / max(u_vfxLightRadius[i], 0.001), 0.0, 1.0);
        att *= att;
        if (att <= 0.0) continue;

        vec3 Lpt = toL / max(dist, 0.001);
        vec3 Lpt_local = vec3(dot(Lpt, quadT), -dot(Lpt, quadB), dot(Lpt, -V));
        vec3 Lpt_pos = max(vec3(0.0), Lpt_local);
        vec3 Lpt_neg = max(vec3(0.0), -Lpt_local);
        Lpt_pos *= Lpt_pos;
        Lpt_neg *= Lpt_neg;
        float ptTransmission = dot(Lpt_pos, mapA) + dot(Lpt_neg, mapB);
        lit += u_vfxLightColor[i] * ptTransmission * att;
    }

    // Baked smoke already contains rich internal shading. Preserve scene-light
    // direction and luminance, but reject chroma so warm sunlight, coloured VFX
    // lights, and ambient probes cannot turn white smoke yellow/purple or make
    // its hue flicker as those contributions change.
    // Volume fire keeps the full coloured-light response; its emission is handled
    // separately by the blackbody path below.
    if (u_sixWayLighting > 1.5 && u_volumeSheet < 0.5)
    {
        float smokeLightY = dot(lit, vec3(0.2126, 0.7152, 0.0722));
        lit = vec3(smokeLightY);
    }
    return lit;
}

// Ghost of Tsushima: Chromaticity-preserving Planck radiance compression.
// Keeps flame rich amber/orange in high HDR while allowing only the ultra-hot
// core to reach incandescent white, avoiding flat white clipping.
vec3 CompressFlameRadiance(vec3 col, float heat)
{
    float maxC = max(col.r, max(col.g, col.b));
    if (maxC > 1.0)
    {
        float compressed = 1.0 + (maxC - 1.0) / (1.0 + (maxC - 1.0) * 0.40);
        vec3 chrom = col / maxC;
        float coreIncandescence = smoothstep(0.96, 1.0, heat);
        vec3 coreColor = mix(chrom, vec3(1.0), coreIncandescence * 0.40);
        return coreColor * compressed;
    }
    return col;
}

void main()
{
    vec2 sampleUV = fragTexCoord;
    vec4 texelColor;
    vec4 texBColor = vec4(0.0);

    if (u_useMotionVectors > 0.5)
    {
        vec2 grid = max(u_atlasGrid, vec2(1.0));
        vec2 cellBaseA = floor(fragTexCoord * grid);
        vec2 localUV = fract(fragTexCoord * grid);
        float subframeT = clamp(fragColor.b, 0.0, 1.0); // Subframe blend factor [0.0, 1.0]

        // Next frame cell calculation
        float frameIdx = cellBaseA.y * grid.x + cellBaseA.x;
        float nextIdx = min(frameIdx + 1.0, grid.x * grid.y - 1.0);
        vec2 cellBaseB = vec2(mod(nextIdx, grid.x), floor(nextIdx / grid.x));

        // Sample optical flow motion vector from u_motionTex
        vec4 flowSamp = texture(u_motionTex, fragTexCoord);
        // Flow encoding: R = Vx (+Right/-Left), G = Vy (+Up/-Down)
        vec2 flow = (flowSamp.rg - 0.5) * 2.0;
        // Invert Y because +G is Up (which is -V in texture coordinates)
        vec2 flowUV = vec2(flow.x, -flow.y);
        float warpScale = (u_motionWarp > 0.001) ? u_motionWarp * 0.08 : 0.06;

        // Advect Frame A forward in time, Frame B backward in time
        vec2 uvA_warped = (cellBaseA + clamp(localUV - flowUV * (subframeT * warpScale), 0.005, 0.995)) / grid;
        vec2 uvB_warped = (cellBaseB + clamp(localUV + flowUV * ((1.0 - subframeT) * warpScale), 0.005, 0.995)) / grid;

        vec4 colA = texture(texture0, uvA_warped);
        vec4 colB = texture(texture0, uvB_warped);
        texelColor = mix(colA, colB, subframeT);
        sampleUV = uvA_warped;

        if (u_sixWayLighting > 1.5)
        {
            vec4 bColA = texture(u_sixWayTexB, uvA_warped);
            vec4 bColB = texture(u_sixWayTexB, uvB_warped);
            texBColor = mix(bColA, bColB, subframeT);
        }
    }
    else
    {
        if (u_volumeSheet > 1.5)
        {
            // UV noise distortion fallback for un-vectored volume sheets
            vec2 grid = max(u_atlasGrid, vec2(1.0));
            vec2 localUV = (grid.x > 1.5 || grid.y > 1.5) ? fract(fragTexCoord * grid) : fragTexCoord;
            vec2 cellBase = floor(fragTexCoord * grid);

            vec2 pNoise = localUV * 2.8 + vec2(fragPosition.x * 0.9, fragPosition.y * 1.6 - u_time * 3.0);
            float dX = vnoise(pNoise) - 0.5;
            float dY = vnoise(pNoise + vec2(17.3, 31.7)) - 0.5;

            // Warp grows stronger towards the top (tongues lick), staying stable at the base
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

    float soft = (u_softFade > 0.0) ? SoftParticle_Factor(u_softFade) : 1.0;

    // ── PACKED VOLUME SHEET ──────────────────────────────────────────────────
    // One draw that both EMITS and OCCLUDES, which the ALPHA/ADDITIVE binary
    // cannot express. Output is premultiplied; see VFX_BLEND_PREMULTIPLIED.
    if (u_volumeSheet > 0.5)
    {
        // Debug 1 = the soft-particle factor. Debug 2 = the sheet's EMISSION,
        // the quantity the discard below tests — painted opaque so a fully
        // discarded quad shows as a hole in THIS view too, which is what tells
        // "the sprite drew nothing" apart from "the sprite was never drawn".
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
            // True 6-Way Flame Volume:
            // Texture 0 (Map A): RGB = (+X, +Y, +Z directional lightmaps), A = Opacity
            // u_sixWayTexB (Map B): RGB = (-X, -Y, -Z directional lightmaps), A = Flame Emission
            emis = texBColor.a;
            opac = texelColor.a;
            rawSoot = clamp(opac * clamp(1.0 - emis * 0.45, 0.0, 1.0), 0.0, 1.0);
            soot = clamp(rawSoot * u_smokeGain, 0.0, 1.0);
            shad = soot;
        }
        else
        {
            emis = texelColor.r;               // flame emission
            rawSoot = texelColor.g;            // smoke density, as simulated
            soot = clamp(rawSoot * u_smokeGain, 0.0, 1.0);
            shad = texelColor.b;   // self-shadowed smoke (B/G = light surviving)
            opac = texelColor.a;   // true opacity
        }

        // =====================================================================
        // PATH A: FLAME VOLUME (u_volumeSheet > 1.5)
        // Authentic, isotropic volumetric flame puff with clean boundary.
        // =====================================================================
        if (u_volumeSheet > 1.5)
        {
            // Clean edge feathering based on emission and opacity
            float flameMask = smoothstep(0.015, 0.08, emis + opac * 0.4);
            if (flameMask < 0.005) discard;

            float fade = fragColor.a * soft * flameMask;
            if (fade < 0.005) discard;

            float heat = clamp(emis * u_heatGain * fragColor.r, 0.0, 1.0);
            float radianceGating = pow(clamp(emis * 1.30, 0.0, 1.0), 1.10) * flameMask;

            // Authoritative Planck Blackbody LUT from u_rampLUT (vibrant radiant colors)
            vec3 rampCol = texture(u_rampLUT, vec2(heat, 0.5)).rgb;

            // High-energy incandescent core peak (> 0.95)
            float corePeak = smoothstep(0.95, 1.0, heat);
            vec3 radiantFlame = mix(rampCol, vec3(1.0, 0.98, 0.92), corePeak * 0.50);

            vec3 flame = radiantFlame * (radianceGating * u_emissiveBoost);
            flame = CompressFlameRadiance(flame, heat);

            // True Premultiplied Alpha: RGB is emitted radiance, Alpha is opacity
            // Pure flame (smokeGain <= 0.0) has low optical occlusion so backgrounds don't get cut out
            float flameOpacity = (u_smokeGain <= 0.0)
                ? clamp(fade * emis * 0.35, 0.0, 0.50)
                : clamp(fade * (0.15 * soot + emis * 0.45), 0.0, 0.95);

            if (u_smokeGain <= 0.0)
            {
                finalColor = vec4(flame * fade, flameOpacity);
                return;
            }

            float selfShadow = (soot > 0.004) ? clamp(shad / soot, 0.0, 1.0) : 1.0;
            float wrap;
            vec3 N = ParticleNormalWorld(ParticleNormalLocal(opac));
            vec3 lit = (u_sixWayLighting > 0.5)
                ? ParticleLightTerm6Way((u_atlasGrid.x > 1.5 || u_atlasGrid.y > 1.5) ? fract(fragTexCoord * u_atlasGrid) : fragTexCoord,
                                        texelColor.rgb, texBColor.rgb, texBColor.a,
                                        soot, selfShadow, opac, ParticleLightDir(), wrap)
                : ParticleLightTerm(N, ParticleLightDir(), wrap);
            vec3 smoke = u_smokeTint * lit * (u_sixWayLighting > 0.5 ? 1.0 : selfShadow);

            float matNow  = soot + emis;
            float matOrig = max(rawSoot + emis, 1e-4);
            float alpha = clamp(opac * clamp(matNow / matOrig, 0.0, 1.0) * fade, 0.0, 1.0);
            float sootFrac = clamp(soot / max(matNow, 1e-4), 0.0, 1.0);
            float blendAlpha = mix(flameOpacity, alpha, sootFrac);

            finalColor = vec4(flame * fade + smoke * alpha * sootFrac, blendAlpha);
            return;
        }

        // =====================================================================
        // PATH B: STANDARD VOLUME SHEET (u_volumeSheet <= 1.5, e.g. EnergyBurst)
        // Authentic, isotropic volumetric detonation and soft smoke puff.
        // =====================================================================
        float fade = fragColor.a * soft;
        if (fade < 0.004 || (emis < 0.004 && soot * opac < 0.004)) discard;

        float heat  = clamp(emis * u_heatGain * fragColor.r, 0.0, 1.0);
        vec3  rampCol = texture(u_rampLUT, vec2(heat, 0.5)).rgb;
        vec3  bbCol = calcBlackbodyNormalized(heat);
        vec3  flame = mix(rampCol, bbCol, 0.45)
                      * emis * opac * u_emissiveBoost;
        flame = CompressFlameRadiance(flame, heat);

        float selfShadow = (soot > 0.004) ? clamp(shad / soot, 0.0, 1.0) : 1.0;
        float wrap;
        vec3  N   = ParticleNormalWorld(ParticleNormalLocal(opac));
        vec3  lit = (u_sixWayLighting > 0.5)
            ? ParticleLightTerm6Way((u_atlasGrid.x > 1.5 || u_atlasGrid.y > 1.5) ? fract(fragTexCoord * u_atlasGrid) : fragTexCoord,
                                    texelColor.rgb, texBColor.rgb, texBColor.a,
                                    soot, selfShadow, opac, ParticleLightDir(), wrap)
            : ParticleLightTerm(N, ParticleLightDir(), wrap);
        vec3  smoke = u_smokeTint * lit * (u_sixWayLighting > 0.5 ? 1.0 : selfShadow);

        if (u_smokeGain <= 0.0)
        {
            finalColor = vec4(flame * fade, 1.0);
            return;
        }

        float matNow  = soot + emis;
        float matOrig = max(rawSoot + emis, 1e-4);
        float alpha = clamp(opac * clamp(matNow / matOrig, 0.0, 1.0) * fade, 0.0, 1.0);
        float sootFrac = clamp(soot / max(matNow, 1e-4), 0.0, 1.0);
        float flameOcclusion = 0.18;
        float blendAlpha = alpha * mix(flameOcclusion, 1.0, sootFrac);

        finalColor = vec4(flame * fade + smoke * alpha * sootFrac, blendAlpha);
        return;
    }

    vec4 base = texelColor * fragColor;

    if (base.a < 0.01) discard;   // fillrate: drop fully transparent edges

    if (u_softDebug > 0.5)
    {
        finalColor = vec4(vec3(soft), 1.0);
        return;
    }

    float effLightingStrength = (u_sixWayLighting > 0.5)
        ? (u_lightingStrength > 0.0 ? u_lightingStrength : 1.0)
        : u_lightingStrength;

    if (effLightingStrength <= 0.0)
    {
        // EMISSIVE particles take this branch — strength is set to 0 for them
        // per batch — so the HDR boost has to be applied HERE, not only at the
        // lit output below. Missing that is why the first attempt changed
        // nothing despite the uniform arriving: sparks and glints are exactly
        // the population that never reaches the lit path.
        // At boost 1.0 this is still byte-identical to the pre-F1 shader.
        float emisBoost = u_emissiveBoost;
        if (u_bgAdapt > 0.0 && u_resolution.x > 0.0) {
            float bg = texture(u_bgLuma, gl_FragCoord.xy / u_resolution).r;
            /* Hold full emission through the range the night arena lives in
               (~0.02), and fall away over the range a bright map would occupy.
               smoothstep, not a linear ramp: a particle crossing a lit/unlit
               boundary must not step. */
            emisBoost *= mix(1.0, 1.0 - smoothstep(0.15, 0.85, bg), u_bgAdapt);
        }
        vec3 unlitRgb = (u_sixWayLighting > 1.5) ? fragColor.rgb : base.rgb;
        float lum = dot(unlitRgb, vec3(0.299, 0.587, 0.114));
        if (lum > 0.05) {
            vec3 coreTint = vec3(1.0, 0.98, 0.92);
            float coreFactor = smoothstep(0.45, 1.0, lum);
            unlitRgb = mix(unlitRgb, coreTint * (1.0 + coreFactor * 1.5), coreFactor * 0.7);
        }
        finalColor = VFX_ResolveEmission(unlitRgb, emisBoost, 1.0,
                                         base.a * soft);
        return;
    }

    // ── Hemisphere normal from the quad-local UV ─────────────────────────────
    // ANALYTIC, not from derivatives. The derivative route shipped first and is
    // kept as the fallback in ParticleNormalLocal, but it fails exactly where it
    // matters: dFdx of the sprite alpha is ~0 across the whole flat CORE of a
    // soft particle, so `dir` collapses to zero there and the normal snaps to
    // (0,0,1) — the brightest, largest part of every sprite ends up facing the
    // camera with no directional shading at all. On top of that the source
    // texture is 64px magnified many times over, so the derivative is quantised
    // into visible radial spokes.
    vec3 n = ParticleNormalLocal(texelColor.a);
    vec3 N = ParticleNormalWorld(n);

    // Debug view: paint the SCREEN-SPACE normal, not the world one. Painting N
    // was useless — its view-direction component dominates, so every fragment
    // resolved to nearly the same colour whether the maths was right or wrong.
    // In screen space a correct dome is unmistakable: red rising left→right,
    // green rising bottom→top, pale blue in the middle. A flat single colour
    // means the normal collapsed and no light tuning will rescue it.
    if (u_debugNormal > 0.5 && u_debugNormal < 1.5)
    {
        finalColor = vec4(n * 0.5 + 0.5, base.a);
        return;
    }

    vec3 L = ParticleLightDir();

    // Mode 3 — is the uniform even arriving? Paint L. Sweeping the azimuth must
    // change this colour. If it does not, the tunable is not reaching the shader
    // and every other observation is meaningless.
    if (u_debugNormal > 2.5)
    {
        finalColor = vec4(L * 0.5 + 0.5, 1.0);
        return;
    }

    float wrap;
    vec3  lit = (u_sixWayLighting > 0.5)
        ? ParticleLightTerm6Way((u_atlasGrid.x > 1.5 || u_atlasGrid.y > 1.5) ? fract(fragTexCoord * u_atlasGrid) : fragTexCoord,
                                texelColor.rgb, texBColor.rgb, texBColor.a,
                                base.a, 1.0 - base.a * 0.5, base.a, L, wrap)
        : ParticleLightTerm(N, L, wrap);

    // Mode 2 — pure lighting, opaque. No texture colour, no alpha falloff, so a
    // bright centre here CANNOT be alpha stacking. Each sprite shows as a hard
    // square: one side must be bright and the opposite side dark, and sweeping
    // the azimuth must rotate which side.
    if (u_debugNormal > 1.5)
    {
        finalColor = vec4(vec3(wrap), 1.0);
        return;
    }

    // Additive-free: modulate the body colour. Deliberately NOT clamped to 1 —
    // ACES in post_fx rolls the highlights off, and clamping here would flatten
    // exactly the bright rim this whole shader exists to produce.
    //
    // For 6-way dual lightmap pairs (Mode 2), texture0 holds directional lightmaps
    // (+X, +Y, +Z) rather than diffuse surface albedo. Diffuse albedo comes from
    // a neutral white carrier. Multiplying directional maps directly into base.rgb
    // would paint the lobes raw red/green/blue; using fragColor.rgb would leak the
    // caller's element tint into smoke that is explicitly authored as white.
    vec3 albedo = (u_sixWayLighting > 1.5) ? vec3(1.0) : base.rgb;
    vec3 shaded = albedo * lit;
    // Boost is 1.0 for lit batches, so this is a no-op for smoke and dust.
    finalColor = VFX_ResolveBody(mix(albedo, shaded, effLightingStrength),
                                 u_emissiveBoost, base.a * soft);
}
