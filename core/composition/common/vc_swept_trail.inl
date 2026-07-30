// ── H1. VFX_ComposeSweptTrail — the swept weapon/body trail ──────────────────
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

#define SWEPT_MAX 8         // concurrent trails
#define SWEPT_STRANDS_MAX 4 // strands per trail (FILAMENT uses all of them)
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
    VFX_TrailStyle style;
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
static Texture2D s_sweptAssetTex = {0}; // energy_flow.png, rotated + made to tile
// Whichever of the two the dial selects. The layer table holds its ADDRESS, so
// swapping sheets is one assignment and takes effect on the next draw.
static Texture2D s_sweptBodyTex = {0};
// THE SAME FILAMENTS, SEAMLESS ACROSS u — for a swept TUBE.
//
// A ribbon sheet cannot be wrapped around a tube, and the reason is structural
// rather than a matter of tuning. On a strip, u = 0 and u = 1 are the two EDGES,
// and the sheet fades to zero at both so the silhouette closes. Wrap that around
// a cylinder and those two edges are THE SAME LINE: the tube goes fully
// transparent along one seam and opaque on the opposite side, i.e. it draws as
// half a tube. With two layers, two nested half-shells — which is exactly what
// the owner described seeing, before anyone had looked at the UV.
//
// So the tube gets its own sheet: no edge kill, and cross-faded in BOTH axes so
// it tiles along the length AND wraps around the circumference.
static Texture2D s_sweptTubeTex = {0};
// The purpose-built volume sheets (scripts/gen_volume_trail_textures.py). These
// REPLACE the tube variant derived from energy_flow.png: that one was a ribbon
// asset rotated, cropped and cross-faded into tiling, and a cross-faded seam
// blurs exactly the detail the sheet exists to carry. These are seamless on both
// axes BY CONSTRUCTION — the noise lattice wraps — so nothing is softened.
//
// One per material family, because they are not interchangeable: smoke has no
// edges, flame is stretched along its travel and has a hot core, energy is
// strands. A single "nice noise" for all three is how every element ends up
// looking like the same effect tinted differently.
static Texture2D s_volEnergyTex = {0};
static Texture2D s_volSmokeTex = {0};
static Texture2D s_volFireTex = {0};

// How much the ribbon behaves like cloth. BLADE is nearly a record (a sword's
// trail is struck, not draped); RIBBON is silk and is allowed to sag, lag and
// overshoot; FILAMENT sits between.
#define SWEPT_STYLES 4
static ForceField s_sweptCloth[SWEPT_STYLES];

// The dust's twinkle: three beats inside one life, so a mote pulses instead of
// fading flat. FLOAT_CURVE_MAX_STOPS is 8, which is exactly three peaks.
static SkillCurve s_sweptTwinkle;

static SkillCurve s_sweptWidthCurve[SWEPT_STYLES];
static SkillCurve s_sweptAlphaCurve[SWEPT_STYLES];

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
static float s_sweptSag = 1.0f;       // x on how much the ribbon sags
static float s_sweptCoreHot = 1.0f;   // x on the white-hot head
// WHICH SHEET, and HOW LONG A TILE OF IT IS — the two dials that decide whether
// the flow reads as surging energy. `swept_sheet` 1 = the authored
// energy_flow.png, 0 = the procedural streaks. `swept_tile` is metres per
// repeat: the asset is authored 4.4:1 along:across, and a 1.10 m tile on a 3 m
// blade draws it at 7.3:1, so strands come out 1.7x longer than they were
// painted. Mild, and that stretch reads as speed.
static float s_sweptSheet = 1.0f;
static float s_sweptTile = SWEPT_FLOW_TILE;
// Hold the shape still and let ONLY the flow move. The decisive instrument for
// "is the energy actually flowing": a moving shape with a moving texture and a
// moving shape with a painted-on one look exactly the same.
static float s_sweptFreeze = 0.0f;
// Both are DIAGNOSTIC, and both default to the stripped-back state. Turn them up
// only once the bare tube is agreed to be the right shape.
//   haze_layers = 2 -> add the outer bloom back
//   haze_tex    = 1 -> put the flow sheet on the tube
static float s_hazeLayers = 1.0f;
static float s_hazeTex = 0.0f;
// 1 = draw both walls, so grazing angles accumulate more material and the
// silhouette brightens on its own. 0 = near wall only: no rim, and half the
// fill. Not a fresnel term — there is no fresnel on a trail — but it is what
// produces the same read, and it is the thing to turn off when the edges are
// too hot.
static float s_hazeRim = 1.0f;
// SOLID WHITE, for judging the SHAPE and nothing else.
//
// "Turn everything off" does not give an opaque surface, because the thing that
// makes a trail see-through is not a setting that was left on — it is
// BLEND_ADDITIVE, which is what the blend law requires of anything that emits
// light. Additive never occludes: it adds to whatever is behind it, so the stars
// show through a fully opaque-alpha tube. Judging a silhouette through it is
// reading a shape through frosted glass.
//
// This flips the whole path to opaque alpha, white, no gradient, single-sided —
// a matte cast of the geometry. It is a DEBUG view and deliberately breaks the
// blend law; nothing that emits should ship this way.
static float s_hazeSolid = 0.0f;
// Vertex deformation by noise, as a fraction of the local radius. This is the
// layer that makes a swept tube stop being a swept tube: without it the surface
// is mathematically smooth and reads as extruded plastic however good the sheet
// on it is. 0.18 is a live surface; 0.4 billows.
static float s_hazeNoise = 0.18f;

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
static float SweptTrail_AspectK(VFX_TrailStyle style)
{
    switch (style)
    {
    case VFX_TRAIL_RIBBON:
        return 0.0715f; // 1:7 — cloth, and cloth is BROAD
    case VFX_TRAIL_FILAMENT:
        return 0.0125f; // 1:40 — thread
    case VFX_TRAIL_HAZE:
        return 0.1600f; // 1:3 — a wake, and a wake is BROAD. This is the widest
                        // additive surface in a projectile, which is exactly why
                        // its alpha ceiling below is the lowest.
    case VFX_TRAIL_BLADE:
    default:
        return 0.0250f; // 1:20 — blade
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
    float cap = travelLen * SweptTrail_AspectK(style) * s_sweptAspectMul;
    if (want < 0.0f)
        want = 0.0f;
    return (cap < want) ? cap : want;
}

// How much the ribbon behaves like cloth. BLADE is nearly a record (a sword's
// trail is struck, not draped); RIBBON is silk and is allowed to sag, lag and
// overshoot; FILAMENT sits between.
static float SweptTrail_Sag(VFX_TrailStyle style)
{
    switch (style)
    {
    case VFX_TRAIL_RIBBON:
        return 2.60f; // m/s^2 downward
    case VFX_TRAIL_FILAMENT:
        return 0.90f;
    default:
        return 0.70f;
    }
}
static float SweptTrail_Drag(VFX_TrailStyle style)
{
    // Higher = the node settles faster = stiffer. Low drag is what lets the tail
    // keep travelling after the head has stopped.
    switch (style)
    {
    case VFX_TRAIL_RIBBON:
        return 1.9f;
    case VFX_TRAIL_FILAMENT:
        return 3.0f;
    default:
        return 5.0f;
    }
}
static float SweptTrail_Wind(VFX_TrailStyle style)
{
    switch (style)
    {
    case VFX_TRAIL_RIBBON:
        return 1.70f;
    case VFX_TRAIL_FILAMENT:
        return 1.10f;
    default:
        return 0.55f;
    }
}

static int SweptTrail_StrandCount(VFX_TrailStyle style, bool lowTier)
{
    if (style != VFX_TRAIL_FILAMENT)
        return 1;
    // E8 tier budget: each strand is its own ribbon submission. The gate only
    // ever clamps DOWN — a low tier loses threads, never gains them.
    // TWO, not four. The reference guide is explicit — "1-3 wisps is enough" —
    // and a composite that spawns 2 wisps of a 4-strand style puts EIGHT bright
    // ribbons on screen, which is the guide's "random chaos" mistake and reads
    // as a bundle of wires rather than as energy. The bundle belongs to the
    // caller, which already decides how many wisps it wants.
    return lowTier ? 1 : 2;
}

// Tail memory in seconds → nodes. The arithmetic moved to vc_common.inl when the
// volume trail became its second caller; the sample rate is still this file's.
static int SweptTrail_MaxNodes(float lifetime)
{
    return VC_TrailNodesForLifetime(lifetime, SWEPT_SAMPLE_HZ);
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

// ── The AUTHORED flow sheet ──────────────────────────────────────────────────
//
// `assets/textures/energy_flow.png` is a real filament bundle: strands that
// braid, cross, thicken and gutter out, with a haze around them. Nothing hand
// written is going to beat it, and the whole reason the procedural sheet exists
// is that nobody had looked to see whether the asset was on disk.
//
// Three things have to happen before it can be a trail sheet, and each of them
// is the reason a straight `ResourceManager_LoadTexture` would not have worked:
//
//  1. **It is authored SIDEWAYS.** The source is 1792 x 896 with the flow
//     running across the WIDTH. A ribbon strip's `u` is across the band and `v`
//     is along it, so the sheet is rotated a quarter turn at load. Doing it here
//     costs one load; doing it in the UV costs a swap on every vertex, forever.
//  2. **Most of it is empty.** Measured row by row: everything above 0.30 and
//     below 0.70 of the height averages under 4/255. Mapped whole, the ribbon
//     would be a thin bright wire floating inside a band of nothing — the
//     geometry would be four times wider than anything you can see in it.
//  3. **It does not tile.** The sheet is scrolled and REPEATed, so a seam would
//     travel down the trail once per tile. The ends are cross-faded into each
//     other over an eighth of the length, which for wisps just reads as more
//     wisps.
//
// The procedural streak sheet is kept as the fallback, and `swept_sheet` swaps
// between them live — the asset is a look decision, and the owner is the one who
// can see it.
#define SWEPT_ASSET_PATH "assets/textures/energy_flow.png"
#define SWEPT_ASSET_CROP0 0.30f // where filaments start, as a fraction of height
#define SWEPT_ASSET_CROP1 0.70f
#define SWEPT_ASSET_FADE 0.125f // of the tile length, spent cross-fading the wrap
#define SWEPT_ASSET_GAIN 1.15f
#define SWEPT_ASSET_FLOOR 0.14f // a dim body so the strip keeps a silhouette
#define SWEPT_ASSET_EDGE 0.12f  // of the width, forced to zero at both rims

static void SweptTrail_BuildAssetSheet(void)
{
    Image src = LoadImage(SWEPT_ASSET_PATH);
    if (src.data == NULL)
    {
        TraceLog(LOG_WARNING,
                 "VFX_SWEPT: %s missing — falling back to the procedural streak sheet",
                 SWEPT_ASSET_PATH);
        return;
    }
    ImageFormat(&src, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

    int y0 = (int)((float)src.height * SWEPT_ASSET_CROP0);
    int y1 = (int)((float)src.height * SWEPT_ASSET_CROP1);
    ImageCrop(&src, (Rectangle){0.0f, (float)y0,
                                (float)src.width, (float)(y1 - y0)});
    ImageRotateCW(&src); // width becomes ACROSS the band, height ALONG it

    int W = src.width, H = src.height;
    int F = (int)((float)H * SWEPT_ASSET_FADE);
    if (F < 2)
        F = 2;
    if (F > H / 3)
        F = H / 3;
    int outH = H - F; // the faded-in head replaces the discarded tail

    Image out = GenImageColor(W, outH, BLANK);
    const Color *sp = (const Color *)src.data;
    for (int y = 0; y < outH; y++)
    {
        for (int x = 0; x < W; x++)
        {
            float a = (float)sp[y * W + x].r / 255.0f; // greyscale source
            if (y < F)
            {
                // Raised-cosine cross-fade, so out[0] continues from out[outH-1].
                float t = 0.5f * (1.0f - cosf(PI * (float)y / (float)F));
                float b = (float)sp[(H - F + y) * W + x].r / 255.0f;
                a = b * (1.0f - t) + a * t;
            }
            float u = ((float)x + 0.5f) / (float)W;
            // The rims are forced to zero over a NARROW margin rather than by
            // multiplying in the band profile: the asset brought its own falloff,
            // and stacking a second one on top leaves a wire. This only
            // guarantees the silhouette closes, which is what stops the strip
            // scalloping (see the halo lesson in docs/LANDMINES.md).
            float edge = SmoothStep01(fminf(u, 1.0f - u) / SWEPT_ASSET_EDGE);
            a = (a * SWEPT_ASSET_GAIN + SWEPT_ASSET_FLOOR * edge) * edge;
            a = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
            ImageDrawPixel(&out, x, y, (Color){255, 255, 255, (unsigned char)(a * 255.0f)});
        }
    }
    s_sweptAssetTex = LoadTextureFromImage(out);

    // The tube variant, from the same rotated source: cross-faded in u as well
    // as v, and with NO edge kill — a rim that fades to nothing is a hole when
    // the two rims meet.
    int G = W / 6;
    if (G < 2) G = 2;
    if (G > W / 3) G = W / 3;
    int outW = W - G;
    Image tube = GenImageColor(outW, outH, BLANK);
    for (int y = 0; y < outH; y++)
    {
        for (int x = 0; x < outW; x++)
        {
            float a = (float)sp[y * W + x].r / 255.0f;
            if (y < F)
            {
                float t = 0.5f * (1.0f - cosf(PI * (float)y / (float)F));
                a = ((float)sp[(H - F + y) * W + x].r / 255.0f) * (1.0f - t) + a * t;
            }
            if (x < G)
            {
                // The same raised-cosine wrap, around the circumference.
                float t = 0.5f * (1.0f - cosf(PI * (float)x / (float)G));
                float b = (float)sp[y * W + (W - G + x)].r / 255.0f;
                if (y < F)
                {
                    float tv = 0.5f * (1.0f - cosf(PI * (float)y / (float)F));
                    b = ((float)sp[(H - F + y) * W + (W - G + x)].r / 255.0f) * (1.0f - tv) + b * tv;
                }
                a = b * (1.0f - t) + a * t;
            }
            // Lifted, not edge-killed: every point around the tube must carry
            // some material or the silhouette has a gap in it.
            a = a * SWEPT_ASSET_GAIN + SWEPT_ASSET_FLOOR;
            a = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
            ImageDrawPixel(&tube, x, y, (Color){255, 255, 255,
                                                (unsigned char)(a * 255.0f)});
        }
    }
    s_sweptTubeTex = LoadTextureFromImage(tube);
    UnloadImage(tube);
    UnloadImage(src);
    UnloadImage(out);
    if (s_sweptTubeTex.id != 0)
    {
        SetTextureFilter(s_sweptTubeTex, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(s_sweptTubeTex, TEXTURE_WRAP_REPEAT); // both axes wrap now
    }
    if (s_sweptAssetTex.id != 0)
    {
        SetTextureFilter(s_sweptAssetTex, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(s_sweptAssetTex, TEXTURE_WRAP_REPEAT);
        TraceLog(LOG_INFO, "VFX_SWEPT: flow sheet from %s (%dx%d, wrap cross-faded)",
                 SWEPT_ASSET_PATH, W, outH);
    }
}

static void SweptTrail_InitShared(void)
{
    if (s_sweptInit)
        return;

    SweptTrail_BuildBladeMask();
    SweptTrail_BuildAssetSheet();
    s_volEnergyTex = ResourceManager_LoadTexture("assets/textures/energy_volume.png");
    s_volSmokeTex = ResourceManager_LoadTexture("assets/textures/smoke_volume.png");
    s_volFireTex = ResourceManager_LoadTexture("assets/textures/fire_volume.png");
    // REPEAT on both axes: a tube wraps u around the section and tiles v along
    // the length, and these sheets were generated to survive exactly that.
    if (s_volEnergyTex.id != 0) { SetTextureFilter(s_volEnergyTex, TEXTURE_FILTER_BILINEAR); SetTextureWrap(s_volEnergyTex, TEXTURE_WRAP_REPEAT); }
    if (s_volSmokeTex.id != 0) { SetTextureFilter(s_volSmokeTex, TEXTURE_FILTER_BILINEAR); SetTextureWrap(s_volSmokeTex, TEXTURE_WRAP_REPEAT); }
    if (s_volFireTex.id != 0) { SetTextureFilter(s_volFireTex, TEXTURE_FILTER_BILINEAR); SetTextureWrap(s_volFireTex, TEXTURE_WRAP_REPEAT); }

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
    // BLADE, RIBBON, FILAMENT, HAZE.
    //
    // HAZE barely holds its path: high curl, almost no drag, so it billows and
    // spreads instead of following. That is the whole difference between a
    // backdrop and a trail — and it is still ANCHORED (see the home spring), or
    // it would stop reading as the wake of anything.
    static const float sag[SWEPT_STYLES] = {0.40f, 0.95f, 0.55f, 0.30f};
    static const float curl[SWEPT_STYLES] = {0.30f, 0.55f, 0.40f, 0.95f};
    static const float drag[SWEPT_STYLES] = {5.50f, 3.40f, 4.20f, 2.20f};
    for (int st = 0; st < SWEPT_STYLES; st++)
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

    // HAZE. A TEARDROP, on the owner's call — the same profile the water stream
    // tube uses, which is `sqrt(sin(t*PI))` times a taper rising toward the head:
    // a needle at the tail, swelling to its widest around t = 0.7, then rounding
    // off. That shape is why a droplet reads as a droplet and a cone reads as a
    // cone, and it is worth copying rather than re-deriving.
    //
    // TWO DEPARTURES FROM THE WATER STREAM, both because of what sits on the end:
    //
    //  - It does NOT return to zero at the head. The stream closes itself with a
    //    rounded end-cap; this one has the orb sitting on it, so a taper there
    //    would open a gap between the ball and its own field. It rounds to 0.86
    //    and the orb covers the rest.
    //  - The tail goes to zero rather than to the stream's 0.15 floor, which
    //    makes the tube's end-cap degenerate to a point instead of a flat disc.
    //
    // This shipped as a straight triangle for one round, and before that
    // BACKWARDS — widest at the tail — because I derived the shape from an
    // analogy ("a wake spreads as it ages") instead of from what was drawn.
    FloatCurve_AddStop(&s_sweptWidthCurve[VFX_TRAIL_HAZE], 0.00f, 0.00f);
    FloatCurve_AddStop(&s_sweptWidthCurve[VFX_TRAIL_HAZE], 0.20f, 0.37f);
    FloatCurve_AddStop(&s_sweptWidthCurve[VFX_TRAIL_HAZE], 0.45f, 0.79f);
    FloatCurve_AddStop(&s_sweptWidthCurve[VFX_TRAIL_HAZE], 0.70f, 1.00f);
    FloatCurve_AddStop(&s_sweptWidthCurve[VFX_TRAIL_HAZE], 0.88f, 0.95f);
    FloatCurve_AddStop(&s_sweptWidthCurve[VFX_TRAIL_HAZE], 1.00f, 0.86f);

    FloatCurve_AddStop(&s_sweptAlphaCurve[VFX_TRAIL_HAZE], 0.00f, 0.00f);
    FloatCurve_AddStop(&s_sweptAlphaCurve[VFX_TRAIL_HAZE], 0.20f, 0.26f);
    FloatCurve_AddStop(&s_sweptAlphaCurve[VFX_TRAIL_HAZE], 0.45f, 0.62f);
    FloatCurve_AddStop(&s_sweptAlphaCurve[VFX_TRAIL_HAZE], 0.70f, 0.88f);
    FloatCurve_AddStop(&s_sweptAlphaCurve[VFX_TRAIL_HAZE], 1.00f, 1.00f);

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
    Tuning_RegisterFloat("swept_sheet", &s_sweptSheet, 1.0f);
    Tuning_RegisterFloat("swept_tile", &s_sweptTile, SWEPT_FLOW_TILE);
    Tuning_RegisterFloat("swept_freeze", &s_sweptFreeze, 0.0f);
    Tuning_RegisterFloat("haze_layers", &s_hazeLayers, 1.0f);
    Tuning_RegisterFloat("haze_tex", &s_hazeTex, 0.0f);
    Tuning_RegisterFloat("haze_rim", &s_hazeRim, 1.0f);
    Tuning_RegisterFloat("haze_solid", &s_hazeSolid, 0.0f);
    Tuning_RegisterFloat("haze_noise", &s_hazeNoise, 0.18f);

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
static TrailLayer s_sweptLayers[3] = {
    {.widthMul = 1.55f, .alphaMul = 0.07f, .whiten = 0.00f, .scrollMul = 0.50f,
     .headAlphaPow = 0.0f, .texture = NULL},
    {.widthMul = 1.00f, .alphaMul = 0.31f, .whiten = 0.06f, .scrollMul = 1.05f,
     .headAlphaPow = 0.0f, .texture = &s_sweptBodyTex},
    // The core burns at the head and is gone by mid-tail, so the ribbon has ONE
    // bright spot rather than three layers bright in the same places.
    // 0.40 WHITEN, not 1.00 — and this is the fix for "the trail's colour has
    // nothing to do with the orb's" (owner, 30/07). The numbers say it outright:
    // this layer contributes 0.28 of the total emitted energy and, at whiten 1.0,
    // ALL of it colourless, so the three layers summed to a hue 66% desaturated
    // toward white while the orb — which does not whiten at all — stayed fully
    // saturated. Two things in one effect, colour-managed by two different rules.
    //
    // Whitening at the source is still right: a saturated hue stacks additively
    // into more of itself and never reaches white, so emissiveBoost has nothing
    // to lift. But it is a seasoning, not a base. At 0.40 the head keeps 55% of
    // its saturation and still reads hot.
    //
    // A STEEPER head power (3.4, not 2.6) shortens the hot spot instead of
    // running it down half the ribbon.
    {.widthMul = 0.26f, .alphaMul = 0.28f, .whiten = 0.20f, .scrollMul = 1.60f,
     .headAlphaPow = 3.4f, .texture = NULL},
};

// THE HAZE STACK — the WIDE ENERGY FIELD, and it carries the flow texture.
//
// This shipped twice with the same mistake, so the reasoning is worth keeping.
// I gave both its layers the STRUCTURE-FREE sheet, citing the rule that the
// structure belongs to exactly ONE layer. That rule is real, and it does not
// apply here: it exists because several layers of ONE trail draw the SAME
// pattern at different scroll phases, and averaging shifted copies of a pattern
// is how you remove it. The haze is a SEPARATE TRAIL with its own history and
// its own UV — nothing of it is a shifted copy of anything.
//
// So applying an intra-stack rule across trails stripped the flow texture from
// the exact layer whose entire job is to be seen flowing. The owner, twice:
// "trường năng lượng cuộn chảy mờ ảo, đâu?" — it was there, faint and
// featureless, and a featureless band cannot be seen to scroll.
//
// AND IT WAS TOO FAINT TO SEE AT ALL. Measured against the main trail: 3.6x
// dimmer per pixel and spread over 2.2x the width, so 8x dimmer in effect. That
// is not "faint", that is absent. "Mờ ảo" means hazy, not invisible — this layer
// is what gives the wake its MASS, and mass you cannot see is not mass.
static TrailLayer s_sweptHazeLayers[2] = {
    // [0] THE TUBE ITSELF. Untextured and single by default while the SHAPE is
    // still being judged — the owner's call, and the right one: two layers and a
    // wrapped sheet have now produced three different wrong silhouettes, and
    // every diagnosis had to guess which of the two was at fault. Flat white on
    // one tube has exactly one thing that can be wrong, and it is the geometry.
    {.widthMul = 1.00f, .alphaMul = 0.45f, .whiten = 0.00f, .scrollMul = 1.00f,
     .headAlphaPow = 0.0f, .texture = NULL},
    // [1] The soft outer bloom, restored by haze_layers = 2.
    {.widthMul = 1.35f, .alphaMul = 0.16f, .whiten = 0.00f, .scrollMul = 0.55f,
     .headAlphaPow = 0.0f, .texture = NULL},
};

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
    cfg.gradient = SweptTrail_Gradient(s->matId);
    cfg.widthCurve = &s_sweptWidthCurve[s->style];
    cfg.alphaCurve = &s_sweptAlphaCurve[s->style];
    cfg.widthEnvelope = TRAIL_WIDTH_ENVELOPE_TAPER_BOTH;
    cfg.forceField = &s_sweptCloth[s->style];
    cfg.ownerTag = SWEPT_TAG_BASE | (slot << 4) | strand;
    cfg.priority = VFX_PRIORITY_LOW;
    cfg.blendMode = BLEND_ADDITIVE;
    cfg.useCustomBlendMode = true;
    cfg.minVertexDistance = SWEPT_MIN_VERTEX;
    // BLADE lies in the plane of the swing — a broad sheet from the front and a
    // thin edge from the side, which IS the sense of a real object moving. The
    // other two are camera-facing, the mode that never pinches on a curve.
    cfg.ribbonMode = (s->style == VFX_TRAIL_BLADE) ? RIBBON_FIXED_NORMAL
                                                   : RIBBON_CAMERA_FACING;
    cfg.fixedNormal = (Vector3){0.0f, 1.0f, 0.0f};
    cfg.disableInnerCore = true; // superseded by the layer stack
    // THE FIELD IS A VOLUME, not a card. Everything else here is a flat strip,
    // which is right for a struck arc and wrong for the mass a projectile drags:
    // a card is the same shape from every angle, so it can be made wide and
    // faint and still read as a decal of a field rather than a field. A swept
    // tube has a silhouette from anywhere, occludes along its own length, and —
    // drawn additively with the far wall showing through the near one —
    // brightens at its own edges without a fresnel term.
    //
    // Its width curve is already the funnel the owner drew (base at the orb,
    // point behind), and that shape pays for itself twice here: a tube's end-cap
    // would otherwise be a flat disc sitting across the tail, and a radius that
    // goes to zero makes the cap degenerate to a point instead.
    //
    // TIER GATE: below GFX_MED it falls back to the flat strip. ~2k triangles
    // per field is not free, and the gate may only ever clamp DOWN.
    if (s->style == VFX_TRAIL_HAZE && GfxQuality_Get() >= GFX_MED)
    {
        cfg.shape = TRAIL_SHAPE_TUBE;
        cfg.tubeRadialSegs = 8;
        // CAPPED. The side quads alone leave the head open and you look straight
        // down the inside of the field — a bowl, not a volume. The teardrop
        // profile ends in a needle, so only the head actually gets a cap.
        cfg.tubeCaps = true;
        // The double wall is what gives a tube its rim for free, so it is the
        // default; `haze_rim = 0` turns it off for a flatter, cheaper read.
        cfg.tubeSingleSided = (s_hazeRim < 0.5f);
        cfg.tubeNoiseAmp = s_hazeNoise;
    }
    if (s->style == VFX_TRAIL_HAZE)
    {
        cfg.layers = s_sweptHazeLayers;
        cfg.layerCount = (s_hazeLayers >= 1.5f) ? 2 : 1;
    }
    else
    {
        cfg.layers = s_sweptLayers;
        cfg.layerCount = 3;
    }
    cfg.uvMetresPerTile = (s_sweptTile > 0.05f) ? s_sweptTile : 0.05f;
    cfg.uvScrollSpeed = SWEPT_FLOW_SPEED * s_sweptFlow;
    cfg.nodeHomeSpring = SWEPT_HOME_SPRING;
    cfg.nodeHomeMaxDev = SWEPT_HOME_MAX_DEV;
    cfg.nodeOrderFrac = SWEPT_ORDER_FRAC;
    cfg.sampleHz = SWEPT_SAMPLE_HZ;
    cfg.teleportSpeed = SWEPT_TELEPORT_SPEED;
    cfg.idleSpeed = SWEPT_IDLE_SPEED;
    cfg.trailLength = (float)SweptTrail_MaxNodes(s->lifetime);
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
             slot, strand, (int)s->style,
             (cfg.shape == TRAIL_SHAPE_TUBE) ? "TUBE" : "ribbon",
             cfg.tubeRadialSegs, (int)GfxQuality_Get(), s->width);
    return id;
}

// ── Public API ───────────────────────────────────────────────────────────────

int VFX_ComposeSweptTrail(const Matrix *followTransform, VC_MaterialId mat,
                          float width, float lifetime, VFX_TrailStyle style)
{
    SweptTrail_InitShared();
    if (!followTransform)
    {
        TraceLog(LOG_WARNING, "VFX_SWEPT: NULL transform — no trail created");
        return -1;
    }
    // AGAINST THE COUNT, never against the last style by name. This check read
    // `style > VFX_TRAIL_FILAMENT` and was not updated when VFX_TRAIL_HAZE was
    // added, so every request for the wide energy field was quietly turned into
    // a BLADE — the narrowest, sharpest style in the file, which is why the
    // field "looked exactly like a normal trail". It did: it WAS one.
    //
    // And it said nothing. A silent clamp is the worst possible failure mode
    // here, because it produces a plausible result rather than an absent one, so
    // there is nothing to notice. It announces itself now.
    if (style < VFX_TRAIL_BLADE || style >= VFX_TRAIL_STYLE_COUNT)
    {
        TraceLog(LOG_WARNING,
                 "VFX_SWEPT: style %d is out of range — clamped to BLADE. "
                 "A new style was probably added without updating this check.",
                 (int)style);
        style = VFX_TRAIL_BLADE;
    }
    if (width <= 0.0f)
        width = 0.22f;
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
        VFX_KillSweptTrail(slot);
    }
    s_sweptNextSerial++;

    VC_SweptTrail *s = &s_swept[slot];
    s->active = true;
    s->xf = followTransform;
    s->matId = mat;
    s->style = style;
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
    s->strands = SweptTrail_StrandCount(style, GfxQuality_Get() < GFX_MED);
    for (int k = 0; k < SWEPT_STRANDS_MAX; k++)
        s->strandId[k] = -1;
    for (int k = 0; k < s->strands; k++)
        s->strandId[k] = SweptTrail_SpawnStrand(s, slot, k);
    return slot;
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

void VFX_KillSweptTrail(int handle)
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

    // The two shared dials, applied once for everything: which sheet the body
    // draws, and how hot the core burns. The layer table holds the texture's
    // ADDRESS, so the swap needs no per-trail work.
    s_sweptBodyTex = (s_sweptSheet >= 0.5f && s_sweptAssetTex.id != 0)
                         ? s_sweptAssetTex
                         : s_sweptBladeTex;
    s_sweptLayers[2].alphaMul = 0.28f * s_sweptCoreHot;
    // The halo and the core are lit shapes; the fibre-less sheet is what makes
    // them shapes rather than second silhouettes.
    s_sweptLayers[0].texture = (s_sweptHaloTex.id != 0) ? &s_sweptHaloTex : NULL;
    s_sweptLayers[2].texture = s_sweptLayers[0].texture;
    // The haze's OUTER layer is a bare shape and takes the structure-free sheet;
    // its BODY keeps the flow sheet, which is the whole point of the field.
    // NULL is the default for BOTH haze layers now. A sheet authored for a strip
    // fades at u = 0 and u = 1 because those are its two edges, and on a tube
    // those edges are the same line — so any band sheet leaves a transparent seam
    // running the whole length. Only the u-seamless variant may ever go on a
    // tube, and only when the dial asks for it.
    // haze_tex: 0 = bare geometry, 1 = energy strands, 2 = smoke, 3 = fire.
    // One dial rather than three, because they are the same slot — and having to
    // pick makes it explicit that the sheet is part of the element's identity
    // rather than a generic prettifier.
    Texture2D *volSheet = NULL;
    if (s_hazeTex >= 2.5f && s_volFireTex.id != 0) volSheet = &s_volFireTex;
    else if (s_hazeTex >= 1.5f && s_volSmokeTex.id != 0) volSheet = &s_volSmokeTex;
    else if (s_hazeTex >= 0.5f && s_volEnergyTex.id != 0) volSheet = &s_volEnergyTex;
    s_sweptHazeLayers[0].texture = volSheet;
    s_sweptHazeLayers[1].texture = NULL;

    for (int i = 0; i < SWEPT_MAX; i++)
    {
        VC_SweptTrail *s = &s_swept[i];
        if (!s->active)
            continue;

        // Ramped width — never popped.
        float k = 1.0f - expf(-dt * 6.0f);
        s->widthLevel += (s->widthTarget - s->widthLevel) * k;

        bool frozen = false;
        const TrailEntity *lead = NULL;

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

            // FREEZE, and it fills first: freezing a trail with one node holds
            // an EMPTY ribbon, and a dial that shows nothing reads as broken.
            frozen = (s_sweptFreeze >= 0.5f) &&
                     (t->historyCount >= SweptTrail_MaxNodes(s->lifetime));
            Trail_SetFrozen(s->strandId[c], frozen);

            // Width from the length the tip ACTUALLY travelled, with the
            // caller's width as a ceiling.
            float travel = SweptTrail_TravelLength(t);
            t->thickness = SweptTrail_HalfWidth(s->width * s_sweptWidthMul,
                                                s->widthLevel, travel, s->style);
            // THE NUMBER THAT DECIDES WHETHER ANY OF THIS IS VISIBLE. The width
            // is EARNED from the length the tip travelled, so a trail can be
            // configured perfectly and still draw as a hairline if the aspect
            // cap is holding it down. Reported once, when the history is full.
            if (!s->widthLogged && t->historyCount >= SweptTrail_MaxNodes(s->lifetime))
            {
                TraceLog(LOG_INFO,
                         "VFX_SWEPT: slot %d strand %d — travelled %.2f m -> "
                         "radius %.3f m (%.2f m across). Ceiling was %.2f m.",
                         i, c, travel, t->thickness, t->thickness * 2.0f,
                         s->width * s_sweptWidthMul * 0.5f);
                s->widthLogged = true;
            }
            t->tint = VC_WithAlpha(WHITE, (unsigned char)(255.0f * Clamp(s_sweptAlphaMul, 0.0f, 1.0f)));
            t->uvMetresPerTile = (s_sweptTile > 0.05f) ? s_sweptTile : 0.05f;
            // The haze's layer count is a live dial, so it is written every
            // frame rather than baked at spawn — otherwise judging one layer
            // against two costs a rebuild each way.
            if (s->style == VFX_TRAIL_HAZE)
            {
                t->layerCount = (s_hazeLayers >= 1.5f) ? 2 : 1;
                // Live, like the layer count: judging rim-on against rim-off is
                // the kind of comparison that must not cost a rebuild each way.
                t->tubeSingleSided = (s_hazeRim < 0.5f);
                t->tubeNoiseAmp = s_hazeNoise;
                if (s_hazeSolid >= 0.5f)
                {
                    // Opaque alpha, pure white, no element gradient, near wall
                    // only. Every one of those is needed: additive alone would
                    // still let the background through, and the gradient would
                    // still tint what is meant to be a matte cast.
                    t->blendMode = BLEND_ALPHA;
                    t->useCustomBlendMode = true;
                    t->gradient = NULL;
                    t->tint = WHITE;
                    t->tubeSingleSided = true;
                    s_sweptHazeLayers[0].alphaMul = 1.0f;
                    s_sweptHazeLayers[0].whiten = 1.0f;
                }
                else
                {
                    t->blendMode = BLEND_ADDITIVE;
                    t->useCustomBlendMode = true;
                    t->gradient = SweptTrail_Gradient(s->matId);
                    s_sweptHazeLayers[0].alphaMul = 0.45f;
                    s_sweptHazeLayers[0].whiten = 0.00f;
                }
            }
            t->uvScrollSpeed = SWEPT_FLOW_SPEED * s_sweptFlow;

            if (c == 0)
                SweptTrail_UpdateNormal(s, t);
            if (s->style == VFX_TRAIL_BLADE)
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
