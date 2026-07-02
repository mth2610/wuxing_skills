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

void AutoTest_SaveScreenshot(const char *name) {
    if (!DirectoryExists("autotest_output")) {
        MakeDirectory("autotest_output");
    }
    Image img = LoadImageFromScreen();
    ExportImage(img, TextFormat("autotest_output/%s.png", name));
    UnloadImage(img);
}
