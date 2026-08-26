// ── VFX_ComposeFilamentTrail — the swept volume, strands inside it ──────────
//
// SAME MESH AND SAME UVs AS VFX_ComposeVolumeTrail, WHICH IT REPLACES. The
// swept droplet, its rings and caps, the path history, the metres-per-tile UV
// — all of it still comes from the trail system and PMDroplet_BuildAlongPath.
// Nothing about the shape is reimplemented; writing a second tube is the
// mistake this module's predecessor documented having made twice.
//
// WHAT CHANGES IS THE FRAGMENT STAGE, AND WHY IT HAD TO.
//
// The old effect put its structure in an authored sheet stretched over the
// droplet's skin. Three problems, and only the first is a bug:
//
//   1. All three tube sheets declared `A:opacity` and shipped with NO alpha
//      channel, so the sheet painted strands onto a surface opaque everywhere
//      and the effect read as crumpled foil. Now caught at configure time by
//      scripts/validate_vfx_surface_registry.py.
//   2. A sheet on a surface IS a surface: the near wall and the far wall carry
//      the same picture in the same place, so nothing reads as being inside
//      anything. No sheet can fix that; it is what a sheet is.
//   3. Repairing (1) alone measured WORSE, five times, and five separate models
//      of why were each disproved by the next measurement. The old draw went
//      through a two-layer stack whose two passes (one forced to BLEND_ALPHA,
//      one additive, with a layer filter between them) I could not predict.
//      Rather than keep reverse-engineering it, this owns its fragment stage.
//
// `ResolveShader` in trail_system.c: "A trail carrying its own shader keeps it
// — that caller owns the pairing." So: ONE layer, ONE blend, one shader, no
// sheet, and every uniform the shader reads is one the trail system already
// pushes. An invented uniform would silently read zero, so the knobs are mapped
// onto that set rather than added to it.
//
// Managed archetype: private pool + VC_FilamentTrail_Update/_Draw3D. The trail
// system draws every trail globally, so _Draw3D is empty on purpose — same as
// its predecessor.

#include "core/tuning.h"

#define FIL_MAX 8
#define FIL_TAG_BASE 0x00F10000
#define FIL_SAMPLE_HZ 60.0f
#define FIL_MIN_VERTEX 0.035f
#define FIL_TELEPORT_SPEED 40.0f
#define FIL_IDLE_SPEED 0.05f

typedef struct {
    bool active;
    const Matrix *xf;
    VC_MaterialId matId;
    float radius, lifetime;
    int trailId;
} VC_FilamentTrail;

static VC_FilamentTrail s_fil[FIL_MAX];
static bool s_filInit = false;
static Shader s_filShader = {0};
static bool s_filShaderTried = false;

// Every one of these rides on a uniform the trail system pushes; see the
// mapping note in core/shaders/filament_trail.fs.
static float s_filRadiusMul = 1.0f;
static float s_filScale     = 12.0f;  // -> uTiling   : strand frequency
static float s_filWarp      = 0.30f;  // -> u_flowStrength: how much they curl
static float s_filSpeed     = 0.55f;  // -> u_flowSpeed   : travel along the volume
static float s_filErode     = 0.18f;  // -> u_dissolve    : how much field survives
static float s_filThin      = 0.85f;  // -> u_maskTiling  : strand thinness

static void FilamentTrail_InitShader(void)
{
    if (s_filShaderTried) return;
    s_filShaderTried = true;
    s_filShader = ResourceManager_LoadShader("core/shaders/filament_trail.vs",
                                             "core/shaders/filament_trail.fs");
    if (s_filShader.id == 0)
        TraceLog(LOG_WARNING, "VFX_FILAMENT: shader failed to load — the trail "
                              "will fall back to the default trail shader and "
                              "look like a flat swept skin");
}

static void FilamentTrail_InitShared(void)
{
    if (s_filInit) return;
    // Lazily, never from a subsystem Init — Tuning_Init runs after those and an
    // early registration silently keeps the default (core/docs/LANDMINES.md).
    Tuning_RegisterFloat("fil_radius", &s_filRadiusMul, 1.0f);
    Tuning_RegisterFloat("fil_scale",  &s_filScale,     12.0f);
    Tuning_RegisterFloat("fil_warp",   &s_filWarp,      0.30f);
    Tuning_RegisterFloat("fil_speed",  &s_filSpeed,     0.55f);
    Tuning_RegisterFloat("fil_erode",  &s_filErode,     0.18f);
    Tuning_RegisterFloat("fil_thin",   &s_filThin,      0.85f);
    FilamentTrail_InitShader();
    s_filInit = true;
}

// ── Shape and tier, owned here rather than borrowed ─────────────────────────
//
// Copied from the effect this replaces rather than referenced, because the last
// step of this migration is to DELETE that file. Same numbers, same reasons —
// the tier ladder clamps DOWN and the volume stays a volume at every tier, and
// the droplet's two sine deform layers stay OFF because they are periodic in
// both t and phi, i.e. a helix, which reads as a tornado ridge running down
// the body.
#define FIL_ASPECT_K 0.20f          // radius ceiling per metre actually swept (VOL_ASPECT_K)

static PMDropletConfig s_filDroplet;
static bool s_filDropletBuilt = false;

static const PMDropletConfig *FilamentTrail_Shape(void)
{
    if (!s_filDropletBuilt)
    {
        s_filDroplet = PMDroplet_DefaultConfig();
        s_filDroplet.wobbleAmplitude = 0.0f;
        s_filDroplet.deform1Amp = 0.0f;
        s_filDroplet.deform2Amp = 0.0f;
        s_filDroplet.noiseScale = 5.0f;
        s_filDroplet.noiseSpeed = 1.6f;
        s_filDropletBuilt = true;
    }
    return &s_filDroplet;
}

static int FilamentTrail_RadialSegs(void)
{
    switch (GfxQuality_Get())
    {
    case GFX_HIGH: return 8;
    case GFX_MED:  return 8;
    case GFX_LOW:  return 6;
    default:       return 5;   // still closed, still round enough to hold a rim
    }
}

static int FilamentTrail_Rings(void)
{
    switch (GfxQuality_Get())
    {
    case GFX_HIGH: return 24;
    case GFX_MED:  return 20;
    case GFX_LOW:  return 14;
    default:       return 10;
    }
}

// THE NUMBER THAT DECIDES WHETHER ANY OF THIS IS VISIBLE. A trail can be
// configured perfectly and still draw as a hairline if the aspect cap holds it
// there: the caller's radius is a ceiling, and the drawn radius is also bounded
// by the length the emitter has actually swept.
static float FilamentTrail_Radius(float radiusMetres, float travelLen)
{
    float want = (radiusMetres > 0.0f) ? radiusMetres : 0.0f;
    float cap = travelLen * FIL_ASPECT_K;
    return (cap < want) ? cap : want;
}

static float FilamentTrail_TravelLength(const TrailEntity *t)
{
    if (t->historyCount < 2) return 0.0f;
    float total = 0.0f;
    int i = t->historyHead;
    for (int n = 0; n < t->historyCount - 1; n++)
    {
        int prev = (i - 1 + TRAIL_HISTORY_COUNT) % TRAIL_HISTORY_COUNT;
        total += Vector3Distance(t->history[i], t->history[prev]);
        i = prev;
    }
    return total;
}

static int FilamentTrail_MaxNodes(float lifetime)
{
    return VC_TrailNodesForLifetime(lifetime, FIL_SAMPLE_HZ);
}

// NEVER trust a stored trail id: ids are reused and the pool evicts by
// priority, so writing into a stale one corrupts an unrelated effect.
static TrailEntity *FilamentTrail_Entity(const VC_FilamentTrail *f, int slot)
{
    if (f->trailId < 0) return NULL;
    TrailEntity *t = GetTrail(f->trailId);
    if (!t || !t->active || t->ownerTag != (FIL_TAG_BASE | slot)) return NULL;
    return t;
}

// ONE layer. The old effect's second layer was a wider untextured shell whose
// alpha faked the volume's falloff; here the falloff is |N.V| optical depth in
// the fragment stage, derived from the geometry that is actually there.
static TrailLayer s_filLayer[1] = {
    {.widthMul = 1.00f, .alphaMul = 1.00f, .whiten = 0.0f,
     .scrollMul = 1.00f, .headAlphaPow = 0.0f, .texture = NULL},
};

static int FilamentTrail_Spawn(const VC_FilamentTrail *f, int slot)
{
    TrailConfig cfg = {0};
    cfg.type = TRAIL_TYPE_FOLLOWER;
    cfg.pos = Vector3Transform((Vector3){0.0f, 0.0f, 0.0f}, *f->xf);
    cfg.life = 1.0e6f;
    cfg.thick = 0.05f;   // real value written every frame below
    cfg.tint = WHITE;    // the ramp carries the colour
    cfg.gradient = VC_ElementRamp(f->matId);
    cfg.widthEnvelope = TRAIL_WIDTH_ENVELOPE_UNIFORM;
    cfg.ownerTag = FIL_TAG_BASE | slot;
    cfg.priority = VFX_PRIORITY_LOW;
    // ONE contract. Premultiplied is (ONE, ONE_MINUS_SRC_ALPHA): the shader
    // hands over premultiplied colour and keeps the (1 - a) term, which is what
    // lets a transparent volume still bite into bright scenery. useCustomBlendMode
    // because BLEND_ALPHA is 0 and cannot be detected by > 0.
    cfg.blendMode = BLEND_ALPHA_PREMULTIPLY;
    cfg.useCustomBlendMode = true;
    cfg.shader = s_filShader;
    cfg.minVertexDistance = FIL_MIN_VERTEX;
    cfg.disableInnerCore = true;
    cfg.shape = TRAIL_SHAPE_TUBE;
    cfg.tubeRadialSegs = FilamentTrail_RadialSegs();
    cfg.tubeMaxRings = FilamentTrail_Rings();
    cfg.tubeCaps = true;
    // BOTH WALLS. Culling the far one is what makes a swept tube read as a
    // solid skin; the fragment stage dims it instead so you can see through the
    // near wall to the strands behind.
    cfg.tubeSingleSided = false;
    cfg.dropletConfig = FilamentTrail_Shape();
    cfg.layers = s_filLayer;
    cfg.layerCount = 1;
    cfg.uvMetresPerTile = 1.0f;
    cfg.sampleHz = FIL_SAMPLE_HZ;
    cfg.teleportSpeed = FIL_TELEPORT_SPEED;
    cfg.idleSpeed = FIL_IDLE_SPEED;
    cfg.trailLength = (float)FilamentTrail_MaxNodes(f->lifetime);
    // The flow uniforms are the shader's knobs. useFlowMap is what makes the
    // trail system push them at all; no flow TEXTURE is bound, and the shader
    // never samples one.
    cfg.useFlowMap = true;
    cfg.flowSpeed = s_filSpeed;
    cfg.flowStrength = s_filWarp;
    cfg.flowTiling = s_filScale;
    cfg.dissolve = s_filErode;
    cfg.maskTiling = s_filThin;

    int id = SpawnTrailEntity(cfg);
    if (id >= 0)
        Trail_AttachToTransform(id, f->xf, (Vector3){0.0f, 0.0f, 0.0f});

    // ONE LINE PER SPAWN, unconditional. "It looks the same as before" and "the
    // new path never ran" are indistinguishable on screen.
    TraceLog(LOG_INFO,
             "VFX_FILAMENT: slot %d — %d radial x %d rings, 1 layer, shader %u, "
             "scale %.2f warp %.2f speed %.2f erode %.2f thin %.2f",
             slot, cfg.tubeRadialSegs, cfg.tubeMaxRings, s_filShader.id,
             s_filScale, s_filWarp, s_filSpeed, s_filErode, s_filThin);
    return id;
}

int VFX_ComposeFilamentTrail(const Matrix *followTransform, VC_MaterialId mat,
                             float radius, float lifetime)
{
    FilamentTrail_InitShared();
    if (!followTransform)
    {
        TraceLog(LOG_WARNING, "VFX_FILAMENT: NULL transform — nothing created");
        return -1;
    }
    for (int i = 0; i < FIL_MAX; i++)
    {
        if (s_fil[i].active) continue;
        s_fil[i] = (VC_FilamentTrail){
            .active = true, .xf = followTransform, .matId = mat,
            .radius = (radius > 0.0f) ? radius : 0.35f,
            .lifetime = (lifetime > 0.05f) ? lifetime : 1.5f,
            .trailId = -1,
        };
        s_fil[i].trailId = FilamentTrail_Spawn(&s_fil[i], i);
        return i;
    }
    return -1;
}

void VFX_KillFilamentTrail(int handle)
{
    if (handle < 0 || handle >= FIL_MAX || !s_fil[handle].active) return;
    TrailEntity *t = FilamentTrail_Entity(&s_fil[handle], handle);
    if (t) KillTrail(s_fil[handle].trailId);
    s_fil[handle].active = false;
    s_fil[handle].trailId = -1;
}

static void VC_FilamentTrail_Update(float dt)
{
    if (dt <= 0.0f) return;
    for (int i = 0; i < FIL_MAX; i++)
    {
        VC_FilamentTrail *f = &s_fil[i];
        if (!f->active) continue;

        TrailEntity *t = FilamentTrail_Entity(f, i);
        if (!t)
        {
            // Evicted or recycled under us. Respawning is why the tag exists:
            // eviction becomes self-healing instead of fatal.
            f->trailId = FilamentTrail_Spawn(f, i);
            continue;
        }
        float travel = FilamentTrail_TravelLength(t);
        t->thickness = FilamentTrail_Radius(f->radius * s_filRadiusMul, travel);
        // Live knobs: pushed every frame so the tuning UI moves the look
        // without a respawn. They are per-ENTITY fields, not per-draw uniforms,
        // so this is a struct write rather than a SetShaderValue.
        t->flowSpeed = s_filSpeed;
        t->flowStrength = s_filWarp;
        t->flowTiling = s_filScale;
        t->dissolve = s_filErode;
        t->maskTiling = s_filThin;
    }
}

static void VC_FilamentTrail_Draw3D(Camera3D cam)
{
    // Empty on purpose: UpdateTrailSystem/DrawTrailSystem own every trail's
    // draw. Same as the effect this replaces.
    (void)cam;
}
