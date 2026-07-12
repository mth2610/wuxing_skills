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
#include "combat/combat.h"
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
    int castSeed; // THÊM BIẾN NÀY

    int burstSeed;
    float burstProgress;
} SkillInstance;

static SkillInstance s_instances[MAX_INSTANCES];

// TỐI ƯU: Cache lại Skill Index để tránh gọi tra cứu chuỗi tốn kém mỗi lần Cast
static int s_glacialCannonSkillIndex = -1;

#include "glacial_cannon_skill_params.inl"

void InitGlacialCannonSkill(int screenWidth, int screenHeight)
{
    for (int i = 0; i < MAX_INSTANCES; i++)
        s_instances[i].active = false;

#define GLACIAL_CANNON_TUNABLE_COUNT 6
    static SkillTunableEntry s_tunables[GLACIAL_CANNON_TUNABLE_COUNT];
    int tn = 0;
#include "glacial_cannon_skill_tunables.inl"

    s_glacialCannonSkillIndex = Skill_GetIndexByName("GLACIAL_CANNON");
    SkillTunables_LoadPersisted("skills/water/glacial_cannon_skill/glacial_cannon_skill.tuning", s_tunables, tn);
    RegisterSkillTunables(s_glacialCannonSkillIndex, s_tunables, tn);
}

void CastGlacialCannonSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    // Dùng index đã cache thay vì GetIndexByName
    if (!SkillManager_CanCast(s_glacialCannonSkillIndex, agentId))
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

        // Trộn thời gian và ID để sinh seed độc nhất cho MỖI LẦN CAST
        s->castSeed = (int)((unsigned int)(GetTime() * 10000.0f) ^ (unsigned int)GetRandomValue(0, 9999999));

        s->burstSeed = 0;
        s->burstProgress = 0.0f;
        s->pathPointCount = 16;

        // TỐI ƯU: Chuyển phép chia ra ngoài vòng lặp. Nhân luôn nhanh hơn chia.
        float invPathSteps = 1.0f / (float)(s->pathPointCount - 1);
        for (int j = 0; j < s->pathPointCount; j++)
        {
            float t = (float)j * invPathSteps;
            s->pathPoints[j] = Vector3Lerp(startPos, target, t);
        }

        s->active = true;

        PlayCastSound(EFFECT_PRESET_ICE_SHATTER);

        // Dùng index đã cache
        SkillManager_TriggerCooldown(s_glacialCannonSkillIndex, agentId, Skill_CalculateCooldown(SKILL_CAT_PROJECTILE, params));
        return;
    }
}

void UpdateGlacialCannonSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    // Đấu Pháp events (peek — COMBAT_API.md: ids = skillIndex*1000 + slot).
    // Losing a clash or crashing into an agent skips the wave straight to
    // its impact burst at the clash point.
    {
        const ClashEvent *ev;
        int evCount = Combat_PeekEvents(&ev);
        for (int k = 0; k < evCount; k++)
        {
            int slot = ev[k].skillInstanceId - s_glacialCannonSkillIndex * 1000;
            if (slot < 0 || slot >= MAX_INSTANCES) continue;
            SkillInstance *s = &s_instances[slot];
            if (!s->active || s->state != STATE_CHANNELING) continue;
            if (ev[k].outcome == CLASH_B_WINS || ev[k].outcome == CLASH_MUTUAL_DESTROY ||
                ev[k].outcome == CLASH_HIT_AGENT)
            {
                PlayImpactSound(EFFECT_PRESET_ICE_SHATTER);
                s->targetPos = ev[k].clashPoint;
                s->burstSeed = (int)((unsigned int)(GetTime() * 1000.0f) ^ ((unsigned int)slot * 2891336453u));
                s->burstProgress = 0.0f;
                s->state = STATE_IMPACT_BURST;
                s->timer = 0.0f;
            }
        }
    }

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
            // Linh khí tan khi chủ nhân chết: a channel with a dead caster
            // fizzles instead of rolling on ownerless (Combat_SubmitProjectile
            // would read its team as NEUTRAL — hitting BOTH sides — and pool
            // slot reuse could even flip it to the wrong team).
            if (Entity_GetAgent(s->ownerAgentId) == NULL)
            {
                s->state = STATE_DONE;
                s->active = false;
                continue;
            }

            float progress = s->timer / s_waveDuration;
            if (progress > 1.0f)
                progress = 1.0f;

            int wavefrontIdx = (int)(progress * (s->pathPointCount - 1));
            if (wavefrontIdx >= s->pathPointCount)
                wavefrontIdx = s->pathPointCount - 1;

            // VFX_ComposePathMistWave(VC_MAT_ICE, s->pathPoints, s->pathPointCount, progress, s->sizeScale * 0.8f);

            // Đấu Pháp: the ice wavefront is a combat collider — combat owns
            // hit detection + agent damage now (COMBAT_API.md §5); the old
            // core ApplyAoEDamage tick (no HP bookkeeping) is gone.
            Combat_SubmitProjectile(s->ownerAgentId, ELEM_WATER,
                                    s->pathPoints[wavefrontIdx],
                                    s_aoeRadius * s->sizeScale,
                                    s_damagePerSecond * 0.5f,
                                    2.0f,
                                    s_glacialCannonSkillIndex * 1000 + i);

            s->damageAccumulator += s_damagePerSecond * dt;
            if (s->damageAccumulator >= 5.0f)
            {
                s->damageAccumulator = 0.0f;
                if (GetRandomValue(0, 100) < 40)
                {
                    PlayImpactSound(EFFECT_PRESET_ICE_SHATTER);
                }
            }

            if (s->timer >= s_waveDuration)
            {
                PlayImpactSound(EFFECT_PRESET_ICE_SHATTER);
                // Final burst: real team-aware agent AoE (allowed direct
                // path for AoE per COMBAT_API.md §5).
                {
                    const Agent *owner = Entity_GetAgent(s->ownerAgentId);
                    Entity_ApplyAoEDamage(s->targetPos, s_aoeRadius * (s->sizeScale * 1.8f),
                                          s_damagePerSecond * 0.5f, 1.5f,
                                          owner ? owner->team : TEAM_NEUTRAL);
                }

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

        if (s->state == STATE_CHANNELING)
        {
            float progress = s->timer / s_waveDuration;
            // VFX_ComposeGroundAura(VC_MAT_ICE, s->startPos, s_aoeRadius * s->sizeScale * 0.5f, -0.9f, time);

            if (progress > 1.0f)
                progress = 1.0f;

            VFX_PathWave(PATH_ICE_SPIKE, s->pathPoints, s->pathPointCount, s_spikeScale * s->sizeScale, progress, time, s->castSeed);
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