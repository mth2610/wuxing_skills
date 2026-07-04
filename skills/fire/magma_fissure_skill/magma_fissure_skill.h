#ifndef SKILL_MAGMA_FISSURE_H
#define SKILL_MAGMA_FISSURE_H

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
void InitMagmaFissureSkill(int screenWidth, int screenHeight);
void CastMagmaFissureSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateMagmaFissureSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawMagmaFissureSkill(void);
void UnloadMagmaFissureSkill(void);

// Engine <-> Skill communication
bool IsMagmaFissureSkillCoiling(void);
int GetMagmaFissureSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateMagmaFissureProjectile(int index);

#endif // SKILL_MAGMA_FISSURE_H
