#include "glowing_vines_skill.h"
#include "core/particle_system.h"
#include "core/skill_helper.h"
#include "core/skill_manager.h"
#include "core/composition/visual_composer.h"
#include "core/path_spline.h"
#include "raymath.h"
#include <math.h>

#ifndef PI
#define PI 3.1415926535f
#endif

#define MAX_INSTANCES 16

typedef struct {
    bool active;
    int ownerAgentId;
    Vector3 startPos;
    Vector3 targetPos;
    float progress;
    float sizeScale;
    float damage;
    int branchCount;
    
    Vector3 p1[5];
    Vector3 p2[5];
    Vector3 contactPos[5];
    bool branchActive[5];
    bool impactTriggered;
} SkillInstance;

static SkillInstance s_instances[MAX_INSTANCES];

#include "glowing_vines_skill_params.inl"

static Vector3 GetWoodPointAt(Vector3 startPos, Vector3 p1, Vector3 p2, Vector3 contactPos, Vector3 targetPos, float t, float sizeScale, int branchIndex, int branchCount) {
    if (t <= 1.0f) {
        Vector3 pos = GetBezierPoint(startPos, p1, p2, contactPos, t);
        Vector3 dir = Vector3Normalize(Vector3Subtract(targetPos, startPos));
        Vector3 perp = (Vector3){-dir.z, 0, dir.x};
        if (Vector3Length(perp) == 0.0f) perp = (Vector3){0, 0, 1};
        perp = Vector3Normalize(perp);
        
        float wiggle = sinf(t * 18.0f) + sinf(t * 31.0f) * 0.4f;
        pos = Vector3Add(pos, Vector3Scale(perp, wiggle * s_wiggleAmp * sizeScale));
        return pos;
    } else {
        float t_spiral = t - 1.0f;
        float ratio = t_spiral / 0.8f;
        if (ratio > 1.0f) ratio = 1.0f;
        
        float contactAngle = atan2f(contactPos.z - targetPos.z, contactPos.x - targetPos.x);
        float coilDir = (branchIndex % 2 == 0) ? 1.0f : -1.0f;
        float theta = ratio * 2.2f * (2.0f * PI) * coilDir +
                      sinf(ratio * 8.0f) * 0.8f + sinf(ratio * 19.0f) * 0.25f;
        theta += sinf(ratio * 15.0f) * 0.25f;
        
        float phiOffset = 0.0f;
        if (branchCount > 1) {
            phiOffset = ((float)branchIndex / (float)branchCount) * PI;
        }
        float phi = contactAngle + phiOffset;
        
        float r_a = s_coilRadius * sizeScale;
        float r_b = s_coilRadius * sizeScale;
        
        float wobbleScale = ratio > 0.1f ? 1.0f : ratio / 0.1f;
        float wobble = (sinf(theta * 6.0f) * 0.04f + cosf(theta * 11.0f) * 0.015f) * wobbleScale;
        
        float tighten = 1.0f - ratio * 0.45f;
        float curr_ra = (r_a + wobble) * tighten;
        float curr_rb = (r_b + wobble) * tighten;
        float wrapFactor = 0.75f + 0.25f * sinf(ratio * 10.0f);
        
        curr_ra *= wrapFactor;
        curr_rb *= wrapFactor;
        
        float radiusNoise = 1.0f + sinf(theta * 3.0f) * 0.08f;
        curr_ra *= radiusNoise;
        curr_rb *= radiusNoise;
        
        float height = s_coilHeight * sizeScale;
        float y = targetPos.y - height * 0.5f + ratio * height;
        
        return (Vector3){
            targetPos.x + curr_ra * cosf(theta) * cosf(phi) - curr_rb * sinf(theta) * sinf(phi),
            y + wobble * 0.1f,
            targetPos.z + curr_ra * cosf(theta) * sinf(phi) + curr_rb * sinf(theta) * cosf(phi)
        };
    }
}

void InitGlowingVinesSkill(int screenWidth, int screenHeight) {
    for (int i = 0; i < MAX_INSTANCES; i++) s_instances[i].active = false;

#define GLOWING_VINES_TUNABLE_COUNT 13
    static SkillTunableEntry s_tunables[GLOWING_VINES_TUNABLE_COUNT];
    int tn = 0;
#include "glowing_vines_skill_tunables.inl"
    int skillIndex = Skill_GetIndexByName("GLOWING_VINES");
    SkillTunables_LoadPersisted("skills/wood/glowing_vines_skill/glowing_vines_skill.tuning", s_tunables, tn);
    RegisterSkillTunables(skillIndex, s_tunables, tn);
}

void CastGlowingVinesSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    if (!SkillManager_CanCast(Skill_GetIndexByName("GLOWING_VINES"), agentId)) return;
    
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (s_instances[i].active) continue;
        
        SkillInstance *s = &s_instances[i];
        s->startPos = startPos;
        s->targetPos = target;
        s->ownerAgentId = agentId;
        s->progress = 0.0f;
        s->sizeScale = params.sizeScale;
        s->damage = s_damage;
        s->impactTriggered = false;
        
        // Determine branch count
        int minB = (int)s_minBranches;
        int maxB = (int)s_maxBranches;
        if (minB < 1) minB = 1;
        if (maxB < minB) maxB = minB;
        s->branchCount = GetRandomValue(minB, maxB);
        if (s->branchCount > 5) s->branchCount = 5;
        
        // Calculate Bezier control points
        for (int j = 0; j < s->branchCount; j++) {
            float curveSign = (j % 2 == 0) ? 1.0f : -1.0f;
            float dist = Vector3Distance(startPos, target);
            Vector3 dir = Vector3Normalize(Vector3Subtract(target, startPos));
            Vector3 perp = (Vector3){-dir.z, 0.0f, dir.x};
            if (Vector3Length(perp) == 0.0f) perp = (Vector3){0, 0, 1};
            perp = Vector3Normalize(perp);
            
            float distMult1 = 0.35f + j * 0.05f;
            float distMult2 = 0.70f - j * 0.05f;
            float perpMult1 = curveSign * (0.25f + j * 0.03f);
            float perpMult2 = -curveSign * (0.15f + j * 0.03f);
            
            s->p1[j] = Vector3Add(Vector3Add(startPos, Vector3Scale(dir, dist * distMult1)),
                                  Vector3Scale(perp, perpMult1 * dist));
            s->p2[j] = Vector3Add(Vector3Add(startPos, Vector3Scale(dir, dist * distMult2)),
                                  Vector3Scale(perp, perpMult2 * dist));
            
            float wrapRadius = s_coilRadius * s->sizeScale;
            Vector3 dirToTarget = Vector3Normalize(Vector3Subtract(target, s->p2[j]));
            s->contactPos[j] = Vector3Subtract(target, Vector3Scale(dirToTarget, wrapRadius));
            s->branchActive[j] = true;
        }
        
        s->active = true;
        
        // Spawn wood cast/bloom effect at start
        SpawnCastEffect(startPos, EFFECT_PRESET_WOOD_BLOOM, params.sizeScale * 0.8f);
        PlayCastSound(EFFECT_PRESET_WOOD_BLOOM);
        
        // Trigger cooldown
        int skillIndex = Skill_GetIndexByName("GLOWING_VINES");
        SkillManager_TriggerCooldown(skillIndex, agentId, Skill_CalculateCooldown(SKILL_CAT_PROJECTILE, params));
        return;
    }
}

void UpdateGlowingVinesSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        SkillInstance *s = &s_instances[i];
        if (!s->active) continue;
        
        s->progress += dt * s_travelSpeed;
        
        // At progress >= 1.0f, trigger impact
        if (s->progress >= 1.0f && !s->impactTriggered) {
            s->impactTriggered = true;
            
            // Spawn wood bloom impact at target pos
            SpawnImpactEffect(s->targetPos, EFFECT_PRESET_WOOD_BLOOM, s->sizeScale);
            PlayImpactSound(EFFECT_PRESET_WOOD_BLOOM);
            
            // Apply AoE Damage
            ApplyAoEDamage(s->targetPos, s_coilRadius * s->sizeScale * 2.5f, s->damage, 1.0f);
        }
        
        if (s->progress >= s_maxProgress) {
            s->active = false;
        }
    }
}

void DrawGlowingVinesSkill(void) {
    float time = GetTime();
    for (int i = 0; i < MAX_INSTANCES; i++) {
        SkillInstance *s = &s_instances[i];
        if (!s->active) continue;
        
        for (int j = 0; j < s->branchCount; j++) {
            if (!s->branchActive[j]) continue;
            VFX_ComposeGlowingVine(s->startPos, s->targetPos, s->p1[j], s->p2[j], s->contactPos[j], s->progress, time, s->sizeScale, j, s->branchCount);
        }
    }
}

void UnloadGlowingVinesSkill(void) {
    // No-op
}

bool IsGlowingVinesSkillCoiling(void) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (s_instances[i].active && s_instances[i].progress >= 1.0f && s_instances[i].progress < 1.8f) {
            return true;
        }
    }
    return false;
}

int GetGlowingVinesSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles) {
    int count = 0;
    for (int i = 0; i < MAX_INSTANCES && count < maxProjectiles; i++) {
        SkillInstance *s = &s_instances[i];
        if (!s->active || s->progress >= 1.0f) continue;
        
        // Use the tip of the first branch as the projectile position
        Vector3 tipPos = GetWoodPointAt(s->startPos, s->p1[0], s->p2[0], s->contactPos[0], s->targetPos, s->progress, s->sizeScale, 0, s->branchCount);
        outProjectiles[count].position = tipPos;
        outProjectiles[count].radius = s_coilRadius * s->sizeScale;
        outProjectiles[count].active = true;
        count++;
    }
    return count;
}

void DeactivateGlowingVinesProjectile(int index) {
    int count = 0;
    for (int i = 0; i < MAX_INSTANCES; i++) {
        SkillInstance *s = &s_instances[i];
        if (s->active && s->progress < 1.0f) {
            if (count == index) {
                s->progress = 1.0f;
                return;
            }
            count++;
        }
    }
}
