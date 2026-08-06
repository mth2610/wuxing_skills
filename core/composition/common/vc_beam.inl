// ── P4. VFX_ComposeBeam — the sustained line. STEPS 1-2 of 3 ────────────────
//
// Spec: core/docs/VFX_PLAN.md §4.3 P4. Read it before changing anything here;
// this file is the first of three deliberately separated steps, and the whole
// point of separating them is that each one gets ONE look:
//
//   STEP 1  DONE  body tube + hot core, no deform. Answered its own question
//                 (shape + both DoD cases) and, unintentionally, showed that a
//                 straight undeformed tube gives nothing a billboard does not.
//   STEP 2  HERE  the three deform layers. This is the step that decides
//                 whether P4 stays a volume at all — see Beam_BuildDeform.
//   STEP 3        material/element identity pass.
//
// Step 1 was a mis-cut: it withheld the ONE capability that distinguishes a
// volume from a strip (a non-cylindrical outline), so it could not possibly
// justify its own architecture. Splitting steps to avoid confounding variables
// is right; splitting them so the first one cannot answer the question being
// asked is not. Recorded because the same trap is available on Projectile and
// Aura next.
//
// ── WHAT THIS IS NOT BUILT FROM, and it is deliberate ────────────────────────
// A `vc_beam.inl` existed before and was deleted. core/ribbon_strip.h still
// carries its scaffolding (`Ribbon_ComputeCrossFrame`, `DrawRibbonEnergyField`)
// and says so in its own comments. That built the beam from two crossed PLANES
// — a flat strip, which VFX_PLAN §0.1 names as the entire diagnosis of why this
// project's VFX do not land. Those functions stay (EnergyFlow uses them); the
// beam is not made of them. A flat flare at the contact point is
// `VFX_ComposeImpactFlash`, a different primary.
//
// ── THE TWO DoD CASES, both of which have a real failure mode ────────────────
//
// 1. NEARLY COINCIDENT ENDPOINTS. PMTubeSamplePath returns early when the path
//    length is 0, and pm_tube.inl's ringGap falls back to baseRadius. Nothing
//    crashes; it simply draws something that is not a beam. Handled by
//    BEAM_MIN_LENGTH below — the beam hides itself rather than drawing garbage,
//    and it hides by taking layer alpha to zero, not by an early return, so the
//    pool slot and the trail stay alive and the beam comes back the instant the
//    endpoints separate again.
//
// 2. VIEWED END-ON — this one is why the HOT CORE is mandatory, not decoration.
//    trail_volume.fs weights every fragment by |N.V|, and looking down the axis
//    of a tube puts N perpendicular to V EVERYWHERE, so d ~ 0 and
//    `rim = smoothstep(0, 0.34, d)` takes alpha to zero across the whole body.
//    Measured: d=0.00 -> edge 0.0000, d=0.05 -> 0.0528, d=0.10 -> 0.1690. A beam
//    fired at the camera is exactly the shot that must not vanish, and the body
//    layer cannot fix this at any tuning because the term is zero, not small.
//    So the core is drawn WITHOUT the volume shader (tubeVolumeShading = false)
//    and additively — its brightness does not consult |N.V| at all. See
//    core/tests/beam_geometry_test.c for the numbers.
//
// ── ARCHETYPE ────────────────────────────────────────────────────────────────
// Managed pool + handle, copied from vc_smoke_column.inl, NOT a fire-and-forget
// void call. VFX_PLAN's original sketch had `void VFX_ComposeBeam(..., float
// t01)`; a beam is SUSTAINED, so a void function called every frame either
// stacks pool entries or rebuilds from scratch, and either way it discards the
// time state — churn phase and UV scroll — that makes a beam boil in place
// instead of just sitting there. Per VFX_PLAN §0.3 a handle-returning
// composition needs its own Update/Draw3D pair wired in (core/docs/LANDMINES.md:
// a manifest `type` of `continuous` is NOT sufficient).

#include "core/deform/mesh_deform.h"
#include "core/tuning.h"

#define VFX_BEAM_MAX 4
#define BEAM_TAG_BASE 0x5E000

// Below this the beam hides — see DoD case 1 above. 5 cm: short enough that a
// legitimately short beam still draws, long enough that the degenerate tangent
// in PMTubeSamplePath is never reached.
#define BEAM_MIN_LENGTH 0.05f

// Static path nodes. A straight segment needs almost none — this is NOT the
// mesh slice count (that is tubeGeomSegs, a separate question entirely; see
// trail_system.h's field doc and core/tests/trail_geom_segs_test.c). 4 rather
// than the minimum 2 only so the transported frame has a settled tangent.
#define BEAM_PATH_NODES 4

typedef struct {
    bool active;
    bool stopping;
    int serial;
    int bodyId; // volume-shaded tube, alpha, occludes
    int coreId; // thin additive tube, NO volume shader — the end-on answer
    Vector3 from, to;
    VC_MaterialId matId;
    float width;
    TrailLayer bodyLayers[1];
    TrailLayer coreLayers[1];
    PMTubeConfig bodyTube;
    PMTubeConfig coreTube;
    MeshDeformField deform; // owned: pointed at by bodyTube.noiseField
    bool spawnedProbe;      // beam_probe as it was AT SPAWN — see VC_Beam_Update
} VC_Beam;

static VC_Beam s_beams[VFX_BEAM_MAX];
static int s_beamSerial = 0;
static bool s_beamInit = false;
static Texture2D s_beamSheet;

// Live knobs, own namespace. Registered lazily on first use — never from an
// Init: Tuning_Init runs after the subsystem inits in main.c, so an early
// registration silently keeps the default (core/docs/LANDMINES.md).
static float s_beamAlphaMul = 1.0f;
static float s_beamCoreMul = 1.0f;
static float s_beamTile = 2.0f;
static float s_beamDeform = 1.0f;
// PROBE MODE — owner's request, 06/08: "vẽ 1 hình tube đơn giản và làm nó đứng
// yên rồi xác định biên cho nó -> coi đúng hay chưa (phải xác nhận được bằng
// hình ảnh)".
//
// The shipped beam stacks a taper, three deform layers, an additive core, a
// scrolling sheet and a vertical alpha mask on top of each other. Every one of
// those moves the apparent edge, so asking "is the boundary in the right
// place" of that image is unanswerable — which is why several rounds of
// looking at it produced descriptions rather than verdicts. Probe strips all
// of it and leaves one smooth stationary cylinder, whose true silhouette is
// known exactly: |N.V| = 0. Pair with volume_debug = 10 and the question
// becomes "does the bright band coincide with the red band", which an image
// CAN answer.
static float s_beamProbe = 0.0f;
static void Beam_EnsureTuning(void)
{
    static bool done = false;
    if (done) return;
    done = true;
    Tuning_RegisterFloat("beam_alpha", &s_beamAlphaMul, 1.0f);
    Tuning_RegisterFloat("beam_core", &s_beamCoreMul, 1.0f);
    Tuning_RegisterFloat("beam_tile", &s_beamTile, 2.0f);
    // extAmp on the whole deform field (pm_tube's cfg.noiseAmp). THE knob for
    // step 2's verdict: sweep it to 0 and the beam is step 1 again, so the
    // comparison "does the outline earn the volume" costs a file save.
    Tuning_RegisterFloat("beam_deform", &s_beamDeform, 1.0f);
    Tuning_RegisterFloat("beam_probe", &s_beamProbe, 0.0f);
}

// VOLUME_FIRE, and NOT VFX_SURFACE_ENERGY_TUBE — which is what step 1 shipped
// with for one look, and it came back "màu sắc kì quặc" with black streaks.
//
// trail_volume.fs states its contract in its own source: "A = coverage in the
// OPAQUE layout; RGB is grey luminance KEPT GREY so the caller's tint
// survives", and it then computes `colour = s1.rgb * vColor.rgb`. Check the
// two sheets' declared channels in assets/vfx_surface_profiles.json:
//
//   VOLUME_SMOKE/FIRE/STEAM  "...GREY luminance in RGB so the caller's
//                             VFX_Material tint survives, real coverage in A"
//   ENERGY_TUBE              "...tintable energy filaments"   <- no grey promise
//
// energy_volume.png is a real COLOUR sheet: bright filaments over a dark
// ground, with coverage in A that stays high BETWEEN the filaments. Multiply
// that by a tint and two things happen at once, both of which were on screen:
// the lit filaments come out some blend of sheet colour and element colour
// ("kì quặc"), and the dark ground between them renders as OPAQUE BLACK rather
// than as transparent, because its alpha is high while its RGB is near zero.
// "Trong suốt -> thành màu đen" is exactly that, and it is not a blend-mode
// bug: the fragment really is opaque and really is black.
//
// VFX_ComposeVolumeTrail's energy variant is the other consumer of this sheet
// and has the same defect for the same reason — logged in docs/PROGRESS.md,
// deliberately NOT fixed here, because changing it changes an effect nobody
// asked to change.
static void Beam_InitShared(void)
{
    if (s_beamInit) return;
    s_beamInit = true;
    const VFX_SurfaceProfile *p = VFX_SurfaceRegistry_Get(VFX_SURFACE_VOLUME_FIRE);
    s_beamSheet = (p != NULL) ? p->body : (Texture2D){0};
}

static float Beam_Length(const VC_Beam *b)
{
    return Vector3Distance(b->from, b->to);
}

// Layer alpha is where "hide" lives — see DoD case 1. Pushed every frame from
// Update so a tuning.cfg reload and an endpoint collapse both land without a
// respawn.
static void Beam_ConfigureLayers(VC_Beam *b)
{
    // THE SELF-HIDE. Not an early return anywhere: the trail keeps existing, so
    // when the endpoints separate again the beam reappears with its UV scroll
    // and (from step 2) its churn phase intact, instead of popping in fresh.
    //
    // EPSILON, NOT ZERO — and zero is what this shipped with until 06/08.
    // trail_system.c does `float aMul = (ly->alphaMul > 0.0f) ? ly->alphaMul
    // : 1.0f;` (twice), i.e. it reads 0 as "caller did not set this" and
    // substitutes FULL alpha. So every "hide" written as `* 0.0f` here did the
    // exact opposite of what it said. Caught because beam_probe was supposed
    // to remove the core and the core stayed at full brightness on screen.
    //
    // Not fixed at the source on purpose: `{0}`-initialised TrailConfigs all
    // over the codebase rely on 0 meaning 1.0, and changing that would make
    // every trail that never set alphaMul go invisible at once. The sentinel
    // is wrong, but it is load-bearing.
    float live = (Beam_Length(b) >= BEAM_MIN_LENGTH) ? 1.0f : 1e-4f;
    // PROBE: one bare wall, nothing layered over it. The core goes to zero
    // because an additive filament down the middle is exactly what makes the
    // body's own gradient unreadable.
    bool probe = (s_beamProbe > 0.5f);

    // ONE alpha layer, not two. Two textured alpha layers on the SAME mesh are
    // drawn back to back with no depth sort between them, and that is one of
    // the two named trail defects the owner reports as long-standing ("lỗi
    // blend màu của trail"). A second layer cannot be composited correctly here
    // — it can only add another unsorted alpha pass over the first. Dropping it
    // does not fix that defect, it just stops this file contributing to it.
    b->bodyLayers[0] = (TrailLayer){
        .widthMul = 1.0f,
        .alphaMul = 0.80f * s_beamAlphaMul * live,
        .whiten = 0.0f,
        .scrollMul = 1.0f,
        // No sheet in probe mode: the sheet's own light and dark patches are
        // indistinguishable from the shading gradient being measured.
        .texture = probe ? NULL : &s_beamSheet,
    };

    // THE CORE IS DELIBERATELY PLAIN — owner's call, 06/08: "cái lõi nên làm
    // đơn giản thôi". No sheet (texture NULL makes trail_system fall back to
    // its flat white), no scroll, no volume shader: a smooth additive filament
    // and nothing else. Its whole job is to be the thing that survives the
    // end-on shot, and every extra feature on it is another unsorted pass over
    // the body for no silhouette gained.
    //
    // 0.35, DOWN FROM 0.85 (06/08, same session): at 0.85 an ADDITIVE flat
    // white core saturates to full white almost immediately and then bleeds,
    // and on screen it swallowed the body entirely — the owner's shot showed a
    // fat white bar with one thin red fringe surviving beside it. Additive
    // alpha is not opacity, it is how much light is ADDED, so the useful range
    // sits far lower than an alpha layer's. Anything above ~0.5 here is not a
    // brighter core, it is a wider one.
    b->coreLayers[0] = (TrailLayer){
        .widthMul = 1.0f,
        .alphaMul = probe ? 1e-4f : (0.35f * s_beamCoreMul * live),
        .whiten = 0.35f, // reads as heat, not as element colour
        .scrollMul = 1.0f,
        .texture = NULL, // flat — see above
    };
}

// ── STEP 2 — the deform layers, and this is the step that decides P4 ─────────
//
// THE QUESTION THIS STEP EXISTS TO ANSWER, stated so a later reader does not
// have to reconstruct it: after step 1 the owner said, correctly, that a
// straight axially-symmetric tube gives nothing a flat billboard does not —
// cheaper, no aliasing, no cull, no |N.V|. That is TRUE of step 1, and it is
// true because the ONE thing a volume has that a billboard can never have is a
// NON-CYLINDRICAL SILHOUETTE, and step 1 deliberately had none. The smoke
// column is the counter-example already on screen: same hollow shading, reads
// as volume anyway, because churn bends its outline. So step 2 is the real
// verdict, with the failure condition agreed up front — if a deformed beam
// still reads no better than a strip, volume is not worth it FOR A BEAM and we
// drop it here.
//
// ── WHY THE PULSE IS NORMAL_SCALE AND NOT TANGENT ───────────────────────────
// VFX_PLAN's spec for P4 says MESH_DEFORM_SINE + MESH_DEFORM_DIR_TANGENT, and
// that is wrong for this shape — found while implementing, not guessed:
// mesh_deform.c's TANGENT branch adds `tangent * w`, and pm_tube.inl passes the
// path tangent there, so on a STRAIGHT tube it slides each ring ALONG its own
// axis. That moves vertices without moving the OUTLINE at all, which is
// precisely the quantity under test. TANGENT earns its keep on a curved sweep
// (peristalsis down a bent body); on a straight beam it is invisible.
//
// NORMAL_SCALE swells and pinches the section instead, so a travelling sine on
// it is a bulge running down the beam — an outline a billboard cannot fake at
// any camera angle. Spec updated to match.
static void Beam_BuildDeform(VC_Beam *b)
{
    MeshDeform_Clear(&b->deform);
    b->deform.amplitude = 1.0f;
    b->deform.timeScale = 1.0f;
    // Procedural lattice (noisePixels stays NULL). latticeAround must divide
    // the section — 16 radial / 4 = 4 lobes, an integer count the section can
    // resolve (mesh_deform.h warns that a period which does not divide leaves
    // a seam down the whole body at u = 0).
    b->deform.latticeAround = 4;
    b->deform.latticeAlong = 3;

    // 1. THE PULSE. A travelling swell, the layer this whole step is for.
    //    mat.y is metres/noiseWavelength (set below), so tiling.y = 1 means one
    //    swell per wavelength REGARDLESS of how long the beam is — a 3 m and a
    //    30 m beam get the same grain, which is the dividend from 05/08's
    //    material-coordinate work. env NONE: the pulse should run the whole
    //    body, including right out of the emitter.
    MeshDeform_AddLayer(&b->deform, (MeshDeformLayer){
        .kind = MESH_DEFORM_SINE,
        .direction = MESH_DEFORM_DIR_NORMAL_SCALE,
        .tiling = {1.0f, 1.0f}, .amplitude = 1.10f, .speed = -1.6f,
        .env = UV_ENV_NONE,
    });

    // 2. BODY NOISE. Breaks the sine's regularity — a pure sine reads as a
    //    machined part (the same reason vc_projectile.inl gives its two wisps
    //    different turn rates). Small amplitude: this is variation, not shape.
    MeshDeform_AddLayer(&b->deform, (MeshDeformLayer){
        .kind = MESH_DEFORM_NOISE_CHANNEL,
        .direction = MESH_DEFORM_DIR_NORMAL_SCALE,
        .tiling = {1.0f, 1.3f}, .amplitude = 0.55f, .speed = 0.9f,
        .latticeMul = 1.0f, .latticeAroundMul = 1.0f,
        .env = UV_ENV_HEAD_WELD, .envStart = 0.0f, .envEnd = 0.12f,
    });

    // 3. SURFACE RIPPLE. NORMAL_OFFSET, the only layer here whose excursion is
    //    in absolute metres and therefore the only one the ring-gap clamp can
    //    bite (PM_TUBE_MAX_OFFSET_RINGS = 0.6 x ringGap). Kept small on
    //    purpose so it is detail rather than something fighting a clamp.
    MeshDeform_AddLayer(&b->deform, (MeshDeformLayer){
        .kind = MESH_DEFORM_NOISE_CHANNEL,
        .direction = MESH_DEFORM_DIR_NORMAL_OFFSET,
        .tiling = {1.0f, 2.1f}, .amplitude = 0.30f, .speed = 1.7f,
        .timeOffset = 11.0f, .latticeMul = 3.0f, .latticeAroundMul = 2.0f,
        .env = UV_ENV_HEAD_WELD, .envStart = 0.0f, .envEnd = 0.20f,
    });
}

static void Beam_BuildShape(VC_Beam *b)
{
    b->bodyTube = PMTube_DefaultConfig();
    b->bodyTube.wobbleAmplitude = 0.0f;
    b->bodyTube.deform1Amp = 0.0f;
    b->bodyTube.deform2Amp = 0.0f;
    b->bodyTube.useTransportFrame = true;
    // Nearly straight, opening slightly toward the far end. Trail_SetStaticPath
    // seeds `from` as the tail, and pm_tube's default anchor pins t=1 (the far
    // end) at exactly the requested radius, so the emitter end comes out at
    // radiusTailFrac x width. anchorAtTail stays FALSE: the emitter is `from`,
    // which is already t=0, so all four anchored quantities (radius, deform
    // envelope, centreline weld, alpha mask) weld there together with no flip.
    bool probe = (s_beamProbe > 0.5f);
    // PROBE: a straight cylinder of constant radius. A taper tilts the
    // silhouette across the screen, and a tilted edge is exactly what makes
    // "is the edge in the right place" hard to judge by eye.
    b->bodyTube.radiusTailFrac = probe ? 1.0f : 0.80f;
    b->bodyTube.radiusPow = 1.0f;
    b->bodyTube.anchorAtTail = false;
    b->bodyTube.centerlineAmp = 0.0f; // STEP 3 territory, not this one

    Beam_BuildDeform(b);
    b->bodyTube.noiseField = probe ? NULL : &b->deform;
    // METRES per lattice pass, not a fraction of the beam's length — this is
    // what keeps the grain identical on a 3 m and a 30 m beam. 2.0 m gives a
    // few swells on a typical bench beam without turning into ripple.
    b->bodyTube.noiseWavelength = 2.0f;
    // The clock-driven drift PMTube_DefaultConfig sets to 1.0 stays ON here,
    // unlike the smoke trail: a beam's endpoints are FIXED, so it has no real
    // material motion of its own and the clock is its only source. (The smoke
    // trail turns it off precisely because it does have one — see that file.)

    b->coreTube = b->bodyTube;
    b->coreTube.radiusTailFrac = 0.90f; // the core is nearly a cylinder
    // THE CORE STAYS SMOOTH — owner's call. A deformed core would put a second
    // wobbling outline inside the first for no gain, and the core's one job is
    // to survive the end-on shot.
    b->coreTube.noiseField = NULL;
    b->coreTube.noiseWavelength = 0.0f;
}

static int Beam_SpawnOne(VC_Beam *b, int slot, bool isCore)
{
    const bool probe = (s_beamProbe > 0.5f); // see s_beamProbe's own comment
    TrailConfig cfg = {0};
    // MANDATORY. TrailConfig{0} means TRAIL_TYPE_PROJECTILE, and
    // Trail_SetStaticPath opens with `if (t->type != TRAIL_TYPE_FOLLOWER)
    // return;` — no log, no return value. The path is then never seeded, the
    // trail holds coincident nodes, the tangent is garbage and the section
    // collapses to a flat quad. That exact bug shipped once in the smoke column;
    // see its file for the full account.
    cfg.type = TRAIL_TYPE_FOLLOWER;
    cfg.pos = b->from;
    cfg.life = 1.0e6f; // dies when the caller stops it, never on its own
    // PROBE: FAT. The shipped beam is 0.10 m over 4.6 m — about ten pixels
    // across on screen, and with 16 radial faces that is under a pixel per
    // face. dFdx/dFdy sample a 2x2 pixel quad, so at that size the derivative
    // straddles DIFFERENT TRIANGLES and the reconstructed normal is noise —
    // visible as the speckled patches in the band views. A probe has to be
    // large enough that each face covers several pixels, or it measures the
    // rasteriser rather than the shading.
    cfg.thick = isCore ? (b->width * 0.34f) : (probe ? (b->width * 6.0f) : b->width);
    cfg.gradient = NULL; // the material carries the colour
    const VFX_ElementMaterial *m = VFX_Material(b->matId);
    cfg.tint = (m != NULL) ? (isCore ? m->glow : m->body) : WHITE;

    cfg.forceField = NULL;         // a beam is not a simulation
    cfg.widthEnvelope = TRAIL_WIDTH_ENVELOPE_UNIFORM; // the taper is in the tube profile

    cfg.shape = TRAIL_SHAPE_TUBE;
    cfg.tubeShapeConfig = isCore ? &b->coreTube : &b->bodyTube;
    // PROBE: more radial segments, so the silhouette is a curve rather than a
    // polygon corner — the corner itself would read as an edge and confound
    // the very thing being located.
    cfg.tubeRadialSegs = isCore ? 8 : (probe ? 32 : 16);
    cfg.tubeMaxRings = BEAM_PATH_NODES;
    // MESH SLICES, separate from the node count above — see trail_system.h.
    // A straight beam needs slices for the deform coming in step 2, not for the
    // path, which two points already describe exactly.
    cfg.tubeGeomSegs = isCore ? 8 : 24;
    cfg.tubeSingleSided = false;
    // THE END-ON ANSWER, and the single most load-bearing line in this file.
    // The body is volume-shaded and therefore weighted by |N.V|, which is zero
    // everywhere when the beam points at the camera. The core is not, so it
    // survives that shot. See the header.
    cfg.tubeVolumeShading = !isCore;
    cfg.tubeDeformFrozen = false;
    // extAmp for the caller-supplied field (pm_tube.inl multiplies the whole
    // field by it). 0 on the core: it stays smooth.
    cfg.tubeNoiseAmp = (isCore || probe) ? 0.0f : (0.45f * s_beamDeform);

    cfg.layers = isCore ? b->coreLayers : b->bodyLayers;
    cfg.layerCount = 1; // body and core are one layer each — see Beam_ConfigureLayers
    cfg.uvMetresPerTile = (s_beamTile > 0.05f) ? s_beamTile : 0.05f;
    // PROBE: dead still. Anything moving turns a position question into a
    // memory question.
    cfg.uvScrollSpeed = (isCore || probe) ? 0.0f : 0.85f;
    // Blend law (VFX_PLAN §0.3): the body OCCLUDES so it is alpha; the core
    // EMITS so it is additive and unlit. Glowing smoke is two draws, and so is
    // a glowing beam.
    cfg.blendMode = isCore ? BLEND_ADDITIVE : BLEND_ALPHA;
    cfg.useCustomBlendMode = true; // BLEND_ALPHA is 0 and cannot be detected by > 0

    cfg.minVertexDistance = 0.0f;
    cfg.sampleHz = 60.0f;
    cfg.idleSpeed = 0.0f; // a still beam is the normal state, never "idle"
    cfg.trailLength = (float)cfg.tubeMaxRings;
    cfg.disableInnerCore = true;
    cfg.ownerTag = BEAM_TAG_BASE | (slot << 1) | (isCore ? 1 : 0);
    // LOW, and it is the only honest choice: VFXPriority (core/vfx_light.h)
    // has exactly two values, LOW and HIGH_ULTIMATE, and the second is
    // reserved for ultimates. A held beam is a normal primary, so it takes the
    // normal priority rather than borrowing the ultimate tier to jump the pool.
    cfg.priority = VFX_PRIORITY_LOW;

    int id = SpawnTrailEntity(cfg);
    if (id < 0) return -1;

    Trail_SetStaticPath(id, b->from, b->to, BEAM_PATH_NODES);

    // READ IT BACK, do not trust the call. Every failure this archetype has
    // shipped was a silent early return that left a plausible wrong result, and
    // none was visible in a log that printed only the INPUTS.
    const TrailEntity *chk = GetTrail(id);
    int laid = (chk != NULL) ? chk->historyCount : -1;
    if (laid != BEAM_PATH_NODES)
        TraceLog(LOG_WARNING,
                 "VFX_BEAM: static path did NOT take (%s) — asked %d nodes, got "
                 "%d. The tube will collapse to a flat quad. Check cfg.type == "
                 "TRAIL_TYPE_FOLLOWER.",
                 isCore ? "core" : "body", BEAM_PATH_NODES, laid);
    return id;
}

// ── Public API ──────────────────────────────────────────────────────────────

int VFX_ComposeBeam(Vector3 from, Vector3 to, VC_MaterialId mat, float width)
{
    Beam_EnsureTuning();
    Beam_InitShared();

    int slot = -1;
    for (int i = 0; i < VFX_BEAM_MAX; i++)
        if (!s_beams[i].active) { slot = i; break; }
    if (slot < 0)
    {
        TraceLog(LOG_WARNING, "VFX_BEAM: pool full (%d) — request dropped", VFX_BEAM_MAX);
        return -1;
    }

    VC_Beam *b = &s_beams[slot];
    *b = (VC_Beam){0};
    b->active = true;
    b->from = from;
    b->to = to;
    b->matId = mat;
    b->width = (width > 0.01f) ? width : 0.20f;
    b->serial = ++s_beamSerial;

    Beam_ConfigureLayers(b);
    Beam_BuildShape(b);
    b->spawnedProbe = (s_beamProbe > 0.5f);
    b->bodyId = Beam_SpawnOne(b, slot, false);
    b->coreId = Beam_SpawnOne(b, slot, true);
    if (b->bodyId < 0) { b->active = false; return -1; }

    // READ THE NUMBERS BACK OFF THE TRAIL, do not re-derive them here. Printing
    // `probe ? 32 : 16` was wrong three times running: trail_system.c clamps to
    // TRAIL_TUBE_RADIAL_MAX = 16, so the log said 32 while the mesh had 16 and
    // TUBE_CHURN_DEV_DEBUG's n = 400 (25 x 16) was the only honest witness. A
    // log that recomputes its own inputs cannot report a clamp, a fallback or
    // a rejected value — the three things a spawn log exists to catch.
    const TrailEntity *bodyT = (b->bodyId >= 0) ? GetTrail(b->bodyId) : NULL;

    // ONE LINE PER SPAWN, unconditional — "it looks the same as before" and
    // "the new path never ran" are the same picture otherwise.
    TraceLog(LOG_INFO,
             "VFX_BEAM: slot %d — %.2f m, width %.2f, MESH %d x %d body ACTUAL "
             "(asked %d radial) | probe %s | tile %.2f m%s",
             slot, Beam_Length(b), b->width,
             (bodyT != NULL) ? bodyT->tubeGeomSegs : -1,
             (bodyT != NULL) ? bodyT->tubeRadialSegs : -1,
             (s_beamProbe > 0.5f) ? 32 : 16,
             (s_beamProbe > 0.5f) ? "ON" : "off", s_beamTile,
             (Beam_Length(b) < BEAM_MIN_LENGTH) ? "  [HIDDEN: below min length]" : "");
    return (s_beamSerial << 8) | slot;
}

// Move the beam without respawning it. Re-seeding the static path keeps
// uvScrollOffset (it lives on the TrailEntity, and Trail_SetStaticPath does not
// touch it), so a beam that sweeps across a scene does not reset its flow every
// frame — which is the whole reason this is a handle API and not a void call.
void VFX_Beam_SetEndpoints(int handle, Vector3 from, Vector3 to)
{
    int slot = handle & 0xFF;
    if (handle < 0 || slot >= VFX_BEAM_MAX) return;
    VC_Beam *b = &s_beams[slot];
    if (!b->active || (handle >> 8) != b->serial) return;

    b->from = from;
    b->to = to;
    if (b->bodyId >= 0) Trail_SetStaticPath(b->bodyId, from, to, BEAM_PATH_NODES);
    if (b->coreId >= 0) Trail_SetStaticPath(b->coreId, from, to, BEAM_PATH_NODES);
}

void VFX_Beam_Stop(int handle)
{
    int slot = handle & 0xFF;
    if (handle < 0 || slot >= VFX_BEAM_MAX) return;
    VC_Beam *b = &s_beams[slot];
    if (!b->active || (handle >> 8) != b->serial) return;
    b->stopping = true;
}

static void VC_Beam_Update(float dt)
{
    (void)dt;
    for (int i = 0; i < VFX_BEAM_MAX; i++)
    {
        VC_Beam *b = &s_beams[i];
        if (!b->active) continue;

        TrailEntity *body = (b->bodyId >= 0) ? GetTrail(b->bodyId) : NULL;
        if (body == NULL) { b->active = false; continue; }

        // PROBE NEEDS A REBUILD, NOT A RE-PUSH — found 06/08 by reading a log
        // that disagreed with the config: TUBE_CHURN_DEV_DEBUG reported
        // n = 400 = 25 rings x 16 radial while beam_probe was on and probe
        // asks for 32. Radial count, taper and noiseField are all decided in
        // Beam_BuildShape/Beam_SpawnOne, which only run at spawn; only
        // tubeNoiseAmp and layer alpha are re-pushed here. So flipping the
        // knob mid-session gave a HALF-probe — deform off but taper still on,
        // 16 radial not 32 — i.e. an instrument quietly measuring something
        // other than what it claimed. Respawn instead.
        bool probeNow = (s_beamProbe > 0.5f);
        if (probeNow != b->spawnedProbe)
        {
            Vector3 from = b->from, to = b->to;
            if (b->bodyId >= 0) KillTrail(b->bodyId);
            if (b->coreId >= 0) KillTrail(b->coreId);
            Beam_ConfigureLayers(b);
            Beam_BuildShape(b);
            b->spawnedProbe = probeNow;
            b->bodyId = Beam_SpawnOne(b, i, false);
            b->coreId = Beam_SpawnOne(b, i, true);
            b->from = from; b->to = to;
            TraceLog(LOG_INFO, "VFX_BEAM: slot %d rebuilt — probe %s", i,
                     probeNow ? "ON" : "off");
            continue;
        }

        // Re-push every live knob so a tuning.cfg reload lands without a
        // respawn — and this is also where the self-hide is re-evaluated, so a
        // beam whose endpoints collapsed this frame goes invisible this frame.
        Beam_ConfigureLayers(b);
        body->tubeNoiseAmp = (s_beamProbe > 0.5f) ? 0.0f
                                                  : (0.45f * s_beamDeform);
        body->uvMetresPerTile = (s_beamTile > 0.05f) ? s_beamTile : 0.05f;
        TrailEntity *core = (b->coreId >= 0) ? GetTrail(b->coreId) : NULL;
        if (core != NULL) core->uvMetresPerTile = (s_beamTile > 0.05f) ? s_beamTile : 0.05f;

        if (b->stopping)
        {
            if (b->bodyId >= 0) KillTrail(b->bodyId);
            if (b->coreId >= 0) KillTrail(b->coreId);
            b->bodyId = b->coreId = -1;
            b->active = false;
        }
    }
}

// Empty ON PURPOSE, and it must exist — the Update/Draw3D PAIR is how a managed
// composition declares itself to scripts/sync_vfx_test.py, and main.c's
// DrawTrailEntitiesBody() already puts every TrailEntity on screen regardless
// of which composition spawned it.
static void VC_Beam_Draw3D(Camera3D cam)
{
    (void)cam;
}
