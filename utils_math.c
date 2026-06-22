#include "utils_math.h"
#include "raymath.h"

float Math_Mix(float x, float y, float a) { return x * (1.0f - a) + y * a; }

float SmoothStep01(float x) {
  x = Clamp(x, 0.0f, 1.0f);
  return x * x * (3.0f - 2.0f * x);
}

float Random01(void) { return (float)GetRandomValue(0, 10000) / 10000.0f; }