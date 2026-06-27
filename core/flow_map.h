#ifndef FLOW_MAP_H
#define FLOW_MAP_H

#include "raylib.h"

// -----------------------------------------------------------------------------
// FLOW MAP MODULE (reusable cho nhiều skill: shield, fire, fluid, tornado...)
//
// Kiến trúc: MỖI hiệu ứng dùng flow map sở hữu riêng một FlowMap instance
// (location cache + config + texture). Không có state global -> nhiều skill
// có thể dùng flow map đồng thời, với shader/texture/tham số khác nhau, mà
// không đụng location hay slot của nhau (bug cũ: static location toàn cục
// chỉ phục vụ được 1 shader tại 1 thời điểm).
//
// Cách dùng:
//   FlowMap fm = FlowMap_Create(shader, flowTex, "u_time");
//   fm.cfg.speed = 1.2f; fm.cfg.strength = 0.05f; fm.cfg.tiling = 1.0f;
//   ...
//   BeginShaderMode(shader);
//   FlowMap_Apply(&fm, time); // chỉ set uniform, KHÔNG tự bind slot thủ công
//   <vẽ mesh>
//   EndShaderMode();
//   ...
//   FlowMap_Unload(&fm);
// -----------------------------------------------------------------------------

typedef struct {
  float speed;    // Tốc độ dòng chảy cuộn UV theo thời gian
  float strength; // Biên độ bẻ cong/méo UV gốc
  float tiling;   // Tiling của Main Texture / Caustics texture
} FlowMapConfig;

typedef struct {
  // Location cache RIÊNG cho từng instance -> an toàn khi nhiều skill
  // dùng flow map cùng lúc với shader khác nhau.
  int uFlowSpeedLoc;
  int uFlowStrengthLoc;
  int uFlowTilingLoc;
  int uFlowTimeLoc; // -1 nếu skill tự quản lý uniform thời gian riêng
  int flowTexLoc;

  Texture2D flowTex; // không sở hữu theo mặc định (xem ownsTexture)
  bool ownsTexture;  // true nếu tạo bằng FlowMap_CreateWithVortexTexture

  FlowMapConfig cfg;
} FlowMap;

// Tạo flow map instance gắn vào MỘT shader cụ thể đã load sẵn.
// flowTex: texture flow (RG = hướng dòng chảy), KHÔNG sở hữu bởi FlowMap;
// người gọi tự Unload khi không cần nữa.
// timeUniformName: tên uniform thời gian của shader đó (ví dụ "u_time").
// Truyền NULL nếu shader tự set thời gian theo cách khác.
FlowMap FlowMap_Create(Shader shader, Texture2D flowTex,
                        const char *timeUniformName);

// Tiện ích: tự sinh một flow texture xoáy (vortex) procedural, FlowMap sẽ
// SỞ HỮU texture này (FlowMap_Unload sẽ giải phóng nó).
FlowMap FlowMap_CreateWithVortexTexture(Shader shader, int texSize,
                                         const char *timeUniformName);

// Set các uniform speed/strength/tiling/time (nếu có) lên `shader`.
// PHẢI gọi sau BeginShaderMode(shader) - và `shader` PHẢI là đúng shader mà
// FlowMap này được tạo ra từ (FlowMap_Create), vì raylib SetShaderValue cần
// shader.id để biết program nào đang được set.
// Hàm này KHÔNG tự bind texture thủ công vào slot bằng rlActiveTextureSlot —
// chỉ gọi SetShaderValueTexture cho đúng uniform flowTex của NÓ, để raylib tự
// quản lý slot. Các texture khác (caustics, main tex...) do skill tự lo,
// tránh side-effect ẩn đụng slot của nhau.
void FlowMap_Apply(const FlowMap *fm, Shader shader, float time);

void FlowMap_Unload(FlowMap *fm);

#endif // FLOW_MAP_H
