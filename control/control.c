// control/control.c
#include "control/control.h"
#include "entities/entities.h"
#include "skills/skill_manager.h"
#include "maps/map_manager.h"
#include <math.h>
#include <stddef.h>

// Brisk walk, real-world-scaled (1 unit = 1 meter) — same value the interim
// game_screen controller used.
static const float RUN_SPEED_MPS  = 6.4f;
static const float WALK_SPEED_MPS = 3.0f;
static const float JUMP_FORCE     = 4.6f;
static const float DASH_SPEED_MPS = 10.0f;
// Fallback cooldown when a skill exposes no per-category params here yet —
// keeps 1-4 from being spammable until game/ wires real per-skill data.
static const float DEFAULT_CAST_COOLDOWN = 1.0f;

static int   s_agentId = -1;
static float s_camYaw  = 0.0f;
static const Camera3D *s_camera = NULL;
static float s_yaw = 0.0f; // facing, radians
static float s_cdMult = 1.0f; // zone-rule cooldown multiplier (game/ sets)
static int   s_castFired = -1; // skillIndex of the cast that fired this frame
static int   s_jumpFired = 0;  // 1 = first jump, 2 = mid-air khinh công jump, 0 = none
static float s_moveMult = 1.0f; // swing damping (game/ sets per frame)
static ControlSkillLevelFn s_skillLevelFn = NULL;
static ControlSkillScalingFn s_skillScalingFn = NULL;

void Control_Init(int agentId) {
    s_agentId = agentId;
    s_yaw = 0.0f;
}

int Control_GetAgentId(void) {
    return s_agentId;
}

void Control_SetCamera(float yawRadians, const Camera3D *camera) {
    s_camYaw = yawRadians;
    s_camera = camera;
}

PlayerIntent Control_ReadIntent(void) {
    PlayerIntent intent = { 0 };
    intent.castSkillSlot = -1;

    float s = sinf(s_camYaw);
    float c = cosf(s_camYaw);
    if (IsKeyDown(KEY_W)) { intent.moveDir.x -= s; intent.moveDir.y -= c; }
    if (IsKeyDown(KEY_S)) { intent.moveDir.x += s; intent.moveDir.y += c; }
    if (IsKeyDown(KEY_A)) { intent.moveDir.x -= c; intent.moveDir.y += s; }
    if (IsKeyDown(KEY_D)) { intent.moveDir.x += c; intent.moveDir.y -= s; }

    intent.jump     = IsKeyPressed(KEY_SPACE);
    intent.dash     = IsKeyPressed(KEY_LEFT_SHIFT);
    intent.meditate = IsKeyPressed(KEY_G);

    if      (IsKeyPressed(KEY_ONE))   intent.castSkillSlot = 0;
    else if (IsKeyPressed(KEY_TWO))   intent.castSkillSlot = 1;
    else if (IsKeyPressed(KEY_THREE)) intent.castSkillSlot = 2;
    else if (IsKeyPressed(KEY_FOUR))  intent.castSkillSlot = 3;
    else if (IsKeyPressed(KEY_FIVE))  intent.castSkillSlot = 4;

    // Basic attack: random đấm/đá/chưởng per press (matches the pre-intent
    // behavior; a real combo system picks deliberately later).
    if (IsKeyPressed(KEY_Z) || IsKeyPressed(KEY_C) ||
        IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        intent.basicAttack = 1 + GetRandomValue(0, 2);
    }

    // Aim: mouse ray onto the Y=0 ground plane when a camera is wired,
    // otherwise 3m ahead of the agent's facing.
    const Agent *a = Entity_GetAgent(s_agentId);
    if (s_camera != NULL) {
        Ray ray = GetScreenToWorldRay(GetMousePosition(), *s_camera);
        if (fabsf(ray.direction.y) > 0.0001f) {
            float t = -ray.position.y / ray.direction.y;
            intent.aimPoint = (Vector3){ ray.position.x + ray.direction.x * t, 0.0f,
                                         ray.position.z + ray.direction.z * t };
        }
    } else if (a != NULL) {
        intent.aimPoint = (Vector3){ a->position.x + sinf(s_yaw) * 3.0f, 0.0f,
                                     a->position.z + cosf(s_yaw) * 3.0f };
    }

    return intent;
}

void Control_Apply(const PlayerIntent *in, float dt) {
    if (in == NULL || s_agentId < 0) return;
    const Agent *a = Entity_GetAgent(s_agentId);
    if (a == NULL) return;

    // Movement — skipped while entities owns the position (airborne arc,
    // stun/pull, dash burst); Entity_SetPosition cancels meditate on a real
    // move, which is exactly the Thiền Định break rule.
    float len = sqrtf(in->moveDir.x * in->moveDir.x + in->moveDir.y * in->moveDir.y);
    bool canRun = !a->staminaExhausted && a->stamina > 0.0f;
    float baseMoveSpeed = canRun ? RUN_SPEED_MPS : WALK_SPEED_MPS;
    if (len > 0.0001f && a->vState == AGENT_GROUNDED &&
        !Entity_IsCrowdControlled(s_agentId) && a->dashTimer <= 0.0f) {
        float speed = baseMoveSpeed * Entity_GetSpeedMult(s_agentId) * s_moveMult;
        Vector3 pos = a->position;
        pos.x += (in->moveDir.x / len) * speed * dt;
        pos.z += (in->moveDir.y / len) * speed * dt;
        pos.y = MapManager_GetGroundHeightAt(pos.x, pos.z);
        Entity_SetPosition(s_agentId, pos);
        Entity_SetRunning(s_agentId, canRun);
        s_yaw = atan2f(in->moveDir.x, in->moveDir.y);
    }

    if (in->jump) {
        // Khinh công supports 2 jumps (ground + mid-air double jump), carrying
        // current horizontal intent as velocity.
        Vector3 hv = { 0 };
        if (len > 0.0001f) {
            float speed = baseMoveSpeed * Entity_GetSpeedMult(s_agentId);
            hv.x = (in->moveDir.x / len) * speed;
            hv.z = (in->moveDir.y / len) * speed;
            s_yaw = atan2f(in->moveDir.x, in->moveDir.y);
        } else if (a->vState == AGENT_JUMPING) {
            hv.x = a->velocity.x;
            hv.z = a->velocity.z;
        }
        float vForce = (a->vState == AGENT_JUMPING) ? (JUMP_FORCE * 1.05f) : JUMP_FORCE;
        if (Entity_PerformJump(s_agentId, vForce, hv)) {
            const Agent *updated = Entity_GetAgent(s_agentId);
            s_jumpFired = updated ? updated->airJumpsUsed : 1;
        }
    }

    if (in->dash) {
        // Dash along the move direction, or facing when standing still.
        Vector3 dir = (len > 0.0001f)
            ? (Vector3){ in->moveDir.x, 0.0f, in->moveDir.y }
            : (Vector3){ sinf(s_yaw), 0.0f, cosf(s_yaw) };
        Entity_Dash(s_agentId, dir, DASH_SPEED_MPS); // cooldown-gated inside
    }

    if (in->meditate) Entity_StartMeditate(s_agentId);

    if (in->castSkillSlot >= 0 && in->castSkillSlot < AGENT_SKILL_SLOTS &&
        !Entity_IsStunned(s_agentId)) {
        int skillIndex = a->equippedSkills[in->castSkillSlot];
        if (skillIndex >= 0 && SkillManager_CanCast(skillIndex, s_agentId)) {
            int lvl = s_skillLevelFn ? s_skillLevelFn(skillIndex) : 1;
            int milestone = 0;
            int quantity = 1;
            float sizeScale = 1.0f;
            float dmgMult = 1.0f;
            float skillCdMult = 1.0f;
            if (s_skillScalingFn) {
                s_skillScalingFn(skillIndex, &lvl, &milestone, &quantity, &sizeScale, &dmgMult, &skillCdMult);
            } else {
                if (lvl < 1) lvl = 1;
                if (lvl > 10) lvl = 10;
                if (lvl > 1) dmgMult = 1.0f + 0.18f * (float)(lvl - 1);
            }
            SkillParams params = {
                .level = lvl,
                .milestone = milestone,
                .quantity = quantity,
                .sizeScale = sizeScale
            };
            Entity_SetLastCast(s_agentId, skillIndex, dmgMult);
            // CastSkill returns false when the mana gate (Entity_TrySpendMana
            // inside skill_manager) or bounds check rejects it — only a real
            // cast starts the cooldown.
            if (CastSkill(skillIndex, s_agentId, a->position, in->aimPoint, params)) {
                SkillManager_TriggerCooldown(skillIndex, s_agentId,
                                             DEFAULT_CAST_COOLDOWN * s_cdMult * skillCdMult);
                // Face the cast direction — the character turning toward its
                // target is the read that a skill actually fired.
                float fdx = in->aimPoint.x - a->position.x;
                float fdz = in->aimPoint.z - a->position.z;
                if (fdx * fdx + fdz * fdz > 0.0001f) s_yaw = atan2f(fdx, fdz);
                s_castFired = skillIndex; // render side plays the cast anim
            }
        }
    }
}

float Control_GetYaw(void) {
    return s_yaw;
}

void Control_SetCastCooldownMult(float mult) {
    s_cdMult = (mult > 0.0f) ? mult : 1.0f;
}

int Control_ConsumeCastFired(void) {
    int idx = s_castFired;
    s_castFired = -1;
    return idx;
}

int Control_ConsumeJumpFired(void) {
    int j = s_jumpFired;
    s_jumpFired = 0;
    return j;
}

void Control_SetMoveSpeedMult(float mult) {
    s_moveMult = (mult > 0.0f) ? mult : 1.0f;
}

void Control_SetSkillLevelProvider(ControlSkillLevelFn fn) {
    s_skillLevelFn = fn;
}

void Control_SetSkillScalingProvider(ControlSkillScalingFn fn) {
    s_skillScalingFn = fn;
}

void Control_FaceTowards(Vector3 point) {
    const Agent *a = Entity_GetAgent(s_agentId);
    if (a == NULL) return;
    float dx = point.x - a->position.x;
    float dz = point.z - a->position.z;
    if (dx * dx + dz * dz > 0.0001f) s_yaw = atan2f(dx, dz);
}

void Control_UpdateFacingFromIntent(const PlayerIntent *in) {
    if (!in) return;
    float lenSq = in->moveDir.x * in->moveDir.x + in->moveDir.y * in->moveDir.y;
    if (lenSq > 0.0001f) {
        s_yaw = atan2f(in->moveDir.x, in->moveDir.y);
    }
    if (in->castSkillSlot >= 0) {
        Control_FaceTowards(in->aimPoint);
    }
}
