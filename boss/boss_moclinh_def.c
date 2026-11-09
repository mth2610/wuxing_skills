// boss/boss_moclinh_def.c — DATA half: Boss Mộc Linh Đại Tinh (Mộc Tinh Cổ Thụ).
// Real 3D skeletal animated boss (assets/models/boss_moclinh.glb).
// 7 animation clips: Idle_Combat, Walk_Heavy, Action_Roar, Attack_Slam,
// Attack_Sweep, React_Stagger, Bind_TPose.

#include "boss/boss_system.h"
#include "skills/skill_manager.h"
#include "core/resource_manager.h"
#include "core/surface_material.h"
#include "environment/environment_system.h"
#include "raylib.h"
#include "raymath.h"
#include <math.h>
#include <stdio.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

#define BOSS_MOCLINH_MODEL_PATH "assets/models/boss_moclinh_mesh.glb"
#define BOSS_MOCLINH_ANIMS_PATH "assets/models/boss_moclinh_anims.glb"
#define BOSS_MOCLINH_FALLBACK "assets/models/boss_moclinh.glb"
#define BOSS_MOCLINH_SCALE 0.52f // ~5.3m tall in meter-scale arena

typedef enum
{
    MOC_ANIM_IDLE = 0,    // MocLinh_Idle_Combat (3.40s, 103 frames)
    MOC_ANIM_WALK = 1,    // MocLinh_Walk_Heavy (2.10s, 64 frames)
    MOC_ANIM_ROAR = 2,    // MocLinh_Action_Roar (3.20s, 97 frames)
    MOC_ANIM_SLAM = 3,    // MocLinh_Attack_Slam (2.60s, 79 frames)
    MOC_ANIM_SWEEP = 4,   // MocLinh_Attack_Sweep (2.20s, 67 frames)
    MOC_ANIM_STAGGER = 5, // MocLinh_React_Stagger (1.90s, 58 frames)
    MOC_ANIM_TPOSE = 6,   // MocLinh_Bind_TPose (2.00s, 61 frames)
    MOC_ANIM_COUNT = 7
} MocLinhAnimClip;

static Model s_model = {0};
static ModelAnimation *s_anims = NULL;
static int s_animCount = 0;
static bool s_loaded = false;
static bool s_loadAttempted = false;

// Playback state
static int s_currentClip = MOC_ANIM_IDLE;
static int s_currentFrame = 0;
static float s_frameTimer = 0.0f;
static bool s_oneShotActive = false;
static float s_oneShotSpeed = 1.0f;

// Spatial and transition tracking
static Vector3 s_prevPos = {0};
static float s_yaw = 0.0f;
static int s_lastPhase = -1;
static float s_lastHp = 0.0f;
static double s_lastUpdateSec = -1.0;
static float s_actionCooldown = 1.5f;

static void EnsureModelLoaded(void)
{
    if (s_loadAttempted)
        return;
    s_loadAttempted = true;

    s_model = ResourceManager_LoadModel(BOSS_MOCLINH_MODEL_PATH);
    if (s_model.meshCount == 0)
    {
        s_model = ResourceManager_LoadModel(BOSS_MOCLINH_FALLBACK);
    }
    if (s_model.meshCount == 0)
    {
        TraceLog(LOG_WARNING, "BOSS_MOCLINH: Failed to load 3D model from %s or %s",
                 BOSS_MOCLINH_MODEL_PATH, BOSS_MOCLINH_FALLBACK);
        return;
    }

    SurfaceMaterial_Apply(&s_model);

    s_anims = ResourceManager_LoadModelAnimations(BOSS_MOCLINH_ANIMS_PATH, &s_animCount);
    if (s_anims == NULL || s_animCount < 5)
    {
        s_anims = ResourceManager_LoadModelAnimations(BOSS_MOCLINH_FALLBACK, &s_animCount);
    }
    if (s_anims == NULL || s_animCount < 5)
    {
        TraceLog(LOG_WARNING, "BOSS_MOCLINH: Model loaded but insufficient animation clips (%d)", s_animCount);
        return;
    }

    s_loaded = true;
    s_currentClip = MOC_ANIM_ROAR; // Enter battle with a ferocious roar
    s_currentFrame = 0;
    s_oneShotActive = true;
    s_oneShotSpeed = 1.0f;
    TraceLog(LOG_INFO, "BOSS_MOCLINH: Successfully initialized 3D model with %d skeletal animations", s_animCount);
}

static void TriggerOneShot(int clip, float speed)
{
    if (!s_loaded || clip < 0 || clip >= s_animCount)
        return;
    s_currentClip = clip;
    s_currentFrame = 0;
    s_frameTimer = 0.0f;
    s_oneShotActive = true;
    s_oneShotSpeed = (speed > 0.1f) ? speed : 1.0f;
}

static Color GetPhaseElementColor(int elem)
{
    switch (elem)
    {
    case ELEM_WATER:
        return ELEMENT_COLOR_WATER;
    case ELEM_WOOD:
        return ELEMENT_COLOR_WOOD;
    case ELEM_FIRE:
        return ELEMENT_COLOR_FIRE;
    case ELEM_EARTH:
        return ELEMENT_COLOR_EARTH;
    case ELEM_METAL:
        return ELEMENT_COLOR_METAL;
    default:
        return ELEMENT_COLOR_TAIJI;
    }
}

static void DrawMocLinh(const Agent *self, float phaseT)
{
    EnsureModelLoaded();

    int phase = Boss_GetPhase();
    Color elemColor = GetPhaseElementColor(self->currentElement);

    // Multi-pass guard: only advance animation state once per visual frame (time delta > 0)
    double now = GetTime();
    bool shouldTick = false;
    float dt = 0.0f;
    if (s_lastUpdateSec < 0.0)
    {
        s_lastUpdateSec = now;
        shouldTick = true;
        dt = 0.016f;
    }
    else if (now > s_lastUpdateSec + 0.0001)
    {
        dt = (float)(now - s_lastUpdateSec);
        if (dt > 0.1f)
            dt = 0.016f;
        s_lastUpdateSec = now;
        shouldTick = true;
    }

    if (shouldTick)
    {
        // 1. Phase change -> roar
        if (s_lastPhase >= 0 && phase != s_lastPhase)
        {
            TriggerOneShot(MOC_ANIM_ROAR, 1.1f);
        }
        s_lastPhase = phase;

        // 2. Heavy hit reaction (stagger if lost > 40 HP in a chunk)
        if (s_lastHp > 0.0f && (s_lastHp - self->health) > 40.0f && !s_oneShotActive)
        {
            TriggerOneShot(MOC_ANIM_STAGGER, 1.2f);
        }
        s_lastHp = self->health;

        // 3. Movement and facing detection
        float dx = self->position.x - s_prevPos.x;
        float dz = self->position.z - s_prevPos.z;
        float moveDist = sqrtf(dx * dx + dz * dz);
        bool isMoving = (moveDist > 0.015f * dt * 60.0f);

        // Find nearest opponent to face
        float targetYaw = s_yaw;
        AgentTeam oppTeam = (self->team == TEAM_ALLY) ? TEAM_ENEMY : TEAM_ALLY;
        int targetIds[4];
        int nTargets = Entity_GetNearbyTargetsTeam(self->position, 30.0f, oppTeam, targetIds, 4);
        if (nTargets > 0)
        {
            const Agent *target = Entity_GetAgent(targetIds[0]);
            if (target)
            {
                float tx = target->position.x - self->position.x;
                float tz = target->position.z - self->position.z;
                if (tx * tx + tz * tz > 0.01f)
                {
                    targetYaw = atan2f(tx, tz);
                }
            }
        }
        else if (isMoving)
        {
            targetYaw = atan2f(dx, dz);
        }

        // Smooth rotation toward target yaw
        float yawDiff = targetYaw - s_yaw;
        while (yawDiff > PI)
            yawDiff -= 2.0f * PI;
        while (yawDiff < -PI)
            yawDiff += 2.0f * PI;
        s_yaw += yawDiff * fminf(1.0f, dt * 6.0f);

        // 4. Periodic attack swings on cadence
        s_actionCooldown -= dt;
        if (!s_oneShotActive && s_actionCooldown <= 0.0f)
        {
            s_actionCooldown = 3.2f;
            int attackClip = (GetRandomValue(0, 1) == 0) ? MOC_ANIM_SLAM : MOC_ANIM_SWEEP;
            TriggerOneShot(attackClip, 1.0f);
        }

        // 5. Advance animation frame
        if (s_loaded && s_currentClip >= 0 && s_currentClip < s_animCount)
        {
            float animFps = 30.0f * (s_oneShotActive ? s_oneShotSpeed : 1.0f);
            s_frameTimer += dt * animFps;
            int totalFrames = s_anims[s_currentClip].keyframeCount;
            while (s_frameTimer >= 1.0f)
            {
                s_frameTimer -= 1.0f;
                s_currentFrame++;
                if (s_currentFrame >= totalFrames)
                {
                    if (s_oneShotActive)
                    {
                        s_oneShotActive = false;
                        s_currentClip = isMoving ? MOC_ANIM_WALK : MOC_ANIM_IDLE;
                        s_currentFrame = 0;
                    }
                    else
                    {
                        s_currentFrame = 0; // loop
                    }
                }
            }
        }

        // 6. Update locomotion when idle/walking
        if (!s_oneShotActive)
        {
            int desiredClip = isMoving ? MOC_ANIM_WALK : MOC_ANIM_IDLE;
            if (s_currentClip != desiredClip)
            {
                s_currentClip = desiredClip;
                s_currentFrame = 0;
                s_frameTimer = 0.0f;
            }
        }

        s_prevPos = self->position;
    }

    // --- RENDER VISUAL ---

    // 1. Smart Ground Shadow
    Environment_DrawSmartShadow(self->position, ENV_SHAPE_SPHERE, 1.8f * BOSS_MOCLINH_SCALE, 2.5f * BOSS_MOCLINH_SCALE);

    // 2. Ancient Wood Sigil / Rune Circle (No-Tutorial Visual Cue for Phase Element)
    Vector3 groundPos = {self->position.x, self->position.y + 0.04f, self->position.z};
    float ringR = 2.4f * BOSS_MOCLINH_SCALE;
    float pulse = 1.0f + 0.06f * sinf(phaseT * 2.5f);
    DrawCircle3D(groundPos, ringR * pulse, (Vector3){1, 0, 0}, 90.0f, elemColor);
    DrawCircle3D(groundPos, ringR * 0.7f, (Vector3){0, 1, 0}, phaseT * 40.0f,
                 (Color){(unsigned char)(elemColor.r / 2), (unsigned char)(elemColor.g / 2), (unsigned char)(elemColor.b / 2), 160});

    // 3. Draw 3D Animated Skeletal Mesh or Fallback
    if (s_loaded && s_currentClip >= 0 && s_currentClip < s_animCount)
    {
        if (IsModelAnimationValid(s_model, s_anims[s_currentClip]))
        {
            UpdateModelAnimation(s_model, s_anims[s_currentClip], s_currentFrame);
        }
        float yawDeg = s_yaw * RAD2DEG;
        DrawModelEx(s_model, self->position, (Vector3){0.0f, 1.0f, 0.0f}, yawDeg,
                    (Vector3){BOSS_MOCLINH_SCALE, BOSS_MOCLINH_SCALE, BOSS_MOCLINH_SCALE}, WHITE);
    }
    else
    {
        // Procedural Treant Fallback if asset missing
        float bob = 1.2f + 0.15f * sinf(phaseT * 1.5f);
        Vector3 core = {self->position.x, self->position.y + bob, self->position.z};
        DrawSphereEx(core, 1.1f, 14, 14, (Color){35, 75, 40, 255});
        DrawCube((Vector3){core.x, core.y - 0.7f, core.z}, 0.8f, 1.4f, 0.8f, (Color){60, 42, 25, 255});
    }

    // 4. Emerald Heartwood Core Glow
    Vector3 chestPos = {self->position.x, self->position.y + 2.4f * BOSS_MOCLINH_SCALE, self->position.z};
    float chestGlow = 0.35f + 0.08f * sinf(phaseT * 3.2f);
    DrawSphereEx(chestPos, chestGlow, 8, 8, (Color){120, 255, 140, 180});
}

const BossDef BOSS_MOC_LINH = {
    .name = "MOC_LINH_DAI_TINH",
    .maxHealth = 600.0f,
    // Phase 0: 100% -> 75% -> 50% -> 25%
    .phaseHpThresholds = {1.0f, 0.75f, 0.50f, 0.25f},
    // Elements: Mộc (Wood) -> Thổ (Earth - Mộc khắc Thổ) -> Thủy (Water - Thủy sinh Mộc) -> Mộc (Enraged)
    .phaseElements = {ELEM_WOOD, ELEM_EARTH, ELEM_WATER, ELEM_WOOD},
    .phaseSkillNames = {"LEAF_WHIRLWIND", "STONE_PRISON", "GLACIAL_CANNON", "LEAF_WHIRLWIND"},
    .castIntervalSeconds = 3.5f,
    .drawVisual = DrawMocLinh,
};
