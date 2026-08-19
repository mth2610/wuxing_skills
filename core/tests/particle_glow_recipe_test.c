/* THE GLOW RECIPE — one premultiplied particle.
 *
 * This test used to pin a two-particle recipe: an additive core plus a large
 * faint halo, on the reasoning that post-process bloom cannot spread from a
 * source a few pixels across. That reasoning is correct and still recorded —
 * switching bloom off changes the frame by 0.09/255, so a spark's glow is DRAWN.
 * What was wrong was the blend.
 *
 * Measured on REF PARTICLES against a white backdrop and a dark one:
 *
 *     structure                    draws   |d| white   |d| dark
 *     one premultiplied particle     1       0.801      ~1.65
 *     dark core + emissive rim       2       0.384      ~1.66
 *     additive core + halo           2       0.132      ~1.70
 *
 * Six times additive's legibility on white, in one draw. §5.2's law does both
 * jobs at once: at high coverage the equation reduces to `src` so the particle
 * COVERS a bright background and keeps its silhouette; as coverage falls to zero
 * it becomes `src + dst` so the skirt still adds light. Additive can only add,
 * which is why it dissolves into anything already near 1.0.
 *
 * THE PROJECT'S BLEND POLICY, stated by the owner and matching the measurements:
 *   premultiplied  the default choice for anything that emits
 *   additive       the exception, for sparse light-only effects with no body
 *   alpha          only for effects that do NOT glow
 *
 * Surveyed 19/08/2026 across core/composition and skills, the tree is the
 * inverse of that: 20 additive, 8 alpha, 4 premultiplied. Migrating is per-effect
 * work — each one changes appearance and has to be measured (§11b) — so this
 * test pins the RECIPE, not the population. */
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

static int Has(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) { printf("FAIL: cannot open %s\n", path); g_failures++; return 0; }
    static char buf[600000];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    return strstr(buf, needle) != NULL;
}

static void Check(int cond, const char *why)
{
    if (cond) printf("PASS: %s\n", why);
    else { printf("FAIL: %s\n", why); g_failures++; }
}

int main(void)
{
    printf("=== the glow recipe: one premultiplied particle ===\n");
    const char *sys = "core/particles/particle_system.c";
    const char *hdr = "core/particles/particle_system.h";

    Check(Has(sys, "core.render.blendMode = VFX_BLEND_PREMULTIPLIED;"),
          "the recipe is PREMULTIPLIED — covers a bright background AND adds light");
    Check(Has(sys, "core.render.unlit = 1;"),
          "and unlit, because lighting is a multiply and an emitter must not go through it");
    Check(!Has(sys, "PARTICLE_GLOW_HALO_RADIUS"),
          "the halo companion is gone — it was compensation for using additive");
    Check(Has(hdr, "void ParticleSystem_SpawnGlow(ParticleConfig core);"),
          "the recipe is public, so an effect need not re-derive it");

    /* The glow sprite stays: a second, wider particle is still the way to author
       a deliberately broad haze, and it must not use the compact spark sprite. */
    Check(Has(sys, "float a = v * v * v;") && Has(sys, "(1.0f - r2)"),
          "the glow sprite remains (1-r^2)^3 — zero value AND slope at r=1");

    Check(Has("core/composition/common/vc_ref_particle.inl", "ParticleSystem_SpawnGlow("),
          "REF PARTICLES calls the recipe rather than spelling it out");
    Check(Has("core/composition/common/vc_ref_particle.inl", "VFX_BLEND_PREMULTIPLIED"),
          "...and keeps a premultiplied row, which is the evidence for it");

    printf("---- %d failures\n", g_failures);
    return g_failures ? 1 : 0;
}
