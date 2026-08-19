#ifndef SCREEN_DISTORT_H
#define SCREEN_DISTORT_H

/* Shockwave refraction sources and the full-screen pass that applies them.
 * The scene render targets this header used to declare live in
 * core/scene_targets.h since 19/08/2026. */
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

/* The distortion shader and its source list. Call SceneTargets_Init FIRST —
 * the distort pass reads that module's colour target. */
void ScreenDistort_Init(void);
void ScreenDistort_Unload(void);

// Thêm một nguồn biến dạng màn hình (sóng xung kích) tại toạ độ World 3D
void ScreenDistort_Add(Vector3 worldPos, float radius, float strength, float lifetime, float speed);

// Cập nhật thời gian sống của các nguồn biến dạng
void ScreenDistort_Update(float dt);

// Vẽ kết quả màn hình kèm theo biến dạng bằng Shader
void ScreenDistort_Draw(Camera3D camera);

/* False when no source is alive, i.e. ScreenDistort_Draw would be an identity
 * copy of the scene target. The frame loop uses this to skip a full-resolution
 * HDR read+write that changes nothing. */
bool ScreenDistort_HasLiveSources(void);

#endif // SCREEN_DISTORT_H