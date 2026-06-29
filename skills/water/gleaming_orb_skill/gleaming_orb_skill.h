#ifndef SKILL_GLEAMING_ORB_H
#define SKILL_GLEAMING_ORB_H

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

/* Vòng đời Core Lifecycle APIs */
void InitGleamingOrbSkill(int screenWidth, int screenHeight);
void CastGleamingOrbSkill(Vector3 startPos, Vector3 target, SkillParams params);
void UpdateGleamingOrbSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawGleamingOrbSkill(void);
void UnloadGleamingOrbSkill(void);

/* Giao tiếp nội bộ Engine */
bool IsGleamingOrbSkillCoiling(void);
int GetGleamingOrbSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateGleamingOrbProjectile(int index);

#endif // SKILL_GLEAMING_ORB_H