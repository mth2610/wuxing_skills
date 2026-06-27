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
} PostFXConfig;

// Khởi tạo Post-processing Stack
void PostFX_Init(int width, int height);

// Giải phóng tài nguyên
void PostFX_Unload(void);

// Bắt đầu vẽ cảnh nền 3D chính vào buffer chính của PostFX
void PostFX_Begin(void);

// Kết thúc vẽ cảnh chính
void PostFX_End(void);

// Thực thi toàn bộ chuỗi thuật toán hậu kỳ (Bloom -> CA -> Grade -> Vignette) vẽ lên màn hình chính
void PostFX_Draw(const PostFXConfig *config);

#endif // POST_FX_H
