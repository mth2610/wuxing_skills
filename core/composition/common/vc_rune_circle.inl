// ── E5.2 — VFX_ComposeRuneCircle ─────────────────────────────────────────────
//
// ONE QUAD. Every ring, rune, dash, tick and square is computed in
// core/shaders/rune_circle.fs over the local coordinate of a single flat quad.
//
// WHAT THIS REPLACED, AND WHY IT HAD TO GO (measured 24/08/2026 with
// scripts/render_vfx_matrix.sh "RUNE CIRCLE"):
//
//   28 DrawRibbonStripEx calls per frame — 4 rings x 2 sub-layers x 2 passes,
//   plus 12 focus polygons — each ending in a forced rlDrawRenderBatchActive(),
//   to trace a path that cos/sin gives in closed form. ~8k vertices and 28
//   batch flushes for a circle.
//
//   And the cost bought a WORSE image, for a reason no amount of tuning could
//   reach: a ribbon is geometry, so a stroke narrower than a pixel is hit or
//   missed by the rasteriser, never partially covered. Every stroke measured
//   1-5 px (most 1-3), so the rings crawled and broke into dashes as they
//   turned. Analytic coverage in the fragment stage has no such floor — see
//   Band() in the shader.
//
//   The numbers that made the case, at warmup 90:
//     cover%   1.24 (dark) -> 0.71 (white)   silhouette losing 46% of itself
//     absvar   26.4 (dark) -> 13.4 (white)   over half the "structure" was
//                                            background showing through gaps
//     darken%  0.0 on dark AND mid           the BODY pass contributed nothing
//                                            on the only backgrounds the game
//                                            actually has
//
// WHAT IT LOOKED LIKE: an orange wireframe. cover% of 1.5 means the effect was
// stroke and nothing else — no fill, no interior, no skirt — so bloom had no
// area to bleed from and the circle could be bright without ever looking like
// it was giving off light. The shader header explains the three things that
// replace that; this file's job is to hand it clean parameters and submit the
// quad twice.
//
// THE BODY PASS IS KEPT DELIBERATELY. Measuring PORTAL DISC and SHOCK RING on
// the same harness: both are INVISIBLE on a white plate. This effect was not,
// and the alpha body pass carrying elemental pigment is the only structural
// difference. Moving to a shader must not quietly drop it.

#include "core/vfx_contrast.h"

static SkillCurve s_runeOpen = {0};   // radius vs t01: snaps open, eases shut
static SkillCurve s_runeFade = {0};   // alpha vs t01
static bool       s_runeInit = false;

static float s_runeWhite  = 0.86f;    // how far the hot ramp goes toward white
static float s_runeWidth  = 1.0f;     // x on stroke thickness
static float s_runeSpin   = 1.0f;     // x on rotation speed
static float s_runeDash   = 1.0f;     // x on comet-head speed
static float s_runeEnergy = 1.0f;     // x on interior, pulses, halo, sweeps
static float s_runeGlow   = 1.0f;     // x on emission gain
static float s_runeConform = 1.0f;    // 0 = flat quad, ignore the terrain (free)

// ── SITTING ON THE GROUND ───────────────────────────────────────────────────
//
// A single flat quad at the caster's Y is COPLANAR with the terrain under it,
// and terrain is not flat. Both halves of that bite:
//   - Where the two surfaces agree to within depth precision, they z-fight: the
//     24/08 grass capture shows a stipple band straight across the inscription.
//   - Where the ground rises even slightly above the quad's plane, the depth
//     test simply hides that part of the circle. On rolling grass most of the
//     circle disappears and what is left reads as unrelated arcs.
//
// So the surface is TESSELLATED and draped over the real ground, and lifted a
// few centimetres clear of it. Two constraints shape how:
//
// COST — and this file briefly carried an elaborate answer to the wrong
// question. Sampling a 7x7 grid every frame originally cost ~6 ms of CPU,
// because `MapManager_GetGroundHeightAt` tested the ray against every triangle
// of the terrain mesh. That got a round-robin cache with an LRU of slots and a
// seeding schedule, all of which worked and none of which should have existed:
// the real defect was in the query, and it was fixed at the source on the same
// day (`MapGroundLookup` in maps/toolkit/map_props_ground.inl — an XZ bin grid
// over the same triangles, 232-292 us -> ~0.59 us per sample, verified identical
// on 4,096 probes). 49 samples a frame is now ~29 us, so this file samples them
// plainly and keeps nothing.
//
// If that ever regresses, the symptom will be frame time rather than anything
// visible; ENGINE_LANDMINES.md 21 has the measurement and the reproduction.
//
// A 7x7 grid of samples is bilinearly interpolated onto a 16x16 mesh, and that
// part is NOT about cost: the terrain under VERDANT_PATH is a 64x64 heightmap
// over the whole map, so 7x7 across a few-metre circle is already finer than
// the surface it is describing, and interpolation is exact on a planar slope.
//
// RELATIVE, NOT ABSOLUTE. Vertices take the ground's height DIFFERENCE from the
// centre, not its absolute height. Snapping to absolute ground would drag a
// circle a caller deliberately placed in mid-air down onto the floor; taking
// the difference makes it follow the terrain's SHAPE at whatever height it was
// asked for.
#define RUNE_MESH_CELLS 16          // NxN quads submitted
#define RUNE_H_SAMPLES  7           // NxN terrain height grid (49 samples)
#define RUNE_Y_LIFT     0.035f      // metres clear of the receiver

// How far past the nominal radius the quad reaches. The halo is a gaussian
// skirt centred on the rim; clipping it at r = 1 would put a straight polygon
// edge across the glow, which is the one artefact a single-quad effect can
// still produce.
#define RUNE_QUAD_EXT 1.32f

typedef struct {
    Shader shader;
    int bodyColor, glowColor, hotColor;
    int params, style, spin, sweep, pulse, flow;
} RuneShader;

static RuneShader s_runeFx = {0};
static bool s_runeShaderTried = false;

static void Rune_InitShared(void)
{
    if (s_runeInit) return;

    FloatCurve_AddStop(&s_runeOpen, 0.00f, 0.05f);
    FloatCurve_AddStop(&s_runeOpen, 0.12f, 1.06f);
    FloatCurve_AddStop(&s_runeOpen, 0.22f, 1.00f);
    FloatCurve_AddStop(&s_runeOpen, 0.82f, 0.98f);
    FloatCurve_AddStop(&s_runeOpen, 1.00f, 0.55f);

    FloatCurve_AddStop(&s_runeFade, 0.00f, 0.0f);
    FloatCurve_AddStop(&s_runeFade, 0.08f, 1.0f);
    FloatCurve_AddStop(&s_runeFade, 0.80f, 1.0f);
    FloatCurve_AddStop(&s_runeFade, 1.00f, 0.0f);

    // Lazily, never from a subsystem Init (core/docs/LANDMINES.md).
    Tuning_RegisterFloat("rune_white",  &s_runeWhite,  0.86f);
    Tuning_RegisterFloat("rune_width",  &s_runeWidth,  1.0f);
    Tuning_RegisterFloat("rune_spin",   &s_runeSpin,   1.0f);
    Tuning_RegisterFloat("rune_dash",   &s_runeDash,   1.0f);
    Tuning_RegisterFloat("rune_energy", &s_runeEnergy, 1.0f);
    Tuning_RegisterFloat("rune_glow",   &s_runeGlow,   1.0f);
    Tuning_RegisterFloat("rune_conform", &s_runeConform, 1.0f);

    s_runeInit = true;
}

static void Rune_InitShader(void)
{
    if (s_runeShaderTried) return;
    s_runeShaderTried = true;

    s_runeFx.shader = ResourceManager_LoadShader("core/shaders/rune_circle.vs",
                                                 "core/shaders/rune_circle.fs");
    if (s_runeFx.shader.id == 0)
    {
        // Say it in the shader's own words. A missing or failed shader
        // otherwise reports as "the effect stopped appearing", which sends the
        // next reader looking at the composition (ENGINE_LANDMINES, "A missing
        // shader file does not report as a shader problem").
        TraceLog(LOG_WARNING, "RuneCircle: rune_circle.vs/.fs failed to load - "
                              "the rune circle will not draw");
        return;
    }
    s_runeFx.bodyColor = GetShaderLocation(s_runeFx.shader, "u_bodyColor");
    s_runeFx.glowColor = GetShaderLocation(s_runeFx.shader, "u_glowColor");
    s_runeFx.hotColor  = GetShaderLocation(s_runeFx.shader, "u_hotColor");
    s_runeFx.params    = GetShaderLocation(s_runeFx.shader, "u_params");
    s_runeFx.style     = GetShaderLocation(s_runeFx.shader, "u_style");
    s_runeFx.spin      = GetShaderLocation(s_runeFx.shader, "u_spin");
    s_runeFx.sweep     = GetShaderLocation(s_runeFx.shader, "u_sweep");
    s_runeFx.pulse     = GetShaderLocation(s_runeFx.shader, "u_pulse");
    s_runeFx.flow      = GetShaderLocation(s_runeFx.shader, "u_flow");
}

static bool Rune_HasShader(void)
{
    return s_runeFx.shader.id != 0 && s_runeFx.bodyColor >= 0 &&
           s_runeFx.glowColor >= 0 && s_runeFx.hotColor >= 0 &&
           s_runeFx.params >= 0 && s_runeFx.style >= 0 && s_runeFx.spin >= 0 &&
           s_runeFx.sweep >= 0 && s_runeFx.pulse >= 0 && s_runeFx.flow >= 0;
}

static void Rune_PlaneBasis(Vector3 normal, Vector3 *outU, Vector3 *outV)
{
    Vector3 n = Vector3Normalize(normal);
    Vector3 ref = (fabsf(n.y) < 0.99f) ? (Vector3){0.0f, 1.0f, 0.0f}
                                       : (Vector3){1.0f, 0.0f, 0.0f};
    *outU = Vector3Normalize(Vector3CrossProduct(n, ref));
    *outV = Vector3CrossProduct(n, *outU);
}

// Every rotating phase is folded into its period HERE, on the CPU, in double
// precision, and handed to the shader already wrapped. Folding it in GLSL with
// fract() cannot recover precision the float lost on the way in — u_time
// reaches four digits in a match, and by then a term like time*speed*count has
// no fractional bits left to fract (ENGINE_LANDMINES, "fract() for float
// precision: fold each product ONCE, never nest").
static float Rune_WrapTau(double x)
{
    const double tau = 6.283185307179586;
    double m = fmod(x, tau);
    if (m < 0.0) m += tau;
    return (float)m;
}

static float Rune_Wrap01(double x)
{
    double m = fmod(x, 1.0);
    if (m < 0.0) m += 1.0;
    return (float)m;
}

static void Rune_SetUniforms(Color bodyCol, Color glowCol, Color hotCol,
                             float fade, float open, float emission,
                             float coverGain, float premultiply,
                             double time, float seed)
{
    Vector4 b = ColorNormalize(bodyCol);
    Vector4 g = ColorNormalize(glowCol);
    Vector4 h = ColorNormalize(hotCol);
    SetShaderValue(s_runeFx.shader, s_runeFx.bodyColor, &b, SHADER_UNIFORM_VEC4);
    SetShaderValue(s_runeFx.shader, s_runeFx.glowColor, &g, SHADER_UNIFORM_VEC4);
    SetShaderValue(s_runeFx.shader, s_runeFx.hotColor,  &h, SHADER_UNIFORM_VEC4);

    float params[4] = { fade, emission, open, premultiply };
    float style[4]  = { s_runeWidth, s_runeEnergy, coverGain, seed };
    SetShaderValue(s_runeFx.shader, s_runeFx.params, params, SHADER_UNIFORM_VEC4);
    SetShaderValue(s_runeFx.shader, s_runeFx.style,  style,  SHADER_UNIFORM_VEC4);

    // Three rings turning at three speeds, the middle one against the other
    // two. Equal-and-opposite pairs are what make a ring assembly read as a
    // mechanism rather than as one picture being spun.
    double sp = (double)s_runeSpin;
    float spin[3] = {
        Rune_WrapTau( time * 0.115 * sp),
        Rune_WrapTau(-time * 0.185 * sp),
        Rune_WrapTau( time * 0.255 * sp),
    };
    SetShaderValue(s_runeFx.shader, s_runeFx.spin, spin, SHADER_UNIFORM_VEC3);

    // The comet heads run much faster than the rings carry them, so charge
    // visibly travels THROUGH the construction instead of riding on it.
    double sw = (double)s_runeDash;
    float sweep[3] = {
        Rune_WrapTau( time * 1.35 * sw),
        Rune_WrapTau(-time * 2.05 * sw + 2.1),
        Rune_WrapTau( time * 0.78 * sw + 4.3),
    };
    SetShaderValue(s_runeFx.shader, s_runeFx.sweep, sweep, SHADER_UNIFORM_VEC3);

    // Two outward pulses, half a period apart, so the circle never sits still
    // between beats.
    float pulse[2] = {
        Rune_Wrap01(time * 0.34),
        Rune_Wrap01(time * 0.34 + 0.5),
    };
    SetShaderValue(s_runeFx.shader, s_runeFx.pulse, pulse, SHADER_UNIFORM_VEC2);

    float flow[2] = { Rune_WrapTau(time * 0.21), Rune_WrapTau(time * 0.33) };
    SetShaderValue(s_runeFx.shader, s_runeFx.flow, flow, SHADER_UNIFORM_VEC2);
}

// The surface. Texture coordinates carry the disc's OWN frame in rune radii, so
// the fragment stage never has to reconstruct it from fragPosition — which
// would be view space, not world space, for every draw inside MyBeginMode3D
// (ENGINE_LANDMINES 9). They are computed from the grid index alone and are
// therefore unaffected by the draping: the circle is a PROJECTION onto the
// ground, so its pattern must not stretch when the ground under it does.
#define RUNE_GRID_VERTS ((RUNE_MESH_CELLS + 1) * (RUNE_MESH_CELLS + 1))
#define RUNE_H_COUNT    (RUNE_H_SAMPLES * RUNE_H_SAMPLES)
static Vector3 s_runeVerts[RUNE_GRID_VERTS];
static float   s_runeH[RUNE_H_COUNT];
static int     s_runeCells = 1;   // 1 when flat, RUNE_MESH_CELLS when draped

static float Rune_SampleGrid(float fx, float fz)
{
    const int S = RUNE_H_SAMPLES;
    float gx = (fx * 0.5f + 0.5f) * (float)(S - 1);
    float gz = (fz * 0.5f + 0.5f) * (float)(S - 1);
    int i0 = (int)floorf(gx), j0 = (int)floorf(gz);
    if (i0 < 0) i0 = 0; if (i0 > S - 2) i0 = S - 2;
    if (j0 < 0) j0 = 0; if (j0 > S - 2) j0 = S - 2;
    float tx = gx - (float)i0, tz = gz - (float)j0;
    float h00 = s_runeH[j0 * S + i0],       h10 = s_runeH[j0 * S + i0 + 1];
    float h01 = s_runeH[(j0 + 1) * S + i0], h11 = s_runeH[(j0 + 1) * S + i0 + 1];
    return Math_Mix(Math_Mix(h00, h10, tx), Math_Mix(h01, h11, tx), tz);
}

// Returns the number of cells per axis actually built (1 = a plain quad).
static int Rune_BuildSurface(Vector3 center, Vector3 u, Vector3 v, Vector3 n,
                             float radius)
{
    const float e = RUNE_QUAD_EXT;
    float half = radius * e;
    Vector3 lift = Vector3Scale(n, RUNE_Y_LIFT);

    // Conform only on a ground-plane circle, and only where the active map can
    // actually answer. SampleGroundSurfaceAt returns FALSE when there is no map
    // or the map has no receiver hook — and that distinction matters, because
    // MapManager_GetGroundHeightAt answers 0.0 in exactly those cases and 0.0 is
    // a perfectly plausible height. Draping on it would silently teleport every
    // circle to y = 0 on the flat arena and in the headless harness.
    Vector3 probePos; Vector3 probeNrm;
    bool conform = (s_runeConform >= 0.5f) && (n.y > 0.94f) &&
                   MapManager_SampleGroundSurfaceAt(center.x, center.z,
                                                    &probePos, &probeNrm);

    int cells = conform ? RUNE_MESH_CELLS : 1;
    const int S = RUNE_H_SAMPLES;

    if (conform)
    {
        float base = MapManager_GetGroundHeightAt(center.x, center.z);
        for (int j = 0; j < S; j++)
        {
            float fz = -1.0f + 2.0f * (float)j / (float)(S - 1);
            for (int i = 0; i < S; i++)
            {
                float fx = -1.0f + 2.0f * (float)i / (float)(S - 1);
                Vector3 wp = Vector3Add(center,
                    Vector3Add(Vector3Scale(u, fx * half),
                               Vector3Scale(v, fz * half)));
                s_runeH[j * S + i] =
                    MapManager_GetGroundHeightAt(wp.x, wp.z) - base;
            }
        }
    }

    for (int j = 0; j <= cells; j++)
    {
        float fz = -1.0f + 2.0f * (float)j / (float)cells;
        for (int i = 0; i <= cells; i++)
        {
            float fx = -1.0f + 2.0f * (float)i / (float)cells;
            Vector3 p = Vector3Add(center,
                Vector3Add(Vector3Scale(u, fx * half),
                           Vector3Scale(v, fz * half)));
            if (conform) p.y += Rune_SampleGrid(fx, fz);
            s_runeVerts[j * (cells + 1) + i] = Vector3Add(p, lift);
        }
    }
    return cells;
}

static void Rune_SubmitSurface(Vector3 n, int cells)
{
    const float e = RUNE_QUAD_EXT;
    float step = 2.0f * e / (float)cells;
    int stride = cells + 1;

    rlColor4ub(255, 255, 255, 255);
    for (int j = 0; j < cells; j++)
    {
        float t0 = -e + step * (float)j;
        float t1 = t0 + step;
        for (int i = 0; i < cells; i++)
        {
            float s0 = -e + step * (float)i;
            float s1 = s0 + step;
            Vector3 a = s_runeVerts[j * stride + i];
            Vector3 b = s_runeVerts[j * stride + i + 1];
            Vector3 c = s_runeVerts[(j + 1) * stride + i + 1];
            Vector3 d = s_runeVerts[(j + 1) * stride + i];

            // A normal on every vertex even though the fragment stage ignores
            // it: VS_FinalOutput normalises it, and normalize((0,0,0)) is NaN,
            // which shows up on Android as a white surface rather than as a
            // missing one (AGENT_CODE_STANDARD 7a).
            rlNormal3f(n.x, n.y, n.z); rlTexCoord2f(s0, t0); rlVertex3f(a.x, a.y, a.z);
            rlNormal3f(n.x, n.y, n.z); rlTexCoord2f(s1, t0); rlVertex3f(b.x, b.y, b.z);
            rlNormal3f(n.x, n.y, n.z); rlTexCoord2f(s1, t1); rlVertex3f(c.x, c.y, c.z);
            rlNormal3f(n.x, n.y, n.z); rlTexCoord2f(s0, t1); rlVertex3f(d.x, d.y, d.z);
        }
    }
}

// Continuous: call once per frame with the caster's progress.
//
// `ringCount` no longer selects how many ribbons are built — the ring hierarchy
// is fixed in the shader, which is what lets it stay a single draw. It now
// scales how much of the assembly is lit, so the parameter still means "how
// elaborate is this circle" to every existing caller.
void VFX_ComposeRuneCircle(Vector3 center, Vector3 normal, VC_MaterialId mat,
                           float radius, float t01, int ringCount)
{
    Rune_InitShared();
    Rune_InitShader();
    if (!Rune_HasShader()) return;

    if (radius <= 0.0f) radius = 1.0f;
    if (t01 < 0.0f) t01 = 0.0f;
    if (t01 > 1.0f) t01 = 1.0f;
    if (ringCount < 1) ringCount = 1;
    if (ringCount > 4) ringCount = 4;

    const VFX_ElementMaterial *m = VFX_Material(mat);
    float open = SkillCurve_Eval(&s_runeOpen, t01);
    float fade = SkillCurve_Eval(&s_runeFade, t01);
    if (fade <= 0.001f) return;

    // Elaboration rides on the caller's ring count without changing the draw.
    float elaborate = 0.55f + 0.15f * (float)ringCount;

    Color bodyInk = VFXContrast_ApplyColor(m->body, VFX_CONTRAST_MAGIC,
                                           VFX_CONTRAST_BODY);
    Color glowCol = VFXContrast_ApplyColor(m->glow, VFX_CONTRAST_MAGIC,
                                           VFX_CONTRAST_EMISSION);
    // The top of the value ramp. A luminous thing goes toward white where it is
    // hottest; keeping the peak at the material hue is exactly what made the
    // previous version read as a coloured drawing rather than as light.
    Color hotCol = VC_Whiten(glowCol, s_runeWhite);

    Vector3 u, v;
    Rune_PlaneBasis(normal, &u, &v);
    Vector3 n = Vector3Normalize(normal);
    double time = (double)TimeFX_Elapsed();
    float seed = 0.0f;

    // Built ONCE and submitted twice — the two passes draw the same surface.
    s_runeCells = Rune_BuildSurface(center, u, v, n, radius);

    for (int pass = 0; pass < 2; pass++)
    {
        bool emissive = (pass == 1);
        VFXRenderScope scope = VFXRender_BeginDraw(
            emissive ? VFX_RENDER_PASS_EMISSION : VFX_RENDER_PASS_BODY,
            // PREMULTIPLIED, not ADDITIVE, for the glow. Additive over a bright
            // destination can only add to something already at 1.0, which is
            // why the two other disc composers measure as invisible on a white
            // plate; premultiplied keeps the (1 - a) term that lets the ink
            // still bite. Paired with the u_params.w branch in the shader —
            // blend state and fragment formula are ONE decision.
            emissive ? VFX_SURFACE_PREMULTIPLIED : VFX_SURFACE_ALPHA, false);

        // Culling off so the circle is legible from below as well as above.
        // Flushed on BOTH sides: rlgl draws the queued geometry LATER and the
        // state at DRAW time is what applies (ENGINE_LANDMINES 1, and its
        // 30/07 postscript establishing that this covers culling too).
        rlDrawRenderBatchActive();
        rlDisableBackfaceCulling();
        rlDrawRenderBatchActive();

        SkillManager_BeginShader(s_runeFx.shader);
        Rune_SetUniforms(bodyInk, glowCol, hotCol,
                         fade * (emissive ? 1.0f : 0.94f),
                         open,
                         emissive ? (1.00f * s_runeGlow * elaborate) : 1.0f,
                         // COVERAGE GAIN, per pass. Premultiplied blending is
                         // (ONE, ONE_MINUS_SRC_ALPHA), so whatever coverage the
                         // emission pass declares BITES INTO the destination a
                         // second time on top of the body pass. On black that
                         // costs nothing; on bright scenery it is the only
                         // thing keeping a glowing ring from saturating into
                         // the background. Swept against the white plate:
                         //   0.30 -> darken 2.9%   absvar 17.7
                         //   0.55 -> darken 6.7%   absvar 20.6
                         //   0.80 -> darken 12.5%  absvar 23.9
                         //   1.00 -> darken 15.4%  absvar 26.6
                         // and the dark-plate figures did not move at any of
                         // them (cover 7.64 -> 7.46, detail 0.524 -> 0.523).
                         // A free axis, so it is spent in full.
                         1.0f,
                         emissive ? 1.0f : 0.0f,
                         time, seed);

        rlSetTexture(0);
        rlBegin(RL_QUADS);
        Rune_SubmitSurface(n, s_runeCells);
        rlEnd();
        rlDrawRenderBatchActive();
        SkillManager_EndShader();

        rlEnableBackfaceCulling();
        rlDrawRenderBatchActive();
        VFXRender_EndDraw(&scope);
    }
}
