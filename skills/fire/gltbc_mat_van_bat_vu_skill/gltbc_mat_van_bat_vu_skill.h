#ifndef GLTBC_MAT_VAN_BAT_VU_SKILL_H
#define GLTBC_MAT_VAN_BAT_VU_SKILL_H

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

void InitGltbcMatVanBatVuSkill(int screenWidth, int screenHeight);
void CastGltbcMatVanBatVuSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateGltbcMatVanBatVuSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawGltbcMatVanBatVuSkill(void);
void UnloadGltbcMatVanBatVuSkill(void);

bool IsGltbcMatVanBatVuSkillCoiling(void);
int  GetGltbcMatVanBatVuSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateGltbcMatVanBatVuProjectile(int index);

#endif /* GLTBC_MAT_VAN_BAT_VU_SKILL_H */
