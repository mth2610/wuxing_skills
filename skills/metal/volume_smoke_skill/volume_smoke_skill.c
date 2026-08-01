#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#include "skills/metal/volume_smoke_skill/volume_smoke_skill.h"
#include "core/resource_manager.h"
#include "core/procedural_mesh_utils.h"
#include "core/screen_distort.h"

#define MAX_SMOKES 16

typedef struct
{
    Vector3 position;
    float radius;
    float life;
    float maxLife;
    bool active;
} VolumeSmokeInst;

static VolumeSmokeInst s_smokes[MAX_SMOKES];
static Shader s_shader = {0};

// Cache uniform locations
static int s_uCenterLoc = -1;
static int s_uRadiusLoc = -1;
static int s_uProgressLoc = -1;

void InitVolumeSmokeSkill(int screenWidth, int screenHeight)
{
    for (int i = 0; i < MAX_SMOKES; i++)
    {
        s_smokes[i].active = false;
    }

    // Load shader cho Raymarching (Vulkan backend cần cả .vs và .fs khi có chiếu sáng 3D)
    s_shader = ResourceManager_LoadShader(
        "skills/metal/volume_smoke_skill/volume_smoke.vs",
        "skills/metal/volume_smoke_skill/volume_smoke.fs");

    s_uCenterLoc = GetShaderLocation(s_shader, "u_center");
    s_uRadiusLoc = GetShaderLocation(s_shader, "u_radius");
    s_uProgressLoc = GetShaderLocation(s_shader, "u_progress");
}

void CastVolumeSmokeSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    for (int i = 0; i < MAX_SMOKES; i++)
    {
        if (!s_smokes[i].active)
        {
            // Ép scale tối thiểu = 1.0 để chống lỗi truyền 0.0
            float safeScale = (params.sizeScale > 0.0f) ? params.sizeScale : 1.0f;
            s_smokes[i].radius = 1.5f * safeScale;

            s_smokes[i].position = target;
            // Nâng tâm khối cầu lên trên mặt đất một nửa bán kính để không bị chìm
            s_smokes[i].position.y += s_smokes[i].radius * 0.5f;

            s_smokes[i].maxLife = 5.0f;
            s_smokes[i].life = 0.0f;
            s_smokes[i].active = true;
            break;
        }
    }
}

void UpdateVolumeSmokeSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    for (int i = 0; i < MAX_SMOKES; i++)
    {
        if (s_smokes[i].active)
        {
            s_smokes[i].life += dt;
            if (s_smokes[i].life >= s_smokes[i].maxLife)
            {
                s_smokes[i].active = false;
            }
        }
    }
}

void DrawVolumeSmokeSkill(void)
{
    // Volumetric smoke is matter, not only emitted light. Draw it into the
    // shared VFX body target so bright scenery cannot bleach its shading.
    ScreenDistort_BeginVFXBody();
    BeginBlendMode(BLEND_ALPHA);

    for (int i = 0; i < MAX_SMOKES; i++)
    {
        if (s_smokes[i].active)
        {
            // Tự động giải quyết matModel cho rlvk immediate-mode (DrawCoreSphere)
            SkillManager_BeginShader(s_shader);

            if (s_uCenterLoc != -1)
                SetShaderValue(s_shader, s_uCenterLoc, &s_smokes[i].position, SHADER_UNIFORM_VEC3);
            if (s_uRadiusLoc != -1)
                SetShaderValue(s_shader, s_uRadiusLoc, &s_smokes[i].radius, SHADER_UNIFORM_FLOAT);

            float progress = s_smokes[i].life / s_smokes[i].maxLife;
            if (s_uProgressLoc != -1)
                SetShaderValue(s_shader, s_uProgressLoc, &progress, SHADER_UNIFORM_FLOAT);

            // Draw bounding sphere để fragment shader tiến hành raymarching bên trong
            DrawCoreSphere(s_smokes[i].position, s_smokes[i].radius, 16, 16, WHITE);

            SkillManager_EndShader();
        }
    }

    EndBlendMode();
    ScreenDistort_EndVFXLayer();
}

void UnloadVolumeSmokeSkill(void)
{
    // ResourceManager tự động dọn dẹp Shader, không gọi UnloadShader ở đây
}
