#ifndef EARTH_PILLAR_PRISON_SKILL_H
#define EARTH_PILLAR_PRISON_SKILL_H

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

void InitEarthPillarPrisonSkill(int screenWidth, int screenHeight);
void CastEarthPillarPrisonSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateEarthPillarPrisonSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawEarthPillarPrisonSkill(void);
void UnloadEarthPillarPrisonSkill(void);

bool IsEarthPillarPrisonSkillCoiling(void);
int GetEarthPillarPrisonSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateEarthPillarPrisonProjectile(int index);

#endif // EARTH_PILLAR_PRISON_SKILL_H
