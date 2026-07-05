#ifndef SKILL_GLACIAL_CANNON_H
#define SKILL_GLACIAL_CANNON_H

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

// Main lifecycle
void InitGlacialCannonSkill(int screenWidth, int screenHeight);
void CastGlacialCannonSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateGlacialCannonSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawGlacialCannonSkill(void);
void UnloadGlacialCannonSkill(void);

// Engine <-> Skill communication
bool IsGlacialCannonSkillCoiling(void);
int GetGlacialCannonSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateGlacialCannonProjectile(int index);

#endif // SKILL_GLACIAL_CANNON_H
