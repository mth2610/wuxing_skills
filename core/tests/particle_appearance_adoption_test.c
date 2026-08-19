/* DOES EACH PARTICLE SAY WHETHER IT GLOWS — and if not, did anyone decide?
 *
 * The policy layer exists and is good: core/vfx_contrast.c resolves
 * VFX_APPEARANCE_GLOW to {ADDITIVE, energy contrast, bodyOpacity 0, emission
 * 2.5, unlit} and VFX_APPEARANCE_NORMAL to {ALPHA, none, 1.0, 0, lit}. A
 * glowing particle and a solid one are one field apart, and emissiveCurve
 * carries intensity over lifetime.
 *
 * BUT NAMING AN APPEARANCE IS NOT FREE, and the first version of this test got
 * that wrong. VFXAppearance_Resolve returns the table row WHOLESALE:
 *
 *     return id == VFX_APPEARANCE_INHERIT ? legacy : s_appearances[id];
 *
 * So a site that says GLOW discards its own emissiveBoost. vc_core_glow ramps
 * that boost Math_Mix(4.0, 14.0) across its population — the ramp IS the effect
 * — and naming GLOW would flatten it to a constant 2.5. Those sites are not
 * un-migrated; they are saying something MORE SPECIFIC than any preset can.
 *
 * The real question is therefore not "did you use a preset" but "did anyone
 * DECIDE". Three outcomes per spawn site:
 *
 *   DECLARED  names render.appearance                    — decided, via policy
 *   EXPLICIT  sets blendMode / unlit / emissiveBoost      — decided, by hand
 *   OMITTED   sets none of them                           — NOT decided: falls
 *             through to INHERIT = 0 = legacy = alpha blended, lit, no
 *             emission, i.e. the NON-glowing particle, by omission.
 *
 * Only OMITTED is debt, and only it is ratcheted. Flipping the meaning of zero
 * would silently change every one of those sites, so that is not the fix; the
 * count below may only go DOWN, and each migration changes how an effect looks
 * and is measured per BRIGHT_BACKGROUND_VFX_SPEC.md §11b rather than done in
 * bulk.
 *

 * TWO WRONG COUNTS WERE PUBLISHED BEFORE THIS ONE, both from grepping whole
 * files instead of call sites. "8 of 56 declare an appearance" counted the tests
 * and the two files that READ the field. "40 undeclared" attributed a file's
 * declarations to all of its spawns. The real figure is 8 inline sites that
 * decide nothing, out of 27 judgeable ones.
 *
 * Count with this test, not with a grep. `grep -c render.appearance` over core/
 * and skills/ returns 8, which is wrong for this question — it counts the tests,
 * and counts particle_system.c and particle_manager.c, which READ the field
 * rather than set it. That mistake was made while writing this file. */
#include <stdio.h>
#include <string.h>
#include <dirent.h>

static int g_failures = 0;

/* Recorded 19/08/2026. LOWER THIS as sites decide; never raise it. */
#define OMITTED_BASELINE 8

static int g_declared = 0, g_explicit = 0, g_omitted = 0, g_indirect = 0;
static char g_worst[64][160];
static int g_worstCount = 0;

/* Scan one spawn call's brace-matched config literal. Per-SITE, not per-file:
 * a file with three spawns and one declaration cannot be judged by counting. */
static void ScanFile(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return;
    static char buf[600000];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);

    for (const char *p = buf; (p = strstr(p, "SpawnParticle")) != NULL; p += 1) {
        /* Only an INLINE config literal can be judged from the call site. The
         * other form builds a named ParticleConfig over many lines and spawns
         * it by name; deciding what that one set would mean following the
         * variable, so it is counted separately rather than guessed at. Guessing
         * is how the first version of this test reported 0 declared while
         * vc_ember_trail plainly declares one. */
        const char *a = p;
        while (*a && *a != '(' && *a != '\n') a++;
        if (*a != '(') continue;
        const char *lit = strstr(a, "(ParticleConfig){");
        if (lit == NULL || lit - a > 4) { g_indirect++; continue; }

        const char *brace = strchr(lit, '{');
        int depth = 0;
        const char *q = brace;
        for (; *q; q++) {
            if (*q == '{') depth++;
            else if (*q == '}') { depth--; if (depth == 0) { q++; break; } }
        }
        size_t len = (size_t)(q - brace);
        static char site[200000];
        if (len == 0 || len >= sizeof(site)) continue;
        memcpy(site, brace, len);
        site[len] = '\0';

        if (strstr(site, "render.appearance") != NULL) { g_declared++; }
        else if (strstr(site, "render.blendMode") != NULL ||
                 strstr(site, "render.unlit") != NULL ||
                 strstr(site, "render.emissiveBoost") != NULL) { g_explicit++; }
        else {
            g_omitted++;
            if (g_worstCount < 64)
                snprintf(g_worst[g_worstCount++], sizeof(g_worst[0]), "%s", path);
        }
        p = q - 1;
    }
}

static void Walk(const char *dir)
{
    DIR *d = opendir(dir);
    if (d == NULL) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        size_t l = strlen(e->d_name);
        int isSrc = (l > 2 && strcmp(e->d_name + l - 2, ".c") == 0) ||
                    (l > 4 && strcmp(e->d_name + l - 4, ".inl") == 0);
        if (isSrc) ScanFile(path);
        else Walk(path);            /* dirent d_type is not portable */
    }
    closedir(d);
}

int main(void)
{
    printf("=== particle appearance: did anyone DECIDE whether it glows ===\n");
    Walk("core/composition");
    Walk("skills");

    int total = g_declared + g_explicit + g_omitted;
    if (total == 0) {
        printf("FAIL: found no SpawnParticle sites at all — run from the repo root\n");
        return 1;
    }
    printf("      %d inline sites: %d declared (preset), %d explicit (by hand), "
           "%d OMITTED\n", total, g_declared, g_explicit, g_omitted);
    printf("      %d more spawn a named config — not judgeable from the call site\n",
           g_indirect);

    if (g_omitted > OMITTED_BASELINE) {
        printf("FAIL: spawns that decide NOTHING rose to %d (baseline %d).\n"
               "      Such a site inherits legacy — alpha blended and lit, the\n"
               "      NON-glowing particle. Say which you meant:\n"
               "        .render.appearance = VFX_APPEARANCE_GLOW;    // emits, blooms\n"
               "        .render.appearance = VFX_APPEARANCE_NORMAL;  // occludes, lit\n"
               "      ...or set blendMode/unlit/emissiveBoost by hand if the preset\n"
               "      cannot express it (naming one DISCARDS your own boost).\n",
               g_omitted, OMITTED_BASELINE);
        for (int i = 0; i < g_worstCount; i++) printf("        %s\n", g_worst[i]);
        g_failures++;
    } else if (g_omitted < OMITTED_BASELINE) {
        printf("PASS: undecided spawns down to %d from %d — LOWER OMITTED_BASELINE "
               "to lock the gain in\n", g_omitted, OMITTED_BASELINE);
    } else {
        printf("PASS: undecided spawns holding at the %d recorded\n", g_omitted);
    }
    printf("---- %d failures\n", g_failures);
    return g_failures ? 1 : 0;
}
