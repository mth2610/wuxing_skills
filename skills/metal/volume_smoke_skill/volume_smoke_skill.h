#ifndef SKILL_VOLUME_SMOKE_H
#define SKILL_VOLUME_SMOKE_H

#include "raylib.h"
#include "core/skill_manager.h"

void InitVolumeSmokeSkill(int screenWidth, int screenHeight);
void CastVolumeSmokeSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateVolumeSmokeSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawVolumeSmokeSkill(void);
void UnloadVolumeSmokeSkill(void);

#endif // SKILL_VOLUME_SMOKE_H