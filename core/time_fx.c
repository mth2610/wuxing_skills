#include "core/time_fx.h"

#define MAX_HITSTOP_DURATION 0.25f
#define MIN_TIME_SCALE       0.05f

static float s_remaining  = 0.0f;
static float s_timeScale  = 1.0f;

void TimeFX_Hitstop(float duration, float timeScale) {
    if (duration <= 0.0f) return;
    float d = duration > MAX_HITSTOP_DURATION ? MAX_HITSTOP_DURATION : duration;
    float s = timeScale < MIN_TIME_SCALE ? MIN_TIME_SCALE : (timeScale > 1.0f ? 1.0f : timeScale);
    if (d > s_remaining) {
        s_remaining = d;
        s_timeScale = s;
    }
}

float TimeFX_Apply(float rawDt) {
    if (s_remaining <= 0.0f) return rawDt;
    s_remaining -= rawDt;
    if (s_remaining < 0.0f) s_remaining = 0.0f;
    return rawDt * s_timeScale;
}
