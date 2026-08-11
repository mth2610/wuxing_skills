// Headless behavioural test for flipbook frame-crop metadata. The sprite
// module is included directly so this tests the shipping state machine, not a
// hand-copied approximation of its UV maths.
#include <math.h>
#include <stdio.h>

int GetRandomValue(int min, int max) { (void)max; return min; }

#include "core/sprite_anim.c"

static int g_failures = 0;
#define CHECK(cond, name) do { \
  if (cond) printf("PASS: %s\n", name); \
  else { printf("FAIL: %s\n", name); g_failures++; } \
} while (0)

static int Near(float a, float b) { return fabsf(a - b) < 0.0001f; }

int main(void)
{
  SpriteAnim anim = {0};
  SpriteAnim_Init(&anim, 2, 2, 4, 1.0f, ANIM_ONCE);
  static const SpriteAnimFrameMeta meta[] = {
      {.crop = {0.25f, 0.25f, 0.50f, 0.50f}},
      {.crop = {0.00f, 0.00f, 1.00f, 1.00f}},
      {.crop = {0.10f, 0.20f, 0.60f, 0.50f}},
      {.crop = {0.00f, 0.00f, 0.00f, 0.00f}}, // invalid => full-cell fallback
  };
  SpriteAnim_SetFrameMetadata(&anim, meta, 4);

  SpriteAnimFrameSample next;
  float blend = 0.0f;
  SpriteAnimFrameSample s =
      SpriteAnim_CalculateFrameSampleBlend(&anim, 0.50f, &next, &blend);
  CHECK(Near(s.uv.x, 0.125f) && Near(s.uv.y, 0.125f) &&
        Near(s.uv.width, 0.25f) && Near(s.uv.height, 0.25f),
        "metadata crops the current frame UV");
  CHECK(Near(s.scale.x, 0.50f) && Near(s.scale.y, 0.50f) &&
        Near(s.offset.x, 0.0f) && Near(s.offset.y, 0.0f),
        "centred crop preserves the particle pivot");
  CHECK(Near(next.uv.x, 0.5f) && Near(next.uv.y, 0.0f) &&
        Near(blend, 0.50f),
        "cross-fade retains the next frame's independent layout");

  s = SpriteAnim_CalculateFrameSampleBlend(&anim, 3.25f, &next, &blend);
  CHECK(Near(s.uv.x, 0.5f) && Near(s.uv.y, 0.5f) &&
        Near(s.uv.width, 0.5f) && Near(s.uv.height, 0.5f),
        "bad metadata falls back to the legacy full cell");
  CHECK(Near(blend, 0.0f), "once animation clamps without a wrap blend");

  printf("sprite anim metadata: %s\n", g_failures ? "FAIL" : "PASS");
  return g_failures ? 1 : 0;
}
