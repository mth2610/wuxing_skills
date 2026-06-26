#include "flow_map.h"
#include "rlgl.h"

static int uTimeLoc = -1;
static int uSpeedLoc = -1;
static int uStrengthLoc = -1;
static int uTilingLoc = -1;
static int flowTexLoc = -1;

Shader FlowMap_LoadShader(const char *shaderPath) {
  Shader shader = LoadShader(0, shaderPath);
  uTimeLoc = GetShaderLocation(shader, "uTime");
  uSpeedLoc = GetShaderLocation(shader, "uSpeed");
  uStrengthLoc = GetShaderLocation(shader, "uStrength");
  uTilingLoc = GetShaderLocation(shader, "uTiling");
  flowTexLoc = GetShaderLocation(shader, "flowTex");
  return shader;
}

void FlowMap_Apply(Shader shader, const FlowMapConfig *cfg, Texture2D flowTex,
                   float time) {
  SetShaderValue(shader, uTimeLoc, &time, SHADER_UNIFORM_FLOAT);
  SetShaderValue(shader, uSpeedLoc, &(cfg->speed), SHADER_UNIFORM_FLOAT);
  SetShaderValue(shader, uStrengthLoc, &(cfg->strength), SHADER_UNIFORM_FLOAT);
  SetShaderValue(shader, uTilingLoc, &(cfg->tiling), SHADER_UNIFORM_FLOAT);

  // Tiến hành bind thủ công Texture dòng chảy vào TEXTURE_SLOT_1 (Slot 0 đã giữ
  // Main Texture bởi raylib)
  int slot = 1;
  SetShaderValueTexture(shader, flowTexLoc, flowTex);
  rlActiveTextureSlot(slot);
  rlEnableTexture(flowTex.id);
}