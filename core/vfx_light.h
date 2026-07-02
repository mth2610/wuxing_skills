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

// Priority used for pool eviction when MAX_VFX_LIGHTS is full (CORE_ISSUES.md
// Item 12). VFX_PRIORITY_HIGH_ULTIMATE should win over VFX_PRIORITY_LOW when
// the pool is saturated.
typedef enum {
    VFX_PRIORITY_LOW = 0,
    VFX_PRIORITY_HIGH_ULTIMATE
} VFXPriority;

// Khởi tạo/Xóa hệ thống quản lý Light
void VFXLight_Init(void);
void VFXLight_Reset(void);

// Spawn một point light động (Hiệu ứng skill, vfx nổ, tia chớp...). When the
// pool is full: scans for the lowest-priority slot (ties broken by shortest
// remaining lifetime) and evicts it instead of rejecting the new spawn. A
// same-or-lower priority incoming spawn can still fail to find a slot only if
// every existing slot has strictly higher priority — in that case the spawn
// is silently dropped, same as the old full-pool behavior.
void VFXLight_Spawn(Vector3 pos, Color color, float radius, float lifetime,
                    VFXPriority priority);

// Cập nhật lifetime của các light theo delta time
void VFXLight_Update(float dt);

// Lấy danh sách các light đang hoạt động để nạp vào Uniform Array của Shader
void VFXLight_GetActive(VFXLightData *out, int *count, int maxCount);

#endif // VFX_LIGHT_H