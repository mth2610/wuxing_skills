#ifndef HYDRO_MAELSTROM_SKILL_H
#define HYDRO_MAELSTROM_SKILL_H

#include "raylib.h"
#include "core/skill_manager.h"

#ifndef SKILL_PROJECTILE_DEF
#define SKILL_PROJECTILE_DEF
typedef struct {
    Vector3 position;
    float   radius;
    bool    active;
} SkillProjectile;
#endif

// ─── Lifecycle ────────────────────────────────────────────────────
void InitHydroMaelstromSkill(int screenWidth, int screenHeight);
void CastHydroMaelstromSkill(Vector3 startPos, Vector3 target, SkillParams params);
void UpdateHydroMaelstromSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawHydroMaelstromSkill(void);
void UnloadHydroMaelstromSkill(void);

// ─── Engine ↔ Skill Communication ────────────────────────────────
bool IsHydroMaelstromSkillCoiling(void);
int  GetHydroMaelstromSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateHydroMaelstromProjectile(int index);

#endif // HYDRO_MAELSTROM_SKILL_H
