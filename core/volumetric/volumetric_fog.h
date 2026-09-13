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
bool VolumetricFog_IsEnabled(void);
void VolumetricFog_SetEnabled(bool enabled);

// Cấu hình cường độ God-rays (0.0 = tắt god rays, 1.0 = mặc định)
void  VolumetricFog_SetGodRayIntensity(float intensity);
float VolumetricFog_GetGodRayIntensity(void);

#endif // VOLUMETRIC_FOG_H
