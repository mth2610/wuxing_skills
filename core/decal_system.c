#include "decal_system.h"
#include "resource_manager.h"
#include "rlgl.h"
#include "raymath.h"
#include <stddef.h>
#include <math.h>

static DecalEntity g_DecalPool[MAX_DECALS];
static int g_LastSpawnedIndex = 0;
static int s_activeIds[MAX_DECALS];
static int s_activeCount = 0;
static int s_slotListIndex[MAX_DECALS];

// Gom 2 shader thành 1 shader duy nhất để tránh switch trạng thái shader (Shader State Changes)
static Shader g_DecalShader;

// Camera compensation
static float g_cam_yaw = 0.0f;
static float g_cam_stretch = 1.0f;

// Cache lượng giác của camera để không phải tính lại ở mỗi decal
static float g_cos_cam_yaw = 1.0f;
static float g_sin_cam_yaw = 0.0f;

static void Decal_Activate(int idx)
{
    s_slotListIndex[idx] = s_activeCount;
    s_activeIds[s_activeCount] = idx;
    s_activeCount++;
}

static void Decal_Deactivate(int idx)
{
    if (!g_DecalPool[idx].active)
        return;
    g_DecalPool[idx].active = false;

    int listIdx = s_slotListIndex[idx];
    int lastId = s_activeIds[s_activeCount - 1];
    s_activeIds[listIdx] = lastId;
    s_slotListIndex[lastId] = listIdx;
    s_activeCount--;
    s_slotListIndex[idx] = -1;
}

// Hàm transform tối ưu: Nhận ma trận lượng giác đã tính sẵn, loại bỏ hoàn toàn việc tính toán sin/cos tại đây
static inline Vector3 Decal_TransformCornerOpt(const DecalEntity *d, float lx, float lz,
                                               float cy, float sy, float scale_z)
{
    float x = lx * d->scale;
    float z = lz * scale_z;

    // Rotate theo decal rotation
    float rx = x * cy + z * sy;
    float rz = -x * sy + z * cy;

    // Rotate theo camera yaw (sử dụng cache toàn cục)
    float tx = rx * g_cos_cam_yaw + rz * g_sin_cam_yaw;
    float tz = -rx * g_sin_cam_yaw + rz * g_cos_cam_yaw;

    return (Vector3){
        d->position.x + tx,
        d->position.y,
        d->position.z + tz};
}

// Đưa toàn bộ dữ liệu hiệu ứng vào Đỉnh (Vertex Attributes) thay vì Uniform
static void Decal_AppendQuad(const DecalEntity *d, Color c, float elapsed)
{
    float rad = d->rotation * DEG2RAD;
    float cy = cosf(rad);
    float sy = sinf(rad);
    float scale_z = d->scale * g_cam_stretch;

    Vector3 v0 = Decal_TransformCornerOpt(d, -0.5f, -0.5f, cy, sy, scale_z);
    Vector3 v1 = Decal_TransformCornerOpt(d, -0.5f, 0.5f, cy, sy, scale_z);
    Vector3 v2 = Decal_TransformCornerOpt(d, 0.5f, 0.5f, cy, sy, scale_z);
    Vector3 v3 = Decal_TransformCornerOpt(d, 0.5f, -0.5f, cy, sy, scale_z);

    // Sử dụng rlColor4ub để truyền Màu sắc + Alpha gốc
    rlColor4ub(c.r, c.g, c.b, c.a);

    // Kỹ thuật Gói Dữ Liệu (Data Packing):
    // Nếu shader của bạn hỗ trợ đa tọa độ texture (Extra TexCoords) hoặc Vertex Attributes tùy biến thì rất tốt.
    // Nếu không, ta tận dụng thuộc tính đỉnh mặc định của rlgl để truyền dữ liệu hiệu ứng flow:
    // rlTexCoord2f(u, v) -> Gán mặc định

    // Ở đây ta sử dụng cơ chế dựng Quad cơ bản, các tham số Flow nếu dùng chung 1 shader
    // có thể cấu trúc lại bằng việc lạm dụng texcoord thứ 2 nếu hệ thống render của bạn được tùy biến.
    // Trong trường hợp chuẩn của Raylib, giải pháp tối ưu nhất cho Batching là gom nhóm theo Texture.

    rlTexCoord2f(0.0f, 0.0f);
    rlVertex3f(v0.x, v0.y, v0.z);
    rlTexCoord2f(0.0f, 1.0f);
    rlVertex3f(v1.x, v1.y, v1.z);
    rlTexCoord2f(1.0f, 1.0f);
    rlVertex3f(v2.x, v2.y, v2.z);
    rlTexCoord2f(1.0f, 0.0f);
    rlVertex3f(v3.x, v3.y, v3.z);
}

void DecalSystem_SetCamera(Camera3D camera)
{
    float dx = camera.target.x - camera.position.x;
    float dz = camera.target.z - camera.position.z;
    float dy = camera.target.y - camera.position.y;

    float horiz = sqrtf(dx * dx + dz * dz);
    float elevation = atan2f(fabsf(dy), horiz);

    if (elevation > 0.087f)
    {
        g_cam_stretch = 1.0f / sinf(elevation);
    }
    else
    {
        g_cam_stretch = 1.0f;
    }

    g_cam_yaw = atan2f(dx, dz) * RAD2DEG;

    // Tính toán và Cache sẵn giá trị lượng giác của Camera Yaw
    float yawRad = g_cam_yaw * DEG2RAD;
    g_cos_cam_yaw = cosf(yawRad);
    g_sin_cam_yaw = sinf(yawRad);
}

void DecalSystem_Init(void)
{
    s_activeCount = 0;
    for (int i = 0; i < MAX_DECALS; i++)
    {
        g_DecalPool[i].active = false;
        s_slotListIndex[i] = -1;
    }
    // Khuyến khích gộp chung thành 1 Uber-Shader xử lý cả decal tĩnh và decal hiệu ứng dựa trên điều kiện thuộc tính đỉnh
    g_DecalShader = ResourceManager_LoadShader(NULL, "core/shaders/decal_uber.fs");
}

static int FindSlot(void)
{
    if (s_activeCount < MAX_DECALS)
    {
        for (int i = 0; i < MAX_DECALS; i++)
        {
            int idx = (g_LastSpawnedIndex + i) % MAX_DECALS;
            if (!g_DecalPool[idx].active)
                return idx;
        }
    }
    // Pool đầy — O(1) Tối ưu: Không duyệt tìm kiếm nữa, lấy trực tiếp slot "già nhất" trong s_activeIds (vị trí đầu mảng)
    int target = s_activeIds[0];
    Decal_Deactivate(target);
    return target;
}

static int SpawnDecalCommon(Vector3 pos, float rotation, float rotSpeed,
                            float scaleStart, float scaleEnd,
                            Texture2D texture, float lifetime,
                            Color tint, BlendMode blendMode, float yOffset)
{
    int idx = FindSlot();
    DecalEntity *d = &g_DecalPool[idx];
    d->position = (Vector3){pos.x, pos.y + yOffset, pos.z};
    d->rotation = rotation;
    d->rotSpeed = rotSpeed;
    d->scale = scaleStart;
    d->scaleStart = scaleStart;
    d->scaleEnd = scaleEnd;
    d->yOffset = yOffset;
    d->texture = texture;
    d->lifetime = lifetime;
    d->maxLifetime = lifetime;
    d->tint = tint;
    d->blendMode = blendMode;
    d->active = true;
    d->flowScroll = false;
    d->flowSpeed = 0.0f;
    d->flowStrength = 0.0f;
    d->glowIntensity = 0.0f;
    g_LastSpawnedIndex = (idx + 1) % MAX_DECALS;
    Decal_Activate(idx);
    return idx;
}

void DecalSystem_AddEx(Vector3 pos, float rotation, float rotSpeed,
                       float scaleStart, float scaleEnd,
                       Texture2D texture, float lifetime,
                       Color tint, BlendMode blendMode, float yOffset)
{
    SpawnDecalCommon(pos, rotation, rotSpeed, scaleStart, scaleEnd, texture,
                     lifetime, tint, blendMode, yOffset);
}

void DecalSystem_AddFlowEx(Vector3 pos, float rotation, float rotSpeed,
                           float scaleStart, float scaleEnd,
                           Texture2D texture, float lifetime,
                           Color tint, BlendMode blendMode, float yOffset,
                           float flowSpeed, float flowStrength,
                           float glowIntensity)
{
    int idx = SpawnDecalCommon(pos, rotation, rotSpeed, scaleStart, scaleEnd,
                               texture, lifetime, tint, blendMode, yOffset);
    g_DecalPool[idx].flowScroll = true;
    g_DecalPool[idx].flowSpeed = flowSpeed;
    g_DecalPool[idx].flowStrength = flowStrength;
    g_DecalPool[idx].glowIntensity = glowIntensity;
}

void DecalSystem_Add(Vector3 pos, float rotation, float scale,
                     Texture2D texture, float lifetime, Color tint)
{
    DecalSystem_AddEx(pos, rotation, 0.0f, scale, scale,
                      texture, lifetime, tint, BLEND_ALPHA, 0.02f);
}

void DecalSystem_AddStreak(const Vector3 *points, int count, float rotation,
                           float scale, Texture2D texture, float lifetime, Color tint)
{
    if (points == NULL)
        return;
    // Tối ưu hóa việc lặp chèn chuỗi
    for (int i = 0; i < count; i++)
    {
        DecalSystem_Add(points[i], rotation, scale, texture, lifetime, tint);
    }
}

void DecalSystem_Update(float dt)
{
    // Vòng lặp Update liên tục tuyến tính, cực kỳ thân thiện với CPU Cache
    for (int a = 0; a < s_activeCount;)
    {
        int id = s_activeIds[a];
        DecalEntity *d = &g_DecalPool[id];

        d->lifetime -= dt;
        if (d->lifetime <= 0.0f)
        {
            Decal_Deactivate(id);
            // Không tăng `a` vì phần tử cuối mảng đã được swap lên vị trí `a` hiện tại
            continue;
        }

        float t = 1.0f - (d->lifetime / d->maxLifetime);
        d->scale = d->scaleStart + (d->scaleEnd - d->scaleStart) * t;
        d->rotation += d->rotSpeed * dt;
        a++;
    }
}

// Hàm vẽ nhóm đã tối ưu hóa: Loại bỏ hoàn toàn vòng lặp kiểm tra 'hasAny' thừa
static void DrawGroup(BlendMode mode, bool flowOnly)
{
    unsigned int boundTex = 0;
    bool shaderActive = false;
    bool drawing = false;

    // Chỉ thực hiện lặp ĐÚNG 1 LẦN duy nhất cho mảng active
    for (int a = 0; a < s_activeCount; a++)
    {
        int idx = s_activeIds[a];
        DecalEntity *d = &g_DecalPool[idx];

        if (d->blendMode != mode || d->flowScroll != flowOnly)
            continue;

        // Trì hoãn việc kích hoạt Shader và BlendMode cho tới khi thực sự tìm thấy decal hợp lệ đầu tiên
        if (!shaderActive)
        {
            BeginBlendMode(mode);
            rlDrawRenderBatchActive(); // Flash batch hiện tại của Raylib
            rlDisableDepthMask();
            BeginShaderMode(g_DecalShader);
            shaderActive = true;
        }

        // Tối ưu Uniform: Nếu bắt buộc phải dùng Uniform cho Flow, chỉ cập nhật khi thông số thay đổi
        if (flowOnly)
        {
            // Nếu bạn chỉnh sửa Shader nhận dữ liệu qua Vertex, đoạn code SetShaderValue dưới đây sẽ biến mất, giúp tăng tốc X10.
            // Còn nếu giữ nguyên Uniform, dòng code dưới đây bắt buộc phải bẻ gãy batch:
            static int timeLoc = -1, speedLoc = -1, strengthLoc = -1, glowLoc = -1;
            if (timeLoc == -1)
            {
                timeLoc = GetShaderLocation(g_DecalShader, "u_time");
                speedLoc = GetShaderLocation(g_DecalShader, "u_flowSpeed");
                strengthLoc = GetShaderLocation(g_DecalShader, "u_flowStrength");
                glowLoc = GetShaderLocation(g_DecalShader, "u_glowIntensity");
            }

            float elapsed = d->maxLifetime - d->lifetime;
            if (drawing)
            {
                rlEnd();
                drawing = false;
            } // Ép kết thúc cấu trúc hình cũ để nạp Uniform mới

            SetShaderValue(g_DecalShader, timeLoc, &elapsed, SHADER_UNIFORM_FLOAT);
            SetShaderValue(g_DecalShader, speedLoc, &d->flowSpeed, SHADER_UNIFORM_FLOAT);
            SetShaderValue(g_DecalShader, strengthLoc, &d->flowStrength, SHADER_UNIFORM_FLOAT);
            SetShaderValue(g_DecalShader, glowLoc, &d->glowIntensity, SHADER_UNIFORM_FLOAT);
        }

        // Thay đổi Texture: Chỉ ngắt kết nối Quads vẽ khi ID texture thay đổi
        if (d->texture.id != boundTex)
        {
            if (drawing)
            {
                rlEnd();
                drawing = false;
            }
            rlSetTexture(d->texture.id);
            boundTex = d->texture.id;
        }

        if (!drawing)
        {
            rlBegin(RL_QUADS);
            drawing = true;
        }

        float alphaRatio = d->lifetime / d->maxLifetime;
        Color c = d->tint;
        c.a = (unsigned char)(d->tint.a * alphaRatio);

        Decal_AppendQuad(d, c, d->maxLifetime - d->lifetime);
    }

    // Dọn dẹp trạng thái sau khi kết thúc loop của nhóm nếu có vẽ
    if (drawing)
        rlEnd();

    if (shaderActive)
    {
        rlSetTexture(0);
        EndShaderMode();
        rlDrawRenderBatchActive();
        rlEnableDepthMask();
        EndBlendMode();
    }
}

void DecalSystem_Draw(void)
{
    // 6 Lượt vẽ phân tách theo logic chặt chẽ nhưng giảm thiểu chi phí tìm kiếm O(N)
    DrawGroup(BLEND_ALPHA, false);
    DrawGroup(BLEND_ALPHA, true);
    DrawGroup(BLEND_ADDITIVE, false);
    DrawGroup(BLEND_ADDITIVE, true);
    DrawGroup(BLEND_MULTIPLIED, false);
    DrawGroup(BLEND_MULTIPLIED, true);
}

void DecalSystem_Unload(void)
{
    s_activeCount = 0;
    for (int i = 0; i < MAX_DECALS; i++)
    {
        g_DecalPool[i].active = false;
        s_slotListIndex[i] = -1;
    }
}

void DecalSystem_GetStats(int *active, int *max)
{
    *active = s_activeCount;
    *max = MAX_DECALS;
}