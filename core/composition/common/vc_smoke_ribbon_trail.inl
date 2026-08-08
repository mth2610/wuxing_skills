// ── VFX_ComposeSmokeRibbonTrail — the flat-PLANE counterpart of vc_smoke_trail ──
//
// vc_smoke_trail.inl sweeps a TUBE (TRAIL_SHAPE_TUBE) and owns a PMTubeConfig
// shape + a churn MeshDeformField to make the surface billow. That geometry is
// the whole point of that file. This file asks the opposite question: what does
// the SAME smoked material look like when the path only gets to sweep a FLAT
// camera-facing BAND (TRAIL_SHAPE_RIBBON)?
//
// A flat ribbon has no cross-section to billow, so none of the tube's machinery
// applies here: no PMTubeConfig, no radial segs, no `anchorAtTail`, no churn
// field. The motion that in vc_smoke_trail.inl lived in the VERTEX stage (the
// deform field pushing rings sideways) has nowhere to live on a flat strip, so
// the smoke reads off the TEXTURE instead — sheet scroll (uvScroll) plus the
// width envelope. This file is the ribbon, that one is the tube, and the two
// should be authored together so they look like the same material behaving two
// different ways.
//
// THE SILHOUETTE. A ribbon is a flat quad; it has no natural edge falloff the
// way a tube's far wall has. The volume sheets are authored SEAMLESS on both
// axes (a repeating MATERIAL — "the silhouette comes from the geometry", per
// assets/vfx_surface_profiles.json), so a single textured layer on a flat strip
// would have HARD edges where the band ends. The fix is the classic ribbon
// layer stack (vc_ribbon_trail.inl's whole point): a wide soft shell (NULL
// texture -> the global soft trail texture the renderer supplies) under a
// narrower body that carries the volume sheet. The wide shell is what supplies
// the falloff the geometry of a tube used to give for free.
//
// THE WIDTH ENVELOPE replaces the funnel/cylinder choice. The tube said "small
// at the FRONT, big at the BACK" (that is what `anchorAtTail` flipped). On a
// ribbon the same intent is a width envelope over the laid path: a compact head
// that widens and stays broad to the tail (SMOKE_WIDEN) is the "funnel"
// reading; a band that grows in, billows, then dissolves as it goes
// (SMOKE_LIFECYCLE) is the cylinder reading with a soft end.
//
// THE POOL / HANDLE / STOP CONTRACT is a verbatim copy of vc_smoke_trail.inl:
// a fixed pool, a serialised handle (serial<<8 | slot) so a stale slot cannot
// address somebody else's effect, and VFX_SmokeTrail_Stop semantics — stop the
// FEED, let the laid ribbon drain and fade, never a hard cut.
//
// Managed archetype: private pool + VC_SmokeRibbonTrail_Update/_Draw3D. That
// pair is how a stateful composition declares itself to scripts/sync_vfx_test.py
// (for the tube's identical stub, see vc_smoke_trail.inl:520).

#include "core/tuning.h"
#include "core/skill_curve.h"

#define VFX_SMOKE_RIBBON_MAX 6
#define SMOKE_RIBBON_TAG_BASE 0x5E0000

// Head→tail ALPHA feather for the flat band. Unlike the tube (whose deform
// shader u_tailFadeA/B evaporates the tip for SMOKE_WIDEN), a RIBBON never
// enables the deform material, so its SMOKE_WIDEN built-in tail ramp is DEAD
// code here — the width envelope grows monotonically to full and a wide
// hard-edged band is left at the tail. This curve is the ribbon's own
// head/tail crossover: 0 at the very head (the emitter, so a fresh puff
// lerps in instead of popping), peak across the broad middle, 0 at the very
// tail (the laid band dissolves instead of ending in a clipped sheet). It is
// sampled with segRatio (1 = head, 0 = tail), i.e. FloatCurve t = 0 IS tail.
static SkillCurve s_smokeRibbonAlpha;

static void SmokeRibbon_BuildAlphaCurve(void)
{
    static bool built = false;
    if (built)
        return;
    built = true;
    // Built by AddStop only — SkillCurve_SetConstant would first seed
    // SKILL_CURVE_KEYS (5) plateau stops and these 5 would overflow
    // FLOAT_CURVE_MAX_STOPS (8). AddStop sorts by t, order below is free.
    FloatCurve_AddStop(&s_smokeRibbonAlpha, 0.00f, 0.0f);  // tail = 0
    FloatCurve_AddStop(&s_smokeRibbonAlpha, 0.10f, 0.90f); // quick rise
    FloatCurve_AddStop(&s_smokeRibbonAlpha, 0.88f, 0.95f); // broad middle
    FloatCurve_AddStop(&s_smokeRibbonAlpha, 0.97f, 0.45f); // soften head
    FloatCurve_AddStop(&s_smokeRibbonAlpha, 1.00f, 0.0f);  // head = 0
}

typedef struct {
    bool active;
    bool stopping;
    int trailId;
    int serial;
    Vector3 pos;
    VC_MaterialId matId;
    VFX_ColumnKind kind;   // SMOKE / FIRE / STEAM — the material's smoke family
    float width;           // band HALF-width in metres (the caller's radius)
    float lifetime;        // seconds of tail memory
    bool funnel;           // true = WIDEN envelope, false = LIFECYCLE envelope
    float uvSpeed;         // smoke scroll rate (tiles / sec), pushed every frame
    TrailLayer layers[2];  // instance-owned: TrailEntity retains this pointer
} VC_SmokeRibbonTrail;

static VC_SmokeRibbonTrail s_smokeRibbon[VFX_SMOKE_RIBBON_MAX];
static int s_smokeRibbonSerial = 0;
static bool s_smokeRibbonInit = false;

// Same smoke family sheets as the tube — the SHEET is a taste choice per kind,
// not a property of whether the path is a tube or a ribbon.
static const VFX_SurfaceId k_smokeRibbonSurface[VFX_COLUMN_KIND_COUNT] = {
    [VFX_COLUMN_SMOKE] = VFX_SURFACE_VOLUME_SMOKE,
    [VFX_COLUMN_FIRE]  = VFX_SURFACE_VOLUME_FIRE,
    [VFX_COLUMN_STEAM] = VFX_SURFACE_VOLUME_STEAM,
};
static Texture2D s_smokeRibbonSheet[VFX_COLUMN_KIND_COUNT];

// Scroll rate per kind. The tube used these same speeds over its surface;
// on a flat ribbon the scroll IS the only loa-moving texture motion, so the dial
// is the knob that decides whether the smoke reads as drifting gas or as a static
// print. Fire licks fastest, smoke churns slowest — same spread the tube has.
static const float k_smokeRibbonScroll[VFX_COLUMN_KIND_COUNT] = {0.35f, 0.85f, 0.30f};

// Live knobs, own namespace from the tube's (smoketrail2_*) so tuning one
// archetype never silently retunes the other. Registered lazily on first use —
// never from an Init: Tuning_Init runs after subsystem inits in main.c
// (core/docs/LANDMINES.md).
static float s_smokeRibbonAlphaMul = 1.0f;
static float s_smokeRibbonScrollMul = 1.0f;
static float s_smokeRibbonTile = 1.50f;
static float s_smokeRibbonShell = 1.0f; // x the soft shell's width (0..halts)
static float s_smokeRibbonBody = 1.0f;   // x the smoke body's opacity
// Vertex bombil — how strongly the flat band is deformed in the GPU deprop
// shader (cfg.deform). Expressed as a FRACTION of the band half-width so it
// scales with whatever radius the caller picks: 0 = flat frozen strip, ~0.5 =
// the band visibly rises and falls like gas, 1.0 = near-folding. Kept well
// under 1 so the strip never folds through itself on a tight path.
static float s_smokeRibbonDefMul = 0.45f;
static float s_smokeRibbonDefFreq = 1.40f; // waviness along the path (arches/metre)

static void SmokeRibbon_EnsureTuning(void)
{
    static bool done = false;
    if (done) return;
    done = true;
    Tuning_RegisterFloat("smokeribbon_alpha", &s_smokeRibbonAlphaMul, 1.0f);
    Tuning_RegisterFloat("smokeribbon_scroll", &s_smokeRibbonScrollMul, 1.0f);
    Tuning_RegisterFloat("smokeribbon_tile", &s_smokeRibbonTile, 1.20f);
    Tuning_RegisterFloat("smokeribbon_shell", &s_smokeRibbonShell, 1.0f);
    Tuning_RegisterFloat("smokeribbon_body", &s_smokeRibbonBody, 1.0f);
    Tuning_RegisterFloat("smokeribbon_defmul", &s_smokeRibbonDefMul, 0.55f);
    Tuning_RegisterFloat("smokeribbon_deffreq", &s_smokeRibbonDefFreq, 1.0f);
}

static void SmokeRibbon_InitShared(void)
{
    if (s_smokeRibbonInit) return;
    s_smokeRibbonInit = true;
    for (int k = 0; k < VFX_COLUMN_KIND_COUNT; k++)
    {
        const VFX_SurfaceProfile *p = VFX_SurfaceRegistry_Get(k_smokeRibbonSurface[k]);
        s_smokeRibbonSheet[k] = (p != NULL) ? p->body : (Texture2D){0};
        // The ribbon tile depends on a material that repeats over the path; the
        // registry owns bilinear/repeat for these sheets, same as the tube.
    }
}

// The two-layer ribbon stack. A flat band has no far wall to give it an edge, so
// layer[0] is a WIDE SOFT SHELL carrying no texture (the renderer substitutes
// the global soft trail texture) — that shell is the band's falloff. Layer[1] is
// the smoke body that actually shows the sheet.
//
// ALPHA BUDGET. These two overlap, so under BLEND_ALPHA (occluding smoke) the
// frame buffer sees their sum through the band: shell 0.10 + body 0.30 = 0.40,
// comfortably under 1.0. The body is kept lower than the old single-layer
// value on purpose — the shell carries the SIDE edges now, and a body that is
// too bright next to its soft rim reads as a hard pill. The alphaCurve
// feathers the head and tail on top of this, so the band's four edges all
// dissolve.
static void SmokeRibbon_ConfigureLayers(VC_SmokeRibbonTrail *c)
{
    const Texture2D *sheet = &s_smokeRibbonSheet[c->kind];
    // SHELL: the wide soft falloff that gives the flat band its two SIDE
    // edges. A body sheet is a seamless MATERIAL with no edge alpha — on the
    // flat band its hard side edges would read as two hard lines. The shell
    // (no texture -> renderer's global soft trail texture) is drawn WIDER than
    // the body so the body's own edge lands inside the shell's falloff, never
    // at full opacity. It cannot do that if it only pokes out a little: it must
    // reach visibly past the body the hard edge would otherwise show.
    c->layers[0] = (TrailLayer){
        .widthMul = 1.65f * s_smokeRibbonShell,
        .alphaMul = 0.10f * s_smokeRibbonAlphaMul,
        .whiten = 0.0f,
        .scrollMul = 0.55f,
        .texture = NULL, // -> soft falloff shell, not the material
    };
    // BODY: the smoke sheet, tucked under the shell so its hard sheet edge
    // sits inside the shell's soft band (the shell carries the sides).
    c->layers[1] = (TrailLayer){
        .widthMul = 0.72f,
        .alphaMul = 0.30f * s_smokeRibbonBody * s_smokeRibbonAlphaMul,
        .whiten = 0.08f,
        .scrollMul = 1.85f,
        .texture = sheet,
    };
}

// Real path laying — the whole point of reusing the trail system: the system
// itself lays new history nodes as the caller's transform moves and drops old
// ones past `lifetime`. Not a frozen shape dragged around.
static int SmokeRibbon_Spawn(VC_SmokeRibbonTrail *c, int slot,
                             const Matrix *followTransform)
{
    TrailConfig cfg = {0};
    cfg.type = TRAIL_TYPE_FOLLOWER;
    cfg.pos = c->pos;
    cfg.life = 1.0e6f; // dies when stopped and the feed drains — long-lived like the tube
    cfg.thick = c->width;
    cfg.tint = WHITE;
    cfg.gradient = NULL; // the material carries the colour
    const VFX_ElementMaterial *m = VFX_Material(c->matId);
    if (m != NULL) cfg.tint = m->body;

    // NULL, not a simulation — same reasoning as the tube: a cloth step here
    // would make the band writhe like a snake, and the material's whole life
    // comes from scroll + width envelope.
    cfg.forceField = NULL;

    cfg.shape = TRAIL_SHAPE_RIBBON;
    cfg.ribbonMode = RIBBON_CAMERA_FACING; // smoke reads as gas, not a blade plane
    // Funnel = compact head widening to a broad tail; cylinder = grow, hold,
    // and let the LIFECYCLE envelope dissolve the end softly.
    cfg.widthEnvelope = c->funnel ? TRAIL_WIDTH_ENVELOPE_SMOKE_WIDEN
                                  : TRAIL_WIDTH_ENVELOPE_SMOKE_LIFECYCLE;
    cfg.disableInnerCore = true; // the layer stack is the body — no separate white core

    cfg.layers = c->layers;
    cfg.layerCount = 2;
    cfg.uvMetresPerTile = (s_smokeRibbonTile > 0.05f) ? s_smokeRibbonTile : 0.05f;
    cfg.uvScrollSpeed = k_smokeRibbonScroll[c->kind] * s_smokeRibbonScrollMul;
    cfg.blendMode = (c->kind == VFX_COLUMN_FIRE) ? BLEND_ADDITIVE : BLEND_ALPHA;
    cfg.useCustomBlendMode = true; // BLEND_ALPHA is 0 and cannot be detected by > 0
    // Flat-band ALPHA feather (head AND tail). The width envelope alone does
    // not dissolve a ribbon's ends: SMOKE_WIDEN grows monotonically and never
    // collapses, and SMOKE_LIFECYCLE leaves a non-zero head. The curve re-adds
    // the feather the tube got from its material ramp, and lets the emitter end
    // lerp in instead of popping a full-width puff. (The deform shader's
    // u_tailFadeA/B would also do the tail, but only for material-mode trails;
    // this band keeps material 0 and shows its own volume sheets, so the curve
    // is the one feather mechanism that is always active.)
    SmokeRibbon_BuildAlphaCurve();
    cfg.alphaCurve = &s_smokeRibbonAlpha;
    // FLAT-BAND VERTEX DEFORM (the thing a smoke band was missing). The mesh
    // itself is now warped by the GPU deform shader (trail_deform.vs) — curl3
    // (mode 2) displaces each vertex in the strip's (side, stripNormal) frame
    // from a 3D hash-noise curl, the organic non-rocking version of "smoke
    // stirs". A flat strip with only texture scroll reads as a STILL decal no
    // matter how fast the sheet moves; geometry billow is what makes the plume
    // feel like gas. The amplitude is a FRACTION of the band half-width
    // (s_smokeRibbonDefMul) so it scales with the caller's radius, and the
    // speed/frequency are large enough to read but small enough not to fold
    // the strip through itself on a tight path.
    //
    // Deform and material are INDEPENDENT routing keys (TrailUsesDeformShader
    // = deform.mode>0 || material.mode>0). Enabling deform.mode alone leaves
    // material.mode at 0, and the deform FRAGMENT shader passes material mode
    // <0.5 straight through — so the volume sheet + soft shell still show; only
    // the vertices now move. No packed-material needed for billow.
    cfg.deform.mode = 2.0f;                       // curl3
    cfg.deform.strength = c->width * s_smokeRibbonDefMul;
    cfg.deform.curlScale = s_smokeRibbonDefFreq;  // noise lattice density (arches/metre)
    cfg.deform.phase = Random01() * 6.28318f;     // desync simultaneous casts
    cfg.deform.envHead = 0.10f;                   // weld the head, no pop
    cfg.deform.envTail = 0.95f;                   // calmer at the old end

    // Same follower constants vc_smoke_trail.inl and vc_volume_trail.inl use —
    // it is the same trail system, a different shape riding on it.
    cfg.minVertexDistance = 0.005f;
    cfg.sampleHz = 60.0f;
    cfg.idleSpeed = 0.10f;
    cfg.teleportSpeed = 45.0f;
    cfg.trailLength = (float)VC_TrailNodesForLifetime(c->lifetime, 60.0f);
    cfg.smoothSpline = false;
    cfg.ownerTag = SMOKE_RIBBON_TAG_BASE | slot;
    cfg.priority = VFX_PRIORITY_LOW;

    int id = SpawnTrailEntity(cfg);
    if (id >= 0)
        Trail_AttachToTransform(id, followTransform, (Vector3){0.0f, 0.0f, 0.0f});

    TraceLog(LOG_INFO,
             "VFX_SMOKE_RIBBON: slot %d — kind %d, %s plane, width %.2f m, "
             "tail %.2f s, scroll %.2f tiles/s, tile %.2f m, blend %s",
             slot, (int)c->kind,
             c->funnel ? "WIDEN" : "LIFECYCLE",
             c->width, c->lifetime,
             cfg.uvScrollSpeed, cfg.uvMetresPerTile,
             cfg.blendMode == BLEND_ADDITIVE ? "ADDITIVE" : "ALPHA");
    return id;
}

// ── Public API ──────────────────────────────────────────────────────────────

// followTransform : caller-owned Matrix, kept valid for the trail's lifetime.
// width           : HALF width of the band in metres (the caller's radius). The
//                    ribbon's `thick` is split into half-widths by the renderer.
// lifetime        : seconds of trail memory at the 60 Hz sample rate.
// kind            : SMOKE / FIRE / STEAM — which sheet family and blend law.
// funnel          : WIDEN = small head spreading to a wide tail; FALSE = the
//                    LIFECYCLE envelope grows, holds, then dissolves the end.
int VFX_ComposeSmokeRibbonTrail(const Matrix *followTransform, VC_MaterialId mat,
                                float width, float lifetime, VFX_ColumnKind kind,
                                bool funnel)
{
    SmokeRibbon_EnsureTuning();
    SmokeRibbon_InitShared();

    if (!followTransform)
    {
        TraceLog(LOG_WARNING, "VFX_SMOKE_RIBBON: NULL transform — no ribbon created");
        return -1;
    }
    if ((int)kind < 0 || (int)kind >= VFX_COLUMN_KIND_COUNT)
    {
        TraceLog(LOG_WARNING,
                 "VFX_SMOKE_RIBBON: kind %d out of range [0,%d) — clamped to SMOKE",
                 (int)kind, (int)VFX_COLUMN_KIND_COUNT);
        kind = VFX_COLUMN_SMOKE;
    }

    int slot = -1;
    for (int i = 0; i < VFX_SMOKE_RIBBON_MAX; i++)
        if (!s_smokeRibbon[i].active) { slot = i; break; }
    if (slot < 0)
    {
        TraceLog(LOG_WARNING, "VFX_SMOKE_RIBBON: pool full (%d) — request dropped",
                 VFX_SMOKE_RIBBON_MAX);
        return -1;
    }

    VC_SmokeRibbonTrail *c = &s_smokeRibbon[slot];
    *c = (VC_SmokeRibbonTrail){0};
    c->active = true;
    c->pos = Vector3Transform((Vector3){0.0f, 0.0f, 0.0f}, *followTransform);
    c->matId = mat;
    c->kind = kind;
    c->width = (width > 0.01f) ? width : 0.18f;
    c->lifetime = (lifetime > 0.05f) ? lifetime : 0.5f;
    c->funnel = funnel;
    c->serial = ++s_smokeRibbonSerial;

    SmokeRibbon_ConfigureLayers(c);
    c->trailId = SmokeRibbon_Spawn(c, slot, followTransform);
    if (c->trailId < 0) { c->active = false; return -1; }

    return (s_smokeRibbonSerial << 8) | slot;
}

// Stops |FEED only. The laid ribbon keeps drifting and fades on its own — same
// as the tube, cutting it out of existence pops.
void VFX_SmokeRibbonTrail_Stop(int handle)
{
    int slot = handle & 0xFF;
    if (handle < 0 || slot >= VFX_SMOKE_RIBBON_MAX) return;
    VC_SmokeRibbonTrail *c = &s_smokeRibbon[slot];
    if (!c->active || (handle >> 8) != c->serial) return;
    c->stopping = true;
}

static void VC_SmokeRibbonTrail_Update(float dt)
{
    for (int i = 0; i < VFX_SMOKE_RIBBON_MAX; i++)
    {
        VC_SmokeRibbonTrail *c = &s_smokeRibbon[i];
        if (!c->active) continue;

        TrailEntity *t = (c->trailId >= 0) ? GetTrail(c->trailId) : NULL;
        if (t == NULL)
        {
            c->active = false;
            continue;
        }

        // Re-push every live knob so a tuning.cfg reload takes effect without a
        // respawn — same pattern as VC_SmokeTrail_Update / VC_SmokeColumn_Update.
        SmokeRibbon_ConfigureLayers(c);
        t->uvScrollSpeed = k_smokeRibbonScroll[c->kind] * s_smokeRibbonScrollMul;
        t->uvMetresPerTile = (s_smokeRibbonTile > 0.05f) ? s_smokeRibbonTile : 0.05f;

        if (c->stopping)
        {
            KillTrail(c->trailId);
            c->trailId = -1;
            c->active = false;
        }
    }
    (void)dt;
}

// Empty ON PURPOSE — see vc_smoke_trail.inl's identical stub for why: the pair
// is how a managed composition declares itself to scripts/sync_vfx_test.py, and
// main.c's DrawTrailEntitiesBody() already puts every TrailEntity on screen.
static void VC_SmokeRibbonTrail_Draw3D(Camera3D cam)
{
    (void)cam;
}