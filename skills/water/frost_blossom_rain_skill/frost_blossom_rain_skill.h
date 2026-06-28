#ifndef SKILL_FROST_BLOSSOM_RAIN_H
#define SKILL_FROST_BLOSSOM_RAIN_H

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

// API vòng đời kỹ năng Mưa Hoa Tuyết (Frost Blossom Rain)
void InitFrostBlossomRainSkill(int w, int h);
void CastFrostBlossomRainSkill(Vector3 start, Vector3 target, SkillParams p);
void UpdateFrostBlossomRainSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawFrostBlossomRainSkill(void);
void UnloadFrostBlossomRainSkill(void);
bool IsFrostBlossomRainSkillCoiling(void);
int GetFrostBlossomRainSkillProjectiles(SkillProjectile *out, int max);
void DeactivateFrostBlossomRainProjectile(int index);

#endif // SKILL_FROST_BLOSSOM_RAIN_H
