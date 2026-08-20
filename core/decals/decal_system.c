#include "decal_system.h"
#include "core/resource_manager.h"
#include "core/vfx_render.h"
#include "rlgl.h"
#include "raymath.h"
#include <stddef.h>
#include <math.h>

static DecalEntity g_DecalPool[MAX_DECALS];
static int g_LastSpawnedIndex = 0;
static int s_activeIds[MAX_DECALS];
static int s_activeCount = 0;
static int s_slotListIndex[MAX_DECALS];
static int s_renderIds[MAX_DECALS];
static int s_renderCount = 0;
static int s_renderCulledCount = 0;
static int s_renderFrustumCulledCount = 0;
static int s_renderLodCulledCount = 0;
static int s_renderEmissiveSuppressedCount = 0;
static int s_renderDistanceCulledCount = 0;
static unsigned char s_renderLod[MAX_DECALS];
static bool s_renderEmissiveAllowed[MAX_DECALS];
static float s_renderBaseCoverage = 0.0f;
static float s_renderEmissiveCoverage = 0.0f;
static int s_renderLegacySubmissions = 0;
static int s_renderConformalSubmissions = 0;
static int s_renderMaterialSwitches = 0;
static int s_renderTextureSwitches = 0;

/* One decal_flow.fs SOURCE, one compiled program per blend law it is drawn
 * under. A decal picks its blend per instance, so the shader cannot know
 * statically which resolver it owes; the OUTPUT_* define decides, and the
 * consumer asks for the variant matching the blend it is about to set. That
 * makes the (blend, resolver) pair structural — a mismatch cannot be expressed
 * here, whereas a runtime `if` inside the shader would still allow one. */
typedef struct {
    Shader shader;
    int    locTime;
    bool   loaded;
} DecalFlowVariant;
static DecalFlowVariant s_flowVariants[3]; /* indexed by VFXSurfaceMode */
static Shader g_MaterialDecalShader;
// Uniform locations của uber-shader (decal_flow.fs), cache 1 lần ở Init
static int s_locMaterialEmissivePass = -1;
static int s_locMaterialBaseTint = -1;
static int s_locMaterialEmissiveTint = -1;
static int s_locMaterialThreshold = -1;
static int s_locMaterialIntensity = -1;
static int s_locMaterialBodyOpacity = -1;

// Camera compensation
static float g_cam_yaw = 0.0f;
static float g_cam_stretch = 1.0f;

// Cache lượng giác của camera để không phải tính lại ở mỗi decal
static float g_cos_cam_yaw = 1.0f;
static float g_sin_cam_yaw = 0.0f;
static Camera3D s_camera;
static bool s_hasCamera = false;

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

/* BLEND_MULTIPLIED maps to the BODY form, which is what this shader already
 * produced for it. Worth knowing, but NOT changed here: raylib's multiplied
 * blend is (DST_COLOR, ZERO), so it ignores alpha outright — a multiplied decal
 * therefore does not fade at its edges or over its lifetime, however its alpha
 * is computed. No caller creates one today (the draw group exists but nothing
 * passes BLEND_MULTIPLIED to DecalSystem_Add*), so this is recorded rather than
 * fixed; fixing it means a real multiply resolver, mix(vec3(1), tint, coverage),
 * and that is a visible change, not a migration. */
static VFXSurfaceMode Decal_SurfaceFor(BlendMode blendMode)
{
    switch (blendMode)
    {
    case BLEND_ADDITIVE: return VFX_SURFACE_ADDITIVE;
    case BLEND_ALPHA:
    case BLEND_MULTIPLIED:
    default:             return VFX_SURFACE_ALPHA;
    }
}

static DecalFlowVariant *Decal_FlowVariant(BlendMode blendMode)
{
    VFXSurfaceMode surface = Decal_SurfaceFor(blendMode);
    DecalFlowVariant *v = &s_flowVariants[(int)surface];
    if (!v->loaded)
    {
        v->shader = ResourceManager_LoadShaderVariant(
            "core/decals/shaders/decal_material.vs",
            "core/decals/shaders/decal_flow.fs",
            VFXRender_OutputDefines(surface));
        v->locTime = GetShaderLocation(v->shader, "u_time");
        v->loaded = true;
    }
    return v;
}

static void Decal_BeginWorldPass(BlendMode blendMode)
{
    rlDrawRenderBatchActive();
    BeginBlendMode(blendMode);
    rlDrawRenderBatchActive();
    rlEnableDepthTest();
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlDrawRenderBatchActive();
}

static void Decal_EndWorldPass(void)
{
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    rlDrawRenderBatchActive();
    rlEnableDepthTest();
    rlDrawRenderBatchActive();
    EndBlendMode();
    rlDrawRenderBatchActive();
}

static int Decal_CompareMaterialBucket(const DecalEntity *a, const DecalEntity *b)
{
    const unsigned char aValues[] = {
        a->material.baseTint.r, a->material.baseTint.g, a->material.baseTint.b,
        a->material.emissiveTint.r, a->material.emissiveTint.g, a->material.emissiveTint.b,
        a->tint.r, a->tint.g, a->tint.b
    };
    const unsigned char bValues[] = {
        b->material.baseTint.r, b->material.baseTint.g, b->material.baseTint.b,
        b->material.emissiveTint.r, b->material.emissiveTint.g, b->material.emissiveTint.b,
        b->tint.r, b->tint.g, b->tint.b
    };
    for (int i = 0; i < 9; ++i)
    {
        if (aValues[i] != bValues[i])
            return aValues[i] < bValues[i] ? -1 : 1;
    }
    if (a->material.emissiveThreshold != b->material.emissiveThreshold)
        return a->material.emissiveThreshold < b->material.emissiveThreshold ? -1 : 1;
    if (a->material.emissiveIntensity != b->material.emissiveIntensity)
        return a->material.emissiveIntensity < b->material.emissiveIntensity ? -1 : 1;
    if (a->material.bodyOpacity != b->material.bodyOpacity)
        return a->material.bodyOpacity < b->material.bodyOpacity ? -1 : 1;
    return 0;
}

static bool Decal_RenderBefore(int lhs, int rhs)
{
    const DecalEntity *a = &g_DecalPool[lhs];
    const DecalEntity *b = &g_DecalPool[rhs];
    if (a->conformalStamp != b->conformalStamp) return !a->conformalStamp;
    if (a->blendMode != b->blendMode) return a->blendMode < b->blendMode;
    if (a->flowScroll != b->flowScroll) return !a->flowScroll;
    int materialOrder = Decal_CompareMaterialBucket(a, b);
    if (materialOrder != 0) return materialOrder < 0;
    if (a->texture.id != b->texture.id) return a->texture.id < b->texture.id;
    return lhs < rhs;
}

static float Decal_BoundsRadius(const DecalEntity *d)
{
    float maximumScale = fmaxf(d->scale, fmaxf(d->scaleStart, d->scaleEnd));
    // The base primitive is a square and conformal stamps perturb their rim.
    // This deliberately overestimates the sphere so CPU admission never clips
    // a visible texel at a frustum boundary.
    return fmaxf(maximumScale * 1.6f, 0.01f);
}

static int Decal_ClampPriority(int priority)
{
    if (priority < 0) return 0;
    if (priority > 255) return 255;
    return priority;
}

static bool Decal_IsWithinDrawDistance(const DecalEntity *d)
{
    if (!s_hasCamera || d->material.maxDrawDistance <= 0.0f)
        return true;
    return Vector3Distance(d->position, s_camera.position) - Decal_BoundsRadius(d) <=
           d->material.maxDrawDistance;
}

static bool Decal_IsVisible(const DecalEntity *d)
{
    if (!s_hasCamera)
        return true;

    Vector3 forward = Vector3Subtract(s_camera.target, s_camera.position);
    float forwardLength = Vector3Length(forward);
    if (forwardLength <= 0.0001f)
        return true;
    forward = Vector3Scale(forward, 1.0f / forwardLength);

    Vector3 up = Vector3Normalize(s_camera.up);
    Vector3 right = Vector3CrossProduct(forward, up);
    float rightLength = Vector3Length(right);
    if (rightLength <= 0.0001f)
        return true;
    right = Vector3Scale(right, 1.0f / rightLength);
    up = Vector3Normalize(Vector3CrossProduct(right, forward));

    Vector3 toDecal = Vector3Subtract(d->position, s_camera.position);
    float depth = Vector3DotProduct(toDecal, forward);
    float radius = Decal_BoundsRadius(d);
    if (depth + radius <= 0.0f)
        return false;

    float vertical = fabsf(Vector3DotProduct(toDecal, up));
    float horizontal = fabsf(Vector3DotProduct(toDecal, right));
    float aspect = (float)GetScreenWidth() / fmaxf((float)GetScreenHeight(), 1.0f);
    if (s_camera.projection == CAMERA_ORTHOGRAPHIC)
    {
        float halfHeight = fmaxf(s_camera.fovy * 0.5f, 0.01f);
        return vertical <= halfHeight + radius &&
               horizontal <= halfHeight * aspect + radius;
    }

    float halfVerticalFov = fmaxf(s_camera.fovy * DEG2RAD * 0.5f, 0.001f);
    float halfHeightAtDepth = depth * tanf(halfVerticalFov);
    return vertical <= halfHeightAtDepth + radius &&
           horizontal <= halfHeightAtDepth * aspect + radius;
}

static int Decal_SelectLod(const DecalEntity *d)
{
    if (!s_hasCamera)
        return 0;
    Vector3 forward = Vector3Subtract(s_camera.target, s_camera.position);
    float forwardLength = Vector3Length(forward);
    if (forwardLength <= 0.0001f)
        return 0;
    forward = Vector3Scale(forward, 1.0f / forwardLength);
    float depth = Vector3DotProduct(Vector3Subtract(d->position, s_camera.position), forward);
    if (depth <= 0.0001f)
        return 0;

    float radius = Decal_BoundsRadius(d);
    float pixelsPerWorld;
    if (s_camera.projection == CAMERA_ORTHOGRAPHIC)
        pixelsPerWorld = (float)GetScreenHeight() / fmaxf(s_camera.fovy, 0.01f);
    else
        pixelsPerWorld = ((float)GetScreenHeight() * 0.5f) /
                         (depth * tanf(fmaxf(s_camera.fovy * DEG2RAD * 0.5f, 0.001f)));
    float diameterPixels = radius * 2.0f * pixelsPerWorld;
    if (diameterPixels < 12.0f) return 3;
    if (diameterPixels < 40.0f) return 2;
    if (diameterPixels < 96.0f) return 1;
    return 0;
}

static float Decal_EstimatedCoverage(const DecalEntity *d)
{
    if (!s_hasCamera) return 0.0f;
    Vector3 forward = Vector3Subtract(s_camera.target, s_camera.position);
    float length = Vector3Length(forward);
    if (length <= 0.0001f) return 0.0f;
    forward = Vector3Scale(forward, 1.0f / length);
    float depth = Vector3DotProduct(Vector3Subtract(d->position, s_camera.position), forward);
    if (depth <= 0.0001f) return 0.0f;
    float pixelsPerWorld = s_camera.projection == CAMERA_ORTHOGRAPHIC ?
        (float)GetScreenHeight() / fmaxf(s_camera.fovy, 0.01f) :
        ((float)GetScreenHeight() * 0.5f) /
        (depth * tanf(fmaxf(s_camera.fovy * DEG2RAD * 0.5f, 0.001f)));
    float radiusPixels = Decal_BoundsRadius(d) * pixelsPerWorld;
    float screenPixels = fmaxf((float)GetScreenWidth() * (float)GetScreenHeight(), 1.0f);
    return fminf(PI * radiusPixels * radiusPixels / screenPixels, 1.0f);
}

static void Decal_BuildRenderQueue(void)
{
    s_renderCount = 0;
    s_renderCulledCount = 0;
    s_renderFrustumCulledCount = 0;
    s_renderLodCulledCount = 0;
    s_renderEmissiveSuppressedCount = 0;
    s_renderDistanceCulledCount = 0;
    s_renderBaseCoverage = 0.0f;
    s_renderEmissiveCoverage = 0.0f;
    s_renderLegacySubmissions = 0;
    s_renderConformalSubmissions = 0;
    s_renderMaterialSwitches = 0;
    s_renderTextureSwitches = 0;
    for (int a = 0; a < s_activeCount; ++a)
    {
        int idx = s_activeIds[a];
        DecalEntity *d = &g_DecalPool[idx];
        if (d->texture.id == 0)
            continue;
        if (!Decal_IsVisible(d))
        {
            ++s_renderCulledCount;
            ++s_renderFrustumCulledCount;
            continue;
        }
        if (!Decal_IsWithinDrawDistance(d))
        {
            ++s_renderCulledCount;
            ++s_renderDistanceCulledCount;
            continue;
        }
        int lod = Decal_SelectLod(d);
        if (lod >= 3)
        {
            ++s_renderCulledCount;
            ++s_renderLodCulledCount;
            continue;
        }
        s_renderLod[idx] = (unsigned char)lod;
        s_renderEmissiveAllowed[idx] = d->material.emissiveIntensity > 0.0f &&
                                       d->material.emissiveTint.a > 0;
        s_renderIds[s_renderCount++] = idx;
    }
    // Admit higher-priority candidates first under the mobile coverage budget.
    for (int i = 1; i < s_renderCount; ++i)
    {
        int value = s_renderIds[i];
        int j = i - 1;
        while (j >= 0 && g_DecalPool[value].material.priority > g_DecalPool[s_renderIds[j]].material.priority)
        {
            s_renderIds[j + 1] = s_renderIds[j];
            --j;
        }
        s_renderIds[j + 1] = value;
    }
    if (s_hasCamera)
    {
        int admitted = 0;
        for (int i = 0; i < s_renderCount; ++i)
        {
            int idx = s_renderIds[i];
            float coverage = Decal_EstimatedCoverage(&g_DecalPool[idx]);
            s_renderBaseCoverage += coverage;
            if (s_renderEmissiveAllowed[idx] &&
                s_renderEmissiveCoverage + coverage <= 0.08f)
                s_renderEmissiveCoverage += coverage;
            else
            {
                s_renderEmissiveAllowed[idx] = false;
                ++s_renderEmissiveSuppressedCount;
            }
            s_renderIds[admitted++] = idx;
        }
        s_renderCount = admitted;
    }
    // Small fixed pool: insertion sort avoids allocations and has lower
    // overhead than a general-purpose sort at the usual active counts.
    for (int i = 1; i < s_renderCount; ++i)
    {
        int value = s_renderIds[i];
        int j = i - 1;
        while (j >= 0 && Decal_RenderBefore(value, s_renderIds[j]))
        {
            s_renderIds[j + 1] = s_renderIds[j];
            --j;
        }
        s_renderIds[j + 1] = value;
    }
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
static void Decal_AppendQuad(const DecalEntity *d, Color c)
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

    // Legacy quad normals are otherwise unused. Pack flow speed/strength/glow
    // there so the flow shader does not need per-decal uniform updates.
    rlNormal3f(d->flowScroll ? d->flowSpeed : 0.0f,
               d->flowScroll ? d->flowStrength : 0.0f,
               d->flowScroll ? d->glowIntensity : 0.0f);

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
    s_camera = camera;
    s_hasCamera = true;
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
    s_renderCount = 0;
    s_renderCulledCount = 0;
    s_renderFrustumCulledCount = 0;
    s_renderLodCulledCount = 0;
    s_renderEmissiveSuppressedCount = 0;
    s_renderDistanceCulledCount = 0;
    s_renderBaseCoverage = 0.0f;
    s_renderEmissiveCoverage = 0.0f;
    s_hasCamera = false;
    for (int i = 0; i < MAX_DECALS; i++)
    {
        g_DecalPool[i].active = false;
        s_slotListIndex[i] = -1;
    }
    // Uber-shader cho cả decal tĩnh lẫn decal flow: decal_flow.fs với mọi
    // uniform = 0 rút gọn ĐÚNG về decal.fs (cùng edge mask radial, texture
    // đứng yên) — không cần file decal_uber.fs riêng (chưa từng tồn tại;
    // trước đây load hụt → rơi về default shader, mất edge fade + flow).
    /* Warm the two blends decals are actually drawn under. Lazy-loading works
     * (Decal_FlowVariant handles it), but the first additive decal would then
     * compile a program mid-frame. */
    Decal_FlowVariant(BLEND_ALPHA);
    Decal_FlowVariant(BLEND_ADDITIVE);
    g_MaterialDecalShader = ResourceManager_LoadShader("core/decals/shaders/decal_material.vs",
                                                        "core/decals/shaders/decal_material.fs");
    s_locMaterialEmissivePass = GetShaderLocation(g_MaterialDecalShader, "u_emissivePass");
    s_locMaterialBaseTint = GetShaderLocation(g_MaterialDecalShader, "u_baseTint");
    s_locMaterialEmissiveTint = GetShaderLocation(g_MaterialDecalShader, "u_emissiveTint");
    s_locMaterialThreshold = GetShaderLocation(g_MaterialDecalShader, "u_emissiveThreshold");
    s_locMaterialIntensity = GetShaderLocation(g_MaterialDecalShader, "u_emissiveIntensity");
    s_locMaterialBodyOpacity = GetShaderLocation(g_MaterialDecalShader, "u_bodyOpacity");
}

static int FindSlot(int incomingPriority)
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
    int target = -1;
    bool targetCulled = false;
    for (int a = 0; a < s_activeCount; ++a)
    {
        int id = s_activeIds[a];
        DecalEntity *candidate = &g_DecalPool[id];
        bool culled = !Decal_IsVisible(candidate) || !Decal_IsWithinDrawDistance(candidate);
        if (target < 0 || (culled && !targetCulled) ||
            (culled == targetCulled && candidate->material.priority < g_DecalPool[target].material.priority) ||
            (culled == targetCulled && candidate->material.priority == g_DecalPool[target].material.priority &&
             candidate->lifetime < g_DecalPool[target].lifetime))
        {
            target = id;
            targetCulled = culled;
        }
    }
    if (target < 0 || incomingPriority < g_DecalPool[target].material.priority)
        return -1;
    Decal_Deactivate(target);
    return target;
}

static int SpawnDecalCommon(Vector3 pos, float rotation, float rotSpeed,
                            float scaleStart, float scaleEnd,
                            Texture2D texture, float lifetime,
                            Color tint, BlendMode blendMode, float yOffset, int priority)
{
    int idx = FindSlot(Decal_ClampPriority(priority));
    if (idx < 0)
        return -1;
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
                                         .emissiveThreshold = 1.1f, .emissiveIntensity = 0.0f,
                                         .priority = Decal_ClampPriority(priority), .maxDrawDistance = 0.0f,
                                         .contrastProfile = VFX_CONTRAST_NONE, .bodyOpacity = 1.0f,
                                         .appearance = VFX_APPEARANCE_INHERIT };
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
    int evictionCandidate = -1;
    int lowestPriority = 256;
    float shortestLife = 0.0f;
    for (int a = 0; a < s_activeCount; ++a)
    {
        int id = s_activeIds[a];
        DecalEntity *candidate = &g_DecalPool[id];
        if (!candidate->conformalStamp) continue;
        conformalCount++;
        if (evictionCandidate < 0 || candidate->material.priority < lowestPriority ||
            (candidate->material.priority == lowestPriority && candidate->lifetime < shortestLife))
        {
            evictionCandidate = id;
            lowestPriority = candidate->material.priority;
            shortestLife = candidate->lifetime;
        }
    }
    // Material stamps are considerably denser than legacy quads. Keep a hard
    // independent budget so repeated impact events cannot consume a frame.
    if (conformalCount >= 12 && evictionCandidate >= 0)
    {
        int incomingPriority = material != NULL ? Decal_ClampPriority(material->priority) : 128;
        if (incomingPriority < lowestPriority)
            return DECAL_HANDLE_INVALID;
        Decal_Deactivate(evictionCandidate);
    }
    int incomingPriority = material != NULL ? Decal_ClampPriority(material->priority) : 128;
    int idx = SpawnDecalCommon(pos, rotation, rotSpeed, scaleStart, scaleEnd, texture,
                               lifetime, tint, blendMode, yOffset, incomingPriority);
    if (idx < 0)
        return DECAL_HANDLE_INVALID;
    DecalEntity *d = &g_DecalPool[idx];
    d->conformalStamp = true;
    if (material != NULL)
    {
        d->material = *material;
        d->material.priority = Decal_ClampPriority(d->material.priority);
        VFXResolvedAppearance appearance = VFXAppearance_Resolve(
            d->material.appearance,
            (VFXResolvedAppearance){
                .surface = VFX_SURFACE_ALPHA,
                .contrast = d->material.contrastProfile,
                .bodyOpacity = d->material.bodyOpacity > 0.0f
                                   ? d->material.bodyOpacity : 1.0f,
                .emissionIntensity = d->material.emissiveIntensity,
                .emissionThreshold = d->material.emissiveThreshold,
                .unlit = false
            });
        // The decal system drives blend from its PASS structure (body groups draw
        // BLEND_ALPHA/BLEND_MULTIPLIED, the emissive group BLEND_ADDITIVE), which
        // keeps it self-consistent with decal_material.fs's two branches. It
        // therefore cannot honour `appearance.surface`, and silently discarding a
        // resolved field is how a caller comes to believe it asked for something.
        if (appearance.surface != VFX_SURFACE_ALPHA)
        {
            static bool warned = false;
            if (!warned)
            {
                warned = true;
                TraceLog(LOG_WARNING,
                         "DECAL: appearance requests surface %d, but decals blend by pass "
                         "(body ALPHA/MULTIPLIED, emissive ADDITIVE) and cannot honour it",
                         (int)appearance.surface);
            }
        }
        d->material.contrastProfile = appearance.contrast;
        d->material.bodyOpacity = appearance.bodyOpacity;
        d->material.emissiveIntensity = appearance.emissionIntensity;
        d->material.emissiveThreshold = appearance.emissionThreshold;
        const VFXContrastProfile *profile = VFXContrast_Get(
            d->material.contrastProfile);
        d->material.baseTint = VFXContrast_ApplyColor(
            d->material.baseTint, d->material.contrastProfile,
            VFX_CONTRAST_BODY);
        d->material.emissiveTint = VFXContrast_ApplyColor(
            d->material.emissiveTint, d->material.contrastProfile,
            VFX_CONTRAST_EMISSION);
        d->material.emissiveThreshold = VFXContrast_ApplyEmissionThreshold(
            d->material.emissiveThreshold, d->material.contrastProfile);
        d->material.emissiveIntensity = VFXContrast_ApplyEmissionIntensity(
            d->material.emissiveIntensity, d->material.contrastProfile);
        d->tint.a = VFXContrast_ScaleAlpha(d->tint.a, profile->alpha);
    }
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
                     lifetime, tint, blendMode, yOffset, 128);
}

void DecalSystem_AddFlowEx(Vector3 pos, float rotation, float rotSpeed,
                           float scaleStart, float scaleEnd,
                           Texture2D texture, float lifetime,
                           Color tint, BlendMode blendMode, float yOffset,
                           float flowSpeed, float flowStrength,
                           float glowIntensity)
{
    int idx = SpawnDecalCommon(pos, rotation, rotSpeed, scaleStart, scaleEnd,
                               texture, lifetime, tint, blendMode, yOffset, 128);
    if (idx < 0) return;
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
                               lifetime, tint, blendMode, 0.0f, 128);
    if (idx < 0) return;
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
    for (int a = 0; a < s_renderCount; a++)
    {
        int idx = s_renderIds[a];
        DecalEntity *d = &g_DecalPool[idx];

        if (d->conformalStamp || d->blendMode != mode || d->flowScroll != flowOnly)
            continue;

        // Trì hoãn việc kích hoạt Shader và BlendMode cho tới khi thực sự tìm thấy decal hợp lệ đầu tiên
        if (!shaderActive)
        {
            Decal_BeginWorldPass(mode);
            DecalFlowVariant *variant = Decal_FlowVariant(mode);
            BeginShaderMode(variant->shader);
            float frameTime = (float)GetTime();
            SetShaderValue(variant->shader, variant->locTime, &frameTime, SHADER_UNIFORM_FLOAT);
            shaderActive = true;
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
            ++s_renderTextureSwitches;
        }

        if (!drawing)
        {
            rlBegin(RL_QUADS);
            drawing = true;
            ++s_renderLegacySubmissions;
        }

        float alphaRatio = d->lifetime / d->maxLifetime;
        Color c = d->tint;
        c.a = (unsigned char)(d->tint.a * alphaRatio);

        Decal_AppendQuad(d, c);
    }

    // Dọn dẹp trạng thái sau khi kết thúc loop của nhóm nếu có vẽ
    if (drawing)
        rlEnd();

    if (shaderActive)
    {
        rlSetTexture(0);
        EndShaderMode();
        rlDrawRenderBatchActive();
        Decal_EndWorldPass();
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

static bool Decal_HasEmissive(const DecalEntity *d, int lod, int idx)
{
    return lod < 2 && d->material.emissiveIntensity > 0.0f &&
           d->material.emissiveTint.a > 0 && s_renderEmissiveAllowed[idx];
}

static bool Decal_SameMaterialBucket(const DecalEntity *a, const DecalEntity *b)
{
    return Decal_CompareMaterialBucket(a, b) == 0;
}

static void Decal_ApplyMaterialBucket(const DecalEntity *d)
{
    float baseTint[4] = {
        d->material.baseTint.r * d->tint.r / (255.0f * 255.0f),
        d->material.baseTint.g * d->tint.g / (255.0f * 255.0f),
        d->material.baseTint.b * d->tint.b / (255.0f * 255.0f), 1.0f
    };
    float emissiveTint[4] = {
        d->material.emissiveTint.r / 255.0f, d->material.emissiveTint.g / 255.0f,
        d->material.emissiveTint.b / 255.0f, 1.0f
    };
    SetShaderValue(g_MaterialDecalShader, s_locMaterialBaseTint, baseTint, SHADER_UNIFORM_VEC4);
    SetShaderValue(g_MaterialDecalShader, s_locMaterialEmissiveTint, emissiveTint, SHADER_UNIFORM_VEC4);
    SetShaderValue(g_MaterialDecalShader, s_locMaterialThreshold, &d->material.emissiveThreshold, SHADER_UNIFORM_FLOAT);
    SetShaderValue(g_MaterialDecalShader, s_locMaterialIntensity, &d->material.emissiveIntensity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(g_MaterialDecalShader, s_locMaterialBodyOpacity, &d->material.bodyOpacity, SHADER_UNIFORM_FLOAT);
}

static void Decal_StampEmitVertex(const DecalEntity *d, float radial, float angle,
                                  int ring, int sector);

static void Decal_DrawConformalMesh(const DecalEntity *d, int lod)
{
    static const int ringStarts[3][3] = {{0, 1, 2}, {0, 2, 0}, {0, 0, 0}};
    static const int ringEnds[3][3] = {{1, 2, 3}, {2, 3, 0}, {3, 0, 0}};
    static const int ringCounts[3] = {3, 2, 1};
    int sectorStep = lod == 0 ? 1 : 2;
    for (int ring = 0; ring < ringCounts[lod]; ++ring)
    {
        int r0Index = ringStarts[lod][ring];
        int r1Index = ringEnds[lod][ring];
        float r0 = (float)r0Index / DECAL_STAMP_RINGS;
        float r1 = (float)r1Index / DECAL_STAMP_RINGS;
        for (int sector = 0; sector < DECAL_STAMP_SECTORS; sector += sectorStep)
        {
            int nextSector = sector + sectorStep;
            float a0 = 2.0f * PI * sector / DECAL_STAMP_SECTORS;
            float a1 = 2.0f * PI * nextSector / DECAL_STAMP_SECTORS;
            Decal_StampEmitVertex(d, r0, a0, r0Index, sector);
            Decal_StampEmitVertex(d, r1, a0, r1Index, sector);
            Decal_StampEmitVertex(d, r1, a1, r1Index, nextSector);
            Decal_StampEmitVertex(d, r0, a0, r0Index, sector);
            Decal_StampEmitVertex(d, r1, a1, r1Index, nextSector);
            Decal_StampEmitVertex(d, r0, a1, r0Index, nextSector);
        }
    }
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
    for (int a = 0; a < s_renderCount; ++a)
    {
        DecalEntity *d = &g_DecalPool[s_renderIds[a]];
        if (d->conformalStamp && d->blendMode == sourceMode &&
            d->texture.id != 0 && (!emissivePass || Decal_HasEmissive(d, s_renderLod[s_renderIds[a]], s_renderIds[a])))
        {
            active = true;
            break;
        }
    }
    if (!active) return;
    Decal_BeginWorldPass(renderMode);
    // Depth testing must remain enabled: a ground decal may sit fractionally
    // above terrain, but it must never paint through characters or props.
    rlDisableBackfaceCulling();
    rlDrawRenderBatchActive();
    BeginShaderMode(g_MaterialDecalShader);
    int useEmissive = emissivePass ? 1 : 0;
    SetShaderValue(g_MaterialDecalShader, s_locMaterialEmissivePass, &useEmissive,
                   SHADER_UNIFORM_INT);
    const DecalEntity *bucketMaterial = NULL;
    unsigned int bucketTextureId = 0;
    bool drawing = false;

    for (int a = 0; a < s_renderCount; ++a)
    {
        DecalEntity *d = &g_DecalPool[s_renderIds[a]];
        if (!d->conformalStamp || d->blendMode != sourceMode || d->texture.id == 0 ||
            (emissivePass && !Decal_HasEmissive(d, s_renderLod[s_renderIds[a]], s_renderIds[a])))
            continue;
        float erosion = 1.0f - d->lifetime / d->maxLifetime;
        Color c = d->tint;
        c.a = (unsigned char)(c.a * (1.0f - erosion * erosion) *
                              Decal_ConformalFadeAlpha(d));
        bool materialChanged = bucketMaterial == NULL || !Decal_SameMaterialBucket(bucketMaterial, d);
        bool textureChanged = bucketTextureId != d->texture.id;
        if (materialChanged || textureChanged)
        {
            if (drawing)
            {
                rlEnd();
                drawing = false;
            }
            rlDrawRenderBatchActive();
        }
        if (materialChanged)
        {
            Decal_ApplyMaterialBucket(d);
            bucketMaterial = d;
            ++s_renderMaterialSwitches;
        }
        if (textureChanged)
        {
            rlSetTexture(d->texture.id);
            bucketTextureId = d->texture.id;
            ++s_renderTextureSwitches;
        }
        if (!drawing)
        {
            rlBegin(RL_TRIANGLES);
            drawing = true;
            ++s_renderConformalSubmissions;
        }
        rlColor4ub((unsigned char)(erosion * 255.0f), 255, 255, c.a);
        Decal_DrawConformalMesh(d, s_renderLod[s_renderIds[a]]);
    }

    if (drawing)
        rlEnd();
    rlSetTexture(0);
    rlDrawRenderBatchActive();
    EndShaderMode();
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlDrawRenderBatchActive();
    Decal_EndWorldPass();
}

void DecalSystem_DrawBody(void)
{
    Decal_BuildRenderQueue();
    // Material/pigment only. Additive groups are deliberately excluded: the
    // VFXBody composite assumes ordinary alpha data and unpremultiplies it.
    DrawGroup(BLEND_ALPHA, false);
    DrawGroup(BLEND_ALPHA, true);
    DrawGroup(BLEND_MULTIPLIED, false);
    DrawGroup(BLEND_MULTIPLIED, true);
    DrawConformalGroup(BLEND_ALPHA, BLEND_ALPHA, false);
}

bool DecalSystem_HasEmission(void)
{
    for (int a = 0; a < s_renderCount; ++a)
    {
        int idx = s_renderIds[a];
        const DecalEntity *d = &g_DecalPool[idx];
        if (d->blendMode == BLEND_ADDITIVE)
            return true;
        if (d->conformalStamp &&
            Decal_HasEmissive(d, s_renderLod[idx], idx))
            return true;
    }
    return false;
}

void DecalSystem_DrawEmission(void)
{
    DrawGroup(BLEND_ADDITIVE, false);
    DrawGroup(BLEND_ADDITIVE, true);
    DrawConformalGroup(BLEND_ADDITIVE, BLEND_ALPHA, true);
}

void DecalSystem_Draw(void)
{
    DecalSystem_DrawBody();
    DecalSystem_DrawEmission();
}

void DecalSystem_Unload(void)
{
    s_activeCount = 0;
    s_renderCount = 0;
    s_renderCulledCount = 0;
    s_renderFrustumCulledCount = 0;
    s_renderLodCulledCount = 0;
    s_renderEmissiveSuppressedCount = 0;
    s_renderDistanceCulledCount = 0;
    s_renderBaseCoverage = 0.0f;
    s_renderEmissiveCoverage = 0.0f;
    s_renderLegacySubmissions = 0;
    s_renderConformalSubmissions = 0;
    s_renderMaterialSwitches = 0;
    s_renderTextureSwitches = 0;
    s_hasCamera = false;
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

void DecalSystem_GetRenderStats(DecalRenderStats *outStats)
{
    if (outStats == NULL)
        return;
    outStats->active = s_activeCount;
    outStats->visible = s_renderCount;
    outStats->culled = s_renderCulledCount;
    outStats->frustumCulled = s_renderFrustumCulledCount;
    outStats->lodCulled = s_renderLodCulledCount;
    outStats->emissiveSuppressed = s_renderEmissiveSuppressedCount;
    outStats->distanceCulled = s_renderDistanceCulledCount;
    outStats->baseCoverage = s_renderBaseCoverage;
    outStats->emissiveCoverage = s_renderEmissiveCoverage;
    outStats->legacySubmissions = s_renderLegacySubmissions;
    outStats->conformalSubmissions = s_renderConformalSubmissions;
    outStats->materialSwitches = s_renderMaterialSwitches;
    outStats->textureSwitches = s_renderTextureSwitches;
}
