#ifndef WOOD_SKILL_H
#define WOOD_SKILL_H

#include "raylib.h"

#ifndef SKILL_PROJECTILE_DEF
#define SKILL_PROJECTILE_DEF
typedef struct {
    Vector2 position;
    float radius;
    bool active;
} SkillProjectile;
#endif

void InitWoodSkill(int screenWidth, int screenHeight);
void CastWoodSkill(Vector2 startPos, Vector2 target, int branchCount, float sizeScale);
void UpdateWoodSkill(float dt);
void DrawWoodSkill(void);
void UnloadWoodSkill(void);
bool IsWoodSkillCoiling(void);

int GetWoodSkillProjectiles(SkillProjectile* outProjectiles, int maxProjectiles);
void DeactivateWoodProjectile(int index);

#endif // WOOD_SKILL_H
