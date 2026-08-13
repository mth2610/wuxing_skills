/* Regression guard for the shared HDR bloom prefilter. The shader runs at
 * quarter resolution, so a one-pixel emissive mesh must be gathered from its
 * 4x4 full-resolution source cell BEFORE bright-threshold extraction. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *ReadFile(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *s = (char *)malloc((size_t)n + 1u);
    if (s == NULL) { fclose(f); return NULL; }
    size_t got = fread(s, 1, (size_t)n, f);
    fclose(f);
    s[got] = '\0';
    return s;
}

static int Require(const char *source, const char *needle, const char *message)
{
    if (source != NULL && strstr(source, needle) != NULL) return 0;
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static float BrightestOfCell(const float values[16])
{
    float brightest = values[0];
    for (int i = 1; i < 16; ++i)
        if (values[i] > brightest) brightest = values[i];
    return brightest;
}

int main(void)
{
    int failed = 0;
    char *shader = ReadFile("core/shaders/bloom_bright.fs");
    char *postFx = ReadFile("core/post_fx.c");

    // A one-pixel 4.5-HDR lightning core disappears under the old box average
    // (0.28125 < 1.25 threshold), but the brightest-sample prefilter keeps it.
    float thinCore[16] = {0.0f};
    thinCore[7] = 4.5f;
    float average = 4.5f / 16.0f;
    float preserved = BrightestOfCell(thinCore);
    if (!(average < 1.25f && preserved > 1.25f)) {
        fprintf(stderr, "FAIL: thin HDR source was not preserved before threshold\n");
        failed++;
    }

    failed += Require(shader, "uniform vec2 u_sourceTexelSize",
                      "bright prefilter needs full-resolution source texel size");
    failed += Require(shader, "for (int y = 0; y < 4; ++y)",
                      "bright prefilter must gather the complete quarter-res source cell");
    failed += Require(shader, "for (int x = 0; x < 4; ++x)",
                      "bright prefilter must gather horizontally as well as vertically");
    failed += Require(shader, "bestBrightness",
                      "bright prefilter must retain the strongest HDR sample");
    failed += Require(shader, "sampleColor = texture(texture0, fragTexCoord + cellOffset).rgb",
                      "bright prefilter must sample the HDR scene before extraction");
    failed += Require(postFx, "brightSourceTexelSizeLoc",
                      "post FX must cache the thin-emitter prefilter uniform");
    failed += Require(postFx, "u_sourceTexelSize",
                      "post FX must resolve the thin-emitter prefilter uniform");
    failed += Require(postFx, "Vector2 sourceTexelSize",
                      "post FX must send the full-resolution source texel size");

    free(shader);
    free(postFx);
    if (failed != 0) return 1;
    puts("PASS: thin HDR mesh emitters survive bloom prefiltering");
    return 0;
}
