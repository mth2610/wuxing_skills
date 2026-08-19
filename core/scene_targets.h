#ifndef SCENE_TARGETS_H
#define SCENE_TARGETS_H

/* The render targets the whole frame is built in — scene colour (HDR), scene
 * depth, the previous frame's depth snapshot, and the refraction copy.
 *
 * Split out of screen_distort.h on 19/08/2026. That header declared 22
 * functions of which 3 were about distortion; everything else here is what the
 * rest of the engine was actually asking it for. `ScreenDistort_Add` is
 * unchanged, so no skill or composition needed touching.
 *
 * ALPHA IN THE SCENE TARGET IS UNDEFINED — additive VFX push it past 1.0 and
 * nothing consumes it. Any pass that composites this target must disable
 * blending first (post_fx.c's composite explains what happens when it does not).
 */
#include "raylib.h"

// Khởi tạo hệ thống Screen Distortion
void SceneTargets_Init(int width, int height);

// Đợt G — true HDR: true if the scene buffer (renderTex, where the whole 3D
// world is drawn) is a 16-bit half-float target, so emissive/additive VFX keep
// values > 1.0 until PostFX tone-maps. false = LDR RGBA8 fallback (GLES2).
// This is the AUTHORITATIVE HDR flag; PostFX_Init matches it. Valid after Init.
bool SceneTargets_IsHDR(void);

// Samples the scene target (renderTex) rasterizes with. 4 = real hardware MSAA on the offscreen
// HDR target (rlvk/Vulkan only — FLAG_MSAA_4X_HINT reaches the swapchain, which no geometry is
// drawn into); 1 = single-sampled, which is what GL 3.3 / GLES and any device that declines
// offscreen MSAA get. PostFX's FXAA pass is the fallback resolve for the 1-sample case.
// Valid after Init.
int SceneTargets_GetSceneSamples(void);

// Giải phóng tài nguyên hệ thống
void SceneTargets_Unload(void);

// Bắt đầu vẽ cảnh 3D vào RenderTexture phụ
void SceneTargets_Begin(void);

// Kết thúc vẽ cảnh 3D
void SceneTargets_End(void);

// Low-level compatibility hooks used by the core/vfx_render.h implementation.
// New feature code uses VFXRender so target/blend/depth state cannot drift
// between particle, trail, ribbon, decal, map and skill renderers.
void SceneTargets_BeginVFXBody(void);
void SceneTargets_BeginVFXEmission(void);
void SceneTargets_EndVFXLayer(void);

/* ============================================================================
 * SOFT PARTICLES — scene depth texture
 * --------------------------------------------------------------------------
 * `renderTex` (RenderTexture nội bộ của module này) là buffer THỰC SỰ chứa
 * toàn cảnh 3D mỗi frame (SceneTargets_Begin/End bọc quanh MyBeginMode3D/
 * MyEndMode3D trong main.c) — đây là nguồn depth per-pixel thật, KHÁC với
 * PostFX's mainRenderTex (chỉ nhận lại 1 quad màu 2D đã distort xong từ
 * ScreenDistort_Draw, không có depth hình học thật).
 *
 * Không thể sample trực tiếp renderTex.depth trong CÙNG frame đang ghi vào
 * nó (feedback loop — đọc/ghi cùng 1 framebuffer attachment là undefined
 * behavior theo spec OpenGL). Giải pháp: snapshot depth của frame TRƯỚC vào
 * 1 texture riêng half-resolution (`prevDepthTex`) ngay sau SceneTargets_End() mỗi frame —
 * particle vẽ trong frame N sample depth của frame N-1 (trễ 1 frame, không
 * đáng kể với soft-particle fade).
 *
 * Quy trình dùng (gọi từ main.c — đã wire sẵn):
 *   SceneTargets_Begin(); ... vẽ scene 3D ...; SceneTargets_End();
 *   SceneTargets_SnapshotDepth();   // ngay sau End(), 1 lần/frame
 *
 * Trong skill: gọi SceneTargets_BindDepthForSoftParticles(shader, slot)
 * sau BeginShaderMode(shader), trước Draw; rồi
 * core/shaders/common/soft_particle.glsl trong .fs để tính fade factor;
 * gọi SceneTargets_UnbindSoftParticleDepth(slot) ngay sau khi vẽ xong.
 *
 * QUAN TRỌNG — bài học từ lần revert trước (CORE_ISSUES.md Item 3): chỉ
 * disable depth WRITE (rlDisableDepthMask) là không đủ cho mesh nửa-chìm.
 * Hardware depth TEST vẫn loại bỏ fragment bị che trước khi fragment shader
 * chạy. Phải disable cả depth TEST (rlDisableDepthTest) quanh draw call của
 * soft particle, không chỉ depth write.
 * ==========================================================================*/

// Gọi đúng 1 lần/frame, ngay sau SceneTargets_End() — copy renderTex.depth
// (frame vừa render xong) sang prevDepthTex để frame KẾ TIẾP particle sample
// an toàn (không feedback loop).
void SceneTargets_SnapshotDepth(void);
/* Submit the full-resolution screen region required by soft particles this
 * frame. Multiple requests are unioned; the next snapshot copies only it. */
void SceneTargets_RequestSoftDepthRegion(Rectangle screenRegion);

// Texture độ sâu của frame TRƯỚC (trễ 1 frame) — sample được. Giá trị đã
// LINEARIZED (world-space distance), KHÔNG phải NDC [0..1] thô.
Texture2D SceneTargets_GetDepthTexture(void);
// Current frame's raw scene attachments. Valid after SceneTargets_End().
Texture2D SceneTargets_GetSceneTexture(void);
Texture2D SceneTargets_GetRawDepthTexture(void);

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
 *   SceneTargets_Begin(); ...3D scene incl. VFX_Compose_Draw3D...;
 *   SceneTargets_End();           // 2: the complete scene now exists
 *   SceneTargets_SnapshotScene(); // 3: 2D time — copies renderTex iff a
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
void SceneTargets_RequestSceneSnapshot(void);
// Copy renderTex -> private snapshot; no-op unless requested this frame.
void SceneTargets_SnapshotScene(void);
// Sample-safe copy of the current frame's scene; texture.id 0 until the
// first successful snapshot. Same storage orientation as renderTex, so sample
// with gl_FragCoord.xy / u_resolution.
Texture2D SceneTargets_GetSceneSnapshotTexture(void);

// Bind depth texture vào textureSlot + set u_cameraDepthTex/u_cameraNear/
// u_cameraFar/u_resolution lên shader (bỏ qua an toàn nếu shader không khai
// báo, cùng pattern SkillManager_BeginShader). Gọi sau BeginShaderMode(shader),
// trước Draw. Bắt buộc gọi SceneTargets_UnbindSoftParticleDepth() cùng slot
// sau khi vẽ xong để giải phóng texture unit.
void SceneTargets_BindDepthForSoftParticles(Shader shader, int textureSlot);
void SceneTargets_UnbindSoftParticleDepth(int textureSlot);

#endif // SCENE_TARGETS_H