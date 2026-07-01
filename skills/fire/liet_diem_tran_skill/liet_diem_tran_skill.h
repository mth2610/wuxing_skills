#ifndef LIET_DIEM_TRAN_SKILL_H
#define LIET_DIEM_TRAN_SKILL_H

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

void InitLietDiemTranSkill(int screenWidth, int screenHeight);
void CastLietDiemTranSkill(Vector3 startPos, Vector3 target, SkillParams params);
void UpdateLietDiemTranSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawLietDiemTranSkill(void);
void UnloadLietDiemTranSkill(void);
bool IsLietDiemTranSkillCoiling(void);
int GetLietDiemTranSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateLietDiemTranProjectile(int index);

#endif // LIET_DIEM_TRAN_SKILL_H
