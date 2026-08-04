#include "sandbox/auto_test.h"
#include "raylib.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define AUTOTEST_MAX_CASES 32
#define AUTOTEST_DEFAULT_MAX_FRAMES 300 // 5s of simulated time at fixed 1/60 dt

typedef struct {
    char name[64];
    AutoTestStepFn step;
    int maxFrames;
    int frameInCase;
    AutoTestResult result;
    char reason[128];
} AutoTestCase;

static AutoTestCase s_cases[AUTOTEST_MAX_CASES];
static int s_caseCount = 0;
static int s_currentIndex = 0;
static int s_passCount = 0;
static bool s_finished = false;

static bool s_enabledChecked = false;
static bool s_enabled = false;

bool AutoTest_IsEnabled(void) {
    if (!s_enabledChecked) {
        s_enabled = (getenv("WUXING_AUTOTEST") != NULL);
        s_enabledChecked = true;
    }
    return s_enabled;
}

void AutoTest_Register(const char *name, AutoTestStepFn step, int maxFrames) {
    if (s_caseCount >= AUTOTEST_MAX_CASES) {
        TraceLog(LOG_WARNING, "[AUTOTEST] Register(\"%s\") dropped — AUTOTEST_MAX_CASES (%d) reached", name, AUTOTEST_MAX_CASES);
        return;
    }
    AutoTestCase *c = &s_cases[s_caseCount++];
    strncpy(c->name, name, sizeof(c->name) - 1);
    c->name[sizeof(c->name) - 1] = '\0';
    c->step = step;
    c->maxFrames = (maxFrames > 0) ? maxFrames : AUTOTEST_DEFAULT_MAX_FRAMES;
    c->frameInCase = 0;
    c->result = AUTOTEST_RUNNING;
    c->reason[0] = '\0';
}

static void FinishCurrentCase(AutoTestCase *c, AutoTestResult result, const char *reason) {
    c->result = result;
    if (reason != NULL) {
        strncpy(c->reason, reason, sizeof(c->reason) - 1);
        c->reason[sizeof(c->reason) - 1] = '\0';
    }
    if (result == AUTOTEST_PASS) {
        s_passCount++;
        TraceLog(LOG_INFO, "[AUTOTEST] %s: PASS", c->name);
    } else {
        TraceLog(LOG_INFO, "[AUTOTEST] %s: FAIL - %s", c->name, c->reason);
    }
    s_currentIndex++;
}

void AutoTest_RunFrame(void) {
    if (s_finished || s_currentIndex >= s_caseCount) {
        s_finished = true;
        return;
    }

    AutoTestCase *c = &s_cases[s_currentIndex];
    char reason[128] = {0};
    AutoTestResult result = c->step(c->frameInCase, reason, sizeof(reason));

    if (result == AUTOTEST_PASS || result == AUTOTEST_FAIL) {
        FinishCurrentCase(c, result, reason);
        return;
    }

    // Still RUNNING.
    c->frameInCase++;
    if (c->frameInCase > c->maxFrames) {
        snprintf(reason, sizeof(reason), "timeout after %d frames", c->maxFrames);
        FinishCurrentCase(c, AUTOTEST_FAIL, reason);
    }
}

bool AutoTest_IsFinished(void) {
    return s_finished || (s_currentIndex >= s_caseCount);
}

int AutoTest_GetExitCode(void) {
    return (s_passCount == s_caseCount) ? 0 : 1;
}

void AutoTest_PrintSummary(void) {
    TraceLog(LOG_INFO, "[AUTOTEST] SUMMARY: %d/%d passed", s_passCount, s_caseCount);
    TraceLog(LOG_INFO, "[AUTOTEST] RESULT: %s", (s_passCount == s_caseCount) ? "PASS" : "FAIL");
}

bool AutoTest_ExpectTrue(bool cond, const char *desc, char *outReason, int outReasonSize) {
    if (!cond) {
        snprintf(outReason, outReasonSize, "expected true: %s", desc);
    }
    return cond;
}

bool AutoTest_ExpectFloatNear(float actual, float expected, float tol, const char *desc, char *outReason, int outReasonSize) {
    bool ok = fabsf(actual - expected) <= tol;
    if (!ok) {
        snprintf(outReason, outReasonSize, "%s: expected %.3f +/- %.3f, got %.3f", desc, expected, tol, actual);
    }
    return ok;
}

void AutoTest_SaveScreenshotWorld(const char *name, Camera3D cam, Vector3 center,
                                  float radius) {
    if (!DirectoryExists("autotest_output")) {
        MakeDirectory("autotest_output");
    }
    /* Project six points on the bounding sphere rather than the centre alone:
     * the centre plus a radius in screen units assumes the projection is
     * isotropic, and under a tilted isometric camera it is not. */
    const Vector3 probe[6] = {
        {center.x + radius, center.y, center.z}, {center.x - radius, center.y, center.z},
        {center.x, center.y + radius, center.z}, {center.x, center.y - radius, center.z},
        {center.x, center.y, center.z + radius}, {center.x, center.y, center.z - radius},
    };
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (int i = 0; i < 6; i++) {
        Vector2 s = GetWorldToScreen(probe[i], cam);
        if (s.x < minX) minX = s.x;
        if (s.x > maxX) maxX = s.x;
        if (s.y < minY) minY = s.y;
        if (s.y > maxY) maxY = s.y;
    }
    const float pad = 24.0f; /* a little background, so the edge has something to be an edge AGAINST */
    minX -= pad; minY -= pad; maxX += pad; maxY += pad;

    int sw = GetScreenWidth(), sh = GetScreenHeight();
    if (minX < 0.0f) minX = 0.0f;
    if (minY < 0.0f) minY = 0.0f;
    if (maxX > (float)sw) maxX = (float)sw;
    if (maxY > (float)sh) maxY = (float)sh;
    if (maxX - minX < 8.0f || maxY - minY < 8.0f) {
        /* Off screen or degenerate — save the whole frame rather than nothing,
         * and say so, because an empty crop and a missing effect look alike. */
        TraceLog(LOG_WARNING,
                 "[AUTOTEST] '%s': effect projects to %.0fx%.0f px — saving the "
                 "full frame instead. Is it behind the camera?",
                 name, (double)(maxX - minX), (double)(maxY - minY));
        AutoTest_SaveScreenshot(name);
        return;
    }

    Image img = LoadImageFromScreen();
    ImageCrop(&img, (Rectangle){minX, minY, maxX - minX, maxY - minY});
    ExportImage(img, TextFormat("autotest_output/%s.png", name));
    TraceLog(LOG_INFO, "[AUTOTEST] '%s' -> autotest_output/%s.png  (%dx%d px, cropped)",
             name, name, img.width, img.height);
    UnloadImage(img);
}

void AutoTest_SaveScreenshot(const char *name) {
    if (!DirectoryExists("autotest_output")) {
        MakeDirectory("autotest_output");
    }
    Image img = LoadImageFromScreen();
    ExportImage(img, TextFormat("autotest_output/%s.png", name));
    UnloadImage(img);
}
