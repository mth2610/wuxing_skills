#ifndef EARTH_SHATTER_SKILL_H
#define EARTH_SHATTER_SKILL_H

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

// Lifecycle
void InitEarthShatterSkill(int screenWidth, int screenHeight);
void CastEarthShatterSkill(Vector3 startPos, Vector3 target, SkillParams params);
void UpdateEarthShatterSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawEarthShatterSkill(void);
void UnloadEarthShatterSkill(void);

// Callouts
bool IsEarthShatterSkillActive(void);
int GetEarthShatterSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateEarthShatterProjectile(int index);

#endif // EARTH_SHATTER_SKILL_H
