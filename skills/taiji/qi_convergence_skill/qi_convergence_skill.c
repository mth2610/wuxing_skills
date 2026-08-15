#include "skills/taiji/qi_convergence_skill/qi_convergence_skill.h"

#include "core/composition/visual_composer.h"
#include "core/resource_manager.h"
#include "core/skill_boilerplate.h"
#include "raymath.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "qi_convergence_skill_params.inl"

#define QI_CONVERGENCE_MAX_CASTS 2
#define QI_CONVERGENCE_TRAILS 3
#define QI_CONVERGENCE_PATH_NODES 20

typedef struct {
    bool active;
    int ownerAgentId;
    Vector3 casterPos;
    Vector3 forward;
    Vector3 sourceOffset[QI_CONVERGENCE_TRAILS];
    Matrix trailTransform[QI_CONVERGENCE_TRAILS];
    int trailHandle[QI_CONVERGENCE_TRAILS];
    float elapsed;
    float duration;
    float sizeScale;
} QiConvergenceCast;

static QiConvergenceCast s_casts[QI_CONVERGENCE_MAX_CASTS];
static Texture2D s_bodyTexture = {0};
static Texture2D s_flowTexture = {0};
static Texture2D s_noiseTexture = {0};
static int s_skillIndex = -1;

static float QiConvergence_Smooth01(float x)
{
    x = Clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

static Vector3 QiConvergence_Focus(const QiConvergenceCast *cast)
{
    Vector3 focus = Vector3Add(
        cast->casterPos, Vector3Scale(cast->forward, s_focusForward * cast->sizeScale));
    focus.y += s_focusUp * cast->sizeScale;
    return focus;
}

static void QiConvergence_Stop(QiConvergenceCast *cast)
{
    for (int i = 0; i < QI_CONVERGENCE_TRAILS; ++i) {
        if (cast->trailHandle[i] >= 0) VFX_KillTrail(cast->trailHandle[i]);
        cast->trailHandle[i] = -1;
    }
    cast->active = false;
}

static QiConvergenceCast *QiConvergence_ClaimCast(void)
{
    QiConvergenceCast *oldest = &s_casts[0];
    for (int i = 0; i < QI_CONVERGENCE_MAX_CASTS; ++i) {
        if (!s_casts[i].active) return &s_casts[i];
        if (s_casts[i].elapsed > oldest->elapsed) oldest = &s_casts[i];
    }
    QiConvergence_Stop(oldest);
    return oldest;
}

static bool QiConvergence_HasActiveInstance(int agentId)
{
    for (int i = 0; i < QI_CONVERGENCE_MAX_CASTS; ++i) {
        if (s_casts[i].active && s_casts[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitQiConvergenceSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth;
    (void)screenHeight;
    for (int i = 0; i < QI_CONVERGENCE_MAX_CASTS; ++i) {
        s_casts[i].active = false;
        for (int j = 0; j < QI_CONVERGENCE_TRAILS; ++j)
            s_casts[i].trailHandle[j] = -1;
    }

    s_bodyTexture = ResourceManager_LoadTexture("assets/textures/energy_volume.png");
    s_flowTexture = ResourceManager_LoadTexture("assets/textures/energy_volume_flow.png");
    s_noiseTexture = ResourceManager_LoadTexture("assets/textures/noise.png");

    s_skillIndex = Skill_GetIndexByName("QI_CONVERGENCE");
    if (s_skillIndex < 0) return;

    static SkillTunableEntry s_tunables[QI_CONVERGENCE_TUNABLE_COUNT];
    int tn = 0;
#include "qi_convergence_skill_tunables.inl"
    SkillTunables_LoadPersisted(
        "skills/taiji/qi_convergence_skill/qi_convergence_skill.tuning",
        s_tunables, tn);
    RegisterSkillTunables(s_skillIndex, s_tunables, tn);
    RegisterSkillLifecycleQuery(s_skillIndex, QiConvergence_HasActiveInstance);
}

void CastQiConvergenceSkill(int agentId, Vector3 startPos, Vector3 target,
                            SkillParams params)
{
    if (s_skillIndex >= 0 && !SkillManager_CanCast(s_skillIndex, agentId)) return;

    Vector3 aim = Vector3Subtract(target, startPos);
    aim.y = 0.0f;
    if (Vector3LengthSqr(aim) < 1e-8f) aim = (Vector3){0.0f, 0.0f, 1.0f};
    aim = Vector3Normalize(aim);
    Vector3 right = Vector3Normalize(Vector3CrossProduct(
        aim, (Vector3){0.0f, 1.0f, 0.0f}));
    Vector3 up = (Vector3){0.0f, 1.0f, 0.0f};

    QiConvergenceCast *cast = QiConvergence_ClaimCast();
    *cast = (QiConvergenceCast){
        .active = true,
        .ownerAgentId = agentId,
        .casterPos = startPos,
        .forward = aim,
        .elapsed = 0.0f,
        .duration = fmaxf(s_castDuration, 0.05f),
        .sizeScale = params.sizeScale > 0.0f ? params.sizeScale : 1.0f,
    };

    static const float k_side[QI_CONVERGENCE_TRAILS] = {-1.00f, 0.88f, -0.32f};
    static const float k_up[QI_CONVERGENCE_TRAILS] = {0.24f, 0.62f, -0.55f};
    static const float k_back[QI_CONVERGENCE_TRAILS] = {0.36f, 0.22f, 0.48f};
    VFX_TrailSurface surface = {
        .texture = s_bodyTexture,
        .flowMap = s_flowTexture,
        .noiseMask = s_noiseTexture,
        .flowSpeed = s_flowSpeed,
        .flowStrength = s_flowStrength,
        .flowTiling = s_flowTiling,
        .dissolve = 0.08f,
        .maskTiling = 1.25f,
    };

    Vector3 focus = QiConvergence_Focus(cast);
    for (int i = 0; i < QI_CONVERGENCE_TRAILS; ++i) {
        float reach = s_sourceReach * cast->sizeScale;
        cast->sourceOffset[i] = Vector3Add(
            Vector3Scale(right, k_side[i] * reach),
            Vector3Add(Vector3Scale(up, k_up[i] * reach),
                       Vector3Scale(aim, -k_back[i] * reach)));
        cast->trailTransform[i] = MatrixTranslate(focus.x, focus.y, focus.z);
        cast->trailHandle[i] = VFX_ComposeTrailEx(
            &cast->trailTransform[i], VC_MAT_QI,
            s_trailWidth * cast->sizeScale, cast->duration,
            TRAIL_PRESET_BACKDROP, &surface);
        Vector3 source = Vector3Add(cast->casterPos, cast->sourceOffset[i]);
        Trail_SetStaticPath(cast->trailHandle[i], source, focus,
                            QI_CONVERGENCE_PATH_NODES);
        VFX_TrailSetWidth(cast->trailHandle[i], 0.0f);
    }

    if (s_skillIndex >= 0)
        SkillManager_TriggerCooldown(s_skillIndex, agentId, fmaxf(s_cooldown, 0.0f));
}

void UpdateQiConvergenceSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    (void)enemyPos;
    (void)enemyRadius;
    for (int i = 0; i < QI_CONVERGENCE_MAX_CASTS; ++i) {
        QiConvergenceCast *cast = &s_casts[i];
        if (!cast->active) continue;

        Vector3 livePos;
        if (SkillManager_GetAgentPos(cast->ownerAgentId, &livePos))
            cast->casterPos = livePos;

        cast->elapsed += fmaxf(dt, 0.0f);
        float t01 = Clamp(cast->elapsed / cast->duration, 0.0f, 1.0f);
        float appear = QiConvergence_Smooth01(t01 / 0.20f);
        float suckDenom = fmaxf(1.0f - s_suckStart, 0.05f);
        float suck = QiConvergence_Smooth01((t01 - s_suckStart) / suckDenom);
        float vanish = 1.0f - QiConvergence_Smooth01((t01 - 0.84f) / 0.16f);
        float width = appear * vanish;
        Vector3 focus = QiConvergence_Focus(cast);

        for (int j = 0; j < QI_CONVERGENCE_TRAILS; ++j) {
            Vector3 source = Vector3Add(cast->casterPos, cast->sourceOffset[j]);
            Vector3 tail = Vector3Lerp(source, focus, suck);
            Trail_SetStaticPath(cast->trailHandle[j], tail, focus,
                                QI_CONVERGENCE_PATH_NODES);
            VFX_TrailSetWidth(cast->trailHandle[j], width);
        }

        if (cast->elapsed >= cast->duration) QiConvergence_Stop(cast);
    }
}

void DrawQiConvergenceSkill(void)
{
    // TrailSystem owns BODY/EMISSION rendering and flow animation.
}

void UnloadQiConvergenceSkill(void)
{
    for (int i = 0; i < QI_CONVERGENCE_MAX_CASTS; ++i)
        QiConvergence_Stop(&s_casts[i]);
}

SKILL_EMPTY_PROJECTILE_API(QiConvergence)
