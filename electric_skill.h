#ifndef ELECTRIC_SKILL_H
#define ELECTRIC_SKILL_H

#include "raylib.h"

#ifndef SKILL_PROJECTILE_DEF
#define SKILL_PROJECTILE_DEF
typedef struct {
    Vector2 position;
    float radius;
    bool active;
} SkillProjectile;
#endif

void InitElectricSkill(int screenWidth, int screenHeight);
void CastElectricSkill(Vector2 startPos, Vector2 target, float sizeScale);
void UpdateElectricSkill(float dt);
void DrawElectricSkill(void);
void UnloadElectricSkill(void);
bool IsElectricSkillShocking(void);

int GetElectricSkillProjectiles(SkillProjectile* outProjectiles, int maxProjectiles);
void DeactivateElectricProjectile(int index);

#endif // ELECTRIC_SKILL_H
