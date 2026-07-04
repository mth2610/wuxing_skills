#include "sandbox/visual_verify.h"
#include "sandbox/auto_test.h"
#include "core/skill_manager.h"
#include "raylib.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Screenshot time offsets (seconds after cast).
static const float SHOT_TIMES[] = { 0.15f, 0.5f, 1.0f, 2.0f, 3.5f };
#define SHOT_COUNT ((int)(sizeof(SHOT_TIMES) / sizeof(SHOT_TIMES[0])))

// Cast position: slightly behind arena centre so projectiles travel forward.
static const Vector3 CAST_POS   = { 6.0f, 0.0f, 7.4f };
static const Vector3 CAST_TARGET = { 6.0f, 0.0f, 1.4f };

static int   s_skillIndex  = -1;
static int   s_nextShot    = 0;
static bool  s_finished    = false;
static int   s_exitCode    = 0;
static float s_castTime    = 0.0f;
static bool  s_castDone    = false;

bool VisualVerify_IsEnabled(void) {
    const char *v = getenv("WUXING_VERIFY");
    return v && v[0] != '\0';
}

const char *VisualVerify_GetSkillName(void) {
    const char *v = getenv("WUXING_VERIFY");
    return v ? v : "";
}

void VisualVerify_Init(int skillIndex) {
    s_skillIndex = skillIndex;
    s_nextShot   = 0;
    s_finished   = false;
    s_exitCode   = 0;
    s_castTime   = 0.0f;
    s_castDone   = false;

    if (skillIndex < 0) {
        TraceLog(LOG_ERROR, "[VISUAL_VERIFY] Unknown skill '%s' — exiting", VisualVerify_GetSkillName());
        s_finished = true;
        s_exitCode = 1;
    }
}

void VisualVerify_RunFrame(float elapsed) {
    if (s_finished) return;

    // Cast on the very first frame (elapsed > 0 so the window is fully up).
    if (!s_castDone && elapsed > 0.0f) {
        SkillParams params = {0};
        params.level     = 1;
        params.milestone = 1;
        params.sizeScale = 1.0f;
        params.quantity  = 3;
        params.anchorType = CAST_ANCHOR_TARGET;
        params.pathType   = CAST_PATH_PROJECTILE;
        params.damage     = 100.0f;
        CastSkill(s_skillIndex, 0, CAST_POS, CAST_TARGET, params);
        s_castTime = elapsed;
        s_castDone = true;
        TraceLog(LOG_INFO, "[VISUAL_VERIFY] Cast '%s' at t=%.3f", VisualVerify_GetSkillName(), elapsed);
    }

    if (!s_castDone) return;

    float sincecast = elapsed - s_castTime;

    // Save screenshots at each threshold as time passes.
    while (s_nextShot < SHOT_COUNT && sincecast >= SHOT_TIMES[s_nextShot]) {
        char fname[128];
        snprintf(fname, sizeof(fname), "verify_%s_%.2fs",
                 VisualVerify_GetSkillName(), SHOT_TIMES[s_nextShot]);
        AutoTest_SaveScreenshot(fname);
        TraceLog(LOG_INFO, "[VISUAL_VERIFY] Screenshot: %s.png", fname);
        s_nextShot++;
    }

    if (s_nextShot >= SHOT_COUNT) {
        TraceLog(LOG_INFO, "[VISUAL_VERIFY] Done — %d screenshots saved.", SHOT_COUNT);
        s_finished = true;
        s_exitCode = 0;
    }
}

bool VisualVerify_IsFinished(void) { return s_finished; }
int  VisualVerify_GetExitCode(void) { return s_exitCode; }
