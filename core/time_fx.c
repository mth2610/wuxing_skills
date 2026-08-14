#include "core/time_fx.h"

#define MAX_HITSTOP_DURATION 0.25f
#define MIN_TIME_SCALE       0.05f

static float s_remaining  = 0.0f;
static float s_timeScale  = 1.0f;
// 1/60 rather than 0: if a caller ever reads this before main.c's first
// SetRawDelta, a zero would freeze every accumulator silently, which looks
// exactly like "the effect is broken" instead of "the wiring is missing".
static float s_rawDelta   = 1.0f / 60.0f;
static float s_elapsed    = 0.0f;
static bool  s_deterministic = false;

void TimeFX_Hitstop(float duration, float timeScale) {
    if (duration <= 0.0f) return;
    float d = duration > MAX_HITSTOP_DURATION ? MAX_HITSTOP_DURATION : duration;
    float s = timeScale < MIN_TIME_SCALE ? MIN_TIME_SCALE : (timeScale > 1.0f ? 1.0f : timeScale);
    if (d > s_remaining) {
        s_remaining = d;
        s_timeScale = s;
    }
}

void TimeFX_SetRawDelta(float rawDt) {
    // Guard against a negative/NaN dt reaching every accumulator in the engine.
    if (!(rawDt >= 0.0f)) return;   // NaN-safe: any NaN comparison is false
    s_rawDelta = rawDt;
    s_elapsed += rawDt;
}

float TimeFX_RawDelta(void) { return s_rawDelta; }
float TimeFX_Elapsed(void)  { return s_elapsed; }

void TimeFX_SetDeterministic(bool deterministic) { s_deterministic = deterministic; }
bool TimeFX_IsDeterministic(void) { return s_deterministic; }

float TimeFX_Apply(float rawDt) {
    if (s_remaining <= 0.0f) return rawDt;
    s_remaining -= rawDt;
    if (s_remaining < 0.0f) s_remaining = 0.0f;
    return rawDt * s_timeScale;
}
