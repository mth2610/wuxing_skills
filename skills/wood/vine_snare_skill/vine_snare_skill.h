#ifndef VINE_SNARE_SKILL_H
#define VINE_SNARE_SKILL_H

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

void InitVineSnareSkill(int screenWidth, int screenHeight);
void CastVineSnareSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateVineSnareSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawVineSnareSkill(void);
void UnloadVineSnareSkill(void);

bool IsVineSnareSkillCoiling(void);
int GetVineSnareSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateVineSnareProjectile(int index);

#endif // VINE_SNARE_SKILL_H
