#include "core/camera_fx.h"
#include "raymath.h"
#include <math.h>

static float currentTrauma = 0.0f;
static float decayRate = 1.2f; // Tốc độ giảm rung chấn mỗi giây (hoàn tất giảm rung sau ~0.8s)

void CameraFX_Shake(float trauma) {
  currentTrauma = Clamp(currentTrauma + trauma, 0.0f, 1.0f);
}

// Hàm sinh nhiễu giả lập (Pseudo-random noise) dựa trên hàm băm hình sin (fBm)
// seed: phân biệt các trục lắc khác nhau
static float Noise(int seed, float time) {
  return sinf(time * 28.0f + seed * 123.45f) * 0.5f +
         sinf(time * 56.0f + seed * 456.78f) * 0.3f +
         sinf(time * 84.0f + seed * 789.12f) * 0.2f;
}

void CameraFX_Update(Camera3D *camera, float dt) {
  if (currentTrauma <= 0.0f) return;

  // Giảm dần chấn động
  currentTrauma = Clamp(currentTrauma - decayRate * dt, 0.0f, 1.0f);

  float time = (float)GetTime();
  float shake = currentTrauma * currentTrauma; // Sử dụng trauma^2 cho độ giật tự nhiên

  // Biên độ rung lắc tối đa (tính bằng đơn vị 3D trong sandbox)
  // Phù hợp với khoảng cách camera đi theo (camera_y = 500, camera_z = 400)
  float maxTranslationX = 22.0f;
  float maxTranslationY = 18.0f;
  float maxTranslationZ = 15.0f;
  float maxRoll = 3.5f * DEG2RAD; // Nghiêng camera tối đa 3.5 độ

  float offsetX = maxTranslationX * shake * Noise(0, time);
  float offsetY = maxTranslationY * shake * Noise(1, time);
  float offsetZ = maxTranslationZ * shake * Noise(2, time);
  float offsetRoll = maxRoll * shake * Noise(3, time);

  // Tính toán các vector hướng cục bộ của camera
  Vector3 forward = Vector3Normalize(Vector3Subtract(camera->target, camera->position));
  Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera->up));
  Vector3 up = Vector3CrossProduct(right, forward);

  // Áp dụng độ lệch vị trí (Translation Offset) theo hệ trục cục bộ
  Vector3 translationOffset = Vector3Zero();
  translationOffset = Vector3Add(translationOffset, Vector3Scale(right, offsetX));
  translationOffset = Vector3Add(translationOffset, Vector3Scale(up, offsetY));
  translationOffset = Vector3Add(translationOffset, Vector3Scale(forward, offsetZ));

  camera->position = Vector3Add(camera->position, translationOffset);
  camera->target = Vector3Add(camera->target, translationOffset);

  // Áp dụng độ nghiêng (Roll) bằng cách xoay vector UP xung quanh trục Forward
  camera->up = Vector3RotateByAxisAngle(camera->up, forward, offsetRoll);
}
