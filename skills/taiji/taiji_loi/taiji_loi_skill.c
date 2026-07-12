// skills/taiji/taiji_loi/taiji_loi_skill.c
#include "taiji_loi_skill.h"
#include "skills/taiji/taiji_phong/taiji_phong_skill.h"
#include "entities/entities.h"
#include "core/composition/visual_composer.h"
#include <stddef.h>

// Meter-scaled. LOI_MANA_COST is deliberately brutal (2 casts drain a full
// default 100 pool → Entity_Update exits Thái Cực) — thiết kế §XVII "Vô Sát".
static const float LOI_RADIUS       = 3.0f;
static const float LOI_DAMAGE       = 35.0f;
static const float LOI_KNOCKBACK    = 5.0f;
static const float LOI_MANA_COST    = 45.0f;
static const float LOI_SKY_HEIGHT   = 14.0f;

static int s_skillIndex = -1;

void InitTaijiLoiSkill(int screenWidth, int screenHeight) {
    (void)screenWidth; (void)screenHeight;
    s_skillIndex = Skill_GetIndexByName("TAIJI_LOI");
    if (s_skillIndex >= 0) RegisterSkillManaCost(s_skillIndex, LOI_MANA_COST);
}

void CastTaijiLoiSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    (void)startPos; (void)params;
    if (!Entity_IsTaijiActive(agentId)) return; // Thái Cực exclusive

    // Giáng vào tâm Phong nếu đang mở — the gather→strike combo.
    Vector3 ground;
    if (!TaijiPhong_GetActiveCenter(&ground)) {
        ground = (Vector3){ target.x, 0.0f, target.z };
    }

    // Damage through the team-aware AoE path.
    const Agent *caster = Entity_GetAgent(agentId);
    AgentTeam team = caster ? caster->team : TEAM_NEUTRAL;
    Entity_ApplyAoEDamage(ground, LOI_RADIUS, LOI_DAMAGE, LOI_KNOCKBACK, team);

    // Sky→ground violet-white bolt + impact burst (composition layer owns
    // draw/lifetime — fire-and-forget, monochrome shader will grayscale it).
    Vector3 sky = { ground.x, LOI_SKY_HEIGHT, ground.z };
    VFX_SpawnProcBeam(sky, ground, EFFECT_PRESET_LIGHTNING_IMPACT, 0.18f, 0.35f);
    VFX_ComposeImpact(ground, EFFECT_PRESET_LIGHTNING_IMPACT, 1.2f);
    VFX_ComposeShockwaveRing(ground, LOI_RADIUS, 0.4f, (Color){ 235, 225, 255, 255 });
}

void UpdateTaijiLoiSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    (void)dt; (void)enemyPos; (void)enemyRadius;
    // Stateless — the composition layer animates the bolt/impact.
}

void DrawTaijiLoiSkill(void) {
    // Nothing owned here — VFX_Compose* draws via the composition pass.
}

void UnloadTaijiLoiSkill(void) {
}
