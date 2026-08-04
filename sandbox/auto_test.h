#ifndef SANDBOX_AUTO_TEST_H
#define SANDBOX_AUTO_TEST_H

#include "raylib.h"

#include <stdbool.h>

// Automated self-test harness, active only when the env var WUXING_AUTOTEST
// is set. Lets any module register a test case from its own Init*() call
// site (no central registry file) using the same Update(dt)-style
// state-machine idiom skills already use — see sandbox/CLAUDE.md for the
// full convention and an example.

typedef enum {
    AUTOTEST_RUNNING = 0,
    AUTOTEST_PASS,
    AUTOTEST_FAIL
} AutoTestResult;

// Called once per autotest frame for the currently-active case.
// frameInCase == 0 means "just activated this frame" (do setup/trigger
// here). Return RUNNING to keep polling next frame, PASS/FAIL to finish
// this case immediately. On FAIL, write a short reason into outReason.
typedef AutoTestResult (*AutoTestStepFn)(int frameInCase, char *outReason, int outReasonSize);

bool AutoTest_IsEnabled(void);
void AutoTest_Register(const char *name, AutoTestStepFn step, int maxFrames);
void AutoTest_RunFrame(void);
bool AutoTest_IsFinished(void);
int  AutoTest_GetExitCode(void);
void AutoTest_PrintSummary(void);

// Assertion helpers — format a reason string and return the bool, to cut
// boilerplate in step() functions.
bool AutoTest_ExpectTrue(bool cond, const char *desc, char *outReason, int outReasonSize);
bool AutoTest_ExpectFloatNear(float actual, float expected, float tol, const char *desc, char *outReason, int outReasonSize);

// Optional visual artifact for later inspection (e.g. via Read on the PNG) —
// saved under autotest_output/<name>.png.
void AutoTest_SaveScreenshot(const char *name);

// CROPPED to the effect, and prefer this one.
//
// A full-screen grab of a 1.6 m column in a 1280 px arena shot is a few dozen
// pixels of subject in a field of black, and judging an EDGE off that is how
// several wrong calls got made: at that scale a soft gradient and a hard step
// are the same handful of pixels. This projects a world-space sphere
// (centre + radius) through the camera, pads it, clamps to the screen and
// crops, so the saved PNG is the effect and nothing else.
void AutoTest_SaveScreenshotWorld(const char *name, Camera3D cam, Vector3 center,
                                  float radius);

#endif // SANDBOX_AUTO_TEST_H
