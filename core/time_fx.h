#ifndef TIME_FX_H
#define TIME_FX_H

#include <stdbool.h>

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

/*
 * The authoritative raw frame delta — NOT hitstop-scaled.
 *
 * WHY THIS EXISTS: the headless capture paths (--render-vfx, --autotest,
 * --visual-verify) pin dt to 1/60 so a run is reproducible, and they skip
 * SetTargetFPS, so raylib's own GetFrameTime() returns free-running wall-clock
 * time there. Anything that advances VFX state from GetFrameTime() directly
 * therefore lands on a DIFFERENT animation phase every run — even the rlvk
 * pipeline cache being cold vs warm is enough to shift it. That made
 * --render-vfx useless as an A/B instrument: run-to-run spread exceeded the
 * effect of the parameter under test, and a capture could look stable purely
 * because the effect had already finished.
 *
 * RULE: simulation/animation code under core/ and sandbox/ reads
 * TimeFX_RawDelta(), never GetFrameTime(). GetFrameTime() remains correct for
 * things that genuinely want wall-clock — perf counters and frame-budget gates
 * (core/post_fx.c's perf sample, core/fluid/fluid_surface.c's budget check) —
 * because pinning those would make them measure nothing.
 *
 * Not hitstop-scaled on purpose: this is a pure determinism fix, so effects
 * that ignored hitstop before still ignore it. Callers wanting scaled time take
 * the dt main.c already passes them.
 */
void  TimeFX_SetRawDelta(float rawDt); // main.c, once per frame, before anything simulates
float TimeFX_RawDelta(void);

/*
 * Accumulated elapsed seconds, from the same pinned delta.
 *
 * The counterpart trap to GetFrameTime(): raylib's GetTime() is wall-clock
 * seconds since InitWindow, so any shader phase driven by it lands somewhere
 * different every headless run even after the timestep is pinned. Animation
 * phase reads THIS. Grows without bound like GetTime() did — callers that feed
 * it to a shader should fmod it, as trail_system already does.
 */
float TimeFX_Elapsed(void);

/*
 * True while the frame is being produced for a REPRODUCIBLE capture
 * (--render-vfx / --autotest / --visual-verify).
 *
 * For load-shedding gates whose INPUT is wall clock. Pinning such a gate's
 * clock is wrong — it would make the gate measure a constant and report a
 * healthy frame forever — but leaving it live makes the gate DECIDE
 * differently between two runs, which changes what is drawn and defeats the
 * capture. A gate consults this and admits the work instead of measuring:
 * a capture wants the full-quality image, and it has no frame budget to blow.
 */
void  TimeFX_SetDeterministic(bool deterministic);
bool  TimeFX_IsDeterministic(void);

#endif // TIME_FX_H
