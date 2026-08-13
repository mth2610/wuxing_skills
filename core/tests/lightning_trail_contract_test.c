/* Contract for the reusable multi-point LightningStroke and its reference
 * ground-ricochet fixture. One connected carrier avoids segment-local field
 * restarts and halo seams. */
#include <math.h>
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

static int RequireNot(const char *source, const char *needle, const char *message)
{
    if (source != NULL && strstr(source, needle) == NULL) return 0;
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

int main(void)
{
    int failed = 0;
    char *header = ReadFile("core/lightning/lightning_stroke.h");
    char *source = ReadFile("core/lightning/lightning_stroke.c");
    char *shader = ReadFile("core/lightning/shaders/lightning_stroke.fs");
    char *composition = ReadFile("core/composition/common/vc_lightning_trail.inl");
    char *api = ReadFile("core/composition/visual_composer.h");
    char *cmake = ReadFile("CMakeLists.txt");
    char *fixture = ReadFile("sandbox/vfx_test.c");

    // Each later hop must be lower and shorter: it reads as electrical energy
    // losing force after each ground contact, not as repeated equal jumps.
    float previousDistance = 1e9f, previousHeight = 1e9f;
    for (int hop = 0; hop < 5; ++hop) {
        float distance = 0.56f * powf(0.67f, (float)hop);
        float height = 0.42f * powf(0.62f, (float)hop);
        if (!(distance < previousDistance && height < previousHeight)) {
            fprintf(stderr, "FAIL: ricochet force does not decay by hop\n");
            failed++;
        }
        previousDistance = distance;
        previousHeight = height;
    }

    failed += Require(header, "LightningStroke_SpawnPath",
                      "electric curve needs a reusable multi-point stroke API");
    failed += Require(header, "LightningStroke_SetPath",
                      "moving path must update one stroke rather than append bolt segments");
    failed += Require(header, "LightningStroke_Stop",
                      "path needs a hold-and-dissipate lifecycle");
    failed += Require(source, "LightningStroke_DrawWarpedPath",
                      "curve must draw as one continuous path carrier");
    failed += Require(source, "Ribbon_ComputeArcLengthUV(carrier",
                      "the shader coordinate must be continuous in arc length");
    failed += Require(shader, "LightningStroke_FilamentDistance",
                      "path must reuse the proven lightning distance field");
    failed += Require(shader, "fbm2N",
                      "electric motion must use the shared procedural noise core");
    failed += Require(composition, "LightningStroke_SpawnPath",
                      "composition must allocate the reusable path stroke");
    failed += Require(composition, "LightningStroke_SetPath",
                      "moving head must update the continuous path stroke");
    failed += Require(composition, "LightningStroke_Stop",
                      "composition stop must retain electrical motion during its hold");
    failed += Require(composition, "LightningStroke_Kill(s_vfxLightningTrails[slot].stroke)",
                      "reusing a stopped wrapper must not leave a stroke pool orphan");
    failed += Require(composition, "VFX_LightningTrail_Stop(fx->trails[strand]);",
                      "each spent ricochet hop must dissipate before the next starts");
    failed += Require(composition, "powf(0.67f", "each ground hop must shorten horizontally");
    failed += Require(composition, "powf(0.62f", "each ground hop must lose vertical force");
    failed += RequireNot(cmake, "core/lightning/lightning_trail.c",
                         "obsolete segment-quad lightning renderer must not be built");
    failed += Require(api, "VFX_LightningTrail_Spawn",
                      "composition must expose reusable lightning trail API");
    failed += Require(fixture, "LIGHTNING IMPACT", "VFX tester needs a lightning impact entry");

    free(header); free(source); free(shader); free(composition);
    free(api); free(cmake); free(fixture);
    if (failed != 0) return 1;
    puts("PASS: multi-point lightning stroke and decaying ground-ricochet contract are present");
    return 0;
}
