#ifndef EMERALD_THORNS_SKILL_H
#define EMERALD_THORNS_SKILL_H

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

void InitEmeraldThornsSkill(int screenWidth, int screenHeight);
void CastEmeraldThornsSkill(Vector3 startPos, Vector3 target, SkillParams params);
void UpdateEmeraldThornsSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawEmeraldThornsSkill(void);
void UnloadEmeraldThornsSkill(void);

bool IsEmeraldThornsSkillCoiling(void);
int GetEmeraldThornsSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateEmeraldThornsProjectile(int index);

#endif // EMERALD_THORNS_SKILL_H