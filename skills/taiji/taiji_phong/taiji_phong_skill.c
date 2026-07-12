// skills/taiji/taiji_phong/taiji_phong_skill.c
#include "taiji_phong_skill.h"
#include "entities/entities.h"
#include "combat/combat.h"
#include "rlgl.h"
#include <math.h>
#include <stddef.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

// Meter-scaled (1 unit = 1m) per root CLAUDE.md.
static const float PHONG_RADIUS       = 5.0f;
static const float PHONG_DURATION     = 3.0f;
static const float PHONG_PULL_MPS     = 4.0f;
static const float PHONG_MANA_COST    = 30.0f;

static bool    s_active = false;
static Vector3 s_center = { 0 };
static int     s_casterId = -1;
static float   s_timer = 0.0f;
static float   s_visT = 0.0f; // visual clock (runs while active)
static int     s_skillIndex = -1;

void InitTaijiPhongSkill(int screenWidth, int screenHeight) {
    (void)screenWidth; (void)screenHeight;
    s_active = false;
    s_skillIndex = Skill_GetIndexByName("TAIJI_PHONG");
    if (s_skillIndex >= 0) RegisterSkillManaCost(s_skillIndex, PHONG_MANA_COST);
}

void CastTaijiPhongSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    (void)startPos; (void)params;
    // Thái Cực exclusive — control/ gates this too; double-guard for boss/
    // sandbox callers.
    if (!Entity_IsTaijiActive(agentId)) return;
    s_active = true;
    s_center = (Vector3){ target.x, 0.0f, target.z };
    s_casterId = agentId;
    s_timer = PHONG_DURATION;
    s_visT = 0.0f;
}

void UpdateTaijiPhongSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    (void)enemyPos; (void)enemyRadius;
    if (!s_active) return;

    s_timer -= dt;
    s_visT += dt;
    if (s_timer <= 0.0f) { s_active = false; return; }

    // Suck in every opposing-team agent (Tứ lạng bạt thiên cân: gather, not
    // damage — the follow-up Lôi is the payoff).
    const Agent *caster = Entity_GetAgent(s_casterId);
    AgentTeam pullTeam = (caster && caster->team == TEAM_ALLY) ? TEAM_ENEMY : TEAM_ALLY;
    int ids[32];
    int n = Entity_GetNearbyTargetsTeam(s_center, PHONG_RADIUS, pullTeam, ids, 32);
    for (int i = 0; i < n; i++) {
        Entity_ApplyPull(ids[i], s_center, PHONG_PULL_MPS, 0.1f);
    }

    // Deflect enemy projectiles submitted this frame (immediate-mode
    // registry — call between skill submissions and Combat_Update).
    Combat_DeflectProjectilesInRadius(s_center, PHONG_RADIUS, s_casterId);
}

void DrawTaijiPhongSkill(void) {
    if (!s_active) return;
    // Monochrome-safe swirl: white rings spiraling inward (the whole screen
    // is black-and-white during Thái Cực, so shape > color).
    float fade = (s_timer < 0.5f) ? s_timer / 0.5f : 1.0f;
    for (int i = 0; i < 4; i++) {
        float t = s_visT * (1.2f + 0.3f * i) + i * (PI * 0.5f);
        float r = PHONG_RADIUS * (0.25f + 0.75f * (1.0f - fmodf(t * 0.25f, 1.0f)));
        Vector3 p = { s_center.x, 0.15f + 0.3f * i, s_center.z };
        unsigned char a = (unsigned char)(255 * fade);
        DrawCircle3D(p, r, (Vector3){ sinf(t * 0.3f), 1.0f, cosf(t * 0.3f) },
                     90.0f + t * 30.0f, (Color){ 240, 240, 250, a });
    }
    DrawCircle3D((Vector3){ s_center.x, 0.03f, s_center.z }, PHONG_RADIUS,
                 (Vector3){ 1, 0, 0 }, 90.0f, (Color){ 200, 200, 220, 255 });
}

void UnloadTaijiPhongSkill(void) {
    s_active = false;
}

bool TaijiPhong_GetActiveCenter(Vector3 *outCenter) {
    if (!s_active || outCenter == NULL) return false;
    *outCenter = s_center;
    return true;
}
