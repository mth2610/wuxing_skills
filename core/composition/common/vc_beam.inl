// ── P4. VFX_ComposeBeam — the sustained line ─────────────────────────────────
//
// Nothing in the library drew a HELD beam. `DrawRibbonEnergyField` had one
// consumer and `core/vfx_proc_ray.h` had none, and both of those are strips: a
// beam made of camera-facing cards is the same shape from every angle and
// vanishes when you look along it, which is exactly when a beam pointed at you
// should be at its most emphatic.
//
// So it is a TUBE, and it is the SAME tube P1 established — the one
// `ProceduralMesh_BuildTubeAlongPath` builds and `core/trail_system.c` sweeps.
// There is one tube in this tree. What differs is where the path comes from: a
// volume trail's path is HISTORY (where an emitter has been), a beam's is a
// straight line between two points that both exist right now. That is the whole
// reason this is not `VFX_ComposeVolumeTrail` with a different argument — there
// is no emitter and nothing to remember, so there is no pool and no handle.
//
// IMMEDIATE MODE: call every frame the beam is held, with `t01` running over its
// life. Called once it draws a single frame and looks like nothing happened
// (core/docs/LANDMINES.md, "A sequence called from Draw restarts every frame" —
// the same line, read the other way).
//
// THE TWO CASES THE DoD NAMES, and how each is handled:
//
//   NEARLY COINCIDENT `from` and `to`. Normalising a near-zero vector returns
//   garbage silently and collapses geometry — the 30/07 landmine that cost four
//   rounds on the tube. Below BEAM_MIN_LEN there is nothing to draw and it
//   returns, announced once. Above it the ASPECT LAW does the rest: the drawn
//   radius is capped against the beam's own length, so a beam that is 20 cm long
//   is thin rather than a ball, and the fire-up therefore extends as a lance
//   instead of inflating.
//
//   VIEWED END-ON. A tube holds a silhouette from every angle and
//   `ProceduralMesh_DrawTube` closes both ends with apex caps, so end-on you see
//   a disc rather than nothing. Drawn additively with the far wall showing
//   through the near one, the whole length accumulates into that disc — a beam
//   aimed at the camera is a bright point, which is correct.

#define BEAM_SEGMENTS 24
#define BEAM_RADIAL 8
// Shorter than this and there is no beam: `to - from` cannot be normalised to
// anything meaningful, and every frame built on it is garbage geometry.
#define BEAM_MIN_LEN 0.02f
// THE ASPECT LAW (core/docs/LANDMINES.md, "Thickness is a ratio against the
// thing's OWN length"). Half-width per metre of beam. 0.05 is full width :
// length = 1:10 — a beam is a LANCE, an order of magnitude thinner than the
// volume trail's 1:2.5 wake, and that ratio is most of what separates a beam
// from a tube of gas.
#define BEAM_ASPECT_K 0.05f
// The builder's own uniform-profile constant. `baseCapsule` is
// `0.3f + 0.7f * sqrtf(sin(t*PI)) * capsuleTailExp` (core/geometry/pm_tube.inl),
// so with capsuleTailExp = 0 the profile is a flat 0.3 and the radius asked for
// has to be divided by it. Mirrored in core/tests/beam_test.c against that line,
// because a silent change to the builder's floor would silently rescale every
// beam in the game.
#define BEAM_PROFILE_BASE 0.3f
// A beam flares slightly toward the far end. Beams widen where they land; a
// perfectly parallel one reads as a pipe.
#define BEAM_HEAD_GROWTH 0.12f
// Lateral wander, as a multiple of the beam's own RADIUS — not of its length. A
// wander keyed to length turns a long beam into a snake; keyed to radius it is
// the shaft breathing, at any range.
#define BEAM_WANDER 1.15f
#define BEAM_WANDER_SPEED 5.5f
// Fire-up and cut-out, as fractions of t01. The beam EXTENDS from `from` during
// the opening — a beam that simply appears at full length has no source.
#define BEAM_OPEN 0.14f
#define BEAM_CLOSE 0.86f
// Metres per texture repeat, and tiles/sec along the shaft. POSITIVE here, and
// that is the one place this differs from every trail in the tree: a trail's
// flow runs AGAINST travel because it is being left behind, but a beam is being
// PUSHED from its source, so its material runs from `from` toward `to`.
#define BEAM_TILE 1.15f
#define BEAM_FLOW 3.20f
// Surface deform, fraction of the local radius. Lower than a volume trail's:
// a beam is coherent energy and a billowing one reads as a jet of gas.
#define BEAM_NOISE 0.11f

// THE LAYER STACK, and the ALPHA BUDGET, which is arithmetic and not taste.
// These are concentric and additive, so the frame buffer sees their SUM:
// 0.12 + 0.38 + 0.26 = 0.76, under 1.0 through the body so the sheet's structure
// survives. The effective ceiling is below 1.0 anyway, because E1's streak bloom
// lifts anything near the threshold (core/docs/LANDMINES.md, "Overlapping
// additive layers clip"). The HOT CORE is narrow and whitened rather than
// brighter: a saturated hue stacks additively into more of itself and never
// reaches white, so the white has to be put in at the source.
#define BEAM_L0_WIDTH 1.85f
#define BEAM_L0_ALPHA 0.12f
#define BEAM_L1_WIDTH 1.00f
#define BEAM_L1_ALPHA 0.38f
#define BEAM_L2_WIDTH 0.34f
#define BEAM_L2_ALPHA 0.26f
#define BEAM_L2_WHITEN 0.72f

static bool s_beamInit = false;
static bool s_beamDegenerateLogged = false;
static Texture2D s_beamSheet = {0};

static float s_beamWidthMul = 1.0f;
static float s_beamAlphaMul = 1.0f;
static float s_beamWanderMul = 1.0f;
static float s_beamFlowMul = 1.0f;

static void Beam_InitShared(void)
{
    if (s_beamInit)
        return;
    // The same seamless-on-both-axes volume sheet P1 uses. ResourceManager
    // caches by path, so this is the same GPU texture, not a second copy.
    s_beamSheet = ResourceManager_LoadTexture("assets/textures/energy_volume.png");
    if (s_beamSheet.id != 0)
    {
        SetTextureFilter(s_beamSheet, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(s_beamSheet, TEXTURE_WRAP_REPEAT);
    }
    else
    {
        TraceLog(LOG_WARNING,
                 "VFX_BEAM: energy_volume.png missing — the shaft draws bare");
    }
    // Lazily, never from a subsystem Init (core/docs/LANDMINES.md).
    Tuning_RegisterFloat("beam_width", &s_beamWidthMul, 1.0f);
    Tuning_RegisterFloat("beam_alpha", &s_beamAlphaMul, 1.0f);
    Tuning_RegisterFloat("beam_wander", &s_beamWanderMul, 1.0f);
    Tuning_RegisterFloat("beam_flow", &s_beamFlowMul, 1.0f);
    s_beamInit = true;
}

// ── The arithmetic, factored out so core/tests/beam_test.c can mirror it ────

// How far the head has reached, and how strongly the beam is drawn, over t01.
// FIRE-UP is a reach; CUT-OUT is an envelope. They are different because a beam
// does not retract into its source — it stops being fed and goes out.
static float Beam_Reach(float t01)
{
    if (t01 >= BEAM_OPEN)
        return 1.0f;
    return (t01 <= 0.0f) ? 0.0f : (t01 / BEAM_OPEN);
}

static float Beam_Envelope(float t01)
{
    if (t01 <= 0.0f || t01 >= 1.0f)
        return 0.0f;
    if (t01 < BEAM_OPEN)
        return t01 / BEAM_OPEN;
    if (t01 > BEAM_CLOSE)
        return (1.0f - t01) / (1.0f - BEAM_CLOSE);
    return 1.0f;
}

// The caller's width is a CEILING. `len` is the length actually being drawn,
// which during the fire-up is a fraction of the full span — so an extending beam
// is a thin lance that lengthens, not a ball that stretches.
static float Beam_Radius(float widthMetres, float len, float env)
{
    float want = widthMetres * 0.5f * env * s_beamWidthMul;
    float cap = len * BEAM_ASPECT_K;
    if (want < 0.0f)
        want = 0.0f;
    return (cap < want) ? cap : want;
}

// A tier ladder that clamps DOWN and leaves it a tube at every tier. A beam that
// degrades to a strip is a different effect on a low machine, not a cheaper one.
static int Beam_Radial(void)
{
    switch (GfxQuality_Get())
    {
    case GFX_HIGH: return BEAM_RADIAL;
    case GFX_MED:  return BEAM_RADIAL;
    case GFX_LOW:  return 6;
    default:       return 5;
    }
}
static int Beam_Segments(void)
{
    switch (GfxQuality_Get())
    {
    case GFX_HIGH: return BEAM_SEGMENTS;
    case GFX_MED:  return 18;
    case GFX_LOW:  return 12;
    default:       return 8;
    }
}
// The outer bloom is the first layer to go: it is the widest fill and the one
// with no structure to lose. The CORE is never dropped — it is the beam.
static int Beam_LayerCount(void)
{
    return (GfxQuality_Get() >= GFX_MED) ? 3 : 2;
}

// ── Public API ──────────────────────────────────────────────────────────────

// `from`/`to` in metres. `width` is the shaft's FULL width at its widest, a
// ceiling: the drawn width is also capped against the beam's own length (1:10).
// `t01` 0→1 over the beam's life — it fires up over the first 14%, holds, and
// cuts out over the last 14%.
void VFX_ComposeBeam(Vector3 from, Vector3 to, VC_MaterialId mat,
                     float width, float t01)
{
    Beam_InitShared();
    if (t01 <= 0.0f || t01 >= 1.0f)
        return;
    if (width <= 0.0f)
        width = 0.18f;

    Vector3 span = Vector3Subtract(to, from);
    float spanLenSq = Vector3LengthSqr(span);
    // THE DEGENERATE CASE, checked on the SQUARED length BEFORE anything is
    // normalised. Normalising ~zero returns garbage rather than failing, and the
    // geometry built on it collapses silently — no NaN, no log, just a wrong
    // shape that looks deliberate (core/docs/LANDMINES.md, 30/07).
    if (spanLenSq < BEAM_MIN_LEN * BEAM_MIN_LEN)
    {
        if (!s_beamDegenerateLogged)
        {
            TraceLog(LOG_WARNING,
                     "VFX_BEAM: from and to are %.4f m apart (minimum %.3f m) — "
                     "nothing drawn. A zero-length beam has no direction.",
                     sqrtf(spanLenSq), BEAM_MIN_LEN);
            s_beamDegenerateLogged = true;
        }
        return;
    }
    float spanLen = sqrtf(spanLenSq);
    Vector3 axis = Vector3Scale(span, 1.0f / spanLen);

    float env = Beam_Envelope(t01);
    float reach = Beam_Reach(t01);
    float drawLen = spanLen * reach;
    if (env <= 0.001f || drawLen < BEAM_MIN_LEN)
        return;

    float radius = Beam_Radius(width, drawLen, env);
    if (radius <= 1e-4f)
        return;

    // The cross-section frame. `axis` is already unit length, so the only failure
    // left is picking a reference PARALLEL to it — the cross product would then
    // be ~zero and normalising it returns garbage, the same landmine one level
    // down. `VC_PlaneFrame` (vc_common.inl) carries that guard for every caller
    // that needs a plane out of a direction; P5 and P6 use the same one.
    Vector3 sideA, sideB;
    VC_PlaneFrame(axis, &sideA, &sideB);

    // THE PATH. A straight line with a lateral wander that is ZERO at both ends,
    // so the beam meets its source and its target exactly. Two frequencies, out
    // of phase and irrationally related, so the shaft never repeats a pose — one
    // sine is a plucked string, which reads as machinery.
    float time = (float)GetTime();
    float wander = radius * BEAM_WANDER * s_beamWanderMul;
    static Vector3 path[BEAM_SEGMENTS + 1];
    int segs = Beam_Segments();
    if (segs > BEAM_SEGMENTS)
        segs = BEAM_SEGMENTS;
    for (int i = 0; i <= segs; i++)
    {
        float t = (float)i / (float)segs;
        Vector3 p = Vector3Add(from, Vector3Scale(axis, drawLen * t));
        // sin(PI*t) pins the deviation to zero at t = 0 and t = 1.
        float hold = sinf(t * PI);
        float a = sinf(t * 7.3f + time * BEAM_WANDER_SPEED);
        float b = sinf(t * 4.1f - time * BEAM_WANDER_SPEED * 0.77f + 1.7f);
        p = Vector3Add(p, Vector3Scale(sideA, hold * wander * a));
        p = Vector3Add(p, Vector3Scale(sideB, hold * wander * b));
        path[i] = p;
    }

    TubeMeshConfig cfg = ProceduralMesh_DefaultTubeConfig();
    // A BEAM IS NOT A DROPLET. The default profile is the water stream's capsule
    // — a teardrop that comes to a point at the tail — and a beam tapered like
    // that reads as a spear thrown, not as energy being sustained from a source.
    // capsuleTailExp 0 flattens it to the builder's 0.3 floor; the taper is
    // switched off; only the slight flare toward the far end is kept.
    cfg.capsuleTailExp = 0.0f;
    cfg.tailTaperMin = 1.0f;
    cfg.tailTaperMax = 1.0f;
    cfg.headGrowth = BEAM_HEAD_GROWTH;
    cfg.wobbleAmplitude = 0.0f; // the wander is in the PATH, not in the frame roll
    // The two sine deform layers stay off and the noise one replaces them: sines
    // are periodic, so the surface ripples on a beat the eye reads as machinery.
    cfg.deform1Amp = 0.0f;
    cfg.deform2Amp = 0.0f;
    cfg.noiseAmp = BEAM_NOISE;
    cfg.noiseScale = 5.0f;
    cfg.noiseSpeed = 1.6f;
    // The field travels ALONG the shaft on the same clock the sheet scrolls on,
    // so the bulges and the texture move together — otherwise the noise is
    // pinned to the tube's own parameterisation and reads as a pre-squeezed
    // shape being dragged.
    cfg.noiseOffset = time * BEAM_FLOW * s_beamFlowMul * 0.5f;
    // The path CURVES (that is the wander), so the cross-section frame has to be
    // parallel-transported or the roll — and with it the UV — shears along the
    // length (core/geometry/pm_tube.inl).
    cfg.useTransportFrame = true;
    // No end caps in the config; ProceduralMesh_DrawTube closes both ends with
    // apex caps, which is what stops the end-on view being a hole.

    static TubeMeshData mesh;
    const VFX_ElementMaterial *m = VFX_Material(mat);
    float alpha = Clamp(s_beamAlphaMul, 0.0f, 2.0f);
    float tiles = drawLen / BEAM_TILE;
    if (tiles < 0.5f)
        tiles = 0.5f;
    float scroll = time * BEAM_FLOW * s_beamFlowMul;

    const float layerW[3] = {BEAM_L0_WIDTH, BEAM_L1_WIDTH, BEAM_L2_WIDTH};
    const float layerA[3] = {BEAM_L0_ALPHA, BEAM_L1_ALPHA, BEAM_L2_ALPHA};
    const float layerWhite[3] = {0.0f, 0.10f, BEAM_L2_WHITEN};
    int layers = Beam_LayerCount();

    // FLUSHED ON BOTH SIDES OF EVERY STATE CHANGE THE QUEUED GEOMETRY DEPENDS
    // ON — blend mode, depth mask AND culling (ENGINE_LANDMINES rule 1 and its
    // 30/07 postscript). rlgl draws the batch LATER, so a tube submitted with
    // culling off and drawn after it was turned back on loses exactly one wall
    // of every ring: a beam that renders as half a shell.
    //
    // The three states, and why each is what it is:
    //   depth mask OFF — it EMITS, so it must not occlude what is behind it.
    //   culling    OFF — the far wall showing through the near one is what gives
    //                    a tube its rim for free: grazing angles cross more
    //                    material, so the silhouette brightens with no fresnel.
    //   ADDITIVE       — the blend law, the other half of the same sentence.
    ScreenDistort_BeginVFXEmission();
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    BeginBlendMode(BLEND_ADDITIVE);
    rlDrawRenderBatchActive();

    for (int L = 0; L < layers; L++)
    {
        // The layer INDEX chosen from the top down, so a lower tier drops the
        // outer bloom rather than the core: the core IS the beam.
        int idx = (layers == 3) ? L : L + 1;
        float r = radius * layerW[idx];
        ProceduralMesh_BuildTubeAlongPath(&mesh, path, segs + 1,
                                          r / BEAM_PROFILE_BASE,
                                          0.0f, 1.0f, time,
                                          segs, Beam_Radial(), &cfg);
        Color col = VC_Whiten(m->glow, layerWhite[idx]);
        float a = 255.0f * layerA[idx] * env * alpha;
        // ONLY THE BODY CARRIES THE SHEET. Several additive copies of one
        // pattern at different scroll phases average into something FLAT, and
        // the wider layer throws the sheet's edge detail outward as spikes. The
        // bloom and the core are lit SHAPES.
        rlSetTexture((idx == 1 && s_beamSheet.id != 0) ? s_beamSheet.id : 0);
        rlColor4ub(col.r, col.g, col.b, (unsigned char)Clamp(a, 0.0f, 255.0f));
        // Per-layer scroll rates: layers moving at one rate read as a single
        // thick surface however many there are, and the PARALLAX between them is
        // most of what sells a shaft as flowing rather than sliding.
        ProceduralMesh_DrawTubeEx(&mesh, tiles, scroll * (0.6f + 0.5f * (float)idx));
    }

    rlSetTexture(0); // must not leak the binding into whatever draws next
    rlColor4ub(255, 255, 255, 255);
    rlDrawRenderBatchActive();
    EndBlendMode();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    rlDrawRenderBatchActive();
    ScreenDistort_EndVFXLayer();
}
