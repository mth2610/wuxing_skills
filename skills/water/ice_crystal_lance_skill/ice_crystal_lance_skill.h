#ifndef ICE_CRYSTAL_LANCE_SKILL_H
#define ICE_CRYSTAL_LANCE_SKILL_H

#include "raylib.h"
#include "skills/skill_manager.h"

#ifndef SKILL_PROJECTILE_DEF
#define SKILL_PROJECTILE_DEF
typedef struct {
    Vector3 position;
    float radius;
    bool active;
} SkillProjectile;
#endif

void InitIceCrystalLanceSkill(int screenWidth, int screenHeight);
void CastIceCrystalLanceSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateIceCrystalLanceSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawIceCrystalLanceSkill(void);
void UnloadIceCrystalLanceSkill(void);

bool IsIceCrystalLanceSkillCoiling(void);
int GetIceCrystalLanceSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateIceCrystalLanceProjectile(int index);

#endif // ICE_CRYSTAL_LANCE_SKILL_H
