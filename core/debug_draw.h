#ifndef DEBUG_DRAW_H
#define DEBUG_DRAW_H

#include "raylib.h"
#include <stdbool.h>

// Thin dev-only wireframe overlay for tuning hitbox/AoE radii visually
// (CORE_ISSUES.md Item 17). Gated behind a single global enable toggle,
// default DISABLED — all Draw* calls no-op when disabled, so call sites can
// leave them in place unconditionally without a perf/visual cost in normal
// play. Not subject to skills' "no raylib primitives" rule (core-internal
// debug tooling, not a shipped VFX mesh).

void DebugDraw_SetEnabled(bool enabled);
bool DebugDraw_IsEnabled(void);

// Wireframe sphere, e.g. for visualizing a damage/AoE radius in-world.
void DebugDraw_Sphere(Vector3 pos, float radius, Color color);

// Wireframe circle on the ground plane (Y = pos.y), e.g. for ground-target
// AoE shapes (radius rings, cast footprints).
void DebugDraw_Circle(Vector3 center, float radius, Color color);

#endif // DEBUG_DRAW_H
