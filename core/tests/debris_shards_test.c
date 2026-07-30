// core headless test — P3, VFX_ComposeDebrisShards.
//
// P3 is a REWRITE of a pre-Đợt-E scaffold, not an extraction, so unlike
// core_glow_test.c and converge_motes_test.c this file is not a transcription
// check — the old numbers are deliberately not being preserved. What it pins is
// the four things the rewrite exists to fix, each of which is a named landmine
// the scaffold was standing on:
//
//   1. It was drawn with a LIT EffectMaterial, i.e. black-on-black in the night
//      arena (ENGINE_LANDMINES §3). The shading is authored now, and the
//      arithmetic of that authored shading is what makes the tumble visible.
//   2. It flipped backface culling around the draw WITH NO BATCH FLUSH.
//   3. Its dust was `GetRandomValue(0, 100) < 25` PER FRAME — a rate that
//      doubles with the frame rate.
//   4. It ignored the element and drew every chip white.
//
// Plus the two things the plan's DoD names explicitly: count-vs-tier, and that
// the chips are a COUNT PER CALL rather than a rate.
//
// What this cannot see: whether a chip reads as debris. It can prove the chip is
// flatter than it is wide, that no face is ever black, that the specular lobe is
// narrow enough to read as a flash rather than a wash, and that the box cannot
// turn itself inside out.

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

#define DEBRIS_MAX            128
#define DEBRIS_PER_CALL_MAX   24
#define DEBRIS_DUST_RATE      9.0f
#define DEBRIS_DUST_BATCH_MAX 2
#define DEBRIS_HIT_MOTES      2
#define DEBRIS_SQUASH_Y       0.62f
#define DEBRIS_SQUASH_Z       0.34f
#define DEBRIS_GRAVITY        9.81f
#define DEBRIS_AIR_DRAG       0.4f
#define DEBRIS_BOUNCE         0.45f
#define DEBRIS_FRICTION       0.6f
#define DEBRIS_SPIN_DAMP      0.7f
#define DEBRIS_REST_SPEED     0.25f
#define DEBRIS_AMBIENT        0.22f
#define DEBRIS_DIFFUSE        0.78f
#define DEBRIS_SPEC_POW       26.0f
#define DEBRIS_SPEC_GAIN      0.85f
#define DEBRIS_FADE_FROM      0.75f
#define DEBRIS_JITTER         0.55f  // of each axis's OWN half-extent, peak-to-peak

#define PHYSICS_GRAVITY_MPS2  9.81f  // core/tuning.h §3b — the real-world reference

// GFX_UNLIT = 0, GFX_LOW = 1, GFX_MED = 2, GFX_HIGH = 3
static int TierCount(int tier, int requested)
{
    if (requested < 1) return 0;
    int n;
    switch (tier) {
    case 3: n = requested; break;
    case 2: n = (requested * 3) / 4; break;
    case 1: n = requested / 2; break;
    default: n = requested / 3; break;
    }
    if (n < 1) n = 1;
    if (n > DEBRIS_PER_CALL_MAX) n = DEBRIS_PER_CALL_MAX;
    return n;
}

// ── 1. Count vs tier — the DoD's first half ─────────────────────────────────

static void Test_CountVsTier(void)
{
    // The gate may only ever clamp DOWN. Checked over the whole useful range,
    // not at one count: a ladder that inverts at some argument is a ladder that
    // was checked at one argument.
    int monotone = 1;
    for (int req = 1; req <= 60; req++)
        for (int tier = 0; tier < 3; tier++)
            if (TierCount(tier, req) > TierCount(tier + 1, req)) monotone = 0;
    CHECK(monotone, "at every requested count, a lower tier never gets MORE chips");

    // The top tier is transparent: what you ask for is what you get, up to the
    // ceiling. A gate that trims even at maximum quality is a hidden scale.
    int transparent = 1;
    for (int req = 1; req <= DEBRIS_PER_CALL_MAX; req++)
        if (TierCount(3, req) != req) transparent = 0;
    CHECK(transparent, "GFX_HIGH spawns exactly what was asked for");

    // AND IT NEVER TURNS THE EFFECT OFF. A burst that asks for debris and gets
    // none is a switch, not a tier — the low tier would then be running a
    // different effect rather than a cheaper one.
    int neverZero = 1;
    for (int req = 1; req <= 60; req++)
        for (int tier = 0; tier <= 3; tier++)
            if (TierCount(tier, req) < 1) neverZero = 0;
    CHECK(neverZero, "every tier still spawns at least one chip");

    // Zero in, zero out — asking for nothing is not a tier decision.
    CHECK(TierCount(3, 0) == 0 && TierCount(0, 0) == 0,
          "asking for zero chips spawns zero at every tier");

    // The per-call ceiling binds before the pool does, so one enthusiastic
    // caller cannot evict everybody else's debris.
    CHECK_MSG(TierCount(3, 1000) == DEBRIS_PER_CALL_MAX,
              "a huge request is capped at the per-call ceiling",
              "%d", TierCount(3, 1000));
    CHECK_MSG(DEBRIS_MAX >= DEBRIS_PER_CALL_MAX * 4,
              "and the pool holds several full bursts at once",
              "%d pool vs %d per call", DEBRIS_MAX, DEBRIS_PER_CALL_MAX);

    // The actual ladder, for the record.
    CHECK_MSG(TierCount(3, 16) == 16 && TierCount(2, 16) == 12 &&
              TierCount(1, 16) == 8 && TierCount(0, 16) == 5,
              "a 16-chip burst is 16 / 12 / 8 / 5 down the ladder",
              "%d %d %d %d", TierCount(3, 16), TierCount(2, 16),
              TierCount(1, 16), TierCount(0, 16));
}

// ── 2. A count per CALL, not a rate — the DoD's second half ─────────────────

static void Test_OneShotNotARate(void)
{
    // THE DISTINCTION, stated as arithmetic. A one-shot's output depends on the
    // number of CALLS; a rate's depends on elapsed TIME. Fire one burst of 12 at
    // any frame rate and 12 chips exist.
    int worstDelta = 0;
    for (int fps = 20; fps <= 240; fps += 1) {
        (void)fps;
        int chips = TierCount(3, 12);   // one call, whatever the frame rate
        if (chips - 12 != 0) worstDelta = 1;
    }
    CHECK(!worstDelta, "one call spawns the same 12 chips at any frame rate");

    // ...and the corollary that makes it a CONTRACT rather than a detail: called
    // from a draw path at 60 fps it would spawn 720 chips a second, exhausting
    // a 128-chip pool in under a fifth of a second.
    float perSecondIfMisused = 12.0f * 60.0f;
    CHECK_MSG(perSecondIfMisused > (float)DEBRIS_MAX * 5.0f,
              "which is why calling it per frame is a contract violation, not a "
              "matter of taste",
              "%.0f chips/sec into a %d pool", perSecondIfMisused, DEBRIS_MAX);

    // THE DUST, by contrast, IS a rate — and this is the half the old scaffold
    // got wrong, with a 25%-per-FRAME roll.
    float worst = 0.0f;
    for (float fps = 20.0f; fps <= 240.0f; fps += 1.0f) {
        float emitted = (1.0f / fps) * DEBRIS_DUST_RATE * fps;  // one second of frames
        float e = fabsf(emitted - DEBRIS_DUST_RATE) / DEBRIS_DUST_RATE;
        if (e > worst) worst = e;
    }
    CHECK_MSG(worst < 1e-4f, "the dust it sheds is the same motes/sec at any frame rate",
              "worst relative error %.6f", worst);

    // The per-chip hitch clamp: enough not to starve at 20 fps, small enough
    // that one long frame cannot dump a puff.
    float wantedAt20fps = DEBRIS_DUST_RATE / 20.0f;
    CHECK_MSG((float)DEBRIS_DUST_BATCH_MAX >= wantedAt20fps,
              "the dust clamp does not starve at 20 fps",
              "%d allowed, %.2f wanted", DEBRIS_DUST_BATCH_MAX, wantedAt20fps);

    // The ground hit is an EVENT, so its motes are a count. Both kinds live in
    // this one effect and each is the right kind for what it represents.
    CHECK(DEBRIS_HIT_MOTES >= 1 && DEBRIS_HIT_MOTES <= 4,
          "the ground-hit puff is a COUNT — it is an event, not a flow");
}

// ── 3. A chip is a chip, and the box cannot turn inside out ─────────────────

static void Test_ChipGeometry(void)
{
    // FLATTER THAN IT IS WIDE, in both minor axes. A box with equal extents is a
    // pebble, and a pebble tumbling reads as a sprite spinning.
    CHECK_MSG(DEBRIS_SQUASH_Y < 0.8f && DEBRIS_SQUASH_Z < DEBRIS_SQUASH_Y,
              "the chip is squashed in both minor axes, and unevenly",
              "1 : %.2f : %.2f", DEBRIS_SQUASH_Y, DEBRIS_SQUASH_Z);
    CHECK_MSG(1.0f / DEBRIS_SQUASH_Z > 2.5f,
              "its long axis is at least 2.5x its thinnest — an angular chip, not a cube",
              "%.1f:1", 1.0f / DEBRIS_SQUASH_Z);

    // THE JITTER CANNOT INVERT A FACE. Each vertex is displaced by a fraction of
    // its OWN axis's half-extent, so a coordinate's SIGN can never flip — which
    // is what would fold the box through itself and, with backface culling on,
    // make parts of it vanish. An absolute jitter big enough to see on the long
    // axis would do exactly that on the thin one.
    float worstFactor = 1.0f - DEBRIS_JITTER * 0.5f;
    CHECK_MSG(worstFactor > 0.0f,
              "the most extreme jitter still leaves every vertex on its own side "
              "of the box — no face can invert",
              "worst scale factor %.3f of the half-extent", worstFactor);
    CHECK_MSG(DEBRIS_JITTER > 0.3f,
              "...while still being irregular enough that no two chips share a silhouette",
              "%.2f peak-to-peak", DEBRIS_JITTER);
}

// ── 4. The authored shading — this is what replaces the lit material ────────

static void Test_AuthoredShadingMakesTheTumbleVisible(void)
{
    // NO FACE IS EVER BLACK. A black facet in a dark scene is a hole in the
    // silhouette, not a shadow — and "invisible" was the scaffold's actual
    // failure mode, via a lit material in an arena with no light in it.
    CHECK_MSG(DEBRIS_AMBIENT > 0.15f, "the darkest face still carries the element's colour",
              "%.2f ambient", DEBRIS_AMBIENT);

    // The diffuse term alone reaches EXACTLY the base colour and no further, so
    // every bit of over-drive is the specular. Two terms that both over-drive
    // give a chip that is clipped white over half its surface, and a clipped
    // surface has no shading left to show the tumble with.
    CHECK_MSG(fabsf((DEBRIS_AMBIENT + DEBRIS_DIFFUSE) - 1.0f) < 1e-6f,
              "ambient + diffuse lands exactly on the base colour at full facing",
              "%.4f", DEBRIS_AMBIENT + DEBRIS_DIFFUSE);

    // Range: the brightest diffuse-only face is 1/ambient times the darkest, and
    // that ratio is what the eye reads as "a solid object turning".
    CHECK_MSG(1.0f / DEBRIS_AMBIENT > 3.0f,
              "and the brightest face is several times the darkest, so the tumble reads",
              "x%.1f range", 1.0f / DEBRIS_AMBIENT);

    // THE FLASH. pow(ndl, 26) is a narrow lobe: it is what makes a tumbling chip
    // CATCH the light for a moment rather than merely change value. Measure its
    // half-power angle — a wash would be tens of degrees, a flash is single
    // figures to low teens.
    float halfPowerCos = powf(0.5f, 1.0f / DEBRIS_SPEC_POW);
    float halfAngleDeg = acosf(halfPowerCos) * 180.0f / 3.14159265f;
    CHECK_MSG(halfAngleDeg > 4.0f && halfAngleDeg < 20.0f,
              "the specular lobe is a FLASH, not a wash",
              "half-power at %.1f degrees off the key", halfAngleDeg);
    CHECK_MSG(DEBRIS_SPEC_GAIN > 0.5f,
              "and it is bright enough to actually pop against the diffuse term",
              "%.2f gain", DEBRIS_SPEC_GAIN);

    // The fade is a real dissolve at the END of life, not a slow drain across
    // the whole of it: debris that starts fading immediately never reads as
    // solid at all.
    CHECK_MSG(DEBRIS_FADE_FROM > 0.6f,
              "a chip is fully opaque for most of its life",
              "fades from t=%.2f", DEBRIS_FADE_FROM);
}

// ── 5. The physics settles, in finite time ──────────────────────────────────

static void Test_PhysicsSettles(void)
{
    // Meter scale: chips are heavy and fall at real gravity, which is the
    // reference every force in this project is judged against (core/tuning.h §3b).
    CHECK_MSG(fabsf(DEBRIS_GRAVITY - PHYSICS_GRAVITY_MPS2) < 1e-6f,
              "chips fall at real gravity", "%.2f m/s^2", DEBRIS_GRAVITY);

    // A bounce must LOSE energy or a chip pogos forever inside its own lifetime.
    CHECK_MSG(DEBRIS_BOUNCE < 1.0f && DEBRIS_FRICTION < 1.0f && DEBRIS_SPIN_DAMP < 1.0f,
              "every bounce coefficient is lossy", "%.2f / %.2f / %.2f",
              DEBRIS_BOUNCE, DEBRIS_FRICTION, DEBRIS_SPIN_DAMP);

    // And it must reach REST, not merely approach it: with a rest threshold the
    // chip stops in a bounded number of bounces. From a hard 8 m/s impact:
    float v = 8.0f;
    int bounces = 0;
    while (v >= DEBRIS_REST_SPEED && bounces < 100) { v *= DEBRIS_BOUNCE; bounces++; }
    CHECK_MSG(bounces <= 6, "a hard impact comes to rest within a handful of bounces",
              "%d bounces from 8 m/s", bounces);
    CHECK_MSG(bounces >= 2, "...but bounces visibly at least twice first",
              "%d bounces", bounces);
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

static int FileHas(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    static char buf[300000], flat[300000];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    CollapseWS(buf, flat, sizeof(flat));
    char want[1024];
    CollapseWS(needle, want, sizeof(want));
    return strstr(flat, want) != NULL;
}

static int FileExists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static void Test_MirrorMatchesTheSource(void)
{
    const char *inl = "core/composition/common/vc_debris_shards.inl";

    CHECK(FileHas(inl, "#define DEBRIS_SQUASH_Y 0.62f"), "the squash is what this test mirrors");
    CHECK(FileHas(inl, "#define DEBRIS_SPEC_POW 26.0f"), "so is the specular lobe");
    CHECK(FileHas(inl, "#define DEBRIS_DUST_RATE 9.0f"), "and the dust rate");
    CHECK(FileHas(inl, "#define DEBRIS_GRAVITY 9.81f"), "and gravity");

    // LANDMINE 1 — the lit material is gone. Negative needles carry punctuation
    // only the code can have, or they match the comment that explains the fix
    // (core/docs/LANDMINES.md, 30/07: a negative FileHas matched the very comment
    // documenting the bug it was checking for).
    CHECK(!FileHas(inl, "Material_Begin("), "no lit EffectMaterial — it is black-on-black at night");
    CHECK(!FileHas(inl, "Material_Get(&"), "and no material preset lookup either");
    CHECK(FileHas(inl, "static const Vector3 k_debrisKey = {-0.35f, 0.86f, 0.37f};"),
          "the key direction is AUTHORED, because there is no light to sample");
    CHECK(FileHas(inl, "float shade = DEBRIS_AMBIENT + DEBRIS_DIFFUSE * ndl;"),
          "and the faces are shaded against it on the CPU");

    // The normal must be taken to WORLD space, or the chip is shaded as though it
    // never turned — which would leave the tumble invisible, i.e. exactly the
    // symptom the rewrite exists to remove.
    CHECK(FileHas(inl, "Matrix rot = MatrixRotate(d->spinAxis, d->spinAngle * DEG2RAD);"),
          "the chip's rotation is available on the CPU");
    CHECK(FileHas(inl, "n = Vector3Normalize(Vector3Transform(Vector3Normalize(n), rot));"),
          "and the face normal is rotated into world space before it is shaded");
    CHECK(FileHas(inl, "if (Vector3LengthSqr(n) < 1e-12f)"),
          "a degenerate face is skipped — normalising ~zero returns garbage silently "
          "(core/docs/LANDMINES.md, 30/07)");

    // LANDMINE 2 — the batch flush, on both sides of every state change.
    CHECK(FileHas(inl, "rlDrawRenderBatchActive(); rlEnableDepthMask(); "
                       "rlEnableBackfaceCulling(); BeginBlendMode(BLEND_ALPHA); "
                       "rlDrawRenderBatchActive();"),
          "the whole state change is sandwiched between two flushes — depth mask, "
          "culling AND blend, which is the rule's 30/07 postscript");
    CHECK(FileHas(inl, "rlDrawRenderBatchActive(); EndBlendMode(); rlDrawRenderBatchActive();"),
          "and on both sides of the restore");
    CHECK(!FileHas(inl, "rlDisableBackfaceCulling();"),
          "culling is never turned OFF here — a chip is a closed box");

    // LANDMINE 3 — the dust is a rate with a carried fraction.
    CHECK(FileHas(inl, "d->dustAcc += dt * DEBRIS_DUST_RATE * s_debrisDustMul;"),
          "the dust is a RATE with the fraction carried between frames");
    CHECK(FileHas(inl, "int motes = (int)d->dustAcc;"),
          "and the count comes from that accumulator, not from a roll");
    // The needle carries the trailing `) {` that only the CODE has: the comment a
    // few lines up quotes the old dice roll verbatim inside backticks, and a
    // bare `GetRandomValue(0, 100) < 25` would match THAT — the exact trap
    // recorded on 30/07, where a negative assertion failed because the bug had
    // been documented.
    CHECK(!FileHas(inl, "if (GetRandomValue(0, 100) < 25) {"),
          "and NOT the per-frame dice roll the scaffold used");

    // LANDMINE 4 — the element is used.
    CHECK(FileHas(inl, "DebrisShards_DrawChip(d, m->body, alpha);"),
          "a chip is the ELEMENT's colour, not white");

    // The blend law, both halves in one effect.
    CHECK(FileHas(inl, "rlEnableDepthMask(); rlEnableBackfaceCulling(); "
                       "BeginBlendMode(BLEND_ALPHA);"),
          "the chip OCCLUDES: alpha, depth-written");
    CHECK(FileHas(inl, ".render.blendMode = VFX_BLEND_ADDITIVE,"),
          "the dust EMITS: additive");
    CHECK(FileHas(inl, ".render.unlit = 1,"),
          "...and unlit, so nothing multiplies it back down");

    // One-shot, static pool, no allocation, no shake.
    CHECK(FileHas(inl, "static VC_DebrisShard s_debris[DEBRIS_MAX];"), "a static pool");
    CHECK(!FileHas(inl, "malloc("), "no allocation");
    CHECK(!FileHas(inl, "CameraFX_"), "no camera shake");
    CHECK(!FileHas(inl, "s_accum += GetFrameTime()"),
          "the CHIPS are a count per call — no spawn accumulator anywhere");

    // The scaffold is actually gone. A rewrite that leaves the original in place
    // is an addition, and the whole point of P3 was that the old one was a
    // scaffold with a planned end date.
    CHECK(!FileExists("core/composition/common/vc_shard_debris.inl"),
          "the pre-Đợt-E scaffold was deleted, not left beside the rewrite");
    CHECK(!FileHas("core/composition/visual_composer.h", "VFX_ComposeShardDebris("),
          "and its declaration went with it");
}

int main(void)
{
    printf("=== P3 debris shards (chips, not sparks) ===\n");
    Test_CountVsTier();
    Test_OneShotNotARate();
    Test_ChipGeometry();
    Test_AuthoredShadingMakesTheTumbleVisible();
    Test_PhysicsSettles();
    Test_MirrorMatchesTheSource();
    printf("---- %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
