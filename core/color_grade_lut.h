#ifndef CORE_COLOR_GRADE_LUT_H
#define CORE_COLOR_GRADE_LUT_H

#include "raylib.h"
#include <stdbool.h>

/*
 * Đợt G5 — 3D colour grading delivered as a 2D STRIP texture.
 *
 * Why a strip and not sampler3D: the project's floor is GLES 3.x on Mali
 * (Android is not a stretch target, it ships), and a 2D strip works on every
 * backend rlvk/GL/GLES presents without a capability query or a second code
 * path. The cost is that the blue axis is interpolated by hand — two slice
 * samples and a mix — which is one extra tap, not a different architecture.
 *
 * LAYOUT (size = COLOR_GRADE_LUT_SIZE):
 *   width  = size*size, height = size. Each of the `size` tiles is one blue
 *   slice, laid left to right. Inside a tile, +U is red and +V is green.
 *   V FOLLOWS IMAGE ROW ORDER: row 0 = green 0. An externally authored strip
 *   must match, or it will grade with green flipped — which reads as a bizarre
 *   colour cast rather than as a mirrored image, so it is easy to misdiagnose.
 *
 * WHERE IT APPLIES: after tone mapping, on display-referred 0..1 values. A LUT
 * applied to HDR scene values would be sampling outside its own domain and
 * clamping every highlight to the LUT's top slice.
 *
 * The default is the NEUTRAL (identity) strip, generated procedurally — no
 * asset dependency, and enabling the system changes nothing until a graded
 * strip is supplied. That matters: it makes "is the LUT path wired correctly"
 * separable from "do I like this look", which are otherwise one confusing
 * question.
 */

#define COLOR_GRADE_LUT_SIZE 16

// Asset-optional integration point: if a strip exists here at Init, it is
// adopted automatically. Absent, the frame stays exactly as it is today.
#define COLOR_GRADE_LUT_DEFAULT_PATH "assets/luts/grade.png"

// Builds the neutral strip and uploads it. Idempotent.
void ColorGradeLut_Init(void);
void ColorGradeLut_Unload(void);

// Replaces the active LUT with a strip loaded from `path`. Dimensions must be
// exactly (SIZE*SIZE) x SIZE. On ANY failure the current LUT is KEPT and false
// is returned — a grading pass that silently falls back to a black or garbage
// texture would tint the whole frame with no clue as to why.
bool ColorGradeLut_Load(const char *path);

// Back to identity.
void ColorGradeLut_UseNeutral(void);

// The strip to bind. Valid after Init; .id == 0 if the upload failed.
Texture2D ColorGradeLut_Texture(void);

// True while the active LUT is the generated identity strip.
bool ColorGradeLut_IsNeutral(void);

#endif // CORE_COLOR_GRADE_LUT_H
