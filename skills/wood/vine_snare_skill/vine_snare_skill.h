#ifndef SKILL_VINE_SNARE_H
#define SKILL_VINE_SNARE_H

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
void InitVineSnareSkill(int screenWidth, int screenHeight);
void CastVineSnareSkill(Vector3 startPos, Vector3 target, SkillParams params);
void UpdateVineSnareSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawVineSnareSkill(void);
void UnloadVineSnareSkill(void);

// Engine communication
bool IsVineSnareSkillCoiling(void);
int GetVineSnareSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateVineSnareProjectile(int index);

#endif // SKILL_VINE_SNARE_H