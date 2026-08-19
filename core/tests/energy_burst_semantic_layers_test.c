/* Source contract: Energy Burst authors matter and radiance as populations. */
#include <stdio.h>
#include <string.h>

static int Has(const char *needle)
{
    FILE *file = fopen("core/composition/common/vc_energy_burst.inl", "rb");
    char buffer[1024];
    size_t used = 0, count;
    if (file == NULL) return 0;
    while ((count = fread(buffer + used, 1, sizeof(buffer) - 1 - used, file)) != 0) {
        used += count;
        buffer[used] = '\0';
        if (strstr(buffer, needle) != NULL) { fclose(file); return 1; }
        if (used > 256) { memmove(buffer, buffer + used - 256, 256); used = 256; }
    }
    fclose(file);
    return 0;
}

static int g_failures = 0;

/* Per-needle reporting. This suite used to accumulate a counter and print one
 * bare "FAIL", so finding out WHICH claim broke meant re-deriving every needle
 * by hand — which is how it sat red for a week. */
static void Require(const char *needle, const char *why)
{
    if (Has(needle)) printf("PASS: %s\n", why);
    else { printf("FAIL: %s  [missing: %s]\n", why, needle); g_failures++; }
}
static void Forbid(const char *needle, const char *why)
{
    if (!Has(needle)) printf("PASS: %s\n", why);
    else { printf("FAIL: %s  [still present: %s]\n", why, needle); g_failures++; }
}

int main(void)
{
    printf("=== energy burst: matter and radiance are populations ===\n");
    /* The five layered-annulus claims that used to open this suite went with
     * core/vfx_layered_field (deleted 19/08/2026): that system was never wired
     * into the build, and vc_energy_burst.inl was rewritten off it on 11/08.
     * What remains is what the burst still actually guarantees. */
    /* PREMULTIPLIED, not ALPHA. Changed deliberately by e7f5833 / 92df536
     * ("the bands were the BLEND MODE — pure light belongs in emission"): pure
     * light composited with straight alpha is what produced the banding. This
     * assertion asked for ALPHA for a week after the fix landed, i.e. it was
     * defending the defect. */
    Require(".render.blendMode = VFX_BLEND_PREMULTIPLIED",
            "emission composites premultiplied, not straight-alpha");
    Require(".render.contrastProfile = VFX_CONTRAST_ENERGY", "and carries the energy profile");
    Forbid("ParticleConfig transitionBody =", "no hand-rolled transition body");
    Forbid("ParticleConfig coreRadiance =", "no hand-rolled core radiance");
    Forbid("Random01() <", "no per-frame coin flip in the layer split");
    printf("---- %d failures\n", g_failures);
    return g_failures ? 1 : 0;
}
