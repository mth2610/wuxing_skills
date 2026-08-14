#ifndef POST_FX_H
#define POST_FX_H

#include "raylib.h"

// Cấu hình tham số hiệu ứng hậu kỳ
typedef struct {
  // Bloom — a 6-level pyramid (1/4 down to 1/128 of the frame) whose upsample
  // chain folds each wide level back into the tighter one above it. The deep
  // levels are what produce a soft far-reaching bleed rather than a tight halo.
  bool bloomEnabled;
  float bloomThreshold; // Ngưỡng lọc sáng (HDR luma, >1.0 = chỉ emissive mới bloom)
  float bloomIntensity; // Cường độ phát sáng cộng dồn
  // How far the glow spreads: the mix factor used when an upsampled level is
  // folded into the level above it. 0 = tight near halo only, 1 = only the
  // widest haze. 0 (the zero-initialised default) means "use the engine
  // default" (0.65), so existing initialisers keep working untouched.
  // Live override: tuning.cfg -> bloom_scatter.
  float bloomScatter;

  // Chromatic Aberration (Tách màu RGB rìa màn hình)
  bool chromaticEnabled;
  float chromaticStrength; // Cường độ dạt màu RGB hướng tâm

  // Vignette (Bo tròn tối góc)
  bool vignetteEnabled;
  float vignetteRadius;    // Bán kính vùng tròn sáng [0.0 .. 1.5]
  float vignetteSoftness;  // Độ mượt rìa đen [0.0 .. 1.0]

  // Color Grading (Cân chỉnh màu nghệ thuật)
  bool colorGradeEnabled;
  float contrast;          // Độ tương phản [0.5 .. 2.0]
  float saturation;        // Độ bão hòa màu sắc [0.0 .. 2.0]
  Vector3 colorTint;       // Bộ lọc nhân tông màu RGB
  // Split-toning (cinematic): multiplicative tint by luminance. shadowTint
  // colours the darks (cool moonlight), highlightTint the brights (warm).
  // Both (1,1,1) = off. Values ~0.85..1.15 per channel are a natural range.
  Vector3 shadowTint;
  Vector3 highlightTint;

  // Tone mapping (Đợt G1 — cinematic base). ACES filmic: rolls bright
  // highlights/bloom off to white smoothly instead of clipping, and gives
  // the frame a filmic response. exposure scales scene brightness pre-curve
  // (1.0 neutral; >1 brighter/more roll-off). Applied AFTER bloom, BEFORE
  // color grade.
  bool  tonemapEnabled;
  float exposure;          // [0.5 .. 2.0], 1.0 = neutral

  // ── Radial blur (Đợt E1a) ──────────────────────────────────────────────
  // Screen-space smear away from a focal point — the "violent burst" read.
  // Normally you do NOT set these by hand: call PostFX_RadialBurst() and let
  // PostFX_UpdateTransient decay them. Defaults OFF, so nothing that exists
  // today changes appearance.
  bool    radialBlurEnabled;
  Vector2 radialBlurCenter;   // screen UV [0..1] — the effect, projected
  float   radialBlurStrength; // 0 = off, ~0.15 = strong. Sample offset scale.
  float   radialBlurFalloff;  // UV radius at which the blur reaches full strength

  // ── Anamorphic streak bloom (Đợt E1b) ──────────────────────────────────
  // Stretches the bloom along one axis, the way an anamorphic lens does.
  // Applied inside the bloom downsample, so it costs nothing when bloom is off.
  // Defaults OFF.
  bool  bloomStreakEnabled;
  float bloomStreakStrength; // 0..1 — blend of the streaked tap vs the round one
  float bloomStreakAngle;    // radians, 0 = horizontal (classic anamorphic)
} PostFXConfig;

// Khởi tạo Post-processing Stack
void PostFX_Init(int width, int height);

// Đợt G — true HDR: returns true if the offscreen scene/bloom chain is a 16-bit
// half-float target (colors > 1.0 preserved until tone mapping). false = the
// GPU fell back to LDR RGBA8 (older GLES2). Valid after PostFX_Init.
bool PostFX_IsHDR(void);

// Giải phóng tài nguyên
void PostFX_Unload(void);

// Bắt đầu vẽ cảnh nền 3D chính vào buffer chính của PostFX
void PostFX_Begin(void);

// Kết thúc vẽ cảnh chính
void PostFX_End(void);

// Thực thi toàn bộ chuỗi thuật toán hậu kỳ (Bloom -> CA -> Grade -> Vignette) vẽ lên màn hình chính
void PostFX_Draw(const PostFXConfig *config);

// Cảnh Giới Thái Cực (MODULES_ROADMAP.md Module 6): monochrome overlay on
// the ONE main canvas — no extra render target. intensity 0..1 blends the
// composite's saturation toward 0 (1 = full black-and-white), overriding
// the config's color grade while > 0. Cheap to call every frame.
void PostFX_SetMonochrome(float intensity01);

// ── Transient radial burst (Đợt E1a) ─────────────────────────────────────────
// Fire-and-forget: an effect asks for a burst at a WORLD position and the decay
// is handled here, mirroring CameraFX_Shake's trauma model. Effects must not
// poke PostFXConfig's radialBlur* fields directly — several effects firing in
// the same frame would each clobber the others, and nothing would decay.
// The strongest live burst wins (a bigger explosion should not be softened by a
// small one that happens to start later).
void PostFX_RadialBurst(Vector3 worldPos, float strength, float duration);

// Decays live bursts and projects the winner's world position to a screen UV.
// Call once per frame from the frame loop, next to VFXLight_Update, BEFORE
// PostFX_Draw. `cam` is the camera the scene was rendered with — the projection
// has to match or the smear centre lands somewhere the effect is not.
void PostFX_UpdateTransient(Camera3D cam, float dt);

// True while a burst is live — lets a caller skip work when nothing is running.
bool PostFX_HasTransient(void);

// Writes the live burst's radialBlur* values into `config`. PostFX_Draw calls
// this itself; exposed so a caller that keeps its own config copy can see what
// was applied.
void PostFX_ApplyTransient(PostFXConfig *config);

#endif // POST_FX_H
