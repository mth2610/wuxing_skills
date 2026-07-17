#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#include "skills/fire/campfire_skill/campfire_skill.h"
#include "core/resource_manager.h"
#include "core/geometry/procedural_mesh_utils.h"
#include "rlgl.h"

#define MAX_CAMPFIRES 8

typedef struct
{
    Vector3 base;    // ground position of the fire (base at y = base.y)
    float   scale;   // overall size in meters
    float   life;
    float   maxLife;
    bool    active;
} CampfireInst;

static CampfireInst s_fires[MAX_CAMPFIRES];
static Shader s_shader = {0};
static int s_uRadiusLoc = -1;

void InitCampfireSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth;
    (void)screenHeight;
    for (int i = 0; i < MAX_CAMPFIRES; i++)
        s_fires[i].active = false;

    // Raymarch shader (rlvk/Vulkan needs both .vs and .fs). viewPos/u_time/u_resolution are
    // auto-bound by SkillManager_BeginShader; u_center/u_scale are set per instance below.
    s_shader = ResourceManager_LoadShader(
        "skills/fire/campfire_skill/campfire.vs",
        "skills/fire/campfire_skill/campfire.fs");
    s_uRadiusLoc = GetShaderLocation(s_shader, "u_radius");
}

void CastCampfireSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    (void)agentId;
    (void)startPos;
    for (int i = 0; i < MAX_CAMPFIRES; i++)
    {
        if (!s_fires[i].active)
        {
            float s = (params.sizeScale > 0.0f) ? params.sizeScale : 1.0f;
            s_fires[i].base = target; // fire base sits on the ground at the target
            s_fires[i].scale = s;
            s_fires[i].maxLife = 10.0f;
            s_fires[i].life = 0.0f;
            s_fires[i].active = true;
            break;
        }
    }
}

void UpdateCampfireSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    (void)enemyPos;
    (void)enemyRadius;
    for (int i = 0; i < MAX_CAMPFIRES; i++)
    {
        if (s_fires[i].active)
        {
            s_fires[i].life += dt;
            if (s_fires[i].life >= s_fires[i].maxLife)
                s_fires[i].active = false;
        }
    }
}

void DrawCampfireSkill(void)
{
    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask(); // additive glow: test against the scene, but don't write depth

    for (int i = 0; i < MAX_CAMPFIRES; i++)
    {
        if (!s_fires[i].active)
            continue;

        float s = s_fires[i].scale;
        Vector3 c = s_fires[i].base;
        float R = 1.2f * s; // bounding-sphere radius; flame base (q.y=-1) sits at the sphere bottom

        // Auto-binds viewPos/u_time/u_resolution and resolves matModel for the immediate-mode draw
        SkillManager_BeginShader(s_shader);
        if (s_uRadiusLoc != -1)
            SetShaderValue(s_shader, s_uRadiusLoc, &R, SHADER_UNIFORM_FLOAT);

        // Proxy sphere centered a radius above the base so its bottom rests on the ground; the
        // fragment shader raymarches the flame in the sphere's local space (fragNormal-based).
        Vector3 proxyCenter = {c.x, c.y + R, c.z};
        DrawCoreSphere(proxyCenter, R, 16, 16, WHITE);

        SkillManager_EndShader();
    }

    rlEnableDepthMask();
    EndBlendMode();
}

void UnloadCampfireSkill(void)
{
    // ResourceManager owns the shader lifecycle — do not UnloadShader here.
}
