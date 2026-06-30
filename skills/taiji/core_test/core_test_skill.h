#ifndef SKILL_CORE_TEST_H
#define SKILL_CORE_TEST_H

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

// Internal test/sandbox skill (Taiji) — exercises the GPU vertex
// displacement system (core/procedural_mesh_utils.h's
// ProceduralMesh_CreateBaseCylinder + displacement.glsl's
// DisplaceVertex_AlongPath): one static cylinder mesh baked once at Init,
// bent every frame by the vertex shader between cast start/target with no
// CPU-side rebuild. Not a gameplay skill.

void InitCoreTestSkill(int screenWidth, int screenHeight);
void CastCoreTestSkill(Vector3 startPos, Vector3 target, SkillParams params);
void UpdateCoreTestSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawCoreTestSkill(void);
void UnloadCoreTestSkill(void);

bool IsCoreTestSkillCoiling(void);
int GetCoreTestSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateCoreTestProjectile(int index);

#endif // SKILL_CORE_TEST_H
