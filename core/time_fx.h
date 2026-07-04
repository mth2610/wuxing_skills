#ifndef TIME_FX_H
#define TIME_FX_H

/*
 * TimeFX — frame-freeze "hitstop" for impact juice (CORE_ISSUES.md Item 30).
 *
 * Usage in main.c:
 *   float dt = TimeFX_Apply(GetFrameTime());  // scaled dt for skills/particles/entities
 *   // Keep raw GetFrameTime() for camera + post-FX so the freeze doesn't
 *   // stall camera shake (frozen world + live shake is the intended effect).
 *
 * Usage at an impact site:
 *   TimeFX_Hitstop(0.09f, 0.05f);   // ~90ms freeze, world runs at 5% speed
 *
 * SpawnImpactEffect() calls TimeFX_Hitstop automatically when scale >= 1.5f.
 * Manual calls also allowed for custom impacts.
 *
 * Constraints:
 *   - duration clamped to [0, 0.25s]; scale clamped to [0.05, 1.0]
 *   - New calls extend the current hitstop, never stack multiplicatively
 */

void  TimeFX_Hitstop(float duration, float timeScale);
float TimeFX_Apply(float rawDt);   // call once per frame; returns rawDt * currentScale

#endif // TIME_FX_H
