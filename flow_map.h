#ifndef FLOW_MAP_H
#define FLOW_MAP_H

#include "raylib.h"

typedef struct {
  float speed;       // Tốc độ dòng chảy cuộn UV theo thời gian
  float strength;    // Biên độ bẻ cong/méo dạng UV gốc
  float tiling;      // Tiling map của Main Texture chính
} FlowMapConfig;

// Khởi tạo và nạp Shader Flow Map phân rã 2 pha
Shader FlowMap_LoadShader(const char *shaderPath);

// Cập nhật tham số Uniform và thực hiện liên kết (bind) Flow Texture vào Slot 1
// Phải được gọi trong block: BeginShaderMode() -> FlowMap_Apply() -> Khối vẽ -> EndShaderMode()
void FlowMap_Apply(Shader shader, const FlowMapConfig *cfg, Texture2D flowTex, float time);

#endif // FLOW_MAP_H