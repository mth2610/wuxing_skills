#include "core/color_gradient.h"
#include "raymath.h"
#include <stddef.h>

bool ColorGradient_AddStop(ColorGradient *g, float t, Color color) {
  if (g->count >= COLOR_GRADIENT_MAX_STOPS)
    return false;
  g->stops[g->count].t = t;
  g->stops[g->count].color = color;
  g->count++;
  return true;
}

Color ColorGradient_Sample(const ColorGradient *g, float t) {
  if (g->count == 0)
    return WHITE;
  if (g->count == 1)
    return g->stops[0].color;

  t = Clamp(t, 0.0f, 1.0f);

  // Nếu t nằm ngoài biên của dải stop đã đăng ký
  if (t <= g->stops[0].t)
    return g->stops[0].color;
  if (t >= g->stops[g->count - 1].t)
    return g->stops[g->count - 1].color;

  // Tìm khoảng giữa 2 stop
  for (int i = 0; i < g->count - 1; i++) {
    if (t >= g->stops[i].t && t <= g->stops[i + 1].t) {
      float leftT = g->stops[i].t;
      float rightT = g->stops[i + 1].t;
      float factor = (t - leftT) / (rightT - leftT);

      Color c0 = g->stops[i].color;
      Color c1 = g->stops[i + 1].color;

      return (Color){(unsigned char)Lerp(c0.r, c1.r, factor),
                     (unsigned char)Lerp(c0.g, c1.g, factor),
                     (unsigned char)Lerp(c0.b, c1.b, factor),
                     (unsigned char)Lerp(c0.a, c1.a, factor)};
    }
  }
  return WHITE;
}

ColorGradient ColorGradient_MakeElectric(void) {
  ColorGradient g = {0};
  ColorGradient_AddStop(&g, 0.0f, (Color){180, 220, 255, 0});
  ColorGradient_AddStop(&g, 0.2f, (Color){100, 180, 255, 200});
  ColorGradient_AddStop(&g, 0.5f, (Color){255, 255, 255, 255});
  ColorGradient_AddStop(&g, 0.8f, (Color){80, 160, 255, 180});
  ColorGradient_AddStop(&g, 1.0f, (Color){0, 80, 200, 0});
  return g;
}

Texture2D ColorGradient_BakeLUT(const ColorGradient *g, int width) {
  Texture2D tex = {0};
  if (g == NULL || g->count <= 0)
    return tex;
  if (width < 2)
    width = 2;
  else if (width > COLOR_GRADIENT_LUT_MAX)
    width = COLOR_GRADIENT_LUT_MAX;

  // Stack buffer, not GenImageColor: core forbids malloc/free, and raylib only
  // needs `pixels` to stay valid for the duration of LoadTextureFromImage.
  Color pixels[COLOR_GRADIENT_LUT_MAX];
  for (int i = 0; i < width; i++)
    pixels[i] = ColorGradient_Sample(g, (float)i / (float)(width - 1));

  Image img = {.data = pixels,
               .width = width,
               .height = 1,
               .mipmaps = 1,
               .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
  tex = LoadTextureFromImage(img); // copies to VRAM; `pixels` may die after
  if (tex.id != 0) {
    // CLAMP, not the default REPEAT: index 1.0 must land on the last stop, not
    // wrap round to the first — that would put a hard white seam at the coldest
    // end of a black-body ramp, exactly where the flame meets its smoke.
    SetTextureWrap(tex, TEXTURE_WRAP_CLAMP);
    SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
  }
  return tex;
}

void ColorGradient_StandardFade(ColorGradient *grad, Color baseColor,
                                float midT, float brightenAmount) {
  if (grad == NULL)
    return;

  grad->count = 0;
  ColorGradient_AddStop(grad, 0.00f, baseColor);
  ColorGradient_AddStop(grad, midT,
                        ColorLerp(baseColor, WHITE, brightenAmount));
  ColorGradient_AddStop(grad, 1.00f, (Color){0, 0, 0, 0});
}