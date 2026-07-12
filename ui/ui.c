// ui/ui.c — Module 9: minimal HUD + auto-targeting.
#include "ui/ui.h"
#include "entities/entities.h"
#include "combat/combat.h"
#include "boss/boss_system.h"
#include "core/skill_manager.h" // slot names + CanCast shading + element colors
#include <math.h>
#include <stddef.h>

// Meter-scaled search ranges.
static const float AIM_PROJECTILE_RANGE = 12.0f; // đối-đòn detection radius
static const float AIM_BOSS_RANGE       = 30.0f;

static const Camera3D *s_camera = NULL;
// Cached per-frame result so DrawOverlay reuses UI_Update's computation.
static bool    s_hasTarget = false;
static Vector3 s_aimPoint = { 0 };
static int     s_aimAgentId = -1;

void UI_Init(void) {
    s_camera = NULL;
    s_hasTarget = false;
    s_aimAgentId = -1;
}

void UI_SetCamera(const Camera3D *camera) {
    s_camera = camera;
}

Vector3 UI_GetAutoAimPoint(int agentId, bool *hasTarget) {
    if (hasTarget) *hasTarget = false;
    const Agent *self = Entity_GetAgent(agentId);
    if (self == NULL) return (Vector3){ 0 };

    // Priority 1: nearest ENEMY projectile in flight — aiming at it lines up
    // the đối-đòn cast that triggers Đấu Pháp in combat/.
    CombatProjectileInfo projs[MAX_COMBAT_PROJECTILES];
    int n = Combat_QueryProjectiles(projs, MAX_COMBAT_PROJECTILES);
    int bestProj = -1;
    float bestSq = AIM_PROJECTILE_RANGE * AIM_PROJECTILE_RANGE;
    for (int i = 0; i < n; i++) {
        if (projs[i].team == self->team) continue;
        float dx = projs[i].pos.x - self->position.x;
        float dz = projs[i].pos.z - self->position.z;
        float dSq = dx * dx + dz * dz;
        if (dSq < bestSq) { bestSq = dSq; bestProj = i; }
    }
    if (bestProj >= 0) {
        if (hasTarget) *hasTarget = true;
        return projs[bestProj].pos;
    }

    // Priority 2: the opposing boss's core.
    if (Boss_IsAlive()) {
        const Agent *boss = Entity_GetAgent(Boss_GetAgentId());
        if (boss && boss->team != self->team) {
            float dx = boss->position.x - self->position.x;
            float dz = boss->position.z - self->position.z;
            if (dx * dx + dz * dz <= AIM_BOSS_RANGE * AIM_BOSS_RANGE) {
                if (hasTarget) *hasTarget = true;
                return boss->position;
            }
        }
    }

    return (Vector3){ 0 };
}

void UI_Update(float dt) {
    (void)dt;
    s_hasTarget = false;
    if (s_aimAgentId >= 0) {
        s_aimPoint = UI_GetAutoAimPoint(s_aimAgentId, &s_hasTarget);
    }
}

void UI_DrawOverlay(int agentId) {
    s_aimAgentId = agentId; // remembered for UI_Update's cached recompute
    const Agent *self = Entity_GetAgent(agentId);
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    // --- Skill slot chips (keys 1-4), cooldown-shaded. No Tutorial: the
    // only text is the key number + the skill's registered name. ---
    if (self != NULL) {
        const int chipW = 92, chipH = 34, gap = 8;
        int x0 = sw / 2 - (chipW * AGENT_SKILL_SLOTS + gap * (AGENT_SKILL_SLOTS - 1)) / 2;
        int y = sh - chipH - 14;
        for (int slot = 0; slot < AGENT_SKILL_SLOTS; slot++) {
            int x = x0 + slot * (chipW + gap);
            int skillIdx = self->equippedSkills[slot];
            bool ready = (skillIdx >= 0) && SkillManager_CanCast(skillIdx, agentId);
            Color back = ready ? (Color){ 35, 35, 45, 230 } : (Color){ 18, 18, 22, 230 };
            DrawRectangle(x, y, chipW, chipH, back);
            DrawRectangleLines(x, y, chipW, chipH,
                               ready ? (Color){ 220, 220, 230, 200 } : (Color){ 90, 90, 100, 160 });
            DrawText(TextFormat("%d", slot + 1), x + 6, y + 4, 10, (Color){ 160, 160, 170, 255 });
            if (skillIdx >= 0) {
                Color c = GetRegisteredSkillColor(skillIdx);
                if (!ready) { c.r /= 2; c.g /= 2; c.b /= 2; }
                DrawText(GetRegisteredSkillName(skillIdx), x + 8, y + 16, 10, c);
            }
        }
    }

    // --- Auto-aim reticle: diamond over the current target. ---
    if (s_hasTarget && s_camera != NULL) {
        Vector2 p = GetWorldToScreen((Vector3){ s_aimPoint.x, s_aimPoint.y + 0.4f, s_aimPoint.z }, *s_camera);
        if (p.x > -50 && p.x < sw + 50 && p.y > -50 && p.y < sh + 50) {
            float r = 10.0f;
            Color c = (Color){ 240, 230, 150, 255 };
            DrawLineV((Vector2){ p.x, p.y - r }, (Vector2){ p.x + r, p.y }, c);
            DrawLineV((Vector2){ p.x + r, p.y }, (Vector2){ p.x, p.y + r }, c);
            DrawLineV((Vector2){ p.x, p.y + r }, (Vector2){ p.x - r, p.y }, c);
            DrawLineV((Vector2){ p.x - r, p.y }, (Vector2){ p.x, p.y - r }, c);
        }
    }
}
