#ifndef SKILL_STONE_SPEAR_H
#define SKILL_STONE_SPEAR_H
#include "raylib.h"
#include "core/skill_manager.h" // For SkillParams and SkillCategory structs

#ifndef SKILL_PROJECTILE_DEF
#define SKILL_PROJECTILE_DEF
typedef struct {
    Vector3 position;
    float radius;
    bool active;
} SkillProjectile;
#endif

// Lifecycle functions (Skill Prefix: "StoneSpear")
void InitStoneSpearSkill(int screenWidth, int screenHeight);
void CastStoneSpearSkill(Vector3 startPos, Vector3 target, SkillParams params);
void UpdateStoneSpearSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawStoneSpearSkill(void);
void UnloadStoneSpearSkill(void);

// For Engine-to-Skill communication (decoupled from direct enemy entity references):
bool IsStoneSpearSkillCoiling(void); // Stone Spear never pins/roots the enemy -> always returns false
int GetStoneSpearSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateStoneSpearProjectile(int index);
#endif
