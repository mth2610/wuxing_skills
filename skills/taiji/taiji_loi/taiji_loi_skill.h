// skills/taiji/taiji_loi/taiji_loi_skill.h
// LÔI — Thái Cực tuyệt học #2 (MODULES_ROADMAP.md Module 6): a violet-white
// sky→ground thunderstrike. Aims at the live PHONG vortex center when one
// exists (gom rồi giáng), otherwise at the cast target. Huge mana cost — the
// intended "Vô Sát" drain that ends the Thái Cực state.
#ifndef TAIJI_LOI_SKILL_H
#define TAIJI_LOI_SKILL_H

#include "raylib.h"
#include "core/skill_manager.h"

void InitTaijiLoiSkill(int screenWidth, int screenHeight);
void CastTaijiLoiSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateTaijiLoiSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawTaijiLoiSkill(void);
void UnloadTaijiLoiSkill(void);

#endif // TAIJI_LOI_SKILL_H
