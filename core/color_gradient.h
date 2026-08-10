#ifndef COLOR_GRADIENT_H
#define COLOR_GRADIENT_H

#include "raylib.h"
#include <stdbool.h>

#define COLOR_GRADIENT_MAX_STOPS 8
// Widest LUT ColorGradient_BakeLUT will produce. Bounds the stack buffer it
// builds the ramp in, so the bake needs no allocation.
#define COLOR_GRADIENT_LUT_MAX 256

typedef struct {
  float t; // [0.0 .. 1.0]
  Color color;
} GradientStop;

typedef struct {
  GradientStop stops[COLOR_GRADIENT_MAX_STOPS];
  int count;
} ColorGradient;

// Thêm một điểm dừng màu. Người gọi tự sắp xếp theo thứ tự t tăng dần.
bool ColorGradient_AddStop(ColorGradient *g, float t, Color color);

// Lấy mẫu màu tại thời điểm t bằng phương pháp nội suy tuyến tính (LERP)
Color ColorGradient_Sample(const ColorGradient *g, float t);

// Khởi tạo nhanh gradient dành riêng cho hệ Điện
ColorGradient ColorGradient_MakeElectric(void);

void ColorGradient_StandardFade(ColorGradient *grad, Color baseColor,
                                float midT, float brightenAmount);

// ── Bake a gradient into a 1-D LUT texture (width x 1) ──────────────────────
//
// For shaders that need the ramp PER TEXEL rather than per particle. The CPU
// path samples a gradient once per particle and hands the result down as a flat
// vertex colour, so every texel of a sprite shares one hue — a flame sprite can
// then never have a white core and an orange rim at the same time, no matter
// what its texture holds. Sampling the same ramp in the shader, indexed by a
// scalar the TEXTURE carries (temperature, density, age), is what buys the
// intra-sprite colour zoning.
//
// Keeping the ramp as data rather than baking hue into the sheet is also what
// keeps one greyscale sheet reusable across elements: swap the LUT and the same
// fire becomes purple magic fire. The texture stays colourless by design.
//
// `width` is clamped to [2, 256]; 64 is plenty for a smooth ramp. Uses
// POINT->BILINEAR filtering and CLAMP wrap, so index 0 and 1 are exactly the
// first and last stop. The caller OWNS the returned texture and should bake it
// once (a static), never per frame.
Texture2D ColorGradient_BakeLUT(const ColorGradient *g, int width);

#endif // COLOR_GRADIENT_H