#ifndef METAL_SKILL_H
#define METAL_SKILL_H

#include "raylib.h"

#ifndef SKILL_PROJECTILE_DEF
#define SKILL_PROJECTILE_DEF
typedef struct {
    Vector2 position;
    float radius;
    bool active;
} SkillProjectile;
#endif

void InitMetalSkill(int screenWidth, int screenHeight);

// Gọi chiêu: Bắn 'count' luồng kiếm khí từ startPos về phía target
void CastMetalSkill(Vector2 startPos, Vector2 target, int count, float sizeScale);

void UpdateMetalSkill(float dt);
void DrawMetalSkill(void);
void UnloadMetalSkill(void);

int GetMetalSkillProjectiles(SkillProjectile* outProjectiles, int maxProjectiles);
void DeactivateMetalProjectile(int index);

#endif // METAL_SKILL_H
