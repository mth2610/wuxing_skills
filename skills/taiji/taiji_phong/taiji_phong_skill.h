// skills/taiji/taiji_phong/taiji_phong_skill.h
// PHONG — Thái Cực tuyệt học #1 (MODULES_ROADMAP.md Module 6): a suction
// vortex at the aim point that pulls enemy agents inward
// (Entity_ApplyPull) and deflects enemy projectiles out of the combat
// registry (Combat_DeflectProjectilesInRadius). Castable only while the
// caster is in Thái Cực (Entity_IsTaijiActive).
#ifndef TAIJI_PHONG_SKILL_H
#define TAIJI_PHONG_SKILL_H

#include "raylib.h"
#include "core/skill_manager.h"
#include <stdbool.h>

void InitTaijiPhongSkill(int screenWidth, int screenHeight);
void CastTaijiPhongSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateTaijiPhongSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawTaijiPhongSkill(void);
void UnloadTaijiPhongSkill(void);

// LÔI aims at the live vortex when there is one (thiết kế: Lôi giáng vào
// tâm Phong). Returns false when no vortex is active (outCenter untouched).
bool TaijiPhong_GetActiveCenter(Vector3 *outCenter);

#endif // TAIJI_PHONG_SKILL_H
