#ifndef SKILL_{{NAME}}_H
#define SKILL_{{NAME}}_H

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

// Main lifecycle
void Init{{Name}}Skill(int screenWidth, int screenHeight);
void Cast{{Name}}Skill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void Update{{Name}}Skill(float dt, Vector3 enemyPos, float enemyRadius);
void Draw{{Name}}Skill(void);
void Unload{{Name}}Skill(void);

// Engine <-> Skill communication
bool Is{{Name}}SkillCoiling(void);
int Get{{Name}}SkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void Deactivate{{Name}}Projectile(int index);

#endif // SKILL_{{NAME}}_H
