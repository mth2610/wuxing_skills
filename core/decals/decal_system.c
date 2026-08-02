#include "decal_system.h"
#include "core/resource_manager.h"
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
static Shader g_MaterialDecalShader;
// Uniform locations của uber-shader (decal_flow.fs), cache 1 lần ở Init
static int s_locFlowTime = -1, s_locFlowSpeed = -1, s_locFlowStrength = -1, s_locGlow = -1;
static int s_locMaterialErosion = -1;
static int s_locMaterialEmissivePass = -1;
static int s_locMaterialBaseTint = -1;
static int s_locMaterialEmissiveTint = -1;
static int s_locMaterialThreshold = -1;
static int s_locMaterialIntensity = -1;

// Camera compensation
static float g_cam_yaw = 0.0f;
static float g_cam_stretch = 1.0f;

// Cache lượng giác của camera để không phải tính lại ở mỗi decal
static float g_cos_cam_yaw = 1.0f;
static float g_sin_cam_yaw = 0.0f;

static DecalHandle Decal_MakeHandle(int idx)
{
    return ((g_DecalPool[idx].generation & 0x00ffffffu) << 8) | (unsigned int)(idx + 1);
}

static int Decal_ResolveHandle(DecalHandle handle)
{
    int idx = (int)(handle & 0xffu) - 1;
    unsigned int generation = handle >> 8;
    if (handle == DECAL_HANDLE_INVALID || idx < 0 || idx >= MAX_DECALS ||
        !g_DecalPool[idx].active || g_DecalPool[idx].generation != generation)
        return -1;
    return idx;
}

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

    if (d->oriented)
    {
        Vector3 bitangent = Vector3Normalize(Vector3CrossProduct(d->surfaceNormal,
                                                                   d->surfaceTangent));
        return Vector3Add(d->position,
                          Vector3Add(Vector3Scale(d->surfaceTangent, rx),
                                     Vector3Scale(bitangent, rz)));
    }

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
    // Camera foreshortening is only a readability correction for legacy
    // horizontal ground decals. A surface-aligned mark has real geometry and
    // must preserve its authored aspect ratio.
    float scale_z = d->scale * (d->oriented ? 1.0f : g_cam_stretch);

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
    // Uber-shader cho cả decal tĩnh lẫn decal flow: decal_flow.fs với mọi
    // uniform = 0 rút gọn ĐÚNG về decal.fs (cùng edge mask radial, texture
    // đứng yên) — không cần file decal_uber.fs riêng (chưa từng tồn tại;
    // trước đây load hụt → rơi về default shader, mất edge fade + flow).
    g_DecalShader   = ResourceManager_LoadShader(NULL, "core/decals/shaders/decal_flow.fs");
    g_MaterialDecalShader = ResourceManager_LoadShader("core/decals/shaders/decal_material.vs",
                                                        "core/decals/shaders/decal_material.fs");
    s_locFlowTime     = GetShaderLocation(g_DecalShader, "u_time");
    s_locFlowSpeed    = GetShaderLocation(g_DecalShader, "u_flowSpeed");
    s_locFlowStrength = GetShaderLocation(g_DecalShader, "u_flowStrength");
    s_locGlow         = GetShaderLocation(g_DecalShader, "u_glowIntensity");
    s_locMaterialErosion = GetShaderLocation(g_MaterialDecalShader, "u_erosion");
    s_locMaterialEmissivePass = GetShaderLocation(g_MaterialDecalShader, "u_emissivePass");
    s_locMaterialBaseTint = GetShaderLocation(g_MaterialDecalShader, "u_baseTint");
    s_locMaterialEmissiveTint = GetShaderLocation(g_MaterialDecalShader, "u_emissiveTint");
    s_locMaterialThreshold = GetShaderLocation(g_MaterialDecalShader, "u_emissiveThreshold");
    s_locMaterialIntensity = GetShaderLocation(g_MaterialDecalShader, "u_emissiveIntensity");
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
    d->generation = (d->generation + 1u) & 0x00ffffffu;
    if (d->generation == 0u) d->generation = 1u;
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
    d->fadeInSeconds = 0.0f;
    d->fadeOutSeconds = 0.0f;
    d->tint = tint;
    d->material = (DecalMaterialParams){ .baseTint = WHITE, .emissiveTint = WHITE,
                                         .emissiveThreshold = 1.1f, .emissiveIntensity = 0.0f };
    d->blendMode = blendMode;
    d->active = true;
    d->flowScroll = false;
    d->flowSpeed = 0.0f;
    d->flowStrength = 0.0f;
    d->glowIntensity = 0.0f;
    d->oriented = false;
    d->surfaceNormal = (Vector3){0.0f, 1.0f, 0.0f};
    d->surfaceTangent = (Vector3){1.0f, 0.0f, 0.0f};
    d->conformalStamp = false;
    d->heightFn = NULL;
    d->heightUserData = NULL;
    d->edgePhase = 0.0f;
    d->stampHeightsCached = false;
    d->stampSurfaceCached = false;
    g_LastSpawnedIndex = (idx + 1) % MAX_DECALS;
    Decal_Activate(idx);
    return idx;
}

DecalHandle DecalSystem_AddConformalMaterialEx(Vector3 pos, float rotation, float rotSpeed,
                                                float scaleStart, float scaleEnd,
                                                Texture2D texture, float lifetime, Color tint,
                                                BlendMode blendMode, float yOffset,
                                                GroundHeightSampleFn heightFn, void *heightUserData,
                                                GroundSurfaceSampleFn surfaceFn,
                                                float edgePhase, float fadeInSeconds,
                                                float fadeOutSeconds, float maxSlopeDegrees,
                                                const DecalMaterialParams *material)
{
    if (surfaceFn != NULL && maxSlopeDegrees < 90.0f)
    {
        Vector3 receiverPos, receiverNormal;
        if (surfaceFn(pos.x, pos.z, &receiverPos, &receiverNormal, heightUserData) &&
            receiverNormal.y < cosf(Clamp(maxSlopeDegrees, 0.0f, 90.0f) * DEG2RAD))
            return DECAL_HANDLE_INVALID;
    }
    else if (heightFn != NULL && maxSlopeDegrees < 90.0f)
    {
        float probe = fmaxf(scaleEnd * 0.5f, 0.10f);
        float dx = (heightFn(pos.x + probe, pos.z, heightUserData) -
                    heightFn(pos.x - probe, pos.z, heightUserData)) / (2.0f * probe);
        float dz = (heightFn(pos.x, pos.z + probe, heightUserData) -
                    heightFn(pos.x, pos.z - probe, heightUserData)) / (2.0f * probe);
        float normalY = 1.0f / sqrtf(1.0f + dx * dx + dz * dz);
        if (normalY < cosf(Clamp(maxSlopeDegrees, 0.0f, 90.0f) * DEG2RAD))
            return DECAL_HANDLE_INVALID;
    }
    int conformalCount = 0;
    int oldest = -1;
    float shortestLife = 0.0f;
    for (int a = 0; a < s_activeCount; ++a)
    {
        int id = s_activeIds[a];
        DecalEntity *candidate = &g_DecalPool[id];
        if (!candidate->conformalStamp) continue;
        conformalCount++;
        if (oldest < 0 || candidate->lifetime < shortestLife)
        {
            oldest = id;
            shortestLife = candidate->lifetime;
        }
    }
    // Material stamps are considerably denser than legacy quads. Keep a hard
    // independent budget so repeated impact events cannot consume a frame.
    if (conformalCount >= 12 && oldest >= 0)
        Decal_Deactivate(oldest);
    int idx = SpawnDecalCommon(pos, rotation, rotSpeed, scaleStart, scaleEnd, texture,
                               lifetime, tint, blendMode, yOffset);
    DecalEntity *d = &g_DecalPool[idx];
    d->conformalStamp = true;
    if (material != NULL)
        d->material = *material;
    d->heightFn = heightFn;
    d->heightUserData = heightUserData;
    d->edgePhase = edgePhase;
    d->fadeInSeconds = fadeInSeconds > 0.0f ? fadeInSeconds : 0.0f;
    d->fadeOutSeconds = fadeOutSeconds > 0.0f ? fadeOutSeconds : 0.0f;
    if (surfaceFn != NULL)
    {
        bool complete = true;
        for (int ring = 0; ring <= DECAL_STAMP_RINGS; ++ring)
        {
            float radial = (float)ring / DECAL_STAMP_RINGS;
            for (int sector = 0; sector < DECAL_STAMP_SECTORS; ++sector)
            {
                float angle = 2.0f * PI * sector / DECAL_STAMP_SECTORS;
                float radius = scaleEnd * radial * (1.0f + 0.075f * sinf(angle * 7.0f + edgePhase) +
                               0.045f * sinf(angle * 13.0f - edgePhase * 1.7f));
                float rot = angle + rotation * DEG2RAD;
                Vector3 point, normal;
                if (!surfaceFn(pos.x + cosf(rot) * radius, pos.z + sinf(rot) * radius,
                               &point, &normal, heightUserData)) { complete = false; break; }
                normal = Vector3Normalize(normal);
                d->stampSurfaceNormals[ring][sector] = normal;
                d->stampSurfacePositions[ring][sector] = Vector3Add(point, Vector3Scale(normal, yOffset));
            }
            if (!complete) break;
        }
        d->stampSurfaceCached = complete;
    }
    if (heightFn != NULL)
    {
        for (int ring = 0; ring <= DECAL_STAMP_RINGS; ++ring)
        {
            float radial = (float)ring / DECAL_STAMP_RINGS;
            for (int sector = 0; sector < DECAL_STAMP_SECTORS; ++sector)
            {
                float angle = 2.0f * PI * sector / DECAL_STAMP_SECTORS;
                float edge = 1.0f + 0.075f * sinf(angle * 7.0f + edgePhase) +
                             0.045f * sinf(angle * 13.0f - edgePhase * 1.7f);
                float radius = scaleEnd * radial * edge;
                float rot = angle + rotation * DEG2RAD;
                d->stampHeights[ring][sector] = heightFn(pos.x + cosf(rot) * radius,
                                                          pos.z + sinf(rot) * radius,
                                                          heightUserData);
            }
        }
        d->stampHeightsCached = true;
    }
    return Decal_MakeHandle(idx);
}

void DecalSystem_AddConformalEx(Vector3 pos, float rotation, float rotSpeed,
                                float scaleStart, float scaleEnd,
                                Texture2D texture, float lifetime, Color tint,
                                BlendMode blendMode, float yOffset,
                                GroundHeightSampleFn heightFn, void *heightUserData,
                                GroundSurfaceSampleFn surfaceFn,
                                float edgePhase, float fadeInSeconds,
                                float fadeOutSeconds, float maxSlopeDegrees)
{
    DecalSystem_AddConformalMaterialEx(pos, rotation, rotSpeed, scaleStart, scaleEnd,
                                       texture, lifetime, tint, blendMode, yOffset,
                                       heightFn, heightUserData, surfaceFn, edgePhase,
                                       fadeInSeconds, fadeOutSeconds, maxSlopeDegrees, NULL);
}

bool DecalSystem_Destroy(DecalHandle handle)
{
    int idx = Decal_ResolveHandle(handle);
    if (idx < 0) return false;
    Decal_Deactivate(idx);
    return true;
}

bool DecalSystem_IsAlive(DecalHandle handle)
{
    return Decal_ResolveHandle(handle) >= 0;
}

bool DecalSystem_SetTransform(DecalHandle handle, Vector3 position,
                              Vector3 normal, float rotationRadians)
{
    int idx = Decal_ResolveHandle(handle);
    if (idx < 0 || g_DecalPool[idx].conformalStamp) return false;
    DecalEntity *d = &g_DecalPool[idx];
    d->rotation = rotationRadians * RAD2DEG;
    if (d->oriented)
    {
        if (Vector3LengthSqr(normal) < 0.0001f) return false;
        d->surfaceNormal = Vector3Normalize(normal);
        Vector3 reference = fabsf(d->surfaceNormal.y) < 0.95f ?
            (Vector3){0.0f, 1.0f, 0.0f} : (Vector3){1.0f, 0.0f, 0.0f};
        d->surfaceTangent = Vector3Normalize(Vector3CrossProduct(reference, d->surfaceNormal));
        d->position = Vector3Add(position, Vector3Scale(d->surfaceNormal, d->yOffset));
    }
    else
        d->position = (Vector3){position.x, position.y + d->yOffset, position.z};
    return true;
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

void DecalSystem_AddOrientedEx(Vector3 pos, Vector3 normal, float rotation,
                               float rotSpeed, float scaleStart, float scaleEnd,
                               Texture2D texture, float lifetime, Color tint,
                               BlendMode blendMode, float yOffset)
{
    if (Vector3LengthSqr(normal) < 0.0001f)
        normal = (Vector3){0.0f, 1.0f, 0.0f};
    normal = Vector3Normalize(normal);

    Vector3 reference = fabsf(normal.y) < 0.95f ? (Vector3){0.0f, 1.0f, 0.0f}
                                                  : (Vector3){1.0f, 0.0f, 0.0f};
    Vector3 tangent = Vector3Normalize(Vector3CrossProduct(reference, normal));
    int idx = SpawnDecalCommon(pos, rotation, rotSpeed, scaleStart, scaleEnd, texture,
                               lifetime, tint, blendMode, 0.0f);
    DecalEntity *d = &g_DecalPool[idx];
    d->oriented = true;
    d->surfaceNormal = normal;
    d->surfaceTangent = tangent;
    d->position = Vector3Add(pos, Vector3Scale(normal, yOffset));
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

        if (d->conformalStamp || d->blendMode != mode || d->flowScroll != flowOnly)
            continue;

        // Trì hoãn việc kích hoạt Shader và BlendMode cho tới khi thực sự tìm thấy decal hợp lệ đầu tiên
        if (!shaderActive)
        {
            BeginBlendMode(mode);
            rlDrawRenderBatchActive(); // Flash batch hiện tại của Raylib
            rlDisableDepthMask();
            BeginShaderMode(g_DecalShader);
            // Nhóm tĩnh dùng CHUNG program với nhóm flow → phải reset 2 uniform
            // "chế độ" về 0 (flowStrength 0 vô hiệu cuộn, glow 0 tắt boost),
            // nếu không giá trị của nhóm flow vẽ trước đó rò sang decal tĩnh.
            if (!flowOnly)
            {
                float zero = 0.0f;
                SetShaderValue(g_DecalShader, s_locFlowStrength, &zero, SHADER_UNIFORM_FLOAT);
                SetShaderValue(g_DecalShader, s_locGlow, &zero, SHADER_UNIFORM_FLOAT);
            }
            shaderActive = true;
        }

        // Tối ưu Uniform: Nếu bắt buộc phải dùng Uniform cho Flow, chỉ cập nhật khi thông số thay đổi
        if (flowOnly)
        {
            // Nếu bạn chỉnh sửa Shader nhận dữ liệu qua Vertex, đoạn code SetShaderValue dưới đây sẽ biến mất, giúp tăng tốc X10.
            // Còn nếu giữ nguyên Uniform, dòng code dưới đây bắt buộc phải bẻ gãy batch:
            float elapsed = d->maxLifetime - d->lifetime;
            if (drawing)
            {
                rlEnd();
                drawing = false;
            } // Ép kết thúc cấu trúc hình cũ để nạp Uniform mới

            SetShaderValue(g_DecalShader, s_locFlowTime, &elapsed, SHADER_UNIFORM_FLOAT);
            SetShaderValue(g_DecalShader, s_locFlowSpeed, &d->flowSpeed, SHADER_UNIFORM_FLOAT);
            SetShaderValue(g_DecalShader, s_locFlowStrength, &d->flowStrength, SHADER_UNIFORM_FLOAT);
            SetShaderValue(g_DecalShader, s_locGlow, &d->glowIntensity, SHADER_UNIFORM_FLOAT);
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

static float Decal_StampRadius(const DecalEntity *d, float angle)
{
    return 1.0f + 0.075f * sinf(angle * 7.0f + d->edgePhase) +
           0.045f * sinf(angle * 13.0f - d->edgePhase * 1.7f);
}

static float Decal_ConformalFadeAlpha(const DecalEntity *d)
{
    float elapsed = d->maxLifetime - d->lifetime;
    float alpha = 1.0f;
    if (d->fadeInSeconds > 0.0f)
        alpha = fminf(alpha, elapsed / d->fadeInSeconds);
    if (d->fadeOutSeconds > 0.0f)
        alpha = fminf(alpha, d->lifetime / d->fadeOutSeconds);
    return Clamp(alpha, 0.0f, 1.0f);
}

static Vector3 Decal_StampVertex(const DecalEntity *d, float radial, float angle,
                                 int ring, int sector)
{
    float radius = d->scale * radial * Decal_StampRadius(d, angle);
    float rot = angle + d->rotation * DEG2RAD;
    float x = d->position.x + cosf(rot) * radius;
    float z = d->position.z + sinf(rot) * radius;
    float y = d->position.y;
    if (d->stampSurfaceCached)
        return d->stampSurfacePositions[ring][sector % DECAL_STAMP_SECTORS];
    if (d->stampHeightsCached)
        y = d->stampHeights[ring][sector % DECAL_STAMP_SECTORS] + d->yOffset;
    else if (d->heightFn != NULL)
        y = d->heightFn(x, z, d->heightUserData) + d->yOffset;
    return (Vector3){x, y, z};
}

static void Decal_StampEmitVertex(const DecalEntity *d, float radial, float angle,
                                  int ring, int sector)
{
    Vector3 v = Decal_StampVertex(d, radial, angle, ring, sector);
    float edge = Decal_StampRadius(d, angle);
    float u = 0.5f + cosf(angle) * radial * edge * 0.5f;
    float w = 0.5f + sinf(angle) * radial * edge * 0.5f;
    Vector3 normal = d->stampSurfaceCached ?
        d->stampSurfaceNormals[ring][sector % DECAL_STAMP_SECTORS] : (Vector3){0.0f, 1.0f, 0.0f};
    rlNormal3f(normal.x, normal.y, normal.z);
    rlTexCoord2f(u, w);
    rlVertex3f(v.x, v.y, v.z);
}

static void DrawConformalGroup(BlendMode renderMode, BlendMode sourceMode, bool emissivePass)
{
    bool active = false;
    for (int a = 0; a < s_activeCount; ++a)
    {
        DecalEntity *d = &g_DecalPool[s_activeIds[a]];
        if (d->conformalStamp && d->blendMode == sourceMode &&
            d->texture.id != 0)
        {
            active = true;
            break;
        }
    }
    if (!active) return;
    rlDrawRenderBatchActive();
    BeginBlendMode(renderMode);
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlDrawRenderBatchActive();
    // Depth testing must remain enabled: a ground decal may sit fractionally
    // above terrain, but it must never paint through characters or props.
    rlDisableBackfaceCulling();
    rlDrawRenderBatchActive();
    BeginShaderMode(g_MaterialDecalShader);
    int useEmissive = emissivePass ? 1 : 0;
    SetShaderValue(g_MaterialDecalShader, s_locMaterialEmissivePass, &useEmissive,
                   SHADER_UNIFORM_INT);

    for (int a = 0; a < s_activeCount; ++a)
    {
        DecalEntity *d = &g_DecalPool[s_activeIds[a]];
        if (!d->conformalStamp || d->blendMode != sourceMode || d->texture.id == 0)
            continue;
        float erosion = 1.0f - d->lifetime / d->maxLifetime;
        Color c = d->tint;
        c.a = (unsigned char)(c.a * (1.0f - erosion * erosion) *
                              Decal_ConformalFadeAlpha(d));
        SetShaderValue(g_MaterialDecalShader, s_locMaterialErosion, &erosion, SHADER_UNIFORM_FLOAT);
        float baseTint[4] = { d->material.baseTint.r / 255.0f, d->material.baseTint.g / 255.0f,
                              d->material.baseTint.b / 255.0f, d->material.baseTint.a / 255.0f };
        float emissiveTint[4] = { d->material.emissiveTint.r / 255.0f, d->material.emissiveTint.g / 255.0f,
                                  d->material.emissiveTint.b / 255.0f, d->material.emissiveTint.a / 255.0f };
        SetShaderValue(g_MaterialDecalShader, s_locMaterialBaseTint, baseTint, SHADER_UNIFORM_VEC4);
        SetShaderValue(g_MaterialDecalShader, s_locMaterialEmissiveTint, emissiveTint, SHADER_UNIFORM_VEC4);
        SetShaderValue(g_MaterialDecalShader, s_locMaterialThreshold, &d->material.emissiveThreshold, SHADER_UNIFORM_FLOAT);
        SetShaderValue(g_MaterialDecalShader, s_locMaterialIntensity, &d->material.emissiveIntensity, SHADER_UNIFORM_FLOAT);
        rlSetTexture(d->texture.id);
        rlColor4ub(c.r, c.g, c.b, c.a);
        rlBegin(RL_TRIANGLES);
        for (int ring = 0; ring < DECAL_STAMP_RINGS; ++ring)
        {
            float r0 = (float)ring / DECAL_STAMP_RINGS;
            float r1 = (float)(ring + 1) / DECAL_STAMP_RINGS;
            for (int sector = 0; sector < DECAL_STAMP_SECTORS; ++sector)
            {
                float a0 = 2.0f * PI * sector / DECAL_STAMP_SECTORS;
                float a1 = 2.0f * PI * (sector + 1) / DECAL_STAMP_SECTORS;
                Decal_StampEmitVertex(d, r0, a0, ring, sector);
                Decal_StampEmitVertex(d, r1, a0, ring + 1, sector);
                Decal_StampEmitVertex(d, r1, a1, ring + 1, sector + 1);
                Decal_StampEmitVertex(d, r0, a0, ring, sector);
                Decal_StampEmitVertex(d, r1, a1, ring + 1, sector + 1);
                Decal_StampEmitVertex(d, r0, a1, ring, sector + 1);
            }
        }
        rlEnd();
    }

    rlSetTexture(0);
    rlDrawRenderBatchActive();
    EndShaderMode();
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    rlDrawRenderBatchActive();
    EndBlendMode();
    rlDrawRenderBatchActive();
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
    DrawConformalGroup(BLEND_ALPHA, BLEND_ALPHA, false);
    DrawConformalGroup(BLEND_ADDITIVE, BLEND_ALPHA, true);
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
    if (active != NULL) *active = s_activeCount;
    if (max != NULL) *max = MAX_DECALS;
}
