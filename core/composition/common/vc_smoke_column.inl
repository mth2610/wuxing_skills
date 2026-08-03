// H — SmokeColumn: a volume that RISES from a fixed source.
//
// WHY THIS IS NOT VFX_ComposeVolumeTrail WITH DIFFERENT NUMBERS. A volume trail
// is what a moving emitter LEAVES BEHIND: its shape is the path, and
// vc_volume_trail.inl sets `forceField = NULL` on purpose, because a cloth step
// on a weapon wake reads as a snake. A column is the opposite case — the
// emitter does not move at all, and the entire shape comes from what happens to
// the material AFTER it is emitted. The force field is not a garnish here, it
// is the effect. Two archetypes, one primitive.
//
// NO SIMULATION. THAT WAS THE FIRST DESIGN AND IT WAS WRONG.
//
// The first build drove the column with a ForceField, so the history nodes were
// integrated upward and the shape came from where they drifted. That is not the
// reference technique — the reference simulates NOTHING. Its cylinder is static
// geometry; every bit of life comes from vertex displacement and a scrolled
// sheet. Simulating the medium instead bought three bugs (the column flew off,
// then froze, then stretched; then it vanished) and not one thing the geometry
// was not already going to do.
//
// So the path here is a FROZEN vertical segment, seeded whole at spawn. There
// is no history to accumulate, which also means no fill delay and no ramp.
//
// WHAT IT IS ASSEMBLED FROM, all of it pre-existing:
//   * TRAIL_SHAPE_TUBE + Trail_SetStaticPath   a fixed vertical volume
//   * core/deform via tubeNoiseAmp             the churn — this IS the motion
//   * volume_surface_*.png                     the sheet scrolled over it
//   * TRAIL_WIDTH_ENVELOPE_*                   cylinder or funnel, a parameter
//
// The reference technique (Simon Schreibt's breakdown of The Invisible Hours)
// reaches the same place from the other side: it bakes the emitter's trajectory
// into a texture and reads it in a vertex shader, because Unreal gave it no
// cheap history and the game had to support time-rewind. This engine already
// keeps the history on the CPU and has no rewind, so the trajectory texture
// would buy nothing. What DID transfer is everything downstream of it: the
// noise-driven vertex displacement along the normal, the along-surface envelope
// welding the base to the source, and the scrolled sheet on top.

#include "core/deform/mesh_deform.h"
#include "core/tuning.h"

#define VFX_SMOKE_COLUMN_MAX 6
#define SMOKE_COLUMN_TAG_BASE 0x5C000

typedef struct {
    bool active;
    bool stopping;
    int trailId;
    int serial;
    Vector3 pos;
    VC_MaterialId matId;
    VFX_ColumnKind kind;
    float radius;
    float elapsed;
    TrailLayer layers[2];
    PMTubeConfig tube;     // owned: the SHAPE, not the trail's business
    MeshDeformField churn; // owned: pointed at by tube.noiseField
} VC_SmokeColumn;

static VC_SmokeColumn s_columns[VFX_SMOKE_COLUMN_MAX];
static int s_columnSerial = 0;
static bool s_columnInit = false;

// One surface per kind. All three are STRAND-layout MATERIAL sheets: every
// channel tiles, because the silhouette comes from the geometry and the sheet
// only supplies detail. See assets/TEXTURE_PACKING.md.
static const VFX_SurfaceId k_columnSurface[VFX_COLUMN_KIND_COUNT] = {
    [VFX_COLUMN_SMOKE] = VFX_SURFACE_VOLUME_SMOKE,
    [VFX_COLUMN_FIRE] = VFX_SURFACE_VOLUME_FIRE,
    [VFX_COLUMN_STEAM] = VFX_SURFACE_VOLUME_STEAM,
};
static Texture2D s_columnSheet[VFX_COLUMN_KIND_COUNT];

// Buoyancy, in m/s^2, to be read against PHYSICS_GRAVITY_MPS2 = 9.81. Smoke
// rises at a fraction of a g — pushing it harder does not look more energetic,
// it looks like a jet. Fire is the one that genuinely accelerates.
// How fast the surface churns and how fast the sheet climbs. These two ARE the
// effect: with the path frozen, nothing else moves.
static const float k_columnNoise[VFX_COLUMN_KIND_COUNT] = {0.34f, 0.30f, 0.22f};
static const float k_columnScroll[VFX_COLUMN_KIND_COUNT] = {-0.55f, -0.95f, -0.40f};

// Live knobs. Registered lazily on first use, never from an Init: Tuning_Init
// runs after subsystem inits in main.c, so early registration silently keeps
// the default (core/docs/LANDMINES.md).
static float s_columnNoiseMul = 1.0f;
static float s_columnScrollMul = 1.0f;
static float s_columnAlphaMul = 1.0f;
static float s_columnTile = 1.10f; // metres per texture repeat

static void SmokeColumn_EnsureTuning(void)
{
    static bool done = false;
    if (done) return;
    done = true;
    Tuning_RegisterFloat("smokecolumn_noise", &s_columnNoiseMul, 1.0f);
    Tuning_RegisterFloat("smokecolumn_scroll", &s_columnScrollMul, 1.0f);
    Tuning_RegisterFloat("smokecolumn_alpha", &s_columnAlphaMul, 1.0f);
    Tuning_RegisterFloat("smokecolumn_tile", &s_columnTile, 1.10f);
}

static void SmokeColumn_InitShared(void)
{
    if (s_columnInit) return;
    s_columnInit = true;
    for (int k = 0; k < VFX_COLUMN_KIND_COUNT; k++)
    {
        const VFX_SurfaceProfile *p = VFX_SurfaceRegistry_Get(k_columnSurface[k]);
        s_columnSheet[k] = (p != NULL) ? p->body : (Texture2D){0};
    }
}

static void SmokeColumn_ConfigureLayers(VC_SmokeColumn *c)
{
    const Texture2D *sheet = &s_columnSheet[c->kind];
    // Two layers scrolling at DIFFERENT rates. One layer is a texture sliding
    // over a shape; two at different rates read as depth, because no single
    // feature can be tracked all the way through.
    c->layers[0] = (TrailLayer){
        .widthMul = 1.0f,
        .alphaMul = 0.85f * s_columnAlphaMul,
        .whiten = 0.0f,
        .scrollMul = 1.0f,
        .texture = sheet,
    };
    c->layers[1] = (TrailLayer){
        .widthMul = 0.72f,
        .alphaMul = 0.55f * s_columnAlphaMul,
        .whiten = 0.22f,
        .scrollMul = 1.85f,
        .texture = sheet,
    };
}

// The shape, and the deform that lives on it. Both are owned by the column and
// handed to the trail by pointer, because DrawLayeredTube no longer decides
// either — before 03/08/2026 it did, and every volume trail in the engine came
// out as the water stream's teardrop whatever it was meant to be.
static void SmokeColumn_BuildShape(VC_SmokeColumn *c, bool funnel)
{
    c->tube = PMTube_DefaultConfig();

    // A PIPE: constant radius, open at both ends. The legacy envelope is a
    // symmetric lens with a cone stuck on each end — neither a droplet nor a
    // tube. With a flat profile, everything the column shows is the deform,
    // which is the only way to judge the deform at all.

    // Funnel = narrow at the TAIL (the base) opening to full at the head (the
    // top). Cylinder = neither end tapers. This is the real cylinder/funnel
    // switch; the trail's width envelope was the wrong lever, because
    // DrawLayeredTube builds the whole tube at the HEAD's half-width, so a
    // head-thin envelope shrinks the volume instead of shaping it.

    // The two analytic sine ripples stay off: they are PERIODIC in both t and
    // phi, which is a helix — a spiral rib winding up the body. That reads as a
    // tornado, not as smoke, and it is half of why the first column looked like
    // a vortex.
    c->tube.wobbleAmplitude = 0.0f;
    c->tube.deform1Amp = 0.0f;
    c->tube.deform2Amp = 0.0f;
    // MỞ hai đầu. Nắp là hai quạt tam giác có đỉnh đẩy ra theo
    // tail/headApexFactor, nên nó không phải một mặt phẳng bịt lại mà là một
    // chóp NÓN — đúng cái chóp nhọn trên đỉnh cột. Khói không có nắp.
    c->tube.useTransportFrame = true;

    // ── The churn (core/deform) ──────────────────────────────────────────────
    // The OTHER half of the vortex look was frequency. One or two lobes around
    // the circumference is an ELLIPSE, and an ellipse whose phase drifts with
    // height is a twisting spiral by construction. Lumps need more lobes than
    // that, decorrelated between layers.
    MeshDeform_Clear(&c->churn);
    c->churn.amplitude = 1.0f; // the trail supplies the real amplitude
    c->churn.timeScale = 0.75f;
    c->churn.latticeAround = 6; // lobes per ring — 1..2 is an ellipse, i.e. a helix
    c->churn.latticeAlong = 7;

    // SCALE breathes the section and keeps the silhouette proportional.
    MeshDeform_AddLayer(&c->churn, (MeshDeformLayer){
        .kind = MESH_DEFORM_NOISE_CHANNEL,
        .direction = MESH_DEFORM_DIR_NORMAL_SCALE,
        .tiling = {1.0f, 1.0f}, .amplitude = 2.0f, .speed = 1.0f,
        .latticeMul = 1.0f, .env = UV_ENV_HEAD_WELD,
        .envStart = 0.0f, .envEnd = 0.22f,
    });
    // OFFSET pushes the surface OFF its own normal — the reference's
    // "Normal + RGB". This is the one that breaks the symmetry: scaling alone
    // can only ever produce a rounder or thinner version of the same section.
    MeshDeform_AddLayer(&c->churn, (MeshDeformLayer){
        .kind = MESH_DEFORM_NOISE_CHANNEL,
        .direction = MESH_DEFORM_DIR_NORMAL_OFFSET,
        .tiling = {1.0f, 1.9f}, .amplitude = 1.1f, .speed = 1.7f,
        .timeOffset = 11.0f, .latticeMul = 3.0f, .env = UV_ENV_HEAD_WELD,
        .envStart = 0.0f, .envEnd = 0.35f,
    });
    c->tube.noiseField = &c->churn;
}

static int SmokeColumn_Spawn(VC_SmokeColumn *c, int slot, float height, bool funnel)
{
    TrailConfig cfg = {0};
    // MANDATORY, and its absence is silent. TrailConfig{0} means
    // TRAIL_TYPE_PROJECTILE, and Trail_SetStaticPath below opens with
    // `if (t->type != TRAIL_TYPE_FOLLOWER) return;` — no log, no error, no
    // return value. The static path is then never seeded, the trail lays
    // coincident nodes at one point, the tangent between them is garbage, and
    // the tube's cross-section collapses to a flat quad. That is exactly what
    // shipped, and the only symptom was a floating sliver.
    cfg.type = TRAIL_TYPE_FOLLOWER;
    cfg.pos = c->pos;
    // Long-lived by construction: it dies when the caller stops it and the feed
    // drains. A lifetime here would be a second, hidden death condition.
    cfg.life = 1.0e6f;
    cfg.thick = c->radius;
    cfg.tint = WHITE;
    cfg.gradient = NULL; // the material carries the colour
    const VFX_ElementMaterial *m = VFX_Material(c->matId);
    if (m != NULL) cfg.tint = m->body;

    // NULL, like vc_volume_trail.inl and for a related reason: the shape is not
    // a simulation. Here it is a fixed segment, deformed. See the file header.
    cfg.forceField = NULL;

    // NOT SMOKE_WIDEN, and the reason is a trap worth stating. DrawLayeredTube
    // builds the whole tube at ONE radius — `headR = scratchOuter[0].halfWidth`,
    // the HEAD's half-width — and then applies its own profile along the body.
    // SMOKE_WIDEN is thin at the head, so it does not widen the column: it
    // shrinks the entire tube to the thin value and the volume renders as a
    // needle. That shipped, and looked exactly like a collapsed cross-section.
    //
    // TAPER_TAIL keeps the head at full width, so headR is the radius the
    // caller asked for, and narrows the TAIL. The head here is the top of the
    // seeded path and the tail is the base, which is the funnel: narrow at the
    // source, open at the top.
    // UNIFORM always: headR is the tube's ONE radius, so the envelope must
    // leave the head at full width. The funnel now lives in the tube profile
    // above, where it belongs.
    cfg.widthEnvelope = TRAIL_WIDTH_ENVELOPE_UNIFORM;

    cfg.shape = TRAIL_SHAPE_TUBE;
    cfg.tubeShapeConfig = &c->tube; // the shape is ours, not DrawLayeredTube's
    // 10 segments cannot show a lump: with 6 lobes around it is under two
    // samples per lobe, so the section reads as a polygon being rotated.
    cfg.tubeRadialSegs = 18;
    cfg.tubeMaxRings = 24;
    cfg.tubeCaps = true;
    // Double-walled: at grazing angles the view ray crosses more material, so
    // the silhouette brightens by itself — a fresnel read with no fresnel term.
    // This is why the column does not need the reference's Fresnel edge fade in
    // an isometric camera; in VR, where the viewer can put their face next to
    // it, it would.
    cfg.tubeSingleSided = false;
    cfg.tubeNoiseAmp = k_columnNoise[c->kind] * s_columnNoiseMul;

    cfg.layers = c->layers;
    cfg.layerCount = 2;
    cfg.uvMetresPerTile = (s_columnTile > 0.05f) ? s_columnTile : 0.05f;
    cfg.uvScrollSpeed = k_columnScroll[c->kind] * s_columnScrollMul;
    cfg.blendMode = (c->kind == VFX_COLUMN_FIRE) ? BLEND_ADDITIVE : BLEND_ALPHA;
    cfg.useCustomBlendMode = true; // BLEND_ALPHA is 0 and cannot be detected by > 0

    // A STATIONARY emitter still has to lay nodes, and it does — but not
    // because the emitter moved. The last node RISES away from the source under
    // the force field, so the gap to it grows and the next node is laid.
    //
    // No node laying at all: the path is seeded whole and frozen below, so
    // minVertexDistance has nothing to gate. It stays 0 deliberately.
    cfg.minVertexDistance = 0.0f;
    cfg.sampleHz = 60.0f;
    cfg.idleSpeed = 0.0f; // never treat a still source as idle — that is the normal state
    cfg.trailLength = (float)cfg.tubeMaxRings;
    cfg.disableInnerCore = true;
    cfg.ownerTag = SMOKE_COLUMN_TAG_BASE | slot;
    cfg.priority = VFX_PRIORITY_LOW;


    // ONE LINE PER SPAWN, unconditional. "It looks the same as before" and "the
    // new path never ran" are indistinguishable on screen.
    int id = SpawnTrailEntity(cfg);
    if (id >= 0)
    {
        // THE COLUMN, IN ONE CALL. A fixed vertical segment from the source
        // upward, seeded whole and frozen — so it is at full height on frame
        // one (the simulated version took seconds to fill, which read as a
        // delayed spawn) and it cannot drift anywhere.
        //
        // Frozen stops the GEOMETRY, not the clocks: UpdateTrailSystem keeps
        // advancing uvScrollOffset, DrawLayeredTube feeds that to the deform as
        // noiseOffset, and core/deform moves the churn along the body. All of
        // the motion is there and none of it is simulation.
        Vector3 top = (Vector3){c->pos.x, c->pos.y + height, c->pos.z};
        Trail_SetStaticPath(id, c->pos, top, cfg.tubeMaxRings);

        // READ IT BACK. Every failure this composition has shipped was a silent
        // early-return that left a plausible-looking wrong result, and none of
        // them was visible in a log that only printed the INPUTS. Assert the
        // effect instead: if the path did not take, say so at WARNING with the
        // reason, because a collapsed tube and a correct one differ by nothing
        // an input line can show.
        const TrailEntity *chk = GetTrail(id);
        int laid = (chk != NULL) ? chk->historyCount : -1;
        if (laid != cfg.tubeMaxRings)
            TraceLog(LOG_WARNING,
                     "VFX_SMOKE_COLUMN: static path did NOT take — asked %d nodes, "
                     "trail has %d. The tube will collapse to a flat quad. Check "
                     "cfg.type == TRAIL_TYPE_FOLLOWER (Trail_SetStaticPath returns "
                     "silently otherwise).",
                     cfg.tubeMaxRings, laid);
    }

    // ONE LINE PER SPAWN, unconditional. "It looks the same as before" and "the
    // new path never ran" are indistinguishable on screen.
    TraceLog(LOG_INFO,
             "VFX_SMOKE_COLUMN: slot %d — kind %d, %s, STATIC PATH %.2f m over "
             "%d rings, TUBE %d radial | churn %.2f, scroll %.2f tiles/s, "
             "tile %.2f m, sheet id %u, nodes %d",
             slot, (int)c->kind, funnel ? "FUNNEL" : "CYLINDER", height,
             cfg.tubeMaxRings, cfg.tubeRadialSegs, cfg.tubeNoiseAmp,
             cfg.uvScrollSpeed, cfg.uvMetresPerTile,
             (unsigned)s_columnSheet[c->kind].id,
             (GetTrail(id) != NULL) ? GetTrail(id)->historyCount : -1);
    return id;
}

// ── Public API ──────────────────────────────────────────────────────────────

int VFX_ComposeSmokeColumn(Vector3 pos, VC_MaterialId mat, float radius,
                           float height, VFX_ColumnKind kind, bool funnel)
{
    SmokeColumn_EnsureTuning();
    SmokeColumn_InitShared();

    // Range-check against the COUNT sentinel, never the last kind by name, and
    // announce the clamp: a silent one gives a plausible wrong result.
    if ((int)kind < 0 || (int)kind >= VFX_COLUMN_KIND_COUNT)
    {
        TraceLog(LOG_WARNING,
                 "VFX_SMOKE_COLUMN: kind %d out of range [0,%d) — clamped to SMOKE",
                 (int)kind, (int)VFX_COLUMN_KIND_COUNT);
        kind = VFX_COLUMN_SMOKE;
    }

    int slot = -1;
    for (int i = 0; i < VFX_SMOKE_COLUMN_MAX; i++)
        if (!s_columns[i].active) { slot = i; break; }
    if (slot < 0)
    {
        TraceLog(LOG_WARNING, "VFX_SMOKE_COLUMN: pool full (%d) — request dropped",
                 VFX_SMOKE_COLUMN_MAX);
        return -1;
    }

    VC_SmokeColumn *c = &s_columns[slot];
    *c = (VC_SmokeColumn){0};
    c->active = true;
    c->pos = pos;
    c->matId = mat;
    c->kind = kind;
    c->radius = (radius > 0.01f) ? radius : 0.35f;
    c->serial = ++s_columnSerial;

    SmokeColumn_ConfigureLayers(c);
    SmokeColumn_BuildShape(c, funnel);
    c->trailId = SmokeColumn_Spawn(c, slot, height, funnel);
    if (c->trailId < 0) { c->active = false; return -1; }

    return (s_columnSerial << 8) | slot;
}

// Stops the FEED. The laid material keeps rising and fades on its own, which is
// what smoke does — cutting it out of existence pops.
void VFX_SmokeColumn_Stop(int handle)
{
    int slot = handle & 0xFF;
    if (handle < 0 || slot >= VFX_SMOKE_COLUMN_MAX) return;
    VC_SmokeColumn *c = &s_columns[slot];
    if (!c->active || (handle >> 8) != c->serial) return;
    // A frozen path has no feed to stop, so there is nothing to drain: the
    // column ends when it is killed. Kept as _Stop rather than _Kill so the
    // call site does not change if a fed variant is ever added.
    c->stopping = true;
}

static void VC_SmokeColumn_Update(float dt)
{
    for (int i = 0; i < VFX_SMOKE_COLUMN_MAX; i++)
    {
        VC_SmokeColumn *c = &s_columns[i];
        if (!c->active) continue;
        c->elapsed += dt;

        TrailEntity *t = (c->trailId >= 0) ? GetTrail(c->trailId) : NULL;
        if (t == NULL)
        {
            c->active = false;
            continue;
        }

        // Re-push every live knob so a tuning.cfg reload takes effect without a
        // respawn.
        SmokeColumn_ConfigureLayers(c);
        t->tubeNoiseAmp = k_columnNoise[c->kind] * s_columnNoiseMul;
        t->uvScrollSpeed = k_columnScroll[c->kind] * s_columnScrollMul;
        t->uvMetresPerTile = (s_columnTile > 0.05f) ? s_columnTile : 0.05f;

        if (c->stopping)
        {
            KillTrail(c->trailId);
            c->trailId = -1;
            c->active = false;
        }
    }
}

// Empty ON PURPOSE, and it must exist. The TrailSystem owns the draw — a column
// is a TrailEntity, and main.c's DrawTrailEntitiesBody() puts it on screen. But
// the Update/Draw3D PAIR is how a managed composition declares itself to
// scripts/sync_vfx_test.py: with only an Update on disk the generator files this
// as a plain helper, moves the include to common.inl, and emits no update
// dispatch at all — so the pool is never pumped and the column never rises.
// That happened once while wiring this file. Same reason vc_volume_trail.inl
// carries the identical stub.
static void VC_SmokeColumn_Draw3D(Camera3D cam)
{
    (void)cam;
}
