#ifndef EARTH_DISC_VOLLEY_SKILL_H
#define EARTH_DISC_VOLLEY_SKILL_H

#include "raylib.h"
#include "skills/skill_manager.h"

#include <stdbool.h>

#ifndef SKILL_PROJECTILE_DEF
#define SKILL_PROJECTILE_DEF
typedef struct {
    Vector3 position;
    float radius;
    bool active;
} SkillProjectile;
#endif

void InitEarthDiscVolleySkill(int screenWidth, int screenHeight);
void CastEarthDiscVolleySkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateEarthDiscVolleySkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawEarthDiscVolleySkill(void);
void UnloadEarthDiscVolleySkill(void);

bool IsEarthDiscVolleySkillCoiling(void);
int  GetEarthDiscVolleySkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateEarthDiscVolleyProjectile(int index);

#endif // EARTH_DISC_VOLLEY_SKILL_H
