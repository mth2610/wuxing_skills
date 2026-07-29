// ── H1. VFX_ComposeSweptTrail — the swept weapon/body trail ──────────────────
//
// Đợt H's first task, and the one with the largest visual delta per line of new
// code, for a reason that is arithmetic rather than aesthetic: `core/trail_system.h`
// is 18 shipping entry points — attach-to-transform, orbit followers, RibbonMode,
// arc-length UV, inner/outer double strips — and after the F0 purge **not one
// composition used it** (VFX_PLAN §0, the primitive table). Everything we owned
// was a camera-facing sprite, and a sprite cannot hold a silhouette from an
// angle. A swept strip lying in the plane of the swing is a broad sheet from the
// front and a thin edge from the side, and that change IS the sense of a real
// object moving.
//
// WHY THIS IS A COMPOSITION AND NOT "JUST CALL THE TRAIL SYSTEM". The trail
// system takes a texture and a config and knows nothing about elements, the
// blend law, the tier budget, or the aspect-ratio rule. Every skill wiring it by
// hand would re-derive those four things and each would get them slightly wrong.
// Concretely, this file owns the five decisions the raw primitive cannot make:
//
//   1. THE PLANE. BLADE runs RIBBON_FIXED_NORMAL with the normal recomputed
//      every frame from the tip's own path (§ the swing normal, below). That is
//      what makes it read as an object rather than a decal, and it is not
//      something a caller can be expected to maintain.
//   2. THE ASPECT RATIO. Width is capped against the length the tip ACTUALLY
//      travelled, not against the caller's number (core/docs/LANDMINES.md,
//      "Thickness is a ratio against the thing's OWN length"). A slow weapon
//      gets a thin trail instead of a fat stub; a fast one gets the full width.
//   3. THE SAMPLE CLOCK. Nodes are laid down at a fixed 60 Hz with sub-frame
//      interpolation, never once per frame — otherwise the tail's length in
//      metres moves with the frame rate, which is the rate-vs-count rule
//      (VFX_PLAN §0.3) applied to geometry instead of particles.
//   4. THE LAG SCHEDULE. FILAMENT is several strands reading the same path at
//      different delays. The delay is in SECONDS and converted to ring samples
//      here, so it does not become a frame count by accident.
//   5. THE TIER GATE. FILAMENT sheds strands below GFX_MED. The gate only ever
//      clamps DOWN.
//
// WHY IT DRIVES THE STRANDS BY HAND INSTEAD OF Trail_AttachToTransform. The
// attach path (trail_system.c:719) is exactly right for one strand at zero lag,
// and useless for the other two things this needs: a delayed sample (FILAMENT)
// and a motion gate (a stationary weapon must let its trail decay, and the
// attach path re-stamps the idle timer every frame, so an idle blade would hold
// a frozen full-length ribbon forever). Driving `UpdateFollowerPosition`
// ourselves also means the entity never holds a pointer into the caller's
// storage, so a caller whose Matrix goes out of scope after VFX_KillSweptTrail
// cannot be read after free.
//
// Managed archetype: private pool + VC_SweptTrail_Update/_Draw3D, which is what
// a handle-returning composition needs — a manifest `type` is not sufficient
// (VFX_PLAN §0.3; same shape as vc_character_aura.inl).

#include "core/tuning.h"

#define SWEPT_MAX          8      // concurrent trails
#define SWEPT_STRANDS_MAX  4      // strands per trail (FILAMENT uses all of them)
#define SWEPT_RING         64     // tip-path samples kept; >= TRAIL_HISTORY_COUNT
#define SWEPT_SAMPLE_HZ    60.0f  // node rate — a RATE, so the tail's metres do
#define SWEPT_SAMPLE_DT    (1.0f / SWEPT_SAMPLE_HZ)   // not move with the frame rate
#define SWEPT_STEPS_MAX    6      // sub-steps per frame after a hitch
#define SWEPT_LAG_STEP     0.030f // seconds between consecutive FILAMENT strands
#define SWEPT_FADE_TIME    0.45f  // seconds to fade once we stop feeding a strand
#define SWEPT_IDLE_SPEED   0.12f  // m/s below which the tip counts as stationary
#define SWEPT_MIN_VERTEX   0.005f // metres; rejects exact duplicate nodes only
// A TELEPORT is not a fast swing. Above this the tip did not travel, it was
// moved — the spawn point was dragged, an agent respawned, a blink fired — and
// laying nodes along the gap draws a long straight streak BRIDGING the two
// places, which is the one artefact in a trail that can never be mistaken for
// motion. The limit is per frame and scales with dt so a frame hitch is not
// mistaken for a jump; the floor is what catches a jump during a long frame.
#define SWEPT_TELEPORT_SPEED 45.0f  // m/s — well above any weapon tip
#define SWEPT_TELEPORT_MIN   0.75f  // metres in one frame, whatever dt was
#define SWEPT_SPARK_RATE     26.0f  // sparks/sec shed off the ribbon, x0.18 s life
// Guide §3: the sheet TILES along the trail rather than stretching over it, so
// texel density stays constant however long the tail is, and it SCROLLS so the
// fibres flow. Metres per tile, and tiles per second.
//
// 2.1, bracketed from both sides by the owner: 1.3 was "it just sits there" and
// 3.6 was "too fast to judge". A 3 m tail is about five and a half tiles across,
// so a fibre now crosses the ribbon in roughly two and a half seconds. The
// per-pass spread (0.5 / 1.05 / 1.6) matters as much as the speed: layers moving
// at nearly the same rate read as one thick stroke however many of them there
// are, and the PARALLAX between them is the effect.
// How hard a node is held to the path it was laid on, and the absolute ceiling
// on how far it may stray. Deviation settles near force/spring: with the cloth
// forces below that is a few centimetres of flutter.
#define SWEPT_HOME_SPRING    9.0f
#define SWEPT_HOME_MAX_DEV   0.30f   // metres, ACROSS the path only
// How far along the path a node may stray, as a fraction of its spacing to the
// next node. Must be < 0.5: both ends of a segment move, so 2 x this is the
// most the gap can close, and anything >= 0.5 lets neighbours swap places —
// which is a FOLD, and a distance constraint cannot undo one.
#define SWEPT_ORDER_FRAC     0.45f
#define SWEPT_FLOW_TILE      1.10f
#define SWEPT_FLOW_SPEED     2.10f

// Trails are tagged so a handle can be VALIDATED rather than trusted. Trail ids
// are recycled and the pool evicts by priority, so "our" id can silently become
// somebody else's entity — writing thickness/normal into that would corrupt an
// unrelated effect. With the tag, an evicted strand is simply respawned on the
// next sample, which makes eviction self-healing instead of fatal.
#define SWEPT_TAG_BASE     0x57540000   /* 'WT' */

typedef struct {
    bool  active;
    const Matrix *xf;             // caller-owned; must outlive the handle
    VC_MaterialId matId;
    VFX_TrailStyle style;
    float width;                  // full width in metres, before the aspect cap
    float widthTarget;            // 0..1, set by VFX_TrailSetWidth
    float widthLevel;             // what is actually drawn — ramps toward target
    int   maxNodes;               // tail memory, in samples

    // The ribbon's nodes. NOT a recording of where the tip went — see
    // SweptTrail_Simulate. Newest at `head`, and only the newest is pinned.
    Vector3 ring[SWEPT_RING];
    Vector3 nvel[SWEPT_RING];     // per-node velocity: the inertia that makes it lag
    float   nrest[SWEPT_RING];    // spacing at which each node was laid, in metres
    Vector3 nhome[SWEPT_RING];    // where the node was LAID — the swept path itself
    int   head;
    int   filled;

    Vector3 prevTip;              // last frame's tip, for sub-frame interpolation
    bool  hasPrevTip;
    Vector3 normal;               // the swing plane's normal (BLADE's fixedNormal)
    bool  hasNormal;
    Vector3 lateralAxis;          // `normal` with a CONTINUOUS sign (FILAMENT spread)
    bool  hasLateral;
    float sampleAcc;
    float sparkAcc;
    float elapsed;   // seconds since spawn — drives the flow scroll and the wobble
} VC_SweptTrail;

static VC_SweptTrail s_swept[SWEPT_MAX];
static int  s_sweptNextSerial = 0;
static bool s_sweptInit = false;
static Texture2D s_sweptBladeTex = {0};   // fibre sheet — body + core passes
static Texture2D s_sweptHaloTex  = {0};   // the SAME band with NO fibres — glow pass

// Indexed by VFX_TrailStyle. Zero-initialised as file-scope statics, which is
// what FloatCurve_AddStop expects (count 0 = no stops yet).
// The air the ribbon hangs in — an authored ForceField per style rather than
// hand-written sin() terms. The owner's point, 29/07: the trail system already
// applies force fields per node, and `core/force_field.h` already has curl noise
// (divergence-free, which is precisely the swirling a cloth sits in), wind,
// gravity and drag. Writing that arithmetic again inside a composition is both
// duplication and a violation of the project's own rule that force fields come
// from the force layer.
//
// Per style, because "how cloth-like is it" IS the difference between a sword
// trail and a silk ribbon: a struck blade trail barely sags and settles fast, a
// silk one is carried by the air and keeps moving after the hand has stopped.
static ForceField s_sweptCloth[3];

// The dust's twinkle: three beats inside one life, so a mote pulses instead of
// fading flat. FLOAT_CURVE_MAX_STOPS is 8, which is exactly three peaks.
static SkillCurve s_sweptTwinkle;

static SkillCurve s_sweptWidthCurve[3];
static SkillCurve s_sweptAlphaCurve[3];

// Live-tunable: every one of these is a look decision, and the alternative to a
// tunable is a rebuild per guess (core/CLAUDE.md §5).
static float s_sweptWidthMul  = 1.0f;   // x on the caller's width
static float s_sweptAspectMul = 1.0f;   // x on the aspect cap (the 1:20 rule)
static float s_sweptLagMul    = 1.0f;   // x on the FILAMENT lag step
// The trail system draws BLADE as TWO concentric strips — a coloured outer one
// and a pure-white core at 0.4x width whose alpha ignores the alpha curve
// entirely (trail_system.c:961). Additive plus bloom, that core can read as a
// separate bright thread INSIDE the band, which is one of the two ways a single
// trail can look like several. Set swept_core = 0 to answer that by eye without
// a rebuild rather than by guessing.
// DEFAULT OFF, changed 29/07 — the last structural difference between BLADE and
// the two styles that never dashed.
//
// The trail system draws a FOLLOWER as two concentric strips, and the inner one
// is 0.4/1.5 = 0.267x the outer half-width, drawn pure white at alpha 255 with
// the alpha curve ignored entirely (trail_system.c:961). So it is ~4x thinner
// and ~1.5x brighter than the band around it: whatever the band is doing, the
// CORE is what the eye actually follows, and it goes sub-pixel four times sooner.
//
// The distance gate added earlier did not save it, and the reason is arithmetic:
// the gate measures the band at the HEAD, once per frame, while the width
// envelope tapers the band along its length. At segRatio 0.4 the taper is ~0.6,
// so a band the gate measured at 9 px is really 5.4 px there and the core is
// 1.4 px — dashes, with the gate reporting everything fine.
//
// RIBBON and FILAMENT both set disableInnerCore = true and neither has ever
// dashed. The hot line BLADE wanted from the core is already in its own mask
// (the rim), so this is a layer that was costing an artefact to duplicate
// something the sheet already had. Set swept_core = 1 to put it back.
static float s_sweptCore      = 0.0f;
static float s_sweptSpread    = 2.2f;   // x on halfW, FILAMENT strand separation
static float s_sweptMinPx     = 1.0f;   // x on the screen-space width floor
static float s_sweptSpark     = 1.0f;   // x on the sparkle rate, 0 = none
static float s_sweptAlphaMul  = 1.0f;   // x on the whole trail's opacity
static float s_sweptFlow      = 1.0f;   // x on the UV scroll speed, 0 = frozen
static float s_sweptSag       = 1.0f;   // x on how much the ribbon sags
static float s_sweptWind      = 1.0f;   // x on the air that moves it, 0 = still
static float s_sweptCoreHot   = 1.0f;   // x on the white-hot head
static float s_sweptUVLog     = 0.0f;   // 1 = report what the flow UV is doing, 1 Hz
// DIAGNOSTIC DIALS, both default OFF. Two rounds were spent on this artefact
// because each candidate cause could only be tested by a rebuild. These make the
// next observation decisive instead: set one, look, and the answer is a fact.
//   swept_blade_flat = 1 -> BLADE uses the CENTRE-weighted global sheet. Still
//       dashed? then the mask is not the cause and the geometry is.
//   swept_camfacing  = 1 -> BLADE drops RIBBON_FIXED_NORMAL for camera-facing,
//       which is the only other thing that separates it from the two styles that
//       never broke up. Fixes it? then the swing plane is the cause.
static float s_sweptBladeFlat = 0.0f;
static float s_sweptCamFacing = 0.0f;

// Where each FILAMENT strand sits across the bundle, in units of half-width.
// DELIBERATELY IRREGULAR. Evenly spaced strands of similar width read as a comb
// — four parallel wires, which is what the owner's 29/07 capture shows — and no
// amount of colour fixes it, because the regularity is the thing the eye picks
// up. Uneven spacing plus the width falloff in SweptTrail_StrandLook is what
// turns four wires into a bundle of threads. Pinned by swept_trail_test.c.
static const float k_sweptSpread[SWEPT_STRANDS_MAX] = { -1.00f, -0.50f, 0.38f, 1.00f };

// ── The arithmetic, factored out so core/tests/swept_trail_test.c can mirror it ─

// Half-width allowed per metre the tip has travelled. THE RATIO IS THE POINT:
// a band's aspect against its OWN length is what decides whether it reads as a
// blade, a cloth or a thread — full width : travelled length is 1:20, 1:10 and
// 1:40 here (core/docs/LANDMINES.md, "Thickness is a ratio against the thing's
// OWN length"; the 1:20 blade figure is the one VFX_ComposeSweepSlash landed on).
// Half-width is half of that, hence 0.025 / 0.05 / 0.0125.
static float SweptTrail_AspectK(VFX_TrailStyle style)
{
    switch (style) {
    case VFX_TRAIL_RIBBON:   return 0.0715f;   // 1:7 — cloth, and cloth is BROAD
    case VFX_TRAIL_FILAMENT: return 0.0125f;   // 1:40 — thread
    case VFX_TRAIL_BLADE:
    default:                 return 0.0250f;   // 1:20 — blade
    }
}

// The caller's width is a CEILING, not a value. Below the speed at which the
// requested width is in proportion, the travelled length wins — which is the
// whole fix for the classic failure this DoD names: on a hard turn the tail
// shortens, and a band that keeps its width through that becomes a blob.
static float SweptTrail_HalfWidth(float widthMetres, float level01,
                                  float travelLen, VFX_TrailStyle style)
{
    float want = widthMetres * 0.5f * level01;
    float cap  = travelLen * SweptTrail_AspectK(style) * s_sweptAspectMul;
    if (want < 0.0f) want = 0.0f;
    return (cap < want) ? cap : want;
}

// Strand `i` reads the tip path delayed by i * SWEPT_LAG_STEP SECONDS. Expressed
// in samples here and nowhere else, so the lag cannot quietly become a frame
// count on a machine running at a different rate.
static int SweptTrail_LagSamples(int strand)
{
    float lag = (float)strand * SWEPT_LAG_STEP * s_sweptLagMul;
    int   n   = (int)(lag / SWEPT_SAMPLE_DT + 0.5f);
    if (n < 0) n = 0;
    if (n > SWEPT_RING - 2) n = SWEPT_RING - 2;
    return n;
}

// ── The screen-space floor — why a correct width still renders as dashes ─────
//
// The blade came out DOTTED, and the giveaway was that it depended on ZOOM:
// close in it was solid except at the tail, far out it broke up along its whole
// length. That is not the mask (which was the first suspect, and replacing it
// changed nothing) — it is the band being THINNER THAN A PIXEL. A strip under
// ~1 px wide is rasterised where its centre happens to land inside a pixel and
// dropped where it does not, which draws exactly a row of dashes. The tail goes
// first at any zoom, because the width envelope takes it to zero.
//
// The fix is the standard hair/thin-line one, and it is a FLOOR PLUS A FADE, not
// just a floor: below the minimum, hold the width at the minimum and scale alpha
// by how much narrower it should have been. The band then keeps roughly the
// light it ought to emit, and a trail too far away to resolve fades out instead
// of disintegrating. A bare floor would do the opposite — a distant trail would
// grow into a fat opaque worm.
#define SWEPT_MIN_PIXELS      2.0f   // full width, below which the strip breaks up
#define SWEPT_CORE_MIN_PIXELS 5.0f   // the inner core is 0.27x the outer half-width

// Pixels per metre at `dist` under a vertical-FOV perspective camera.
static float SweptTrail_PixelsPerMetre(float dist, float fovyDeg, float screenH)
{
    if (dist < 0.01f) dist = 0.01f;
    float halfFov = fovyDeg * 0.5f * DEG2RAD;
    float t = tanf(halfFov);
    if (t < 1e-4f) t = 1e-4f;
    return screenH / (2.0f * dist * t);
}

// Returns the half-width to actually draw and writes the alpha compensation.
// Split out with no raylib types in the signature so the headless test can
// mirror it exactly (core/tests/swept_trail_test.c).
static float SweptTrail_ScreenFloor(float halfW, float pxPerMetre, float minFullPx,
                                    float *outAlphaScale)
{
    *outAlphaScale = 1.0f;
    if (pxPerMetre <= 0.0f || minFullPx <= 0.0f) return halfW;
    float minHalf = (minFullPx * 0.5f) / pxPerMetre;
    if (halfW >= minHalf) return halfW;
    *outAlphaScale = (halfW > 0.0f) ? (halfW / minHalf) : 0.0f;
    return minHalf;
}

// How much the ribbon behaves like cloth. BLADE is nearly a record (a sword's
// trail is struck, not draped); RIBBON is silk and is allowed to sag, lag and
// overshoot; FILAMENT sits between.
static float SweptTrail_Sag(VFX_TrailStyle style)
{
    switch (style) {
    case VFX_TRAIL_RIBBON:   return 2.60f;   // m/s^2 downward
    case VFX_TRAIL_FILAMENT: return 0.90f;
    default:                 return 0.70f;
    }
}
static float SweptTrail_Drag(VFX_TrailStyle style)
{
    // Higher = the node settles faster = stiffer. Low drag is what lets the tail
    // keep travelling after the head has stopped.
    switch (style) {
    case VFX_TRAIL_RIBBON:   return 1.9f;
    case VFX_TRAIL_FILAMENT: return 3.0f;
    default:                 return 5.0f;
    }
}
static float SweptTrail_Wind(VFX_TrailStyle style)
{
    switch (style) {
    case VFX_TRAIL_RIBBON:   return 1.70f;
    case VFX_TRAIL_FILAMENT: return 1.10f;
    default:                 return 0.55f;
    }
}

static int SweptTrail_StrandCount(VFX_TrailStyle style, bool lowTier)
{
    if (style != VFX_TRAIL_FILAMENT) return 1;
    // E8 tier budget: each strand is its own ribbon submission. The gate only
    // ever clamps DOWN — a low tier loses threads, never gains them.
    return lowTier ? 2 : SWEPT_STRANDS_MAX;
}

// Tail memory in seconds → nodes. The ceiling is TRAIL_HISTORY_COUNT (60), which
// at 60 Hz is exactly 1.0 s of history; asking for more silently gets 1.0 s.
static int SweptTrail_MaxNodes(float lifetime)
{
    int n = (int)(lifetime * SWEPT_SAMPLE_HZ + 0.5f);
    if (n < 4) n = 4;
    if (n > TRAIL_HISTORY_COUNT) n = TRAIL_HISTORY_COUNT;
    return n;
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
    float d = fabsf(u - 0.5f) * 2.0f;      // 0 at the centre line, 1 at the edges
    float a = 1.0f - d * d;
    if (a < 0.0f) a = 0.0f;
    return powf(a, 1.35f);                 // soft shoulders, no hard rim
}

static void SweptTrail_BuildBladeMask(void)
{
    const int W = 128, H = 256;            // H carries the streaks; must be seamless
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
        0.045f,0.035f,0.050f,0.040f,0.030f,0.055f,0.038f,0.032f,
        0.048f,0.042f,0.036f,0.052f,0.034f,0.044f,0.030f,0.028f};
    static const float st_amp[SWEPT_STREAKS] = {
        1.00f, 0.72f, 0.66f, 0.90f, 0.55f, 0.60f, 0.85f, 0.62f,
        0.48f, 0.95f, 0.58f, 0.68f, 0.74f, 0.50f, 0.44f, 0.52f};
    static const float st_slant[SWEPT_STREAKS] = {
        0.05f,-0.04f, 0.06f,-0.07f, 0.03f,-0.05f, 0.06f,-0.03f,
        0.05f,-0.06f, 0.04f,-0.05f, 0.03f,-0.04f, 0.05f,-0.03f};

    Image img  = GenImageColor(W, H, BLANK);
    Image halo = GenImageColor(W, H, BLANK);
    for (int y = 0; y < H; y++) {
        float v = ((float)y + 0.5f) / (float)H;
        for (int x = 0; x < W; x++) {
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
            for (int f = 0; f < SWEPT_STREAKS; f++) {
                // CIRCULAR distance along v. The sheet is tiled and scrolled, so
                // a streak whose middle sits near v = 0 has to reach round to
                // v = 1 or the wrap shows as a seam travelling down the trail
                // ("harsh edges and tiling textures", the guide's mistake list).
                // Wrapping the distance is what makes the finite envelope legal
                // here at all — the old lanes bought seamlessness with integer
                // periods, which is exactly what forced them to be continuous.
                float dv = v - st_v[f];
                dv -= floorf(dv + 0.5f);              // into [-0.5, 0.5)
                float t = dv / st_len[f];
                if (t <= -1.0f || t >= 1.0f) continue;
                // Raised cosine: zero VALUE and zero SLOPE at both ends, so a
                // streak fades in and out instead of switching on at a hard edge.
                float env = 0.5f * (1.0f + cosf(PI * t));
                float c = st_u[f] + st_slant[f] * t;
                float d = (u - c) / st_wide[f];
                a += st_amp[f] * expf(-d * d) * env * base * base;
            }
            a = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
            ImageDrawPixel(&img, x, y, (Color){255, 255, 255,
                                               (unsigned char)(a * 255.0f)});

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
            ImageDrawPixel(&halo, x, y, (Color){255, 255, 255,
                                                (unsigned char)(h * 255.0f)});
        }
    }
    s_sweptBladeTex = LoadTextureFromImage(img);
    s_sweptHaloTex  = LoadTextureFromImage(halo);
    UnloadImage(img);
    UnloadImage(halo);
    if (s_sweptHaloTex.id != 0) {
        SetTextureFilter(s_sweptHaloTex, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(s_sweptHaloTex, TEXTURE_WRAP_REPEAT);
    }
    if (s_sweptBladeTex.id != 0) {
        SetTextureFilter(s_sweptBladeTex, TEXTURE_FILTER_BILINEAR);
        // REPEAT, not CLAMP: the sheet is TILED along the trail and scrolled, so
        // it must wrap. Harmless across u because the profile is zero at both
        // edges.
        SetTextureWrap(s_sweptBladeTex, TEXTURE_WRAP_REPEAT);
    }
}

static void SweptTrail_InitShared(void)
{
    if (s_sweptInit) return;

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
    // perturbation — see the home spring in SweptTrail_Simulate, which is what
    // actually bounds the deviation.
    static const float sag[3]  = {0.40f, 0.95f, 0.55f};   // BLADE, RIBBON, FILAMENT
    static const float curl[3] = {0.30f, 0.55f, 0.40f};
    static const float drag[3] = {5.50f, 3.40f, 4.20f};
    for (int st = 0; st < 3; st++) {
        ForceField_AddLayer(&s_sweptCloth[st], (ForceLayer){
            .type = FORCE_GRAVITY_DIR,
            .direction = {0.0f, -1.0f, 0.0f},
            .strength = sag[st],
        });
        ForceField_AddLayer(&s_sweptCloth[st], (ForceLayer){
            .type = FORCE_NOISE_CURL,
            .strength = curl[st],
            .noiseScale = 0.95f,  // finer eddies: a flutter, not a swing
            .noiseSpeed = 0.45f,  // the air itself moves, slowly
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
    FloatCurve_AddStop(&s_sweptWidthCurve[VFX_TRAIL_BLADE], 0.00f, 0.00f);
    FloatCurve_AddStop(&s_sweptWidthCurve[VFX_TRAIL_BLADE], 0.25f, 0.55f);
    FloatCurve_AddStop(&s_sweptWidthCurve[VFX_TRAIL_BLADE], 0.60f, 1.00f);
    FloatCurve_AddStop(&s_sweptWidthCurve[VFX_TRAIL_BLADE], 0.88f, 0.72f);
    FloatCurve_AddStop(&s_sweptWidthCurve[VFX_TRAIL_BLADE], 1.00f, 0.18f);

    FloatCurve_AddStop(&s_sweptWidthCurve[VFX_TRAIL_RIBBON], 0.00f, 0.00f);
    FloatCurve_AddStop(&s_sweptWidthCurve[VFX_TRAIL_RIBBON], 0.30f, 0.70f);
    FloatCurve_AddStop(&s_sweptWidthCurve[VFX_TRAIL_RIBBON], 0.62f, 1.00f);
    FloatCurve_AddStop(&s_sweptWidthCurve[VFX_TRAIL_RIBBON], 0.90f, 0.78f);
    FloatCurve_AddStop(&s_sweptWidthCurve[VFX_TRAIL_RIBBON], 1.00f, 0.22f);

    FloatCurve_AddStop(&s_sweptWidthCurve[VFX_TRAIL_FILAMENT], 0.00f, 0.00f);
    FloatCurve_AddStop(&s_sweptWidthCurve[VFX_TRAIL_FILAMENT], 0.22f, 0.78f);
    FloatCurve_AddStop(&s_sweptWidthCurve[VFX_TRAIL_FILAMENT], 0.85f, 1.00f);
    FloatCurve_AddStop(&s_sweptWidthCurve[VFX_TRAIL_FILAMENT], 1.00f, 0.20f);

    // Brightness rides toward the head, and — the rule that is not taste — the
    // tail's alpha must fall at least as fast as its width, or the last stretch
    // is sub-pixel while still visible and breaks into dashes (LANDMINES 29/07).
    FloatCurve_AddStop(&s_sweptAlphaCurve[VFX_TRAIL_BLADE], 0.00f, 0.00f);
    FloatCurve_AddStop(&s_sweptAlphaCurve[VFX_TRAIL_BLADE], 0.25f, 0.32f);
    FloatCurve_AddStop(&s_sweptAlphaCurve[VFX_TRAIL_BLADE], 0.70f, 0.82f);
    FloatCurve_AddStop(&s_sweptAlphaCurve[VFX_TRAIL_BLADE], 1.00f, 1.00f);

    FloatCurve_AddStop(&s_sweptAlphaCurve[VFX_TRAIL_RIBBON], 0.00f, 0.00f);
    FloatCurve_AddStop(&s_sweptAlphaCurve[VFX_TRAIL_RIBBON], 0.30f, 0.55f);
    FloatCurve_AddStop(&s_sweptAlphaCurve[VFX_TRAIL_RIBBON], 1.00f, 1.00f);

    FloatCurve_AddStop(&s_sweptAlphaCurve[VFX_TRAIL_FILAMENT], 0.00f, 0.00f);
    FloatCurve_AddStop(&s_sweptAlphaCurve[VFX_TRAIL_FILAMENT], 0.25f, 0.70f);
    FloatCurve_AddStop(&s_sweptAlphaCurve[VFX_TRAIL_FILAMENT], 1.00f, 1.00f);

    // Lazily, never from a subsystem Init — Tuning_Init runs after those and an
    // early registration silently keeps the default (core/docs/LANDMINES.md).
    Tuning_RegisterFloat("swept_width",  &s_sweptWidthMul,  1.0f);
    Tuning_RegisterFloat("swept_aspect", &s_sweptAspectMul, 1.0f);
    Tuning_RegisterFloat("swept_lag",    &s_sweptLagMul,    1.0f);
    Tuning_RegisterFloat("swept_core",   &s_sweptCore,      0.0f);
    Tuning_RegisterFloat("swept_spread", &s_sweptSpread,    2.2f);
    Tuning_RegisterFloat("swept_minpx",  &s_sweptMinPx,     1.0f);
    Tuning_RegisterFloat("swept_spark",  &s_sweptSpark,     1.0f);
    Tuning_RegisterFloat("swept_alpha",  &s_sweptAlphaMul,  1.0f);
    Tuning_RegisterFloat("swept_flow",   &s_sweptFlow,      1.0f);
    Tuning_RegisterFloat("swept_sag",    &s_sweptSag,       1.0f);
    Tuning_RegisterFloat("swept_wind",   &s_sweptWind,      1.0f);
    Tuning_RegisterFloat("swept_corehot",&s_sweptCoreHot,   1.0f);
    Tuning_RegisterFloat("swept_uvlog",  &s_sweptUVLog,     0.0f);
    Tuning_RegisterFloat("swept_blade_flat", &s_sweptBladeFlat, 0.0f);
    Tuning_RegisterFloat("swept_camfacing",  &s_sweptCamFacing, 0.0f);

    s_sweptInit = true;
}
// ── The layered draw ────────────────────────────────────────────────────────
//
// WHY THIS FILE DRAWS THE RIBBON ITSELF, having spent its first version driving
// TRAIL_TYPE_FOLLOWER entities. The trail system's value is history plus node
// physics; this composition samples its OWN history ring (it has to, for the
// fixed-rate clock and the lag schedule) and uses no node physics at all, so
// the entities were carrying nothing but their draw — and that draw is a fixed
// two strips whose inner one is 0.267x as wide, pure white, and ignores the
// alpha curve. Three of the artefacts chased this week came out of it.
//
// WHAT MAKES A TRAIL READ AS BEAUTIFUL, from the owner's own reference sheet
// (29/07): every trail on it is FOUR LAYERS — a wide soft glow, a hot near-white
// core, a taper to nothing, and sparkle points along the way. A single strip
// with a mask is a line, however good the mask is, and that is exactly what ours
// looked like.
//
// The pass ratios are NOT invented here: they are VFX_ComposeSweepSlash's, which
// is the one multi-pass effect in the tree the owner has not objected to. Same
// reason a ribbon goes through the immediate-mode path with no emissiveBoost —
// its vertex colour is 8-bit and caps at 1.0, so drawing it again is the only
// way plain geometry gets past the bloom threshold.
// 1.55, not the 2.6 this shipped with. The halo is a MULTIPLIER on a width that
// is itself earned from the path length, so on a 6 m ribbon 2.6x meant a band
// over two metres across — and additive light that wide, through E1's bloom,
// stops being a glow behind the ribbon and becomes a coarse slab around it
// (owner, 29/07: "a big rough bloom ring"). VFX_ComposeSweepSlash uses 1.5 for
// the same job and does not do this.
static const float k_sweptPassW[3]     = {1.55f, 1.00f, 0.26f}; // x half-width
static const float k_sweptPassA[3]     = {0.16f, 0.85f, 1.00f}; // x alpha
static const float k_sweptPassWhite[3] = {0.00f, 0.18f, 1.00f}; // toward white
// CONTRAST, and it is the reason the trail read as "even, no blazing spot".
// All three passes used the SAME alpha profile, so every layer was bright over
// the same stretch and the result averaged into one flat luminance. The core is
// now concentrated at the HEAD — pow(t, HEAD) — so there is a small white-hot
// point with saturated element colour trailing behind it, which is what every
// image on the reference sheet does. The glow, being the layer that carries the
// hue, is deliberately NOT whitened at all.
#define SWEPT_CORE_HEAD_POW  2.6f
// ...and the tail keeps its colour instead of washing out: the mid layer is only
// lightly whitened now (0.18, was 0.30). Whitening is what kills saturation, and
// a desaturated additive band is exactly the "even, no bright spot" look.

// ── Public API ───────────────────────────────────────────────────────────────

int VFX_ComposeSweptTrail(const Matrix *followTransform, VC_MaterialId mat,
                          float width, float lifetime, VFX_TrailStyle style)
{
    SweptTrail_InitShared();
    if (!followTransform) {
        TraceLog(LOG_WARNING, "VFX_SWEPT: NULL transform — no trail created");
        return -1;
    }
    if (style < VFX_TRAIL_BLADE || style > VFX_TRAIL_FILAMENT) style = VFX_TRAIL_BLADE;
    if (width <= 0.0f) width = 0.22f;

    int slot = -1;
    for (int i = 0; i < SWEPT_MAX; i++) {
        if (!s_swept[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        // Announced: a trail that never appears and a trail that was never
        // requested look identical on screen (core/CLAUDE.md §4).
        slot = s_sweptNextSerial % SWEPT_MAX;
        TraceLog(LOG_WARNING, "VFX_SWEPT: pool full (%d) — recycling slot %d",
                 SWEPT_MAX, slot);
    }
    s_sweptNextSerial++;

    VC_SweptTrail *s = &s_swept[slot];
    Vector3 tip = Vector3Transform((Vector3){0.0f, 0.0f, 0.0f}, *followTransform);

    s->active      = true;
    s->xf          = followTransform;
    s->matId       = mat;
    s->style       = style;
    s->width       = width;
    s->widthTarget = 1.0f;
    s->widthLevel  = 1.0f;
    s->maxNodes    = SweptTrail_MaxNodes(lifetime);
    s->head        = 0;
    s->filled      = 1;
    s->ring[0]     = tip;
    // SEED THE ANCHOR TOO. Missing this left nhome[0] at {0,0,0} — the world
    // origin — and the home spring then dragged the trail's first node toward
    // the MAP CENTRE, which is the "it appears in the middle of the map and then
    // snaps to the spawn point" the owner saw. Any state that shadows `ring`
    // has to be seeded on the same line as `ring`, not only in Push().
    s->nhome[0]    = tip;
    s->nvel[0]     = (Vector3){0.0f, 0.0f, 0.0f};
    s->nrest[0]    = 1e-4f;
    s->prevTip     = tip;
    s->hasPrevTip  = true;
    s->normal      = (Vector3){0.0f, 1.0f, 0.0f};
    s->hasNormal   = false;
    s->lateralAxis = (Vector3){0.0f, 1.0f, 0.0f};
    s->hasLateral  = false;
    s->sampleAcc   = 0.0f;
    s->sparkAcc    = 0.0f;
    s->elapsed     = 0.0f;
    return slot;
}

void VFX_TrailSetWidth(int handle, float width01)
{
    if (handle < 0 || handle >= SWEPT_MAX || !s_swept[handle].active) return;
    if (width01 < 0.0f) width01 = 0.0f;
    if (width01 > 1.0f) width01 = 1.0f;
    // Only the TARGET moves; Update ramps toward it. Assigning straight through
    // is what makes a wind-down pop.
    s_swept[handle].widthTarget = width01;
}

void VFX_KillSweptTrail(int handle)
{
    if (handle < 0 || handle >= SWEPT_MAX) return;
    // Deliberately does NOT KillTrail the strands. Cutting a ribbon out of
    // existence mid-swing is a pop; releasing the slot stops the feed, and the
    // strands then drain their own history and fade over SWEPT_FADE_TIME, which
    // is the wind-down the caller wanted. Nothing dangles: the entities never
    // held a pointer to the caller's Matrix (see the header comment).
    s_swept[handle].active = false;
}

// ── Per-frame ────────────────────────────────────────────────────────────────

// Metres of tip path currently inside the tail window. Walked in full rather
// than kept as a running sum: the window is at most 59 segments and this runs at
// most 8 times per sample, which is nothing, and an incremental sum would drift.
static float SweptTrail_TravelLength(const VC_SweptTrail *s)
{
    int segs = s->filled - 1;
    if (segs > s->maxNodes - 1) segs = s->maxNodes - 1;
    if (segs < 1) return 0.0f;
    float total = 0.0f;
    int i = s->head;
    for (int n = 0; n < segs; n++) {
        int prev = (i - 1 + SWEPT_RING) % SWEPT_RING;
        total += Vector3Distance(s->ring[i], s->ring[prev]);
        i = prev;
    }
    return total;
}

// The swing plane. Three samples spanning the tail give two chords; their cross
// product is along the axis the tip is turning about.
//
// WHICH SIDE OF THE STRIP THIS PUTS u = 0 ON, and it is not a matter of taste
// (core/docs/LANDMINES.md, "A masked ribbon: which side of the strip is u = 0").
// DrawRibbonStripEx emits u = 0 at center + side*halfWidth with
// side = normalize(cross(tangent, normal)). For a tip turning at angular
// velocity w about a radius r, tangent = w x r and normal here is along w, so
//     side = (w x r) x ŵ = r(w·ŵ) - w(r·ŵ) = |w| r     for r ⊥ w,
// i.e. side points radially OUTWARD — u = 0 is the outer edge of the swing, the
// line the blade itself traced. That is exactly the edge the reused SweepSlash
// mask puts its hot rim on, so the two agree without a flip. Asserted
// numerically in core/tests/swept_trail_test.c.
//
// Sign: cross(chord1, chord2) already carries the turn's handedness, so it is
// used unflipped. At an inflection (the crossing of a figure-eight) the plane
// genuinely reverses; the normal is SNAPPED there rather than interpolated,
// because lerping across a sign change passes through a degenerate normal and
// the strip would collapse for a few frames. Curvature is ~0 at an inflection,
// so the snap is invisible.
static void SweptTrail_UpdateNormal(VC_SweptTrail *s)
{
    int span = s->filled - 1;
    if (span > s->maxNodes - 1) span = s->maxNodes - 1;
    if (span < 2) return;
    int half = span / 2;
    Vector3 c = s->ring[s->head];
    Vector3 b = s->ring[(s->head - half + SWEPT_RING) % SWEPT_RING];
    Vector3 a = s->ring[(s->head - span + SWEPT_RING) % SWEPT_RING];
    Vector3 n = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, b));
    if (Vector3LengthSqr(n) < 1e-10f) return;      // straight line: keep the last plane
    n = Vector3Normalize(n);
    if (!s->hasNormal || Vector3DotProduct(n, s->normal) < 0.0f) {
        s->normal = n;                              // first sample, or an inflection
        s->hasNormal = true;
    } else {
        s->normal = Vector3Normalize(Vector3Lerp(s->normal, n, 0.25f));
    }

    // The SAME axis, sign-stabilised, and the two must not be conflated.
    //
    // `normal` above carries the true handedness of the turn, which is what the
    // BLADE's masked strip needs (it decides which side u = 0 lands on). But
    // FILAMENT uses this axis for something else entirely — to spread its
    // strands sideways — and there the sign flip at an inflection is a BUG: the
    // offsets jump from +o to -o in one frame, so every strand's newest node
    // teleports across the bundle and the threads knot together at the head.
    // That knot is visible in the owner's capture (29/07). Flipping to keep the
    // sign continuous costs nothing here and cannot affect BLADE, whose ribbon
    // is drawn from `normal`.
    Vector3 lat = s->normal;
    if (s->hasLateral && Vector3DotProduct(lat, s->lateralAxis) < 0.0f)
        lat = Vector3Negate(lat);
    s->lateralAxis = lat;
    s->hasLateral  = true;
}

// Cut the trail dead and restart it at `tip`. Used for a teleport, where the
// alternative is a straight streak bridging two places the weapon never swept.
// The strands are hard-killed here — unlike VFX_KillSweptTrail, which fades them
// on purpose: a fading bridge is still a bridge.
// Cut the trail dead and restart it at `tip`. Used for a teleport, where the
// alternative is a straight streak bridging two places the weapon never swept.
static void SweptTrail_Cut(VC_SweptTrail *s, int slot, Vector3 tip)
{
    (void)slot;
    s->head       = 0;
    s->filled     = 1;
    s->ring[0]    = tip;
    s->nhome[0]   = tip;      // see VFX_ComposeSweptTrail: seed the anchor too
    s->nvel[0]    = (Vector3){0.0f, 0.0f, 0.0f};
    s->nrest[0]   = 1e-4f;
    s->prevTip    = tip;
    s->hasNormal  = false;
    s->hasLateral = false;
    s->sampleAcc  = 0.0f;
}

static void SweptTrail_Push(VC_SweptTrail *s, Vector3 p, Vector3 tipVel)
{
    int prev = s->head;
    s->head = (s->head + 1) % SWEPT_RING;
    s->ring[s->head]  = p;
    // A node is born MOVING, carrying the tip's velocity. Born at rest it would
    // be dropped behind the emitter with no momentum, and the ribbon would form
    // by falling rather than by being flung.
    s->nvel[s->head]  = tipVel;
    s->nhome[s->head] = p;
    s->nrest[s->head] = (s->filled > 0) ? Vector3Distance(p, s->ring[prev]) : 0.0f;
    if (s->nrest[s->head] < 1e-4f) s->nrest[s->head] = 1e-4f;
    if (s->filled < SWEPT_RING) s->filled++;
}

// ── The ribbon is SIMULATED, not recorded ───────────────────────────────────
//
// The owner's words, 29/07: "like holding a picture and moving it in a circle,
// not a silk ribbon in the wind." That is exactly what a history ring is — every
// node sits precisely where the emitter was, so the shape is a rigid record being
// translated, and no amount of texture makes rigid geometry read as cloth. The
// sine displacement the previous version added made it worse for the same
// reason: a fixed pattern riding along is still a picture.
//
// What silk does is LAG. Each node carries momentum, is dragged by the air,
// sags, and is pulled back toward the node ahead of it by the cloth's own
// inextensibility — so the tail keeps travelling after the head has stopped and
// overshoots when the head turns. That is a follow-the-leader chain, and it is
// cheap: integrate, then satisfy a distance constraint against the node NEARER
// THE HEAD, twice.
//
// Only the head is pinned. Everything behind it is free, which is the whole
// difference between a ribbon and a decal of one.
static void SweptTrail_Simulate(VC_SweptTrail *s, float dt)
{
    int n = s->filled;
    if (n > s->maxNodes) n = s->maxNodes;
    if (n < 3) return;

    const ForceField *fld = &s_sweptCloth[s->style];
    const float time = (float)GetTime();

    // Integrate. k = 0 is the head and is left alone — it belongs to the emitter.
    for (int k = 1; k < n; k++) {
        int idx = (s->head - k + SWEPT_RING) % SWEPT_RING;
        Vector3 acc = ForceField_Evaluate(fld, s->ring[idx], s->nvel[idx], time,
                                          (Vector3){0}, (Vector3){0});
        // THE ANCHOR. A node is sprung back toward where it was LAID — the swept
        // path — so the air and the sag move it a few centimetres instead of
        // carrying it away. Without this the chain is free-floating and writhes;
        // with it the deviation settles at roughly force/spring, which is the
        // difference between silk fluttering and a snake being swung.
        Vector3 pull = Vector3Subtract(s->nhome[idx], s->ring[idx]);
        acc = Vector3Add(acc, Vector3Scale(pull, SWEPT_HOME_SPRING));
        // Deeper nodes feel it more — the far end of a ribbon is the loose end,
        // and a uniform response reads as the whole sheet swinging as one piece.
        float depth = (float)k / (float)(n - 1);
        acc = Vector3Scale(acc, (0.25f + 0.75f * depth) * s_sweptSag);
        s->nvel[idx] = Vector3Add(s->nvel[idx], Vector3Scale(acc, dt));
        s->ring[idx] = Vector3Add(s->ring[idx], Vector3Scale(s->nvel[idx], dt));

        // Hard ceiling on the deviation, SPLIT INTO TWO BOUNDS — and the split
        // is the whole point, not tidiness.
        //
        // THE SELF-TWIST BUG (owner, 29/07: "vẫn bị tự xoắn"). The bound used to
        // be one number, 0.30 m, applied to the deviation as a whole. Do the
        // arithmetic for the bench swing: a 3 m arm at 2.4 rad/s puts the tip at
        // 7.2 m/s, and at the 60 Hz sample clock that lays nodes 0.12 m apart. A
        // 0.30 m bound is therefore TWO AND A HALF TIMES the node spacing — a
        // node was free to travel past its own leader, and the polyline folded
        // back on itself. That fold is what reads as a twist: the strip's side
        // vector reverses across the crossing and the band pinches into a wedge.
        //
        // A distance constraint cannot fix it. Distance is a scalar, so a node
        // that has passed THROUGH its leader is simply "a bit too close" again,
        // and the constraint happily settles it in the reversed order. Ordering
        // has to be protected where it can still be seen — in the direction
        // ALONG the path.
        //
        //   - ALONG the laid path: bounded by a FRACTION of the node spacing, so
        //     two neighbours can approach but provably never swap. This is a
        //     correctness bound.
        //   - ACROSS it: the old loose metre bound. Sag, curl and flutter are
        //     almost entirely lateral, so this is the component the look lives
        //     in and it is left alone — the fix costs no motion.
        int leadI = (s->head - k + 1 + SWEPT_RING) % SWEPT_RING;
        Vector3 off = Vector3Subtract(s->ring[idx], s->nhome[idx]);
        Vector3 seg = Vector3Subtract(s->nhome[leadI], s->nhome[idx]);
        float segLen = Vector3Length(seg);
        if (segLen > 1e-5f) {
            Vector3 dir = Vector3Scale(seg, 1.0f / segLen);
            float along = Vector3DotProduct(off, dir);
            // The neighbour is free to move too, so each end gets less than half
            // the gap between them: 0.45 + 0.45 < 1, and they can meet without
            // ever crossing.
            float spacing = fminf(s->nrest[idx], s->nrest[leadI]);
            float alongMax = SWEPT_ORDER_FRAC * spacing;
            float clamped  = along;
            if (clamped >  alongMax) clamped =  alongMax;
            if (clamped < -alongMax) clamped = -alongMax;
            if (clamped != along)
                off = Vector3Add(off, Vector3Scale(dir, clamped - along));
        }
        float offLen = Vector3Length(off);
        if (offLen > SWEPT_HOME_MAX_DEV)
            off = Vector3Scale(off, SWEPT_HOME_MAX_DEV / offLen);
        Vector3 corrected = Vector3Add(s->nhome[idx], off);
        if (Vector3DistanceSqr(corrected, s->ring[idx]) > 1e-10f) {
            s->ring[idx] = corrected;
            s->nvel[idx] = Vector3Scale(s->nvel[idx], 0.5f);
        }
    }

    // Inextensibility, head-pinned, two passes — the same rope constraint the
    // trail system runs on its WISP strands (`Ribbon_ConstrainSegment`), not a
    // second implementation of it. Stretch only: a node closer than its rest
    // spacing is a ribbon bunching up, which cloth does, and forcing it back out
    // makes it spring.
    for (int pass = 0; pass < 2; pass++) {
        for (int k = 1; k < n; k++) {
            int idx  = (s->head - k + SWEPT_RING) % SWEPT_RING;
            int lead = (s->head - k + 1 + SWEPT_RING) % SWEPT_RING;
            (void)0;
            // A CEILING: the ribbon is inextensible, so pull it back to its
            // rest spacing when it has been stretched past it.
            Ribbon_ConstrainSegment(&s->ring[lead], &s->ring[idx],
                                    s->nrest[idx], true, RIBBON_CONSTRAIN_MAX);
            // ...and a FLOOR. Cloth bunches, but two nodes that collapse onto
            // each other — or pass THROUGH each other — give a zero or REVERSED
            // segment, which flips the strip's side vector and pinches the band
            // into a bowtie. A third of the rest spacing lets it gather without
            // ever degenerating. 0.60 rather than 0.34: the floor also protects
            // the TANGENT, which is a central difference over the neighbours and
            // is fabricated outright when they crowd together (see
            // core/ribbon_strip.c). A third of the rest spacing left far too
            // little room for that.
            //
            // MODE MATTERS HERE MORE THAN THE NUMBER. This second call shipped
            // for one round as `stretchOnly = false`, which is not "also enforce
            // a minimum" but "force the distance to be EXACTLY a third of rest",
            // and it silently overwrote the ceiling above. The whole 6 m ribbon
            // collapsed to a third of its length and read as a short stiff
            // spindle stuck to the head — the owner's "it is stiff, and the
            // energy no longer flows" (the flow had barely any length left to
            // travel along).
            Ribbon_ConstrainSegment(&s->ring[lead], &s->ring[idx],
                                    s->nrest[idx] * 0.60f, true,
                                    RIBBON_CONSTRAIN_MIN);
            // NO velocity feedback from the correction. It was `corr * 0.25/dt`,
            // which at 60 fps is a gain of FIFTEEN on the position error and at
            // 144 fps is thirty-six: a node nudged a centimetre got a 0.15 m/s
            // kick, the next node reacted to that, and the chain rang node-to-node.
            // The liveliness comes from the force field and the home spring, both
            // of which are framerate-independent; a correction gain that divides
            // by dt is not, and it is not needed.
        }
    }
}

static void VC_SweptTrail_Update(float dt)
{
    if (dt <= 0.0f) return;
    for (int i = 0; i < SWEPT_MAX; i++) {
        VC_SweptTrail *s = &s_swept[i];
        if (!s->active) continue;

        s->elapsed += dt;
        Vector3 tip = Vector3Transform((Vector3){0.0f, 0.0f, 0.0f}, *s->xf);
        if (!s->hasPrevTip) { s->prevTip = tip; s->hasPrevTip = true; }

        // Ramped width — never popped.
        float k = 1.0f - expf(-dt * 6.0f);
        s->widthLevel += (s->widthTarget - s->widthLevel) * k;

        // THE MOTION GATE. A stationary weapon must let its trail decay, not
        // hold a frozen full-length ribbon: feeding is what keeps a strand
        // alive, so simply not feeding it IS the decay.
        float moved = Vector3Distance(tip, s->prevTip);

        // TELEPORT before anything else. Everything below this line assumes the
        // gap between two samples is a path the tip actually swept.
        float jumpLimit = SWEPT_TELEPORT_SPEED * dt;
        if (jumpLimit < SWEPT_TELEPORT_MIN) jumpLimit = SWEPT_TELEPORT_MIN;
        if (moved > jumpLimit) {
            TraceLog(LOG_INFO,
                     "VFX_SWEPT: slot %d cut — the transform jumped %.2f m in one "
                     "frame (limit %.2f m). Treated as a teleport, not a swing.",
                     i, moved, jumpLimit);
            SweptTrail_Cut(s, i, tip);
            continue;
        }

        bool  moving = (moved / dt) > SWEPT_IDLE_SPEED;

        // Nodes at a fixed RATE with sub-frame interpolation. Pushing once per
        // frame instead would make the tail's length in metres a function of the
        // frame rate, and at 30 fps every sub-step would land on the same point.
        s->sampleAcc += dt;
        int steps = (int)(s->sampleAcc / SWEPT_SAMPLE_DT);
        if (steps > SWEPT_STEPS_MAX) { steps = SWEPT_STEPS_MAX; s->sampleAcc = 0.0f; }
        else s->sampleAcc -= (float)steps * SWEPT_SAMPLE_DT;

        if (moving) {
            for (int n = 1; n <= steps; n++) {
                SweptTrail_Push(s, Vector3Lerp(s->prevTip, tip,
                                               (float)n / (float)steps),
                                Vector3Scale(Vector3Subtract(tip, s->prevTip),
                                             1.0f / dt));
            }
        }
        s->prevTip = tip;
        // The chain runs EVERY frame, moving or not. A ribbon whose emitter has
        // stopped is still a ribbon settling, and freezing it the instant the
        // hand stops is exactly the "picture" read.
        SweptTrail_Simulate(s, dt);
        // The head belongs to the emitter, always.
        if (s->filled > 0) { s->ring[s->head] = tip; s->nvel[s->head] = (Vector3){0,0,0}; }
        if (!moving || steps <= 0) continue;

        SweptTrail_UpdateNormal(s);

        // SPARKLE — layer four of the reference sheet, and the only one that is
        // not geometry. A RATE, carried between frames, and only while moving.
        // Born ON the trail's newest node with the tip's own velocity, so they
        // shed off the head instead of being sprinkled along the band.
        s->sparkAcc += dt * SWEPT_SPARK_RATE * s_sweptSpark;
        int sparks = (int)s->sparkAcc;
        if (sparks > 3) sparks = 3;
        s->sparkAcc -= (float)sparks;
        if (s_sweptSpark > 0.01f && GfxQuality_Get() >= GFX_MED) {
            const VFX_ElementMaterial *sm = VFX_Material(s->matId);
            for (int k = 0; k < sparks; k++) {
                Vector3 v = Vector3Scale(Vector3Subtract(tip, s->prevTip), 1.0f / dt);
                Vector3 jit = { (Random01() - 0.5f), (Random01() - 0.5f), (Random01() - 0.5f) };
                // ALONG the ribbon, not only off the head — guide §5, "particles
                // along ribbon". Biased toward the head (sqrt) so the shower is
                // densest where the energy is, without the tail going bare.
                int span = s->filled - 1;
                if (span > s->maxNodes - 1) span = s->maxNodes - 1;
                int back = (span > 0) ? (int)(sqrtf(Random01()) * (float)span) : 0;
                Vector3 born = s->ring[(s->head - back + SWEPT_RING) % SWEPT_RING];
                SpawnParticle((ParticleConfig){
                    .position = Vector3Add(born, Vector3Scale(jit, 0.09f)),
                    // MAGIC DUST, not sparks. The owner's direction, 29/07:
                    // "born, stay where they are, drift gently, then go." A
                    // spark thrown at a quarter of the tip's speed is grit
                    // leaving a grinder; dust hangs in the air the ribbon passed
                    // through. So the velocity is almost nothing and there is no
                    // stretch at all — a streaked dust mote is a contradiction.
                    .velocity = Vector3Scale(jit, 0.11f),
                    // Small and VERY bright: the sparkle comes from luminance,
                    // not from area. emissiveBoost pushes it over the bloom
                    // threshold, which is what turns a 1 cm dot into a star.
                    .radius   = Math_Mix(0.006f, 0.013f, Random01()),
                    .lifetime = Math_Mix(0.55f, 1.40f, Random01()),
                    .colorStart = VC_WithAlpha(VC_Whiten(sm->glow, 0.75f), 255),
                    .colorEnd   = VC_WithAlpha(VC_Whiten(sm->glow, 0.30f), 0),
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

// Fills `out` with this frame's strip, tail (s = 0) to head (s = 1), and returns
// the point count. Positions only — width, colour and UV are per PASS.
static int SweptTrail_BuildPoints(const VC_SweptTrail *s, RibbonPoint *out, int max)
{
    int n = s->filled;
    if (n > s->maxNodes) n = s->maxNodes;
    if (n > max) n = max;
    if (n < 2) return 0;
    for (int i = 0; i < n; i++) {
        // ring[head] is the newest, so walk backwards from it into out[] from
        // the END: out[n-1] is the head.
        int idx = (s->head - (n - 1 - i) + SWEPT_RING * 2) % SWEPT_RING;
        out[i].position = s->ring[idx];
    }
    return n;
}

static void VC_SweptTrail_Draw3D(Camera3D cam)
{
    static RibbonPoint pts[SWEPT_RING];

    // Additive, depth test on, depth write off, batch flushed on BOTH sides of
    // every state change (ENGINE_LANDMINES §1). Once for all trails, not once
    // per trail: the whole point of doing this here is that the passes batch.
    bool began = false;

    for (int i = 0; i < SWEPT_MAX; i++) {
        VC_SweptTrail *s = &s_swept[i];
        if (!s->active) continue;

        int n = SweptTrail_BuildPoints(s, pts, SWEPT_RING);
        if (n < 2) continue;

        // Width from the length the tip ACTUALLY travelled, with the caller's
        // width as a ceiling — the thickness rule, unchanged.
        float travel = SweptTrail_TravelLength(s);
        float halfW  = SweptTrail_HalfWidth(s->width * s_sweptWidthMul,
                                            s->widthLevel, travel, s->style);
        float pxPerM = SweptTrail_PixelsPerMetre(
                           Vector3Distance(cam.position, s->ring[s->head]),
                           cam.fovy, (float)GetScreenHeight());
        float aScale = 1.0f;
        halfW = SweptTrail_ScreenFloor(halfW, pxPerM,
                                       SWEPT_MIN_PIXELS * s_sweptMinPx, &aScale);
        if (halfW <= 0.0f) continue;

        const VFX_ElementMaterial *m = VFX_Material(s->matId);
        // FILAMENT is the same layered strip drawn a few times across the
        // bundle; every other style is one.
        int copies = (s->style == VFX_TRAIL_FILAMENT)
                         ? SweptTrail_StrandCount(s->style, GfxQuality_Get() < GFX_MED)
                         : 1;

        if (!began) {
            rlDrawRenderBatchActive();
            BeginBlendMode(BLEND_ADDITIVE);
            rlDisableDepthMask();
            rlDrawRenderBatchActive();
            began = true;
        }

        for (int c = 0; c < copies; c++) {
            Vector3 lateral = {0.0f, 0.0f, 0.0f};
            if (copies > 1 && s->hasLateral) {
                lateral = Vector3Scale(s->lateralAxis,
                                       k_sweptSpread[c] * halfW * s_sweptSpread);
            }
            // Cumulative arc length in METRES, and the flow-noise offset, both
            // computed once for all three passes.
            float arc[SWEPT_RING];
            Vector3 disp[SWEPT_RING];
            arc[0] = 0.0f;
            for (int k = 0; k < n; k++) {
                int idx = (s->head - (n - 1 - k) + SWEPT_RING * 2) % SWEPT_RING;
                Vector3 p = Vector3Add(s->ring[idx], lateral);
                if (k > 0) arc[k] = arc[k - 1] + Vector3Distance(p, disp[k - 1]);
                disp[k] = p;
            }
            float arcLen = arc[n - 1];
            if (arcLen < 1e-4f) continue;

            // NO synthetic displacement here any more. The shape's life comes
            // from SweptTrail_Simulate — real lag, sag and overshoot in the node
            // positions — and a decorative wave layered on top of that was
            // precisely the "picture being moved around" the owner saw.

            for (int pass = 0; pass < 3; pass++) {
                for (int k = 0; k < n; k++) {
                    float t = (float)k / (float)(n - 1);          // 0 tail, 1 head
                    float env = SkillCurve_Eval(&s_sweptWidthCurve[s->style], t);
                    float a   = SkillCurve_Eval(&s_sweptAlphaCurve[s->style], t);
                    pts[k].position  = disp[k];
                    pts[k].halfWidth = halfW * env * k_sweptPassW[pass];
                    // GUIDE §4 "colour over length": the element's BODY in the
                    // cooling tail, its GLOW at the head. One flat colour along
                    // a band is what makes an additive strip read as plastic.
                    Color col = VC_MixColor(m->body, m->glow,
                                            SmoothStep01((t - 0.35f) / 0.65f));
                    col = VC_Whiten(col, k_sweptPassWhite[pass]);
                    float al = a * k_sweptPassA[pass] * aScale * s_sweptAlphaMul;
                    // The core burns at the head and is gone by mid-tail, so the
                    // ribbon has ONE bright spot rather than a uniformly lit
                    // length. Without this the three layers are bright in the
                    // same places and average into a flat stroke.
                    if (pass == 2) al *= powf(t, SWEPT_CORE_HEAD_POW) * s_sweptCoreHot;
                    pts[k].tint = ColorAlpha(col, Clamp(al, 0.0f, 1.0f));
                    // GUIDE §3: TILED by metres so texel density does not change
                    // with the tail's length, and SCROLLED so the fibres flow.
                    // The outer glow scrolls slower than the core — parallax
                    // between the layers is what stops three passes reading as
                    // one thick stroke.
                    pts[k].v = arc[k] / SWEPT_FLOW_TILE
                             - s->elapsed * SWEPT_FLOW_SPEED * s_sweptFlow
                               * (0.50f + 0.55f * (float)pass);
                }
                // Pass 0 is the halo and gets the fibre-less sheet.
                // THE INSTRUMENT. Four rounds have now gone on "the fibres do
                // not move", each answered by changing a number and waiting for
                // a screenshot. This prints what the UV is ACTUALLY doing, once
                // a second, so the next observation settles it: if dv is moving
                // and the screen is not, the scroll is not the problem.
                if (s_sweptUVLog >= 0.5f && pass == 1 && c == 0) {
                    static double lastUVLog = 0.0;
                    static float  lastHeadV = 0.0f;
                    double now = GetTime();
                    if (now - lastUVLog > 1.0) {
                        TraceLog(LOG_INFO,
                                 "VFX_SWEPT uv: arc %.2f m = %.1f tiles | v tail %.2f head %.2f "
                                 "| head v moved %.2f since last second",
                                 arcLen, arcLen / SWEPT_FLOW_TILE, pts[0].v,
                                 pts[n - 1].v, pts[n - 1].v - lastHeadV);
                        lastHeadV = pts[n - 1].v;
                        lastUVLog = now;
                    }
                }
                // ONLY THE BODY CARRIES THE FIBRES, and this is why the flow
                // looked frozen however fast it was scrolled. Three additive
                // layers of the SAME quasi-periodic pattern at three different
                // phases SUM TO SOMETHING FLAT — that is what averaging shifted
                // copies of a pattern does — so the interior structure cancelled
                // itself out and left a smooth glow that could not visibly move.
                // The halo and the core are lit SHAPES, not textured ones; the
                // structure belongs to exactly one layer or it belongs to none.
                Texture2D sheet = (pass == 1 || s_sweptHaloTex.id == 0)
                                      ? s_sweptBladeTex : s_sweptHaloTex;
                DrawRibbonStripEx(pts, n, sheet, cam,
                                  RIBBON_CAMERA_FACING, (Vector3){0.0f, 1.0f, 0.0f});
            }
        }
    }

    if (began) {
        rlDrawRenderBatchActive();
        rlEnableDepthMask();
        EndBlendMode();
        rlDrawRenderBatchActive();
    }
}
