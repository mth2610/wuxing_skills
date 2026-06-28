#ifndef SKILL_MANAGER_H
#define SKILL_MANAGER_H

#include "raylib.h"

// Base colors for Wuxing and Taiji elements
#define ELEMENT_COLOR_WATER (Color){ 41, 128, 185, 255 }  // Cyan-Blue
#define ELEMENT_COLOR_WOOD  (Color){ 46, 204, 113, 255 }  // Vibrant Green
#define ELEMENT_COLOR_FIRE  (Color){ 231, 76, 60, 255 }   // Crimson Red
#define ELEMENT_COLOR_EARTH (Color){ 230, 126, 34, 255 }  // Ochre Brown / Orange
#define ELEMENT_COLOR_METAL (Color){ 149, 165, 166, 255 } // Silver Gray
#define ELEMENT_COLOR_TAIJI (Color){ 155, 89, 182, 255 } // Purple

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

typedef enum {
    SKILL_CAT_PROJECTILE = 0, // Tầm xa: Sát thương thấp, bay nhanh, an toàn cao
    SKILL_CAT_AOE_CONTROL,    // Tầm trung: Sát thương vừa, khống chế mạnh (Slow/Root), diện rộng, hồi lâu
    SKILL_CAT_MELEE,          // Cận chiến: Sát thương cực cao, phá giáp, đẩy lùi mạnh, tầm ngắn
    SKILL_CAT_TRAP_UTILITY,   // Bùa chú / Trận pháp: Ném cắm cọc, hiệu ứng dị biệt, hồi rất lâu
    SKILL_CAT_BUFF_SUPPORT    // Hỗ trợ / Hộ thuẫn: Tạo khiên chặn đạn, tăng tốc, mana, không gây dame
} SkillCategory;

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

float Skill_CalculateDamage(SkillCategory cat, SkillParams params);
float Skill_CalculateCooldown(SkillCategory cat, SkillParams params);
float Skill_CalculateKnockback(SkillCategory cat, SkillParams params);
float Skill_CalculateManaCost(SkillCategory cat, SkillParams params);
const char* Skill_GetCategoryName(SkillCategory cat);

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
bool IsEnemyRooted(void);

void AddRootToEnemy(float duration);
void AddSlowToEnemy(float duration);
void AddFloatingText(Vector3 pos, const char *text, Color color, float size, float lifetime);

Vector3 GetAccumulatedKnockback(void);
void ClearAccumulatedKnockback(void);
void AddKnockbackToEnemy(Vector3 force);

// New Combat and Shader API functions for Core API test
void ApplyAoEDamage(Vector3 position, float radius, float damage, float knockback);
void SkillManager_BeginShader(Shader shader);
void SkillManager_EndShader(void);

#endif // SKILL_MANAGER_H
