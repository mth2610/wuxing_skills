// ── H2. VFX_ComposeGroundWave — geometry that follows the terrain ────────────
//
// An expanding ring of ground-conforming geometry: it rises, it has a lip, and
// its inner face is brighter than its outer one. That last part is what a flat
// additive decal can never do, and it is the whole reason this is geometry.
//
// WHAT THE PLAN ASKED FOR AND WHY THIS DOES NOT USE IT. VFX_PLAN §H2 names
// `ProceduralMesh_BuildWavePlane` as the unused primitive to put to work. It
// does not fit, and the mismatch is structural rather than a matter of taste:
// that builder makes a RECTANGULAR grid displaced by two or three travelling
// sine waves — a water surface — and it takes no height callback, so it cannot
// conform to anything. A shockwave is an ANNULUS whose radius is a function of
// time and whose vertices sit on the terrain. Nothing in the 53 entry points of
// `procedural_mesh_utils.h` builds that: `DrawCoreGroundPatch` is the only
// terrain-conforming one and it is a square patch drawn in a single flat colour,
// which rules it out for a shaded ring. So the ring is built here, with rlgl and
// static arrays, and the height callback the plan really meant —
// `GroundHeightSampleFn` — is used per vertex exactly as it describes.
//
// THE LIGHTING DECISION, WHICH IS A LANDMINE AND NOT A PREFERENCE. §H2 says the
// wave "catches the light on its inner face", which reads as an instruction to
// light it. It must not be lit: ENGINE_LANDMINES §3 — a lit material on ground
// geometry renders black-on-black in the night arena, and that is exactly this
// shape in exactly that place. The inner face is therefore brighter by AUTHORED
// VERTEX COLOUR (a self-shaded gradient), drawn additive and unlit per the blend
// law. The look the plan wanted, by the only route that survives the arena.
//
// Continuous / immediate-mode: it keeps no state and emits this frame's geometry,
// so it is called EVERY FRAME from a draw path with `t01` running 0 -> 1. Calling
// it once draws a single frame and looks like nothing happened
// (core/docs/LANDMINES.md, "A sequence called from Draw restarts every frame" —
// the same distinction, from the other side).

#include "core/tuning.h"

#define GROUND_WAVE_MAX_SLICES 64   // around the ring
// Azimuthal HEIGHT samples, deliberately far fewer than the slices and
// deliberately independent of the tier.
//
// HISTORICAL, and kept because the shape of the sampling is still right.
// `MapProp_SampleGroundHeight` used to be a real RAY-TRIANGLE TEST against
// every triangle of the terrain mesh. The first version of this file sampled it
// once per vertex — 65 slices x 7 radials = 455 raycasts EVERY FRAME — and the
// grass map dropped to 13 fps, which is what drove the budget below.
//
// That cost is GONE as of 25/08/2026: the query now goes through an XZ bin grid
// over the same triangles (232-292 us -> ~0.59 us per sample, ENGINE_LANDMINES.md
// 21), so 455 samples would today cost ~0.27 ms rather than most of a frame.
// The 24-azimuth budget is therefore no longer a performance ration — it stays
// because it is ENOUGH, for the reason below, and raising it would change the
// wave's silhouette for nothing. Raise it only if the shape needs it.
//
// The ground under a 5 m ring does not need 455 samples to be described. 24
// azimuths gives a 15-degree arc between samples, and the height in between is
// interpolated — which is EXACT for a planar slope and imperceptible on
// anything smooth. Across the band the same applies: sample the inner and outer
// edge and interpolate, so the cost is 2 x 24 = 48 raycasts, and it does not
// grow when the quality tier raises the slice count.
#define GROUND_WAVE_HEIGHT_AZIM 24
#define GROUND_WAVE_RADIALS    7    // across the band, inner base -> outer base
#define GROUND_WAVE_Y_LIFT     0.035f  // metres above the sampled ground

// Where the crest sits across the band. A shockwave's crest LEADS: it is pushed
// toward the outer edge, with the long slope trailing behind it on the inside.
// Dead centre reads as a symmetric doughnut, which is a ripple, not a blast.
//
// 2/3 EXACTLY, and the exactness is the point: the band is only
// GROUND_WAVE_RADIALS samples across, so its vertices land at 0, 1/6, 2/6 ... 1.
// A crest at 0.62 falls BETWEEN two of them, which means the geometry never
// builds the crest at all — the profile is evaluated at 0.5 and 0.667 and the
// peak between them is simply skipped. The curve would be right and the mesh
// would be wrong, which is the shape of bug that survives a screenshot. Placing
// the crest on a vertex costs nothing and also buys a stronger lead (area ratio
// 1.40 inner:outer, against 1.28 at 0.62).
#define GROUND_WAVE_CREST_U    0.6666667f

static bool  s_gwInit = false;
// Ground heights around the ring, sampled once per frame and interpolated from.
static float s_gwHIn[GROUND_WAVE_HEIGHT_AZIM + 1];
static float s_gwHOut[GROUND_WAVE_HEIGHT_AZIM + 1];
static float s_gwLip   = 1.0f;   // x on lip height
static float s_gwBand  = 1.0f;   // x on band width
static float s_gwAlpha = 1.0f;   // x on opacity
static float s_gwConform = 1.0f; // 0 = ignore heightFn entirely (flat, and free)
static float s_gwPerfLog = 0.0f; // 1 = report the height-sampling cost, 1 Hz

typedef struct
{
    Shader shader;
    int bodyColor;
    int glowColor;
    int opacity;
    int surfaceIntensity;
} GroundWaveShader;

static GroundWaveShader s_gwShader = {0};

static bool GroundWave_HasShader(void)
{
    return s_gwShader.shader.id != 0 && s_gwShader.bodyColor >= 0 &&
           s_gwShader.glowColor >= 0 && s_gwShader.opacity >= 0 &&
           s_gwShader.surfaceIntensity >= 0;
}

static void GroundWave_InitShader(void)
{
    if (s_gwShader.shader.id != 0) return;
    s_gwShader.shader = ResourceManager_LoadShader("core/shaders/ground_wave.vs",
                                                    "core/shaders/ground_wave.fs");
    if (s_gwShader.shader.id == 0) return;
    s_gwShader.bodyColor = GetShaderLocation(s_gwShader.shader, "u_bodyColor");
    s_gwShader.glowColor = GetShaderLocation(s_gwShader.shader, "u_glowColor");
    s_gwShader.opacity = GetShaderLocation(s_gwShader.shader, "u_opacity");
    s_gwShader.surfaceIntensity = GetShaderLocation(s_gwShader.shader,
                                                     "u_surfaceIntensity");
}

static void GroundWave_SetSurfaceUniforms(const VFX_ElementMaterial *mat,
                                          float opacity, float intensity)
{
    Vector4 body = ColorNormalize(mat->body);
    Vector4 glow = ColorNormalize(VC_Whiten(mat->glow, 0.30f));
    if (s_gwShader.bodyColor >= 0)
        SetShaderValue(s_gwShader.shader, s_gwShader.bodyColor, &body, SHADER_UNIFORM_VEC4);
    if (s_gwShader.glowColor >= 0)
        SetShaderValue(s_gwShader.shader, s_gwShader.glowColor, &glow, SHADER_UNIFORM_VEC4);
    if (s_gwShader.opacity >= 0)
        SetShaderValue(s_gwShader.shader, s_gwShader.opacity, &opacity, SHADER_UNIFORM_FLOAT);
    if (s_gwShader.surfaceIntensity >= 0)
        SetShaderValue(s_gwShader.shader, s_gwShader.surfaceIntensity,
                       &intensity, SHADER_UNIFORM_FLOAT);
}

static void GroundWave_InitShared(void)
{
    if (s_gwInit) return;
    // Lazily, never from a subsystem Init (core/docs/LANDMINES.md).
    Tuning_RegisterFloat("ground_wave_lip",   &s_gwLip,   1.0f);
    Tuning_RegisterFloat("ground_wave_band",  &s_gwBand,  1.0f);
    Tuning_RegisterFloat("ground_wave_alpha", &s_gwAlpha, 1.0f);
    // Both of these exist because "is the terrain sampler what costs me the
    // frame" must be answerable by looking, not by a rebuild (core/CLAUDE.md §5).
    Tuning_RegisterFloat("ground_wave_conform",  &s_gwConform, 1.0f);
    Tuning_RegisterFloat("ground_wave_perf_log", &s_gwPerfLog, 0.0f);
    s_gwInit = true;
}

// ── The arithmetic, factored out so core/tests/ground_wave_test.c mirrors it ──

// Radius over the wave's life. Ease-OUT: a blast front is fastest at the instant
// it is released and decelerates, so a linear expansion reads as a growing
// circle rather than as something that was thrown.
static float GroundWave_Radius01(float t01)
{
    if (t01 <= 0.0f) return 0.0f;
    if (t01 >= 1.0f) return 1.0f;
    return 1.0f - powf(1.0f - t01, 2.2f);
}

// Band width IN METRES, keyed to the ring's CURRENT radius. A shockwave spreads
// as it travels; a band of fixed width would read as a hoop of constant section
// sliding outward. Ratio picked so the band is about a fifth of the radius.
static float GroundWave_BandWidth(float radiusNow)
{
    return radiusNow * 0.22f * s_gwBand;
}

// Lip height, as a ratio against the band's OWN width — the thickness rule
// (core/docs/LANDMINES.md, "Thickness is a ratio against the thing's OWN
// length"). Keyed to the radius instead, the wave would grow a taller and taller
// wall as it expanded; keyed to a constant it would flatten into a decal. The
// envelope on top of that is the wave's own life: a fast rise off the ground and
// a long settle, reaching zero exactly at t01 = 1.
static float GroundWave_LipHeight(float bandWidth, float t01)
{
    if (t01 <= 0.0f || t01 >= 1.0f) return 0.0f;
    float rise   = SmoothStep01(t01 / 0.14f);        // out of the ground, fast
    float settle = powf(1.0f - t01, 1.3f);           // and back down, slowly
    return bandWidth * 0.35f * rise * settle * s_gwLip;
}

// Cross-band profile: 0 at both bases, 1 at the crest, with the crest at
// GROUND_WAVE_CREST_U rather than the middle. `sin(PI * u^k)` peaks where
// u^k = 0.5, so k = ln(0.5)/ln(crestU) puts the peak exactly there — one
// expression, no piecewise seam to go discontinuous at the join.
static float GroundWave_Profile(float u)
{
    if (u <= 0.0f || u >= 1.0f) return 0.0f;
    const float k = 1.7095f;   // ln(0.5)/ln(2/3)
    // Exponent 1.0, not 1.3: at 1.3 the crest is narrow enough that only the
    // couple of quads either side of it clear the visibility threshold, and the
    // whole band reads as a thin LINE rather than as a raised lip (owner's
    // capture, 29/07). A broader crest is also closer to what a shockwave's
    // section actually looks like.
    return sinf(PI * powf(u, k));
}

// The self-shading that replaces real lighting. Height carries most of it (a
// crest catches more than a trough), and the INNER face is brighter than the
// outer one at the same height — that asymmetry is the whole "lit from the blast
// centre" read, and it is the thing a flat decal cannot fake.
static float GroundWave_Shade(float u)
{
    float h = GroundWave_Profile(u);
    // The face factor RAMPS across the outer slope; it is not a step at the
    // crest. As a step it multiplied the crest itself by 0.62 — the brightest
    // point of the band was being dimmed by the rule meant to darken the face
    // BEHIND it, so the crest never reached full brightness and the band lost
    // most of its contrast. `u <= crest` keeps the crest vertex on the bright
    // side of the comparison, which matters because the crest sits exactly ON a
    // vertex (see GROUND_WAVE_CREST_U).
    float face = (u <= GROUND_WAVE_CREST_U)
                     ? 1.0f
                     : Math_Mix(1.0f, 0.60f,
                                (u - GROUND_WAVE_CREST_U) / (1.0f - GROUND_WAVE_CREST_U));
    // Floor 0.30, not 0.18: below about a quarter the band's slopes fall under
    // what an additive draw can show against the arena and only the crest
    // survives, which is the "faint thin line" failure.
    return Math_Mix(0.30f, 1.0f, h) * face;
}

// Alpha over the life: in fast, out slow, zero at both ends.
static float GroundWave_Alpha01(float t01)
{
    if (t01 <= 0.0f || t01 >= 1.0f) return 0.0f;
    float in  = SmoothStep01(t01 / 0.10f);
    // 1.1, not 1.6: at 1.6 the wave is already down to a third of its brightness
    // by the halfway point, so most of the time it is on screen it is too faint
    // to read as geometry at all.
    float out = powf(1.0f - t01, 1.1f);
    return in * out;
}

// Slice count. E8 tier budget: this is the only thing here that scales with
// fill, and the gate may only ever clamp DOWN.
static int GroundWave_Slices(void)
{
    switch (GfxQuality_Get()) {
    case GFX_HIGH: return 64;
    case GFX_MED:  return 40;
    default:       return 24;
    }
}

// ── The composition ──────────────────────────────────────────────────────────

// The default terrain sampler: the active map's own ground height. Pass this as
// `heightFn` whenever the wave should follow the map the fight is on — without
// it the ring is FLAT at `center.y`, which coincides with the ground on level
// terrain and floats or sinks on a slope. That is not a subtle failure and it
// is exactly what a NULL callback looks like (owner's report, 29/07: "on the
// grass map it works on the flat parts but not on the steep ones").
// Maps without a height hook return 0 here, which is the flat case anyway.
float VFX_GroundHeightFromMap(float worldX, float worldZ, void *unused)
{
    (void)unused;
    return MapManager_GetGroundHeightAt(worldX, worldZ);
}

bool VFX_GroundSurfaceFromMap(float worldX, float worldZ, Vector3 *outPosition,
                              Vector3 *outNormal, void *unused)
{
    (void)unused;
    return MapManager_SampleGroundSurfaceAt(worldX, worldZ, outPosition, outNormal);
}

// `radius` = the radius the front reaches at t01 = 1, in metres. `heightFn` may
// be NULL, in which case the ring is flat at `center.y` — the callback is what
// makes it follow a slope instead of clipping through it, and the map layer
// supplies it (`GroundHeightSampleFn`, procedural_mesh_utils.h:24).
void VFX_ComposeGroundWave(Vector3 center, VC_MaterialId mat, float radius,
                           float t01, GroundHeightSampleFn heightFn, void *ud)
{
    GroundWave_InitShared();
    GroundWave_InitShader();
    if (radius <= 0.0f) radius = 4.0f;
    if (t01 <= 0.0f || t01 >= 1.0f) return;

    float alpha = GroundWave_Alpha01(t01) * s_gwAlpha;
    if (alpha <= 0.004f) return;

    float rNow  = radius * GroundWave_Radius01(t01);
    float band  = GroundWave_BandWidth(rNow);
    float lip   = GroundWave_LipHeight(band, t01);
    if (band <= 0.001f) return;

    const VFX_ElementMaterial *m = VFX_Material(mat);
    const int slices  = GroundWave_Slices();
    const int radials = GROUND_WAVE_RADIALS;

    // The dedicated geometry builder owns position, normal, terrain sampling
    // and UV topology. It is static and bounded: no allocation on a cast.
    static ShockwaveMeshData mesh;
    static Color   ringCol[GROUND_WAVE_RADIALS];

    // Colour and shading are the same for every slice, so they are computed once
    // across the band rather than per vertex.
    for (int i = 0; i < radials; i++) {
        float u = (float)i / (float)(radials - 1);
        float h = GroundWave_Profile(u);
        // Body at the base, glow at the crest: the hot line is where the
        // material is thinnest and moving fastest.
        Color c = VC_MixColor(m->body, m->glow, h);
        float s = GroundWave_Shade(u);
        ringCol[i] = VC_WithAlpha(c, (unsigned char)(alpha * s * 255.0f));
    }

    ShockwaveMeshConfig meshCfg = ProceduralMesh_DefaultShockwaveConfig();
    meshCfg.radius = rNow;
    meshCfg.bandWidth = band;
    meshCfg.lipHeight = lip;
    meshCfg.crestU = GROUND_WAVE_CREST_U;
    // Shape, not particles: only the silhouette and crest receive this low
    // frequency irregularity. The flow/shader pass will animate smaller detail.
    meshCfg.radialJitter = band * 0.20f;
    meshCfg.lipJitter = lip * 0.18f;
    meshCfg.angularLobes = 5;
    meshCfg.angularPhase = t01 * 1.20f;
    meshCfg.yLift = GROUND_WAVE_Y_LIFT;
    if (s_gwConform < 0.5f) heightFn = NULL;
    ProceduralMesh_BuildShockwave(&mesh, center, &meshCfg, slices, radials - 1,
                                  heightFn, ud);

    // The coloured pressure front belongs in BODY; otherwise an additive pass
    // loses its green hue against a bright floor. A restrained second draw in
    // EMISSION gives the crest bloom without replacing the visible material.
    VFXRenderScope bodyScope = VFXRender_BeginDraw(
        VFX_RENDER_PASS_BODY, VFX_SURFACE_ALPHA, false);
    if (GroundWave_HasShader()) {
        SkillManager_BeginShader(s_gwShader.shader);
        GroundWave_SetSurfaceUniforms(m, alpha * 0.78f, 1.0f);
        ProceduralMesh_DrawShockwave(&mesh, NULL);
        rlDrawRenderBatchActive();
        SkillManager_EndShader();
    } else {
        ProceduralMesh_DrawShockwave(&mesh, ringCol);
    }
    VFXRender_EndDraw(&bodyScope);

    VFXRenderScope emissionScope = VFXRender_BeginDraw(
        VFX_RENDER_PASS_EMISSION, VFX_SURFACE_ADDITIVE, false);
    if (GroundWave_HasShader()) {
        SkillManager_BeginShader(s_gwShader.shader);
        GroundWave_SetSurfaceUniforms(m, alpha * 0.48f, 1.35f);
        ProceduralMesh_DrawShockwave(&mesh, NULL);
        rlDrawRenderBatchActive();
        SkillManager_EndShader();
    } else {
        ProceduralMesh_DrawShockwave(&mesh, ringCol);
    }
    rlSetTexture(0);
    VFXRender_EndDraw(&emissionScope);
}
