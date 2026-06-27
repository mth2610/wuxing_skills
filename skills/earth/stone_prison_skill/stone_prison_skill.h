#ifndef STONE_PRISON_SKILL_H
#define STONE_PRISON_SKILL_H

#include "raylib.h"
#include "core/skill_manager.h"

#ifndef SKILL_PROJECTILE_DEF
#define SKILL_PROJECTILE_DEF
typedef struct {
    Vector3 position;
    float radius;
    bool active;
} SkillProjectile;
#endif

// Standard Wuxing lifecycle API
void InitStonePrisonSkill(int screenWidth, int screenHeight);
void CastStonePrisonSkill(Vector3 startPos, Vector3 target, SkillParams params);
void UpdateStonePrisonSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawStonePrisonSkill(void);
void UnloadStonePrisonSkill(void);

// Callouts
bool IsStonePrisonSkillActive(void);
int GetStonePrisonSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateStonePrisonProjectile(int index);

#endif // STONE_PRISON_SKILL_H
