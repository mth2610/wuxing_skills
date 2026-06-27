#ifndef FIRE_SKILL_H
#define FIRE_SKILL_H

#include "raylib.h"

#ifndef SKILL_PROJECTILE_DEF
#define SKILL_PROJECTILE_DEF
typedef struct {
    Vector3 position;
    float radius;
    bool active;
} SkillProjectile;
#endif

void InitFireSkill(int screenWidth, int screenHeight);
void CastFireSkill(Vector3 startPos, Vector3 target, float twistPhase, float sizeScale);
void UpdateFireSkill(float dt);
void DrawFireSkill(void);
void UnloadFireSkill(void);

int GetFireSkillProjectiles(SkillProjectile* outProjectiles, int maxProjectiles);
void DeactivateFireProjectile(int index);

#endif // FIRE_SKILL_H
