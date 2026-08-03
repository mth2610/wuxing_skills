// ── P1. VFX_ComposeVolumeTrail — the swept VOLUME, promoted to a primary ─────
//
// WHY THIS EXISTS AND WHAT IT IS NOT.
//
// On 30/07 the trail system learned to sweep a tube, and it was verified on
// screen after four independent causes of one symptom. But the tube was only
// reachable as `VFX_TRAIL_HAZE`, a STYLE of `VFX_ComposeSweptTrail` — so
// anything that wanted a swept volume also got a weapon trail's cloth
// simulation, its per-style aspect table (1:20 blade, 1:40 thread) and its
// sparkle emitter, none of which a column of smoke has any use for.
//
// So this promotes the tube and drops those three. It does NOT reimplement it:
// every ring, cap, transported frame and noise deform still comes from
// `TRAIL_SHAPE_TUBE` → `ProceduralMesh_BuildTubeAlongPath`. **There is exactly
// one tube in this tree.** Writing a second one is the mistake this module has
// now made twice (a composition growing its own history ring beside
// core/trail_system.h; the trail system growing its own tube beside the
// procedural mesh module), and both times the existing one was better and had
// one fixable defect.
//
// WHAT IS AUTHORED HERE, i.e. why this is a composition and not "just call the
// trail system with shape = TUBE":
//
//   1. THE KIND TABLE. Three knobs — sheet, noise amplitude, flow swirl — and
//      the rule that nothing else may join them. A fourth knob is how a
//      parameter becomes three implementations.
//   2. THE BLEND LAW. Smoke occludes, energy and fire emit. Blend AND colour
//      ramp are both derived from one predicate, so they cannot drift apart.
//   3. THE ASPECT LAW. The caller's radius is a ceiling; the drawn radius is
//      also bounded by the length the emitter actually swept.
//   4. THE TIER LADDER. Radial segments, ring count and layer count all clamp
//      DOWN and the volume stays a volume at every tier.
//   5. THE ALPHA BUDGET. Two overlapping layers, summing under 1.0.
//
// Managed archetype: private pool + VC_VolumeTrail_Update/_Draw3D. That pair is
// how a stateful composition declares itself to scripts/sync_vfx_test.py.

#include "core/tuning.h"

#define VOL_MAX 8             // concurrent volumes
#define VOL_SAMPLE_HZ 60.0f   // nodes/sec, sub-frame interpolated
#define VOL_IDLE_SPEED 0.10f  // m/s below which the emitter counts as stopped
#define VOL_MIN_VERTEX 0.005f // metres; rejects exact duplicate nodes only
// A TELEPORT is not fast travel. Above this the emitter did not move, it was
// MOVED — a respawn, a blink — and laying nodes along the gap draws a straight
// streak bridging two places, the one artefact that can never read as motion.
#define VOL_TELEPORT_SPEED 45.0f
// METRES OF TUBE PER TEXTURE REPEAT. The sheet TILES along the length rather
// than stretching over it, so texel density is constant however long the volume
// is, and `swirl` below is then a real speed in tiles/sec rather than a ratio.
#define VOL_TILE 1.30f
// THE ASPECT LAW (core/docs/LANDMINES.md, "Thickness is a ratio against the
// thing's OWN length"). Half-width allowed per metre the emitter has travelled.
// 0.20 is full width : travelled length = 1:2.5 — a volume is BROAD, broader
// than the haze style's 1:3 and far broader than a blade's 1:20, and that
// breadth is most of what separates a plume from a wire.
//
// This is NOT the weapon trail's per-style table, which is the thing P1 exists
// to shed. It is ONE ratio, shared by every kind, and it only ever binds when
// the emitter has not moved far enough to have swept the radius it asked for —
// which is precisely the case where a fixed radius draws a ball.
#define VOL_ASPECT_K 0.20f
// Trails are tagged so a handle can be VALIDATED rather than trusted: trail ids
// are recycled and the pool evicts by priority, so "our" id can silently become
// somebody else's entity. With the tag an evicted volume is simply respawned,
// which makes eviction self-healing instead of fatal.
#define VOL_TAG_BASE 0x564C0000 /* 'VL' */

typedef struct
{
    bool active;
    const Matrix *xf; // caller-owned; must outlive the handle
    VC_MaterialId matId;
    VFX_VolumeKind kind;
    VFX_TrailSurface surface;
    bool hasSurface;
    TrailLayer layers[2]; // instance-owned: TrailEntity retains this pointer
    float radius;   // ceiling, metres
    float lifetime; // tail memory, seconds
    int trailId;
    bool widthLogged;
} VC_VolumeTrail;

/* HÌNH DẠNG của ống, do composition sở hữu. DrawLayeredTube từng quyết định
 * thay mọi consumer và luôn cho ra thấu kính đối xứng + nắp nón; một vệt thể
 * tích đúng ra là GIỌT NƯỚC — đầu tròn đầy, đuôi vuốt nhọn. Static vì con trỏ
 * phải sống lâu hơn mọi trail trong pool. */
static PMDropletConfig s_volTube;
static bool s_volTubeBuilt = false;

static const PMDropletConfig *VolumeTrail_Shape(void)
{
    if (!s_volTubeBuilt)
    {
        s_volTube = PMDroplet_DefaultConfig();
        /* Hai lớp sóng sin tắt: chúng TUẦN HOÀN theo cả t và phi, tức một
         * đường xoắn — gờ xoắn ốc chạy dọc thân, đọc ra là lốc xoáy. */
        s_volTube.wobbleAmplitude = 0.0f;
        s_volTube.deform1Amp = 0.0f;
        s_volTube.deform2Amp = 0.0f;
        s_volTube.noiseScale = 5.0f;
        s_volTube.noiseSpeed = 1.6f;
        s_volTubeBuilt = true;
    }
    return &s_volTube;
}

static VC_VolumeTrail s_vol[VOL_MAX];
static int s_volNextSerial = 0;
static bool s_volInit = false;

// ── The kind table, and the rule that it has exactly three columns ──────────
//
// The purpose-built volume sheets (scripts/gen_volume_trail_textures.py). They
// are seamless on BOTH axes by construction — the noise lattice wraps — which is
// what makes them legal on a tube at all: a sheet authored for a STRIP fades to
// zero at u = 0 and u = 1 because those are its two edges, and on a tube those
// two edges are the SAME LINE, so it draws a transparent seam down the whole
// length and the tube reads as half a shell.
//
// One per family because they are not interchangeable: smoke has no edges, flame
// is stretched along its travel and has a hot core, energy is strands. A single
// "nice noise" for all three is how every element ends up looking like the same
// effect tinted differently.
//
// P1: the composition names a semantic tube surface, never an asset path.
// Smoke/fire entries remain preview-only at Spawn; their profiles are retained
// so a rejected tube cannot silently grow a second local asset table.
static const VFX_SurfaceId k_volSurface[VFX_VOLUME_KIND_COUNT] = {
    VFX_SURFACE_ENERGY_TUBE,
    VFX_SURFACE_SMOKE_TUBE,
    VFX_SURFACE_FIRE_TUBE,
};
static Texture2D s_volSheet[VFX_VOLUME_KIND_COUNT];
static Texture2D s_volFlowMap[VFX_VOLUME_KIND_COUNT];

// COLUMN 2 — vertex deformation, as a fraction of the local radius. This is the
// layer that stops a swept tube being a swept tube: without it the surface is
// mathematically smooth and reads as extruded plastic however good the sheet on
// it is. Smoke billows hardest, energy holds its shape, fire sits between.
static const float k_volNoise[VFX_VOLUME_KIND_COUNT] = {0.14f, 0.34f, 0.24f};
// COLUMN 3 — the swirl: tiles per second of sheet flowing over the surface.
// NEGATIVE runs AGAINST travel, and that is the guide's rule rather than a
// preference — flow against the direction of motion reads as something being
// LEFT BEHIND, flow with it reads as a pattern being pushed along in front.
// Fire licks fastest, smoke churns slowest.
static const float k_volSwirl[VFX_VOLUME_KIND_COUNT] = {-2.60f, -0.55f, -4.20f};

// THE BLEND LAW, as one predicate. Smoke BLOCKS light; energy and fire EMIT.
// Blend mode and colour ramp are both read off this, so they cannot drift apart
// — an additive volume over a dark ramp is invisible and an alpha volume over a
// glow ramp is a bright pastel tube, and each of those has been shipped once.
static bool VolumeTrail_Emits(VFX_VolumeKind kind)
{
    return kind != VOL_SMOKE;
}

// An alpha-blended volume is read by what it TAKES OUT of the scene, so its
// colour must be dark: a saturated glow drawn at 45% opacity is coloured fog,
// not smoke. `VFX_ComposeSmokePuff` makes the same choice and says so in its own
// header — deliberately dark, brightness comes from the light that reaches it.
static Color VolumeTrail_Darken(Color c, float t)
{
    if (t <= 0.0f)
        return c;
    if (t > 1.0f)
        t = 1.0f;
    float k = 1.0f - t;
    c.r = (unsigned char)((float)c.r * k);
    c.g = (unsigned char)((float)c.g * k);
    c.b = (unsigned char)((float)c.b * k);
    return c;
}

#define VOL_RAMP_MAX 16
static ColorGradient s_volSmokeRamp[VOL_RAMP_MAX];
static bool s_volSmokeRampBuilt[VOL_RAMP_MAX];

// The occluding ramp — still the ELEMENT's colour (VFX_Material), just taken
// down to where it can absorb. Kept dark end to light end in the same tail→head
// direction as VC_ElementRamp so the two are interchangeable at the call site.
static const ColorGradient *VolumeTrail_SmokeRamp(VC_MaterialId mat)
{
    int i = (int)mat;
    if (i < 0 || i >= VOL_RAMP_MAX)
        return NULL;
    if (!s_volSmokeRampBuilt[i])
    {
        const VFX_ElementMaterial *m = VFX_Material(mat);
        ColorGradient_AddStop(&s_volSmokeRamp[i], 0.00f, VolumeTrail_Darken(m->body, 0.72f));
        ColorGradient_AddStop(&s_volSmokeRamp[i], 0.35f, VolumeTrail_Darken(m->body, 0.62f));
        ColorGradient_AddStop(&s_volSmokeRamp[i], 1.00f, VolumeTrail_Darken(m->body, 0.45f));
        s_volSmokeRampBuilt[i] = true;
    }
    return &s_volSmokeRamp[i];
}

static const ColorGradient *VolumeTrail_Ramp(VFX_VolumeKind kind, VC_MaterialId mat)
{
    return VolumeTrail_Emits(kind) ? VC_ElementRamp(mat) : VolumeTrail_SmokeRamp(mat);
}

// ── The layer stack, shared by every kind ───────────────────────────────────
//
// Two layers: a wide soft shell and the body that carries the sheet. THE
// STRUCTURE LIVES IN EXACTLY ONE OF THEM — several additive copies of one
// quasi-periodic pattern at different scroll phases average into something FLAT
// (averaging shifted copies of a pattern is how you remove it), and the wider
// layer throws the sheet's edge detail outward as spikes.
//
// THE ALPHA BUDGET, and it is arithmetic rather than taste. These overlap: the
// body sits inside the shell, so the frame buffer sees their SUM, 0.16 + 0.46 =
// 0.62. It must stay under 1.0 through the body or the sheet's detail is
// mathematically unrecoverable however good the sheet is — and the effective
// ceiling is well under 1.0, because E1's streak bloom lifts anything near the
// threshold (core/docs/LANDMINES.md, "Overlapping additive layers clip").
//
// Under BLEND_ALPHA the same two numbers are opacities rather than an emission
// budget, and 0.62 through the middle against 0.16 at the rim is what gives an
// occluding volume its falloff. One table, two readings, no branch.
//
// Per-kind only in `.texture`: three tables that differ in one pointer, because
// TrailLayer holds the sheet and a trail holds one layer array.
static TrailLayer s_volLayers[VFX_VOLUME_KIND_COUNT][2] = {
    {{.widthMul = 1.30f, .alphaMul = 0.16f, .whiten = 0.00f, .scrollMul = 0.55f, .headAlphaPow = 0.0f, .texture = NULL},
     {.widthMul = 1.00f, .alphaMul = 0.46f, .whiten = 0.08f, .scrollMul = 1.00f, .headAlphaPow = 0.0f, .texture = &s_volSheet[VOL_ENERGY]}},
    {{.widthMul = 1.30f, .alphaMul = 0.16f, .whiten = 0.00f, .scrollMul = 0.55f, .headAlphaPow = 0.0f, .texture = NULL},
     {.widthMul = 1.00f, .alphaMul = 0.46f, .whiten = 0.08f, .scrollMul = 1.00f, .headAlphaPow = 0.0f, .texture = &s_volSheet[VOL_SMOKE]}},
    {{.widthMul = 1.30f, .alphaMul = 0.16f, .whiten = 0.00f, .scrollMul = 0.55f, .headAlphaPow = 0.0f, .texture = NULL},
     {.widthMul = 1.00f, .alphaMul = 0.46f, .whiten = 0.08f, .scrollMul = 1.00f, .headAlphaPow = 0.0f, .texture = &s_volSheet[VOL_FIRE]}},
};

static void VolumeTrail_ConfigureLayers(VC_VolumeTrail *v)
{
    for (int L = 0; L < 2; L++)
        v->layers[L] = s_volLayers[v->kind][L];
    if (v->hasSurface && v->surface.texture.id != 0)
        v->layers[1].texture = &v->surface.texture;
}

// ── The tier ladder — it may only ever clamp DOWN ───────────────────────────
//
// And the volume stays a VOLUME at every tier. Falling back to a flat strip on a
// low tier is not a tier, it is a switch: a gate that can only turn a thing off
// gives the low tier a different effect rather than a cheaper one. What actually
// scales here is triangle count — radial x rings x 2 per layer — so that is what
// the ladder moves.
static int VolumeTrail_RadialSegs(void)
{
    switch (GfxQuality_Get())
    {
    case GFX_HIGH:
        return 8;
    case GFX_MED:
        return 8;
    case GFX_LOW:
        return 6;
    default:
        return 5; // still closed, still round enough to hold a rim
    }
}
static int VolumeTrail_Rings(void)
{
    switch (GfxQuality_Get())
    {
    case GFX_HIGH:
        return 24;
    case GFX_MED:
        return 20;
    case GFX_LOW:
        return 14;
    default:
        return 10;
    }
}
// The outer shell is the first thing to go: it is the widest fill in the effect
// and the one layer with no structure to lose.
static int VolumeTrail_LayerCount(void)
{
    return (GfxQuality_Get() >= GFX_MED) ? 2 : 1;
}

// ── Live dials ──────────────────────────────────────────────────────────────
// Every one of these is a look decision, and the alternative to a tunable is a
// rebuild per guess (core/CLAUDE.md §5).
static float s_volRadiusMul = 1.0f; // x the caller's radius
static float s_volAspectMul = 1.0f; // x the aspect cap
static float s_volAlphaMul = 1.0f;  // x the whole volume's opacity
static float s_volNoiseMul = 1.0f;  // x the kind's noise amplitude
// Legacy key `vol_flow`: controls only sheet UV scroll / tube-noise travel.
// Flow-map distortion has independent controls below.
static float s_volFlowMul = 1.0f;
static float s_volMapSpeed = 1.5f;
static float s_volMapStrength = 0.20f;
static float s_volMapTiling = 2.0f;
static float s_volTile = VOL_TILE;  // metres per texture repeat
static float s_volLayerMul = 1.0f;  // 0 = body only; may only clamp DOWN
// Static geometry is a diagnostic probe for UV scroll and flow-map distortion.
// Shipping/default behaviour is the moving follower; set vol_static = 1 only
// while isolating texture motion.
static float s_volStatic = 0.0f;
// SOLID WHITE, for judging the SHAPE and nothing else.
//
// "Turn everything off" does not give an opaque surface, because what makes an
// emitting volume see-through is not a setting left on — it is BLEND_ADDITIVE,
// which is what the blend law requires of anything that emits. Additive never
// occludes, so the stars show through a fully opaque-alpha tube and judging a
// silhouette through it is reading a shape through frosted glass. This flips the
// path to opaque alpha, white, no ramp, near wall only: a matte cast of the
// geometry. DEBUG ONLY — it deliberately breaks the blend law.
static float s_volSolid = 0.0f;

static void VolumeTrail_InitShared(void)
{
    if (s_volInit)
        return;
    for (int k = 0; k < VFX_VOLUME_KIND_COUNT; k++)
    {
        const VFX_SurfaceProfile *profile = VFX_SurfaceRegistry_Get(k_volSurface[k]);
        s_volSheet[k] = profile != NULL ? profile->body : (Texture2D){0};
        if (s_volSheet[k].id != 0)
        {
            // Registry owns repeat + bilinear: a tube closes around U and tiles
            // along V, so a non-seam-safe source may not enter this path.
        }
        else
        {
            // Announced. A missing sheet draws the flat fallback, which looks
            // like a deliberate bare tube — "the sheet did not load" and "the
            // sheet has no detail" are indistinguishable on screen.
            TraceLog(LOG_WARNING,
                     "VFX_VOLUME: %s body missing — kind %d falls back to a bare tube",
                     profile != NULL ? profile->name : "surface profile", k);
        }

        s_volFlowMap[k] = profile != NULL ? profile->flowMap : (Texture2D){0};
        if (s_volFlowMap[k].id != 0)
        {
        }
        else
        {
            TraceLog(LOG_WARNING,
                     "VFX_VOLUME: %s flow missing — distortion disabled for kind %d",
                     profile != NULL ? profile->name : "surface profile", k);
        }
    }
    // Lazily, never from a subsystem Init — Tuning_Init runs after those and an
    // early registration silently keeps the default (core/docs/LANDMINES.md).
    Tuning_RegisterFloat("vol_radius", &s_volRadiusMul, 1.0f);
    Tuning_RegisterFloat("vol_aspect", &s_volAspectMul, 1.0f);
    Tuning_RegisterFloat("vol_alpha", &s_volAlphaMul, 1.0f);
    Tuning_RegisterFloat("vol_noise", &s_volNoiseMul, 1.0f);
    Tuning_RegisterFloat("vol_flow", &s_volFlowMul, 1.0f);
    Tuning_RegisterFloat("vol_map_speed", &s_volMapSpeed, 1.5f);
    Tuning_RegisterFloat("vol_map_strength", &s_volMapStrength, 0.20f);
    Tuning_RegisterFloat("vol_map_tiling", &s_volMapTiling, 2.0f);
    Tuning_RegisterFloat("vol_tile", &s_volTile, VOL_TILE);
    Tuning_RegisterFloat("vol_layers", &s_volLayerMul, 1.0f);
    Tuning_RegisterFloat("vol_solid", &s_volSolid, 0.0f);
    Tuning_RegisterFloat("vol_static", &s_volStatic, 0.0f);
    s_volInit = true;
}

// ── The arithmetic, factored out so core/tests/volume_trail_test.c can mirror it

// The caller's radius is a CEILING, not a value: below the speed at which it is
// in proportion, the travelled length wins. This is the whole fix for the
// classic failure — on a hard turn the tail shortens, and a volume that keeps
// its radius through that becomes a ball.
static float VolumeTrail_Radius(float radiusMetres, float travelLen)
{
    float want = radiusMetres * s_volRadiusMul;
    float cap = travelLen * VOL_ASPECT_K * s_volAspectMul;
    if (want < 0.0f)
        want = 0.0f;
    return (cap < want) ? cap : want;
}

static int VolumeTrail_MaxNodes(float lifetime)
{
    return VC_TrailNodesForLifetime(lifetime, VOL_SAMPLE_HZ);
}

// ── Trail plumbing ──────────────────────────────────────────────────────────

// The entity this handle refers to, or NULL if it has been recycled under us.
// NEVER trust a stored trail id: ids are reused and the pool evicts by priority,
// so writing a radius into a stale one corrupts an unrelated effect.
static TrailEntity *VolumeTrail_Entity(const VC_VolumeTrail *v, int slot)
{
    if (v->trailId < 0)
        return NULL;
    TrailEntity *t = GetTrail(v->trailId);
    if (!t || !t->active || t->ownerTag != (VOL_TAG_BASE | slot))
        return NULL;
    return t;
}

static int VolumeTrail_Spawn(const VC_VolumeTrail *v, int slot)
{

    TrailConfig cfg = {0};
    cfg.type = TRAIL_TYPE_FOLLOWER;
    cfg.pos = Vector3Transform((Vector3){0.0f, 0.0f, 0.0f}, *v->xf);
    // Long-lived by construction: this dies when the caller kills it, or when it
    // stops being fed and the idle fade drains it. A lifetime here would be a
    // second, hidden death condition.
    cfg.life = 1.0e6f;
    cfg.thick = 0.05f; // real value written every frame from the aspect cap
    cfg.tint = WHITE;  // the ramp carries the colour
    cfg.gradient = VolumeTrail_Ramp(v->kind, v->matId);
    cfg.widthEnvelope = TRAIL_WIDTH_ENVELOPE_UNIFORM;
    // NO FORCE FIELD, and that is a decision rather than an omission. A weapon
    // trail is cloth — struck, draped, lagging — and simulating that is what the
    // swept trail is for. A volume's life comes from its SURFACE: the noise
    // deform moving along the tube on the same clock the sheet scrolls on. A
    // cloth step here would only make the tube writhe, which reads as a snake.
    cfg.forceField = NULL;
    cfg.ownerTag = VOL_TAG_BASE | slot;
    cfg.priority = VFX_PRIORITY_LOW;
    cfg.blendMode = VolumeTrail_Emits(v->kind) ? BLEND_ADDITIVE : BLEND_ALPHA;
    cfg.useCustomBlendMode = true; // BLEND_ALPHA is 0 and cannot be detected by >0
    cfg.minVertexDistance = VOL_MIN_VERTEX;
    cfg.disableInnerCore = true; // superseded by the layer stack
    cfg.shape = TRAIL_SHAPE_TUBE;
    cfg.tubeRadialSegs = VolumeTrail_RadialSegs();
    cfg.tubeMaxRings = VolumeTrail_Rings();
    // CAPPED. The side quads alone leave the head open and you look straight down
    // the inside — a bowl, not a volume. The teardrop profile ends in a needle at
    // the tail, so only the head actually gets a cap.
    cfg.tubeCaps = true;
    // The double wall is what gives a tube its rim for free: at grazing angles
    // the view ray crosses more material, so the silhouette brightens on its own
    // — a fresnel read with no fresnel term.
    cfg.tubeSingleSided = false;
    cfg.tubeNoiseAmp = k_volNoise[v->kind] * s_volNoiseMul;
    cfg.dropletConfig = VolumeTrail_Shape();
    cfg.layers = v->layers;
    cfg.layerCount = VolumeTrail_LayerCount();
    cfg.uvMetresPerTile = (s_volTile > 0.05f) ? s_volTile : 0.05f;
    cfg.uvScrollSpeed = k_volSwirl[v->kind] * s_volFlowMul;
    cfg.sampleHz = VOL_SAMPLE_HZ;
    cfg.teleportSpeed = VOL_TELEPORT_SPEED;
    cfg.idleSpeed = VOL_IDLE_SPEED;
    cfg.trailLength = (float)VolumeTrail_MaxNodes(v->lifetime);
    const Texture2D *flowMap = v->hasSurface ? &v->surface.flowMap
                                               : &s_volFlowMap[v->kind];
    cfg.useFlowMap = (flowMap->id != 0);
    cfg.flowMap = flowMap; // RG vector field, separate from display sheet
    cfg.flowSpeed = v->hasSurface ? v->surface.flowSpeed : s_volMapSpeed;
    cfg.flowStrength = v->hasSurface ? v->surface.flowStrength : s_volMapStrength;
    cfg.flowTiling = (v->hasSurface && v->surface.flowTiling > 0.0f)
                         ? v->surface.flowTiling : s_volMapTiling;
    cfg.noiseMask = (v->hasSurface && v->surface.noiseMask.id != 0)
                        ? &v->surface.noiseMask : NULL;
    cfg.dissolve = v->hasSurface ? v->surface.dissolve : 0.0f;
    cfg.maskTiling = (v->hasSurface && v->surface.maskTiling > 0.0f)
                         ? v->surface.maskTiling : 1.0f;

    int id = SpawnTrailEntity(cfg);
    if (id >= 0)
    {
        if (s_volStatic >= 0.5f)
        {
            // A vertical, world-space test segment: the mesh stays still while
            // UpdateTrailSystem continues its UV and flow-map clocks.
            Vector3 tail = Vector3Add(cfg.pos, (Vector3){0.0f, 0.0f, -1.20f});
            Trail_SetStaticPath(id, tail, cfg.pos, VolumeTrail_MaxNodes(v->lifetime));
        }
        else
        {
            Trail_AttachToTransform(id, v->xf, (Vector3){0.0f, 0.0f, 0.0f});
        }
    }
    // ONE LINE PER SPAWN, unconditional. "It looks the same as before" and "the
    // new path never ran" are indistinguishable on screen, and the swept trail
    // spent four rounds learning that (core/docs/VFX_PLAN.md §4.4).
    TraceLog(LOG_INFO,
             "VFX_VOLUME: slot %d — kind %d, TUBE %d radial x %d rings, %d layer(s), "
             "tier %d, noise %.2f, swirl %.2f tiles/s, %s, %s",
             slot, (int)v->kind, cfg.tubeRadialSegs, cfg.tubeMaxRings,
             cfg.layerCount, (int)GfxQuality_Get(), cfg.tubeNoiseAmp,
             cfg.uvScrollSpeed,
             VolumeTrail_Emits(v->kind) ? "ADDITIVE (emits)" : "ALPHA (occludes)",
             (s_volStatic >= 0.5f) ? "STATIC FLOW PROBE" : "FOLLOWER");
    return id;
}

// ── Public API ──────────────────────────────────────────────────────────────

int VFX_ComposeVolumeTrailEx(const Matrix *followTransform, VC_MaterialId mat,
                             float radius, float lifetime, VFX_VolumeKind kind,
                             const VFX_TrailSurface *surface)
{
    VolumeTrail_InitShared();
    if (!followTransform)
    {
        TraceLog(LOG_WARNING, "VFX_VOLUME: NULL transform — no volume created");
        return -1;
    }
    // AGAINST THE COUNT, never against the last kind by name. A silent clamp is
    // the worst failure mode available here because it produces a PLAUSIBLE
    // result rather than an absent one, so there is nothing to notice — which is
    // how every HAZE request in the tree drew a BLADE for a day
    // (core/docs/LANDMINES.md, 30/07). It announces itself.
    if (kind < VOL_ENERGY || kind >= VFX_VOLUME_KIND_COUNT)
    {
        TraceLog(LOG_WARNING,
                 "VFX_VOLUME: kind %d is out of range — clamped to VOL_ENERGY. "
                 "A new kind was probably added without updating this check.",
                 (int)kind);
        kind = VOL_ENERGY;
    }
    if (kind != VOL_ENERGY)
    {
        TraceLog(LOG_WARNING,
                 "VFX_VOLUME: kind %d is preview-only; use P2 SmokeEmitter/FlameEmitter "
                 "until owner approves smoke/fire tubes.", kind);
        return -1;
    }
    if (radius <= 0.0f)
        radius = 0.30f;
    if (lifetime <= 0.0f)
        lifetime = 0.5f;

    int slot = -1;
    for (int i = 0; i < VOL_MAX; i++)
    {
        if (!s_vol[i].active)
        {
            slot = i;
            break;
        }
    }
    if (slot < 0)
    {
        // Announced: a volume that never appears and one that was never
        // requested look identical on screen (core/CLAUDE.md §4).
        slot = s_volNextSerial % VOL_MAX;
        TraceLog(LOG_WARNING, "VFX_VOLUME: pool full (%d) — recycling slot %d",
                 VOL_MAX, slot);
        VFX_KillVolumeTrail(slot);
    }
    s_volNextSerial++;

    VC_VolumeTrail *v = &s_vol[slot];
    v->active = true;
    v->xf = followTransform;
    v->matId = mat;
    v->kind = kind;
    v->hasSurface = (surface != NULL);
    v->surface = surface ? *surface : (VFX_TrailSurface){0};
    v->radius = radius;
    v->lifetime = lifetime;
    v->widthLogged = false;
    VolumeTrail_ConfigureLayers(v);
    v->trailId = VolumeTrail_Spawn(v, slot);
    return slot;
}

int VFX_ComposeVolumeTrail(const Matrix *followTransform, VC_MaterialId mat,
                           float radius, float lifetime, VFX_VolumeKind kind)
{
    return VFX_ComposeVolumeTrailEx(followTransform, mat, radius, lifetime, kind, NULL);
}

void VFX_KillVolumeTrail(int handle)
{
    if (handle < 0 || handle >= VOL_MAX)
        return;
    VC_VolumeTrail *v = &s_vol[handle];
    // DETACH rather than kill. Cutting a volume out of existence mid-flight is a
    // pop; detaching stops the feed, and the tube then drains its own history and
    // fades — the wind-down the caller wanted.
    //
    // And detaching is not optional. The entity holds the CALLER'S Matrix, so a
    // trail still attached after the caller's storage goes out of scope is a read
    // after free every frame until the idle fade finishes.
    if (VolumeTrail_Entity(v, handle))
        Trail_AttachToTransform(v->trailId, NULL, (Vector3){0.0f, 0.0f, 0.0f});
    v->trailId = -1;
    v->active = false;
    v->xf = NULL;
}

// ── Per-frame ───────────────────────────────────────────────────────────────

// Metres of emitter path currently inside the tail window, walked from the
// entity's own history. This is what the aspect cap is measured against — the
// length the emitter ACTUALLY swept, not the caller's idea of it.
static float VolumeTrail_TravelLength(const TrailEntity *t)
{
    if (t->historyCount < 2)
        return 0.0f;
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

static void VC_VolumeTrail_Update(float dt)
{
    if (dt <= 0.0f)
        return;

    for (int i = 0; i < VOL_MAX; i++)
    {
        VC_VolumeTrail *v = &s_vol[i];
        if (!v->active)
            continue;

        TrailEntity *t = VolumeTrail_Entity(v, i);
        if (!t)
        {
            // Evicted or recycled under us. Respawning is why the tag exists:
            // eviction becomes self-healing instead of fatal.
            v->trailId = VolumeTrail_Spawn(v, i);
            continue;
        }

        // `vol_static` is a live diagnostic dial. Existing probes must be
        // reattached when it is turned off; evaluating it only at spawn left
        // their frozen flag latched forever.
        if (s_volStatic >= 0.5f && !t->frozen)
        {
            Vector3 head = Vector3Transform((Vector3){0.0f, 0.0f, 0.0f}, *v->xf);
            Vector3 tail = Vector3Add(head, (Vector3){0.0f, 0.0f, -1.20f});
            Trail_SetStaticPath(v->trailId, tail, head, VolumeTrail_MaxNodes(v->lifetime));
        }
        else if (s_volStatic < 0.5f && t->frozen)
        {
            Trail_SetFrozen(v->trailId, false);
            Trail_AttachToTransform(v->trailId, v->xf, (Vector3){0.0f, 0.0f, 0.0f});
            v->widthLogged = false;
            TraceLog(LOG_INFO, "VFX_VOLUME: slot %d — static flow probe disabled; follower restored", i);
        }

        float travel = VolumeTrail_TravelLength(t);
        t->thickness = VolumeTrail_Radius(v->radius, travel);
        // THE NUMBER THAT DECIDES WHETHER ANY OF THIS IS VISIBLE. The radius is
        // EARNED from the length the emitter swept, so a volume can be configured
        // perfectly and still draw as a hairline if the aspect cap is holding it
        // down. Reported once, when the history is full.
        if (!v->widthLogged && t->historyCount >= VolumeTrail_MaxNodes(v->lifetime))
        {
            TraceLog(LOG_INFO,
                     "VFX_VOLUME: slot %d — travelled %.2f m -> radius %.3f m "
                     "(%.2f m across). Ceiling was %.2f m.",
                     i, travel, t->thickness, t->thickness * 2.0f,
                     v->radius * s_volRadiusMul);
            v->widthLogged = true;
        }

        // The live dials, pushed every frame rather than baked at spawn —
        // otherwise judging one setting against another costs a rebuild each way,
        // which is the trap the tunable system exists to remove.
        t->uvMetresPerTile = (s_volTile > 0.05f) ? s_volTile : 0.05f;
        t->uvScrollSpeed = k_volSwirl[v->kind] * s_volFlowMul;
        t->tubeNoiseAmp = k_volNoise[v->kind] * s_volNoiseMul;
        t->flowSpeed = v->hasSurface ? v->surface.flowSpeed : s_volMapSpeed;
        t->flowStrength = v->hasSurface ? v->surface.flowStrength : s_volMapStrength;
        t->flowTiling = (v->hasSurface && v->surface.flowTiling > 0.0f)
                             ? v->surface.flowTiling
                             : ((s_volMapTiling > 0.0f) ? s_volMapTiling : 1.0f);
        t->dissolve = v->hasSurface ? v->surface.dissolve : 0.0f;
        t->maskTiling = (v->hasSurface && v->surface.maskTiling > 0.0f)
                            ? v->surface.maskTiling : 1.0f;
        // The ladder still owns the ceiling; the dial may only take it DOWN.
        int layers = VolumeTrail_LayerCount();
        if (s_volLayerMul < 0.5f)
            layers = 1;
        t->layerCount = layers;

        if (s_volSolid >= 0.5f)
        {
            // Opaque alpha, pure white, no ramp, near wall only. Every one of
            // those is needed: additive alone would still let the background
            // through, and the ramp would still tint what is meant to be a matte
            // cast of the geometry.
            t->blendMode = BLEND_ALPHA;
            t->useCustomBlendMode = true;
            t->gradient = NULL;
            t->tint = WHITE;
            t->tubeSingleSided = true;
        }
        else
        {
            t->blendMode = VolumeTrail_Emits(v->kind) ? BLEND_ADDITIVE : BLEND_ALPHA;
            t->useCustomBlendMode = true;
            t->gradient = VolumeTrail_Ramp(v->kind, v->matId);
            t->tubeSingleSided = false;
            t->tint = VC_WithAlpha(WHITE,
                                   (unsigned char)(255.0f * Clamp(s_volAlphaMul, 0.0f, 1.0f)));
        }
    }
}

// Nothing to draw. DrawTrailEntities owns the tube, which is the whole point of
// reusing it. The function stays because the Update/Draw3D PAIR is how a stateful
// composition declares itself to scripts/sync_vfx_test.py; without it the
// generator emits no update dispatch either, and the volume is never fed.
static void VC_VolumeTrail_Draw3D(Camera3D cam)
{
    (void)cam;
}
