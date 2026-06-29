#ifndef SKILL_CLOUD_DRAGON_H
#define SKILL_CLOUD_DRAGON_H

#include "raylib.h"
#include "core/skill_manager.h"

// WARNING: When calling Skill_CalculateDamage or Skill_CalculateKnockback,
// ONLY use exact enum values from core/skill_manager.h (e.g. SKILL_CAT_AOE_CONTROL).

#ifndef SKILL_PROJECTILE_DEF
#define SKILL_PROJECTILE_DEF
typedef struct { Vector3 position; float radius; bool active; } SkillProjectile;
#endif

void InitCloudDragonSkill(int w, int h);
void CastCloudDragonSkill(Vector3 start, Vector3 target, SkillParams p);
void UpdateCloudDragonSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawCloudDragonSkill(void);
void UnloadCloudDragonSkill(void);
bool IsCloudDragonSkillCoiling(void);
int  GetCloudDragonSkillProjectiles(SkillProjectile *out, int max);
void DeactivateCloudDragonProjectile(int index);

#endif // SKILL_CLOUD_DRAGON_H
