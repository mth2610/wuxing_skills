#ifndef TSUNAMI_SKILL_H
#define TSUNAMI_SKILL_H

#include "raylib.h"
#include <stdbool.h>
#include "core/skill_manager.h"   // SkillParams (canonical definition)

#ifndef SKILL_PROJECTILE_DEF
#define SKILL_PROJECTILE_DEF
typedef struct {
    Vector3 position;
    float radius;
    bool active;
} SkillProjectile;
#endif

void InitTsunamiSkill(int screenWidth, int screenHeight);
void CastTsunamiSkill(Vector3 startPos, Vector3 target, SkillParams params);
void UpdateTsunamiSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawTsunamiSkill(void);
void UnloadTsunamiSkill(void);

bool IsTsunamiSkillCoiling(void);
int GetTsunamiSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateTsunamiProjectile(int index);

#endif // TSUNAMI_SKILL_H