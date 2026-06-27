#ifndef SCREEN_DISTORT_H
#define SCREEN_DISTORT_H

#include "raylib.h"

#define MAX_DISTORTION_SOURCES 8

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
void ScreenDistort_AddSource(Vector3 worldPos, float radius, float strength, float lifetime, float speed);

// Cập nhật thời gian sống của các nguồn biến dạng
void ScreenDistort_Update(float dt);

// Vẽ kết quả màn hình kèm theo biến dạng bằng Shader
void ScreenDistort_Draw(Camera3D camera);

#endif // SCREEN_DISTORT_H
