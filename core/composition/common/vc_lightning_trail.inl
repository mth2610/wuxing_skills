// Composition adapter and reference ground-hopping impact for Core's multi-point
// LightningStroke path. LightningArc owns two endpoints; this wrapper records a
// moving head and feeds one continuous polyline into the same proven stroke
// shader. It deliberately does not use a general-purpose Trail material: an
// electric path needs the stroke's one field, reveal and HDR profile.

#define LIGHTNING_RICOCHET_MAX       8
#define LIGHTNING_RICOCHET_STRANDS   2
#define LIGHTNING_RICOCHET_HOPS      5
#define LIGHTNING_RICOCHET_HOP_TIME  0.105f
#define VFX_LIGHTNING_TRAIL_MAX       32

typedef struct {
    bool active;
    int serial;
    int stroke;
    VFX_LightningTrailConfig config;
    Vector3 points[RIBBON_MIDPOINT_MAX_POINTS];
    int pointCount;
} VFXLightningTrail;

static VFXLightningTrail s_vfxLightningTrails[VFX_LIGHTNING_TRAIL_MAX];
static int s_vfxLightningTrailSerial = 0;

typedef struct {
    bool active;
    float elapsed;
    Vector3 impactPos;
    VC_MaterialId material;
    float scale;
    unsigned int seed;
    int hop;
    int trails[LIGHTNING_RICOCHET_STRANDS];
} LightningRicochet;

static LightningRicochet s_lightningRicochets[LIGHTNING_RICOCHET_MAX];

static LightningStrokeConfig LightningTrail_ToStrokeConfig(const VFX_LightningTrailConfig *config)
{
    const VFX_ElementMaterial *mat = VFX_Material(config->material);
    Color cobaltBlue = (Color){18, 61, 255, 255};
    return (LightningStrokeConfig){
        .bodyColor = VC_WithAlpha(VC_MixColor(cobaltBlue, mat->glow, 0.42f), 96),
        .haloColor = VC_WithAlpha(VC_MixColor(cobaltBlue, mat->glow, 0.54f), 76),
        .coreColor = VC_WithAlpha(VC_Whiten(mat->glow, 0.84f), 255),
        .width = config->width,
        .lifetime = 60.0f,
        .travelDuration = 0.07f,
        // A live moving path must not time out. VFX_LightningTrail_Stop turns
        // this into its authored short post-impact hold.
        .postImpactDuration = -1.0f,
        .coreEmission = config->coreEmission,
        .haloEmission = config->haloEmission,
        .jaggedness = config->jaggedness,
        .flickerInterval = config->flickerInterval,
        .branchCount = 0,
        .seed = config->seed,
    };
}

VFX_LightningTrailConfig VFX_LightningTrail_DefaultConfig(void)
{
    return (VFX_LightningTrailConfig){
        .material = VC_MAT_LIGHTNING,
        .width = 0.055f,
        .pointLifetime = 0.26f,
        .sampleDistance = 0.10f,
        .jaggedness = 0.16f,
        .coreEmission = 4.2f,
        .haloEmission = 0.36f,
        .flickerInterval = 0.045f,
        .seed = 0u
    };
}

int VFX_LightningTrail_Spawn(Vector3 head, const VFX_LightningTrailConfig *config)
{
    VFX_LightningTrailConfig resolved = config ? *config : VFX_LightningTrail_DefaultConfig();
    if (resolved.width <= 0.0f) resolved.width = 0.055f;
    if (resolved.pointLifetime <= 0.0f) resolved.pointLifetime = 0.26f;
    if (resolved.sampleDistance <= 0.0f) resolved.sampleDistance = 0.10f;
    if (resolved.jaggedness < 0.0f) resolved.jaggedness = 0.0f;
    if (resolved.coreEmission <= 0.0f) resolved.coreEmission = 4.2f;
    if (resolved.haloEmission <= 0.0f) resolved.haloEmission = 0.36f;
    if (resolved.seed == 0u)
    {
        // Desynchronise concurrent casts without a global RNG dependency; the
        // same input position keeps deterministic replay/network behaviour.
        resolved.seed = (unsigned int)(fabsf(head.x * 719.0f) +
                                       fabsf(head.y * 431.0f) +
                                       fabsf(head.z * 1237.0f)) | 1u;
    }
    int slot = -1;
    for (int i = 0; i < VFX_LIGHTNING_TRAIL_MAX; ++i)
        if (!s_vfxLightningTrails[i].active) { slot = i; break; }
    // Stop deliberately leaves its child stroke alive for the short electrical
    // hold. Reusing that wrapper must retire the old child first, otherwise a
    // rapid stream of trails would leave invisible pool occupants behind.
    if (slot >= 0 && s_vfxLightningTrails[slot].stroke >= 0)
        LightningStroke_Kill(s_vfxLightningTrails[slot].stroke);
    if (slot < 0)
    {
        // Recycle the oldest serial slot deterministically. The stroke is a
        // child allocation and must be released with its wrapper.
        slot = 0;
        for (int i = 1; i < VFX_LIGHTNING_TRAIL_MAX; ++i)
            if (s_vfxLightningTrails[i].serial < s_vfxLightningTrails[slot].serial)
                slot = i;
        if (s_vfxLightningTrails[slot].stroke >= 0)
            LightningStroke_Kill(s_vfxLightningTrails[slot].stroke);
    }
    VFXLightningTrail *trail = &s_vfxLightningTrails[slot];
    *trail = (VFXLightningTrail){0};
    trail->active = true;
    trail->serial = ++s_vfxLightningTrailSerial;
    trail->stroke = -1;
    trail->config = resolved;
    trail->points[0] = head;
    trail->pointCount = 1;
    return (trail->serial << 8) | slot;
}

void VFX_LightningTrail_SetHead(int handle, Vector3 head)
{
    int slot = handle & 0xff;
    if (handle < 0 || slot >= VFX_LIGHTNING_TRAIL_MAX) return;
    VFXLightningTrail *trail = &s_vfxLightningTrails[slot];
    if (!trail->active || (handle >> 8) != trail->serial) return;
    if (Vector3DistanceSqr(head, trail->points[trail->pointCount - 1]) < 1e-8f)
        return;
    if (trail->pointCount == RIBBON_MIDPOINT_MAX_POINTS)
    {
        for (int i = 1; i < trail->pointCount; ++i)
            trail->points[i - 1] = trail->points[i];
        --trail->pointCount;
    }
    trail->points[trail->pointCount++] = head;
    LightningStrokeConfig strokeConfig = LightningTrail_ToStrokeConfig(&trail->config);
    if (trail->stroke < 0)
        trail->stroke = LightningStroke_SpawnPath(trail->points, trail->pointCount,
                                                   &strokeConfig);
    else
        LightningStroke_SetPath(trail->stroke, trail->points, trail->pointCount);
}

void VFX_LightningTrail_Stop(int handle)
{
    int slot = handle & 0xff;
    if (handle < 0 || slot >= VFX_LIGHTNING_TRAIL_MAX) return;
    VFXLightningTrail *trail = &s_vfxLightningTrails[slot];
    if (!trail->active || (handle >> 8) != trail->serial) return;
    if (trail->stroke >= 0)
        LightningStroke_Stop(trail->stroke, trail->config.pointLifetime);
    trail->active = false;
}

void VFX_LightningTrail_Kill(int handle)
{
    int slot = handle & 0xff;
    if (handle < 0 || slot >= VFX_LIGHTNING_TRAIL_MAX) return;
    VFXLightningTrail *trail = &s_vfxLightningTrails[slot];
    if (!trail->active || (handle >> 8) != trail->serial) return;
    if (trail->stroke >= 0)
        LightningStroke_Kill(trail->stroke);
    trail->active = false;
}

static Vector3 LightningRicochet_HopStart(const LightningRicochet *fx, int strand, int hop)
{
    float angle = (float)strand * (2.0f * PI / (float)LIGHTNING_RICOCHET_STRANDS) +
                  (float)(fx->seed & 255u) * 0.013f;
    Vector3 pos = fx->impactPos;
    for (int h = 0; h < hop; ++h) {
        float distance = fx->scale * 0.56f * powf(0.67f, (float)h);
        angle += 0.92f + (float)((fx->seed >> ((h + strand) & 15)) & 15u) * 0.029f;
        pos.x += cosf(angle) * distance;
        pos.z += sinf(angle) * distance;
    }
    return pos;
}

static Vector3 LightningRicochet_HopHead(const LightningRicochet *fx, int strand,
                                         int hop, float t)
{
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    Vector3 start = LightningRicochet_HopStart(fx, strand, hop);
    float angle = (float)strand * (2.0f * PI / (float)LIGHTNING_RICOCHET_STRANDS) +
                  (float)(fx->seed & 255u) * 0.013f;
    for (int h = 0; h <= hop; ++h)
        angle += 0.92f + (float)((fx->seed >> ((h + strand) & 15)) & 15u) * 0.029f;
    float hopDistance = fx->scale * 0.56f * powf(0.67f, (float)hop);
    float hopHeight = fx->scale * 0.42f * powf(0.62f, (float)hop);
    Vector3 head = start;
    head.x += cosf(angle) * hopDistance * t;
    head.z += sinf(angle) * hopDistance * t;
    // A sharp electrical launch and fall, rather than a long smooth projectile
    // parabola. The tiny transverse snap prevents two hops reading as rope.
    float rise = sinf(t * PI);
    float snap = sinf(t * PI * 3.0f + (float)((fx->seed >> (strand * 5)) & 31u)) *
                 rise * hopDistance * 0.10f;
    head.x -= sinf(angle) * snap;
    head.z += cosf(angle) * snap;
    head.y += 0.018f + hopHeight * rise;
    return head;
}

static Vector3 LightningRicochet_Head(const LightningRicochet *fx, int strand)
{
    int hop = (int)(fx->elapsed / LIGHTNING_RICOCHET_HOP_TIME);
    if (hop >= LIGHTNING_RICOCHET_HOPS) hop = LIGHTNING_RICOCHET_HOPS - 1;
    float t = (fx->elapsed - (float)hop * LIGHTNING_RICOCHET_HOP_TIME) /
              LIGHTNING_RICOCHET_HOP_TIME;
    return LightningRicochet_HopHead(fx, strand, hop, t);
}

static void LightningRicochet_BeginHop(LightningRicochet *fx, int hop)
{
    VFX_LightningTrailConfig trail = VFX_LightningTrail_DefaultConfig();
    trail.material = fx->material;
    trail.width = 0.032f * fx->scale;
    trail.pointLifetime = 0.135f;
    trail.sampleDistance = 0.10f * fx->scale;
    trail.jaggedness = 0.14f * fx->scale;
    trail.coreEmission = 4.4f;
    trail.haloEmission = 0.34f;
    for (int strand = 0; strand < LIGHTNING_RICOCHET_STRANDS; ++strand) {
        trail.seed = fx->seed + (unsigned int)(hop * 17 + strand) * 0x9e3779b9u;
        Vector3 origin = LightningRicochet_HopStart(fx, strand, hop);
        origin.y += 0.018f;
        fx->trails[strand] = VFX_LightningTrail_Spawn(origin, &trail);
    }
}

static void LightningRicochet_Update(float dt)
{
    const float duration = LIGHTNING_RICOCHET_HOP_TIME * (float)LIGHTNING_RICOCHET_HOPS;
    for (int i = 0; i < LIGHTNING_RICOCHET_MAX; ++i) {
        LightningRicochet *fx = &s_lightningRicochets[i];
        if (!fx->active) continue;
        fx->elapsed += dt;
        if (fx->elapsed >= duration) {
            for (int strand = 0; strand < LIGHTNING_RICOCHET_STRANDS; ++strand)
                VFX_LightningTrail_Stop(fx->trails[strand]);
            fx->active = false;
            continue;
        }
        int nextHop = (int)(fx->elapsed / LIGHTNING_RICOCHET_HOP_TIME);
        if (nextHop != fx->hop) {
            for (int strand = 0; strand < LIGHTNING_RICOCHET_STRANDS; ++strand) {
                VFX_LightningTrail_SetHead(fx->trails[strand],
                    LightningRicochet_HopHead(fx, strand, fx->hop, 1.0f));
                VFX_LightningTrail_Stop(fx->trails[strand]);
            }
            fx->hop = nextHop;
            LightningRicochet_BeginHop(fx, fx->hop);
        }
        for (int strand = 0; strand < LIGHTNING_RICOCHET_STRANDS; ++strand)
            VFX_LightningTrail_SetHead(fx->trails[strand], LightningRicochet_Head(fx, strand));
    }
}

void VFX_ComposeLightningGroundRicochet(Vector3 impactPos, VC_MaterialId material,
                                        float scale, unsigned int seed)
{
    int slot = -1;
    for (int i = 0; i < LIGHTNING_RICOCHET_MAX; ++i)
        if (!s_lightningRicochets[i].active) { slot = i; break; }
    if (slot < 0) {
        float oldest = -1.0f;
        for (int i = 0; i < LIGHTNING_RICOCHET_MAX; ++i) {
            if (s_lightningRicochets[i].elapsed > oldest) {
                oldest = s_lightningRicochets[i].elapsed;
                slot = i;
            }
        }
        // Reclaim the live sparse histories too. Replacing only the wrapper
        // would leave three invisible owners consuming trail-pool slots.
        for (int strand = 0; strand < LIGHTNING_RICOCHET_STRANDS; ++strand)
            VFX_LightningTrail_Kill(s_lightningRicochets[slot].trails[strand]);
    }

    LightningRicochet *fx = &s_lightningRicochets[slot];
    *fx = (LightningRicochet){0};
    fx->active = true;
    fx->impactPos = impactPos;
    fx->material = material;
    fx->scale = fmaxf(scale, 0.25f);
    fx->seed = seed ? seed : (unsigned int)(fabsf(impactPos.x * 719.0f) + fabsf(impactPos.z * 1237.0f));

    fx->hop = 0;
    LightningRicochet_BeginHop(fx, fx->hop);
}
