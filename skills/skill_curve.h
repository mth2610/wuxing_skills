#ifndef SKILL_CURVE_H
#define SKILL_CURVE_H

#include "core/float_curve.h"

// Time-varying scalar tunable for skill parameters (speed, radius, alpha,
// force strength...) that should change over a phase's lifetime instead of
// staying a flat constant (CORE_ISSUES.md Item 34 follow-up: sandbox-tunable
// system upgrade). Built directly on core/float_curve.h's FloatCurve (linear
// interpolation between stops) rather than a bespoke keyframe format — a
// SkillCurve is just a FloatCurve under the fixed convention "always exactly
// SKILL_CURVE_KEYS stops, at t = 0%, 25%, 50%, 75%, 100%". The fixed
// count/positions keep the sandbox UI to 5 plain sliders (no free-form
// add/remove/drag-point graph editor).
//
// t01 is always caller-supplied and caller-defined — this module has no
// notion of elapsed time or phase duration. Typical sources: a
// LayeredTimeline's Timeline_LayerProgress() (core/skill_helper.h), or
// SkillHelper_StepCurveFlight's own elapsed/maxDuration ratio for
// curve-driven flight speed.

#define SKILL_CURVE_KEYS 5

typedef FloatCurve SkillCurve;

// Fills a fresh SkillCurve with SKILL_CURVE_KEYS stops at the fixed t's
// above, all set to `value` — seeds a curve so it starts flat (identical to
// a plain constant) before any persisted/tuned shape loads. Must be called
// once before first use (a zero-initialized SkillCurve has 0 stops).
void SkillCurve_SetConstant(SkillCurve *curve, float value);

// Samples the curve at t01 (clamped to [0,1] internally by FloatCurve_Sample).
// Thin, intention-revealing wrapper over FloatCurve_Sample.
float SkillCurve_Eval(const SkillCurve *curve, float t01);

#endif // SKILL_CURVE_H
