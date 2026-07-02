#include "core/debug_draw.h"

static bool s_enabled = false;

void DebugDraw_SetEnabled(bool enabled) { s_enabled = enabled; }

bool DebugDraw_IsEnabled(void) { return s_enabled; }

void DebugDraw_Sphere(Vector3 pos, float radius, Color color) {
  if (!s_enabled) return;
  DrawSphereWires(pos, radius, 8, 8, color);
}

void DebugDraw_Circle(Vector3 center, float radius, Color color) {
  if (!s_enabled) return;
  DrawCircle3D(center, radius, (Vector3){1.0f, 0.0f, 0.0f}, 90.0f, color);
}
