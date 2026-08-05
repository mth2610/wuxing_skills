// H2 — SmokeTrail: the MOVING counterpart of vc_smoke_column.inl's SmokeColumn.
//
// SPLIT INTO ITS OWN FILE, not a mode flag on VC_SmokeColumn — two archetypes,
// two files, same reasoning as the column/vc_volume_trail.inl split it mirrors.
//
// TEXTURE/CHURN TASTE IS A VERBATIM COPY of SmokeColumn_ConfigureLayers —
// same sheets, same noise amplitude/timescale/lattice, same blend law. A
// stationary column and a moving trail should look like the same material
// behaving two different ways, not two different materials.
//
// THE TAPER NUMBERS ARE ALSO A VERBATIM COPY (radiusTailFrac/radiusPow,
// 0.12/1.7 funnel, 0.55/1.4 cylinder) — what differs from the column is
// which physical end they anchor to, via `tube.radiusAnchorAtTail`
// (core/geometry/procedural_mesh_utils.h, wired in pm_tube.inl). The column
// is small at its fixed source and widens toward the far end; this file
// needs small at the FRONT (current/leading position) and large at the BACK
// (old, dispersing end) — "smoke rolls from front to back" on a moving
// emitter. trail_system.c's tube parametrisation for a follower runs the
// OPPOSITE way from the column's static path (t=1 is the column's far end
// but the trail's current/leading end — see the derivation at
// SmokeTrail_BuildShape), so reaching the same visual intent with the SAME
// numbers needs the anchor flipped, not the numbers changed. An earlier
// version of this file tried to reach the same shape by pushing
// radiusTailFrac past 1 instead (bending a parameter documented as [0,1] to
// fight the formula's fixed anchor) and it ballooned the back end to
// several times the requested radius — see git history. That is what
// `radiusAnchorAtTail` now exists in pm_tube.inl to fix properly.
//
// THE PATH MECHANISM IS THE OTHER DIFFERENCE: SmokeColumn_Spawn ends with
// `Trail_SetStaticPath` — a frozen vertical segment, seeded once (see
// vc_smoke_column.inl's own header for why: a real ForceField-driven moving
// column was tried once and cost three bugs in a row before being
// abandoned). This file ends with `Trail_AttachToTransform` instead — the
// exact mechanism vc_volume_trail.inl uses for a moving emitter: the trail
// system itself lays new history nodes as the caller's transform moves, and
// drops old ones past `lifetime`. Real trail-follower behaviour, not a
// frozen shape dragged around.

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
    // UNUSED as of 05/08/2026 — tube.noiseField stays NULL (see the "NO
    // VERTEX DEFORM" block in SmokeTrail_BuildShape). Kept declared rather
    // than deleted: the struct still needs SOME field of this type if a
    // future revision ever wants a CPU-side field for something other than
    // pm_tube.inl's vertex deform (unlikely) — delete outright if that
    // hasn't happened by the time the shader churn work starts.
    MeshDeformField churn;
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

// k_smokeTrailNoise removed 05/08/2026 with the vertex-deform amplitude it
// fed (cfg.tubeNoiseAmp is now hardcoded 0 — see "NO VERTEX DEFORM" in
// SmokeTrail_BuildShape). Re-add a per-kind table when the shader churn
// lands, since its amplitude will likely want to differ by kind too.
static const float k_smokeTrailScroll[VFX_COLUMN_KIND_COUNT] = {0.55f, 0.95f, 0.40f};

// Live knobs, own namespace from the column's (smokecolumn_*) so tuning one
// archetype never silently retunes the other. Registered lazily on first use
// — never from an Init, Tuning_Init runs after subsystem inits in main.c
// (core/docs/LANDMINES.md).
static float s_smokeTrailScrollMul = 1.0f;
static float s_smokeTrailAlphaMul = 1.0f;
static float s_smokeTrailTile = 3.00f;
// smoketrail2_noise/_freeze/_bend/_wavelength/_agescroll REMOVED 05/08/2026,
// not just left at neutral — a registered tunable that no longer drives
// anything is a "silent knob does nothing" landmine (see vc_strand_trail.inl
// for the same principle applied to its own style-override guard). They
// drove pm_tube.inl vertex-deform math (centerlineAmp bend, noise churn
// amplitude/coordinate) that this shape no longer uses at all — see the "NO
// VERTEX DEFORM" block in SmokeTrail_BuildShape for why. Re-add fresh
// tunables (new names, since these concepts don't map 1:1) when the
// shader-side churn (trail_volume.fs, arc-length-anchored like
// trail_deform.fs mode 2) actually lands.
static void SmokeTrail_EnsureTuning(void)
{
    static bool done = false;
    if (done) return;
    done = true;
    Tuning_RegisterFloat("smoketrail2_scroll", &s_smokeTrailScrollMul, 1.0f);
    Tuning_RegisterFloat("smoketrail2_alpha", &s_smokeTrailAlphaMul, 1.0f);
    Tuning_RegisterFloat("smoketrail2_tile", &s_smokeTrailTile, 3.00f);
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

// Shares the column's texture/churn taste AND the column's exact taper
// numbers (SmokeColumn_ConfigureLayers is still called verbatim, and
// radiusTailFrac/radiusPow below are copy-pasted, not retuned) — what
// differs is which PHYSICAL end they are anchored to, via
// `tube.radiusAnchorAtTail` (core/geometry/procedural_mesh_utils.h).
//
// A moving trail is not a stationary column: freshly emitted smoke is a
// tight puff at the emitter and billows out into a wide wake behind it as
// it disperses — small at the FRONT (current/leading position), large at
// the BACK (old, dispersing end). Confirmed directly from
// core/trails/trail_system.c, not assumed: DrawLayeredTube's
// `path[i] = scratchOuter[n-1-i]` (:1479), and the projectile-node loop
// tagging h=0 "head" / h=drawCount-1 "tail" (:1826), put the tube's t=0 at
// the OLD/trailing end (the back) and t=1 at the CURRENT/leading end (the
// front) — the opposite of the column's static path, where t=0 is the
// fixed source and t=1 is the far/old end.
//
// pm_tube.inl's r(t) = tailFrac + (1-tailFrac)*t^p always pins t=1 to
// EXACTLY headR (the (1-tailFrac) term cancels there), so with the column's
// plain formula the trail's FRONT (t=1) would be stuck at the caller's full
// requested radius and only the BACK (t=0) could move — shrinking the front
// is not reachable that way, and forcing it by pushing tailFrac past 1
// instead balloons the back to `tailFrac x headR` (measured: 8.33 x 0.35 m
// = a 2.9 m back end — visibly bloated, not proportionally tapered; see
// this file's git history, that version shipped and was wrong). That is a
// caller bending a parameter outside the domain its own doc describes
// ("bán kính ở đuôi, tỉ lệ so với đầu") to fight the formula's fixed anchor,
// not a fix.
//
// The actual fix lives in pm_tube.inl/procedural_mesh_utils.h:
// `radiusAnchorAtTail` re-derives r(t) from (1-t) instead of t, moving the
// pinned-at-headR point from t=1 to t=0. With it set, headR — the radius the
// CALLER asked for — lands on the BACK (t=0, correct: that is the "full"
// dispersed size), and the FRONT (t=1) becomes `tailFrac x headR`, with
// tailFrac staying in its documented [0,1] domain, same 0.12/0.55 the
// column already uses. No number here differs from the column's; only the
// anchor does.
static void SmokeTrail_BuildShape(VC_SmokeTrail *c, bool funnel)
{
    c->tube = PMTube_DefaultConfig();

    c->tube.wobbleAmplitude = 0.0f;
    c->tube.deform1Amp = 0.0f;
    c->tube.deform2Amp = 0.0f;

    // Same numbers as vc_smoke_column.inl's SmokeColumn_BuildShape,
    // unchanged — see the header comment above for why only the anchor,
    // not the numbers, needs to differ from the column.
    if (funnel) { c->tube.radiusTailFrac = 0.12f; c->tube.radiusPow = 1.7f; }
    else        { c->tube.radiusTailFrac = 0.55f; c->tube.radiusPow = 1.4f; }
    // THE re-anchor. headR (the caller's requested radius) lands on the
    // BACK (t=0) instead of pm_tube.inl's default FRONT (t=1) — see the
    // header comment above.
    c->tube.radiusAnchorAtTail = true;
    c->tube.useTransportFrame = true;

    // NO VERTEX DEFORM ON THIS SHAPE — ARCHITECTURE DECISION, 05/08/2026,
    // reverses the four previous fixes on this file (centerlineAmp bend,
    // noiseWavelength, noiseOffsetScrollMul, the pm_tube.inl offset clamps).
    // Every one of those was patching the SAME underlying mistake from a
    // different angle, each patch only moving where the artifact showed up:
    //   1. centerlineAmp=column's value -> "dragged decal" (real motion +
    //      synthetic bend fighting each other)
    //   2. centerlineAmp=0, noiseWavelength fix -> "chaotic, doesn't blend"
    //      (raising noise amplitude just made the remaining offset noise
    //      louder, not more alive)
    //   3. radius-relative offset clamp -> "sharp stair-step edges, feels
    //      squeezed" (a HARD clamp forces every over-limit vertex onto the
    //      exact same sphere, creating a wall between clamped and free
    //      vertices)
    //   4. soft-knee clamp -> same category of artifact, softer
    // vc_strand_trail.inl already diagnosed this exact failure mode and
    // solved it correctly, for the same reason: "displacing the VERTICES
    // read as a rigid rope... folds through itself on a tight turn... the
    // wave belongs in the FRAGMENT stage, where it is arc-length anchored
    // and cannot touch the geometry." Vertex-space noise displacement on a
    // TUBE that both TAPERS steeply (0.12x at the front) AND MOVES through
    // real, curving space cannot be made to not self-intersect/wall/stair-
    // step by clamping harder — the clamp itself is what creates the visible
    // edge, no matter how it is shaped.
    //
    // So: pm_tube.inl builds PURE, UNDEFORMED geometry for this shape —
    // radius taper only, no noiseField, no centerlineAmp, no offset, nothing
    // for any clamp to ever have to fight. c->churn is deliberately left
    // unbuilt (not just unused) — there is nothing here for it to drive.
    // The organic surface churn belongs in trail_volume.fs instead (a UV-
    // space warp anchored to real metres of laid path, mirroring
    // trail_deform.fs mode 2's technique) — NOT YET IMPLEMENTED. Until it
    // is, this shape's only surface motion is the existing sheet pan
    // (SmokeTrail_ConfigureLayers' scrollMul) — flatter than the column,
    // stable, no artifacts. Do not re-wire c->tube.noiseField/centerlineAmp
    // to "bring the detail back" without building the shader-side churn
    // first, or every artifact above comes back.
    c->tube.noiseField = NULL;
    c->tube.centerlineAmp = 0.0f;
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
    // 0, deliberately — see the "NO VERTEX DEFORM" block in
    // SmokeTrail_BuildShape. tubeNoiseAmp>0 with noiseField==NULL still
    // builds an internal preset field in PMTubeShapeDeformNoise and deforms
    // anyway (pm_tube.inl's field==NULL branch) — both have to be off
    // together, not just noiseField.
    cfg.tubeDeformFrozen = false;
    cfg.tubeNoiseAmp = 0.0f;

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
        // NO vertex deform to re-push — see the "NO VERTEX DEFORM" block in
        // SmokeTrail_BuildShape. tubeNoiseAmp/tubeDeformFrozen/centerlineAmp/
        // noiseWavelength/noiseOffsetScrollMul all stay at the geometry-only
        // defaults set at spawn (0 / false / 0 / whatever / whatever — the
        // last two are moot with noiseAmp 0). Only the sheet PAN below is
        // this shape's live surface motion right now.
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
