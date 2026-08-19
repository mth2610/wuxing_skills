/* THE SCENE TARGET'S ALPHA IS UNDEFINED, AND THIS PINS THE THREE PLACES THAT
 * DEPEND ON IT.
 *
 * VFX draw into the scene target with additive and premultiplied blending, so
 * its alpha channel accumulates past 1.0 and nothing consumes it. That is fine
 * — until some later pass composites the target WITH BLENDING ON, at which
 * point every VFX region is multiplied by its own accumulated alpha (~1.5 was
 * measured) and clips to white on the 8-bit swapchain. That is the "everything
 * blows out" symptom BRIGHT_BACKGROUND_VFX_SPEC.md exists to fix, and it is one
 * missing call away at all times.
 *
 * Three things keep it away, and none of them is obvious from its own file:
 *   1. distortion.fs writes a LITERAL 1.0 alpha — this is where the undefined
 *      alpha becomes defined. Pass the sampled alpha through instead and the
 *      whole post chain inherits the accumulation.
 *   2. the bloom prefilter reads the scene with blending disabled.
 *   3. the final composite disables blending AND FLUSHES INSIDE the disabled
 *      window: rlDisableColorBlend is flush-scoped on both backends, so
 *      re-enabling before the batch is drawn hands the draw back to the
 *      blender and the guard silently does nothing.
 *
 * rlvk's `colorblend_flush` scenario pins (3) at runtime. This pins all three
 * at the source, where the reason is written down. */
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

static int FileHas(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) { printf("FAIL: cannot open %s\n", path); g_failures++; return 0; }
    static char buf[400000];
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

/* rlDisableColorBlend ... rlDrawRenderBatchActive ... rlEnableColorBlend, in
 * that order, with the flush strictly between the two toggles. */
static int FlushesInsideDisabledWindow(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) { printf("FAIL: cannot open %s\n", path); g_failures++; return 0; }
    static char buf[400000];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);

    int windows = 0, good = 0;
    const char *p = buf;
    while ((p = strstr(p, "rlDisableColorBlend()")) != NULL) {
        const char *end = strstr(p, "rlEnableColorBlend()");
        if (end == NULL) break;
        windows++;
        const char *flush = strstr(p, "rlDrawRenderBatchActive()");
        if (flush != NULL && flush < end) good++;
        p = end + 1;
    }
    if (windows == 0) { printf("FAIL: %s has no disabled-blend window at all\n", path); g_failures++; return 0; }
    if (good != windows) {
        printf("FAIL: %s — %d of %d disabled-blend windows re-enable before the flush\n",
               path, windows - good, windows);
        g_failures++;
        return 0;
    }
    printf("PASS: all %d disabled-blend windows in %s flush before re-enabling\n", windows, path);
    return 1;
}

int main(void)
{
    printf("=== scene target: alpha is undefined, and who has to know ===\n");

    Check(FileHas("core/scene_targets.h", "ALPHA IN THE SCENE TARGET IS UNDEFINED") &&
              FileHas("core/scene_targets.c", "ALPHA IN THE SCENE TARGET IS UNDEFINED"),
          "the contract is stated where the target is owned");

    /* The conversion point. Written as the exact expression so that swapping in
     * the sampled alpha — the tempting "fix" when a refraction looks wrong —
     * goes red here rather than at the far end of the post chain. */
    Check(FileHas("core/shaders/distortion.fs", "finalColor = vec4(hdr * colDiffuse.rgb, 1.0);"),
          "the distortion pass writes a LITERAL 1.0 alpha — undefined becomes defined here");

    Check(FileHas("core/post_fx.c", "rlDisableColorBlend();"),
          "post_fx composites with blending disabled");
    FlushesInsideDisabledWindow("core/post_fx.c");

    printf("---- %d failures\n", g_failures);
    return g_failures ? 1 : 0;
}
