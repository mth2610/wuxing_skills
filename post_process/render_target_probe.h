#ifndef WUXING_RENDER_TARGET_PROBE_H
#define WUXING_RENDER_TARGET_PROBE_H

#include "raylib.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* Opt-in renderer instrumentation. It is header-only so the production render
 * graph gains no new lifecycle or allocation owner.
 *
 *   WUXING_VFX_PROBE_PREFIX=/private/tmp/energy_dark
 *   WUXING_VFX_PROBE_FRAME=29
 *
 * Every participating render stage uses the same zero-based frame index. */
static inline bool RenderTargetProbe_MatchesFrame(int frameIndex)
{
  const char *prefix = getenv("WUXING_VFX_PROBE_PREFIX");
  const char *frame = getenv("WUXING_VFX_PROBE_FRAME");
  return prefix != NULL && prefix[0] != '\0' &&
         frameIndex == ((frame != NULL) ? atoi(frame) : 0);
}

static inline void RenderTargetProbe_Dump(RenderTexture2D target,
                                          const char *label,
                                          bool splitAlpha)
{
  const char *prefix = getenv("WUXING_VFX_PROBE_PREFIX");
  if (prefix == NULL || prefix[0] == '\0' || target.texture.id == 0) return;

  Image image = LoadImageFromTexture(target.texture);
  if (image.data == NULL)
  {
    TraceLog(LOG_WARNING, "VFX_PROBE: readback failed for %s texture %u",
             label, target.texture.id);
    return;
  }

  ImageFlipVertical(&image);
  ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R32G32B32A32);
  if (image.data == NULL || image.format != PIXELFORMAT_UNCOMPRESSED_R32G32B32A32)
  {
    TraceLog(LOG_WARNING, "VFX_PROBE: float conversion failed for %s", label);
    UnloadImage(image);
    return;
  }

  const int pixelCount = image.width * image.height;
  float *pixels = (float *)image.data;
  float maxR = 0.0f, maxG = 0.0f, maxB = 0.0f, maxA = 0.0f;
  double sumRgb = 0.0, sumAlpha = 0.0;
  int rgbCount = 0, alphaCount = 0, hdrCount = 0, rgbAboveAlpha = 0;
  for (int i = 0; i < pixelCount; ++i)
  {
    const float r = pixels[i * 4 + 0];
    const float g = pixels[i * 4 + 1];
    const float b = pixels[i * 4 + 2];
    const float a = pixels[i * 4 + 3];
    const float rgb = fmaxf(r, fmaxf(g, b));
    if (r > maxR) maxR = r;
    if (g > maxG) maxG = g;
    if (b > maxB) maxB = b;
    if (a > maxA) maxA = a;
    if (rgb > 0.001f) { sumRgb += rgb; rgbCount++; }
    if (a > 0.001f) { sumAlpha += a; alphaCount++; }
    if (rgb > 1.0f) hdrCount++;
    /* For an isolated <=1 straight-colour population accumulated into a
     * premultiplied body target, RGB cannot exceed accumulated coverage. */
    if (a > 0.001f && rgb > a + 0.01f) rgbAboveAlpha++;
  }

  TraceLog(LOG_INFO,
           "VFX_PROBE %s: %dx%d fmt=%d max=(%.4f %.4f %.4f %.4f) "
           "nonzero rgb=%d a=%d hdr=%d rgb>a=%d meanMaxRgb=%.4f meanA=%.4f",
           label, image.width, image.height, target.texture.format,
           maxR, maxG, maxB, maxA, rgbCount, alphaCount, hdrCount,
           rgbAboveAlpha, rgbCount ? (float)(sumRgb / rgbCount) : 0.0f,
           alphaCount ? (float)(sumAlpha / alphaCount) : 0.0f);
  if (target.texture.format == PIXELFORMAT_UNCOMPRESSED_R16G16B16A16)
    TraceLog(LOG_WARNING,
             "VFX_PROBE %s: R16F CPU readback is diagnostic only; validate "
             "HDR shape through the GPU-side LDR stage probes", label);

  char path[512];
  /* raylib's optional HDR exporter is not enabled in every build. Preserve the
   * bytes returned by CPU readback as tightly packed float32 RGBA; do not
   * mistake them for authoritative GPU values on the rlvk R16F path. */
  snprintf(path, sizeof(path), "%s_%s_rgba32f.bin", prefix, label);
  FILE *raw = fopen(path, "wb");
  if (raw == NULL ||
      fwrite(pixels, sizeof(float) * 4, (size_t)pixelCount, raw) != (size_t)pixelCount)
    TraceLog(LOG_WARNING, "VFX_PROBE: could not write %s", path);
  if (raw != NULL) fclose(raw);

  Image rgbView = ImageCopy(image);
  float *rgbPixels = (float *)rgbView.data;
  for (int i = 0; i < pixelCount; ++i) rgbPixels[i * 4 + 3] = 1.0f;
  snprintf(path, sizeof(path), "%s_%s_rgb.png", prefix, label);
  if (!ExportImage(rgbView, path))
    TraceLog(LOG_WARNING, "VFX_PROBE: could not write %s", path);
  UnloadImage(rgbView);

  if (splitAlpha)
  {
    Image alphaView = ImageCopy(image);
    float *alphaPixels = (float *)alphaView.data;
    for (int i = 0; i < pixelCount; ++i)
    {
      const float a = alphaPixels[i * 4 + 3];
      alphaPixels[i * 4 + 0] = a;
      alphaPixels[i * 4 + 1] = a;
      alphaPixels[i * 4 + 2] = a;
      alphaPixels[i * 4 + 3] = 1.0f;
    }
    snprintf(path, sizeof(path), "%s_%s_alpha.png", prefix, label);
    if (!ExportImage(alphaView, path))
      TraceLog(LOG_WARNING, "VFX_PROBE: could not write %s", path);
    UnloadImage(alphaView);
  }

  UnloadImage(image);
}

#endif
