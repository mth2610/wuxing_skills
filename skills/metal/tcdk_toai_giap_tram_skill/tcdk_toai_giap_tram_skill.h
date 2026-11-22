#ifndef TCDK_TOAI_GIAP_TRAM_SKILL_H
#define TCDK_TOAI_GIAP_TRAM_SKILL_H

#include "raylib.h"
#include "skills/skill_manager.h"

#ifndef SKILL_PROJECTILE_DEF
#define SKILL_PROJECTILE_DEF
typedef struct {
    Vector3 position;
    float   radius;
    bool    active;
} SkillProjectile;
#endif

void InitTcdkToaiGiapTramSkill(int screenWidth, int screenHeight);
void CastTcdkToaiGiapTramSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateTcdkToaiGiapTramSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawTcdkToaiGiapTramSkill(void);
void UnloadTcdkToaiGiapTramSkill(void);

bool IsTcdkToaiGiapTramSkillCoiling(void);
int  GetTcdkToaiGiapTramSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateTcdkToaiGiapTramProjectile(int index);

#endif /* TCDK_TOAI_GIAP_TRAM_SKILL_H */
