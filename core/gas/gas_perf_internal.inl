#ifndef WUXING_GAS_PERF_INTERNAL_INL
#define WUXING_GAS_PERF_INTERNAL_INL

#include <string.h>

typedef struct GasPerfFrameSample {
    float updateCpuMs;
    float atlasUploadCpuMs;
    float raymarchSubmitCpuMs;
    float compositeSubmitCpuMs;
    int simSubsteps;
    unsigned int atlasUploadBytes;
    int gridWidth;
    int gridHeight;
    int gridDepth;
    int raymarchWidth;
    int raymarchHeight;
    int raymarchSteps;
} GasPerfFrameSample;

typedef struct GasPerfWindow {
    double elapsedSeconds;
    double updateCpuMsTotal;
    double atlasUploadCpuMsTotal;
    double raymarchSubmitCpuMsTotal;
    double compositeSubmitCpuMsTotal;
    unsigned int simSubstepsTotal;
    unsigned int sampleFrames;
    unsigned int atlasUploads;
    unsigned long long atlasUploadBytesTotal;
} GasPerfWindow;

static void GasPerfWindow_Reset(GasPerfWindow *window)
{
    if (window != NULL) memset(window, 0, sizeof(*window));
}

static bool GasPerfWindow_Add(GasPerfWindow *window,
                              const GasPerfFrameSample *sample,
                              float frameSeconds,
                              GasPerfStats *published)
{
    if (window == NULL || sample == NULL || published == NULL) return false;

    window->elapsedSeconds += frameSeconds > 0.0f ? frameSeconds : 0.0f;
    window->updateCpuMsTotal += sample->updateCpuMs;
    window->atlasUploadCpuMsTotal += sample->atlasUploadCpuMs;
    window->raymarchSubmitCpuMsTotal += sample->raymarchSubmitCpuMs;
    window->compositeSubmitCpuMsTotal += sample->compositeSubmitCpuMs;
    window->simSubstepsTotal += (unsigned int)(sample->simSubsteps > 0
                                               ? sample->simSubsteps : 0);
    window->sampleFrames++;
    if (sample->atlasUploadBytes > 0) {
        window->atlasUploads++;
        window->atlasUploadBytesTotal += sample->atlasUploadBytes;
    }

    if (window->elapsedSeconds < 1.0 || window->sampleFrames == 0) return false;

    float invFrames = 1.0f / (float)window->sampleFrames;
    *published = (GasPerfStats){
        .updateCpuMsAvg = (float)window->updateCpuMsTotal * invFrames,
        .atlasUploadCpuMsAvg = (float)window->atlasUploadCpuMsTotal * invFrames,
        .raymarchSubmitCpuMsAvg = (float)window->raymarchSubmitCpuMsTotal * invFrames,
        .compositeSubmitCpuMsAvg = (float)window->compositeSubmitCpuMsTotal * invFrames,
        .simSubstepsAvg = (float)window->simSubstepsTotal * invFrames,
        .sampleFrames = window->sampleFrames,
        .atlasUploads = window->atlasUploads,
        .atlasUploadBytesTotal = window->atlasUploadBytesTotal,
        .gridWidth = sample->gridWidth,
        .gridHeight = sample->gridHeight,
        .gridDepth = sample->gridDepth,
        .raymarchWidth = sample->raymarchWidth,
        .raymarchHeight = sample->raymarchHeight,
        .raymarchSteps = sample->raymarchSteps,
        .raymarchAtlasTapUpperBound =
            (unsigned long long)(sample->raymarchWidth > 0 ? sample->raymarchWidth : 0) *
            (unsigned long long)(sample->raymarchHeight > 0 ? sample->raymarchHeight : 0) *
            (unsigned long long)(sample->raymarchSteps > 0 ? sample->raymarchSteps : 0) * 4ull
    };
    GasPerfWindow_Reset(window);
    return true;
}

#endif
