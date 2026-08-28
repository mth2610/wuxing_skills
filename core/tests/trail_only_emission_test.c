// core headless test — the EMISSION PASS GATE and trail-only particles.
//
// SYMPTOM PAID FOR ON 28/08/2026: a converge built entirely from additive,
// trail-only threads (`render.trailOnly = 1`, the "a thread of gas, not a comet"
// authoring) rendered as NOTHING. Not dim, not thin — absent. The particles
// existed, their trail history filled, and DrawParticlesLayer's ribbon pass was
// ready to draw them; the pass was simply never called.
//
// CAUSE: main.c only runs the emission pass when
// ParticleSystem_HasAdditiveParticles() says there is something in it, and that
// predicate carried `!p->trailOnly` — the HEAD pass's rule, copied into a test
// that also governs the RIBBON pass twenty lines below it. A trailOnly particle
// draws no billboard, so by the head pass's rule it is nothing; by the ribbon
// pass's rule it is the entire effect.
//
// The comment above the predicate already warned that a mode missing from it "is
// never drawn". This test is that warning made executable, because the next
// person to add a render mode will read a test before they read a paragraph.
//
// WHAT THIS FILE CANNOT SEE: whether the ribbon then looks right. It proves the
// pass is opened, not that anything in it is beautiful — that is
// scripts/render_vfx_matrix.sh "CHARGE CONVERGE".

#include <stdio.h>
#include <string.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, name) do { \
    g_checks++; \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s\n", name); g_failures++; } \
} while (0)

// The three blend modes, in the order core/vfx_config.h declares them. Only the
// distinction "is it ALPHA" matters here.
#define BLEND_ALPHA_ 0
#define BLEND_ADDITIVE_ 1

typedef struct {
    int blendMode;
    int renderMode;      // 3 = SURFACE_INPUT, drawn by FluidSurface, never here
    int trailOnly;
    int trailLength;
    int trailHistoryCount;
} P;

// MIRROR of ParticleSystem_HasAdditiveParticles (particle_system.c). Kept as a
// mirror rather than linked because the real one lives in a translation unit
// that needs raylib and a GL context; the source guard at the bottom is what
// stops the mirror drifting.
static int HasAdditive(const P *ps, int n)
{
    for (int i = 0; i < n; i++) {
        const P *p = &ps[i];
        if (p->blendMode == BLEND_ALPHA_ || p->renderMode == 3) continue;
        if (p->trailOnly && !(p->trailLength > 0 && p->trailHistoryCount >= 2)) continue;
        return 1;
    }
    return 0;
}

// MIRROR of the ribbon pass's own admission rule in DrawParticlesLayer: a trail
// is drawn when it has points, whatever the head pass decided about the head.
static int RibbonWouldDraw(const P *p, int layerFilter)
{
    if (!(p->trailLength > 0 && p->trailHistoryCount >= 2)) return 0;
    if (layerFilter == 0 && p->blendMode != BLEND_ALPHA_) return 0;
    if (layerFilter == 1 && p->blendMode == BLEND_ALPHA_) return 0;
    return 1;
}

static void Test_TheGateAdmitsWhatTheRibbonPassWouldDraw(void)
{
    // THE BUG, AS ONE CASE: an effect made only of additive trail-only threads.
    P converge[] = {
        { BLEND_ADDITIVE_, 0, 1, 8, 5 },
        { BLEND_ADDITIVE_, 0, 1, 8, 3 },
    };
    CHECK(HasAdditive(converge, 2),
          "a converge of additive trail-only threads opens the emission pass");
    CHECK(RibbonWouldDraw(&converge[0], 1),
          "...which is exactly what the ribbon pass was waiting to draw");

    // THE INVARIANT, STATED ONCE: over every combination the two passes can
    // disagree on, the gate must never say "nothing" about a particle the
    // emission ribbon pass would draw. This is the general form of the bug;
    // the case above is only its first instance.
    int mismatches = 0;
    for (int blend = 0; blend <= 1; blend++)
      for (int rm = 0; rm <= 3; rm++)
        for (int only = 0; only <= 1; only++)
          for (int len = 0; len <= 8; len += 8)
            for (int hist = 0; hist <= 5; hist++) {
                P p = { blend, rm, only, len, hist };
                if (rm == 3) continue;   // not this pass's particle at all
                if (RibbonWouldDraw(&p, 1) && !HasAdditive(&p, 1)) mismatches++;
            }
    CHECK(mismatches == 0,
          "no particle the emission ribbon pass would draw is gated out of the pass");
}

static void Test_TheGateStillClosesWhenItShould(void)
{
    // A gate that always opens is not a fix, it is a cost: the emission pass
    // allocates and composites a full-screen layer.
    P alphaOnly[] = { { BLEND_ALPHA_, 0, 1, 8, 5 }, { BLEND_ALPHA_, 0, 0, 0, 0 } };
    CHECK(!HasAdditive(alphaOnly, 2), "alpha particles do not open the emission pass");

    P surface[] = { { BLEND_ADDITIVE_, 3, 0, 0, 0 } };
    CHECK(!HasAdditive(surface, 1),
          "nor does SURFACE_INPUT, which FluidSurface draws on its own");

    // A thread that has only just spawned has one history point and no ribbon
    // yet. Admitting it would open the pass for a frame with nothing in it.
    P justSpawned[] = { { BLEND_ADDITIVE_, 0, 1, 8, 1 } };
    CHECK(!HasAdditive(justSpawned, 1),
          "a trail-only thread with no ribbon yet does not open an empty pass");

    P none[] = { { BLEND_ALPHA_, 0, 0, 0, 0 } };
    CHECK(!HasAdditive(none, 1), "and an idle scene keeps it shut");
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
    static char buf[400000], flat[400000];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    CollapseWS(buf, flat, sizeof(flat));
    char want[1024];
    CollapseWS(needle, want, sizeof(want));
    return strstr(flat, want) != NULL;
}

static void Test_TheRealPredicateStillReadsLikeTheMirror(void)
{
    const char *ps = "core/particles/particle_system.c";

    CHECK(FileHas(ps, "if (p->blendMode == VFX_BLEND_ALPHA || p->renderMode == 3) continue;"),
          "the shipped gate skips alpha and surface particles");
    CHECK(FileHas(ps, "if (p->trailOnly && !(p->trailLength > 0 && p->trailHistoryCount >= 2)) continue;"),
          "and admits a trail-only particle once its ribbon has something to draw");
    // The exact expression that caused the vanishing. It is worth naming: a
    // future edit that "simplifies" the two lines above back into one is the
    // regression, and it will look like a tidy-up.
    CHECK(!FileHas(ps, "if (p->blendMode != VFX_BLEND_ALPHA && p->renderMode != 3 && !p->trailOnly)"),
          "the head pass's rule is NOT back in the emission gate");

    // The other side of the agreement: the ribbon pass must still draw the
    // trails the gate now admits. If someone adds a trailOnly skip there, the
    // gate becomes wrong again — in the opposite direction.
    CHECK(FileHas(ps, "if (p->trailLength > 0 && p->trailHistoryCount >= 2)"),
          "the ribbon pass still admits on history alone, not on trailOnly");
}

int main(void)
{
    printf("=== emission gate vs trail-only particles ===\n");
    Test_TheGateAdmitsWhatTheRibbonPassWouldDraw();
    Test_TheGateStillClosesWhenItShould();
    Test_TheRealPredicateStillReadsLikeTheMirror();
    printf("---- %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
