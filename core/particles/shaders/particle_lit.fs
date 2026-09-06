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
        vec2  xy = (rr > 1e-5) ? (q / rr) * rc * u_normalBulge : vec2(0.0);
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

// dFdx/dFdy are in screen space, so n is too — rebuild a world-space basis from
// the view vector. Billboards face the camera, so camera right/up are the quad's
// right/up to a very good approximation.
vec3 ParticleNormalWorld(vec3 n)
{
    vec3 V = normalize(viewPos - fragPosition);
    vec3 upRef = abs(V.y) > 0.99 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 R = normalize(cross(upRef, V));
    vec3 U = cross(V, R);
    return normalize(n.x * R + n.y * U + n.z * V);
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

    // ── Forward scatter — the backlit glow ───────────────────────────────────
    // The single most convincing volumetric cue: light coming from BEHIND the
    // puff bleeds through it. Peaks when the view vector aligns with the light.
    if (u_scatterStrength > 0.0)
    {
        vec3  V = normalize(viewPos - fragPosition);
        float backlit = max(0.0, dot(-V, L));
        lit += u_sunColor * pow(backlit, 4.0) * u_scatterStrength;
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
// Unity VFX Graph 6-way directional transmission & scattering model.
// Evaluates light along 6 cardinal directions in particle billboard space:
//   Map A: (+X Right, +Y Top,    +Z Back / Transmitted through volume)
//   Map B: (-X Left,  -Y Bottom, -Z Front / Camera-facing reflection)
vec3 ParticleLightTerm6Way(vec2 luv, float soot, float selfShadow, float opac, vec3 L, out float wrapOut)
{
    // Billboard local coordinate basis:
    // R = Right (+X), U = Up (+Y), -V = Back (+Z, light shining towards camera through particle)
    vec3 V = normalize(viewPos - fragPosition);
    vec3 upRef = abs(V.y) > 0.99 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 R = normalize(cross(upRef, V));
    vec3 U = cross(V, R);

    vec3 mapA;
    vec3 mapB;
    float ao = 1.0;

    if (u_sixWayLighting > 1.5)
    {
        // Dual-texture 6-way lightmap pair (Unity standard):
        // texture0 = Map A (+X Right, +Y Top, +Z Backlight, Alpha: Opacity)
        // u_sixWayTexB = Map B (-X Left, -Y Bottom, -Z Front, Alpha: Opacity)
        mapA = texture(texture0, fragTexCoord).rgb;
        mapB = texture(u_sixWayTexB, fragTexCoord).rgb;
        ao = clamp(dot(mapA + mapB, vec3(1.0 / 6.0)), 0.05, 1.0);

        // Modulate with scattering and absorption controls
        float scFactor = (u_sixWayScattering > 0.0 ? u_sixWayScattering : 1.0);
        float absFactor = (u_sixWayAbsorption > 0.0 ? u_sixWayAbsorption : 1.0);
        mapA.b *= scFactor;
        if (abs(absFactor - 1.0) > 0.01)
        {
            mapA = pow(clamp(mapA, 0.0, 1.0), vec3(absFactor));
            mapB = pow(clamp(mapB, 0.0, 1.0), vec3(absFactor));
        }
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

        float ext = clamp((u_sixWayAbsorption > 0.0 ? u_sixWayAbsorption : 1.0) * 1.6, 0.2, 5.0);
        float tX_pos = pow(clamp(1.0 - pX_pos * dens * ext, 0.0, 1.0), 1.6);
        float tX_neg = pow(clamp(1.0 - pX_neg * dens * ext, 0.0, 1.0), 1.6);
        float tY_pos = pow(clamp(1.0 - pY_pos * dens * ext, 0.0, 1.0), 1.6);
        float tY_neg = pow(clamp(1.0 - pY_neg * dens * ext, 0.0, 1.0), 1.6);

        // +Z is forward scatter: light coming from BEHIND puff bleeding through to camera
        float scFactor = (u_sixWayScattering > 0.0 ? u_sixWayScattering : 1.0);
        float backlit = max(0.0, dot(L, -V));
        float fwdScatter = clamp(1.0 + scFactor * 3.5 * pow(backlit, 4.0), 1.0, 6.0);
        float tZ_back = pow(clamp(selfShadow, 0.0, 1.0), 0.6) * fwdScatter;

        // -Z is front reflection from camera side
        float tZ_front = clamp(1.0 - dens * 0.45, 0.15, 1.0) * clamp(1.0 - rQuad * 0.35, 0.1, 1.0);

        mapA = vec3(tX_pos, tY_pos, tZ_back);
        mapB = vec3(tX_neg, tY_neg, tZ_front);
    }

    // Direct directional light (Sun) transformed into billboard local coordinates
    vec3 L_local = vec3(dot(L, R), dot(L, U), dot(L, -V));
    vec3 L_pos = max(vec3(0.0), L_local);
    vec3 L_neg = max(vec3(0.0), -L_local);
    float dirLit = dot(L_pos, mapA) + dot(L_neg, mapB);
    wrapOut = dirLit;

    float effectiveSunGain = max(u_sunGain, 1.8);
    vec3 lit = u_sunColor * (effectiveSunGain * dirLit);

    // Multi-directional ambient environment lighting
    vec3 ambGround = (length(u_ambientGround) > 1e-4) ? u_ambientGround : (u_ambient * 0.35);
    vec3 ambHorizon = (length(u_ambientHorizon) > 1e-4) ? u_ambientHorizon : (u_ambient * 0.65);

    vec3 ambLit = (u_ambient * u_ambientGain * mapA.g
                + ambGround * mapB.g
                + ambHorizon * ((mapA.r + mapB.r + mapA.b + mapB.b) * 0.25)) * ao;
    lit += ambLit;

    // VFX point lights evaluated in 6-way billboard space
    for (int i = 0; i < MAX_VFX_LIGHTS; i++)
    {
        if (i >= u_vfxLightCount) break;
        vec3 toL = u_vfxLightPos[i] - fragPosition;
        float dist = length(toL);
        float att = clamp(1.0 - dist / max(u_vfxLightRadius[i], 0.001), 0.0, 1.0);
        att *= att;
        if (att <= 0.0) continue;

        vec3 Lpt = toL / max(dist, 0.001);
        vec3 Lpt_local = vec3(dot(Lpt, R), dot(Lpt, U), dot(Lpt, -V));
        vec3 Lpt_pos = max(vec3(0.0), Lpt_local);
        vec3 Lpt_neg = max(vec3(0.0), -Lpt_local);
        float ptTransmission = dot(Lpt_pos, mapA) + dot(Lpt_neg, mapB);
        lit += u_vfxLightColor[i] * ptTransmission * att;
    }
    return lit;
}

void main()
{
    vec4 texelColor = texture(texture0, fragTexCoord);
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

        float emis = texelColor.r;               // flame emission
        float rawSoot = texelColor.g;            // smoke density, as simulated
        float soot = clamp(rawSoot * u_smokeGain, 0.0, 1.0);
        float shad = texelColor.b;   // self-shadowed smoke (B/G = light surviving)
        float opac = texelColor.a;   // true opacity

        // fragColor.a is the particle's own fade (alphaCurve x colorStart.a).
        // fragColor.r is its HEAT scale over life — in volume mode the C side
        // writes a GREY vertex colour whose level is the cooling curve, because
        // hue now comes from the ramp and the vertex slot is free to carry a
        // scalar instead. See ParticleSystem's volume branch.
        float fade = fragColor.a * soft;
        if (fade < 0.004 || (emis < 0.004 && soot * opac < 0.004)) discard;

        // FLAME — emission indexes the ramp, so a single sprite carries a
        // white-hot core, a yellow shoulder and a deep-red rim. This is the
        // whole reason the volume path exists: the legacy path multiplies the
        // entire quad by ONE vertex colour and cannot zone colour at all.
        float heat  = clamp(emis * u_heatGain * fragColor.r, 0.0, 1.0);
        // Radiance is gated by COVERAGE as well as by emission. Without the
        // `opac` factor a texel with a whisker of emission still radiates at
        // full strength, so the faint tail of every cell lights up and each
        // sprite shows as a glowing SQUARE — the quad's own boundary becomes
        // visible because nothing makes the light fall off where the gas stops.
        // Multiplying by opacity is also what the physics says: a ray radiates
        // in proportion to how much hot gas it crossed, which is exactly what A
        // integrates.
        vec3  flame = texture(u_rampLUT, vec2(heat, 0.5)).rgb
                      * emis * opac * u_emissiveBoost;

        // SMOKE — B/G is the fraction of light that survived to each texel,
        // baked at sim time. A billboard cannot compute that at runtime, and
        // without it stacked puffs read as flat cards (scripts/flipbook/render.py).
        float selfShadow = (soot > 0.004) ? clamp(shad / soot, 0.0, 1.0) : 1.0;
        float wrap;
        vec3  N   = ParticleNormalWorld(ParticleNormalLocal(opac));
        vec3  lit = (u_sixWayLighting > 0.5)
            ? ParticleLightTerm6Way((u_atlasGrid.x > 1.5 || u_atlasGrid.y > 1.5) ? fract(fragTexCoord * u_atlasGrid) : fragTexCoord,
                                    soot, selfShadow, opac, ParticleLightDir(), wrap)
            : ParticleLightTerm(N, ParticleLightDir(), wrap);
        vec3  smoke = u_smokeTint * lit * (u_sixWayLighting > 0.5 ? 1.0 : selfShadow);

        // COVERAGE IS `opac`, NOT `soot * opac`.
        //
        // The first version gated alpha on soot, reasoning that only smoke
        // occludes and flame is pure light. That is wrong twice over. The sim
        // already answers the question: A is 1 - transmittance with the flame's
        // own extinction folded in (render.py --flame-extinction), so hot gas
        // DOES block what is behind it. And gating on soot put the LEAST alpha
        // exactly where emission is strongest — hot gas carries little soot —
        // so the bright core had almost no coverage and simply added light to
        // whatever was behind it. Over a night sky that passes; over a bright
        // sky the core turns milky and the whole flame washes out, because
        // adding light to an already-bright destination only pushes it toward
        // white and strips the colour out.
        //
        // With coverage from `opac` the core REPLACES its background instead of
        // tinting it, so the flame holds the same colour on any sky — which is
        // the actual requirement, and the thing the additive-core build could
        // never satisfy at all.
        // Coverage has to follow the smoke dial too, or a flame with its soot
        // turned off still occludes as if the soot were there. `opac` is the
        // whole gas column's 1 - transmittance; scale it by how much material
        // survives the dial. At u_smokeGain 1 the ratio is exactly 1, so this
        // is an identity for everything already authored.
        // ── PURE LIGHT: no soot means no silhouette ──────────────────────
        //
        // u_smokeGain 0 declares an effect with no soot at all — an energy
        // burst, not a fire. Such a thing cannot occlude, so it emits alpha 0,
        // and premultiplied blending (`src.rgb + dst*(1-0)`) degenerates to
        // EXACT addition. One blend mode therefore serves both: fire occludes
        // with the coverage it earns, energy adds and never darkens anything.
        //
        // Taking the branch matters rather than just letting the arithmetic
        // trend to zero: below, coverage is opac*(soot+emis)/(rawSoot+emis),
        // which stays NON-zero at smokeGain 0 because emission is in both
        // terms. An energy burst would keep punching a faint hole in the scene.
        if (u_smokeGain <= 0.0)
        {
            // ALPHA 1, NOT 0 — and the difference is the routing.
            //
            // Pure light belongs in the EMISSION pass with BLEND_ADDITIVE,
            // which rlvk defines as `src*SRC_ALPHA + dst`: alpha SCALES the
            // contribution, so emitting 0 here deletes the effect entirely.
            // The intensity is already in rgb (flame * fade), so alpha 1 makes
            // the hardware add exactly that.
            //
            // The first attempt emitted alpha 0 and asked for
            // VFX_BLEND_PREMULTIPLIED instead, reasoning that `src.rgb +
            // dst*(1-0)` is also exact addition. It is — but PREMULTIPLIED is
            // routed to the BODY pass, alongside trails, decals and
            // afterimages and inside their depth-mask handling, and drawn
            // there this produced sharp horizontal bands cut out of the
            // effect. Swapping the blend mode alone made them vanish, which is
            // what identified it after six other causes had been eliminated.
            // Emission is where something with no silhouette belongs anyway —
            // it is the rule particle_system.h already states.
            finalColor = vec4(flame * fade, 1.0);
            return;
        }

        float matNow  = soot + emis;
        float matOrig = max(rawSoot + emis, 1e-4);
        float alpha = clamp(opac * clamp(matNow / matOrig, 0.0, 1.0) * fade, 0.0, 1.0);

        // Split that coverage between the two populations by what is actually
        // in the texel. The soot half is lit and can be DARKER than the sky;
        // the flame half is emission, added on top of the background it just
        // occluded — the pair is what premultiplied blending exists for.
        float sootFrac = clamp(soot / max(matNow, 1e-4), 0.0, 1.0);

        // NOT clamped to 1: ACES in post_fx rolls the highlights off, and
        // clamping here would flatten the blown-out core and kill its bloom.
        finalColor = vec4(flame * fade + smoke * alpha * sootFrac, alpha);
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
    // fragColor.rgb. Multiplying directional maps directly into base.rgb would paint
    // the lobes raw red/green/blue.
    vec3 albedo = (u_sixWayLighting > 1.5) ? fragColor.rgb : base.rgb;
    vec3 shaded = albedo * lit;
    // Boost is 1.0 for lit batches, so this is a no-op for smoke and dust.
    finalColor = VFX_ResolveBody(mix(albedo, shaded, effLightingStrength),
                                 u_emissiveBoost, base.a * soft);
}
