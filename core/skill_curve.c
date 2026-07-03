#include "core/skill_curve.h"

void SkillCurve_SetConstant(SkillCurve *curve, float value) {
  curve->count = 0;
  for (int i = 0; i < SKILL_CURVE_KEYS; i++)
    FloatCurve_AddStop(curve, (float)i / (float)(SKILL_CURVE_KEYS - 1), value);
}

float SkillCurve_Eval(const SkillCurve *curve, float t01) {
  return FloatCurve_Sample(curve, t01);
}
