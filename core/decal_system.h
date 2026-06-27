#ifndef DECAL_SYSTEM_H
#define DECAL_SYSTEM_H

#include "raylib.h"
#include <stdbool.h>

#define MAX_DECALS 32

typedef struct {
    Vector3 position;
    float rotation;     // Góc quay quanh trục đứng Y (độ)
    float scale;
    Texture2D texture;
    float lifetime;     // Thời gian tồn tại còn lại (giây)
    float maxLifetime;  // Thời gian tồn tại ban đầu (để tính tỷ lệ fade)
    Color tint;
    bool active;
} DecalEntity;

// Khởi tạo hệ thống Decal
void DecalSystem_Init(void);

// Spawn một vết sẹo/vòng sáng trên mặt đất
// Thường set Y offset nhỏ (ví dụ +0.02f) để tránh hiện tượng Z-fighting (nhấp nháy với mesh đất)
void Decal_Spawn(Vector3 pos, float rotation, float scale, Texture2D texture, float lifetime, Color tint);

// Cập nhật thời gian sống của các Decal
void DecalSystem_Update(float dt);

// Vẽ toàn bộ Decal áp sát mặt đất sử dụng BLEND_ALPHA
void DecalSystem_Draw(void);

// Giải phóng hệ thống
void DecalSystem_Unload(void);

#endif // DECAL_SYSTEM_H