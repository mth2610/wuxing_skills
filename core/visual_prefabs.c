#include "visual_prefabs.h"
#include "core/procedural_mesh_utils.h"
#include "core/particle_system.h"
#include "core/particle_radial_burst.h"
#include "core/decal_system.h"
#include "core/vfx_light.h"
#include "core/skill_helper.h"
#include "core/trail_system.h"
#include "core/camera_fx.h"
#include "raymath.h"
#include "rlgl.h"
#include "core/resource_manager.h"
#include <math.h>

#define CACHE_SIZE 16

// ============================================================================
// CACHE STRUCTURES
// ============================================================================

typedef struct {
    int seed;
    float jaggedness;
    RockMeshData data;
    bool active;
} RockCache;
static RockCache s_rockCache[CACHE_SIZE];

typedef struct {
    int seed;
    float sharpness;
    ShardClusterMeshData data;
    bool active;
} IceCache;
static IceCache s_iceCache[CACHE_SIZE];

// ============================================================================
// HELPER CACHE FUNCTIONS
// ============================================================================

static RockMeshData* GetCachedRock(int seed, float jaggedness) {
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (s_rockCache[i].active && s_rockCache[i].seed == seed && fabsf(s_rockCache[i].jaggedness - jaggedness) < 0.01f) {
            return &s_rockCache[i].data;
        }
    }
    int slot = 0;
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (!s_rockCache[i].active) { slot = i; break; }
    }
    s_rockCache[slot].active = true;
    s_rockCache[slot].seed = seed;
    s_rockCache[slot].jaggedness = jaggedness;
    ProceduralMesh_BuildRock(&s_rockCache[slot].data, Vector3Zero(), 1.0f, jaggedness, seed, 2);
    return &s_rockCache[slot].data;
}

static ShardClusterMeshData* GetCachedIce(int seed, float sharpness) {
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (s_iceCache[i].active && s_iceCache[i].seed == seed && fabsf(s_iceCache[i].sharpness - sharpness) < 0.01f) {
            return &s_iceCache[i].data;
        }
    }
    int slot = 0;
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (!s_iceCache[i].active) { slot = i; break; }
    }
    s_iceCache[slot].active = true;
    s_iceCache[slot].seed = seed;
    s_iceCache[slot].sharpness = sharpness;
    
    ShardClusterConfig cfg = ProceduralMesh_DefaultShardClusterConfig();
    cfg.thicknessMin = 0.4f;
    cfg.thicknessMax = 0.8f;
    cfg.tipSharpness = sharpness;
    cfg.sides = 6;
    ProceduralMesh_BuildShardCluster(&s_iceCache[slot].data, Vector3Zero(), (Vector3){0,1,0}, 4, 0.7f, 1.2f, seed, &cfg);
    return &s_iceCache[slot].data;
}

// ============================================================================
// GROUP 1: MESH & HÌNH KHỐI TĨNH (Draw)
// ============================================================================

static void DrawOrganicStonePillar(Vector3 pillarPos, float currentHeight, float baseRad, float topRad) {
    #define HEIGHT_SEGS 8
    #define RADIAL_SEGS 8
    
    Vector3 rings[HEIGHT_SEGS + 1][RADIAL_SEGS];
    Vector3 normals[HEIGHT_SEGS + 1][RADIAL_SEGS];

    // Cột cong nhẹ vào trong
    float tiltAngle = 8.0f * DEG2RAD;
    float dirInX = -0.5f;
    float dirInZ = -0.5f;

    for (int h = 0; h <= HEIGHT_SEGS; h++) {
        float hRatio = (float)h / HEIGHT_SEGS;
        float rad = Lerp(baseRad, topRad, hRatio);

        float shiftDist = hRatio * currentHeight * sinf(tiltAngle);
        Vector3 centerOffset = { dirInX * shiftDist, 0.0f, dirInZ * shiftDist };

        for (int r = 0; r < RADIAL_SEGS; r++) {
            float angle = (float)r / RADIAL_SEGS * 2.0f * PI;
            float noiseWave = 1.0f + 0.15f * sinf(hRatio * 8.0f + angle * 3.0f);
            float perturbedRad = rad * noiseWave;

            Vector3 localPos = {
                perturbedRad * cosf(angle),
                hRatio * currentHeight,
                perturbedRad * sinf(angle)
            };

            Vector3 localNormal = { cosf(angle), 0.1f, sinf(angle) };
            localNormal = Vector3Normalize(localNormal);

            rings[h][r] = Vector3Add(Vector3Add(pillarPos, localPos), centerOffset);
            normals[h][r] = localNormal;
        }
    }

    rlPushMatrix();
    rlColor4ub(255, 255, 255, 255);
    rlCheckRenderBatchLimit(HEIGHT_SEGS * RADIAL_SEGS * 4);
    rlBegin(RL_QUADS);
    for (int h = 0; h < HEIGHT_SEGS; h++) {
        float v1 = (float)h / HEIGHT_SEGS;
        float v2 = (float)(h + 1) / HEIGHT_SEGS;
        for (int r = 0; r < RADIAL_SEGS; r++) {
            int nextR = (r + 1) % RADIAL_SEGS;
            float u1 = (float)r / RADIAL_SEGS;
            float u2 = (float)(r + 1) / RADIAL_SEGS;

            rlNormal3f(normals[h][nextR].x, normals[h][nextR].y, normals[h][nextR].z);
            rlTexCoord2f(u2, v1);
            rlVertex3f(rings[h][nextR].x, rings[h][nextR].y, rings[h][nextR].z);

            rlNormal3f(normals[h][r].x, normals[h][r].y, normals[h][r].z);
            rlTexCoord2f(u1, v1);
            rlVertex3f(rings[h][r].x, rings[h][r].y, rings[h][r].z);

            rlNormal3f(normals[h + 1][r].x, normals[h + 1][r].y, normals[h + 1][r].z);
            rlTexCoord2f(u1, v2);
            rlVertex3f(rings[h + 1][r].x, rings[h + 1][r].y, rings[h + 1][r].z);

            rlNormal3f(normals[h + 1][nextR].x, normals[h + 1][nextR].y, normals[h + 1][nextR].z);
            rlTexCoord2f(u2, v2);
            rlVertex3f(rings[h + 1][nextR].x, rings[h + 1][nextR].y, rings[h + 1][nextR].z);
        }
    }
    rlEnd();

    rlCheckRenderBatchLimit(RADIAL_SEGS * 3);
    rlBegin(RL_TRIANGLES);
    float finalShift = currentHeight * sinf(tiltAngle);
    Vector3 peak = { pillarPos.x + dirInX * finalShift, pillarPos.y + currentHeight, pillarPos.z + dirInZ * finalShift };
    for (int r = 0; r < RADIAL_SEGS; r++) {
        int nextR = (r + 1) % RADIAL_SEGS;
        rlNormal3f(0.0f, 1.0f, 0.0f);
        rlTexCoord2f((float)r/RADIAL_SEGS, 1.0f); rlVertex3f(rings[HEIGHT_SEGS][r].x, rings[HEIGHT_SEGS][r].y, rings[HEIGHT_SEGS][r].z);
        rlTexCoord2f((float)nextR/RADIAL_SEGS, 1.0f); rlVertex3f(rings[HEIGHT_SEGS][nextR].x, rings[HEIGHT_SEGS][nextR].y, rings[HEIGHT_SEGS][nextR].z);
        rlTexCoord2f(0.5f, 1.0f); rlVertex3f(peak.x, peak.y, peak.z);
    }
    rlEnd();

    rlPopMatrix();
    #undef HEIGHT_SEGS
    #undef RADIAL_SEGS
}

void Prefab_DrawRoundBoulder(Vector3 pos, float radius) {
    EffectMaterialParams params = {0};
    params.baseColor = (Color){150, 110, 80, 255}; // Đá đất sét lởm chởm
    params.rimStrength = 0.2f;
    params.fresnelPower = 3.0f;
    params.texture1 = ResourceManager_LoadTexture("assets/textures/tex_rock_albedo.png");
    
    EffectMaterial mat = Material_LoadCustom(params);
    
    Material_Begin(mat);
    DrawCoreSphere(pos, radius, 32, 32, WHITE);
    Material_End();
}

void Prefab_DrawStonePillar(Vector3 basePos, float radius, float height, float sharpness, float progress) {
    if (progress <= 0.0f) return;
    
    rlDisableBackfaceCulling();
    // Smoothstep để tạo cảm giác trồi lên nhanh ở giữa
    float rise = progress * progress * (3.0f - 2.0f * progress);
    
    // Bắt đầu từ sâu dưới đất (-height) và trồi lên đến mặt đất (0), thêm -0.2f để giấu phần gờ dưới
    float yOffset = -height * (1.0f - rise) - 0.2f;
    Vector3 actualPos = Vector3Add(basePos, (Vector3){0, yOffset, 0});
    
    float topRadius = radius * (1.0f - sharpness);
    
    EffectMaterialParams params = {0};
    params.baseColor = (Color){150, 110, 80, 255}; // Đá đất nung
    params.rimStrength = 0.3f;
    params.fresnelPower = 2.0f;
    params.distortionStrength = 0.0f; // Tắt wobble để không bị bay lơ lửng như cái mền
    params.translucency = 0.0f;
    params.texture1 = ResourceManager_LoadTexture("assets/textures/tex_rock_albedo.png");
    
    EffectMaterial mat = Material_LoadCustom(params);
    Material_Begin(mat);
    
    // Vẽ cột đá với chiều cao cố định, chỉ tịnh tiến vị trí Y để tạo hiệu ứng trồi lên
    DrawOrganicStonePillar(actualPos, height + 0.2f, radius, topRadius);
    
    Material_End();
}

void Prefab_DrawBoulder(Vector3 pos, float radius, float jaggedness, int seed) {
    RockMeshData* data = GetCachedRock(seed, jaggedness);
    
    EffectMaterialParams params = {0};
    params.baseColor = (Color){150, 110, 80, 255}; // Đá đất sét lởm chởm
    params.rimStrength = 0.2f;
    params.fresnelPower = 3.0f;
    params.texture1 = ResourceManager_LoadTexture("assets/textures/tex_rock_albedo.png");
    
    EffectMaterial mat = Material_LoadCustom(params);
    
    rlDrawRenderBatchActive(); // Flush trước khi đổi state culling
    rlDisableBackfaceCulling(); // Tắt backface culling để không bị rỗng ruột do winding order
    Material_Begin(mat);
    
    rlPushMatrix();
    rlTranslatef(pos.x, pos.y, pos.z);
    rlScalef(radius, radius, radius);
    ProceduralMesh_DrawRock(data, WHITE);
    rlPopMatrix();
    
    Material_End();
    
    rlDrawRenderBatchActive(); // Flush batch chứa Rock trước khi bật lại culling!
    rlEnableBackfaceCulling();
}

void Prefab_DrawIceCrystal(Vector3 basePos, float radius, float height, float sharpness, int seed) {
    ShardClusterMeshData* data = GetCachedIce(seed, sharpness);
    
    EffectMaterialParams params = {0};
    params.baseColor = (Color){170, 220, 255, 150}; // Màu băng (Alpha 150 để trong suốt)
    params.rimStrength = 1.5f;
    params.fresnelPower = 5.0f;
    params.emissiveIntensity = 0.5f;
    params.translucency = 0.6f;
    params.texture1 = ResourceManager_LoadTexture("assets/textures/tex_ice_crystal.png");
    
    EffectMaterial mat = Material_LoadCustom(params);
    BeginBlendMode(BLEND_ALPHA);
    rlDrawRenderBatchActive(); // Flush trước khi tắt depth mask
    rlDisableDepthMask(); // Tắt ghi depth để băng trong suốt không che nhau
    Material_Begin(mat);
    
    rlPushMatrix();
    rlTranslatef(basePos.x, basePos.y, basePos.z);
    // Mesh được build với height ~ 1.0, nên scale y theo height, x/z theo radius
    rlScalef(radius, height, radius);
    ProceduralMesh_DrawShardCluster(data, WHITE);
    rlPopMatrix();
    
    Material_End();
    rlDrawRenderBatchActive(); // Flush batch băng trước khi bật lại depth mask
    rlEnableDepthMask();
    EndBlendMode();
}

static void DrawOrganicPuddle(Vector3 pos, float radius) {
    int sides = 32;
    float time = GetTime();
    
    rlCheckRenderBatchLimit(sides * 3);
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < sides; i++) {
        float angle1 = (i / (float)sides) * 2.0f * PI;
        float angle2 = ((i + 1) / (float)sides) * 2.0f * PI;
        
        // Tạo viền lượn sóng tự nhiên bằng Sine noise
        float n1 = 1.0f + 0.15f * sinf(angle1 * 3.0f + time) + 0.1f * cosf(angle1 * 5.0f - time * 0.5f);
        float n2 = 1.0f + 0.15f * sinf(angle2 * 3.0f + time) + 0.1f * cosf(angle2 * 5.0f - time * 0.5f);
        
        Vector3 v1 = {pos.x + radius * n1 * cosf(angle1), pos.y, pos.z + radius * n1 * sinf(angle1)};
        Vector3 v2 = {pos.x + radius * n2 * cosf(angle2), pos.y, pos.z + radius * n2 * sinf(angle2)};
        
        // UV căn giữa, map theo biến dạng n1, n2 để texture không bị giãn
        rlColor4ub(0, 150, 255, 220); // Tint color xanh nước biển
        
        // UV căn giữa (0.5, 0.5) toả ra ngoài (0->1) để decal_flow.fs tính toán edge mask tròn
        rlTexCoord2f(0.5f, 0.5f);
        rlVertex3f(pos.x, pos.y, pos.z);
        
        rlTexCoord2f(0.5f + 0.5f * n2 * cosf(angle2), 0.5f + 0.5f * n2 * sinf(angle2));
        rlVertex3f(v2.x, v2.y, v2.z);
        
        rlTexCoord2f(0.5f + 0.5f * n1 * cosf(angle1), 0.5f + 0.5f * n1 * sinf(angle1));
        rlVertex3f(v1.x, v1.y, v1.z);
    }
    rlEnd();
}

void Prefab_DrawMagicPuddle(Vector3 pos, float radius) {
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ALPHA); // Đổi thành ALPHA để nước không bị cộng dồn thành màu trắng sáng chói
    rlDisableDepthMask();
    
    rlPushMatrix();
    rlTranslatef(pos.x, pos.y + 0.05f, pos.z);
    
    Shader flowShader = ResourceManager_LoadShader(0, "core/shaders/puddle.fs");
    Texture2D tex = ResourceManager_LoadTexture("assets/textures/water_caustics.png");
    Texture2D flowTex = ResourceManager_LoadTexture("assets/textures/water_flow.png");
    
    int timeLoc = GetShaderLocation(flowShader, "u_time");
    int tex0Loc = GetShaderLocation(flowShader, "causticsTex");
    int tex1Loc = GetShaderLocation(flowShader, "flowTex");
    
    float time = GetTime();
    
    SetShaderValue(flowShader, timeLoc, &time, SHADER_UNIFORM_FLOAT);
    
    BeginShaderMode(flowShader);
    
    // Bind both textures explicitly using Raylib's built-in uniform manager
    // This avoids conflicts with rlBegin's internal texture unit 0
    SetShaderValueTexture(flowShader, tex0Loc, tex);
    SetShaderValueTexture(flowShader, tex1Loc, flowTex);
    
    rlDrawRenderBatchActive(); 
    rlSetTexture(tex.id);
    
    DrawOrganicPuddle((Vector3){0, 0, 0}, radius);
    
    rlSetTexture(0);
    EndShaderMode();
    
    rlDrawRenderBatchActive(); // Flush batch trước khi đổi depth mask
    rlEnableDepthMask();
    EndBlendMode();
    rlPopMatrix();
    rlEnableBackfaceCulling();
}

void Prefab_DrawFireball(Vector3 pos, float radius, float time) {
    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();
    
    // Lõi rực sáng (không distortion)
    EffectMaterialParams coreParams = {0};
    coreParams.baseColor = (Color){255, 200, 100, 255};
    coreParams.emissiveIntensity = 2.0f;
    EffectMaterial coreMat = Material_LoadCustom(coreParams);
    Material_Begin(coreMat);
    DrawCoreSphere(pos, radius * 0.6f, 16, 16, WHITE);
    Material_End();
    
    // Vỏ lửa lởm chởm bóp méo (distortion mạnh)
    EffectMaterialParams auraParams = {0};
    auraParams.baseColor = (Color){255, 100, 0, 150};
    auraParams.rimStrength = 2.0f;
    auraParams.fresnelPower = 2.0f;
    auraParams.emissiveIntensity = 1.0f;
    auraParams.distortionStrength = 0.8f; // Bóp méo dữ dội
    auraParams.translucency = 1.0f;
    EffectMaterial auraMat = Material_LoadCustom(auraParams);
    Material_Begin(auraMat);
    DrawCoreSphere(pos, radius, 16, 16, WHITE);
    Material_End();
    
    rlEnableDepthMask();
    EndBlendMode();
}

// ============================================================================
// GROUP 2: EFFECT & PARTICLE (Cast/Update)
// ============================================================================

void Prefab_SpawnSmokePuff(Vector3 pos, float size) {
    ParticleRadialBurstConfig cfg = {0};
    cfg.countMin = 15;
    cfg.countMax = 25;
    cfg.speedMin = 2.0f;
    cfg.speedMax = 4.0f;
    cfg.radiusMin = size * 0.3f;
    cfg.radiusMax = size * 0.8f;
    cfg.lifetimeMin = 1.5f;
    cfg.lifetimeMax = 2.5f;
    cfg.pitchRange = PI;
    cfg.upwardBias = 2.0f;
    
    cfg.colorStart = (Color){100, 100, 100, 200};
    cfg.colorEnd = (Color){50, 50, 50, 0};
    
    static ForceField f = {0};
    if (f.layerCount == 0) {
        ForceLayer fl = {0};
        fl.type = FORCE_VISCOSITY;
        fl.strength = 5.0f;
        ForceField_AddLayer(&f, fl);
    }
    cfg.forceField = &f;
    
    ParticleSystem_SpawnRadialBurst(pos, size, &cfg);
}

void Prefab_SpawnSmokeTrail(Vector3 start, Vector3 end, float duration) {
    Vector3 dir = Vector3Subtract(end, start);
    float len = Vector3Length(dir);
    if (len < 0.1f) return;
    dir = Vector3Scale(dir, 1.0f / len);
    
    int numPuffs = (int)(len / 2.0f) + 1;
    
    ParticleRadialBurstConfig cfg = {0};
    cfg.countMin = 2;
    cfg.countMax = 4;
    cfg.speedMin = 0.5f;
    cfg.speedMax = 1.0f;
    cfg.radiusMin = 0.5f;
    cfg.radiusMax = 1.2f;
    cfg.lifetimeMin = duration * 0.8f;
    cfg.lifetimeMax = duration * 1.2f;
    cfg.pitchRange = PI;
    cfg.upwardBias = 1.0f;
    
    cfg.colorStart = (Color){100, 100, 100, 200};
    cfg.colorEnd = (Color){50, 50, 50, 0};
    
    static ForceField f = {0};
    if (f.layerCount == 0) {
        ForceLayer fl = {0};
        fl.type = FORCE_VISCOSITY;
        fl.strength = 3.0f;
        ForceField_AddLayer(&f, fl);
    }
    cfg.forceField = &f;
    
    for (int i = 0; i <= numPuffs; i++) {
        float t = (float)i / (float)numPuffs;
        Vector3 pos = Vector3Add(start, Vector3Scale(dir, t * len));
        ParticleSystem_SpawnRadialBurst(pos, 1.0f, &cfg);
    }
}

void Prefab_SpawnLongFissure(Vector3 start, Vector3 end, float width) {
    // Tạo Decal Vết nứt dọc theo đường thẳng
    int numDecals = 4;
    for (int i = 0; i <= numDecals; i++) {
        float t = (float)i / numDecals;
        Vector3 pos = Vector3Add(start, Vector3Scale(Vector3Subtract(end, start), t));
        SpawnGroundDecal(DECAL_PRESET_EARTH_SHATTER, pos, width * 3.0f, GetRandomValue(-10, 10) * 0.1f);
    }
    
    // Khói bụi dọc vết nứt
    Prefab_SpawnSmokeTrail(start, end, 3.0f);
    
    // Rung nhẹ
    CameraFX_Shake(0.3f);
}

void Prefab_SpawnLightningBeam(Vector3 start, Vector3 end, float duration) {
    // Sử dụng SpawnLightningTrail đã được xây dựng riêng cho vệt sét
    float dist = Vector3Distance(start, end);
    float speed = dist / fmaxf(duration, 0.001f);
    SpawnLightningTrail(start, end, 1.0f, speed);
    
    // Cập nhật flash sáng (bán kính nhỏ lại để không làm lóa trắng cả màn hình)
    VFXLight_Spawn(start, (Color){0, 200, 255, 255}, 3.0f, 0.2f, 0);
    VFXLight_Spawn(end, (Color){0, 200, 255, 255}, 4.0f, 0.3f, 0);
}
