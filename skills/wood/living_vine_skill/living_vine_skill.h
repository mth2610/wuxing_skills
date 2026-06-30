#ifndef LIVING_VINE_SKILL_H
#define LIVING_VINE_SKILL_H

#include "raylib.h"
#include "core/skill_manager.h"   // SkillParams

#ifndef SKILL_PROJECTILE_DEF
#define SKILL_PROJECTILE_DEF
typedef struct { Vector3 position; float radius; bool active; } SkillProjectile;
#endif

// Living Vine — a massive enchanted vine erupts from the ground, grows organically
// toward the target, coils around it, then dissolves back into leaves and roots.
void InitLivingVineSkill(int screenWidth, int screenHeight);
void CastLivingVineSkill(Vector3 startPos, Vector3 target, SkillParams params);
void UpdateLivingVineSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawLivingVineSkill(void);
void UnloadLivingVineSkill(void);
bool IsLivingVineSkillCoiling(void);
int  GetLivingVineSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateLivingVineProjectile(int index);

#endif // LIVING_VINE_SKILL_H
