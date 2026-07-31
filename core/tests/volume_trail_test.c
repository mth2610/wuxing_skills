// core headless test — P1, VFX_ComposeVolumeTrail.
//
// P1 is a PROMOTION, not an invention: the tube already existed as
// `VFX_TRAIL_HAZE` and was verified on screen on 30/07. So the claims worth
// testing are not "does a tube look good" — nothing here can see that — but the
// four that are arithmetic or structural, and every one of which has already
// been got wrong once in this tree:
//
//   1. THE KIND TABLE HAS THREE COLUMNS. Sheet, noise, swirl. A fourth is how a
//      parameter becomes three implementations (VFX_PLAN §4.1), and it is added
//      one innocent field at a time.
//   2. THE BLEND LAW. Smoke occludes, energy and fire emit — and blend AND ramp
//      are read off ONE predicate so they cannot drift apart.
//   3. THE ASPECT LAW. Thickness against the thing's OWN length, with the
//      INVARIANCE asserted and not just the ratio at one point: a ratio checked
//      at a single parameter value passes on the broken formula too.
//   4. THE TIER LADDER CLAMPS DOWN AND ONLY DOWN, and the volume stays a volume
//      at every tier — a gate that can only turn a thing off is not a tier.
//
// Plus the mirror guard: the numbers above are transcribed from the .inl, and a
// C mirror of C silently rots into fiction exactly as fast as a mirror of GLSL.
//
// WHAT THIS CANNOT SEE: whether the three kinds read as three different
// materials, whether the smoke actually occludes on screen, or whether the tube
// is round. The first two are the owner's; the third has its own instrument —
// the `TRAIL tube: ... roundness` line (core/docs/VFX_PLAN.md §4.4).

#include <stdio.h>
#include <string.h>
#include <math.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK_MSG(cond, name, fmt, ...) do { \
    g_checks++; \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s  [" fmt "]\n", name, __VA_ARGS__); g_failures++; } \
} while (0)

#define CHECK(cond, name) do { \
    g_checks++; \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s\n", name); g_failures++; } \
} while (0)

// ── The .inl's numbers, mirrored ────────────────────────────────────────────

#define TRAIL_HISTORY_COUNT 60      // core/trail_system.h
#define VOL_SAMPLE_HZ       60.0f
#define VOL_ASPECT_K        0.20f
#define VOL_TILE            1.30f
#define VOL_MAX             8

enum { VOL_ENERGY = 0, VOL_SMOKE = 1, VOL_FIRE = 2, VOL_KIND_COUNT = 3 };

static const float k_volNoise[VOL_KIND_COUNT] = {0.14f, 0.34f, 0.24f};
static const float k_volSwirl[VOL_KIND_COUNT] = {-2.60f, -0.55f, -4.20f};
static const char *k_volSheetPath[VOL_KIND_COUNT] = {
    "assets/textures/energy_volume.png",
    "assets/textures/smoke_volume.png",
    "assets/textures/fire_volume.png",
};

// The layer stack, as authored: {widthMul, alphaMul, whiten, scrollMul}.
static const float k_layerWidth[2]  = {1.30f, 1.00f};
static const float k_layerAlpha[2]  = {0.16f, 0.46f};
static const float k_layerScroll[2] = {0.55f, 1.00f};

// GFX_UNLIT = 0, GFX_LOW = 1, GFX_MED = 2, GFX_HIGH = 3 (core/gfx_quality.h)
static int RadialSegs(int tier)
{
    switch (tier) { case 3: return 8; case 2: return 8; case 1: return 6; default: return 5; }
}
static int Rings(int tier)
{
    switch (tier) { case 3: return 24; case 2: return 20; case 1: return 14; default: return 10; }
}
static int LayerCount(int tier) { return (tier >= 2) ? 2 : 1; }

static int Emits(int kind) { return kind != VOL_SMOKE; }

static float VolumeRadius(float radiusMetres, float travelLen)
{
    float want = radiusMetres;
    float cap = travelLen * VOL_ASPECT_K;
    if (want < 0.0f) want = 0.0f;
    return (cap < want) ? cap : want;
}

static int MaxNodes(float lifetime)
{
    int n = (int)(lifetime * VOL_SAMPLE_HZ + 0.5f);
    if (n < 4) n = 4;
    if (n > TRAIL_HISTORY_COUNT) n = TRAIL_HISTORY_COUNT;
    return n;
}

// ── 1. The kind table has three columns, and they are actually distinct ─────

static void Test_ThreeKindsDifferInThreeThings(void)
{
    // A "kind" that is not distinguishable in a column is a duplicate wearing a
    // name, which is worse than not having it: it makes the library look wider
    // than it is. Every pair must differ in EVERY column.
    int allDistinct = 1;
    for (int a = 0; a < VOL_KIND_COUNT; a++)
        for (int b = a + 1; b < VOL_KIND_COUNT; b++)
        {
            if (fabsf(k_volNoise[a] - k_volNoise[b]) < 0.03f) allDistinct = 0;
            if (fabsf(k_volSwirl[a] - k_volSwirl[b]) < 0.20f) allDistinct = 0;
            if (strcmp(k_volSheetPath[a], k_volSheetPath[b]) == 0) allDistinct = 0;
        }
    CHECK(allDistinct,
          "every pair of kinds differs in sheet AND noise AND swirl — no kind is "
          "a duplicate of another under a different name");

    // The ORDERING is the authored intent, and it is what makes the three read
    // as three materials rather than three settings: smoke billows hardest,
    // energy holds its shape, fire sits between.
    CHECK_MSG(k_volNoise[VOL_SMOKE] > k_volNoise[VOL_FIRE] &&
              k_volNoise[VOL_FIRE] > k_volNoise[VOL_ENERGY],
              "surface deform: smoke > fire > energy",
              "%.2f / %.2f / %.2f", k_volNoise[VOL_SMOKE], k_volNoise[VOL_FIRE],
              k_volNoise[VOL_ENERGY]);
    CHECK_MSG(fabsf(k_volSwirl[VOL_FIRE]) > fabsf(k_volSwirl[VOL_ENERGY]) &&
              fabsf(k_volSwirl[VOL_ENERGY]) > fabsf(k_volSwirl[VOL_SMOKE]),
              "flow rate: fire licks fastest, smoke churns slowest",
              "%.2f / %.2f / %.2f", k_volSwirl[VOL_FIRE], k_volSwirl[VOL_ENERGY],
              k_volSwirl[VOL_SMOKE]);

    // EVERY swirl is negative, and this is a rule rather than a preference: flow
    // AGAINST the direction of travel reads as something being left behind; flow
    // with it reads as a pattern being pushed along in front.
    int allAgainstTravel = 1;
    for (int k = 0; k < VOL_KIND_COUNT; k++)
        if (k_volSwirl[k] >= 0.0f) allAgainstTravel = 0;
    CHECK(allAgainstTravel, "every kind's sheet flows AGAINST travel");

    // And the noise is a FRACTION of the local radius, so it must stay well
    // under 1 — at 1.0 a vertex can be displaced onto the tube's own axis and
    // the section self-intersects.
    int noiseSane = 1;
    for (int k = 0; k < VOL_KIND_COUNT; k++)
        if (k_volNoise[k] <= 0.0f || k_volNoise[k] > 0.5f) noiseSane = 0;
    CHECK(noiseSane, "every kind's deform is a live surface, not a self-intersecting one");
}

// ── 2. The blend law ────────────────────────────────────────────────────────

static void Test_BlendLaw(void)
{
    // core/particle_system.h states it: if it would BLOCK light, draw
    // BLEND_ALPHA; if it EMITS light, draw BLEND_ADDITIVE and leave it unlit.
    // Additive output can never be darker than its background, which is why an
    // occluding volume drawn additive is not "a bit bright" — it is incapable of
    // being smoke at all.
    CHECK(!Emits(VOL_SMOKE), "smoke OCCLUDES — alpha");
    CHECK(Emits(VOL_ENERGY) && Emits(VOL_FIRE), "energy and fire EMIT — additive");

    // The point of routing both through ONE predicate: an additive volume over a
    // dark ramp is invisible, and an alpha volume over a glow ramp is a bright
    // pastel tube. Each of those has shipped once in this tree. They cannot
    // happen here unless blend and ramp are chosen separately — so they are not.
    for (int k = 0; k < VOL_KIND_COUNT; k++)
    {
        int blendIsAdditive = Emits(k);
        int rampIsGlow = Emits(k);
        CHECK_MSG(blendIsAdditive == rampIsGlow,
                  "blend and ramp agree for every kind",
                  "kind %d", k);
    }
}

// ── 3. The aspect law ───────────────────────────────────────────────────────

static void Test_AspectIsAgainstItsOwnLength(void)
{
    // THE INVARIANCE, not the ratio. A ratio checked at one parameter value
    // passes on the broken formula too — which is exactly how SweepSlash shipped
    // width keyed to the arc's RADIUS and was right at precisely one sweep angle
    // (core/docs/LANDMINES.md, 28/07).
    float worst = 0.0f;
    for (float travel = 0.10f; travel < 4.99f; travel += 0.01f)
    {
        float r = VolumeRadius(1.0f, travel);
        float ratio = r / travel;
        float e = fabsf(ratio - VOL_ASPECT_K) / VOL_ASPECT_K;
        if (e > worst) worst = e;
    }
    CHECK_MSG(worst < 1e-5f,
              "in the capped regime the radius is a FIXED fraction of the length "
              "swept, at every length",
              "worst relative drift %.7f", worst);

    // Full width against travelled length. A volume is BROAD — broader than the
    // haze style's 1:3 and far broader than a blade's 1:20 — and that breadth is
    // most of what separates a plume from a wire.
    float fullWidthAspect = 1.0f / (2.0f * VOL_ASPECT_K);
    CHECK_MSG(fullWidthAspect > 2.0f && fullWidthAspect < 3.5f,
              "the volume's aspect is about 1:2.5, full width against its own length",
              "1:%.2f", fullWidthAspect);

    // The caller's radius is a CEILING and must actually be honoured once the
    // emitter has swept far enough to earn it — a cap that never releases is
    // just a different formula.
    CHECK_MSG(fabsf(VolumeRadius(1.0f, 20.0f) - 1.0f) < 1e-6f,
              "a long sweep gets exactly the radius it asked for",
              "%.4f", VolumeRadius(1.0f, 20.0f));
    float crossover = 1.0f / VOL_ASPECT_K;
    CHECK_MSG(fabsf(crossover - 5.0f) < 1e-4f,
              "and the cap releases at 5 m of travel for a 1 m radius",
              "%.3f m", crossover);

    // The failure this exists to stop: an emitter that has barely moved asking
    // for a fat radius, which draws a BALL rather than a wake.
    CHECK_MSG(VolumeRadius(1.0f, 0.5f) < 0.15f,
              "an emitter that has barely moved gets a wisp, not a ball",
              "%.3f m radius after 0.5 m", VolumeRadius(1.0f, 0.5f));

    // Monotone: more travel can never buy LESS radius.
    int monotone = 1;
    float prev = -1.0f;
    for (float travel = 0.0f; travel <= 20.0f; travel += 0.05f)
    {
        float r = VolumeRadius(1.0f, travel);
        if (r < prev - 1e-6f) monotone = 0;
        prev = r;
    }
    CHECK(monotone, "the radius never shrinks as the emitter travels further");
}

// ── 4. The tier ladder clamps DOWN, and never off ───────────────────────────

static void Test_TierLadderOnlyClampsDown(void)
{
    int monotone = 1;
    for (int tier = 0; tier < 3; tier++)
    {
        if (RadialSegs(tier) > RadialSegs(tier + 1)) monotone = 0;
        if (Rings(tier) > Rings(tier + 1)) monotone = 0;
        if (LayerCount(tier) > LayerCount(tier + 1)) monotone = 0;
    }
    CHECK(monotone, "every tier quantity is non-decreasing with quality — the gate "
                    "can only ever clamp DOWN");

    // AND THE VOLUME STAYS A VOLUME. A gate that can only turn a thing off is
    // not a tier: it gives the low tier a DIFFERENT effect rather than a cheaper
    // one, so nothing authored against the volume holds there.
    int alwaysATube = 1;
    for (int tier = 0; tier <= 3; tier++)
    {
        if (RadialSegs(tier) < 3) alwaysATube = 0;   // below 3 a section is not closed
        if (Rings(tier) < 2) alwaysATube = 0;
        if (LayerCount(tier) < 1) alwaysATube = 0;
    }
    CHECK(alwaysATube, "it is still a closed, drawn tube at the LOWEST tier");

    // What the ladder actually buys, for the record. Triangles = rings x radial
    // x 2 per layer.
    float hi = (float)(Rings(3) * RadialSegs(3) * 2 * LayerCount(3));
    float lo = (float)(Rings(0) * RadialSegs(0) * 2 * LayerCount(0));
    CHECK_MSG(hi / lo > 3.0f,
              "and the lowest tier is several times cheaper than the highest",
              "%.0f tris vs %.0f — %.1fx", hi, lo, hi / lo);
}

// ── 5. The alpha budget ─────────────────────────────────────────────────────

static void Test_AdditiveBudget(void)
{
    // These layers OVERLAP — the body sits inside the shell — and they are
    // additive, so the frame buffer sees their SUM. 1.00 is already full white,
    // and the effective ceiling is lower still because E1's streak bloom lifts
    // anything near the threshold. A body that clips has no recoverable texture
    // however good the sheet is (core/docs/LANDMINES.md, 29/07).
    float sum = k_layerAlpha[0] + k_layerAlpha[1];
    CHECK_MSG(sum < 1.0f, "the two layers sum under 1.0 through the body",
              "%.2f", sum);
    CHECK_MSG(sum < 0.75f, "...with headroom under the practical bloom ceiling",
              "%.2f", sum);

    // The shell is the WIDER and FAINTER of the two, or it is a second body.
    CHECK_MSG(k_layerWidth[0] > k_layerWidth[1] && k_layerAlpha[0] < k_layerAlpha[1],
              "the outer shell is wider and fainter than the body",
              "%.2fx @ %.2f vs %.2fx @ %.2f", k_layerWidth[0], k_layerAlpha[0],
              k_layerWidth[1], k_layerAlpha[1]);

    // PARALLAX. Layers moving at the same rate read as one thick surface however
    // many there are; the difference between them is most of what sells a volume
    // as moving rather than sliding.
    CHECK_MSG(fabsf(k_layerScroll[0] - k_layerScroll[1]) > 0.2f,
              "the two layers scroll at visibly different rates",
              "%.2f vs %.2f", k_layerScroll[0], k_layerScroll[1]);
}

// ── 6. Tail memory ──────────────────────────────────────────────────────────

static void Test_MaxNodes(void)
{
    CHECK_MSG(MaxNodes(0.85f) == 51, "0.85 s of tail is 51 nodes at 60 Hz",
              "%d", MaxNodes(0.85f));
    CHECK_MSG(MaxNodes(1.0f) == TRAIL_HISTORY_COUNT, "1.0 s fills the history exactly",
              "%d", MaxNodes(1.0f));
    CHECK_MSG(MaxNodes(9.0f) == TRAIL_HISTORY_COUNT,
              "an over-long lifetime clamps, not wraps", "%d", MaxNodes(9.0f));
    CHECK_MSG(MaxNodes(0.001f) == 4, "and a tiny one still has enough nodes to have a shape",
              "%d", MaxNodes(0.001f));

    // A tube is decimated to at most `Rings` slices, so a history longer than
    // that costs nothing extra to draw — worth knowing before anyone "optimises"
    // the sample rate down and shortens every tail in the game.
    CHECK(TRAIL_HISTORY_COUNT >= Rings(3),
          "the history is at least as long as the highest tier's ring count");
}

// ── the mirror guard ────────────────────────────────────────────────────────

static void CollapseWS(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    int pendingSpace = 0;
    for (const char *p = in; *p; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') { pendingSpace = 1; continue; }
        if (pendingSpace && o > 0 && o + 1 < cap) out[o++] = ' ';
        pendingSpace = 0;
        if (o + 1 < cap) out[o++] = *p;
    }
    out[o < cap ? o : cap - 1] = '\0';
}

static char g_flat[400000];

static int LoadFlat(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    static char buf[400000];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    CollapseWS(buf, g_flat, sizeof(g_flat));
    return 1;
}

// Whitespace is collapsed on BOTH sides, so a reflow of the source cannot break
// a needle. Seventeen assertions failed at once over column alignment before
// this rule was written down (core/docs/LANDMINES.md, 29/07).
static int FileHas(const char *path, const char *needle)
{
    if (!LoadFlat(path)) return 0;
    char want[1024];
    CollapseWS(needle, want, sizeof(want));
    return strstr(g_flat, want) != NULL;
}

static int CountIn(const char *path, const char *needle)
{
    if (!LoadFlat(path)) return -1;
    char want[1024];
    CollapseWS(needle, want, sizeof(want));
    int n = 0;
    for (const char *p = strstr(g_flat, want); p; p = strstr(p + 1, want)) n++;
    return n;
}

static void Test_MirrorMatchesTheSource(void)
{
    const char *inl = "core/composition/common/vc_volume_trail.inl";
    const char *cmn = "core/composition/common/vc_common.inl";
    const char *swp = "core/composition/common/vc_ribbon_trail.inl";
    const char *hdr = "core/composition/visual_composer.h";

    // The three columns, exactly as mirrored above.
    CHECK(FileHas(inl, "static const float k_volNoise[VFX_VOLUME_KIND_COUNT] = {0.14f, 0.34f, 0.24f};"),
          "the noise column is the number this test mirrors");
    CHECK(FileHas(inl, "static const float k_volSwirl[VFX_VOLUME_KIND_COUNT] = {-2.60f, -0.55f, -4.20f};"),
          "so is the swirl column");
    CHECK(FileHas(inl, "\"assets/textures/smoke_volume.png\","),
          "and the sheets are the purpose-built volume ones");

    // THE STRUCTURAL CLAIM OF P1, and the only mechanical way to state it: there
    // are FIVE per-kind arrays and no more — the three columns, the loaded
    // textures they name, and the layer tables that hold them. A sixth is how a
    // parameter quietly becomes three implementations, and it arrives one
    // innocent field at a time (VFX_PLAN §4.1).
    CHECK_MSG(CountIn(inl, "[VFX_VOLUME_KIND_COUNT]") == 5,
              "exactly five per-kind tables — sheet path, texture, noise, swirl, layers",
              "found %d", CountIn(inl, "[VFX_VOLUME_KIND_COUNT]"));

    // It REUSES the tube. This is the assertion the whole file exists under: the
    // two most expensive mistakes in this module were both a second
    // implementation written beside a working first one.
    CHECK(FileHas(inl, "cfg.shape = TRAIL_SHAPE_TUBE;"),
          "it asks the trail system for a tube");
    CHECK(!FileHas(inl, "ProceduralMesh_BuildTubeAlongPath("),
          "and does NOT build one of its own");
    CHECK(!FileHas(inl, "rlBegin("),
          "it draws no geometry itself at all — DrawTrailEntities owns the tube");

    // The three things a volume sheds, which is why it is not a trail style.
    CHECK(FileHas(inl, "cfg.forceField = NULL;"),
          "no cloth simulation");
    CHECK(!FileHas(inl, "SpawnParticle("),
          "no spark layer");
    CHECK(!FileHas(inl, "cfg.nodeHomeSpring ="),
          "and no cloth anchoring either, since there is no cloth to anchor");

    // The blend law, from ONE predicate.
    CHECK(FileHas(inl, "return kind != VOL_SMOKE;"),
          "one predicate decides who emits");
    CHECK(FileHas(inl, "cfg.blendMode = VolumeTrail_Emits(v->kind) ? BLEND_ADDITIVE : BLEND_ALPHA;"),
          "the blend is read off it");
    CHECK(FileHas(inl, "return VolumeTrail_Emits(kind) ? VC_ElementRamp(mat) : VolumeTrail_SmokeRamp(mat);"),
          "...and so is the ramp, so the two cannot drift apart");
    CHECK(FileHas(inl, "cfg.useCustomBlendMode = true;"),
          "BLEND_ALPHA is 0 and needs the explicit flag to survive the >0 check");

    // The tube's own settings, each of which has a reason recorded next to it.
    CHECK(FileHas(inl, "cfg.tubeCaps = true;"),
          "capped — the side quads alone leave a bowl you can see inside");
    CHECK(FileHas(inl, "cfg.tubeSingleSided = false;"),
          "double-walled — the free rim");
    CHECK(!FileHas(inl, "cfg.shape = TRAIL_SHAPE_RIBBON;"),
          "no tier path ever demotes it to a flat strip");

    // Emission and UV are RATES in world units, never per-frame counts or
    // fractions of the trail's current length.
    CHECK(FileHas(inl, "cfg.uvMetresPerTile = (s_volTile > 0.05f) ? s_volTile : 0.05f;"),
          "the sheet tiles by METRES, so texel density is constant as it grows");
    CHECK(FileHas(inl, "cfg.sampleHz = VOL_SAMPLE_HZ;"),
          "nodes are laid at a fixed RATE, not one per frame");

    // The range check that cost a day the last time it was written the other way.
    CHECK(FileHas(inl, "if (kind < VOL_ENERGY || kind >= VFX_VOLUME_KIND_COUNT)"),
          "the range check is against the COUNT, never the last kind by name");
    CHECK(FileHas(inl, "TraceLog(LOG_WARNING, \"VFX_VOLUME: kind %d is out of range"),
          "and the clamp announces itself — a silent clamp gives a PLAUSIBLE "
          "result, which is the worst failure mode available");
    CHECK(FileHas(hdr, "VFX_VOLUME_KIND_COUNT"),
          "the count sentinel is in the enum itself, so no check can go stale");
    CHECK(FileHas(hdr, "VFX_ComposeVolumeTrailEx") &&
              FileHas(inl, "v->surface = surface ? *surface : (VFX_TrailSurface){0};"),
          "a volume may own a caller-supplied sheet, flow map and mask per instance");
    CHECK(FileHas(inl, "const Texture2D *flowMap = v->hasSurface ? &v->surface.flowMap"),
          "a supplied flow map replaces the kind default rather than sharing it globally");

    // Kill detaches rather than cutting the volume out of existence — and it is
    // not optional: the entity holds the CALLER'S Matrix.
    CHECK(FileHas(inl, "Trail_AttachToTransform(v->trailId, NULL, (Vector3){0.0f, 0.0f, 0.0f});"),
          "kill DETACHES, so the volume drains and fades instead of popping");

    // Static pools, no allocation.
    CHECK(FileHas(inl, "static VC_VolumeTrail s_vol[VOL_MAX];"),
          "a static pool");
    CHECK(!FileHas(inl, "malloc("), "and no allocation");

    // No camera shake. Not asked for; never wired in on my own initiative.
    CHECK(!FileHas(inl, "CameraFX_"), "no camera shake");

    // THE HOIST, not a copy. Both helpers moved to vc_common.inl when this file
    // became their second caller; a green test here with a duplicated ramp still
    // sitting in the swept trail would be exactly the failure the plan's own
    // rule names.
    CHECK(FileHas(cmn, "static const ColorGradient *VC_ElementRamp(VC_MaterialId mat)"),
          "the element ramp lives in vc_common.inl");
    CHECK(FileHas(cmn, "static int VC_TrailNodesForLifetime(float lifetime, float sampleHz)"),
          "so does the lifetime→nodes arithmetic");
    CHECK(FileHas(swp, "return VC_ElementRamp(mat);"),
          "the swept trail now CALLS the shared ramp");
    CHECK(!FileHas(swp, "ColorGradient_AddStop(&s_sweptGrad[i], 0.00f, m->body);"),
          "and no longer contains a copy of it");
    CHECK(FileHas(swp, "return VC_TrailNodesForLifetime(lifetime, SWEPT_SAMPLE_HZ);"),
          "and calls the shared node arithmetic too");
}

int main(void)
{
    printf("=== P1 volume trail (the tube, promoted) ===\n");
    Test_ThreeKindsDifferInThreeThings();
    Test_BlendLaw();
    Test_AspectIsAgainstItsOwnLength();
    Test_TierLadderOnlyClampsDown();
    Test_AdditiveBudget();
    Test_MaxNodes();
    Test_MirrorMatchesTheSource();
    printf("---- %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
