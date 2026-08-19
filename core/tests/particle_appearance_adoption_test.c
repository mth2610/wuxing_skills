/* HOW A PARTICLE DECLARES WHETHER IT GLOWS — and how far that has spread.
 *
 * The policy layer already exists and is good: core/vfx_contrast.c's table
 * resolves VFX_APPEARANCE_GLOW to {ADDITIVE, energy contrast, bodyOpacity 0,
 * emission 2.5, unlit} and VFX_APPEARANCE_NORMAL to {ALPHA, none, 1.0, 0, lit}.
 * A glowing particle and a solid one are one field apart:
 *
 *     cfg.render.appearance = VFX_APPEARANCE_GLOW;    // emits; unlit; blooms
 *     cfg.render.appearance = VFX_APPEARANCE_NORMAL;  // occludes; gets lit
 *     cfg.emissiveCurve     = &curve;                 // intensity over lifetime
 *
 * WHAT THIS TEST IS FOR. Measured 19/08/2026: of the 41 SpawnParticle sites in
 * core/composition and skills, exactly ONE names an appearance
 * (vc_ember_trail.inl's halo). The other 40 fall through to
 * VFX_APPEARANCE_INHERIT, which is zero and therefore means "legacy" — alpha
 * blended, lit, no emission. So the effective default is the NON-glowing
 * particle, which is the opposite of the intended one, and it is a default by
 * omission rather than by decision.
 *
 * Count it with this test, not with a grep: `grep -c render.appearance` over
 * core/ and skills/ returns 8, which is wrong for this question — it counts the
 * tests, and it counts particle_system.c and particle_manager.c, which READ the
 * field rather than set it. That mistake was made while writing this file.
 *
 * Flipping the meaning of zero would silently change every one of those sites,
 * so it is not the fix. The fix is that the number below only ever goes DOWN.
 * This is a ratchet: it records the debt, fails if it grows, and asks whoever
 * pays some of it off to lower the number. It deliberately does NOT demand zero
 * — each migration changes how an effect looks and has to be measured
 * (BRIGHT_BACKGROUND_VFX_SPEC.md §11b), which is not a thing to do in bulk. */
#include <stdio.h>
#include <string.h>
#include <dirent.h>

static int g_failures = 0;

/* Recorded 19/08/2026. LOWER THIS as sites migrate; never raise it. */
#define UNDECLARED_BASELINE 40

static int CountIn(const char *path, const char *needle, int *outSpawns)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return 0;
    static char buf[500000];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    int c = 0;
    for (const char *p = buf; (p = strstr(p, needle)) != NULL; p += 1) c++;
    if (outSpawns) {
        int s = 0;
        for (const char *p = buf; (p = strstr(p, "SpawnParticle(")) != NULL; p += 1) s++;
        *outSpawns = s;
    }
    return c;
}

static void Walk(const char *dir, int *spawns, int *declared)
{
    DIR *d = opendir(dir);
    if (d == NULL) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        size_t l = strlen(e->d_name);
        int isSrc = (l > 2 && (strcmp(e->d_name + l - 2, ".c") == 0)) ||
                    (l > 4 && (strcmp(e->d_name + l - 4, ".inl") == 0));
        if (isSrc) {
            int s = 0;
            int a = CountIn(path, "render.appearance", &s);
            *spawns += s;
            *declared += (a < s) ? a : s;   /* a site cannot declare twice */
        } else {
            Walk(path, spawns, declared);   /* dirent d_type is not portable */
        }
    }
    closedir(d);
}

int main(void)
{
    printf("=== particle appearance: is a particle SAYING whether it glows ===\n");
    int spawns = 0, declared = 0;
    Walk("core/composition", &spawns, &declared);
    Walk("skills", &spawns, &declared);

    if (spawns == 0) {
        printf("FAIL: found no SpawnParticle sites at all — run from the repo root\n");
        return 1;
    }
    int undeclared = spawns - declared;
    printf("      %d SpawnParticle sites, %d declare an appearance, %d do not\n",
           spawns, declared, undeclared);

    if (undeclared > UNDECLARED_BASELINE) {
        printf("FAIL: undeclared particle spawns rose to %d (baseline %d).\n"
               "      A new SpawnParticle without render.appearance inherits the LEGACY\n"
               "      default — alpha blended and lit, i.e. the NON-glowing particle.\n"
               "      Say which one you meant:\n"
               "        cfg.render.appearance = VFX_APPEARANCE_GLOW;   // emits, blooms\n"
               "        cfg.render.appearance = VFX_APPEARANCE_NORMAL; // occludes, lit\n",
               undeclared, UNDECLARED_BASELINE);
        g_failures++;
    } else if (undeclared < UNDECLARED_BASELINE) {
        printf("PASS: undeclared spawns down to %d from a baseline of %d — "
               "LOWER UNDECLARED_BASELINE in this file to lock the gain in\n",
               undeclared, UNDECLARED_BASELINE);
    } else {
        printf("PASS: undeclared spawns holding at the %d recorded\n", undeclared);
    }
    printf("---- %d failures\n", g_failures);
    return g_failures ? 1 : 0;
}
