#include "decal_system.h"
#include "resource_manager.h"
#include "rlgl.h"
#include "raymath.h"
#include <stddef.h>
#include <math.h>

static DecalEntity g_DecalPool[MAX_DECALS];
static int g_LastSpawnedIndex = 0;
static Shader g_DecalShader;
static Shader g_DecalFlowShader;
static int g_flowTimeLoc, g_flowSpeedLoc, g_flowStrengthLoc;

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
    g_DecalFlowShader = ResourceManager_LoadShader(NULL, "core/shaders/decal_flow.fs");
    g_flowTimeLoc = GetShaderLocation(g_DecalFlowShader, "u_time");
    g_flowSpeedLoc = GetShaderLocation(g_DecalFlowShader, "u_flowSpeed");
    g_flowStrengthLoc = GetShaderLocation(g_DecalFlowShader, "u_flowStrength");
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

static int SpawnDecalCommon(Vector3 pos, float rotation, float rotSpeed,
                            float scaleStart, float scaleEnd,
                            Texture2D texture, float lifetime,
                            Color tint, BlendMode blendMode, float yOffset) {
    int idx = FindSlot();
    DecalEntity *d = &g_DecalPool[idx];
    d->position     = (Vector3){ pos.x, pos.y + yOffset, pos.z };
    d->rotation     = rotation;
    d->rotSpeed     = rotSpeed;
    d->scale        = scaleStart;
    d->scaleStart   = scaleStart;
    d->scaleEnd     = scaleEnd;
    d->yOffset      = yOffset;
    d->texture      = texture;
    d->lifetime     = lifetime;
    d->maxLifetime  = lifetime;
    d->tint         = tint;
    d->blendMode    = blendMode;
    d->active       = true;
    d->flowScroll   = false;
    d->flowSpeed    = 0.0f;
    d->flowStrength = 0.0f;
    g_LastSpawnedIndex = (idx + 1) % MAX_DECALS;
    return idx;
}

void DecalSystem_AddEx(Vector3 pos, float rotation, float rotSpeed,
                       float scaleStart, float scaleEnd,
                       Texture2D texture, float lifetime,
                       Color tint, BlendMode blendMode, float yOffset) {
    SpawnDecalCommon(pos, rotation, rotSpeed, scaleStart, scaleEnd, texture,
                     lifetime, tint, blendMode, yOffset);
}

void DecalSystem_AddFlowEx(Vector3 pos, float rotation, float rotSpeed,
                           float scaleStart, float scaleEnd,
                           Texture2D texture, float lifetime,
                           Color tint, BlendMode blendMode, float yOffset,
                           float flowSpeed, float flowStrength) {
    int idx = SpawnDecalCommon(pos, rotation, rotSpeed, scaleStart, scaleEnd,
                               texture, lifetime, tint, blendMode, yOffset);
    g_DecalPool[idx].flowScroll = true;
    g_DecalPool[idx].flowSpeed = flowSpeed;
    g_DecalPool[idx].flowStrength = flowStrength;
}

void DecalSystem_Add(Vector3 pos, float rotation, float scale,
                     Texture2D texture, float lifetime, Color tint) {
    DecalSystem_AddEx(pos, rotation, 0.0f, scale, scale,
                      texture, lifetime, tint, BLEND_ALPHA, 0.02f);
}

void DecalSystem_AddStreak(const Vector3 *points, int count, float rotation,
                           float scale, Texture2D texture, float lifetime, Color tint) {
    if (points == NULL) return;
    for (int i = 0; i < count; i++) {
        DecalSystem_Add(points[i], rotation, scale, texture, lifetime, tint);
    }
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

// Vẽ tất cả decal có blendMode + flowScroll chỉ định, gọi lần lượt từ Draw().
// flowOnly tách riêng decal cuộn (dùng g_DecalFlowShader, cần set u_time/
// u_flowSpeed/u_flowStrength mỗi decal) khỏi decal tĩnh (g_DecalShader,
// không đổi gì so với trước) — không có decal flow nào thì pass này no-op.
static void DrawGroup(BlendMode mode, bool flowOnly) {
    bool hasAny = false;
    for (int i = 0; i < MAX_DECALS; i++) {
        if (g_DecalPool[i].active && g_DecalPool[i].blendMode == mode &&
            g_DecalPool[i].flowScroll == flowOnly) {
            hasAny = true;
            break;
        }
    }
    if (!hasAny) return;

    Shader shader = flowOnly ? g_DecalFlowShader : g_DecalShader;

    BeginBlendMode(mode);
    rlDisableDepthMask();
    BeginShaderMode(shader);

    for (int i = 0; i < MAX_DECALS; i++) {
        DecalEntity *d = &g_DecalPool[i];
        if (!d->active || d->blendMode != mode || d->flowScroll != flowOnly) continue;

        float alphaRatio = d->lifetime / d->maxLifetime;
        Color c = d->tint;
        c.a = (unsigned char)(d->tint.a * alphaRatio);

        if (flowOnly) {
            float elapsed = d->maxLifetime - d->lifetime;
            SetShaderValue(shader, g_flowTimeLoc, &elapsed, SHADER_UNIFORM_FLOAT);
            SetShaderValue(shader, g_flowSpeedLoc, &d->flowSpeed, SHADER_UNIFORM_FLOAT);
            SetShaderValue(shader, g_flowStrengthLoc, &d->flowStrength, SHADER_UNIFORM_FLOAT);
        }

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
    // Alpha trước (vết đất, puddle, scorch) → Additive sau (glow, rune sáng).
    // Trong mỗi blend mode: decal tĩnh trước, decal flow sau (cùng thứ tự
    // tổng thể như trước, chỉ thêm 1 pass shader riêng cho flow).
    DrawGroup(BLEND_ALPHA, false);
    DrawGroup(BLEND_ALPHA, true);
    DrawGroup(BLEND_ADDITIVE, false);
    DrawGroup(BLEND_ADDITIVE, true);
    DrawGroup(BLEND_MULTIPLIED, false);
    DrawGroup(BLEND_MULTIPLIED, true);
}

void DecalSystem_Unload(void) {
    // ResourceManager tự giải phóng shader khi app shutdown — không UnloadShader ở đây
    for (int i = 0; i < MAX_DECALS; i++) {
        g_DecalPool[i].active = false;
    }
}
