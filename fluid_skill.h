#ifndef FLUID_SKILL_H
#define FLUID_SKILL_H

#include "raylib.h"

#ifndef SKILL_PROJECTILE_DEF
#define SKILL_PROJECTILE_DEF
typedef struct {
    Vector2 position;
    float radius;
    bool active;
} SkillProjectile;
#endif

// Khởi tạo hệ thống
void InitFluidSkill(int screenWidth, int screenHeight);

// Gọi 1 lần cho 1 tia nước độc lập.
// twistPhase: Độ lệch pha (0.0f cho tia 1, PI cho tia 2 để chúng xoắn chéo nhau)
void CastFluidSkill(Vector2 startPos, Vector2 target, float twistPhase, float sizeScale);

// Cập nhật hệ thống (Không cần truyền toạ độ tay vào đây nữa)
void UpdateFluidSkill(float dt);

// Vẽ ra màn hình
void DrawFluidSkill(void);

// Dọn dẹp
void UnloadFluidSkill(void);

int GetFluidSkillProjectiles(SkillProjectile* outProjectiles, int maxProjectiles);
void DeactivateFluidProjectile(int index);

#endif // FLUID_SKILL_H
