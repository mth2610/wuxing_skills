#ifndef WOOD_SKILL_H
#define WOOD_SKILL_H

#include "raylib.h"
#include "skill_manager.h"

// DÙNG LẠI KIẾN TRÚC GỐC CỦA BẠN: Macro chống định nghĩa lại
#ifndef SKILL_PROJECTILE_DEF
#define SKILL_PROJECTILE_DEF
typedef struct {
  Vector3 position;
  float radius;
  bool active;
} SkillProjectile;
#endif

void InitWoodSkill(int screenWidth, int screenHeight);
void CastWoodSkill(Vector3 startPos, Vector3 target, SkillParams params);
void UpdateWoodSkill(float dt);
void DrawWoodSkill(void);
void UnloadWoodSkill(void);

bool IsWoodSkillCoiling(void);

// Trả lại hàm gốc, không cần dùng void*
int GetWoodSkillProjectiles(SkillProjectile *outProjectiles,
                            int maxProjectiles);
void DeactivateWoodProjectile(int index);

#endif // WOOD_SKILL_H