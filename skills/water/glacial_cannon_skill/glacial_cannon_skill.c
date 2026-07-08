#include "glacial_cannon_skill.h"
#include "core/skill_boilerplate.h"
#include "core/skill_helper.h"
#include "core/skill_manager.h"
#include "core/path_spline.h"
#include "core/procedural_mesh_utils.h"
#include "core/utils_math.h"
#include "core/decal_system.h"
#include "core/particle_system.h"
#include "core/resource_manager.h"
#include "raymath.h"

#define MAX_INSTANCES 4

typedef enum
{
    STATE_CASTING,
    STATE_CHANNELING,
    STATE_IMPACT_BURST,
    STATE_DONE
} SkillState;

typedef struct
{
    bool active;
    SkillState state;
    int ownerAgentId;
    Vector3 startPos;
    Vector3 targetPos;
    float timer;
    float sizeScale;
    float damageAccumulator;

    Vector3 pathPoints[32];
    int pathPointCount;
    int lastSpawnedIdx;

    int burstSeed;
    float burstProgress;
} SkillInstance;

static SkillInstance s_instances[MAX_INSTANCES];

#include "glacial_cannon_skill_params.inl"

void InitGlacialCannonSkill(int screenWidth, int screenHeight)
{
    for (int i = 0; i < MAX_INSTANCES; i++)
        s_instances[i].active = false;

#define GLACIAL_CANNON_TUNABLE_COUNT 6
    static SkillTunableEntry s_tunables[GLACIAL_CANNON_TUNABLE_COUNT];
    int tn = 0;
#include "glacial_cannon_skill_tunables.inl"
    int skillIndex = Skill_GetIndexByName("GLACIAL_CANNON");
    SkillTunables_LoadPersisted("skills/water/glacial_cannon_skill/glacial_cannon_skill.tuning", s_tunables, tn);
    RegisterSkillTunables(skillIndex, s_tunables, tn);
}

void CastGlacialCannonSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    if (!SkillManager_CanCast(Skill_GetIndexByName("GLACIAL_CANNON"), agentId))
        return;

    for (int i = 0; i < MAX_INSTANCES; i++)
    {
        if (s_instances[i].active)
            continue;

        SkillInstance *s = &s_instances[i];
        s->state = STATE_CASTING;
        s->startPos = startPos;
        s->targetPos = target;
        s->timer = 0.0f;
        s->sizeScale = params.sizeScale;
        s->damageAccumulator = 0.0f;
        s->ownerAgentId = agentId;
        s->lastSpawnedIdx = -1;
        s->burstSeed = 0;
        s->burstProgress = 0.0f;

        // Initialize path points (line from start to target)
        s->pathPointCount = 16;
        for (int j = 0; j < s->pathPointCount; j++)
        {
            float t = (float)j / (float)(s->pathPointCount - 1);
            s->pathPoints[j] = Vector3Lerp(startPos, target, t);
        }

        s->active = true;

        PlayCastSound(EFFECT_PRESET_ICE_SHATTER);

        // Trigger cooldown
        int skillIndex = Skill_GetIndexByName("GLACIAL_CANNON");
        SkillManager_TriggerCooldown(skillIndex, agentId, Skill_CalculateCooldown(SKILL_CAT_PROJECTILE, params));
        return;
    }
}

void UpdateGlacialCannonSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    for (int i = 0; i < MAX_INSTANCES; i++)
    {
        SkillInstance *s = &s_instances[i];
        if (!s->active)
            continue;

        s->timer += dt;

        if (s->state == STATE_CASTING)
        {
            if (s->timer >= s_castDuration)
            {
                s->state = STATE_CHANNELING;
                s->timer = 0.0f;
            }
        }
        else if (s->state == STATE_CHANNELING)
        {
            float progress = s->timer / s_waveDuration;
            if (progress > 1.0f)
                progress = 1.0f;

            int wavefrontIdx = (int)(progress * (s->pathPointCount - 1));
            if (wavefrontIdx >= s->pathPointCount)
                wavefrontIdx = s->pathPointCount - 1;

            VFX_ComposePathMistWave(VC_MAT_ICE, s->pathPoints, s->pathPointCount, progress, s->sizeScale * 0.8);

            s->damageAccumulator += s_damagePerSecond * dt;
            if (s->damageAccumulator >= 5.0f)
            {
                Vector3 currentImpactPos = s->pathPoints[wavefrontIdx];

                ApplyAoEDamage(currentImpactPos, s_aoeRadius * s->sizeScale, s->damageAccumulator, 0.0f);
                s->damageAccumulator = 0.0f;

                if (GetRandomValue(0, 100) < 40)
                {
                    // VFX_TriggerExplosion(VC_MAT_ICE, currentImpactPos, s->sizeScale, false);
                    PlayImpactSound(EFFECT_PRESET_ICE_SHATTER);
                }
            }

            if (s->timer >= s_waveDuration)
            {
                PlayImpactSound(EFFECT_PRESET_ICE_SHATTER);
                ApplyAoEDamage(s->targetPos, s_aoeRadius * s->sizeScale * 1.8f, s_damagePerSecond * 0.5f, 1.5f);

                unsigned int seedBits = (unsigned int)(GetTime() * 1000.0f) ^ ((unsigned int)s->ownerAgentId * 747796405u) ^ ((unsigned int)i * 2891336453u);
                s->burstSeed = (int)seedBits;
                s->burstProgress = 0.0f;

                s->state = STATE_IMPACT_BURST;
                s->timer = 0.0f;
            }
        }
        else if (s->state == STATE_IMPACT_BURST)
        {
            s->burstProgress = s->timer / s_burstDuration;
            if (s->burstProgress > 1.0f)
                s->burstProgress = 1.0f;

            if (s->timer >= s_burstDuration)
            {
                s->state = STATE_DONE;
                s->active = false;
            }
        }
    }
}

void DrawGlacialCannonSkill(void)
{
    float time = GetTime();
    for (int i = 0; i < MAX_INSTANCES; i++)
    {
        SkillInstance *s = &s_instances[i];
        if (!s->active)
            continue;

        // Draw Aura Qi continuously from cast until the skill is complete
        // VFX_ComposeAura(VC_MAT_QI, s->startPos, s_aoeRadius * s->sizeScale, time);

        if (s->state == STATE_CHANNELING)
        {
            float progress = s->timer / s_waveDuration;
            // VFX_ComposeCylinderAura(VC_MAT_ICE, s->startPos, s_aoeRadius * s->sizeScale * 0.3, progress, time);
            VFX_ComposeGroundAura(VC_MAT_ICE, s->startPos, s_aoeRadius * s->sizeScale * 0.5, -0.9f, time);

            if (progress > 1.0f)
                progress = 1.0f;

            // Draw sequential Ice Spikes along path (with updated deterministic non-wobbly mesh)
            VFX_PathWave(PATH_ICE_SPIKE, s->pathPoints, s->pathPointCount, s_spikeScale * s->sizeScale, progress, time);
        }
        else if (s->state == STATE_IMPACT_BURST)
        {
            VFX_DrawIceCrystalBurst(s->targetPos, GLACIAL_CANNON_BURST_CRYSTAL_COUNT, s->burstSeed, s->burstProgress);
        }
    }
}

void UnloadGlacialCannonSkill(void)
{
    // No-op
}

SKILL_EMPTY_PROJECTILE_API(GlacialCannon)
