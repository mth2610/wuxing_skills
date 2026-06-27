#ifndef METAL_SKILL_H
#define METAL_SKILL_H

#include "raylib.h"
#include "core/skill_manager.h"

// Dùng chung macro chống định nghĩa lại của bạn
#ifndef SKILL_PROJECTILE_DEF
#define SKILL_PROJECTILE_DEF
typedef struct {
  Vector3 position;
  float radius;
  bool active;
} SkillProjectile;
#endif

void InitMetalSkill(int screenWidth, int screenHeight);

// Đã cập nhật thành SkillParams
void CastMetalSkill(Vector3 startPos, Vector3 target, SkillParams params);

void UpdateMetalSkill(float dt);
void DrawMetalSkill(void);
void UnloadMetalSkill(void);

int GetMetalSkillProjectiles(SkillProjectile *outProjectiles,
                             int maxProjectiles);
void DeactivateMetalProjectile(int index);

#endif // METAL_SKILL_H