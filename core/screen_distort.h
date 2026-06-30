#ifndef SCREEN_DISTORT_H
#define SCREEN_DISTORT_H

#include "raylib.h"

#define MAX_DISTORTION_SOURCES 16

typedef struct {
  Vector3 worldPos;     // Vị trí 3D trong không gian game
  float radius;         // Bán kính sóng xung kích cực đại
  float strength;       // Cường độ biến dạng khúc xạ (độ méo UV)
  float lifetime;       // Thời gian tồn tại còn lại (giây)
  float maxLifetime;    // Tổng thời gian tồn tại ban đầu (giây)
  float speed;          // Tốc độ lan tỏa sóng
} DistortionSource;

// Khởi tạo hệ thống Screen Distortion
void ScreenDistort_Init(int width, int height);

// Giải phóng tài nguyên hệ thống
void ScreenDistort_Unload(void);

// Bắt đầu vẽ cảnh 3D vào RenderTexture phụ
void ScreenDistort_Begin(void);

// Kết thúc vẽ cảnh 3D
void ScreenDistort_End(void);

// Thêm một nguồn biến dạng màn hình (sóng xung kích) tại toạ độ World 3D
void ScreenDistort_Add(Vector3 worldPos, float radius, float strength, float lifetime, float speed);

// Cập nhật thời gian sống của các nguồn biến dạng
void ScreenDistort_Update(float dt);

// Vẽ kết quả màn hình kèm theo biến dạng bằng Shader
void ScreenDistort_Draw(Camera3D camera);

/* ============================================================================
 * SOFT PARTICLES — scene depth texture
 * --------------------------------------------------------------------------
 * `renderTex` (RenderTexture nội bộ của module này) là buffer THỰC SỰ chứa
 * toàn cảnh 3D mỗi frame (ScreenDistort_Begin/End bọc quanh MyBeginMode3D/
 * MyEndMode3D trong main.c) — đây là nguồn depth per-pixel thật, KHÁC với
 * PostFX's mainRenderTex (chỉ nhận lại 1 quad màu 2D đã distort xong từ
 * ScreenDistort_Draw, không có depth hình học thật).
 *
 * Không thể sample trực tiếp renderTex.depth trong CÙNG frame đang ghi vào
 * nó (feedback loop — đọc/ghi cùng 1 framebuffer attachment là undefined
 * behavior theo spec OpenGL). Giải pháp: snapshot depth của frame TRƯỚC vào
 * 1 texture riêng (`prevDepthTex`) ngay sau ScreenDistort_End() mỗi frame —
 * particle vẽ trong frame N sample depth của frame N-1 (trễ 1 frame, không
 * đáng kể với soft-particle fade).
 *
 * Quy trình dùng (gọi từ main.c — đã wire sẵn):
 *   ScreenDistort_Begin(); ... vẽ scene 3D ...; ScreenDistort_End();
 *   ScreenDistort_SnapshotDepth();   // ngay sau End(), 1 lần/frame
 *
 * Trong skill: gọi ScreenDistort_BindDepthForSoftParticles(shader, slot)
 * sau BeginShaderMode(shader), trước Draw; rồi
 * core/shaders/common/soft_particle.glsl trong .fs để tính fade factor;
 * gọi ScreenDistort_UnbindSoftParticleDepth(slot) ngay sau khi vẽ xong.
 *
 * QUAN TRỌNG — bài học từ lần revert trước (CORE_ISSUES.md Item 3): chỉ
 * disable depth WRITE (rlDisableDepthMask) là không đủ cho mesh nửa-chìm.
 * Hardware depth TEST vẫn loại bỏ fragment bị che trước khi fragment shader
 * chạy. Phải disable cả depth TEST (rlDisableDepthTest) quanh draw call của
 * soft particle, không chỉ depth write.
 * ==========================================================================*/

// Gọi đúng 1 lần/frame, ngay sau ScreenDistort_End() — copy renderTex.depth
// (frame vừa render xong) sang prevDepthTex để frame KẾ TIẾP particle sample
// an toàn (không feedback loop).
void ScreenDistort_SnapshotDepth(void);

// Texture độ sâu của frame TRƯỚC (trễ 1 frame) — sample được. Giá trị đã
// LINEARIZED (world-space distance), KHÔNG phải NDC [0..1] thô.
Texture2D ScreenDistort_GetDepthTexture(void);

// Bind depth texture vào textureSlot + set u_cameraDepthTex/u_cameraNear/
// u_cameraFar/u_resolution lên shader (bỏ qua an toàn nếu shader không khai
// báo, cùng pattern SkillManager_BeginShader). Gọi sau BeginShaderMode(shader),
// trước Draw. Bắt buộc gọi ScreenDistort_UnbindSoftParticleDepth() cùng slot
// sau khi vẽ xong để giải phóng texture unit.
void ScreenDistort_BindDepthForSoftParticles(Shader shader, int textureSlot);
void ScreenDistort_UnbindSoftParticleDepth(int textureSlot);

#endif // SCREEN_DISTORT_H
