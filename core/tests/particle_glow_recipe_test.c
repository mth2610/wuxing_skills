/* THE GLOW RECIPE, pinned where it can be read.
 *
 * A glowing particle is TWO particles, and the reason is a property of the
 * pipeline rather than a style choice: post-process bloom spreads in proportion
 * to the SIZE of what feeds it, so a core a few pixels across is sub-pixel by
 * the third pyramid level and the deep levels that would carry a wide haze get
 * nothing. Measured on REF PARTICLES, switching bloom off changes the frame by
 * 0.09/255. The glow around a spark is DRAWN.
 *
 * ParticleSystem_SpawnGlow is that recipe. This test pins the three ratios and
 * the sprite choice, because each of them was measured and each is easy to
 * "tidy" into something worse:
 *
 *   radius x4.20  small enough to belong to the core, large enough to read
 *   alpha  x0.24  atmosphere, not a second core
 *   boost  x0.30  a BRIGHT halo erases the core it frames — measured, §7.6c:
 *                 the dark-core row's legibility fell to a third at mid boost
 *   sprite = ParticleSystem_GlowSprite(), never the core's spark sprite. The
 *            spark is a Gaussian under a smoothstep window; scaled up it reads
 *            as a DISC WITH AN EDGE. The glow sprite is (1-r^2)^3, which is
 *            exactly zero at r=1 with ZERO SLOPE, so its quad cannot show. */
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
    printf("=== the glow recipe: a spark is two particles ===\n");
    const char *sys = "core/particles/particle_system.c";
    const char *hdr = "core/particles/particle_system.h";

    Check(Has(sys, "#define PARTICLE_GLOW_HALO_RADIUS 4.20f"), "halo radius ratio is 4.20");
    Check(Has(sys, "#define PARTICLE_GLOW_HALO_ALPHA  0.24f"), "halo alpha ratio is 0.24");
    Check(Has(sys, "#define PARTICLE_GLOW_HALO_BOOST  0.30f"),
          "halo boost ratio is 0.30 — a brighter halo erases its own core");
    Check(Has(sys, "halo.render.texture = ParticleSystem_GlowSprite();"),
          "the halo takes the GLOW sprite, not the core's spark sprite");
    Check(Has(sys, "ParticleConfig halo = core;"),
          "and inherits the core's motion, life and curves, so the pair dies as one");

    /* The glow sprite's kernel. Written as the exact expression: a Gaussian or a
       Lorentzian here would not reach zero at the quad edge, and the cut is what
       produced the visible rim this whole thread started from. */
    Check(Has(sys, "float a = v * v * v;") && Has(sys, "(1.0f - r2)"),
          "the glow sprite is (1-r^2)^3 — zero value AND zero slope at r=1");

    Check(Has(hdr, "void ParticleSystem_SpawnGlow(ParticleConfig core);"),
          "the recipe is public, so an effect need not re-derive it");

    /* The reference fixture must keep exercising the shipped recipe, or it stops
       being evidence for it. */
    Check(Has("core/composition/common/vc_ref_particle.inl", "ParticleSystem_SpawnGlow("),
          "REF PARTICLES calls the recipe rather than spelling it out");

    printf("---- %d failures\n", g_failures);
    return g_failures ? 1 : 0;
}
