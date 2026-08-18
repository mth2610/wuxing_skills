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

// Đợt G — true HDR: true if the scene buffer (renderTex, where the whole 3D
// world is drawn) is a 16-bit half-float target, so emissive/additive VFX keep
// values > 1.0 until PostFX tone-maps. false = LDR RGBA8 fallback (GLES2).
// This is the AUTHORITATIVE HDR flag; PostFX_Init matches it. Valid after Init.
bool ScreenDistort_IsHDR(void);

// Samples the scene target (renderTex) rasterizes with. 4 = real hardware MSAA on the offscreen
// HDR target (rlvk/Vulkan only — FLAG_MSAA_4X_HINT reaches the swapchain, which no geometry is
// drawn into); 1 = single-sampled, which is what GL 3.3 / GLES and any device that declines
// offscreen MSAA get. PostFX's FXAA pass is the fallback resolve for the 1-sample case.
// Valid after Init.
int ScreenDistort_GetSceneSamples(void);

// Giải phóng tài nguyên hệ thống
void ScreenDistort_Unload(void);

// Bắt đầu vẽ cảnh 3D vào RenderTexture phụ
void ScreenDistort_Begin(void);

// Kết thúc vẽ cảnh 3D
void ScreenDistort_End(void);

// Low-level compatibility hooks used by the core/vfx_render.h implementation.
// New feature code uses VFXRender so target/blend/depth state cannot drift
// between particle, trail, ribbon, decal, map and skill renderers.
void ScreenDistort_BeginVFXBody(void);
void ScreenDistort_BeginVFXEmission(void);
void ScreenDistort_EndVFXLayer(void);

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
 * 1 texture riêng half-resolution (`prevDepthTex`) ngay sau ScreenDistort_End() mỗi frame —
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
/* Submit the full-resolution screen region required by soft particles this
 * frame. Multiple requests are unioned; the next snapshot copies only it. */
void ScreenDistort_RequestSoftDepthRegion(Rectangle screenRegion);

// Texture độ sâu của frame TRƯỚC (trễ 1 frame) — sample được. Giá trị đã
// LINEARIZED (world-space distance), KHÔNG phải NDC [0..1] thô.
Texture2D ScreenDistort_GetDepthTexture(void);
// Current frame's raw scene attachments. Valid after ScreenDistort_End().
Texture2D ScreenDistort_GetSceneTexture(void);
Texture2D ScreenDistort_GetRawDepthTexture(void);

/* ============================================================================
 * SCENE SNAPSHOT — refraction taps (glass shields / refractive volumes)
 * --------------------------------------------------------------------------
 * The VFX body pass binds `renderTex` itself (the split layer targets were
 * retired), so an effect that wants to refract "what is behind it" cannot
 * sample the scene target while drawing into it — undefined in GL, a
 * read/write hazard in Vulkan (engine landmine #15; the same trap that caught
 * FluidSurface). The safe pattern is the one FluidSurface uses privately:
 * copy the finished scene into a separate target while it is still only a
 * source, then sample the copy.
 *
 * Frame flow (wired in main.c):
 *   VFX_Compose_Update();          // 1: refractive effect calls Request
 *   ScreenDistort_Begin(); ...3D scene incl. VFX_Compose_Draw3D...;
 *   ScreenDistort_End();           // 2: the complete scene now exists
 *   ScreenDistort_SnapshotScene(); // 3: 2D time — copies renderTex iff a
 *                                  //    request arrived (Effect draws next)
 *
 * The snapshot MUST run at 2D time (outside MyBeginMode3D), even though the
 * copy looks like a plain blit and "would work inside the 3D pass":
 * raylib's EndTextureMode() hard-resets the projection and modelview to
 * screen-space ortho and does NOT restore the caller's matrices, so a copy
 * taken inside the 3D pass silently corrupts every later draw (engine
 * landmine #15 — the glass shield once vanished entirely this way).
 *
 * The request flag is per-frame: a full-resolution copy only happens while
 * some refractive effect is actually alive.
 * ==========================================================================*/
void ScreenDistort_RequestSceneSnapshot(void);
// Copy renderTex -> private snapshot; no-op unless requested this frame.
void ScreenDistort_SnapshotScene(void);
// Sample-safe copy of the current frame's scene; texture.id 0 until the
// first successful snapshot. Same storage orientation as renderTex, so sample
// with gl_FragCoord.xy / u_resolution.
Texture2D ScreenDistort_GetSceneSnapshotTexture(void);

// Bind depth texture vào textureSlot + set u_cameraDepthTex/u_cameraNear/
// u_cameraFar/u_resolution lên shader (bỏ qua an toàn nếu shader không khai
// báo, cùng pattern SkillManager_BeginShader). Gọi sau BeginShaderMode(shader),
// trước Draw. Bắt buộc gọi ScreenDistort_UnbindSoftParticleDepth() cùng slot
// sau khi vẽ xong để giải phóng texture unit.
void ScreenDistort_BindDepthForSoftParticles(Shader shader, int textureSlot);
void ScreenDistort_UnbindSoftParticleDepth(int textureSlot);

#endif // SCREEN_DISTORT_H
