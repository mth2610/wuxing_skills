#include "decal_system.h"
#include "resource_manager.h"
#include "rlgl.h"
#include "raymath.h"
#include <stddef.h>
#include <math.h>

static DecalEntity g_DecalPool[MAX_DECALS];
static int g_LastSpawnedIndex = 0;
static Shader g_DecalShader;

// Camera compensation — bù foreshortening khi camera nhìn từ góc nghiêng
static float g_cam_yaw     = 0.0f;   // góc xoay camera quanh Y (độ)
static float g_cam_stretch = 1.0f;   // hệ số kéo dài trục Z-forward để circle không thành ellipse

void DecalSystem_SetCamera(Camera3D camera) {
    // Vector từ camera xuống target, chiếu lên mặt phẳng XZ
    float dx = camera.target.x - camera.position.x;
    float dz = camera.target.z - camera.position.z;
    float dy = camera.target.y - camera.position.y;

    float horiz = sqrtf(dx*dx + dz*dz);

    // Góc elevation (từ mặt đất lên camera)
    float elevation = atan2f(fabsf(dy), horiz);  // radian

    // Bảo vệ: nếu camera gần như thẳng đứng, không cần bù
    if (elevation > 0.087f) {  // > 5 độ
        g_cam_stretch = 1.0f / sinf(elevation);
    } else {
        g_cam_stretch = 1.0f;
    }

    // Yaw: hướng camera nhìn trên mặt phẳng XZ (dùng để rotate quad trước khi stretch)
    g_cam_yaw = atan2f(dx, dz) * RAD2DEG;
}

void DecalSystem_Init(void) {
    for (int i = 0; i < MAX_DECALS; i++) {
        g_DecalPool[i].active = false;
    }
    g_DecalShader = ResourceManager_LoadShader(NULL, "core/shaders/decal.fs");
}

static int FindSlot(void) {
    for (int i = 0; i < MAX_DECALS; i++) {
        int idx = (g_LastSpawnedIndex + i) % MAX_DECALS;
        if (!g_DecalPool[idx].active) return idx;
    }
    // Pool đầy — ghi đè slot có lifetime thấp nhất
    float minLife = 99999.0f;
    int target = 0;
    for (int i = 0; i < MAX_DECALS; i++) {
        if (g_DecalPool[i].lifetime < minLife) {
            minLife = g_DecalPool[i].lifetime;
            target = i;
        }
    }
    return target;
}

void DecalSystem_AddEx(Vector3 pos, float rotation, float rotSpeed,
                       float scaleStart, float scaleEnd,
                       Texture2D texture, float lifetime,
                       Color tint, BlendMode blendMode, float yOffset) {
    int idx = FindSlot();
    DecalEntity *d = &g_DecalPool[idx];
    d->position    = (Vector3){ pos.x, pos.y + yOffset, pos.z };
    d->rotation    = rotation;
    d->rotSpeed    = rotSpeed;
    d->scale       = scaleStart;
    d->scaleStart  = scaleStart;
    d->scaleEnd    = scaleEnd;
    d->yOffset     = yOffset;
    d->texture     = texture;
    d->lifetime    = lifetime;
    d->maxLifetime = lifetime;
    d->tint        = tint;
    d->blendMode   = blendMode;
    d->active      = true;
    g_LastSpawnedIndex = (idx + 1) % MAX_DECALS;
}

void DecalSystem_Add(Vector3 pos, float rotation, float scale,
                     Texture2D texture, float lifetime, Color tint) {
    DecalSystem_AddEx(pos, rotation, 0.0f, scale, scale,
                      texture, lifetime, tint, BLEND_ALPHA, 0.02f);
}

void DecalSystem_Update(float dt) {
    for (int i = 0; i < MAX_DECALS; i++) {
        DecalEntity *d = &g_DecalPool[i];
        if (!d->active) continue;

        d->lifetime -= dt;
        if (d->lifetime <= 0.0f) {
            d->active = false;
            continue;
        }

        float t = 1.0f - (d->lifetime / d->maxLifetime); // 0 → 1 theo thời gian
        d->scale    = d->scaleStart + (d->scaleEnd - d->scaleStart) * t;
        d->rotation += d->rotSpeed * dt;
    }
}

// Vẽ tất cả decal có blendMode chỉ định, gọi lần lượt từ Draw()
static void DrawGroup(BlendMode mode) {
    bool hasAny = false;
    for (int i = 0; i < MAX_DECALS; i++) {
        if (g_DecalPool[i].active && g_DecalPool[i].blendMode == mode) {
            hasAny = true;
            break;
        }
    }
    if (!hasAny) return;

    BeginBlendMode(mode);
    rlDisableDepthMask();
    BeginShaderMode(g_DecalShader);

    for (int i = 0; i < MAX_DECALS; i++) {
        DecalEntity *d = &g_DecalPool[i];
        if (!d->active || d->blendMode != mode) continue;

        float alphaRatio = d->lifetime / d->maxLifetime;
        Color c = d->tint;
        c.a = (unsigned char)(d->tint.a * alphaRatio);

        rlPushMatrix();
            rlTranslatef(d->position.x, d->position.y, d->position.z);
            rlRotatef(d->rotation, 0.0f, 1.0f, 0.0f);
            // Bù foreshortening: rotate về hướng camera-forward, stretch Z, rotate lại
            // Kết quả: circle texture → hiển thị tròn trên màn hình dù camera nghiêng
            rlRotatef(g_cam_yaw,  0.0f, 1.0f, 0.0f);
            rlScalef(d->scale, 1.0f, d->scale * g_cam_stretch);
            rlRotatef(-g_cam_yaw, 0.0f, 1.0f, 0.0f);

            rlSetTexture(d->texture.id);
            rlBegin(RL_QUADS);
                rlColor4ub(c.r, c.g, c.b, c.a);
                rlTexCoord2f(0.0f, 0.0f); rlVertex3f(-0.5f, 0.0f, -0.5f);
                rlTexCoord2f(0.0f, 1.0f); rlVertex3f(-0.5f, 0.0f,  0.5f);
                rlTexCoord2f(1.0f, 1.0f); rlVertex3f( 0.5f, 0.0f,  0.5f);
                rlTexCoord2f(1.0f, 0.0f); rlVertex3f( 0.5f, 0.0f, -0.5f);
            rlEnd();
            rlSetTexture(0);
        rlPopMatrix();
    }

    EndShaderMode();
    rlEnableDepthMask();
    EndBlendMode();
}

void DecalSystem_Draw(void) {
    // Alpha trước (vết đất, puddle, scorch) → Additive sau (glow, rune sáng)
    DrawGroup(BLEND_ALPHA);
    DrawGroup(BLEND_ADDITIVE);
    DrawGroup(BLEND_MULTIPLIED);
}

void DecalSystem_Unload(void) {
    // ResourceManager tự giải phóng shader khi app shutdown — không UnloadShader ở đây
    for (int i = 0; i < MAX_DECALS; i++) {
        g_DecalPool[i].active = false;
    }
}
