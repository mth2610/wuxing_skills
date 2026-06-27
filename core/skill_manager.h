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
    bool showPortal;
} SkillParams;

void InitSkillManager(int screenWidth, int screenHeight);
void UpdateSkillManager(float dt, Vector3 enemyPos, float enemyRadius);
void DrawSkillManagerWorld3D(void);
void DrawSkillManagerOverlay(void);
void UnloadSkillManager(void);
void CastSkill(int skillIndex, Vector3 startPos, Vector3 target, SkillParams params);

int RegisterSkill(const char* name, Color color,
                  void (*init)(int screenWidth, int screenHeight),
                  void (*cast)(Vector3 startPos, Vector3 target, SkillParams params),
                  void (*update)(float dt, Vector3 enemyPos, float enemyRadius),
                  void (*draw)(void),
                  void (*unload)(void));
void SetSkillOverrides(int skillIndex, int pathType, int anchorType, int quantity, float sizeScale);
int GetRegisteredSkillCount(void);
const char* GetRegisteredSkillName(int index);
Color GetRegisteredSkillColor(int index);

typedef struct {
    Vector2 screenPos;
    float   depthFactor;
    bool    behindCamera;
} ProjectedPoint;

ProjectedPoint ProjectPointCached(Vector3 worldPos, Camera3D cam);

void RegisterStaticOccluder(Vector3 center, float radius, float height);
void ClearStaticOccluders(void);
float GetLineOfSightVisibility(Vector3 viewPoint, Vector3 targetPoint);

bool IsAnySkillCoiling(void);
bool IsAnySkillShocking(void);
bool IsEnemySlowed(void);
bool IsEnemyBurning(void);

#endif // SKILL_MANAGER_H
