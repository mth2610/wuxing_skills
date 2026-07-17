#ifndef SKILL_GALAXY_SPIRAL_H
#define SKILL_GALAXY_SPIRAL_H

#include "raylib.h"
#include "core/skill_manager.h"

// Spiral galaxy — a GPU-particle showcase. Spawns thousands of glowing particles along spiral
// arms in a disk around the cast target; a vortex force field spins them with differential
// rotation (inner faster) while a gentle inward pull keeps the disk bound. Particles are drawn by
// the central GpuParticleSystem_Draw (main.c), so this skill's Draw does nothing.
void InitGalaxySpiralSkill(int screenWidth, int screenHeight);
void CastGalaxySpiralSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateGalaxySpiralSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawGalaxySpiralSkill(void);
void UnloadGalaxySpiralSkill(void);

#endif // SKILL_GALAXY_SPIRAL_H
