#ifndef TTQP_PHA_LANG_QUYEN_SKILL_H
#define TTQP_PHA_LANG_QUYEN_SKILL_H

#include "raylib.h"
#include "skills/skill_manager.h"
#include "skills/skill_helper.h"

#ifndef SKILL_PROJECTILE_DEF
#define SKILL_PROJECTILE_DEF
typedef struct {
    Vector3 position;
    float radius;
    bool active;
} SkillProjectile;
#endif

void InitTtqpPhaLangQuyenSkill(int screenWidth, int screenHeight);
void CastTtqpPhaLangQuyenSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateTtqpPhaLangQuyenSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawTtqpPhaLangQuyenSkill(void);
void UnloadTtqpPhaLangQuyenSkill(void);
bool IsTtqpPhaLangQuyenSkillCoiling(void);
int  GetTtqpPhaLangQuyenSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateTtqpPhaLangQuyenProjectile(int index);

#endif /* TTQP_PHA_LANG_QUYEN_SKILL_H */
