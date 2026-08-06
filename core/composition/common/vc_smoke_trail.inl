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
// which physical end they anchor to, via `tube.anchorAtTail`
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
// `anchorAtTail` now exists in pm_tube.inl to fix properly.
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

// Same numbers as the column's k_columnNoise — re-added 05/08/2026 once
// core/geometry/pm_tube.inl's offset clamp was fixed to be basis-correct
// (PMSweptSection_ClampOffset, procedural_mesh_utils.c; proven by
// core/tests/pm_tube_offset_clamp_test.c) — see the churn block in
// SmokeTrail_BuildShape for why only the CLAMP MECHANISM differs from the
// column now, not these amplitudes.
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
static void SmokeTrail_EnsureTuning(void)
{
    static bool done = false;
    if (done) return;
    done = true;
    Tuning_RegisterFloat("smoketrail2_noise", &s_smokeTrailNoiseMul, 1.0f);
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
// `tube.anchorAtTail` (core/geometry/procedural_mesh_utils.h).
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
// `anchorAtTail` re-derives r(t) from (1-t) instead of t, moving the
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
    //
    // RENAMED from `radiusAnchorAtTail` 05/08/2026, and the rename IS the
    // third fix of this session: the flag never was about the radius alone.
    // It states one fact — "my emitter is at t=0 of this path" — and THREE
    // things anchor to the emitter: the radius profile, the deform
    // ENVELOPE (UV_ENV_HEAD_WELD = "no excursion at the source"), and the
    // centreline weld. While it re-anchored only the radius, this file got
    // the two anchors pointing at OPPOSITE ends: the churn envelope stayed
    // welded at t=0 — which, after the radius flip, is the FAT back — so
    // the widest part of the tube carried zero churn, while full churn
    // (env=1) landed at t=1, the 0.12x-radius front tip where there is
    // nothing to bulge. Product capsuleCurve x env — the absolute bulge
    // actually visible — peaked at 0.197 against the column's 1.000 with
    // the identical layer numbers. That is the "still flat" symptom, and no
    // amplitude could fix it: it scales both sides equally. Measured in
    // core/tests/pm_tube_envelope_anchor_test.c.
    c->tube.anchorAtTail = true;
    c->tube.useTransportFrame = true;

    // VERTEX DEFORM RE-ENABLED, 05/08/2026 — after 4 prior visual-only patch
    // rounds on THIS file all failed (centerlineAmp bend -> "dragged decal";
    // noiseWavelength -> "chaotic, doesn't blend"; hard offset clamp ->
    // "stair-step, squeezed"; soft-knee clamp -> same category, never
    // confirmed), the mechanism was found to have a real, numerically-
    // provable defect: core/geometry/pm_tube.inl's offset clamp based its
    // cap on the ring's NOMINAL (undeformed) radius instead of the vertex's
    // OWN deformed radius, which let the independent SCALE and OFFSET noise
    // channels combine at a single vertex to push the effective radius to
    // roughly -0.30x the nominal radius — the mesh folding through its own
    // centreline. Proven and fixed (core/geometry/procedural_mesh_utils.c's
    // PMSweptSection_ClampOffset, basis = deformed radius; verified by
    // core/tests/pm_tube_offset_clamp_test.c) BEFORE this block was
    // re-enabled, not after — see that test file for the numbers.
    //
    // The churn LAYERS below are a VERBATIM COPY of SmokeColumn_BuildShape's
    // churn (core/composition/common/vc_smoke_column.inl) — same amplitude,
    // timeScale, lattice, both MeshDeform_AddLayer calls unchanged. Retuning
    // amplitude/frequency would re-conflate this fix with an unverified
    // second variable, exactly what went wrong across the 4 prior rounds.
    // centerlineAmp (the axis bend implicated in patch #1) is still NOT
    // re-added — a separate concern, out of scope here.
    //
    // noiseWavelength/noiseOffsetScrollMul ARE now wired, 05/08/2026 — after
    // the radius fix above was confirmed (visually, by the user, matching
    // the column's own radius) to fix SHAPE, but MOTION still did not read
    // like the column. That is exactly patch #2's already-diagnosed,
    // SEPARATE problem (core/deform/README.md's "the drive coordinate is
    // not the raw parametric position"): pm_tube.inl's per-ring `t` is a
    // fraction of the CURRENT total path length, and on a moving/looping
    // emitter that length pulses every frame — the same 3 lattice cells
    // stretch and squash with instantaneous speed instead of spanning a
    // fixed number of metres, reading as pumping/breathing rather than the
    // column's stable "boiling in place". core/trails/trail_system.c
    // already computes and smooths the real span length (`tubeNoiseSpanLen`)
    // and feeds it into `noiseSpanLenOverride` EVERY FRAME, fully
    // automatically, the instant `noiseWavelength > 0` — no additional
    // wiring needed here beyond setting it.
    //
    // 5.0f is not a new number — it is the column's own `height` (its
    // frozen path is 5 m; SmokeColumn_Spawn's `height` argument, sandbox
    // fixture uses 5.0f), i.e. the column's implicit "one full 3-cell
    // lattice pass = 5 m". Reusing it means the trail's churn has the SAME
    // physical grain as the column regardless of how long the trail
    // currently is, rather than a newly-invented tuning constant.
    //
    // noiseOffsetScrollMul = 0 turns OFF the clock-driven "fake drift"
    // PMTube_DefaultConfig() sets to 1.0 for a FROZEN path (the column has
    // no other way to look like it's moving). A moving trail already gets
    // real material-age motion for free from the history buffer advancing
    // — adding the independent clock on top is a second, unrelated motion
    // source fighting the first one (see the field's own doc in
    // procedural_mesh_utils.h for the full "two clocks" rationale).
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
    c->tube.centerlineAmp = 0.0f; // NOT re-added — see block comment above
    c->tube.noiseWavelength = 5.0f;      // column's own height — see above
    c->tube.noiseOffsetScrollMul = 0.0f; // no fake clock-drift on a real mover
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
    // 24 LÁT HÌNH HỌC, tách khỏi 60 node lịch sử ở trên — 06/08/2026, và
    // hai con số này trả lời hai câu hỏi khác nhau (doc đầy đủ ở
    // trail_system.h's `tubeGeomSegs`): 60 node = "đuôi dài 1 s", 24 lát =
    // "dựng hình mịn tới đâu". Trước đây một số làm cả hai, nên đuôi dài bắt
    // buộc kéo theo lưới dày.
    //
    // Vì sao dày lại HẠI chứ không phải mịn hơn thì đẹp hơn: churn đo bằng
    // MÉT và không biết khoảng cách hai vành. Ở 48 lát (TUBE_MESH_MAX_SEGMENTS
    // kẹp 60 xuống 48) trên ~3.9 m thì ringGap chỉ 8.1 cm, trong khi churn
    // đẩy vành lệch 0.30-0.66 m — bề mặt giữa hai vành liền kề dốc 75-80°,
    // pháp tuyến dựng bằng sai phân trung tâm gần như nằm ngang và đảo dấu
    // liên tục (nhìn thấy trực tiếp ở volume_debug=5: sọc magenta/lục xen kẽ
    // TỪNG VÀNH ở đoạn đuôi mảnh). Cột khói cùng biên độ chỉ dốc 50° vì
    // ringGap của nó là 12.5 cm — nó chưa bao giờ dính lỗi này.
    //
    // 24 lát cho ringGap ~16 cm, tức THOÁNG HƠN cả cột khói, và vì trần
    // offset cũng đo theo ringGap (PM_TUBE_MAX_OFFSET_RINGS) nên nó đồng
    // thời nới gấp đôi cho tầng NORMAL_OFFSET đang bị kẹp 20-65% thời gian.
    cfg.tubeGeomSegs = 24;
    // FALSE on purpose — trail_volume.fs drops the far wall by its normal
    // (`if (facing < 0.0) discard;`), not by winding: PMTube_DrawFaded's
    // winding is inward, so GL backface culling would keep the wrong side.
    cfg.tubeSingleSided = false;
    cfg.tubeVolumeShading = true;
    // FALSE, not the column's live freeze toggle — this trail is always
    // moving, so there is no "frozen path, still churning" state to debug
    // here the way the column has (smokecolumn_freeze).
    cfg.tubeDeformFrozen = false;
    // Same amplitude the column uses per kind — see the churn block in
    // SmokeTrail_BuildShape for why re-enabling this only needed the
    // pm_tube.inl clamp fix, not a new number.
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
             "%d nodes, MESH %d x %d radial | churn %.2f, scroll %.2f tiles/s, "
             "tile %.2f m, sheet id %u",
             slot, (int)c->kind, c->lifetime, cfg.tubeMaxRings,
             cfg.tubeGeomSegs, cfg.tubeRadialSegs,
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
