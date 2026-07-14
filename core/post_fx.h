#ifndef POST_FX_H
#define POST_FX_H

#include "raylib.h"

// Cấu hình tham số hiệu ứng hậu kỳ
typedef struct {
  // Bloom
  bool bloomEnabled;
  float bloomThreshold; // Ngưỡng lọc sáng [0.0 .. 1.0] (Luma)
  float bloomIntensity; // Cường độ phát sáng cộng dồn

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

#endif // POST_FX_H
