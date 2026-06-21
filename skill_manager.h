#ifndef SKILL_MANAGER_H
#define SKILL_MANAGER_H

#include "raylib.h"

typedef enum {
    SKILL_WATER = 0,
    SKILL_METAL,
    SKILL_FIRE,
    SKILL_WOOD,
    SKILL_ELECTRIC
} SkillType;

typedef enum {
    CAST_ANCHOR_CASTER = 0,
    CAST_ANCHOR_TARGET
} CastAnchorType;

typedef enum {
    CAST_PATH_PROJECTILE = 0,
    CAST_PATH_UNDERGROUND,
    CAST_PATH_SKY
} CastPathType;

typedef struct {
    int level;
    int milestone;
    int quantity;
    float sizeScale;
    float damage;
    CastAnchorType anchorType;
    CastPathType pathType;
    float casterZ;
    bool showPortal;
} SkillParams;

void InitSkillManager(int screenWidth, int screenHeight);
void UpdateSkillManager(float dt, Vector2 enemyPos, float enemyRadius);
void DrawSkillManager(void);
void UnloadSkillManager(void);
void CastSkill(SkillType type, Vector2 startPos, Vector2 target, SkillParams params);

bool IsAnySkillCoiling(void);
bool IsAnySkillShocking(void);
bool IsEnemySlowed(void);
bool IsEnemyBurning(void);

#endif // SKILL_MANAGER_H
