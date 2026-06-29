#ifndef DECAL_SYSTEM_H
#define DECAL_SYSTEM_H

#include "raylib.h"
#include <stdbool.h>

#define MAX_DECALS 64

typedef struct {
    Vector3 position;
    float rotation;       // Góc quay ban đầu quanh trục Y (độ)
    float rotSpeed;       // Tốc độ xoay liên tục (độ/giây), 0 = tĩnh
    float scale;          // Scale hiện tại (nội suy từ scaleStart → scaleEnd)
    float scaleStart;     // Scale khi vừa spawn
    float scaleEnd;       // Scale khi hết lifetime (tạo hiệu ứng nở/thu)
    float yOffset;        // Y offset tránh Z-fighting, mặc định 0.02f
    Texture2D texture;
    float lifetime;       // Thời gian tồn tại còn lại (giây)
    float maxLifetime;
    Color tint;
    BlendMode blendMode;  // BLEND_ALPHA | BLEND_ADDITIVE | BLEND_MULTIPLIED
    bool active;
} DecalEntity;

// Khởi tạo hệ thống Decal
void DecalSystem_Init(void);

// API cơ bản — BLEND_ALPHA, scale tĩnh, không xoay, yOffset = 0.02f
void DecalSystem_Add(Vector3 pos, float rotation, float scale,
                     Texture2D texture, float lifetime, Color tint);

// API đầy đủ — kiểm soát blend mode, scale animate, rotation liên tục
void DecalSystem_AddEx(Vector3 pos, float rotation, float rotSpeed,
                       float scaleStart, float scaleEnd,
                       Texture2D texture, float lifetime,
                       Color tint, BlendMode blendMode, float yOffset);

// Cập nhật thời gian sống, scale nội suy, rotation
void DecalSystem_Update(float dt);

// Cập nhật thông tin camera để bù foreshortening — gọi 1 lần/frame trước DecalSystem_Draw
// Không gọi → decal bị ellipse vì camera nhìn từ góc nghiêng
void DecalSystem_SetCamera(Camera3D camera);

// Vẽ toàn bộ Decal, nhóm theo blendMode để giảm state switch
void DecalSystem_Draw(void);

// Giải phóng hệ thống
void DecalSystem_Unload(void);

#endif // DECAL_SYSTEM_H