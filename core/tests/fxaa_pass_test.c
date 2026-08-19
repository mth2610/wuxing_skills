// Headless contract test — the FXAA resolve pass.
//
// The scene rasterises into an OFFSCREEN HDR render target, not the swapchain, so
// raylib's FLAG_MSAA_4X_HINT cannot antialias it: that hint applies to the window's own
// framebuffer. Every geometric silhouette in the game therefore landed with binary
// coverage — measured on the ShieldShell, a +64 luma step in ONE pixel between the last
// background pixel and the first shell pixel, with a smooth bloom ramp on either side of
// it; and confirmed on geometry with nothing to do with any effect, the map's own ellipse
// staircased identically. This pass is the fix.
#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(c, n) do { if (c) printf("PASS: %s\n", n); else { printf("FAIL: %s\n", n); failures++; } } while (0)

static int Has(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    static char text[200000];
    size_t count = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[count] = '\0';
    return strstr(text, needle) != NULL;
}

/* Mirror of the shader's edge test. The mirror cannot validate the blend itself (that is
   a texture-fetch path), but the THRESHOLD is where FXAA either eats detail or does
   nothing, and it is pure arithmetic. */
static int FxaaWouldFilter(float lumaMin, float lumaMax)
{
    float range = lumaMax - lumaMin;
    float relative = lumaMax * (1.0f / 16.0f);
    float absolute = 1.0f / 32.0f;
    float threshold = relative > absolute ? relative : absolute;
    return range >= threshold;
}

int main(void)
{
    const char *fs = "core/shaders/fxaa.fs";

    /* A shader missing from CMakeLists does not fail — LoadShader falls back to raylib's
       default and the pass keeps "working" as a plain blit. Same failure shape as the
       bloom and trail shaders before it (see core/docs/LANDMINES.md). */
    CHECK(Has("CMakeLists.txt", "configure_file(core/shaders/fxaa.fs"),
          "the FXAA shader is copied into desktop build trees");

    CHECK(Has("core/post_fx.c", "fxaaShader = LoadShader(0, \"core/shaders/fxaa.fs\")"),
          "post FX loads the FXAA shader");
    CHECK(Has("core/post_fx.c", "Tuning_RegisterFloat(\"postfx_fxaa\""),
          "FXAA can be turned off from tuning.cfg for an A/B");

    /* The texel size must come from the LIVE target, never from the window: the two differ
       under rlvk on Android, where the swapchain is the display's native resolution while
       the offscreen targets are the logical size. A hardcoded or window-derived texel
       makes the filter sample the wrong neighbours at exactly the resolutions nobody
       tests on. Same rule as the bloom composite's u_bloomTexel. */
    CHECK(Has("core/post_fx.c", "1.0f / (float)ldrTex.texture.width"),
          "the FXAA texel size comes from the live target, not the window size");

    /* `postfx_fxaa = 0` has to be a true A/B against the previous output, which means the
       composite still goes straight to the swapchain in that case rather than through a
       second, differently-sized draw. */
    CHECK(Has("core/post_fx.c", "if (useFxaa) BeginTextureMode(ldrTex);") &&
          Has("core/post_fx.c", "useFxaa ? (float)width : (float)GetRenderWidth()"),
          "with FXAA off the composite still targets the swapchain directly");

    /* RT->RT full-frame convention. A positive source height here is an upside-down
       frame; the expensive mistake is the PARTIAL-rect case, where the flip also needs a
       mirrored destination — that one shipped silently for months in
       SceneTargets_SnapshotDepth. This draw is full-frame, so the flip alone is right. */
    CHECK(Has("core/post_fx.c", "-(float)ldrTex.texture.height"),
          "the FXAA resolve keeps the RT->RT vertical-flip convention");

    /* It runs on tone-mapped values. FXAA thresholds on perceptual luma; on linear HDR a
       40:1 range is a modest perceptual one, so it would smear highlights and ignore
       shadow edges. */
    CHECK(Has(fs, "0.299") && Has(fs, "0.587") && Has(fs, "0.114"),
          "FXAA weights luma perceptually, because it runs after the tone map");
    CHECK(!Has(fs, "0.2126"),
          "FXAA does not use linear-luminance weights");

    /* The +-8 texel clamp is what keeps a one-pixel-wide bright feature — which is
       exactly what a shell rim is — from having unrelated colour dragged into it. */
    CHECK(Has(fs, "vec2(-8.0), vec2(8.0)"),
          "the search direction is clamped, so thin bright features are not eaten");

    /* Overshoot guard: the wide four-tap average can leave the local luma range, which
       shows as a fringe hugging the edge. */
    CHECK(Has(fs, "(lB < lMin || lB > lMax) ? rgbA : rgbB"),
          "the wide average falls back to the narrow one when it overshoots");

    /* Behavioural: the threshold must reject gentle gradients and near-black noise, and
       accept a real silhouette. The measured ShieldShell edge is 188 -> 252 of 255. */
    CHECK(FxaaWouldFilter(188.0f / 255.0f, 252.0f / 255.0f),
          "a +64/255 silhouette step is filtered");
    CHECK(!FxaaWouldFilter(0.500f, 0.520f),
          "a gentle mid-tone gradient is left alone (bloom halos, sky ramps)");
    CHECK(!FxaaWouldFilter(0.004f, 0.020f),
          "near-black 8-bit noise is left alone, or it would shimmer every frame");
    /* The absolute floor is what makes the near-black case pass: without it the relative
       test alone would call 0.004 -> 0.020 an edge. */
    {
        float relativeOnly = 0.020f * (1.0f / 16.0f);
        CHECK((0.020f - 0.004f) > relativeOnly && !FxaaWouldFilter(0.004f, 0.020f),
              "the absolute floor, not the relative test, is what rejects dark noise");
    }

    return failures ? 1 : 0;
}
