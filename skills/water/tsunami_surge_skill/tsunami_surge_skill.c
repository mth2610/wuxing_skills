#include "skills/water/tsunami_surge_skill/tsunami_surge_skill.h"
#include "core/skill_helper.h"
#include "core/procedural_mesh_utils.h"
#include "core/resource_manager.h"
#include "core/utils_math.h"
#include "core/screen_distort.h"
#include "core/metaball_fx.h"
#include "raymath.h"
#include "rlgl.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#define MAX_INSTANCES 4

typedef enum {
    STATE_CASTING,
    STATE_SURGING,
    STATE_IMPACT,
    STATE_DISSOLVE
} SkillState;

typedef struct {
    SkillState state;
    Vector3 startPos;
    Vector3 currentPos;
    Vector3 targetPos;
    Vector3 direction;
    
    float stateTimer;
    float sizeScale;
    float damage;
    float waveRadius;
    float speed;
    bool active;

    MeshDisplacementParams displacement;
} TsunamiInstance;

static TsunamiInstance s_instances[MAX_INSTANCES];
static Shader s_tsunamiShader;
static Mesh s_baseCylinderGPU; // Cache 1 lần duy nhất cho mọi ngọn sóng
static int s_uDissolveLoc;
static int s_uRadiusLoc;

void InitTsunamiSurgeSkill(int screenWidth, int screenHeight) {
    for (int i = 0; i < MAX_INSTANCES; i++) s_instances[i].active = false;

    // Khởi tạo lưới Cylinder (Hở 2 đầu) lưu vĩnh viễn trên GPU
    s_baseCylinderGPU = ProceduralMesh_CreateBaseCylinder(24, 16);

    s_tsunamiShader = ResourceManager_LoadShader(
        "skills/water/tsunami_surge_skill/tsunami_surge.vs", 
        "skills/water/tsunami_surge_skill/tsunami_surge.fs"
    );
    
    if (s_tsunamiShader.id > 0) {
        s_uDissolveLoc = GetShaderLocation(s_tsunamiShader, "u_dissolve");
        s_uRadiusLoc = GetShaderLocation(s_tsunamiShader, "u_radius");
    }
}

void CastTsunamiSurgeSkill(Vector3 startPos, Vector3 target, SkillParams params) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (s_instances[i].active) continue;

        TsunamiInstance *inst = &s_instances[i];
        inst->state = STATE_CASTING;
        inst->startPos = startPos;
        inst->currentPos = startPos;
        inst->targetPos = target;
        inst->direction = Vector3Normalize(Vector3Subtract(target, startPos));
        
        inst->stateTimer = 0.0f;
        inst->sizeScale = params.sizeScale;
        inst->damage = params.damage;
        inst->waveRadius = 18.0f * params.sizeScale; 
        inst->speed = 180.0f; // Tốc độ di chuyển khổng lồ
        inst->active = true;

        inst->displacement = ProceduralMesh_DefaultDisplacementParams();
        inst->displacement.amplitude = 10.0f * params.sizeScale;
        inst->displacement.frequency = 0.15f;
        inst->displacement.speed = 8.0f;
        inst->displacement.taperStart = 0.3f; // Đuôi sóng nhỏ lại
        inst->displacement.taperEnd = 1.0f;   // Đầu sóng phình to

        SpawnCastEffect(startPos, EFFECT_PRESET_WATER_SPLASH, params.sizeScale * 0.8f);
        return;
    }
}

void UpdateTsunamiSurgeSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        TsunamiInstance *inst = &s_instances[i];
        if (!inst->active) continue;
        inst->stateTimer += dt;

        switch (inst->state) {
            case STATE_CASTING:
                if (inst->stateTimer >= 0.3f) {
                    inst->state = STATE_SURGING;
                    inst->stateTimer = 0.0f;
                }
                break;

            case STATE_SURGING: {
                // Di chuyển ngọn sóng tới trước
                inst->currentPos = Vector3Add(inst->currentPos, Vector3Scale(inst->direction, inst->speed * dt));
                
                // Uốn cong Bezier: Đuôi theo sau, phần thân vòm lên khỏi mặt đất
                float waveLength = 55.0f * inst->sizeScale;
                Vector3 tail = Vector3Subtract(inst->currentPos, Vector3Scale(inst->direction, waveLength));
                
                inst->displacement.pathP0 = tail;
                inst->displacement.pathP3 = inst->currentPos;
                
                // Nâng cao Y ở giữa để tạo vòm sóng đè tới trước
                inst->displacement.pathP1 = Vector3Add(tail, Vector3Scale(inst->direction, waveLength * 0.33f));
                inst->displacement.pathP1.y += 15.0f * inst->sizeScale;
                inst->displacement.pathP2 = Vector3Add(tail, Vector3Scale(inst->direction, waveLength * 0.66f));
                inst->displacement.pathP2.y += 20.0f * inst->sizeScale;

                // Tương tác thế giới mỗi frame (Đánh lừa thị giác AAA)
                if (GetRandomValue(0, 10) < 4) {
                    ScreenDistort_Add(inst->currentPos, 35.0f * inst->sizeScale, 0.15f, 0.4f, 25.0f);
                    SpawnGroundDecal(DECAL_PRESET_WATER_SPLASH, inst->currentPos, 35.0f * inst->sizeScale, 1.5f);
                    MetaballFX_RegisterBlob(inst->currentPos, 20.0f * inst->sizeScale);
                }

                if (Vector3Distance(inst->currentPos, inst->targetPos) < inst->waveRadius + enemyRadius) {
                    inst->state = STATE_IMPACT;
                    inst->stateTimer = 0.0f;
                }
                break;
            }

            case STATE_IMPACT:
                // Va chạm vỡ òa thành đại hồng thủy
                SpawnImpactEffect(inst->currentPos, EFFECT_PRESET_WATER_SPLASH, inst->sizeScale * 1.5f);
                ApplyAoEDamage(inst->currentPos, inst->waveRadius * 2.0f, inst->damage, 50.0f);
                
                inst->state = STATE_DISSOLVE;
                inst->stateTimer = 0.0f;
                break;

            case STATE_DISSOLVE:
                if (inst->stateTimer >= 0.5f) {
                    inst->active = false;
                }
                break;
        }
    }
}

void DrawTsunamiSurgeSkill(void) {
    if (s_tsunamiShader.id == 0) return;

    for (int i = 0; i < MAX_INSTANCES; i++) {
        TsunamiInstance *inst = &s_instances[i];
        if (!inst->active || inst->state == STATE_CASTING) continue;

        float dissolveVal = 0.0f;
        if (inst->state == STATE_DISSOLVE) {
            dissolveVal = SmoothStep01(inst->stateTimer / 0.5f);
        }

        SkillManager_BeginShader(s_tsunamiShader);
        
        SetShaderValue(s_tsunamiShader, s_uDissolveLoc, &dissolveVal, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_tsunamiShader, s_uRadiusLoc, &inst->waveRadius, SHADER_UNIFORM_FLOAT);
        ProceduralMesh_SetDisplacementUniforms(s_tsunamiShader, &inst->displacement);

        rlColor4ub(255, 255, 255, 255);
        
        // Nước phải trong suốt, dùng BLEND_ALPHA thay vì cản sáng toàn bộ
        BeginBlendMode(BLEND_ALPHA);
        
        Material mat = LoadMaterialDefault();
        mat.shader = s_tsunamiShader;
        DrawMesh(s_baseCylinderGPU, mat, MatrixIdentity());

        EndBlendMode();
        SkillManager_EndShader();
    }
}

void UnloadTsunamiSurgeSkill(void) {
    ProceduralMesh_UnloadBase(&s_baseCylinderGPU);
}

bool IsTsunamiSurgeSkillCoiling(void) { return false; }

int GetTsunamiSurgeSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles) {
    int count = 0;
    for (int i = 0; i < MAX_INSTANCES && count < maxProjectiles; i++) {
        if (!s_instances[i].active || s_instances[i].state != STATE_SURGING) continue;
        outProjectiles[count].position = s_instances[i].currentPos;
        outProjectiles[count].radius = s_instances[i].waveRadius;
        outProjectiles[count].active = true;
        count++;
    }
    return count;
}

void DeactivateTsunamiSurgeProjectile(int index) {
    if (index >= 0 && index < MAX_INSTANCES) {
        s_instances[index].state = STATE_IMPACT;
        s_instances[index].stateTimer = 0.0f;
    }
}