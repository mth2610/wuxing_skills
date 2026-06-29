#ifndef COLOR_GRADIENT_H
#define COLOR_GRADIENT_H

#include "raylib.h"
#include <stdbool.h>

#define COLOR_GRADIENT_MAX_STOPS 8

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

#endif // COLOR_GRADIENT_H