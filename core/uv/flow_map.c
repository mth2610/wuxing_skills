#include "core/uv/flow_map.h"
#include <math.h>

FlowMap FlowMap_Create(Shader shader, Texture2D flowTex,
                        const char *timeUniformName) {
  FlowMap fm = {0};

  fm.uFlowSpeedLoc = GetShaderLocation(shader, "uFlowSpeed");
  fm.uFlowStrengthLoc = GetShaderLocation(shader, "uFlowStrength");
  fm.uFlowTilingLoc = GetShaderLocation(shader, "uFlowTiling");
  fm.flowTexLoc = GetShaderLocation(shader, "flowTex");

  fm.uFlowTimeLoc =
      timeUniformName ? GetShaderLocation(shader, timeUniformName) : -1;

  fm.flowTex = flowTex;
  fm.ownsTexture = false;

  // Giá trị mặc định an toàn, skill có thể override ngay sau khi tạo.
  fm.cfg.speed = 1.0f;
  fm.cfg.strength = 0.05f;
  fm.cfg.tiling = 1.0f;

  return fm;
}

FlowMap FlowMap_CreateWithTrailTexture(Shader shader, int texSize, float swirl,
                                        const char *timeUniformName) {
  if (texSize <= 0)
    texSize = 128;
  if (swirl < 0.0f)
    swirl = 0.0f;

  Image img = GenImageColor(texSize, texSize, BLANK);

  // Harmonics with INTEGER frequencies in both axes. That is what makes the
  // field tile: sin(2*PI*(k*u + m*v)) has the same value at u=0 and u=1 for any
  // integers k, m, so the seam is exact rather than cross-faded. A flow map
  // cross-faded at its seam would blend two DIRECTIONS, and the average of two
  // opposing directions is no flow at all — a dead band across the tube.
  //
  // Deliberately uneven amplitudes and frequencies: equal harmonics produce a
  // regular lattice, and a regular flow field reads as a machined pattern
  // travelling, not as something billowing.
  const float amp[3]  = {0.55f, 0.30f, 0.18f};
  const int   ku[3]   = {1, 2, 3};
  const int   kv[3]   = {2, 1, 3};
  const float phase[3] = {0.0f, 1.9f, 3.7f};

  for (int y = 0; y < texSize; y++) {
    float v = ((float)y + 0.5f) / (float)texSize;
    for (int x = 0; x < texSize; x++) {
      float u = ((float)x + 0.5f) / (float)texSize;

      // ALONG the tube is the base flow, and it is NEGATIVE: the guide's rule is
      // that a projectile's energy runs against its direction of travel, which
      // is what reads as something being left behind.
      float fu = 0.0f;
      float fv = -1.0f;
      for (int h = 0; h < 3; h++) {
        float a = 2.0f * PI * ((float)ku[h] * u + (float)kv[h] * v) + phase[h];
        // The swirl component goes AROUND the section; the along component only
        // modulates the base speed, so the flow never reverses into itself.
        fu += swirl * amp[h] * sinf(a);
        fv += 0.35f * amp[h] * cosf(a);
      }

      // Normalise so the encoding never clips. A clipped flow vector is a
      // direction silently bent toward the axis it clipped on.
      float len = sqrtf(fu * fu + fv * fv);
      float maxLen = 1.0f + 0.35f * (amp[0] + amp[1] + amp[2]) +
                     swirl * (amp[0] + amp[1] + amp[2]);
      if (maxLen < 1e-4f)
        maxLen = 1e-4f;
      fu /= maxLen;
      fv /= maxLen;
      (void)len;

      unsigned char r = (unsigned char)((fu * 0.5f + 0.5f) * 255.0f);
      unsigned char g = (unsigned char)((fv * 0.5f + 0.5f) * 255.0f);
      ImageDrawPixel(&img, x, y, (Color){r, g, 0, 255});
    }
  }

  Texture2D tex = LoadTextureFromImage(img);
  UnloadImage(img);
  SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
  SetTextureWrap(tex, TEXTURE_WRAP_REPEAT);

  FlowMap fm = FlowMap_Create(shader, tex, timeUniformName);
  fm.ownsTexture = true;
  return fm;
}

FlowMap FlowMap_CreateWithVortexTexture(Shader shader, int texSize,
                                         const char *timeUniformName) {
  if (texSize <= 0)
    texSize = 128;

  Image img = GenImageColor(texSize, texSize, BLANK);
  float cx = texSize / 2.0f;
  float cy = texSize / 2.0f;

  for (int y = 0; y < texSize; y++) {
    for (int x = 0; x < texSize; x++) {
      float dx = (float)x - cx;
      float dy = (float)y - cy;
      float dist = sqrtf(dx * dx + dy * dy);
      if (dist < 0.1f)
        dist = 0.1f;

      // Vector tiếp tuyến (xoáy tròn quanh tâm)
      float tangentX = -dy / dist;
      float tangentY = dx / dist;

      unsigned char r = (unsigned char)((tangentX * 0.5f + 0.5f) * 255.0f);
      unsigned char g = (unsigned char)((tangentY * 0.5f + 0.5f) * 255.0f);
      ImageDrawPixel(&img, x, y, (Color){r, g, 0, 255});
    }
  }

  Texture2D tex = LoadTextureFromImage(img);
  UnloadImage(img);
  SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
  SetTextureWrap(tex, TEXTURE_WRAP_REPEAT);

  FlowMap fm = FlowMap_Create(shader, tex, timeUniformName);
  fm.ownsTexture = true;
  return fm;
}

void FlowMap_Apply(const FlowMap *fm, Shader shader, float time) {
  if (fm->uFlowSpeedLoc != -1) {
    SetShaderValue(shader, fm->uFlowSpeedLoc, &fm->cfg.speed,
                   SHADER_UNIFORM_FLOAT);
  }
  if (fm->uFlowStrengthLoc != -1) {
    SetShaderValue(shader, fm->uFlowStrengthLoc, &fm->cfg.strength,
                   SHADER_UNIFORM_FLOAT);
  }
  if (fm->uFlowTilingLoc != -1) {
    SetShaderValue(shader, fm->uFlowTilingLoc, &fm->cfg.tiling,
                   SHADER_UNIFORM_FLOAT);
  }
  if (fm->uFlowTimeLoc != -1) {
    SetShaderValue(shader, fm->uFlowTimeLoc, &time, SHADER_UNIFORM_FLOAT);
  }
  if (fm->flowTexLoc != -1) {
    // SetShaderValueTexture tự quản lý slot dựa theo uniform location, raylib
    // không cần (và không nên) bị can thiệp thủ công bằng
    // rlActiveTextureSlot/rlEnableTexture — bug cũ trong bản trước gây xung
    // đột slot với các texture khác (causticsTex) đang dùng trong cùng
    // shader.
    SetShaderValueTexture(shader, fm->flowTexLoc, fm->flowTex);
  }
}

void FlowMap_Unload(FlowMap *fm) {
  if (fm->ownsTexture) {
    UnloadTexture(fm->flowTex);
  }
  fm->flowTex = (Texture2D){0};
  fm->ownsTexture = false;
}
