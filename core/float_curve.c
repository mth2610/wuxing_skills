#include "core/float_curve.h"
#include "raymath.h"
#include <stddef.h>

bool FloatCurve_AddStop(FloatCurve *c, float t, float value) {
  if (c->count >= FLOAT_CURVE_MAX_STOPS)
    return false;
  c->stops[c->count].t = t;
  c->stops[c->count].value = value;
  c->count++;
  return true;
}

float FloatCurve_Sample(const FloatCurve *c, float t) {
  if (c->count == 0)
    return 0.0f;
  if (c->count == 1)
    return c->stops[0].value;

  t = Clamp(t, 0.0f, 1.0f);

  // Nếu t nằm ngoài biên của dải stop đã đăng ký
  if (t <= c->stops[0].t)
    return c->stops[0].value;
  if (t >= c->stops[c->count - 1].t)
    return c->stops[c->count - 1].value;

  // Tìm khoảng giữa 2 stop
  for (int i = 0; i < c->count - 1; i++) {
    if (t >= c->stops[i].t && t <= c->stops[i + 1].t) {
      float leftT = c->stops[i].t;
      float rightT = c->stops[i + 1].t;
      float factor = (t - leftT) / (rightT - leftT);
      return Lerp(c->stops[i].value, c->stops[i + 1].value, factor);
    }
  }
  return 0.0f;
}
