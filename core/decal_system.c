#include "decal_system.h"
#include "rlgl.h"
#include "raymath.h"
#include <stddef.h>

static DecalEntity g_DecalPool[MAX_DECALS];
static int g_LastSpawnedIndex = 0;
static Shader g_DecalShader;

void DecalSystem_Init(void) {
    for (int i = 0; i < MAX_DECALS; i++) {
        g_DecalPool[i].active = false;
    }
    g_DecalShader = LoadShader(NULL, "core/shaders/decal.fs");
}

void Decal_Spawn(Vector3 pos, float rotation, float scale, Texture2D texture, float lifetime, Color tint) {
    int targetIdx = -1;

    // 1. Tìm slot trống trong pool tĩnh
    for (int i = 0; i < MAX_DECALS; i++) {
        int idx = (g_LastSpawnedIndex + i) % MAX_DECALS;
        if (!g_DecalPool[idx].active) {
            targetIdx = idx;
            break;
        }
    }

    // 2. Nếu pool đã đầy 32 phần tử, tìm decal có lifetime thấp nhất để ghi đè (LRU-style)
    if (targetIdx == -1) {
        float minLife = 99999.0f;
        for (int i = 0; i < MAX_DECALS; i++) {
            if (g_DecalPool[i].lifetime < minLife) {
                minLife = g_DecalPool[i].lifetime;
                targetIdx = i;
            }
        }
    }

    if (targetIdx != -1) {
        // Áp một Y offset nhỏ để tránh Z-fighting trùng khít mặt phẳng với mặt đất sàn sandbox
        g_DecalPool[targetIdx].position = (Vector3){ pos.x, pos.y + 0.02f, pos.z };
        g_DecalPool[targetIdx].rotation = rotation;
        g_DecalPool[targetIdx].scale = scale;
        g_DecalPool[targetIdx].texture = texture;
        g_DecalPool[targetIdx].lifetime = lifetime;
        g_DecalPool[targetIdx].maxLifetime = lifetime;
        g_DecalPool[targetIdx].tint = tint;
        g_DecalPool[targetIdx].active = true;

        g_LastSpawnedIndex = (targetIdx + 1) % MAX_DECALS;
    }
}

void DecalSystem_Update(float dt) {
    for (int i = 0; i < MAX_DECALS; i++) {
        if (!g_DecalPool[i].active) continue;

        g_DecalPool[i].lifetime -= dt;
        if (g_DecalPool[i].lifetime <= 0.0f) {
            g_DecalPool[i].active = false;
        }
    }
}

void DecalSystem_Draw(void) {
    // Ép kiểu Blend Mode thuần Alpha theo yêu cầu kiến trúc của bạn
    BeginBlendMode(BLEND_ALPHA);
    
    // Tắt ghi Depth Buffer nhưng vẫn giữ Depth Test để decal không bị nhìn xuyên qua tường/cột đá
    rlDisableDepthMask(); 

    BeginShaderMode(g_DecalShader);

    for (int i = 0; i < MAX_DECALS; i++) {
        if (!g_DecalPool[i].active) continue;

        DecalEntity *d = &g_DecalPool[i];
        
        // Tính toán độ mờ nhạt dần (Fade out) theo dòng đời còn lại
        float alphaRatio = d->lifetime / d->maxLifetime;
        Color finalColor = d->tint;
        finalColor.a = (unsigned char)(d->tint.a * alphaRatio);

        // Đẩy ma trận biến hình cục bộ (Local Transform Matrix) lên GPU
        rlPushMatrix();
            rlTranslatef(d->position.x, d->position.y, d->position.z);
            rlRotatef(d->rotation, 0.0f, 1.0f, 0.0f); // Xoay quanh trục đứng Y
            rlScalef(d->scale, 1.0f, d->scale);        // Co giãn trên mặt phẳng ngang XZ

            rlSetTexture(d->texture.id);
            rlBegin(RL_QUADS);
                rlColor4ub(finalColor.r, finalColor.g, finalColor.b, finalColor.a);
                
                // Vẽ tấm Quad nằm ngang song song mặt đất (XZ plane)
                // Top-Left
                rlTexCoord2f(0.0f, 0.0f);
                rlVertex3f(-0.5f, 0.0f, -0.5f);
                
                // Bottom-Left
                rlTexCoord2f(0.0f, 1.0f);
                rlVertex3f(-0.5f, 0.0f, 0.5f);
                
                // Bottom-Right
                rlTexCoord2f(1.0f, 1.0f);
                rlVertex3f(0.5f, 0.0f, 0.5f);
                
                // Top-Right
                rlTexCoord2f(1.0f, 0.0f);
                rlVertex3f(0.5f, 0.0f, -0.5f);
            rlEnd();
            rlSetTexture(0);
        rlPopMatrix();
    }

    EndShaderMode();

    rlEnableDepthMask();
    EndBlendMode();
}

void DecalSystem_Unload(void) {
    UnloadShader(g_DecalShader);
    for (int i = 0; i < MAX_DECALS; i++) {
        g_DecalPool[i].active = false;
    }
}