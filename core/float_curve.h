#ifndef FLOAT_CURVE_H
#define FLOAT_CURVE_H

#include <stdbool.h>

#define FLOAT_CURVE_MAX_STOPS 8

typedef struct {
  float t; // [0.0 .. 1.0]
  float value;
} FloatCurveStop;

typedef struct {
  FloatCurveStop stops[FLOAT_CURVE_MAX_STOPS];
  int count;
} FloatCurve;

// Thêm một điểm dừng giá trị. Người gọi tự sắp xếp theo thứ tự t tăng dần.
bool FloatCurve_AddStop(FloatCurve *c, float t, float value);

// Lấy mẫu giá trị tại thời điểm t bằng phương pháp nội suy tuyến tính (LERP)
float FloatCurve_Sample(const FloatCurve *c, float t);

#endif // FLOAT_CURVE_H
