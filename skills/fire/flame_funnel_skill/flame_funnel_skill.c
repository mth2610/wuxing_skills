#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "raylib.h"
#include "skills/fire/flame_funnel_skill/flame_funnel_skill.h"

#include "core/resource_manager.h"
#include "core/procedural_mesh_utils.h"
#include "core/particle_system.h"
#include "core/force_field.h"
#include "core/color_gradient.h"
#include "core/vfx_light.h"
#include "core/utils_math.h"
#include "core/tuning.h"

// ---------------------------------------------------------------------------
// Kỹ thuật: một khối lửa dạng "phễu ngược" (đáy to, đỉnh nhỏ), dựng bằng
// ProceduralMesh_BuildVortexFunnel (bottomRadius > topRadius). Vertex shader
// làm phần "phập phồng cuộn từ dưới lên": mỗi đỉnh bị đẩy ra/vào theo một
// sóng fbm cuộn theo trục Y trừ đi thời gian, nên đỉnh sóng luôn leo dần từ
// gốc lên ngọn mỗi frame. Fragment shader không dùng texture flow-map thật
// (VortexFunnel không có UV) mà giả lập flow bằng fbm2 lấy theo world
// position/góc quanh trục — cùng ý tưởng "cuộn theo thời gian" như flowBlend
// nhưng không tốn băng thông texture, nên rẻ. Toàn bộ khối lửa là DUY NHẤT
// một draw call (một rlBegin/rlEnd), không texture, không multi-pass.
// ---------------------------------------------------------------------------

#define MAX_FLAME_FUNNEL_INSTANCES 4

typedef struct
{
    bool active;
    int ownerAgentId;
    Vector3 basePos;
    float age;
    float lifetime;
    float sizeScale;
    float seed; // lệch pha riêng mỗi lần cast, tránh nhiều đám lửa nhấp nháy đồng bộ (12.3)
    float emberTimer;
    VortexFunnelMeshData mesh;
} FlameFunnelInstance;

static FlameFunnelInstance s_instances[MAX_FLAME_FUNNEL_INSTANCES];

static Shader s_shader;
static bool s_shaderReady = false;

// Uniform locations — tra cứu một lần ở Init, không GetShaderLocation mỗi frame.
static int s_locCenter;
static int s_locBaseY;
static int s_locHeight;
static int s_locFlickerAmp;
static int s_locScrollSpeed;
static int s_locRidgeFreq;
static int s_locSeed;
static int s_locAlphaFade;

// Tunable — chỉnh trực tiếp trong tuning.cfg lúc game đang chạy, không cần rebuild.
static float s_bottomRadius = 0.64f;
static float s_topRadius = 0.15f;
static float s_height = 3.35f;
static float s_flickerAmp = 0.10f;
static float s_scrollSpeed = 1.8f;
static float s_ridgeFreq = 5.0f;
static float s_twistDegPerS = 14.0f;
static float s_duration = 10.5f;

static ForceField s_emberForce;
static ColorGradient s_emberGradient;

static int FindFreeFlameFunnelSlot(void)
{
    for (int i = 0; i < MAX_FLAME_FUNNEL_INSTANCES; i++)
    {
        if (!s_instances[i].active)
            return i;
    }
    return -1;
}

void InitFlameFunnelSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth;
    (void)screenHeight;

    for (int i = 0; i < MAX_FLAME_FUNNEL_INSTANCES; i++)
    {
        s_instances[i].active = false;
    }

    s_shader = ResourceManager_LoadShader(
        "skills/fire/flame_funnel_skill/flame_funnel.vs",
        "skills/fire/flame_funnel_skill/flame_funnel.fs");
    s_shaderReady = (s_shader.id != 0);

    s_locCenter = GetShaderLocation(s_shader, "u_center");
    s_locBaseY = GetShaderLocation(s_shader, "u_baseY");
    s_locHeight = GetShaderLocation(s_shader, "u_height");
    s_locFlickerAmp = GetShaderLocation(s_shader, "u_flickerAmp");
    s_locScrollSpeed = GetShaderLocation(s_shader, "u_scrollSpeed");
    s_locRidgeFreq = GetShaderLocation(s_shader, "u_ridgeFreq");
    s_locSeed = GetShaderLocation(s_shader, "u_seed");
    s_locAlphaFade = GetShaderLocation(s_shader, "u_alphaFade");

    Tuning_RegisterFloat("flame_funnel_bottom_radius", &s_bottomRadius, s_bottomRadius);
    Tuning_RegisterFloat("flame_funnel_top_radius", &s_topRadius, s_topRadius);
    Tuning_RegisterFloat("flame_funnel_height", &s_height, s_height);
    Tuning_RegisterFloat("flame_funnel_flicker_amp", &s_flickerAmp, s_flickerAmp);
    Tuning_RegisterFloat("flame_funnel_scroll_speed", &s_scrollSpeed, s_scrollSpeed);
    Tuning_RegisterFloat("flame_funnel_ridge_freq", &s_ridgeFreq, s_ridgeFreq);
    Tuning_RegisterFloat("flame_funnel_twist_deg_s", &s_twistDegPerS, s_twistDegPerS);
    Tuning_RegisterFloat("flame_funnel_duration", &s_duration, s_duration);

    // Lớp tàn lửa nhẹ bay lên quanh gốc lửa — buoyant wind + curl nhẹ cho tự nhiên,
    // drag để không bay quá xa. Rẻ vì rate thấp (xem UpdateFlameFunnelSkill).
    ForceField_Clear(&s_emberForce);
    ForceField_AddLayer(&s_emberForce, (ForceLayer){
                                           .type = FORCE_WIND,
                                           .direction = (Vector3){0.0f, 1.0f, 0.0f},
                                           .strength = 1.4f,
                                       });
    ForceField_AddLayer(&s_emberForce, (ForceLayer){
                                           .type = FORCE_NOISE_CURL,
                                           .origin = (Vector3){0.0f, 0.0f, 0.0f},
                                           .strength = 0.6f,
                                           .noiseScale = 2.5f,
                                           .noiseSpeed = 1.2f,
                                       });
    ForceField_AddLayer(&s_emberForce, (ForceLayer){
                                           .type = FORCE_DRAG,
                                           .strength = 0.5f,
                                       });

    s_emberGradient.count = 0;
    ColorGradient_AddStop(&s_emberGradient, 0.0f, (Color){255, 244, 200, 255});
    ColorGradient_AddStop(&s_emberGradient, 0.45f, ColorAlpha(ELEMENT_COLOR_FIRE, 1.0f));
    ColorGradient_AddStop(&s_emberGradient, 1.0f, ColorAlpha((Color){120, 20, 10, 255}, 0.0f));
}

void CastFlameFunnelSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    (void)target;

    int slot = FindFreeFlameFunnelSlot();
    if (slot < 0)
        return;

    FlameFunnelInstance *inst = &s_instances[slot];
    inst->active = true;
    inst->ownerAgentId = agentId;
    inst->basePos = startPos;
    inst->age = 0.0f;
    inst->lifetime = s_duration;
    inst->sizeScale = (params.sizeScale > 0.0f) ? params.sizeScale : 1.0f;
    inst->seed = Random01() * 100.0f;
    inst->emberTimer = 0.0f;
}

void UpdateFlameFunnelSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    (void)enemyPos;
    (void)enemyRadius;

    for (int i = 0; i < MAX_FLAME_FUNNEL_INSTANCES; i++)
    {
        FlameFunnelInstance *inst = &s_instances[i];
        if (!inst->active)
            continue;

        inst->age += dt;
        if (inst->age >= inst->lifetime)
        {
            inst->active = false;
            continue;
        }

        // Dựng lại phễu mỗi frame (bắt buộc với funnel đang xoay/quay theo time,
        // xem CORE_API.md §"Vortex Funnel"). twistAmount xoay chậm để lửa có
        // chút chuyển động xoáy tự nhiên, không đứng yên cứng nhắc.
        VortexFunnelConfig cfg = ProceduralMesh_DefaultVortexFunnelConfig();
        cfg.bottomRadius = s_bottomRadius * inst->sizeScale;
        cfg.topRadius = s_topRadius * inst->sizeScale;
        cfg.height = s_height * inst->sizeScale;
        cfg.twistAmount = 20.0f;
        cfg.ridgeCount = 5;
        cfg.ridgeAmount = 0.16f;

        float spinTime = inst->age * (s_twistDegPerS / 20.0f);
        ProceduralMesh_BuildVortexFunnel(&inst->mesh, inst->basePos, &cfg, 14, 10, spinTime);

        // Tàn lửa bay lên quanh gốc — rate thấp (khoảng 14 hạt/giây), tắt dần
        // trong 20% cuối vòng đời để không "bụp" tắt cùng lúc với khối lửa.
        float fadeStart = inst->lifetime * 0.8f;
        bool fadingOut = inst->age > fadeStart;
        if (!fadingOut)
        {
            inst->emberTimer += dt;
            float emberInterval = 0.07f;
            while (inst->emberTimer >= emberInterval)
            {
                inst->emberTimer -= emberInterval;

                float ang = Random01() * 6.2831853f;
                float r = Random01() * cfg.bottomRadius * 0.8f;
                ParticleConfig ember = {0};
                ember.position = (Vector3){
                    inst->basePos.x + cosf(ang) * r,
                    inst->basePos.y + 0.03f,
                    inst->basePos.z + sinf(ang) * r,
                };
                ember.velocity = (Vector3){
                    (Random01() - 0.5f) * 0.3f,
                    1.0f + Random01() * 0.7f,
                    (Random01() - 0.5f) * 0.3f,
                };
                ember.radius = (0.02f + Random01() * 0.02f) * inst->sizeScale;
                ember.lifetime = 0.55f + Random01() * 0.35f;
                ember.gradient = &s_emberGradient;
                ember.forceField = &s_emberForce;
                SpawnParticle(ember);
            }
        }

        // Ánh sáng bập bùng — spawn lại mỗi frame với lifetime ngắn để tự tắt/tự
        // mồi lại, đúng kiểu "flicker" chuẩn của engine (VFX Standards §"keep
        // point lights alive during active phase").
        float flicker = 0.85f + 0.15f * sinf(inst->age * 18.0f + inst->seed * 3.0f) + (Random01() - 0.5f) * 0.08f;
        VFXLight_Spawn(
            (Vector3){inst->basePos.x, inst->basePos.y + 0.25f * inst->sizeScale, inst->basePos.z},
            ColorLerp(ELEMENT_COLOR_FIRE, WHITE, 0.25f),
            (0.9f + 0.3f * inst->sizeScale) * flicker,
            0.12f,
            VFX_PRIORITY_LOW);
    }
}

void DrawFlameFunnelSkill(void)
{
    if (!s_shaderReady)
        return;

    for (int i = 0; i < MAX_FLAME_FUNNEL_INSTANCES; i++)
    {
        FlameFunnelInstance *inst = &s_instances[i];
        if (!inst->active)
            continue;

        float growIn = SmoothStep01(inst->age / 0.25f);
        float fadeStart = inst->lifetime * 0.8f;
        float fadeOut = (inst->age > fadeStart)
                            ? (1.0f - (inst->age - fadeStart) / (inst->lifetime - fadeStart))
                            : 1.0f;
        float alphaFade = growIn * fadeOut;
        if (alphaFade <= 0.0f)
            continue;

        BeginBlendMode(BLEND_ADDITIVE);
        SkillManager_BeginShader(s_shader);

        Vector3 center = inst->basePos;
        float baseY = inst->basePos.y;
        float height = s_height * inst->sizeScale;

        if (s_locCenter >= 0)
            SetShaderValue(s_shader, s_locCenter, &center, SHADER_UNIFORM_VEC3);
        if (s_locBaseY >= 0)
            SetShaderValue(s_shader, s_locBaseY, &baseY, SHADER_UNIFORM_FLOAT);
        if (s_locHeight >= 0)
            SetShaderValue(s_shader, s_locHeight, &height, SHADER_UNIFORM_FLOAT);
        if (s_locFlickerAmp >= 0)
            SetShaderValue(s_shader, s_locFlickerAmp, &s_flickerAmp, SHADER_UNIFORM_FLOAT);
        if (s_locScrollSpeed >= 0)
            SetShaderValue(s_shader, s_locScrollSpeed, &s_scrollSpeed, SHADER_UNIFORM_FLOAT);
        if (s_locRidgeFreq >= 0)
            SetShaderValue(s_shader, s_locRidgeFreq, &s_ridgeFreq, SHADER_UNIFORM_FLOAT);
        if (s_locSeed >= 0)
            SetShaderValue(s_shader, s_locSeed, &inst->seed, SHADER_UNIFORM_FLOAT);
        if (s_locAlphaFade >= 0)
            SetShaderValue(s_shader, s_locAlphaFade, &alphaFade, SHADER_UNIFORM_FLOAT);

        ProceduralMesh_DrawVortexFunnel(&inst->mesh, WHITE);

        SkillManager_EndShader();
        EndBlendMode();
    }
}

void UnloadFlameFunnelSkill(void)
{
    // Không gọi UnloadShader ở đây — Resource Manager tự dọn khi thoát game
    // (xem CORE_API.md §3 "Mandatory Teardown Rule").
}

bool IsFlameFunnelSkillCoiling(void)
{
    return false; // Không có pha lên đà (coil) — lửa bùng lên gần như tức thì rồi fade-in nhẹ.
}

int GetFlameFunnelSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles)
{
    (void)outProjectiles;
    (void)maxProjectiles;
    return 0; // Đây là hiệu ứng đứng yên tại chỗ, không có projectile.
}

void DeactivateFlameFunnelProjectile(int index)
{
    (void)index;
}
