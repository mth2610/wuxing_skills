#include "wood_silk_skill.h"
#include "core/trail_system.h"
#include "core/skill_helper.h"
#include "core/color_gradient.h"
#include <string.h>
#include <math.h>

#define MAX_SILKS 4

typedef struct {
    bool active;
    Vector3 currentPos;
    int trailId;
} WoodSilkInstance;

static WoodSilkInstance s_silks[MAX_SILKS] = {0};
static int s_skillIndex = -1;
static ColorGradient s_silkGradient;
static Matrix s_casterMatrix;

#include "wood_silk_skill_params.inl"

void InitWoodSilkSkill(int screenWidth, int screenHeight) {
    (void)screenWidth;
    (void)screenHeight;

    s_skillIndex = Skill_GetIndexByName("WOOD_SILK");

    s_silkGradient.count = 0;
    ColorGradient_AddStop(&s_silkGradient, 0.0f, (Color){150, 255, 150, 255});
    ColorGradient_AddStop(&s_silkGradient, 0.5f, ELEMENT_COLOR_WOOD);
    ColorGradient_AddStop(&s_silkGradient, 1.0f, (Color){0, 50, 0, 0});
    
    s_casterMatrix = MatrixIdentity();

    // Register tunables
    static SkillTunableEntry s_tunables[WOOD_SILK_TUNABLE_COUNT];
    int tn = 0;
#include "wood_silk_skill_tunables.inl"
    SkillTunables_LoadPersisted("skills/wood/wood_silk_skill/wood_silk_skill.tuning", s_tunables, tn);
    RegisterSkillTunables(s_skillIndex, s_tunables, tn);

    // Initial force field build
    ForceField_Clear(&s_windForce);
    SkillForceMix_AddLayers(&s_windForceMix, &s_windForce);
}

void CastWoodSilkSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    (void)agentId;
    (void)target;
    (void)params;
    
    for (int i = 0; i < MAX_SILKS; i++) {
        if (s_silks[i].active && s_silks[i].trailId >= 0) {
            TrailEntity *t = GetTrail(s_silks[i].trailId);
            if (t && t->ownerTag == 999) { // Our unique tag
                KillTrail(s_silks[i].trailId);
            }
            s_silks[i].active = false;
        }
    }

    s_casterMatrix = MatrixTranslate(startPos.x, startPos.y, startPos.z);

    for (int i = 0; i < MAX_SILKS; i++) {
        TrailConfig cfg = {0};
        cfg.type = TRAIL_TYPE_WISP;
        cfg.pos = startPos;
        cfg.target = (Vector3){0, -1, 0}; // Initial strand direction
        cfg.len = s_strandLength;
        cfg.thick = s_strandThick;
        cfg.life = s_strandLife;
        cfg.tint = WHITE;
        cfg.forceField = &s_windForce;
        cfg.gradient = &s_silkGradient;
        cfg.priority = VFX_PRIORITY_LOW;
        cfg.ownerTag = 999;
        
        int tid = SpawnTrailEntity(cfg);
        if (tid >= 0) {
            s_silks[i].active = true;
            s_silks[i].currentPos = startPos;
            s_silks[i].trailId = tid;
        }
    }
}

void UpdateWoodSilkSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    (void)dt;
    (void)enemyPos;
    (void)enemyRadius;
    
    // Rebuild force field every frame so live tunable edits apply instantly
    ForceField_Clear(&s_windForce);
    SkillForceMix_AddLayers(&s_windForceMix, &s_windForce);

    for (int i = 0; i < MAX_SILKS; i++) {
        if (s_silks[i].active && s_silks[i].trailId >= 0) {
            TrailEntity *t = GetTrail(s_silks[i].trailId);
            if (t && t->ownerTag == 999) {
                float angle = (float)i * PI / 2.0f;
                Vector3 offset = (Vector3){ cosf(angle)*0.5f, sinf(angle)*0.5f, 0.0f };
                
                // Pin the head of the WISP to the caster's position
                // In a real skill we would track casterPos. We use s_silks[i].currentPos as a static anchor here.
                t->history[0] = Vector3Add(s_silks[i].currentPos, offset);
                t->nodeVelocity[0] = (Vector3){0, 0, 0}; // Head is fixed
            }
        }
    }
}

void DrawWoodSilkSkill(void) {
}

void UnloadWoodSilkSkill(void) {
    for (int i = 0; i < MAX_SILKS; i++) {
        if (s_silks[i].active && s_silks[i].trailId >= 0) {
            TrailEntity *t = GetTrail(s_silks[i].trailId);
            if (t && t->ownerTag == 999) {
                KillTrail(s_silks[i].trailId);
            }
            s_silks[i].active = false;
        }
    }
}
