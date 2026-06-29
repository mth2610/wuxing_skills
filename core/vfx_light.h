#ifndef VFX_LIGHT_H
#define VFX_LIGHT_H

#include "raylib.h"
#include <stdbool.h>

#define MAX_VFX_LIGHTS 16

// Cấu trúc phẳng truyền trực tiếp sang Shader qua Uniform Array
typedef struct {
    Vector3 position;
    float radius;
    Color color;
} VFXLightData;

// Khởi tạo/Xóa hệ thống quản lý Light
void VFXLight_Init(void);
void VFXLight_Reset(void);

// Spawn một point light động (Hiệu ứng skill, vfx nổ, tia chớp...)
void VFXLight_Spawn(Vector3 pos, Color color, float radius, float lifetime);

// Cập nhật lifetime của các light theo delta time
void VFXLight_Update(float dt);

// Lấy danh sách các light đang hoạt động để nạp vào Uniform Array của Shader
void VFXLight_GetActive(VFXLightData *out, int *count, int maxCount);

#endif // VFX_LIGHT_H