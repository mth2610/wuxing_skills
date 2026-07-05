#ifndef SKILL_BOULDER_BARRAGE_H
#define SKILL_BOULDER_BARRAGE_H

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
void InitBoulderBarrageSkill(int screenWidth, int screenHeight);
void CastBoulderBarrageSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateBoulderBarrageSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawBoulderBarrageSkill(void);
void UnloadBoulderBarrageSkill(void);

// Engine <-> Skill communication
bool IsBoulderBarrageSkillCoiling(void);
int GetBoulderBarrageSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateBoulderBarrageProjectile(int index);

#endif // SKILL_BOULDER_BARRAGE_H
