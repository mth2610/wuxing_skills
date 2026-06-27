#include "core/flow_map.h"
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
