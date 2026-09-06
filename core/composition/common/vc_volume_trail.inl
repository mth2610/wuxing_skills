// ── VFX_ComposeVolumeTrail — Unified Primary 3D Swept Volume Trail ──────────
//
// General-purpose primary volumetric trail following an emitter transform.
// Sweeps a 3D procedural tube mesh (PMTubeConfig + trail_volume.fs) to simulate:
//   - VFX_VOLUME_SMOKE  (0): Billowing absorbing smoke wake / dark miasma / dust
//   - VFX_VOLUME_FIRE   (1): Turbulent blazing fire plume / flame wake
//   - VFX_VOLUME_STEAM  (2): Dispersing water vapor / steam / cold mist
//   - VFX_VOLUME_ENERGY (3): Radiant mana beam / plasma wake with white-hot core
//
// Replaces the legacy droplet prototype and supersedes vc_smoke_trail.inl into
// a single canonical, high-performance primary VFX component.

#include "core/deform/mesh_deform.h"
#include "core/tuning.h"

#define VFX_VOLUME_TRAIL_MAX 8
#define VOLUME_TRAIL_TAG_BASE 0x564C0000 /* 'VL' */

typedef struct {
    bool active;
    bool stopping;
    int trailId;
    int serial;
    Vector3 pos;
    VC_MaterialId matId;
    VFX_VolumeKind kind;
    float radius;
    float lifetime;
    TrailLayer layers[2];
    PMTubeConfig tube;     // owned procedural tube geometry config
    MeshDeformField churn; // owned 3D noise deform field
} VC_VolumeTrail;

static VC_VolumeTrail s_volumeTrails[VFX_VOLUME_TRAIL_MAX];
static int s_volumeTrailSerial = 0;
static bool s_volumeTrailInit = false;

// Canonical surface sheets for each volume kind
static const VFX_SurfaceId k_volumeTrailSurface[VFX_VOLUME_KIND_COUNT] = {
    [VFX_VOLUME_SMOKE]  = VFX_SURFACE_VOLUME_SMOKE,
    [VFX_VOLUME_FIRE]   = VFX_SURFACE_VOLUME_FIRE,
    [VFX_VOLUME_STEAM]  = VFX_SURFACE_VOLUME_STEAM,
    [VFX_VOLUME_ENERGY] = VFX_SURFACE_VOLUME_SMOKE, // Reuses smoke texture with volumetric plasma optics
};
static Texture2D s_volumeTrailSheet[VFX_VOLUME_KIND_COUNT];

static const float k_volumeTrailNoise[VFX_VOLUME_KIND_COUNT]  = {0.34f, 0.30f, 0.22f, 0.34f};
static const float k_volumeTrailScroll[VFX_VOLUME_KIND_COUNT] = {0.55f, 0.95f, 0.40f, 0.85f};

// Live tuning knobs
static float s_volTrailNoiseMul = 1.0f;
static float s_volTrailScrollMul = 1.0f;
static float s_volTrailAlphaMul = 1.0f;
static float s_volTrailRadiusMul = 1.0f;
static float s_volTrailTile = 3.00f;
static float s_volTrailSwirlAmp = 0.22f;
static float s_volTrailSwirlSpeed = 2.5f;
static float s_volTrailKind = -1.0f;
static float s_volTrailMat = -1.0f;

static void VolumeTrail_EnsureTuning(void)
{
    static bool done = false;
    if (done) return;
    done = true;
    Tuning_RegisterFloat("volumetrail_noise", &s_volTrailNoiseMul, 1.0f);
    Tuning_RegisterFloat("volumetrail_scroll", &s_volTrailScrollMul, 1.0f);
    Tuning_RegisterFloat("volumetrail_alpha", &s_volTrailAlphaMul, 1.0f);
    Tuning_RegisterFloat("volumetrail_radius", &s_volTrailRadiusMul, 1.0f);
    Tuning_RegisterFloat("volumetrail_tile", &s_volTrailTile, 3.00f);
    Tuning_RegisterFloat("volumetrail_swirl_amp", &s_volTrailSwirlAmp, 0.22f);
    Tuning_RegisterFloat("volumetrail_swirl_speed", &s_volTrailSwirlSpeed, 2.5f);
    Tuning_RegisterFloat("volumetrail_kind", &s_volTrailKind, -1.0f);
    Tuning_RegisterFloat("volumetrail_mat", &s_volTrailMat, -1.0f);

    // Aliases to preserve compatibility with existing tuning.cfg
    Tuning_RegisterFloat("smoketrail2_noise", &s_volTrailNoiseMul, 1.0f);
    Tuning_RegisterFloat("smoketrail2_scroll", &s_volTrailScrollMul, 1.0f);
    Tuning_RegisterFloat("smoketrail2_alpha", &s_volTrailAlphaMul, 1.0f);
    Tuning_RegisterFloat("smoketrail2_radius", &s_volTrailRadiusMul, 1.0f);
    Tuning_RegisterFloat("smoketrail2_tile", &s_volTrailTile, 3.00f);
    Tuning_RegisterFloat("smoketrail2_swirl_amp", &s_volTrailSwirlAmp, 0.22f);
    Tuning_RegisterFloat("smoketrail2_swirl_speed", &s_volTrailSwirlSpeed, 2.5f);
    Tuning_RegisterFloat("smoketrail2_kind", &s_volTrailKind, -1.0f);
    Tuning_RegisterFloat("smoketrail2_mat", &s_volTrailMat, -1.0f);
}

static void VolumeTrail_InitShared(void)
{
    if (s_volumeTrailInit) return;
    s_volumeTrailInit = true;
    for (int k = 0; k < VFX_VOLUME_KIND_COUNT; k++)
    {
        const VFX_SurfaceProfile *p = VFX_SurfaceRegistry_Get(k_volumeTrailSurface[k]);
        s_volumeTrailSheet[k] = (p != NULL) ? p->body : (Texture2D){0};
    }
}

static void VolumeTrail_ConfigureLayers(VC_VolumeTrail *c)
{
    const Texture2D *sheet = &s_volumeTrailSheet[c->kind];
    if (c->kind == VFX_VOLUME_ENERGY)
    {
        // Primary glowing plasma body (100% elemental material hue, shader produces white-hot core)
        c->layers[0] = (TrailLayer){
            .widthMul = 1.0f,
            .alphaMul = 0.80f * s_volTrailAlphaMul,
            .whiten = 0.0f,
            .scrollMul = 1.0f,
            .texture = sheet,
        };
        // Secondary subtle counter-swirling vapor layer for organic depth
        c->layers[1] = (TrailLayer){
            .widthMul = 0.80f,
            .alphaMul = 0.35f * s_volTrailAlphaMul,
            .whiten = 0.05f,
            .scrollMul = 1.40f,
            .texture = sheet,
        };
        return;
    }
    c->layers[0] = (TrailLayer){
        .widthMul = 1.0f,
        .alphaMul = 0.85f * s_volTrailAlphaMul,
        .whiten = 0.0f,
        .scrollMul = 1.0f,
        .texture = sheet,
    };
    c->layers[1] = (TrailLayer){
        .widthMul = 0.72f,
        .alphaMul = 0.55f * s_volTrailAlphaMul,
        .whiten = 0.22f,
        .scrollMul = 1.85f,
        .texture = sheet,
    };
}

static void VolumeTrail_BuildShape(VC_VolumeTrail *c, bool funnel)
{
    c->tube = PMTube_DefaultConfig();

    if (c->kind == VFX_VOLUME_ENERGY)
    {
        c->tube.wobbleAmplitude = 0.12f;
        c->tube.wobbleFrequency = 1.4f;
        c->tube.wobbleSpeed = s_volTrailSwirlSpeed * 1.0f;

        // Helical corkscrew swirl (vortex motion):
        // Flowing spirals for energetic plasma vapor
        c->tube.deform1Amp = s_volTrailSwirlAmp * 1.1f;
        c->tube.deform1FreqT = 6.0f;
        c->tube.deform1FreqPhi = 1.0f;
        c->tube.deform1Speed = s_volTrailSwirlSpeed * 1.3f;

        // Secondary counter-rotating twist to break drill-bit symmetry
        c->tube.deform2Amp = s_volTrailSwirlAmp * 0.45f;
        c->tube.deform2FreqT = 12.0f;
        c->tube.deform2FreqPhi = 2.0f;
        c->tube.deform2Speed = -s_volTrailSwirlSpeed * 1.5f;
    }
    else
    {
        c->tube.wobbleAmplitude = 0.12f;
        c->tube.wobbleFrequency = 1.2f;
        c->tube.wobbleSpeed = s_volTrailSwirlSpeed * 0.7f;

        c->tube.deform1Amp = s_volTrailSwirlAmp;
        c->tube.deform1FreqT = 7.0f;
        c->tube.deform1FreqPhi = 1.0f;
        c->tube.deform1Speed = s_volTrailSwirlSpeed;

        c->tube.deform2Amp = s_volTrailSwirlAmp * 0.40f;
        c->tube.deform2FreqT = 14.0f;
        c->tube.deform2FreqPhi = 2.0f;
        c->tube.deform2Speed = -s_volTrailSwirlSpeed * 1.3f;
    }

    if (c->kind == VFX_VOLUME_ENERGY)
    {
        // Sleek aerodynamic taper for energy beam/projectile: sharp piercing head
        if (funnel) { c->tube.radiusTailFrac = 0.14f; c->tube.radiusPow = 1.65f; }
        else        { c->tube.radiusTailFrac = 0.40f; c->tube.radiusPow = 1.25f; }
    }
    else
    {
        if (funnel) { c->tube.radiusTailFrac = 0.22f; c->tube.radiusPow = 1.40f; }
        else        { c->tube.radiusTailFrac = 0.70f; c->tube.radiusPow = 1.15f; }
    }

    // Crucial for moving trails: emitter is at t=0 of the swept path
    c->tube.anchorAtTail = true;
    c->tube.useTransportFrame = true;

    MeshDeform_Clear(&c->churn);
    c->churn.amplitude = 1.0f;
    c->churn.timeScale = (c->kind == VFX_VOLUME_ENERGY) ? 1.15f : 0.75f;
    c->churn.latticeAround = 3;
    c->churn.latticeAlong = 3;
    MeshDeform_AddLayer(&c->churn, (MeshDeformLayer){
        .kind = MESH_DEFORM_NOISE_CHANNEL,
        .direction = MESH_DEFORM_DIR_NORMAL_SCALE,
        .tiling = {1.0f, 1.0f}, .amplitude = (c->kind == VFX_VOLUME_ENERGY) ? 1.8f : 4.2f, .speed = 1.0f,
        .latticeMul = 1.0f, .latticeAroundMul = 1.0f, .env = UV_ENV_HEAD_WELD,
        .envStart = 0.0f, .envEnd = 0.22f,
    });
    MeshDeform_AddLayer(&c->churn, (MeshDeformLayer){
        .kind = MESH_DEFORM_NOISE_CHANNEL,
        .direction = MESH_DEFORM_DIR_NORMAL_OFFSET,
        .tiling = {1.0f, 1.9f}, .amplitude = (c->kind == VFX_VOLUME_ENERGY) ? 0.70f : 1.30f, .speed = 1.7f,
        .timeOffset = 11.0f, .latticeMul = 3.0f, .latticeAroundMul = 2.0f,
        .env = UV_ENV_HEAD_WELD_SQ, .envStart = 0.0f, .envEnd = 0.35f,
    });
    c->tube.noiseField = &c->churn;
    c->tube.centerlineAmp = (c->kind == VFX_VOLUME_ENERGY) ? (c->radius * 0.85f) : (c->radius * 1.6f);
    c->tube.noiseWavelength = 5.0f;
    c->tube.noiseOffsetScrollMul = 0.0f;
}

static int VolumeTrail_Spawn(VC_VolumeTrail *c, int slot, const Matrix *followTransform)
{
    TrailConfig cfg = {0};
    cfg.type = TRAIL_TYPE_FOLLOWER;
    cfg.pos = c->pos;
    cfg.life = 1.0e6f; // Long-lived: dies when caller stops it and the feed drains
    cfg.thick = c->radius * s_volTrailRadiusMul;
    cfg.tint = WHITE;
    cfg.gradient = NULL;
    const VFX_ElementMaterial *m = VFX_Material(c->matId);
    if (m != NULL)
    {
        if (c->kind == VFX_VOLUME_ENERGY || c->kind == VFX_VOLUME_FIRE)
            cfg.tint = m->glow;
        else
            cfg.tint = m->body;
    }

    cfg.forceField = NULL;
    cfg.widthEnvelope = TRAIL_WIDTH_ENVELOPE_UNIFORM;

    cfg.shape = TRAIL_SHAPE_TUBE;
    cfg.tubeShapeConfig = &c->tube;
    cfg.tubeRadialSegs = 16;
    cfg.tubeMaxRings = VC_TrailNodesForLifetime(c->lifetime, 60.0f);
    cfg.tubeGeomSegs = 24;
    cfg.tubeSingleSided = false;
    cfg.tubeVolumeShading = true;
    cfg.tubeDeformFrozen = false;
    cfg.tubeNoiseAmp = k_volumeTrailNoise[c->kind] * s_volTrailNoiseMul;

    cfg.layers = c->layers;
    cfg.layerCount = 2;
    cfg.uvMetresPerTile = (s_volTrailTile > 0.05f) ? s_volTrailTile : 0.05f;
    cfg.uvScrollSpeed = k_volumeTrailScroll[c->kind] * s_volTrailScrollMul;
    cfg.blendMode = (c->kind == VFX_VOLUME_FIRE || c->kind == VFX_VOLUME_ENERGY) ? BLEND_ADDITIVE : BLEND_ALPHA;
    cfg.useCustomBlendMode = true;

    cfg.minVertexDistance = 0.005f;
    cfg.sampleHz = 60.0f;
    cfg.idleSpeed = 0.10f;
    cfg.teleportSpeed = 45.0f;
    cfg.trailLength = (float)cfg.tubeMaxRings;
    cfg.disableInnerCore = true;
    cfg.ownerTag = VOLUME_TRAIL_TAG_BASE | slot;
    cfg.priority = VFX_PRIORITY_LOW;

    int id = SpawnTrailEntity(cfg);
    if (id >= 0)
        Trail_AttachToTransform(id, followTransform, (Vector3){0.0f, 0.0f, 0.0f});

    TraceLog(LOG_INFO,
             "VFX_VOLUME_TRAIL: slot %d — kind %d, FOLLOWING, tail %.2f s over "
             "%d nodes, MESH %d x %d radial | churn %.2f, scroll %.2f tiles/s, "
             "tile %.2f m, sheet id %u",
             slot, (int)c->kind, c->lifetime, cfg.tubeMaxRings,
             cfg.tubeGeomSegs, cfg.tubeRadialSegs,
             cfg.tubeNoiseAmp, cfg.uvScrollSpeed, cfg.uvMetresPerTile,
             (unsigned)s_volumeTrailSheet[c->kind].id);
    return id;
}

// ── Public API ──────────────────────────────────────────────────────────────

int VFX_ComposeVolumeTrail(const Matrix *followTransform, VC_MaterialId mat,
                           float radius, float lifetime, VFX_VolumeKind kind, bool funnel)
{
    VolumeTrail_EnsureTuning();
    VolumeTrail_InitShared();

    if (!followTransform)
    {
        TraceLog(LOG_WARNING, "VFX_VOLUME_TRAIL: NULL transform — no trail created");
        return -1;
    }
    if (s_volTrailKind >= 0.0f && (int)s_volTrailKind < VFX_VOLUME_KIND_COUNT)
        kind = (VFX_VolumeKind)(int)s_volTrailKind;
    if (s_volTrailMat >= 0.0f && (int)s_volTrailMat < VC_MAT_COUNT)
        mat = (VC_MaterialId)(int)s_volTrailMat;

    if ((int)kind < 0 || (int)kind >= VFX_VOLUME_KIND_COUNT)
    {
        TraceLog(LOG_WARNING,
                 "VFX_VOLUME_TRAIL: kind %d out of range [0,%d) — clamped to SMOKE",
                 (int)kind, (int)VFX_VOLUME_KIND_COUNT);
        kind = VFX_VOLUME_SMOKE;
    }

    int slot = -1;
    for (int i = 0; i < VFX_VOLUME_TRAIL_MAX; i++)
        if (!s_volumeTrails[i].active) { slot = i; break; }
    if (slot < 0)
    {
        TraceLog(LOG_WARNING, "VFX_VOLUME_TRAIL: pool full (%d) — request dropped",
                 VFX_VOLUME_TRAIL_MAX);
        return -1;
    }

    VC_VolumeTrail *c = &s_volumeTrails[slot];
    *c = (VC_VolumeTrail){0};
    c->active = true;
    c->pos = Vector3Transform((Vector3){0.0f, 0.0f, 0.0f}, *followTransform);
    c->matId = mat;
    c->kind = kind;
    c->radius = (radius > 0.01f) ? radius : 0.22f;
    c->lifetime = (lifetime > 0.05f) ? lifetime : 0.5f;
    c->serial = ++s_volumeTrailSerial;

    VolumeTrail_ConfigureLayers(c);
    VolumeTrail_BuildShape(c, funnel);
    c->trailId = VolumeTrail_Spawn(c, slot, followTransform);
    if (c->trailId < 0) { c->active = false; return -1; }

    return (s_volumeTrailSerial << 8) | slot;
}

int VFX_ComposeVolumeTrailEx(const Matrix *followTransform, VC_MaterialId mat,
                             float radius, float lifetime, VFX_VolumeKind kind,
                             const VFX_TrailSurface *surface)
{
    (void)surface;
    return VFX_ComposeVolumeTrail(followTransform, mat, radius, lifetime, kind, true);
}

void VFX_VolumeTrail_Stop(int handle)
{
    int slot = handle & 0xFF;
    if (handle < 0 || slot >= VFX_VOLUME_TRAIL_MAX) return;
    VC_VolumeTrail *c = &s_volumeTrails[slot];
    if (!c->active || (handle >> 8) != c->serial) return;
    c->stopping = true;
}

void VFX_KillVolumeTrail(int handle)
{
    VFX_VolumeTrail_Stop(handle);
}

// Compatibility wrapper for callers using legacy SmokeTrail function name
int VFX_ComposeSmokeTrail(const Matrix *followTransform, VC_MaterialId mat,
                          float radius, float lifetime, VFX_ColumnKind kind, bool funnel)
{
    return VFX_ComposeVolumeTrail(followTransform, mat, radius, lifetime, (VFX_VolumeKind)kind, funnel);
}

void VFX_SmokeTrail_Stop(int handle)
{
    VFX_VolumeTrail_Stop(handle);
}

static void VC_VolumeTrail_Update(float dt)
{
    for (int i = 0; i < VFX_VOLUME_TRAIL_MAX; i++)
    {
        VC_VolumeTrail *c = &s_volumeTrails[i];
        if (!c->active) continue;

        TrailEntity *t = (c->trailId >= 0) ? GetTrail(c->trailId) : NULL;
        if (t == NULL)
        {
            c->active = false;
            continue;
        }

        if (s_volTrailKind >= 0.0f && (int)s_volTrailKind < VFX_VOLUME_KIND_COUNT)
        {
            VFX_VolumeKind targetKind = (VFX_VolumeKind)(int)s_volTrailKind;
            if (c->kind != targetKind)
            {
                c->kind = targetKind;
                t->blendMode = (c->kind == VFX_VOLUME_FIRE || c->kind == VFX_VOLUME_ENERGY) ? BLEND_ADDITIVE : BLEND_ALPHA;
                const VFX_ElementMaterial *m = VFX_Material(c->matId);
                if (m != NULL)
                {
                    Color baseCol = (c->kind == VFX_VOLUME_ENERGY || c->kind == VFX_VOLUME_FIRE) ? m->glow : m->body;
                    t->tint = baseCol;
                }
            }
        }

        VolumeTrail_ConfigureLayers(c);
        t->tubeNoiseAmp = k_volumeTrailNoise[c->kind] * s_volTrailNoiseMul;
        t->uvScrollSpeed = k_volumeTrailScroll[c->kind] * s_volTrailScrollMul;
        t->uvMetresPerTile = (s_volTrailTile > 0.05f) ? s_volTrailTile : 0.05f;

        if (c->stopping)
        {
            KillTrail(c->trailId);
            c->trailId = -1;
            c->active = false;
        }
    }
    (void)dt;
}

static void VC_VolumeTrail_Draw3D(Camera3D cam)
{
    (void)cam;
}
