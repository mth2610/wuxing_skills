// ── P2. VFX_ComposeConvergeMotes — the one visual idea ChargeConverge owned ──
//
// EXTRACTED FROM `VFX_ComposeChargeConverge`, 30/07/2026, VERBATIM. The composite
// had already given up its destination when `VFX_ComposeCoreGlow` was pulled out;
// this is the other half — motes peeling off the surface of a shell and being
// drawn in along curved threads — and with it gone `VFX_ComposeChargeConverge`
// is a pure score over primaries with no visual idea of its own.
//
// WHY EXTRACT AT ALL. A converge is not a charge. A summon draws motes into a
// rune, a drain pulls them off a victim, an absorb takes them into a weapon —
// every one of those wants this and none of them wants a hot core at the middle.
// While it lived inside the composite the only way to have it was to have all of
// it (VFX_PLAN §4.1).
//
// AND WHY IT IS THE CHEAP KIND OF PRIMARY. A primary extracted from an approved
// composite costs no visual iteration: the look is already signed off, so the
// only acceptance criterion is that nothing drifted in transit. That is what
// core/tests/converge_motes_test.c is for, and it is why most of that file is a
// transcription check rather than an argument.
//
// THE TWO THINGS THAT MAKE IT LOOK LIKE QI, both carried over unchanged:
//
//   SHAPE COMES FROM GEOMETRY, NOT FROM A FORMULA. Owner's direction: *"nó chỉ
//   nên đơn giản, nên phát ra các trail ở bề mặt 1 hình cầu, sau đó tụ lại, quan
//   trọng tạo được chuyển động mềm mại của chân khí."* So the emitter is the
//   engine's MESH emitter — `SpawnParticleOnMesh` sampling a real sphere mesh's
//   edges (`MeshAdjacency`) — not a parametric ring. A closed-form curve gives
//   texture but no outline.
//
//   MOTION COMES FROM FORCES, NOT FROM A PARAMETRISED PATH. An earlier pass drove
//   each mote along an analytic spiral and it read as machinery: every thread
//   swept the identical arc at the identical rate. Here a thread is released with
//   a tangential push and then falls under a point attractor with drag, so it
//   accelerates as it closes in and no two paths are alike — which is what "mềm
//   mại" actually is. `VC_MotionSpiralIn` stays available for callers that need a
//   deterministic path.
//
// BLEND LAW: qi EMITS, so additive + unlit = 1, and the white-hot look comes from
// render.emissiveBoost (unlit only). Threads are trail-only particles: no head
// billboard, just the smoothed ribbon — a thread of gas rather than a comet.

#define CONVERGE_MAX_FIELDS 4

static SkillCurve s_chargeFade = {0}; // alpha over a thread's life
static SkillCurve s_chargeEmis = {0}; // brightens as it falls inward
static bool       s_chargeInit = false;

// The emitter mesh: a unit sphere, built once. SpawnParticleOnMesh samples a
// random point along a random EDGE of it, so threads leave from all over the
// surface rather than from a band.
static MeshAdjacency s_chargeSphere;
static bool          s_chargeSphereBuilt = false;

// Live knobs — a cast tell is judged by eye (core/CLAUDE.md §5). The KEYS are
// unchanged by the extraction on purpose: a tuning.cfg written against the
// composite must keep working, and renaming a dial is a silent behaviour change
// for anyone who had set it.
static float s_chargeRate  = 1.0f;   // x on threads/sec
static float s_chargeSize  = 1.0f;   // x on thread thickness
static float s_chargePull  = 1.0f;   // x on the attractor (how hard it sucks)
static float s_chargeSwirl = 1.0f;   // x on the tangential launch push
// How white a thread is. Owner's call: threads are drawn near-white so a caller
// can TINT them per element, instead of each material baking its own hue into
// the geometry. 1 = pure white, 0 = the raw element glow.
static float s_chargeWhite = 0.88f;

// `charge_size` also scales the destination glow in `VFX_ComposeChargeConverge`,
// which is a different file, so the dial has to be readable from there.
//
// NOT static, and not read directly either, and both of those are deliberate:
//   - A direct read of `s_chargeSize` from the composite would COMPILE — every
//     .inl is pasted into one translation unit — and would silently depend on
//     include ORDER, which scripts/sync_vfx_test.py owns and which appends new
//     files at the end.
//   - A `static` forward declaration in the composite would fix the order
//     problem and trip core/tests/composition_tu_test.c, which flags any
//     file-scope `static` name appearing in two files in the TU. That guard is
//     right — file-scope statics here are effectively global — and it cannot
//     tell a second declaration of one symbol from two different symbols.
// So it is an ordinary function with its prototype in visual_composer.h.
float VC_ConvergeMotesSizeMul(void)
{
    return s_chargeSize;
}

// One force field per concurrent converge. The field carries the attractor's
// ORIGIN and a particle holds a pointer to it for its whole life, so a single
// shared field would yank every thread of every converge toward whichever centre
// was written last. Slots are matched by position, recycled round-robin.
static ForceField s_chargeFields[CONVERGE_MAX_FIELDS];
static Vector3    s_chargeFieldPos[CONVERGE_MAX_FIELDS];
static bool       s_chargeFieldUsed[CONVERGE_MAX_FIELDS] = {0};
static int        s_chargeFieldNext = 0;

static void ConvergeMotes_InitShared(void)
{
    if (s_chargeInit) return;

    // Fade in fast, hold, fade out — a thread should be visible by the time it
    // has left the surface.
    FloatCurve_AddStop(&s_chargeFade, 0.0f, 0.0f);
    FloatCurve_AddStop(&s_chargeFade, 0.10f, 1.0f);
    FloatCurve_AddStop(&s_chargeFade, 0.72f, 0.9f);
    FloatCurve_AddStop(&s_chargeFade, 1.0f, 0.0f);

    // "Brightness ramping on an emissiveCurve": hotter the closer it gets, so the
    // eye is pulled inward along the thread.
    FloatCurve_AddStop(&s_chargeEmis, 0.0f, 0.7f);
    FloatCurve_AddStop(&s_chargeEmis, 1.0f, 2.0f);

    Tuning_RegisterFloat("charge_rate",  &s_chargeRate,  1.0f);
    Tuning_RegisterFloat("charge_size",  &s_chargeSize,  1.0f);
    Tuning_RegisterFloat("charge_pull",  &s_chargePull,  1.0f);
    Tuning_RegisterFloat("charge_swirl", &s_chargeSwirl, 1.0f);
    Tuning_RegisterFloat("charge_white", &s_chargeWhite, 0.88f);

    s_chargeInit = true;
}

static const MeshAdjacency *ConvergeMotes_Sphere(void)
{
    if (!s_chargeSphereBuilt)
    {
        // Unit sphere; the caller's radius is applied through the spawn matrix,
        // so one mesh serves every scale. Rings/slices kept modest — the edge
        // set only has to be dense enough that launch points look scattered.
        Mesh m = GenMeshSphere(1.0f, 10, 16);
        MeshAdjacency_Build(&s_chargeSphere, m);
        UnloadMesh(m);
        s_chargeSphereBuilt = true;
    }
    return &s_chargeSphere;
}

// Point attractor + vortex + drag for one converge centre.
static const ForceField *ConvergeMotes_Field(Vector3 center, float radius, float t01)
{
    int slot = -1;
    for (int i = 0; i < CONVERGE_MAX_FIELDS; i++)
    {
        if (s_chargeFieldUsed[i] &&
            Vector3DistanceSqr(s_chargeFieldPos[i], center) < 0.25f) { slot = i; break; }
    }
    if (slot < 0)
    {
        slot = s_chargeFieldNext++ % CONVERGE_MAX_FIELDS;
        s_chargeFieldUsed[slot] = true;
    }
    s_chargeFieldPos[slot] = center;

    ForceField *ff = &s_chargeFields[slot];
    ForceField_Clear(ff);
    // Strength scales with radius so the flight time is the same at any size (a
    // fixed acceleration would make a big converge feel sluggish and a small one
    // snap). It rises with t01: the pull visibly hardens as it fills.
    ForceField_AddLayer(ff, (ForceLayer){
        .type = FORCE_GRAVITY_POINT,
        .origin = center,
        .strength = radius * Math_Mix(5.0f, 11.0f, t01) * s_chargePull,
        .radius = 0.0f,           // unbounded: a thread must never stall outside
        .falloff = 0.0f,
    });
    // A gentle vortex about the vertical axis through the centre. Not enough to
    // make anything orbit — it bends the last stretch of every thread the same
    // way, which is what stops N independent attractor falls from reading as N
    // straight lines converging.
    ForceField_AddLayer(ff, (ForceLayer){
        .type = FORCE_VORTEX,
        .origin = center,
        .direction = (Vector3){ 0.0f, 1.0f, 0.0f },
        .strength = radius * 2.6f * s_chargeSwirl,
        .radius = radius * 1.6f,
        .falloff = 1.0f,
    });
    // Drag is what makes the motion SOFT rather than ballistic: without it the
    // threads whip through the centre and out the far side, and the arc reads as
    // a slingshot instead of qi settling in.
    ForceField_AddLayer(ff, (ForceLayer){
        .type = FORCE_DRAG,
        .strength = 2.2f,
    });
    return ff;
}

// Continuous: call once per frame while the converge should exist.
// `radius`    = the emitter sphere's radius in metres (where threads are born).
// `t01`       = progress 0→1: density, pull, brightness.
// `moteCount` = threads per second (the mesh decides WHERE, this decides HOW MANY).
void VFX_ComposeConvergeMotes(Vector3 center, VC_MaterialId mat, float radius,
                              float t01, int moteCount)
{
    ConvergeMotes_InitShared();
    if (radius <= 0.0f) radius = 1.0f;
    if (t01 < 0.0f) t01 = 0.0f;
    if (t01 > 1.0f) t01 = 1.0f;
    if (moteCount < 1) moteCount = 1;

    const VFX_ElementMaterial *m = VFX_Material(mat);
    const MeshAdjacency *sphere  = ConvergeMotes_Sphere();
    const ForceField    *field   = ConvergeMotes_Field(center, radius, t01);

    // The emitter shell tightens as it fills, so the whole tell closes in visibly
    // even though each thread runs its own flight.
    float shell = radius * Math_Mix(1.0f, 0.78f, t01);
    Matrix xform = MatrixMultiply(MatrixScale(shell, shell, shell),
                                  MatrixTranslate(center.x, center.y, center.z));

    // Framerate-independent spawn budget carried between frames. A COUNT per call
    // makes density a function of the frame rate — the rule that has bitten this
    // project in three separate effects.
    static float s_accum = 0.0f;
    s_accum += GetFrameTime() * ((float)moteCount * Math_Mix(0.6f, 1.8f, t01) * s_chargeRate);
    int spawn = (int)s_accum;
    if (spawn > 24) spawn = 24;         // clamp after a hitch
    s_accum -= (float)spawn;

    for (int s = 0; s < spawn; s++)
    {
        // SpawnParticleOnMesh chooses the position (a random point on a random
        // edge of the sphere), so the launch VELOCITY has to be built without
        // knowing it. A push along a random direction gives the same curl
        // wherever the point lands, and the attractor does the rest.
        Vector3 axis = VC_DirCone((Vector3){0.0f, 1.0f, 0.0f}, PI, Random01(), Random01());
        Vector3 push = Vector3Scale(axis, radius * Math_Mix(0.5f, 1.1f, Random01()) * s_chargeSwirl);
        // A little lift first — qi peels OFF the surface before it is caught.
        // Without it threads start already falling and the surface never reads
        // as the source.
        push.y += radius * 0.12f;

        // Per-thread opacity. Every thread at the same alpha reads as a printed
        // pattern; qi has some strands catching the light and others barely
        // there. The trail inherits it (the ribbon scales its alpha by the
        // particle's), so one random value varies the whole thread.
        unsigned char alpha = (unsigned char)(90.0f + 165.0f * Random01());

        SpawnParticleOnMesh(sphere, xform, (ParticleConfig){
            .velocity = push,
            .forceField = field,
            // Thread thickness: with no head billboard this only scales the
            // ribbon (half-width = radius * trailWidthRatio).
            .radius   = (0.010f + 0.008f * Random01()) * radius * s_chargeSize,
            // Long enough to fly in and be absorbed; the fade curve retires it.
            .lifetime = Math_Mix(0.85f, 1.35f, Random01()),
            // Whitened at the source: a saturated element colour stacks
            // additively into more of the same hue and never reaches white, so
            // emissiveBoost's multiply has nothing to lift (see VC_Whiten).
            // Near-white body, element hue only as it fades out. White is what
            // makes the element a TINT rather than the thread's identity, and it
            // is also the only colour additive stacking can push to a hot core:
            // a saturated hue just accumulates more of itself.
            .colorStart = VC_WithAlpha(VC_Whiten(m->glow, s_chargeWhite), alpha),
            .colorEnd   = VC_WithAlpha(m->glow, 0),
            .alphaCurve    = &s_chargeFade,
            .emissiveCurve = &s_chargeEmis,
            .render.blendMode = VFX_BLEND_ADDITIVE,   // emits...
            .render.unlit     = 1,                    // ...so no lighting multiply
            .render.emissiveBoost = Math_Mix(2.0f, 4.5f, t01),
            // The thread itself.
            .render.trailLength     = 8,        // pool max
            .render.trailOnly       = 1,        // no head: a thread, not a comet
            .render.trailSmooth     = 1,        // Catmull-Rom, or the arc facets
            .render.trailStepTime   = 0.055f,   // 8 pts x 0.055 s of travel
            .render.trailWidthRatio = 2.6f,
            .render.trailColorStart = VC_WithAlpha(VC_Whiten(m->glow, s_chargeWhite), alpha),
            .render.trailColorEnd   = VC_WithAlpha(m->glow, 0),
        });
    }
}
