#include "post_process/camera_fx.h"
#include "core/time_fx.h" // TimeFX_Elapsed — pinned clock; GetTime() is wall clock
#include "raymath.h"
#include <math.h>

static float currentTrauma = 0.0f;
static float decayRate = 1.2f; // Tốc độ giảm rung chấn mỗi giây (hoàn tất giảm rung sau ~0.8s)

void CameraFX_Shake(float trauma)
{
  currentTrauma = Clamp(currentTrauma + trauma, 0.0f, 1.0f);
}

// Hàm sinh nhiễu giả lập (Pseudo-random noise) tối ưu hóa bằng công thức lượng giác bậc 2, bậc 3
static inline float NoiseFast(int seed, float baseTime)
{
  float x1 = baseTime + (float)seed * 123.45f;
  float s1 = sinf(x1);
  float c1 = cosf(x1);
  // Bậc 2 và bậc 3 tận dụng phép nhân thay vì gọi thêm sinf() tốn kém
  float s2 = 2.0f * s1 * c1;
  float s3 = s1 * (3.0f - 4.0f * s1 * s1);
  return s1 * 0.5f + s2 * 0.3f + s3 * 0.2f;
}

void CameraFX_Update(Camera3D *camera, float dt)
{
  if (currentTrauma <= 0.0001f)
  {
    currentTrauma = 0.0f;
    return;
  }

  // Giảm dần chấn động
  currentTrauma = Clamp(currentTrauma - decayRate * dt, 0.0f, 1.0f);
  if (currentTrauma <= 0.0001f)
  {
    currentTrauma = 0.0f;
    return;
  }

  float baseTime = TimeFX_Elapsed() * 28.0f;
  float shake = currentTrauma * currentTrauma;

  const float maxTranslationX = 22.0f;
  const float maxTranslationY = 18.0f;
  const float maxTranslationZ = 15.0f;
  const float maxRoll = 3.5f * DEG2RAD;

  float offsetX = maxTranslationX * shake * NoiseFast(0, baseTime);
  float offsetY = maxTranslationY * shake * NoiseFast(1, baseTime);
  float offsetZ = maxTranslationZ * shake * NoiseFast(2, baseTime);
  float offsetRoll = maxRoll * shake * NoiseFast(3, baseTime);

  Vector3 forward = Vector3Normalize(Vector3Subtract(camera->target, camera->position));
  Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera->up));
  Vector3 up = Vector3CrossProduct(right, forward);

  Vector3 translationOffset = {
      right.x * offsetX + up.x * offsetY + forward.x * offsetZ,
      right.y * offsetX + up.y * offsetY + forward.y * offsetZ,
      right.z * offsetX + up.z * offsetY + forward.z * offsetZ};

  camera->position = Vector3Add(camera->position, translationOffset);
  camera->target = Vector3Add(camera->target, translationOffset);
  camera->up = Vector3RotateByAxisAngle(camera->up, forward, offsetRoll);
}