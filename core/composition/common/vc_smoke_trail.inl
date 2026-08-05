// H2 — SmokeTrail: the MOVING counterpart of vc_smoke_column.inl's SmokeColumn.
//
// SPLIT INTO ITS OWN FILE, not a mode flag on VC_SmokeColumn — two archetypes,
// two files, same reasoning as the column/vc_volume_trail.inl split it mirrors.
//
// EVERYTHING SHAPE-RELATED IS A VERBATIM COPY of SmokeColumn_ConfigureLayers/
// BuildShape — same textures, same noise churn, same funnel taper direction
// (radiusTailFrac 0.12/0.55, radiusPow 1.7/1.4, untouched), same blend law.
// Deliberately NOT reworked or inverted: the shape is the column's, proven
// and already tuned: the ONLY thing this file changes is how the path is fed
// to it. A stationary column and a moving trail should look like the same
// material behaving two different ways, not two different materials.
//
// THE ONE DIFFERENCE: SmokeColumn_Spawn ends with `Trail_SetStaticPath` — a
// frozen vertical segment, seeded once (see vc_smoke_column.inl's own header
// for why: a real ForceField-driven moving column was tried once and cost
// three bugs in a row before being abandoned). This file ends with
// `Trail_AttachToTransform` instead — the exact mechanism
// vc_volume_trail.inl uses for a moving emitter: the trail system itself
// lays new history nodes as the caller's transform moves, and drops old ones
// past `lifetime`. Real trail-follower behaviour, not a frozen shape dragged
// around.

#include "core/deform/mesh_deform.h"
#include "core/tuning.h"

#define VFX_SMOKE_TRAIL_MAX 6
#define SMOKE_TRAIL_TAG_BASE 0x5D000

typedef struct {
    bool active;
    bool stopping;
    int trailId;
    int serial;
    Vector3 pos;
    VC_MaterialId matId;
    VFX_ColumnKind kind;
    float radius;
    float lifetime;
    TrailLayer layers[2];
    PMTubeConfig tube;     // owned: the SHAPE, not the trail's business
    MeshDeformField churn; // owned: pointed at by tube.noiseField
} VC_SmokeTrail;

static VC_SmokeTrail s_smokeTrails[VFX_SMOKE_TRAIL_MAX];
static int s_smokeTrailSerial = 0;
static bool s_smokeTrailInit = false;

// Same surfaces as the column — the sheet is a taste choice per kind, not a
// property of how the path is driven.
static const VFX_SurfaceId k_smokeTrailSurface[VFX_COLUMN_KIND_COUNT] = {
    [VFX_COLUMN_SMOKE] = VFX_SURFACE_VOLUME_SMOKE,
    [VFX_COLUMN_FIRE] = VFX_SURFACE_VOLUME_FIRE,
    [VFX_COLUMN_STEAM] = VFX_SURFACE_VOLUME_STEAM,
};
static Texture2D s_smokeTrailSheet[VFX_COLUMN_KIND_COUNT];

static const float k_smokeTrailNoise[VFX_COLUMN_KIND_COUNT] = {0.34f, 0.30f, 0.22f};
static const float k_smokeTrailScroll[VFX_COLUMN_KIND_COUNT] = {0.55f, 0.95f, 0.40f};

// Live knobs, own namespace from the column's (smokecolumn_*) so tuning one
// archetype never silently retunes the other. Registered lazily on first use
// — never from an Init, Tuning_Init runs after subsystem inits in main.c
// (core/docs/LANDMINES.md).
static float s_smokeTrailNoiseMul = 1.0f;
static float s_smokeTrailScrollMul = 1.0f;
static float s_smokeTrailAlphaMul = 1.0f;
static float s_smokeTrailTile = 3.00f;
static float s_smokeTrailFreezeDeform = 0.0f;

static void SmokeTrail_EnsureTuning(void)
{
    static bool done = false;
    if (done) return;
    done = true;
    Tuning_RegisterFloat("smoketrail2_noise", &s_smokeTrailNoiseMul, 1.0f);
    Tuning_RegisterFloat("smoketrail2_scroll", &s_smokeTrailScrollMul, 1.0f);
    Tuning_RegisterFloat("smoketrail2_alpha", &s_smokeTrailAlphaMul, 1.0f);
    Tuning_RegisterFloat("smoketrail2_tile", &s_smokeTrailTile, 3.00f);
    Tuning_RegisterFloat("smoketrail2_freeze", &s_smokeTrailFreezeDeform, 0.0f);
}

static void SmokeTrail_InitShared(void)
{
    if (s_smokeTrailInit) return;
    s_smokeTrailInit = true;
    for (int k = 0; k < VFX_COLUMN_KIND_COUNT; k++)
    {
        const VFX_SurfaceProfile *p = VFX_SurfaceRegistry_Get(k_smokeTrailSurface[k]);
        s_smokeTrailSheet[k] = (p != NULL) ? p->body : (Texture2D){0};
    }
}

// Verbatim copy of SmokeColumn_ConfigureLayers.
static void SmokeTrail_ConfigureLayers(VC_SmokeTrail *c)
{
    const Texture2D *sheet = &s_smokeTrailSheet[c->kind];
    c->layers[0] = (TrailLayer){
        .widthMul = 1.0f,
        .alphaMul = 0.85f * s_smokeTrailAlphaMul,
        .whiten = 0.0f,
        .scrollMul = 1.0f,
        .texture = sheet,
    };
    c->layers[1] = (TrailLayer){
        .widthMul = 0.72f,
        .alphaMul = 0.55f * s_smokeTrailAlphaMul,
        .whiten = 0.22f,
        .scrollMul = 1.85f,
        .texture = sheet,
    };
}

// Verbatim copy of SmokeColumn_BuildShape — same taper direction, same churn.
// See this file's header for why nothing here is inverted or retuned.
static void SmokeTrail_BuildShape(VC_SmokeTrail *c, bool funnel)
{
    c->tube = PMTube_DefaultConfig();

    c->tube.wobbleAmplitude = 0.0f;
    c->tube.deform1Amp = 0.0f;
    c->tube.deform2Amp = 0.0f;

    // r(t) = tailFrac + (1 - tailFrac) * t^p, tail = the base — same numbers
    // as vc_smoke_column.inl's SmokeColumn_BuildShape, unchanged.
    if (funnel) { c->tube.radiusTailFrac = 0.12f; c->tube.radiusPow = 1.7f; }
    else        { c->tube.radiusTailFrac = 0.55f; c->tube.radiusPow = 1.4f; }

    c->tube.centerlineAmp = c->radius * 1.6f;
    c->tube.useTransportFrame = true;

    MeshDeform_Clear(&c->churn);
    c->churn.amplitude = 1.0f;
    c->churn.timeScale = 0.75f;
    c->churn.latticeAround = 3;
    c->churn.latticeAlong = 3;

    MeshDeform_AddLayer(&c->churn, (MeshDeformLayer){
        .kind = MESH_DEFORM_NOISE_CHANNEL,
        .direction = MESH_DEFORM_DIR_NORMAL_SCALE,
        .tiling = {1.0f, 1.0f}, .amplitude = 4.2f, .speed = 1.0f,
        .latticeMul = 1.0f, .latticeAroundMul = 1.0f, .env = UV_ENV_HEAD_WELD,
        .envStart = 0.0f, .envEnd = 0.22f,
    });
    MeshDeform_AddLayer(&c->churn, (MeshDeformLayer){
        .kind = MESH_DEFORM_NOISE_CHANNEL,
        .direction = MESH_DEFORM_DIR_NORMAL_OFFSET,
        .tiling = {1.0f, 1.9f}, .amplitude = 1.30f, .speed = 1.7f,
        .timeOffset = 11.0f, .latticeMul = 3.0f, .latticeAroundMul = 2.0f,
        .env = UV_ENV_HEAD_WELD_SQ, .envStart = 0.0f, .envEnd = 0.35f,
    });
    c->tube.noiseField = &c->churn;
}

static int SmokeTrail_Spawn(VC_SmokeTrail *c, int slot, const Matrix *followTransform)
{
    TrailConfig cfg = {0};
    cfg.type = TRAIL_TYPE_FOLLOWER;
    cfg.pos = c->pos;
    // Long-lived by construction, same as the column and vc_volume_trail.inl:
    // dies when the caller stops it and the feed drains.
    cfg.life = 1.0e6f;
    cfg.thick = c->radius;
    cfg.tint = WHITE;
    cfg.gradient = NULL; // the material carries the colour
    const VFX_ElementMaterial *m = VFX_Material(c->matId);
    if (m != NULL) cfg.tint = m->body;

    // NULL — not a simulation, same reasoning as the column: a cloth step
    // here would make the tube writhe like a snake.
    cfg.forceField = NULL;
    // UNIFORM always — the funnel lives in the tube profile (radiusTailFrac
    // above), not the envelope. Verbatim copy of the column's reasoning.
    cfg.widthEnvelope = TRAIL_WIDTH_ENVELOPE_UNIFORM;

    cfg.shape = TRAIL_SHAPE_TUBE;
    cfg.tubeShapeConfig = &c->tube;
    cfg.tubeRadialSegs = 16;
    // Ring buffer sized off the tail memory asked for — same conversion
    // vc_volume_trail.inl uses — capped at TRAIL_HISTORY_COUNT. NOT a fixed
    // 40 like the column: there is no fixed "height" here, the trail's
    // reach is however far the emitter has travelled in `lifetime` seconds.
    cfg.tubeMaxRings = VC_TrailNodesForLifetime(c->lifetime, 60.0f);
    // FALSE on purpose — trail_volume.fs drops the far wall by its normal
    // (`if (facing < 0.0) discard;`), not by winding: PMTube_DrawFaded's
    // winding is inward, so GL backface culling would keep the wrong side.
    cfg.tubeSingleSided = false;
    cfg.tubeVolumeShading = true;
    cfg.tubeDeformFrozen = (s_smokeTrailFreezeDeform > 0.5f);
    cfg.tubeNoiseAmp = k_smokeTrailNoise[c->kind] * s_smokeTrailNoiseMul;

    cfg.layers = c->layers;
    cfg.layerCount = 2;
    cfg.uvMetresPerTile = (s_smokeTrailTile > 0.05f) ? s_smokeTrailTile : 0.05f;
    cfg.uvScrollSpeed = k_smokeTrailScroll[c->kind] * s_smokeTrailScrollMul;
    cfg.blendMode = (c->kind == VFX_COLUMN_FIRE) ? BLEND_ADDITIVE : BLEND_ALPHA;
    cfg.useCustomBlendMode = true; // BLEND_ALPHA is 0 and cannot be detected by > 0

    // Real node laying — the whole point of this file. Same constants
    // vc_volume_trail.inl uses (VOL_MIN_VERTEX / VOL_IDLE_SPEED /
    // VOL_TELEPORT_SPEED): this is the same trail system, a different
    // shape/texture riding on it.
    cfg.minVertexDistance = 0.005f;
    cfg.sampleHz = 60.0f;
    cfg.idleSpeed = 0.10f;
    cfg.teleportSpeed = 45.0f;
    cfg.trailLength = (float)cfg.tubeMaxRings;
    cfg.disableInnerCore = true;
    cfg.ownerTag = SMOKE_TRAIL_TAG_BASE | slot;
    cfg.priority = VFX_PRIORITY_LOW;

    // ONE LINE PER SPAWN, unconditional — "it looks the same as before" and
    // "the new path never ran" are indistinguishable on screen.
    int id = SpawnTrailEntity(cfg);
    if (id >= 0)
        Trail_AttachToTransform(id, followTransform, (Vector3){0.0f, 0.0f, 0.0f});

    TraceLog(LOG_INFO,
             "VFX_SMOKE_TRAIL: slot %d — kind %d, FOLLOWING, tail %.2f s over "
             "%d rings, TUBE %d radial | churn %.2f, scroll %.2f tiles/s, "
             "tile %.2f m, sheet id %u",
             slot, (int)c->kind, c->lifetime, cfg.tubeMaxRings, cfg.tubeRadialSegs,
             cfg.tubeNoiseAmp, cfg.uvScrollSpeed, cfg.uvMetresPerTile,
             (unsigned)s_smokeTrailSheet[c->kind].id);
    return id;
}

// ── Public API ──────────────────────────────────────────────────────────────

int VFX_ComposeSmokeTrail(const Matrix *followTransform, VC_MaterialId mat,
                          float radius, float lifetime, VFX_ColumnKind kind, bool funnel)
{
    SmokeTrail_EnsureTuning();
    SmokeTrail_InitShared();

    if (!followTransform)
    {
        TraceLog(LOG_WARNING, "VFX_SMOKE_TRAIL: NULL transform — no trail created");
        return -1;
    }
    if ((int)kind < 0 || (int)kind >= VFX_COLUMN_KIND_COUNT)
    {
        TraceLog(LOG_WARNING,
                 "VFX_SMOKE_TRAIL: kind %d out of range [0,%d) — clamped to SMOKE",
                 (int)kind, (int)VFX_COLUMN_KIND_COUNT);
        kind = VFX_COLUMN_SMOKE;
    }

    int slot = -1;
    for (int i = 0; i < VFX_SMOKE_TRAIL_MAX; i++)
        if (!s_smokeTrails[i].active) { slot = i; break; }
    if (slot < 0)
    {
        TraceLog(LOG_WARNING, "VFX_SMOKE_TRAIL: pool full (%d) — request dropped",
                 VFX_SMOKE_TRAIL_MAX);
        return -1;
    }

    VC_SmokeTrail *c = &s_smokeTrails[slot];
    *c = (VC_SmokeTrail){0};
    c->active = true;
    c->pos = Vector3Transform((Vector3){0.0f, 0.0f, 0.0f}, *followTransform);
    c->matId = mat;
    c->kind = kind;
    c->radius = (radius > 0.01f) ? radius : 0.35f;
    c->lifetime = (lifetime > 0.05f) ? lifetime : 0.5f;
    c->serial = ++s_smokeTrailSerial;

    SmokeTrail_ConfigureLayers(c);
    SmokeTrail_BuildShape(c, funnel);
    c->trailId = SmokeTrail_Spawn(c, slot, followTransform);
    if (c->trailId < 0) { c->active = false; return -1; }

    return (s_smokeTrailSerial << 8) | slot;
}

// Stops the FEED. The laid material keeps drifting and fades on its own —
// same as the column, cutting it out of existence pops.
void VFX_SmokeTrail_Stop(int handle)
{
    int slot = handle & 0xFF;
    if (handle < 0 || slot >= VFX_SMOKE_TRAIL_MAX) return;
    VC_SmokeTrail *c = &s_smokeTrails[slot];
    if (!c->active || (handle >> 8) != c->serial) return;
    c->stopping = true;
}

static void VC_SmokeTrail_Update(float dt)
{
    for (int i = 0; i < VFX_SMOKE_TRAIL_MAX; i++)
    {
        VC_SmokeTrail *c = &s_smokeTrails[i];
        if (!c->active) continue;

        TrailEntity *t = (c->trailId >= 0) ? GetTrail(c->trailId) : NULL;
        if (t == NULL)
        {
            c->active = false;
            continue;
        }

        // Re-push every live knob so a tuning.cfg reload takes effect without
        // a respawn — same pattern as VC_SmokeColumn_Update.
        SmokeTrail_ConfigureLayers(c);
        t->tubeNoiseAmp = k_smokeTrailNoise[c->kind] * s_smokeTrailNoiseMul;
        bool freeze = (s_smokeTrailFreezeDeform > 0.5f);
        if (freeze != t->tubeDeformFrozen)
        {
            t->tubeDeformFrozen = freeze;
            TraceLog(LOG_INFO, "VFX_SMOKE_TRAIL: deform %s — sheet vẫn trượt",
                     freeze ? "ĐÓNG BĂNG (smoketrail2_freeze=1)" : "chạy lại");
        }
        t->uvScrollSpeed = k_smokeTrailScroll[c->kind] * s_smokeTrailScrollMul;
        t->uvMetresPerTile = (s_smokeTrailTile > 0.05f) ? s_smokeTrailTile : 0.05f;

        if (c->stopping)
        {
            KillTrail(c->trailId);
            c->trailId = -1;
            c->active = false;
        }
    }
    (void)dt;
}

// Empty ON PURPOSE, and it must exist — see vc_smoke_column.inl's identical
// stub for why: the Update/Draw3D PAIR is how a managed composition declares
// itself to scripts/sync_vfx_test.py, and main.c's DrawTrailEntitiesBody()
// already puts every TrailEntity on screen regardless of which composition
// spawned it.
static void VC_SmokeTrail_Draw3D(Camera3D cam)
{
    (void)cam;
}
