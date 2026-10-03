#ifndef VOLUMETRIC_FOG_H
#define VOLUMETRIC_FOG_H

#include "raylib.h"
#include <stdbool.h>

// Khởi tạo hệ thống sương mù thể tích không gian & God-Rays
void VolumetricFog_Init(int width, int height);

// Giải phóng tài nguyên
void VolumetricFog_Unload(void);

// Cập nhật khi kích thước cửa sổ thay đổi
void VolumetricFog_Resize(int width, int height);

// Gọi trong 3D pass để yêu cầu snapshot depth cho volumetric raymarch
void VolumetricFog_PreFrame(void);

// Thực thi render pass sương mù thể tích & God-rays, sau đó hòa trộn vào Scene HDR
void VolumetricFog_Render(Camera3D camera);

// Bật/tắt sương mù thể tích
// Effective enable state, including tuning.cfg volumetric_fog_enabled (default 1).
bool VolumetricFog_IsEnabled(void);
void VolumetricFog_SetEnabled(bool enabled);

// Cấu hình cường độ God-rays (0.0 = tắt god rays, 1.0 = mặc định)
void  VolumetricFog_SetGodRayIntensity(float intensity);
float VolumetricFog_GetGodRayIntensity(void);

// Fraction of the legacy distant ground-depth footprint, compressed toward
// the far edge of the camera frame. Default 1; clamped to [0,1]; NaN resets 1.
// 2/3 moves the distant onset from 12% to 41.33% of the projected ground
// half-span. Zero removes global distant haze. Local volumes, nearby profiles,
// density and light intensity are unaffected. Persists across resize; map
// owners must restore 1 when leaving a map with a custom footprint.
void VolumetricFog_SetDistantCoverage(float areaRatio);
float VolumetricFog_GetDistantCoverage(void);

#endif // VOLUMETRIC_FOG_H
