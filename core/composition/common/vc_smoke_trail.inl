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
// LIVE DIAL for the "dragged decal, not venting smoke" read reported
// 05/08/2026. Hypothesis: centerlineAmp is a SYNTHETIC lateral bend, copied
// verbatim from the column, where it is the ONLY source of shape motion (the
// column never moves, so the bend IS the motion cue). The trail already gets
// real shape variation for free from the emitter's actual path — an
// independent noise bend competing with genuine motion is what a rigid
// shape riding along a spline (a decal) looks like, not what material
// genuinely trailing behind motion looks like. 0 = bend off, pure real-path
// shape; 1 = the column's amount, unchanged. Tune down first before touching
// anything else — this is the cheapest lever to test the hypothesis with.
static float s_smokeTrailBendMul = 1.0f;
// TRAIL SYSTEM FIX, 05/08/2026 — core/geometry/pm_tube.inl's noiseWavelength.
// Confirmed from actual test footage (fixture 24's Lissajous path): a moving
// trail's recorded path length pulses with the emitter's instantaneous speed
// (minVertexDistance gates node-adding by real distance, but tubeMaxRings —
// the mesh's ring COUNT — is fixed), so sampling the churn at a raw [0,1]
// fraction of "current path length" stretches/squashes the noise's spatial
// grain in sync with speed — a pure geometry-pumping artifact with nothing
// to do with real smoke, and the root of the "dragged decal" read. 5.0
// matches the column's own height in the live fixture — same latticeAlong=3
// churn, same ~1.67 m per cell, so the two archetypes read as the same
// material regardless of which one happens to be moving.
static float s_smokeTrailWavelength = 5.0f;
// TRAIL SYSTEM FIX #2, 05/08/2026 — pm_tube.inl's noiseOffsetScrollMul.
// Raising smoketrail2_noise made the trail MORE chaotic, not more alive —
// it did not "blend" with the trail's own motion. Root cause, reasoned from
// first principles (the column's IDENTICAL noise formula — normal vector +
// RGB-channel field + time — already looks correct when the mesh doesn't
// translate, so the formula itself was never the problem):
//
// runNoiseOffset (core/trails/trail_system.c) is a noise-coordinate scroll
// driven by a REAL-TIME clock (-uvScrollOffset*0.5), unconditionally applied
// to every TUBE trail. The column NEEDS it: its path is frozen, so t carries
// no notion of material age (t=0 is forever the fixed source) — the ONLY way
// its noise can look like it's evolving is to scroll the sampling coordinate
// against wall-clock time. That is its one and only motion source, so it
// reads as coherent.
//
// A MOVING trail's t already means something else: t tracks a ring's
// position in the live history buffer, i.e. its MATERIAL AGE — old material
// sits near t=0, freshly emitted material near t=1, driven by the emitter's
// REAL motion, for free. Layering runNoiseOffset's independent, constant-
// rate clock on top of that is two uncorrelated motions driving the same
// field: the noise pattern marches at a fixed rate while the material's own
// age is marching at whatever rate real motion dictates. Raising amplitude
// just makes that mismatch louder — exactly the "more chaotic, doesn't
// blend" symptom.
//
// 0.0 = fully decoupled: the trail's OWN t-to-age mapping supplies
// "material changes as it ages" for free, with no synthetic scroll fighting
// it. `time` alone (MeshDeform_Evaluate's own parameter, not this scroll)
// still lets the field breathe in place. 1.0 = the column's mechanism,
// unchanged, if this turns out to be wrong.
static float s_smokeTrailAgeScrollMul = 0.0f;

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
    Tuning_RegisterFloat("smoketrail2_bend", &s_smokeTrailBendMul, 1.0f);
    Tuning_RegisterFloat("smoketrail2_wavelength", &s_smokeTrailWavelength, 5.0f);
    Tuning_RegisterFloat("smoketrail2_agescroll", &s_smokeTrailAgeScrollMul, 0.0f);
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

    // s_smokeTrailBendMul re-applied every frame in VC_SmokeTrail_Update too
    // (tuning.cfg live-reload) — set here so frame 1, before the first
    // Update tick, already reflects the current dial.
    c->tube.centerlineAmp = c->radius * 1.6f * s_smokeTrailBendMul;
    c->tube.useTransportFrame = true;
    // THE fix for "moves like a dragged picture" — see s_smokeTrailWavelength's
    // own comment. Re-pushed in Update too.
    c->tube.noiseWavelength = s_smokeTrailWavelength;
    // THE fix for "raising noise makes it more chaotic, not alive" — see
    // s_smokeTrailAgeScrollMul's own comment. Re-pushed in Update too.
    c->tube.noiseOffsetScrollMul = s_smokeTrailAgeScrollMul;

    MeshDeform_Clear(&c->churn);
    c->churn.amplitude = 1.0f;
    c->churn.timeScale = 0.75f;
    c->churn.latticeAround = 3;
    c->churn.latticeAlong = 3;

    // ENVELOPE MIRRORED TOO, same t=0/t=1 mapping as radiusTailFrac above,
    // and for the same visual reason: "smoke rolls from front to back" means
    // the surface should be QUIET at the front (t=1, fresh) and churn
    // increasingly toward the back (t=0, old) as it disperses. UV_ENV_HEAD_WELD
    // / _SQ (mesh_deform.h) are `smoothstep(start,end,c) * c` / `* c*c` — that
    // trailing `* c` factor pins the excursion to exactly zero at c=0
    // regardless of start/end, so they can only ever weld at t=0 and cannot
    // be pointed at t=1. UV_ENV_SMOOTHSTEP has no such anchor — plain
    // `clamp((c-start)/(end-start), 0, 1)` — and DOES invert when
    // start > end (core/uv/uv_deform.c). envStart=1.0f/envEnd=0.78f|0.65f
    // welds it at the front and lets it run free toward the back, mirroring
    // the column's start=0.0f/end=0.22f|0.35f (welded at ITS t=0, the source).
    MeshDeform_AddLayer(&c->churn, (MeshDeformLayer){
        .kind = MESH_DEFORM_NOISE_CHANNEL,
        .direction = MESH_DEFORM_DIR_NORMAL_SCALE,
        .tiling = {1.0f, 1.0f}, .amplitude = 4.2f, .speed = 1.0f,
        .latticeMul = 1.0f, .latticeAroundMul = 1.0f, .env = UV_ENV_SMOOTHSTEP,
        .envStart = 1.0f, .envEnd = 0.78f,
    });
    MeshDeform_AddLayer(&c->churn, (MeshDeformLayer){
        .kind = MESH_DEFORM_NOISE_CHANNEL,
        .direction = MESH_DEFORM_DIR_NORMAL_OFFSET,
        .tiling = {1.0f, 1.9f}, .amplitude = 1.30f, .speed = 1.7f,
        .timeOffset = 11.0f, .latticeMul = 3.0f, .latticeAroundMul = 2.0f,
        .env = UV_ENV_SMOOTHSTEP, .envStart = 1.0f, .envEnd = 0.65f,
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
        // c->tube is the SAME struct t->tubeShapeConfig points at (set once
        // in SmokeTrail_Spawn), so writing here reaches the live geometry
        // next draw — no respawn needed to sweep smoketrail2_bend/wavelength/
        // agescroll.
        c->tube.centerlineAmp = c->radius * 1.6f * s_smokeTrailBendMul;
        c->tube.noiseWavelength = s_smokeTrailWavelength;
        c->tube.noiseOffsetScrollMul = s_smokeTrailAgeScrollMul;

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
