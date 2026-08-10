// ── VFX_ComposeTrail — THE trail composition ────────────────────────────────
//
// UNIFIED 10/08/2026 (was vc_ribbon_trail.inl; vc_strand_trail.inl folded in).
// There used to be two composers describing the same object with two private
// style tables, backed by two hand-written modes of one fragment shader. A trail
// is now DATA: a `TrailRecipe` (core/trails/trail_recipe.h) that says which
// sheet, how to warp its coordinate, how to sample it, what to mask, what
// colour, and how the BODY and EMISSION passes each resolve. `k_trailPresets[]`
// below is that data; adding a trail is a row, not a branch.
//
// The two bugs on 10/08/2026 are what made the case. A stale `strandtrail_style`
// in tuning.cfg swapped an entire style row without the trail's own description
// having any say — possible only because "which style" was a hidden global. And
// an emission weight was reused as alpha coverage in two duplicated layer loops,
// so the identical fix had to land twice. Both are shape problems, not value
// problems, and neither can recur against a recipe: the description travels with
// the trail, and there is one formula to fix.
//
// LOOK vs MOTION. `TrailRecipe` is look only. Motion — the aspect cap, the
// cloth, the strand count, the sample clock — is `k_trailMotion[]` here, because
// it is authoring too but it belongs to the simulation half of trail_system.c
// rather than to the surface. Keeping them as two tables is deliberate: they
// change for different reasons and are judged by different evidence (a headless
// number vs. an eyeball).
//
// PORTED ONTO core/trail_system.h, 29/07. The first version of this file grew
// its own history ring, its own fixed-rate sample clock, its own cloth
// simulation and its own layered draw — 849 lines of code — right next to a
// trail system with 18 public entry points and, after the F0 purge, zero
// consumers. H1 existed to BE that consumer and had quietly become a second
// implementation instead.
//
// Every mechanism it had is now in the engine, where the other three trail types
// get it too: `TrailConfig.layers`, `.uvMetresPerTile`, `.nodeHomeSpring`,
// `.nodeOrderFrac`, `.sampleHz`, `.teleportSpeed`, `.idleSpeed`. Three of those
// fixed bugs that had been sitting in `trail_system.c` the whole time — FOLLOWER
// laid one node per FRAME, had no constraint at all, and built its UV from
// `segRatio`, which both stretches with the trail's length and is anchored to
// the moving head.
//
// WHAT IS LEFT HERE IS AUTHORING, and it is why this is a composition and not
// "just call the trail system". The trail system takes a config and knows
// nothing about elements, the blend law, the tier budget or the aspect rule.
// Five decisions live here and cannot sensibly live in a caller:
//
//   1. THE PLANE. BLADE runs RIBBON_FIXED_NORMAL with the normal recomputed
//      every frame from the tip's own path. That is what makes it read as an
//      object rather than a decal.
//   2. THE ASPECT RATIO. Width is capped against the length the tip ACTUALLY
//      travelled, not against the caller's number (core/docs/LANDMINES.md,
//      "Thickness is a ratio against the thing's OWN length"). A slow weapon
//      gets a thin trail instead of a fat stub.
//   3. THE LAYER STACK. Three strips — wide soft glow, textured body, hot core —
//      with the structure in exactly ONE of them.
//   4. THE CLOTH. Per style: a blade's trail is struck, silk is draped.
//   5. THE TIER GATE. FILAMENT sheds strands below GFX_MED, and only ever
//      clamps DOWN.
//
// TWO THINGS WERE DROPPED IN THE PORT, both deliberately:
//
//   - The SCREEN-SPACE WIDTH FLOOR. It existed to stop the blade rendering as a
//     dotted line, and it never did. The dashing turned out to be the ribbon
//     FOLDING: an absolute deviation bound of 0.30 m against a 0.12 m node
//     spacing let a node pass its own neighbour. That is fixed at the source
//     (`nodeOrderFrac`), so the floor is a workaround for a bug that no longer
//     exists — and keeping it would drag a camera dependency into an update path
//     with no business knowing about one.
//   - The FILAMENT LAG SCHEDULE. Strands used to read one path at staggered
//     delays so they would not overlap. Each strand is now its own entity at its
//     own lateral offset, sampling the cloth field at its own position — so they
//     diverge because they are in different places in a moving air field, which
//     is what happens to a real bundle of threads, rather than because they are
//     copies of one path in the past.
//
// Managed archetype: private pool + VC_SweptTrail_Update/_Draw3D. That pair is
// how a stateful composition declares itself to scripts/sync_vfx_test.py, so the
// Draw3D half stays even though the trail system now does the drawing.

#include "core/tuning.h"
#include "core/trails/trail_recipe.h"
#include "core/uv/uv_fx.h"

#define SWEPT_MAX 8         // concurrent trails
#define SWEPT_STRANDS_MAX 4 // strands per trail (FILAMENT uses all of them)
#define VFX_LEGACY_VOLUME_HANDLE_FLAG 0x40000000
#define SWEPT_SAMPLE_HZ 60.0f
#define SWEPT_IDLE_SPEED 0.12f  // m/s below which the tip counts as stationary
#define SWEPT_MIN_VERTEX 0.005f // metres; rejects exact duplicate nodes only
// A TELEPORT is not a fast swing. Above this the tip did not travel, it was
// moved — a respawn, a blink — and laying nodes along the gap draws a long
// straight streak BRIDGING two places, the one artefact in a trail that can
// never be mistaken for motion.
#define SWEPT_TELEPORT_SPEED 45.0f // m/s — well above any weapon tip
#define SWEPT_SPARK_RATE 26.0f     // sparks/sec shed along the ribbon
// How hard a node is held to the path it was laid on, and the ceiling on how far
// it may stray ACROSS that path. Deviation settles near force/spring: with the
// cloth forces below, a few centimetres of flutter.
#define SWEPT_HOME_SPRING 9.0f
#define SWEPT_HOME_MAX_DEV 0.30f
// ALONG the path, as a fraction of the node spacing. Must be < 0.5: both ends of
// a segment move, so 2x this is the most the gap can close, and anything >= 0.5
// lets neighbours swap places — a FOLD, which no distance constraint can undo.
#define SWEPT_ORDER_FRAC 0.45f
// The sheet TILES along the trail rather than stretching over it, so texel
// density stays constant however long the tail is, and it SCROLLS so the fibres
// flow. Metres per tile, and tiles per second OVER THE CLOTH — with the material
// UV that rate is exactly what it says, whatever the emitter is doing.
#define SWEPT_FLOW_TILE 1.10f
// Tiles per second over the cloth. 2.10 was bracketed on the flat RIBBON, where
// one tile is 1.10 m of a 3 m strip — under three tiles visible, so two a second
// crosses it quickly. A TUBE is longer and its texture wraps, so the same number
// reads as barely moving on it; the dial carries the difference rather than a
// second constant, but the base is raised so `swept_flow = 1` is already a flow
// rather than a drift.
#define SWEPT_FLOW_SPEED 3.60f
// Trails are tagged so a handle can be VALIDATED rather than trusted. Trail ids
// are recycled and the pool evicts by priority, so "our" id can silently become
// somebody else's entity — writing a thickness or a normal into that would
// corrupt an unrelated effect. With the tag an evicted strand is simply
// respawned, which makes eviction self-healing instead of fatal.
#define SWEPT_TAG_BASE 0x57540000 /* 'WT' */

typedef struct
{
    bool active;
    const Matrix *xf; // caller-owned; must outlive the handle
    VC_MaterialId matId;
    TrailPresetId kind;
    VFX_TrailSurface surface;
    bool hasSurface;
    TrailLayer layers[3]; // instance-owned: TrailEntity retains this pointer
    // Instance copy of the preset row. The renderer keeps a pointer to it for
    // the trail's whole life, so it must live in the pool, not in the shared
    // table (which a live tunable would otherwise edit for every trail at once).
    TrailRecipe recipe;
    float width;       // full width in metres, before the aspect cap
    float widthTarget; // 0..1, set by VFX_TrailSetWidth
    float widthLevel;  // what is actually drawn — ramps toward target
    float lifetime;    // tail memory, seconds
    int strandId[SWEPT_STRANDS_MAX];
    int strands;
    float sparkAcc;
    Vector3 normal; // the swing plane's normal (BLADE's fixedNormal)
    bool hasNormal;
    Vector3 lateralAxis; // `normal` with a CONTINUOUS sign (FILAMENT spread)
    bool hasLateral;
    bool wasFrozen;
    bool widthLogged;
} VC_SweptTrail;

static VC_SweptTrail s_swept[SWEPT_MAX];
static int s_sweptNextSerial = 0;
static bool s_sweptInit = false;
static Texture2D s_sweptBladeTex = {0}; // procedural streak sheet — the fallback
static Texture2D s_sweptHaloTex = {0};  // the SAME band with NO structure — glow pass
// Whichever of the two the dial selects. The layer table holds its ADDRESS, so
// swapping sheets is one assignment and takes effect on the next draw.
static Texture2D s_sweptBodyTex = {0};
// Registry-resolved sheet per preset, kept engine-owned (ResourceManager holds
// the texture; this is only a handle so a trail never owns a filename).
static Texture2D s_trailSheet[TRAIL_PRESET_COUNT] = {0};
// THE SAME FILAMENTS, SEAMLESS ACROSS u — for a swept TUBE.
//
// How much the ribbon behaves like cloth. BLADE is nearly a record (a sword's
// trail is struck, not draped); MAIN is silk; WISP is more controlled; and a
// BACKDROP stays broad, soft and close to the travelled path.
static ForceField s_sweptCloth[TRAIL_PRESET_COUNT];

// The dust's twinkle: three beats inside one life, so a mote pulses instead of
// fading flat. FLOAT_CURVE_MAX_STOPS is 8, which is exactly three peaks.
static SkillCurve s_sweptTwinkle;

static SkillCurve s_sweptWidthCurve[TRAIL_PRESET_COUNT];
static SkillCurve s_sweptAlphaCurve[TRAIL_PRESET_COUNT];

// Live-tunable: every one of these is a look decision, and the alternative to a
// tunable is a rebuild per guess (core/CLAUDE.md §5).
static float s_sweptWidthMul = 1.0f;  // x on the caller's width
static float s_sweptAspectMul = 1.0f; // x on the aspect cap (the 1:20 rule)
static float s_sweptSpread = 2.2f;    // x on the FILAMENT bundle's width
static float s_sweptSpark = 1.0f;     // x on the sparkle rate, 0 = none
static float s_sweptAlphaMul = 1.0f;  // x on the whole trail's opacity
// x on the flow rate. NEGATIVE by default, and that is the guide's rule, not a
// preference: "UV luôn chạy ngược chiều projectile" — the flow runs AGAINST the
// direction of travel, which is what reads as something being left behind rather
// than as a pattern being pushed along in front.
static float s_sweptFlow = -1.0f;
static float s_sweptSag = 1.0f;     // x on how much the ribbon sags
static float s_sweptCoreHot = 1.0f; // x on the white-hot head
// How much the swept trail OCCLUDES in the BLEND_ALPHA body pass. The layer
// stack above carries EMISSION weights (they sum as light); additive alone can
// only ADD, so over a bright destination it pushes toward white and the trail
// loses its hue — measured peak chroma 0.32 on a bright clear vs 0.61 on the
// night sky. This is the separately-authored coverage the body pass draws with
// instead (trail_system.h, TrailMaterialConfig::bodyOpacity).
//
// 1.0, and it is not "fully opaque": only layer 1 — the flow-sheet BODY layer —
// draws in the body pass at all (the halo and the hot core stay emission-only),
// and its coverage is still shaped by the sheet's own soft alpha, the width
// taper and the lifetime curve. 1.0 means "draw that layer at the alpha it was
// authored with", which is the whole point; anything less re-applies an
// emission weight as coverage, which is the bug this replaced. Measured peak
// chroma over a bright destination: 0.31 at 0.0 (the old behaviour), 0.40 at
// 0.55, 0.72 at 1.0 — against 0.61 over the night sky. Lower it per-effect for
// something that really is mostly glow (a spark wants 0).
static float s_sweptBodyOpacity = 1.0f;
// The procedural finite-streak sheet is the neutral default. The old authored
// `energy_flow.png` has a strong ornamental rhythm, so it is now opt-in only:
// it must be chosen intentionally by a look-dev dial or supplied per-instance
// through VFX_TrailSurface, never imposed on every element trail.
static float s_sweptTile = SWEPT_FLOW_TILE;
// Hold the shape still and let ONLY the flow move. The decisive instrument for
// "is the energy actually flowing": a moving shape with a moving texture and a
// moving shape with a painted-on one look exactly the same.
static float s_sweptFreeze = 0.0f;
// Uneven by design: evenly spaced strands of equal width read as a comb, no
// amount of colour fixes it, and the regularity is the thing the eye picks up.
static const float k_sweptSpread[SWEPT_STRANDS_MAX] = {-1.00f, -0.50f, 0.38f, 1.00f};

// ── The arithmetic, factored out so core/tests/swept_trail_test.c can mirror it ─

// Half-width allowed per metre the tip has travelled. THE RATIO IS THE POINT:
// a band's aspect against its OWN length is what decides whether it reads as a
// blade, a cloth or a thread — full width : travelled length is 1:20, 1:10 and
// 1:40 here (core/docs/LANDMINES.md, "Thickness is a ratio against the thing's
// OWN length"; the 1:20 blade figure is the one VFX_ComposeSweepSlash landed on).
// Half-width is half of that, hence 0.025 / 0.05 / 0.0125.
// ── THE MOTION TABLE ────────────────────────────────────────────────────────
// The simulation half of a preset's authoring, kept beside the recipe (the look
// half) but deliberately separate: these change for different reasons and are
// judged by different evidence — a headless number here, an eyeball there.
//
// This replaced five per-kind switch statements. Five switches over one enum is
// the same shape as the style tables this file exists to remove: adding a preset
// meant remembering all five, and a `default:` silently handed a new row some
// other preset's physics. A table cannot forget a column.
//
// aspectCap = false means the caller's width is used as-is. The swept presets
// cap width against the length the tip ACTUALLY travelled (see SweptTrail_HalfWidth
// and the "thickness is a ratio against the thing's OWN length" landmine); the
// two strand presets are authored at a fixed radius and must not be re-capped,
// which is exactly how they behaved as their own composer.
typedef struct {
    float aspectK;     // half-width : travelled length, when aspectCap
    bool  aspectCap;
    bool  cloth;       // build and attach a ForceField at all
    float sag;         // m/s^2 downward   \  cloth only — unread when !cloth
    float curl;        //                   |
    float drag;        // higher = stiffer  |
    float wind;        //                  /
    int   strands;     // before the tier gate
    // Whether s_sweptWidthCurve/s_sweptAlphaCurve carry stops for this preset.
    // ONLY the swept presets are authored with them; the strand presets shape
    // their width from the width ENVELOPE and their alpha from the sheet. This
    // is a flag rather than "just index the array" because an unpopulated
    // FloatCurve evaluates to ZERO, which silently collapses the strip to no
    // width and no alpha — the trail is still simulated, still has history and
    // still reports a healthy thickness, and draws nothing at all. That cost a
    // full bisection to find.
    bool  curves;
    // Whether the `swept_*` tuning knobs apply to this preset. A unified
    // composer inherits one family's GLOBAL multipliers, and applying them to a
    // family that never had them is the §1 bug wearing different clothes: a
    // stale `swept_width = 3.0` in tuning.cfg silently tripled the strand
    // presets' quad (0.45 m -> 1.35 m) and read as "the new composer got the
    // shape wrong". A global knob in a shared composer MUST name who it applies
    // to.
    bool  sweptKnobs;
    float sampleHz;
    float idleSpeed;
    float teleportSpeed;
    bool  smoothSpline;
    TrailWidthEnvelopeType widthEnvelope;
} TrailMotion;

static const TrailMotion k_trailMotion[TRAIL_PRESET_COUNT] = {
    [TRAIL_PRESET_BLADE]    = {0.0250f, true, true, 0.70f, 0.30f, 5.0f, 0.55f, 1, true, true,
                               SWEPT_SAMPLE_HZ, SWEPT_IDLE_SPEED, SWEPT_TELEPORT_SPEED,
                               true, TRAIL_WIDTH_ENVELOPE_TAPER_BOTH},
    [TRAIL_PRESET_MAIN]     = {0.0715f, true, true, 2.60f, 0.55f, 1.9f, 1.70f, 1, true, true,
                               SWEPT_SAMPLE_HZ, SWEPT_IDLE_SPEED, SWEPT_TELEPORT_SPEED,
                               true, TRAIL_WIDTH_ENVELOPE_TAPER_BOTH},
    [TRAIL_PRESET_WISP]     = {0.0125f, true, true, 0.90f, 0.40f, 3.0f, 1.10f, 2, true, true,
                               SWEPT_SAMPLE_HZ, SWEPT_IDLE_SPEED, SWEPT_TELEPORT_SPEED,
                               true, TRAIL_WIDTH_ENVELOPE_TAPER_BOTH},
    [TRAIL_PRESET_BACKDROP] = {0.1000f, true, true, 1.35f, 0.28f, 4.4f, 0.75f, 1, true, true,
                               SWEPT_SAMPLE_HZ, SWEPT_IDLE_SPEED, SWEPT_TELEPORT_SPEED,
                               true, TRAIL_WIDTH_ENVELOPE_TAPER_BOTH},
    // The two strand presets ran as plain followers with no cloth: their motion
    // is the wave field in the fragment stage, and adding a force field on top
    // fought it. Slower sample clock and a lower idle threshold than the swept
    // presets, exactly as they were authored.
    [TRAIL_PRESET_ENERGY]   = {0.0f, false, false, 0, 0, 0, 0, 1, false, false,
                               30.0f, 0.05f, 25.0f,
                               false, TRAIL_WIDTH_ENVELOPE_ENERGY_BLADE},
    [TRAIL_PRESET_SMOKE]    = {0.0f, false, false, 0, 0, 0, 0, 1, false, false,
                               30.0f, 0.05f, 25.0f,
                               false, TRAIL_WIDTH_ENVELOPE_SMOKE_WIDEN},
};

static const TrailMotion *TrailMotionOf(TrailPresetId p)
{
    if (p < 0 || p >= TRAIL_PRESET_COUNT) p = TRAIL_PRESET_BLADE;
    return &k_trailMotion[p];
}

static float SweptTrail_AspectK(TrailPresetId kind)
{
    return TrailMotionOf(kind)->aspectK;
}

// The caller's width is a CEILING, not a value. Below the speed at which the
// requested width is in proportion, the travelled length wins — which is the
// whole fix for the classic failure this DoD names: on a hard turn the tail
// shortens, and a band that keeps its width through that becomes a blob.
static float SweptTrail_HalfWidth(float widthMetres, float level01,
                                  float travelLen, TrailPresetId kind)
{
    // A preset authored at a fixed radius uses that radius AS the half-width —
    // the strand presets size their quad so the waves have room to swing inside
    // it, and halving it (the swept convention, where the caller passes a full
    // width) shrinks the room the effect lives in.
    float want = TrailMotionOf(kind)->aspectCap ? widthMetres * 0.5f * level01
                                                : widthMetres * level01;
    if (want < 0.0f)
        want = 0.0f;
    // A preset authored at a fixed radius is NOT re-capped. The strand presets
    // size their quad so the waves have room to swing inside it — that width is
    // the effect, not a ceiling — and running them through the aspect rule with
    // an aspectK of 0 collapses the half-width to zero and renders nothing,
    // which on screen is indistinguishable from a broken shader.
    if (!TrailMotionOf(kind)->aspectCap)
        return want;
    float cap = travelLen * SweptTrail_AspectK(kind) * s_sweptAspectMul;
    return (cap < want) ? cap : want;
}

// How much the ribbon behaves like cloth. BLADE is nearly a record (a sword's
// trail is struck, not draped); RIBBON is silk and is allowed to sag, lag and
// overshoot; FILAMENT sits between.
// Higher drag = the node settles faster = stiffer. Low drag is what lets the
// tail keep travelling after the head has stopped.
static float SweptTrail_Sag(TrailPresetId kind) { return TrailMotionOf(kind)->sag; }
static float SweptTrail_Drag(TrailPresetId kind) { return TrailMotionOf(kind)->drag; }
static float SweptTrail_Wind(TrailPresetId kind) { return TrailMotionOf(kind)->wind; }

static int SweptTrail_StrandCount(TrailPresetId kind, bool lowTier)
{
    if (TrailMotionOf(kind)->strands <= 1)
        return 1;
    // E8 tier budget: each strand is its own ribbon submission. The gate only
    // ever clamps DOWN — a low tier loses threads, never gains them.
    // TWO, not four. The reference guide is explicit — "1-3 wisps is enough" —
    // and a composite that spawns 2 wisps of a 4-strand style puts EIGHT bright
    // ribbons on screen, which is the guide's "random chaos" mistake and reads
    // as a bundle of wires rather than as energy. The bundle belongs to the
    // caller, which already decides how many wisps it wants.
    return lowTier ? 1 : TrailMotionOf(kind)->strands;
}

// Tail memory in seconds → nodes. The arithmetic moved to vc_common.inl when the
// volume trail became its second caller; the sample rate is still this file's.
// Nodes the tail window holds. MUST use the preset's OWN sample rate: sizing a
// 30 Hz trail's window with the 60 Hz constant keeps twice the intended history,
// which stretches the along-trail coordinate the tail fade is measured in and
// ends the strip on a hard square cut instead of a taper.
static int SweptTrail_MaxNodesFor(TrailPresetId kind, float lifetime)
{
    return VC_TrailNodesForLifetime(lifetime, TrailMotionOf(kind)->sampleHz);
}

// ── Shared authored state ────────────────────────────────────────────────────

// The trail's sheet — guide §3 "TEXTURE AND UV", which the first version simply
// did not have. It was a plain symmetric falloff: one soft band, no interior, so
// the strip could only ever be a smooth gradient. Every trail on the reference
// sheet has FIBRES running along it, and that interior structure is most of what
// separates "energy" from "a painted stroke".
//
// Two properties are load-bearing:
//   - SEAMLESS along v. The sheet is tiled and scrolled, so every term in v must
//     have an INTEGER period or the wrap shows as a repeating seam travelling
//     down the trail ("harsh edges and tiling textures", guide's mistake list).
//   - Nothing high-frequency ACROSS u. A band a few pixels wide cannot resolve
//     it, and unresolvable detail comes back as dashes (LANDMINES, 29/07).
// WHY STREAKS AND NOT LANES (owner, 29/07, and this is the exact diagnosis):
// "nó giống với việc bạn lôi 1 đồ thị hình sin đi, chứ ko phải dao động lên
// xuống của dây, tức texture này ko hợp" — it looks like a sine GRAPH being
// dragged along, not a cord oscillating.
//
// He was describing the maths precisely. The first sheet drew six CONTINUOUS
// lanes, `c = lane[f] + wob[f] * sin(2*PI*cyc[f]*v + phase[f])`, each running
// the full height of the sheet. Scrolling v then translates a rigid pattern:
// every feature is present at every moment, nothing begins, nothing ends, and
// the eye — which reads motion from things APPEARING and VANISHING far more
// readily than from a rigid shape sliding — calls it static. Three additive
// passes of that same rigid pattern at different scroll rates only made it
// worse: their sum is flatter than any one of them, which is why no scroll
// speed from 1.3 to 3.6 looked like it was moving.
//
// Energy flow is FINITE STREAKS. Each one occupies a bounded stretch of v with
// soft ends, so as the sheet scrolls a filament slides into view, brightens,
// stretches past and fades — motion the eye gets for free from the envelope,
// independent of the scroll rate.
#define SWEPT_STREAKS 16

static float SweptTrail_BandProfile(float u)
{
    float d = fabsf(u - 0.5f) * 2.0f; // 0 at the centre line, 1 at the edges
    float a = 1.0f - d * d;
    if (a < 0.0f)
        a = 0.0f;
    return powf(a, 1.35f); // soft shoulders, no hard rim
}

static void SweptTrail_BuildBladeMask(void)
{
    const int W = 128, H = 256; // H carries the streaks; must be seamless
    // One row per streak: where it sits across the band, where its middle sits
    // along it, its HALF-length in v, how tight it is across u, how bright, and
    // how far it drifts across the band over its own length (a streak that runs
    // dead straight down v reads as a printed line; a slight drift reads as
    // something being carried).
    //
    // Deliberately uneven in every column — evenly spaced streaks of equal
    // length and width resolve into a comb, which is the same failure the
    // filament spread had.
    static const float st_u[SWEPT_STREAKS] = {
        0.50f, 0.42f, 0.60f, 0.53f, 0.38f, 0.64f, 0.47f, 0.57f,
        0.35f, 0.50f, 0.62f, 0.44f, 0.55f, 0.40f, 0.66f, 0.48f};
    static const float st_v[SWEPT_STREAKS] = {
        0.05f, 0.19f, 0.13f, 0.31f, 0.40f, 0.36f, 0.52f, 0.61f,
        0.58f, 0.72f, 0.79f, 0.85f, 0.93f, 0.97f, 0.66f, 0.24f};
    static const float st_len[SWEPT_STREAKS] = {
        0.16f, 0.11f, 0.13f, 0.19f, 0.09f, 0.14f, 0.17f, 0.10f,
        0.12f, 0.20f, 0.11f, 0.15f, 0.12f, 0.10f, 0.08f, 0.09f};
    static const float st_wide[SWEPT_STREAKS] = {
        0.045f, 0.035f, 0.050f, 0.040f, 0.030f, 0.055f, 0.038f, 0.032f,
        0.048f, 0.042f, 0.036f, 0.052f, 0.034f, 0.044f, 0.030f, 0.028f};
    static const float st_amp[SWEPT_STREAKS] = {
        1.00f, 0.72f, 0.66f, 0.90f, 0.55f, 0.60f, 0.85f, 0.62f,
        0.48f, 0.95f, 0.58f, 0.68f, 0.74f, 0.50f, 0.44f, 0.52f};
    static const float st_slant[SWEPT_STREAKS] = {
        0.05f, -0.04f, 0.06f, -0.07f, 0.03f, -0.05f, 0.06f, -0.03f,
        0.05f, -0.06f, 0.04f, -0.05f, 0.03f, -0.04f, 0.05f, -0.03f};

    Image img = GenImageColor(W, H, BLANK);
    Image halo = GenImageColor(W, H, BLANK);
    for (int y = 0; y < H; y++)
    {
        float v = ((float)y + 0.5f) / (float)H;
        for (int x = 0; x < W; x++)
        {
            float u = ((float)x + 0.5f) / (float)W;
            // A dim base band so the fibres sit INSIDE something, rather than
            // floating as separate wires.
            float base = SweptTrail_BandProfile(u);
            float a = 0.30f * base;
            // Streaks are damped by the band profile SQUARED, so they live in the
            // middle of the strip and are gone well before its edge. A streak
            // drifts across u as it travels down v; near the edge that drift
            // modulates the alpha of the silhouette itself, and the band comes out
            // SCALLOPED — a regular sawtooth that reads as the ribbon being cut
            // into segments (owner's recording, 29/07). Damping is cheaper than
            // moving the streaks, and it keeps them free to drift in the middle
            // where the drift is the whole point.
            for (int f = 0; f < SWEPT_STREAKS; f++)
            {
                // CIRCULAR distance along v. The sheet is tiled and scrolled, so
                // a streak whose middle sits near v = 0 has to reach round to
                // v = 1 or the wrap shows as a seam travelling down the trail
                // ("harsh edges and tiling textures", the guide's mistake list).
                // Wrapping the distance is what makes the finite envelope legal
                // here at all — the old lanes bought seamlessness with integer
                // periods, which is exactly what forced them to be continuous.
                float dv = v - st_v[f];
                dv -= floorf(dv + 0.5f); // into [-0.5, 0.5)
                float t = dv / st_len[f];
                if (t <= -1.0f || t >= 1.0f)
                    continue;
                // Raised cosine: zero VALUE and zero SLOPE at both ends, so a
                // streak fades in and out instead of switching on at a hard edge.
                float env = 0.5f * (1.0f + cosf(PI * t));
                float c = st_u[f] + st_slant[f] * t;
                float d = (u - c) / st_wide[f];
                a += st_amp[f] * expf(-d * d) * env * base * base;
            }
            a = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
            ImageDrawPixel(&img, x, y, (Color){255, 255, 255, (unsigned char)(a * 255.0f)});

            // THE HALO SHEET: the same band with NO fibres at all.
            //
            // This is a lesson already written down in vc_sweep_slash.inl and
            // repeated here anyway: "the halo must not carry the rim". The glow
            // pass is 2.6x wider than the body, so any structure in its sheet is
            // thrown 2.6x further out — the fibre wander that is invisible at the
            // body's edge becomes long spikes at the halo's. A halo exists to put
            // light BEHIND the ribbon, not to be a second silhouette.
            float h = base * (0.90f + 0.10f * sinf(v * 2.0f * PI));
            h = h < 0.0f ? 0.0f : (h > 1.0f ? 1.0f : h);
            ImageDrawPixel(&halo, x, y, (Color){255, 255, 255, (unsigned char)(h * 255.0f)});
        }
    }
    s_sweptBladeTex = LoadTextureFromImage(img);
    s_sweptHaloTex = LoadTextureFromImage(halo);
    UnloadImage(img);
    UnloadImage(halo);
    if (s_sweptHaloTex.id != 0)
    {
        SetTextureFilter(s_sweptHaloTex, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(s_sweptHaloTex, TEXTURE_WRAP_REPEAT);
    }
    if (s_sweptBladeTex.id != 0)
    {
        SetTextureFilter(s_sweptBladeTex, TEXTURE_FILTER_BILINEAR);
        // REPEAT, not CLAMP: the sheet is TILED along the trail and scrolled, so
        // it must wrap. Harmless across u because the profile is zero at both
        // edges.
        SetTextureWrap(s_sweptBladeTex, TEXTURE_WRAP_REPEAT);
    }
}

// ── Sheet note: composition owns no filename ───────────────────────────────
//
// A legacy path here rebuilt a ribbon sheet from `energy_flow.png` by cropping
// and rotating it (it is authored sideways, and only its middle 40% carries
// anything). It was DELETED 10/08/2026, and not because the technique was
// wrong: it had been migrated to read whatever `VFX_SURFACE_ENERGY_RIBBON`
// holds, which is now `energy_wisp.png` — a different image, 512x512 and
// tiling, owned by the strand presets. So every crop constant was being applied
// to a sheet it did not describe, behind a `swept_sheet` knob that defaulted
// off. A branch nothing ships, measured against an asset it no longer loads, is
// not a fallback — it is a trap for the next reader.
//
// If that sheet is wanted back, register it as its own surface profile in
// `assets/vfx_surface_profiles.json` (channel grammar per assets/TEXTURE_PACKING.md)
// and point a preset's `recipe.surface` at it. Composition never owns a path.

static void SweptTrail_InitShared(void)
{
    if (s_sweptInit)
        return;

    SweptTrail_BuildBladeMask();

    FloatCurve_AddStop(&s_sweptTwinkle, 0.00f, 0.00f);
    FloatCurve_AddStop(&s_sweptTwinkle, 0.10f, 1.00f);
    FloatCurve_AddStop(&s_sweptTwinkle, 0.28f, 0.25f);
    FloatCurve_AddStop(&s_sweptTwinkle, 0.46f, 0.95f);
    FloatCurve_AddStop(&s_sweptTwinkle, 0.62f, 0.18f);
    FloatCurve_AddStop(&s_sweptTwinkle, 0.80f, 0.70f);
    FloatCurve_AddStop(&s_sweptTwinkle, 1.00f, 0.00f);

    // sag (m/s^2 down) · curl (the swirling air) · drag (how fast it settles).
    // Meter scale: real gravity is 9.81, so 2.6 is cloth that clearly has weight
    // without falling like a rock.
    // RETUNED 29/07 after the owner's "like grabbing a snake by the head and
    // swinging it". The first numbers let the forces DRIVE the shape: curl noise
    // accumulated over a node's whole life against nothing but a
    // distance-to-neighbour constraint, so the chain wandered wherever the air
    // took it and stopped reading as the trail OF anything. Cloth flutters
    // AROUND the path it was dragged along; it does not leave it. These are a
    // perturbation — see `nodeHomeSpring` in core/trail_system.c, which is what
    // actually bounds the deviation.
    // BLADE, MAIN, WISP, BACKDROP. A backdrop does not become a second main
    // tail: it settles quickly and only supplies soft mass behind the shape.
    static const float sag[TRAIL_PRESET_COUNT] = {0.40f, 0.95f, 0.55f, 0.65f};
    static const float curl[TRAIL_PRESET_COUNT] = {0.30f, 0.55f, 0.40f, 0.28f};
    static const float drag[TRAIL_PRESET_COUNT] = {5.50f, 3.40f, 4.20f, 5.10f};
    for (int st = 0; st < TRAIL_PRESET_COUNT; st++)
    {
        ForceField_AddLayer(&s_sweptCloth[st], (ForceLayer){
                                                   .type = FORCE_GRAVITY_DIR,
                                                   .direction = {0.0f, -1.0f, 0.0f},
                                                   .strength = sag[st],
                                               });
        ForceField_AddLayer(&s_sweptCloth[st], (ForceLayer){
                                                   .type = FORCE_NOISE_CURL,
                                                   .strength = curl[st],
                                                   .noiseScale = 0.95f, // finer eddies: a flutter, not a swing
                                                   .noiseSpeed = 0.45f, // the air itself moves, slowly
                                               });
        // Drag is what makes the tail LAG rather than snap: it is the difference
        // between a node that keeps its momentum and one that is glued to the path.
        ForceField_AddLayer(&s_sweptCloth[st], (ForceLayer){
                                                   .type = FORCE_DRAG,
                                                   .strength = drag[st],
                                               });
    }

    // Width along the strip. segRatio 1 = the HEAD (the newest node, where the
    // weapon is now); 0 = the tail. Derived, not guessed: the FOLLOWER draw path
    // walks h = 0..drawCount-1 with segRatio = 1 - h/(drawCount-1) and
    // GetHistoryNodeIndex maps i = historyCount-1 to historyHead
    // (trail_system.c:928 and :111).
    //
    // A blade trail is anchored at the head and dissipates behind it, so unlike
    // a free-flying element it is NOT a symmetric lens — it is full at the head
    // and pointed at the tail. The small step down in the last 6% keeps the
    // leading end from being a flat cap, which on a short strip is the flat base
    // of a triangle (core/docs/LANDMINES.md).
    // WIDTH OVER LENGTH — a LENS, per the guide's own diagram: thin at the head,
    // widest in the body, thin at the tail. The first version held 0.74 at the
    // head, which draws a band that simply STOPS: the owner's capture shows a
    // flat vertical edge where the trail ends, and "no taper" is item three on
    // the guide's list of common mistakes. A trail is emitted at a point, so its
    // newest end is a needle, not a wall.
    FloatCurve_AddStop(&s_sweptWidthCurve[TRAIL_PRESET_BLADE], 0.00f, 0.00f);
    FloatCurve_AddStop(&s_sweptWidthCurve[TRAIL_PRESET_BLADE], 0.25f, 0.55f);
    FloatCurve_AddStop(&s_sweptWidthCurve[TRAIL_PRESET_BLADE], 0.60f, 1.00f);
    FloatCurve_AddStop(&s_sweptWidthCurve[TRAIL_PRESET_BLADE], 0.88f, 0.72f);
    FloatCurve_AddStop(&s_sweptWidthCurve[TRAIL_PRESET_BLADE], 1.00f, 0.18f);

    FloatCurve_AddStop(&s_sweptWidthCurve[TRAIL_PRESET_MAIN], 0.00f, 0.00f);
    FloatCurve_AddStop(&s_sweptWidthCurve[TRAIL_PRESET_MAIN], 0.30f, 0.70f);
    FloatCurve_AddStop(&s_sweptWidthCurve[TRAIL_PRESET_MAIN], 0.62f, 1.00f);
    FloatCurve_AddStop(&s_sweptWidthCurve[TRAIL_PRESET_MAIN], 0.90f, 0.78f);
    FloatCurve_AddStop(&s_sweptWidthCurve[TRAIL_PRESET_MAIN], 1.00f, 0.22f);

    FloatCurve_AddStop(&s_sweptWidthCurve[TRAIL_PRESET_WISP], 0.00f, 0.00f);
    FloatCurve_AddStop(&s_sweptWidthCurve[TRAIL_PRESET_WISP], 0.22f, 0.78f);
    FloatCurve_AddStop(&s_sweptWidthCurve[TRAIL_PRESET_WISP], 0.85f, 1.00f);
    FloatCurve_AddStop(&s_sweptWidthCurve[TRAIL_PRESET_WISP], 1.00f, 0.20f);

    FloatCurve_AddStop(&s_sweptWidthCurve[TRAIL_PRESET_BACKDROP], 0.00f, 0.00f);
    FloatCurve_AddStop(&s_sweptWidthCurve[TRAIL_PRESET_BACKDROP], 0.24f, 0.82f);
    FloatCurve_AddStop(&s_sweptWidthCurve[TRAIL_PRESET_BACKDROP], 0.70f, 1.00f);
    FloatCurve_AddStop(&s_sweptWidthCurve[TRAIL_PRESET_BACKDROP], 1.00f, 0.38f);

    // Brightness rides toward the head, and — the rule that is not taste — the
    // tail's alpha must fall at least as fast as its width, or the last stretch
    // is sub-pixel while still visible and breaks into dashes (LANDMINES 29/07).
    FloatCurve_AddStop(&s_sweptAlphaCurve[TRAIL_PRESET_BLADE], 0.00f, 0.00f);
    FloatCurve_AddStop(&s_sweptAlphaCurve[TRAIL_PRESET_BLADE], 0.25f, 0.32f);
    FloatCurve_AddStop(&s_sweptAlphaCurve[TRAIL_PRESET_BLADE], 0.70f, 0.82f);
    FloatCurve_AddStop(&s_sweptAlphaCurve[TRAIL_PRESET_BLADE], 1.00f, 1.00f);

    FloatCurve_AddStop(&s_sweptAlphaCurve[TRAIL_PRESET_MAIN], 0.00f, 0.00f);
    FloatCurve_AddStop(&s_sweptAlphaCurve[TRAIL_PRESET_MAIN], 0.30f, 0.62f);
    FloatCurve_AddStop(&s_sweptAlphaCurve[TRAIL_PRESET_MAIN], 1.00f, 1.00f);

    FloatCurve_AddStop(&s_sweptAlphaCurve[TRAIL_PRESET_WISP], 0.00f, 0.00f);
    FloatCurve_AddStop(&s_sweptAlphaCurve[TRAIL_PRESET_WISP], 0.25f, 0.62f);
    FloatCurve_AddStop(&s_sweptAlphaCurve[TRAIL_PRESET_WISP], 1.00f, 0.90f);

    FloatCurve_AddStop(&s_sweptAlphaCurve[TRAIL_PRESET_BACKDROP], 0.00f, 0.00f);
    FloatCurve_AddStop(&s_sweptAlphaCurve[TRAIL_PRESET_BACKDROP], 0.28f, 0.28f);
    FloatCurve_AddStop(&s_sweptAlphaCurve[TRAIL_PRESET_BACKDROP], 0.72f, 0.38f);
    FloatCurve_AddStop(&s_sweptAlphaCurve[TRAIL_PRESET_BACKDROP], 1.00f, 0.18f);

    // Lazily, never from a subsystem Init — Tuning_Init runs after those and an
    // early registration silently keeps the default (core/docs/LANDMINES.md).
    // Nine, down from seventeen. `swept_lag`, `swept_minpx`, `swept_core`,
    // `swept_blade_flat` and `swept_camfacing` were all instruments for the
    // dashing, and the dashing was the ribbon FOLDING — fixed at the source, so
    // the instruments for it are dead weight. `swept_wind` and `swept_uvlog`
    // went with the hand-rolled simulation and the hand-rolled UV.
    Tuning_RegisterFloat("swept_width", &s_sweptWidthMul, 1.0f);
    Tuning_RegisterFloat("swept_aspect", &s_sweptAspectMul, 1.0f);
    Tuning_RegisterFloat("swept_spread", &s_sweptSpread, 2.2f);
    Tuning_RegisterFloat("swept_spark", &s_sweptSpark, 1.0f);
    Tuning_RegisterFloat("swept_alpha", &s_sweptAlphaMul, 1.0f);
    Tuning_RegisterFloat("swept_flow", &s_sweptFlow, -1.0f);
    Tuning_RegisterFloat("swept_sag", &s_sweptSag, 1.0f);
    Tuning_RegisterFloat("swept_corehot", &s_sweptCoreHot, 1.0f);
    Tuning_RegisterFloat("swept_body", &s_sweptBodyOpacity, 1.0f);
    Tuning_RegisterFloat("swept_tile", &s_sweptTile, SWEPT_FLOW_TILE);
    Tuning_RegisterFloat("swept_freeze", &s_sweptFreeze, 0.0f);
    s_sweptInit = true;
}
// ── The layer stack ─────────────────────────────────────────────────────────
//
// What makes a trail read as beautiful, from the owner's reference sheet: every
// trail on it is FOUR LAYERS — a wide soft glow, a textured body, a hot near-
// white core, and sparkle points along the way. A single strip with a mask is a
// line, however good the mask is.
//
// The ratios are VFX_ComposeSweepSlash's, the one multi-pass effect in the tree
// that has never been objected to. 1.55 for the halo, not the 2.6 this shipped
// with: the halo multiplies a width that is itself earned from the path length,
// so on a 6 m ribbon 2.6x meant a band over two metres across, and additive
// light that wide through E1's bloom stops being a glow behind the ribbon and
// becomes a coarse slab around it.
//
// ONLY THE BODY IS TEXTURED, and this is the rule, not a preference: three
// additive copies of the same quasi-periodic pattern at three phases SUM TO
// SOMETHING FLAT — averaging shifted copies of a pattern is how you remove it —
// and the wider layers throw the sheet's edge detail outward as spikes. The halo
// and the core are lit SHAPES.
//
// Not const: `swept_corehot` scales the core's alpha per frame, and the body's
// texture pointer follows the sheet dial.
// THE ALPHA BUDGET, and it is arithmetic, not taste. These layers are ADDITIVE
// and they overlap: the core sits inside the body, which sits inside the halo,
// so what the frame buffer sees is their SUM. The first version summed to 2.01
// at the head and 1.85 wherever body and core overlap — where 1.00 is already
// full white. Every texel in that region clipped, so the sheet's filaments were
// mathematically unrecoverable however good the sheet was.
//
// That was true of the hand-rolled version too, and it went unnoticed for a
// reason worth writing down: the ribbon was FOLDING, and the fold carved dark
// notches across the band. Those notches were read as detail. Fixing the fold
// removed the only structure that was surviving the clipping, which is why "it
// works now" and "it is blown out" arrived in the same frame.
//
// The budget: BELOW 1.0 along the body, so the texture survives, and over it only
// at the head, which is the one place a trail is supposed to blow out.
//
// HALVED AGAIN after the owner ran it: 0.14/0.62/0.55 summed to 0.86 through the
// body — under 1.0 by the arithmetic — and still burned out on screen, and
// `swept_alpha = 0.5` was what looked right. The arithmetic was not wrong, the
// CEILING was: E1's streak bloom lifts anything near the threshold, so the
// effective saturation point is well under 1.0 and a "safe" 0.86 is not safe at
// all. These are the owner's 0.5 folded in, so the default ships correct instead
// of relying on a tuning.cfg override.
static const TrailLayer k_sweptLayers[TRAIL_PRESET_COUNT][3] = {
    [TRAIL_PRESET_BLADE] = {
        {.widthMul = 1.30f, .alphaMul = 0.07f, .whiten = 0.00f, .scrollMul = 0.50f, .headAlphaPow = 0.0f, .texture = NULL},
        {.widthMul = 1.00f, .alphaMul = 0.28f, .whiten = 0.06f, .scrollMul = 1.05f, .headAlphaPow = 0.0f, .texture = NULL},
        {.widthMul = 0.20f, .alphaMul = 0.22f, .whiten = 0.18f, .scrollMul = 1.60f, .headAlphaPow = 3.4f, .texture = NULL},
    },
    [TRAIL_PRESET_MAIN] = {
        {.widthMul = 1.55f, .alphaMul = 0.10f, .whiten = 0.00f, .scrollMul = 0.50f, .headAlphaPow = 0.0f, .texture = NULL},
        {.widthMul = 1.00f, .alphaMul = 0.36f, .whiten = 0.06f, .scrollMul = 1.05f, .headAlphaPow = 0.0f, .texture = NULL},
        {.widthMul = 0.26f, .alphaMul = 0.30f, .whiten = 0.20f, .scrollMul = 1.60f, .headAlphaPow = 3.4f, .texture = NULL},
    },
    [TRAIL_PRESET_WISP] = {
        {.widthMul = 1.30f, .alphaMul = 0.035f, .whiten = 0.00f, .scrollMul = 0.55f, .headAlphaPow = 0.0f, .texture = NULL},
        {.widthMul = 1.00f, .alphaMul = 0.19f, .whiten = 0.04f, .scrollMul = 1.10f, .headAlphaPow = 0.0f, .texture = NULL},
        // A wisp has a CONTINUOUS inner core. It is thinner and dimmer than
        // MAIN, but never gated by a head-only exponent or broken texture.
        {.widthMul = 0.18f, .alphaMul = 0.17f, .whiten = 0.12f, .scrollMul = 1.35f, .headAlphaPow = 0.0f, .texture = NULL},
    },
    // BACKDROP's weights are LOWER than the others', not by the same budget but
    // for a different reason: filling its gaps (strand gain 0.19) multiplied the
    // lit area several times over, so the weights that made a sparse hairline
    // read correctly made a solid one glare. Dim is also its job — it is an
    // underlay, and it is judged behind another trail, not alone.
    [TRAIL_PRESET_BACKDROP] = {
        {.widthMul = 1.65f, .alphaMul = 0.025f, .whiten = 0.00f, .scrollMul = 0.45f, .headAlphaPow = 0.0f, .texture = NULL},
        {.widthMul = 1.00f, .alphaMul = 0.063f, .whiten = 0.02f, .scrollMul = 0.85f, .headAlphaPow = 0.0f, .texture = NULL},
        {0},
    },
    // The strand presets are ONE quad: their waves swing inside it and the edge
    // mask is the silhouette, so there is no wider halo to stack.
    [TRAIL_PRESET_ENERGY] = {{.widthMul = 1.0f, .alphaMul = 1.0f, .scrollMul = 1.0f, .texture = NULL}, {0}, {0}},
    [TRAIL_PRESET_SMOKE]  = {{.widthMul = 1.0f, .alphaMul = 1.0f, .scrollMul = 1.0f, .texture = NULL}, {0}, {0}},
};

// ── THE RECIPE TABLE — what each preset LOOKS like ──────────────────────────
//
// This is the whole point of the file. Six trails, one struct, no branches: what
// used to be `VFX_RIBBON_*` in one style table and `VFX_STRAND_*` in another,
// backed by two hand-written shader modes, is now six rows read by one formula.
//
// The two families differ in exactly two fields — `topology` and whether they
// carry deform layers — and that difference is real rather than incidental: a
// swept ribbon gets its shape from CLOTH (CPU nodes moving in a force field)
// and wears a painted sheet, while a strand trail is geometrically a straight
// strip whose shape is entirely the wave field in UV. SUMMED warps one
// coordinate; PARALLEL gives each wave layer its own sample and combines with
// MAX, because the gaps between the bundles ARE the effect.
static TrailRecipe k_trailPresets[TRAIL_PRESET_COUNT];
static bool s_trailPresetsBuilt = false;

// Built rather than declared: UVDeformField/SurfaceFlow are layer stacks with
// their own constructors, and a designated-initializer wall for four wave layers
// x three presets is exactly the unreadable table this refactor is removing.
static void TrailPresets_Build(void)
{
    if (s_trailPresetsBuilt) return;
    s_trailPresetsBuilt = true;

    for (int p = 0; p < TRAIL_PRESET_COUNT; p++)
    {
        TrailRecipe *r = &k_trailPresets[p];
        *r = (TrailRecipe){0};
        r->ribbonMode = (p == TRAIL_PRESET_BLADE) ? RIBBON_FIXED_NORMAL
                                                  : RIBBON_CAMERA_FACING;
        r->fixedNormal = (Vector3){0.0f, 1.0f, 0.0f};
        r->colour.contrast = VFX_CONTRAST_ENERGY;
        r->additive = true;
        r->tintAlpha = 255;
        r->hdrGain = 1.0f;
        // Today's measured default. An emission weight reused as coverage caps
        // the body at ~0.36 and the trail cannot hold hue over a bright
        // destination; 1.0 means "draw the sheet at the alpha it was authored
        // with", still shaped by the sheet's soft alpha and the width taper.
        r->bodyOpacity = 1.0f;
        // A filament default, so a recipe that forgets to author its archetype
        // still renders SOMETHING recognisable. `gain` at 0 would otherwise
        // clamp to 0.05 in the shader and blow every sample to solid white,
        // which is the failure a zeroed struct must not be able to produce.
        r->strand = (TrailStrandConfig){.bundleWidth = 0.34f, .gain = 1.35f,
                                        .fineMix = 0.70f, .thirdWeight = 0.80f,
                                        .flowDistort = 0.55f};
        UVDeform_Clear(&r->deform);
        SurfaceFlow_Clear(&r->flow);
    }

    // ── The four SWEPT presets ──────────────────────────────────────────────
    // MIGRATED onto the strand material 10/08/2026. They used to run the
    // shader's PASSTHROUGH branch wearing a mask generated at runtime — a sheet
    // the code itself calls a fallback, worn permanently because the authored
    // path was deleted for pointing at the wrong image. Passthrough now belongs
    // to trails with no recipe at all, and every preset shares ONE material.
    //
    // Their shape still comes from the CLOTH: the deform layers here are small,
    // and they exist to give the sheet's filaments something to swim along, not
    // to bend the ribbon. That is the split the recipe is for — motion is the
    // motion table's business, surface is this one's.
    // THE ARCHETYPE IS `gain` AND `bundle`, NOT THE IMAGE. All four wear
    // `energy_wisp.png`, and a sweep on 10/08/2026 established that blade,
    // cloth and mass are all reachable from it — `gain` below 1 lifts the gaps
    // between the authored hairs until they merge into a body, above 1 pushes
    // them apart into separate filaments, and `bundle` says how much of the
    // quad that body is allowed to occupy. Three extra sheets were scoped for
    // this (§9.1); the measurement says they are not needed.
    //
    // `bundle` was authored here from the start and then DISCARDED — the bridge
    // recomputed it as `amp * 0.85`, so every preset was pinned under a quarter
    // of its own quad and read as a hairline at any radius. It is read now.
    static const struct { float amp, freq, travel, bundle, gain, edge, dissolve; int layers; }
    k_sweptStrand[TRAIL_PRESET_COUNT] = {
        // BLADE — an OBJECT, not energy: filled body, hard outer edge, tight
        // swim. gain well under 1 is what stops it reading as loose hairs.
        [TRAIL_PRESET_BLADE]    = {0.14f, 0.90f, 1.20f, 0.36f, 0.34f, 0.06f, 0.42f, 3},
        // MAIN — cloth: broad, slow, folds you can follow along its length.
        [TRAIL_PRESET_MAIN]     = {0.22f, 0.55f, 0.80f, 0.65f, 0.61f, 0.32f, 0.42f, 3},
        // WISP — the one that stays filaments (gain > 1). Widened only enough
        // that a thread is a thread rather than a single-pixel scratch.
        [TRAIL_PRESET_WISP]     = {0.12f, 0.70f, 1.00f, 0.22f, 1.35f, 0.14f, 0.42f, 3},
        // BACKDROP — mass: it sits BEHIND another trail, so detail here fights
        // the trail in front. Nearly the whole quad, gaps filled flat, very soft
        // edges, and a low dissolve so it stays one body instead of islands.
        [TRAIL_PRESET_BACKDROP] = {0.26f, 0.35f, 0.45f, 0.95f, 0.19f, 0.75f, 0.17f, 2},
    };
    for (int p = TRAIL_PRESET_BLADE; p <= TRAIL_PRESET_BACKDROP; p++)
    {
        TrailRecipe *r = &k_trailPresets[p];
        const float amp = k_sweptStrand[p].amp;
        r->topology = TRAIL_SAMPLE_PARALLEL;
        r->surface = VFX_SURFACE_ENERGY_RIBBON; // resolved through the registry
        r->sheetOverride = NULL;                // no runtime-generated mask
        r->layers = k_sweptLayers[p];
        r->layerCount = k_sweptStrand[p].layers;
        r->colour.useElementRamp = true;      // the element's authored N-stop ramp
        r->colour.coreWidth = (p == TRAIL_PRESET_BACKDROP) ? 0.0f : 0.16f;
        r->colour.coreIntensity = (p == TRAIL_PRESET_BACKDROP) ? 0.0f : 0.55f;
        r->strand.bundleWidth = k_sweptStrand[p].bundle;
        r->strand.gain = k_sweptStrand[p].gain;
        r->strand.fineMix = 0.70f;
        r->strand.thirdWeight = 0.81f;
        r->strand.flowDistort = 0.63f;
        r->mask.edgeSoft = k_sweptStrand[p].edge;
        r->mask.dissolve = k_sweptStrand[p].dissolve;
        r->mask.dissolveSoft = 0.30f;
        r->mask.tailFadeA = 0.78f;
        r->mask.tailFadeB = 1.0f;
        r->mask.tailStagger = 0.15f;
        r->mask.tailDissolve = 0.22f;
        r->mask.tailNarrow = 0.60f;
        r->radiusDefault = 0.10f;
        for (int i = 0; i < 3; i++)
        {
            float detune = 1.0f + (float)i * 0.70f;
            UVDeform_AddLayer(&r->deform, (UVDeformLayer){
                .kind = UV_DEFORM_SINE, .driveAxis = 0, .outAxis = 0,
                .amplitude = amp, .frequency = k_sweptStrand[p].freq * detune,
                .speed = k_sweptStrand[p].travel, .phase = 0.0f,
                .param = (float)(i + 1),
                .env = UV_ENV_HEAD_WELD, .envAxis = 1,
                .envStart = 0.0f, .envEnd = 0.10f});
        }
        SurfaceFlow_AddLayer(&r->flow, (SurfaceFlowLayer){
            .tiling = {1.0f, 0.65f}, .pan = {0.0f, 0.30f},
            .blend = SURFACE_FLOW_MAX, .env = UV_ENV_NONE});
        r->colour.tailDarken = 0.40f;
    }

    // ── ENERGY — braided hot filaments with a gold core ─────────────────────
    {
        TrailRecipe *r = &k_trailPresets[TRAIL_PRESET_ENERGY];
        r->topology = TRAIL_SAMPLE_PARALLEL;
        r->surface = VFX_SURFACE_ENERGY_RIBBON;
        r->layers = k_sweptLayers[TRAIL_PRESET_ENERGY];
        r->layerCount = 1;
        r->radiusDefault = 0.45f;
        r->hdrGain = 1.85f;
        r->tintSource = TRAIL_TINT_GLOW;
        r->additive = true;
        // Three wave fields, detuned so they braid instead of stacking into one
        // thick bundle. Arc-anchored (cycles per METRE): the crests stand on the
        // laid path and only time moves them, which is what separates flowing
        // energy from a rigid swinging rope.
        for (int i = 0; i < 3; i++)
        {
            float detune = 1.0f + (float)i * 0.65f;
            UVDeform_AddLayer(&r->deform, (UVDeformLayer){
                .kind = UV_DEFORM_SINE, .driveAxis = 1, .outAxis = 0,
                .amplitude = 0.40f, .frequency = 0.55f * detune,
                .speed = 0.85f, .phase = 0.0f, .param = (float)(i + 1),
                .env = UV_ENV_HEAD_WELD, .envAxis = 1,
                .envStart = 0.0f, .envEnd = 0.10f});
        }
        SurfaceFlow_AddLayer(&r->flow, (SurfaceFlowLayer){
            .tiling = {1.0f, 0.65f}, .pan = {0.0f, 0.35f},
            .blend = SURFACE_FLOW_MAX, .env = UV_ENV_NONE});
        // Unchanged numbers, now written down instead of inferred: ENERGY is
        // the one preset the owner has approved on screen, so the migration off
        // the derived values must leave it bit-for-bit where it was.
        r->strand = (TrailStrandConfig){.bundleWidth = 0.34f, .gain = 1.35f,
                                        .fineMix = 0.70f, .thirdWeight = 0.80f,
                                        .flowDistort = 0.55f};
        r->mask.edgeSoft = 0.18f;
        r->mask.dissolve = 0.55f;
        r->mask.dissolveSoft = 0.22f;
        r->mask.tailFadeA = 0.72f;
        r->mask.tailFadeB = 1.0f;
        r->mask.tailStagger = 0.13f;
        r->mask.tailDissolve = 0.20f;
        r->mask.tailNarrow = 0.55f;
        r->colour.coreWidth = 0.18f;
        r->colour.coreIntensity = 0.72f;  // was hotWhiten
        r->colour.tailDarken = 0.45f;
        r->colour.contrast = VFX_CONTRAST_ENERGY;
        r->bodyOpacity = 0.90f;
    }

    // ── SMOKE — many faint strands piling into occluding mass ───────────────
    // additive OFF with bodyOpacity near 1: smoke must hide what is behind it.
    // An additive plume is a glow whatever colour you give it.
    {
        TrailRecipe *r = &k_trailPresets[TRAIL_PRESET_SMOKE];
        r->topology = TRAIL_SAMPLE_PARALLEL;
        r->surface = VFX_SURFACE_SMOKE_STRAND;
        r->layers = k_sweptLayers[TRAIL_PRESET_SMOKE];
        r->layerCount = 1;
        r->radiusDefault = 0.70f;
        r->hdrGain = 1.0f;
        r->tintSource = TRAIL_TINT_NEUTRAL; // a plume carries no element identity
        r->additive = false;
        r->tintAlpha = 205;
        for (int i = 0; i < 3; i++)
        {
            float detune = 1.0f + (float)i * 0.85f;
            UVDeform_AddLayer(&r->deform, (UVDeformLayer){
                .kind = UV_DEFORM_SINE, .driveAxis = 1, .outAxis = 0,
                .amplitude = 0.30f, .frequency = 0.35f * detune,
                .speed = 0.30f, .phase = 0.0f, .param = (float)(i + 1),
                .env = UV_ENV_HEAD_WELD, .envAxis = 1,
                .envStart = 0.0f, .envEnd = 0.06f});
        }
        SurfaceFlow_AddLayer(&r->flow, (SurfaceFlowLayer){
            .tiling = {1.0f, 0.30f}, .pan = {0.0f, 0.12f},
            .blend = SURFACE_FLOW_MAX, .env = UV_ENV_NONE});
        // Also unchanged, also now written down. Smoke's `gain` is already
        // under 1 — a plume has no separate hairs — which is the same knob that
        // turns BACKDROP into mass, from the other direction.
        r->strand = (TrailStrandConfig){.bundleWidth = 0.26f, .gain = 0.75f,
                                        .fineMix = 0.70f, .thirdWeight = 0.85f,
                                        .flowDistort = 0.78f};
        r->mask.edgeSoft = 0.34f;
        r->mask.dissolve = 0.40f;
        r->mask.dissolveSoft = 0.45f;
        // The sheet already tapers both ends, so the material ramp only takes
        // the very tip — fading twice thins the plume to nothing.
        r->mask.tailFadeA = 0.94f;
        r->mask.tailFadeB = 1.0f;
        r->mask.tailStagger = 0.22f;
        r->mask.tailDissolve = 0.40f;
        r->mask.tailNarrow = 0.78f;
        r->colour.coreWidth = 0.0f;   // no hot core in smoke
        r->colour.coreIntensity = 0.0f;
        r->colour.tailDarken = 0.35f;
        // White at the head, cooling to a neutral grey. No element hue either
        // end: what tints a plume is the light on it, not what burned.
        r->colour.tail = (Color){112, 112, 120, 255};
        r->colour.contrast = VFX_CONTRAST_SMOKE;
        r->bodyOpacity = 0.96f;
        // smoke_strand.png is ONE complete streak with its taper painted in,
        // stretched once over the trail. Tiling a shape sheet gives a rope of
        // identical segments; the authoring decides this, not taste.
        UVFx_SyncStretch(&r->deform, &r->flow, true);
    }
}

// ── RECIPE → the fragment shader's current uniform set ──────────────────────
//
// TEMPORARY BRIDGE, and deliberately the only one. `trail_deform.fs` still
// carries the hand-written mode-2 formula, whose parameters the strand composer
// used to set field by field. Collapsing that shader to read the packed
// `u_uvField`/`u_flowLayer` blocks directly is the next step (core/docs/PROGRESS.md);
// until it lands, this function is where a recipe becomes those uniforms.
//
// Why a bridge rather than finishing the shader in the same change: the mode-2
// formula is ~150 lines the owner has already signed off on visually, and
// rewriting it in the same pass as the composer would leave no way to tell a
// composer regression from a shader regression if the result looked wrong.
// ONE translation point is also the whole property being bought here — the bug
// this refactor exists to prevent was the same fix having to land in three
// places, and a single function is not three.
//
// Deleting this is the definition of done for the shader step: when
// trail_deform.fs reads the packed blocks, this function and every
// `cfg.material.<mode-2 field>` line below go together.
static void TrailRecipe_ToLegacyMaterial(const TrailRecipe *r,
                                         const VFX_ElementMaterial *m,
                                         Color base, TrailMaterialConfig *out,
                                         TrailDeformConfig *outDeform)
{
    // The VERTEX deform stays off — every version of this effect that displaced
    // the vertices read as a rigid rope. But envHead/envTail/phase are still
    // read by the FRAGMENT stage (the disorder ramp and the per-spawn phase),
    // so they have to be filled even though mode is 0. Leaving envHead at 0
    // collapses the head-weld window, the ramp saturates at the first segment,
    // and the dissolve then bites from the head instead of the tail.
    outDeform->mode = 0.0f;
    outDeform->envHead = (r->deform.layerCount > 0)
                             ? r->deform.layers[0].envEnd : 0.10f;
    outDeform->envTail = 0.99f;
    outDeform->phase = Random01() * 10.0f; // no two casts read identical
    // Mode 2 IS "PARALLEL topology over a strand-grammar sheet". A SUMMED preset
    // has no wave layers and wants the plain textured path, which is mode 0 —
    // the swept presets' layer stack draws through the body shader instead.
    out->mode = (r->topology == TRAIL_SAMPLE_PARALLEL) ? 2.0f : 0.0f;
    if (out->mode < 1.5f)
        return;

    // The three wave layers were authored as one amplitude/frequency with a
    // per-layer detune; recover the pair the shader still expects.
    const UVDeformLayer *L0 = &r->deform.layers[0];
    out->waveAmp = L0->amplitude;
    out->waveFreq = L0->frequency;
    out->waveTravel = L0->speed;
    // detune of layer 1 over layer 0 — the "spread" between the fields
    out->waveSpread = (r->deform.layerCount > 1 && L0->frequency > 0.0f)
                          ? (r->deform.layers[1].frequency / L0->frequency) - 1.0f
                          : 0.0f;
    // AUTHORED, not inferred. This used to be `amplitude * 0.85`, which ties
    // how WIDE a bundle is to how far it SWINGS — two independent things, and
    // the tie is what capped every swept preset at a quarter of its own quad
    // (a hairline however large the radius). See TrailStrandConfig.
    out->bundleWidth = r->strand.bundleWidth;
    out->edgeSoft = r->mask.edgeSoft;
    out->hdrGain = r->hdrGain;
    out->stretchUV = r->deform.stretchUV ? 1.0f : 0.0f;

    const SurfaceFlowLayer *F0 = &r->flow.layers[0];
    out->tilingX = F0->tiling.y;
    out->tilingY = 1.0f;
    out->panCoarse = F0->pan.y;
    out->panFine = F0->pan.y * 2.0f;

    out->dissolve = r->mask.dissolve;
    out->dissolveSoft = r->mask.dissolveSoft;
    out->tailFadeA = r->mask.tailFadeA;
    out->tailFadeB = r->mask.tailFadeB;
    out->tailStagger = r->mask.tailStagger;
    out->tailDissolve = r->mask.tailDissolve;
    out->tailNarrow = r->mask.tailNarrow;

    // The core colour comes from the material's authored hot gradient. For Fire
    // this moves orange-red toward gold instead of toward pink-white, which is
    // what whitening a red glow directly produced.
    Color hotTarget = ColorGradient_Sample(m->hotGrad, 0.20f);
    out->hotColor = ColorLerp(base, hotTarget, r->colour.coreIntensity);
    out->hotColor.a = 255;
    // An AUTHORED tail wins. The computed blend walks `base` toward `m->body`,
    // so a preset whose head already IS `m->body` blends a colour toward itself
    // and the ramp silently becomes a no-op — head and tail identical, which is
    // one flat hue down the whole trail. `colour.tail` exists for that case.
    if (r->colour.tail.a > 0)
    {
        out->tailColor = r->colour.tail;
        out->tailColor.a = 255;
    }
    else
    {
        out->tailColor = (Color){
            (unsigned char)(base.r * (1.0f - r->colour.tailDarken) + m->body.r * r->colour.tailDarken),
            (unsigned char)(base.g * (1.0f - r->colour.tailDarken) + m->body.g * r->colour.tailDarken),
            (unsigned char)(base.b * (1.0f - r->colour.tailDarken) + m->body.b * r->colour.tailDarken), 255};
    }

    // The archetype, straight off the recipe. These were derived from unrelated
    // mask/colour fields so that no constant had to be written down; the result
    // was that editing a tail knob re-sampled the surface. TrailStrandConfig.
    out->wispMix = r->strand.fineMix;
    out->strandGain = r->strand.gain;
    out->flowStrength = r->strand.flowDistort;
    out->bundleWeight = r->strand.thirdWeight;
}

static const TrailRecipe *TrailPresetRecipe(TrailPresetId p)
{
    TrailPresets_Build();
    if (p < 0 || p >= TRAIL_PRESET_COUNT) p = TRAIL_PRESET_BLADE;
    return &k_trailPresets[p];
}

static int SweptTrail_LayerCount(TrailPresetId kind)
{
    return TrailPresetRecipe(kind)->layerCount;
}

// The recipe is COPIED into the pool slot, not pointed at: the renderer holds a
// pointer to it for the trail's whole life, and a per-instance sheet or a live
// tunable has to be able to move without editing the shared preset row. Same
// lifetime contract as `layers` — pool storage, never a caller's stack.
static void SweptTrail_ConfigureLayers(VC_SweptTrail *s)
{
    int count = SweptTrail_LayerCount(s->kind);
    for (int L = 0; L < count; L++)
        s->layers[L] = k_sweptLayers[s->kind][L];

    s->recipe = *TrailPresetRecipe(s->kind);
    s->recipe.layers = s->layers;
    s->recipe.layerCount = count;
    s->recipe.bodyOpacity = s_sweptBodyOpacity;

    // WHERE THE SHEET COMES FROM, in priority order: a per-instance surface, the
    // preset's runtime-generated sheet, or the registry profile it names. The
    // registry arm is the normal one and was the whole point of `recipe.surface`
    // — a preset that names a profile and never resolves it renders NOTHING,
    // which is not distinguishable on screen from a broken shader.
    const Texture2D *body = NULL;
    if (s->hasSurface && s->surface.texture.id != 0)
        body = &s->surface.texture;
    else if (s->recipe.sheetOverride != NULL)
        body = s->recipe.sheetOverride;
    else
    {
        const VFX_SurfaceProfile *prof = VFX_SurfaceRegistry_Get(s->recipe.surface);
        if (prof != NULL && prof->body.id > 0)
        {
            s_trailSheet[s->kind] = prof->body;
            SetTextureFilter(s_trailSheet[s->kind], TEXTURE_FILTER_BILINEAR);
            SetTextureWrap(s_trailSheet[s->kind], TEXTURE_WRAP_REPEAT);
            body = &s_trailSheet[s->kind];
        }
        else
        {
            TraceLog(LOG_WARNING,
                     "VFX_TRAIL: preset %d names surface %d but the registry has "
                     "no sheet for it — falling back to the procedural mask",
                     (int)s->kind, (int)s->recipe.surface);
            body = &s_sweptBodyTex;
        }
    }
    s->recipe.sheetOverride = body;

    s->layers[count > 1 ? 1 : 0].texture = body;
    if (count >= 2)
        s->layers[0].texture = (s_sweptHaloTex.id != 0) ? &s_sweptHaloTex : NULL;
    if (count == 3)
    {
        s->layers[2].texture = s->layers[0].texture;
        s->layers[2].alphaMul *= s_sweptCoreHot;
    }
}

// The tail→head colour ramp, one per material, built on first use. MOVED to
// vc_common.inl when VFX_ComposeVolumeTrail became its second caller — the ramp
// is the element's, not this composition's. Kept as a name here because the
// spawn path reads better with it and one test pins the call site.
static const ColorGradient *SweptTrail_Gradient(VC_MaterialId mat)
{
    return VC_ElementRamp(mat);
}

// ── Strand plumbing ─────────────────────────────────────────────────────────

// The entity this handle's strand refers to, or NULL if it has been recycled
// under us. NEVER trust a stored trail id: ids are reused and the pool evicts by
// priority, so writing a thickness into a stale one corrupts an unrelated effect.
static TrailEntity *SweptTrail_Strand(const VC_SweptTrail *s, int slot, int strand)
{
    int id = s->strandId[strand];
    if (id < 0)
        return NULL;
    TrailEntity *t = GetTrail(id);
    if (!t || !t->active || t->ownerTag != (SWEPT_TAG_BASE | (slot << 4) | strand))
        return NULL;
    return t;
}

// A composition may request a visual helix without moving its attached Matrix.
// The public swept-trail contract stays a path follower; this is private
// composition plumbing for effects that know their flight axis.
static void SweptTrail_SetAnchoredHelix(int handle, Vector3 axis, float radius,
                                        float turns, float phase)
{
    if (handle < 0 || handle >= SWEPT_MAX || !s_swept[handle].active)
        return;

    VC_SweptTrail *s = &s_swept[handle];
    for (int strand = 0; strand < s->strands; strand++)
    {
        TrailEntity *t = SweptTrail_Strand(s, handle, strand);
        if (!t)
            continue;
        t->helixAxis = axis;
        t->helixRadius = radius;
        t->helixTurns = turns;
        t->helixPhase = phase;
    }
}

static int SweptTrail_SpawnStrand(const VC_SweptTrail *s, int slot, int strand)
{
    const VFX_ElementMaterial *m = VFX_Material(s->matId);
    TrailConfig cfg = {0};
    cfg.type = TRAIL_TYPE_FOLLOWER;
    cfg.pos = Vector3Transform((Vector3){0.0f, 0.0f, 0.0f}, *s->xf);
    // Long-lived by construction: this trail dies when the caller kills it, or
    // when it stops being fed and the idle fade drains it. A lifetime here would
    // be a second, hidden death condition.
    cfg.life = 1.0e6f;
    cfg.thick = 0.05f; // real value written every frame from the aspect cap
    cfg.tint = WHITE;  // the gradient carries the colour
    const TrailRecipe *rec = &s->recipe;
    const TrailMotion *mot = TrailMotionOf(s->kind);
    // Two encodings of the along-trail ramp, one concept. The swept presets take
    // the element's authored N-stop gradient; the strand presets cool toward a
    // tone computed from the material, which is what `tailDarken` is for.
    if (rec->colour.useElementRamp)
        cfg.gradient = SweptTrail_Gradient(s->matId);
    // An unpopulated curve evaluates to 0, not to 1 — passing one a preset never
    // authored zeroes the strip's width and alpha while every other symptom
    // (history, thickness, texture, material mode) still looks healthy.
    cfg.widthCurve = mot->curves ? &s_sweptWidthCurve[s->kind] : NULL;
    cfg.alphaCurve = mot->curves ? &s_sweptAlphaCurve[s->kind] : NULL;
    cfg.widthEnvelope = mot->widthEnvelope;
    cfg.forceField = mot->cloth ? &s_sweptCloth[s->kind] : NULL;
    cfg.ownerTag = SWEPT_TAG_BASE | (slot << 4) | strand;
    cfg.priority = VFX_PRIORITY_LOW;
    cfg.blendMode = rec->additive ? BLEND_ADDITIVE : BLEND_ALPHA;
    // BLEND_ALPHA is enum value 0, so without this flag the legacy fallback
    // reads zero as "unspecified" and silently draws additive — which for the
    // smoke preset turns an occluding plume back into a glow.
    cfg.useCustomBlendMode = true;
    cfg.minVertexDistance = SWEPT_MIN_VERTEX;
    // BLADE lies in the plane of the swing — a broad sheet from the front and a
    // thin edge from the side, which IS the sense of a real object moving. The
    // other two are camera-facing, the mode that never pinches on a curve.
    cfg.ribbonMode = rec->ribbonMode;
    cfg.fixedNormal = rec->fixedNormal;
    cfg.disableInnerCore = true; // superseded by the layer stack / the hot core
    // ── THE WHOLE MATERIAL, in one assignment ───────────────────────────────
    // Everything the fragment stage needs now travels as the recipe. The
    // per-mode field bags this replaced are what let a fix land in one of three
    // copies; there is nothing left here to forget to set.
    cfg.material.recipe = rec;
    cfg.material.contrastProfile = rec->colour.contrast;
    cfg.material.bodyOpacity = rec->bodyOpacity;
    {
        const VFX_ElementMaterial *em = VFX_Material(s->matId);
        Color base;
        switch (rec->tintSource)
        {
        case TRAIL_TINT_NEUTRAL: base = (Color){236, 236, 240, 255}; break;
        case TRAIL_TINT_BODY:    base = em->body; break;
        default:                 base = em->glow; break;
        }
        cfg.tint = VC_WithAlpha(base, rec->tintAlpha);
        TrailRecipe_ToLegacyMaterial(rec, em, base, &cfg.material, &cfg.deform);
    }
    cfg.layers = s->layers;
    cfg.layerCount = rec->layerCount;
    if (mot->sweptKnobs)
    {
        cfg.uvMetresPerTile = (s_sweptTile > 0.05f) ? s_sweptTile : 0.05f;
        cfg.uvScrollSpeed = SWEPT_FLOW_SPEED * s_sweptFlow;
    }
    // Node CONSTRAINTS belong to the cloth simulation, not to every trail. A
    // strand preset's shape is the wave field in UV; pulling its nodes back
    // toward a home position and re-ordering them bends the laid path the waves
    // are anchored to, which shows up as a bulge at the head rather than as the
    // even braid the sheet was authored for.
    cfg.nodeHomeSpring = mot->cloth ? SWEPT_HOME_SPRING : 0.0f;
    cfg.nodeHomeMaxDev = mot->cloth ? SWEPT_HOME_MAX_DEV : 0.0f;
    cfg.nodeOrderFrac = mot->cloth ? SWEPT_ORDER_FRAC : 0.0f;
    cfg.sampleHz = mot->sampleHz;
    cfg.teleportSpeed = mot->teleportSpeed;
    cfg.idleSpeed = mot->idleSpeed;
    cfg.smoothSpline = mot->smoothSpline;
    cfg.trailLength = (float)SweptTrail_MaxNodesFor(s->kind, s->lifetime);
    cfg.useFlowMap = s->hasSurface && s->surface.flowMap.id != 0;
    cfg.flowMap = cfg.useFlowMap ? &s->surface.flowMap : NULL;
    cfg.flowSpeed = s->hasSurface ? s->surface.flowSpeed : 0.0f;
    cfg.flowStrength = s->hasSurface ? s->surface.flowStrength : 0.0f;
    cfg.flowTiling = (s->hasSurface && s->surface.flowTiling > 0.0f)
                         ? s->surface.flowTiling : 1.0f;
    cfg.noiseMask = (s->hasSurface && s->surface.noiseMask.id != 0)
                        ? &s->surface.noiseMask : NULL;
    cfg.dissolve = s->hasSurface ? s->surface.dissolve : 0.0f;
    cfg.maskTiling = (s->hasSurface && s->surface.maskTiling > 0.0f)
                         ? s->surface.maskTiling : 1.0f;
    (void)m;
    int id = SpawnTrailEntity(cfg);
    if (id >= 0)
        Trail_AttachToTransform(id, s->xf, (Vector3){0.0f, 0.0f, 0.0f});
    // ONE LINE PER STRAND, unconditional. "It looks the same as before" and "the
    // new path is not running" are indistinguishable on screen, and three rounds
    // have now been spent reading code to tell them apart. This says which.
    TraceLog(LOG_INFO,
             "VFX_SWEPT: slot %d strand %d — style %d, shape %s, radial %d, "
             "tier %d, requested width %.2f m",
             slot, strand, (int)s->kind,
             "ribbon",
             cfg.tubeRadialSegs, (int)GfxQuality_Get(), s->width);
    return id;
}

// ── Public API ───────────────────────────────────────────────────────────────

int VFX_ComposeTrailEx(const Matrix *followTransform, VC_MaterialId mat,
                             float width, float lifetime, TrailPresetId kind,
                             const VFX_TrailSurface *surface)
{
    SweptTrail_InitShared();
    if (!followTransform)
    {
        TraceLog(LOG_WARNING, "VFX_SWEPT: NULL transform — no trail created");
        return -1;
    }
    if (kind < TRAIL_PRESET_BLADE || kind >= TRAIL_PRESET_COUNT)
    {
        TraceLog(LOG_WARNING,
                 "VFX_TRAIL: preset %d is out of range — clamped to BLADE.",
                 (int)kind);
        kind = TRAIL_PRESET_BLADE;
    }
    // A preset's authored radius when the caller does not care. The strand
    // presets are authored at a fixed half-width (their waves swing inside it),
    // which is why they carry a bigger default than the swept ones.
    if (width <= 0.0f)
        width = TrailPresetRecipe(kind)->radiusDefault > 0.0f
                    ? TrailPresetRecipe(kind)->radiusDefault : 0.22f;
    if (lifetime <= 0.0f)
        lifetime = 0.5f;

    int slot = -1;
    for (int i = 0; i < SWEPT_MAX; i++)
    {
        if (!s_swept[i].active)
        {
            slot = i;
            break;
        }
    }
    if (slot < 0)
    {
        // Announced: a trail that never appears and a trail that was never
        // requested look identical on screen (core/CLAUDE.md §4).
        slot = s_sweptNextSerial % SWEPT_MAX;
        TraceLog(LOG_WARNING, "VFX_SWEPT: pool full (%d) — recycling slot %d",
                 SWEPT_MAX, slot);
        VFX_KillTrail(slot);
    }
    s_sweptNextSerial++;

    VC_SweptTrail *s = &s_swept[slot];
    s->active = true;
    s->xf = followTransform;
    s->matId = mat;
    s->kind = kind;
    s->hasSurface = (surface != NULL);
    s->surface = surface ? *surface : (VFX_TrailSurface){0};
    s->width = width;
    s->lifetime = lifetime;
    s->widthTarget = 1.0f;
    s->widthLevel = 1.0f;
    s->sparkAcc = 0.0f;
    s->normal = (Vector3){0.0f, 1.0f, 0.0f};
    s->hasNormal = false;
    s->lateralAxis = (Vector3){0.0f, 1.0f, 0.0f};
    s->hasLateral = false;
    s->wasFrozen = false;
    s->widthLogged = false;
    s->strands = SweptTrail_StrandCount(kind, GfxQuality_Get() < GFX_MED);
    SweptTrail_ConfigureLayers(s);
    for (int k = 0; k < SWEPT_STRANDS_MAX; k++)
        s->strandId[k] = -1;
    for (int k = 0; k < s->strands; k++)
        s->strandId[k] = SweptTrail_SpawnStrand(s, slot, k);
    return slot;
}

int VFX_ComposeTrail(const Matrix *followTransform, VC_MaterialId mat,
                           float width, float lifetime, TrailPresetId kind)
{
    return VFX_ComposeTrailEx(followTransform, mat, width, lifetime, kind, NULL);
}

void VFX_TrailSetWidth(int handle, float width01)
{
    if (handle < 0 || handle >= SWEPT_MAX || !s_swept[handle].active)
        return;
    if (width01 < 0.0f)
        width01 = 0.0f;
    if (width01 > 1.0f)
        width01 = 1.0f;
    // Only the TARGET moves; Update ramps toward it. Assigning straight through
    // is what makes a wind-down pop.
    s_swept[handle].widthTarget = width01;
}

// Ends emission while PRESERVING the laid ribbon, so it drifts and dissolves on
// its own instead of popping out of existence. Detaching keeps the history;
// VFX_KillTrail() is the immediate cut.
void VFX_Trail_Stop(int trailId)
{
    if (trailId < 0 || trailId >= SWEPT_MAX || !s_swept[trailId].active)
        return;
    VC_SweptTrail *s = &s_swept[trailId];
    for (int c = 0; c < s->strands; c++)
    {
        if (s->strandId[c] >= 0)
            Trail_AttachToTransform(s->strandId[c], NULL, (Vector3){0.0f, 0.0f, 0.0f});
    }
    s->active = false;
    s->xf = NULL;
}

void VFX_KillTrail(int handle)
{
    if (handle < 0 || handle >= SWEPT_MAX)
        return;
    VC_SweptTrail *s = &s_swept[handle];
    // DETACH rather than kill. Cutting a ribbon out of existence mid-swing is a
    // pop; detaching stops the feed, and the strands then drain their own
    // history and fade — the wind-down the caller wanted.
    //
    // And detaching is not optional. The entity holds the CALLER'S Matrix, so a
    // strand still attached after the caller's storage goes out of scope is a
    // read after free every frame until the idle fade finishes.
    for (int k = 0; k < SWEPT_STRANDS_MAX; k++)
    {
        TrailEntity *t = SweptTrail_Strand(s, handle, k);
        if (t)
            Trail_AttachToTransform(s->strandId[k], NULL, (Vector3){0.0f, 0.0f, 0.0f});
        s->strandId[k] = -1;
    }
    s->active = false;
    s->xf = NULL;
}

// ── Per-frame ────────────────────────────────────────────────────────────────

// Metres of tip path currently inside the tail window, walked from the entity's
// own history. This is what the aspect cap is measured against — the length the
// tip ACTUALLY travelled, not the caller's idea of it.
static float SweptTrail_TravelLength(const TrailEntity *t)
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

// The swing plane. Three samples spanning the tail give two chords; their cross
// product is along the axis the tip is turning about.
//
// WHICH SIDE OF THE STRIP THIS PUTS u = 0 ON, and it is not a matter of taste.
// The ribbon strip emits u = 0 at center + side*halfWidth with
// side = normalize(cross(tangent, normal)). For a tip turning at angular
// velocity w about a radius r, tangent = w x r and normal here is along w, so
//     side = (w x r) x ŵ = r(w·ŵ) - w(r·ŵ) = |w| r     for r ⊥ w,
// i.e. side points radially OUTWARD — u = 0 is the outer edge of the swing, the
// line the blade itself traced.
//
// Sign: cross(chord1, chord2) already carries the turn's handedness, so it is
// used unflipped. At an inflection (the crossing of a figure-eight) the plane
// genuinely reverses; the normal is SNAPPED there rather than interpolated,
// because lerping across a sign change passes through a degenerate normal and
// the strip would collapse for a few frames. Curvature is ~0 at an inflection,
// so the snap is invisible.
static void SweptTrail_UpdateNormal(VC_SweptTrail *s, const TrailEntity *t)
{
    int span = t->historyCount - 1;
    if (span < 2)
        return;
    int half = span / 2;
    Vector3 c = t->history[t->historyHead];
    Vector3 b = t->history[(t->historyHead - half + TRAIL_HISTORY_COUNT) % TRAIL_HISTORY_COUNT];
    Vector3 a = t->history[(t->historyHead - span + TRAIL_HISTORY_COUNT) % TRAIL_HISTORY_COUNT];
    Vector3 n = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, b));
    if (Vector3LengthSqr(n) < 1e-10f)
        return; // straight line: keep the last plane
    n = Vector3Normalize(n);
    if (!s->hasNormal || Vector3DotProduct(n, s->normal) < 0.0f)
    {
        s->normal = n; // first sample, or an inflection
        s->hasNormal = true;
    }
    else
    {
        s->normal = Vector3Normalize(Vector3Lerp(s->normal, n, 0.25f));
    }

    // The SAME axis, sign-stabilised, and the two must not be conflated.
    // `normal` carries the true handedness of the turn, which is what BLADE's
    // masked strip needs. FILAMENT uses this axis to spread its strands, and
    // there the sign flip at an inflection is a BUG: the offsets jump from +o to
    // -o in one frame, every strand's newest node teleports across the bundle,
    // and the threads knot together at the head.
    Vector3 lat = s->normal;
    if (s->hasLateral && Vector3DotProduct(lat, s->lateralAxis) < 0.0f)
        lat = Vector3Negate(lat);
    s->lateralAxis = lat;
    s->hasLateral = true;
}

static void VC_SweptTrail_Update(float dt)
{
    if (dt <= 0.0f)
        return;

    // The procedural streak sheet is the swept presets' body. The strand presets
    // name a registry surface instead and resolve it per preset at spawn, so
    // this is a fallback, not a global override.
    s_sweptBodyTex = s_sweptBladeTex;

    for (int i = 0; i < SWEPT_MAX; i++)
    {
        VC_SweptTrail *s = &s_swept[i];
        if (!s->active)
            continue;
        SweptTrail_ConfigureLayers(s);

        // Ramped width — never popped.
        float k = 1.0f - expf(-dt * 6.0f);
        s->widthLevel += (s->widthTarget - s->widthLevel) * k;

        bool frozen = false;
        const TrailEntity *lead = NULL;

        const TrailMotion *mot = TrailMotionOf(s->kind);
        for (int c = 0; c < s->strands; c++)
        {
            TrailEntity *t = SweptTrail_Strand(s, i, c);
            if (!t)
            {
                // Evicted or recycled under us. Respawning is why the tag
                // exists: eviction becomes self-healing instead of fatal.
                s->strandId[c] = SweptTrail_SpawnStrand(s, i, c);
                continue;
            }
            if (c == 0)
                lead = t;

            // Re-pushed every frame, not baked at spawn: bodyOpacity is a
            // tunable, and a long-lived trail (cfg.life = 1e6) would otherwise
            // hold the value it was born with — the sweep would move nothing
            // until something respawned, which reads as "the knob is dead".
            t->material.bodyOpacity = s_sweptBodyOpacity;

            // FREEZE, and it fills first: freezing a trail with one node holds
            // an EMPTY ribbon, and a dial that shows nothing reads as broken.
            frozen = (s_sweptFreeze >= 0.5f) &&
                     (t->historyCount >= SweptTrail_MaxNodesFor(s->kind, s->lifetime));
            Trail_SetFrozen(s->strandId[c], frozen);

            // Width from the length the tip ACTUALLY travelled, with the
            // caller's width as a ceiling.
            // The swept family's global knobs, or neutral 1.0 for a preset
            // that never had them. See TrailMotion::sweptKnobs.
            const float knobW = mot->sweptKnobs ? s_sweptWidthMul : 1.0f;
            const float knobA = mot->sweptKnobs ? s_sweptAlphaMul : 1.0f;
            float travel = SweptTrail_TravelLength(t);
            t->thickness = SweptTrail_HalfWidth(s->width * knobW,
                                                s->widthLevel, travel, s->kind);
            // THE NUMBER THAT DECIDES WHETHER ANY OF THIS IS VISIBLE. The width
            // is EARNED from the length the tip travelled, so a trail can be
            // configured perfectly and still draw as a hairline if the aspect
            // cap is holding it down. Reported once, when the history is full.
            if (!s->widthLogged && t->historyCount >= SweptTrail_MaxNodesFor(s->kind, s->lifetime))
            {
                TraceLog(LOG_INFO,
                         "VFX_SWEPT: slot %d strand %d — travelled %.2f m -> "
                         "radius %.3f m (%.2f m across). Ceiling was %.2f m.",
                         i, c, travel, t->thickness, t->thickness * 2.0f,
                         s->width * knobW * 0.5f);
                s->widthLogged = true;
            }
            // WHITE only where a GRADIENT carries the colour. A preset whose
            // colour lives in the tint (useElementRamp = false) would be bleached
            // to white every frame by this — the swept convention is
            // `cfg.tint = WHITE` precisely because its gradient owns the hue.
            if (s->recipe.colour.useElementRamp)
                t->tint = VC_WithAlpha(WHITE, (unsigned char)(255.0f * Clamp(knobA, 0.0f, 1.0f)));
            else
                t->tint.a = (unsigned char)((float)s->recipe.tintAlpha *
                                            Clamp(knobA, 0.0f, 1.0f));
            if (mot->sweptKnobs)
            {
                t->uvMetresPerTile = (s_sweptTile > 0.05f) ? s_sweptTile : 0.05f;
                t->uvScrollSpeed = SWEPT_FLOW_SPEED * s_sweptFlow;
            }
            t->flowSpeed = s->hasSurface ? s->surface.flowSpeed : 0.0f;
            t->flowStrength = s->hasSurface ? s->surface.flowStrength : 0.0f;
            t->flowTiling = (s->hasSurface && s->surface.flowTiling > 0.0f)
                                ? s->surface.flowTiling : 1.0f;
            t->dissolve = s->hasSurface ? s->surface.dissolve : 0.0f;
            t->maskTiling = (s->hasSurface && s->surface.maskTiling > 0.0f)
                                ? s->surface.maskTiling : 1.0f;

            if (c == 0)
                SweptTrail_UpdateNormal(s, t);
            if (s->kind == TRAIL_PRESET_BLADE)
                t->fixedNormal = s->normal;

            // FILAMENT's strands are spread along the swing-plane axis. Not a
            // fixed local offset: the axis is derived from the path the trail
            // has already travelled, so it only exists here and has to be
            // pushed in each frame.
            if (s->strands > 1 && s->hasLateral)
                Trail_SetLateralOffset(s->strandId[c],
                                       Vector3Scale(s->lateralAxis,
                                                    k_sweptSpread[c] * t->thickness *
                                                        s_sweptSpread));
        }

        if (frozen != s->wasFrozen)
        {
            TraceLog(LOG_INFO,
                     "VFX_SWEPT: slot %d %s — flow %.2f tiles/s still running",
                     i, frozen ? "FROZEN, only the flow moves" : "released",
                     SWEPT_FLOW_SPEED * s_sweptFlow);
            s->wasFrozen = frozen;
        }
        if (frozen || !lead)
            continue;

        // SPARKLE — the fourth layer of the reference sheet, and the only one
        // that is not geometry. A RATE, carried between frames.
        s->sparkAcc += dt * SWEPT_SPARK_RATE * s_sweptSpark;
        int sparks = (int)s->sparkAcc;
        if (sparks > 3)
            sparks = 3;
        s->sparkAcc -= (float)sparks;
        if (s_sweptSpark > 0.01f && GfxQuality_Get() >= GFX_MED && lead->historyCount > 1)
        {
            const VFX_ElementMaterial *sm = VFX_Material(s->matId);
            for (int k2 = 0; k2 < sparks; k2++)
            {
                Vector3 jit = {(Random01() - 0.5f), (Random01() - 0.5f), (Random01() - 0.5f)};
                // ALONG the ribbon, not only off the head. Biased toward the
                // head (sqrt) so the shower is densest where the energy is,
                // without the tail going bare.
                int span = lead->historyCount - 1;
                int back = (int)(sqrtf(Random01()) * (float)span);
                Vector3 born = lead->history[(lead->historyHead - back + TRAIL_HISTORY_COUNT) %
                                             TRAIL_HISTORY_COUNT];
                SpawnParticle((ParticleConfig){
                    .position = Vector3Add(born, Vector3Scale(jit, 0.09f)),
                    // MAGIC DUST, not sparks: "born, stay where they are, drift
                    // gently, then go." A spark thrown at a quarter of the tip's
                    // speed is grit leaving a grinder; dust hangs in the air the
                    // ribbon passed through. So the velocity is almost nothing
                    // and there is no stretch at all — a streaked dust mote is a
                    // contradiction.
                    .velocity = Vector3Scale(jit, 0.11f),
                    // Small and VERY bright: the sparkle comes from luminance,
                    // not from area. emissiveBoost pushes it over the bloom
                    // threshold, which turns a 1 cm dot into a star.
                    .radius = Math_Mix(0.006f, 0.013f, Random01()),
                    .lifetime = Math_Mix(0.55f, 1.40f, Random01()),
                    .colorStart = VC_WithAlpha(VC_Whiten(sm->glow, 0.75f), 255),
                    .colorEnd = VC_WithAlpha(VC_Whiten(sm->glow, 0.30f), 0),
                    // The TWINKLE. One shared curve would pulse every mote in
                    // lockstep, so each gets a random lifetime instead and the
                    // curve's three beats land at different wall-clock moments.
                    .alphaCurve = &s_sweptTwinkle,
                    .render.blendMode = VFX_BLEND_ADDITIVE,
                    .render.unlit = 1,
                    .render.emissiveBoost = 1.9f,
                });
            }
        }
    }
}

// Nothing to draw. DrawTrailEntities owns the ribbon now, and the sparkles
// belong to the particle system — which is the whole point of the port.
//
// The function stays because the Update/Draw3D PAIR is how a stateful
// composition declares itself to scripts/sync_vfx_test.py; without it the
// generator would not emit the update dispatch either, and the trail would
// silently never be fed.
static void VC_SweptTrail_Draw3D(Camera3D cam)
{
    (void)cam;
}
