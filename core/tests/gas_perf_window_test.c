#include <math.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct Camera3D { int unused; } Camera3D;
#include "core/gas/gas_system.h"
#include "core/gas/gas_perf_internal.inl"

#define CHECK(condition, message) do { \
    if (!(condition)) { fprintf(stderr, "FAIL: %s\n", message); return 1; } \
} while (0)

static bool Near(float a, float b) {
    return fabsf(a - b) < 0.0001f;
}

int main(void) {
    GasPerfWindow window;
    GasPerfStats stats = {0};
    GasPerfWindow_Reset(&window);

    GasPerfFrameSample first = {
        .updateCpuMs = 1.0f,
        .atlasUploadCpuMs = 2.0f,
        .raymarchSubmitCpuMs = 4.0f,
        .compositeSubmitCpuMs = 0.5f,
        .simSubsteps = 1,
        .atlasUploadBytes = 256 * 128 * 4,
        .gridWidth = 20, .gridHeight = 28, .gridDepth = 20,
        .raymarchWidth = 320, .raymarchHeight = 180, .raymarchSteps = 24
    };
    GasPerfFrameSample second = first;
    second.updateCpuMs = 3.0f;
    second.atlasUploadCpuMs = 0.0f;
    second.raymarchSubmitCpuMs = 6.0f;
    second.compositeSubmitCpuMs = 1.5f;
    second.simSubsteps = 2;
    second.atlasUploadBytes = 0;

    CHECK(!GasPerfWindow_Add(&window, &first, 0.4f, &stats),
          "a sub-second window must not publish partial averages");
    CHECK(GasPerfWindow_Add(&window, &second, 0.6f, &stats),
          "one second of active frames must publish a sample");
    CHECK(stats.sampleFrames == 2, "both active frames must contribute");
    CHECK(Near(stats.updateCpuMsAvg, 2.0f), "solver/update timing must average");
    CHECK(Near(stats.atlasUploadCpuMsAvg, 1.0f), "upload timing must include zero-upload frames");
    CHECK(Near(stats.raymarchSubmitCpuMsAvg, 5.0f), "raymarch submit timing must average");
    CHECK(Near(stats.compositeSubmitCpuMsAvg, 1.0f), "composite submit timing must average");
    CHECK(Near(stats.simSubstepsAvg, 1.5f), "simulation substeps must average");
    CHECK(stats.atlasUploads == 1, "only frames that upload the atlas count");
    CHECK(stats.atlasUploadBytesTotal == 256 * 128 * 4,
          "the window must report actual uploaded bytes");
    CHECK(stats.raymarchAtlasTapUpperBound == 320u * 180u * 24u * 4u,
          "raymarch workload must expose the four atlas taps per step upper bound");
    CHECK(window.sampleFrames == 0, "publishing must reset the accumulator");

    puts("gas_perf_window_test: PASS");
    return 0;
}
