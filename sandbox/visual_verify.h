#ifndef SANDBOX_VISUAL_VERIFY_H
#define SANDBOX_VISUAL_VERIFY_H

#include <stdbool.h>

// Item 20: Visual verification harness.
// Activated by WUXING_VERIFY=<skill_name> env var.
// Casts the named skill at a fixed position, saves screenshots at
// canonical time offsets, then exits with code 0.
//
// Usage:
//   WUXING_VERIFY=fire_ball ./wuxing
//
// Output PNGs land in autotest_output/verify_<skill>_<time>.png
// (same directory as AutoTest_SaveScreenshot output, for easy CI archiving).

bool        VisualVerify_IsEnabled(void);
const char *VisualVerify_GetSkillName(void);

// Call after InitSkillManager + all skill Inits, passing the skill index
// resolved from VisualVerify_GetSkillName().  If skillIndex == -1, the
// harness immediately marks itself finished (unknown skill → exit 1).
void VisualVerify_Init(int skillIndex);

// Call once per frame with total elapsed game time (NOT autotest fixed dt).
void VisualVerify_RunFrame(float elapsed);

bool VisualVerify_IsFinished(void);
int  VisualVerify_GetExitCode(void);

#endif // SANDBOX_VISUAL_VERIFY_H
