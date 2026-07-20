#ifndef SKILL_FLAME_FUNNEL_H
#define SKILL_FLAME_FUNNEL_H

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
void InitFlameFunnelSkill(int screenWidth, int screenHeight);
void CastFlameFunnelSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateFlameFunnelSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawFlameFunnelSkill(void);
void UnloadFlameFunnelSkill(void);

// Engine <-> Skill communication
bool IsFlameFunnelSkillCoiling(void);
int GetFlameFunnelSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateFlameFunnelProjectile(int index);

#endif // SKILL_FLAME_FUNNEL_H
