#include "skills/water/tsunami_skill/tsunami_skill.h"
#include "core/skill_manager.h"
#include "core/resource_manager.h"
#include "core/particle_system.h"
#include "core/procedural_mesh_utils.h"
#include "core/skill_helper.h"
#include "core/force_field.h"
#include "core/vfx_light.h"
#include "core/camera_fx.h"
#include "core/impact_burst.h"
#include "core/decal_system.h"
#include "core/utils_math.h"
#include "core/flow_map.h"
#include "rlgl.h"
#include "raymath.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef PI
#define PI 3.1415926535f
#endif

#define MAX_TSUNAMI_INSTANCES 4

typedef enum {
    STATE_INACTIVE,
    STATE_CASTING,
    STATE_MOVING,
    STATE_IMPACT,
    STATE_DISSOLVE
} TsunamiState;

typedef struct {
    TsunamiState state;
    Vector3 startPos;
    Vector3 targetPos;
    Vector3 currentPos;
    Vector3 direction;
    Vector3 perp;
    
    float timer;
    float castDuration;
    float dissolveTimer;
    
    float damage;
    float speed;
    float width;
    float height;
    float curlAmount;
    float lastEmitterTime;
    
    CurlingWaveMeshData waveMesh;
    WavePlaneMeshData poolMesh;
} TsunamiInstance;

static TsunamiInstance s_instances[MAX_TSUNAMI_INSTANCES];
static Shader s_shader;
static int s_locDissolve;
static int s_locBaseColor;
static int s_locEmissiveColor;
static FlowMap s_flowMap;
static ForceField s_forceField;

void InitTsunamiSkill(int screenWidth, int screenHeight) {
    s_shader = ResourceManager_LoadShader("skills/water/tsunami_skill/tsunami.vs", 
                                          "skills/water/tsunami_skill/tsunami.fs");
    
    if (s_shader.id != 0) {
        s_locDissolve = GetShaderLocation(s_shader, "u_dissolve");
        s_locBaseColor = GetShaderLocation(s_shader, "u_baseColor");
        s_locEmissiveColor = GetShaderLocation(s_shader, "u_emissiveColor");
    }
    
    s_flowMap = FlowMap_CreateWithVortexTexture(s_shader, 256, "u_time");
    ForceField_Clear(&s_forceField);
    
    for (int i = 0; i < MAX_TSUNAMI_INSTANCES; i++) {
        s_instances[i].state = STATE_INACTIVE;
    }
}

void CastTsunamiSkill(Vector3 startPos, Vector3 target, SkillParams params) {
    int idx = -1;
    for (int i = 0; i < MAX_TSUNAMI_INSTANCES; i++) {
        if (s_instances[i].state == STATE_INACTIVE) {
            idx = i;
            break;
        }
    }
    if (idx == -1) return;
    
    TsunamiInstance *inst = &s_instances[idx];
    inst->state = STATE_CASTING;
    inst->startPos = startPos;
    inst->currentPos = startPos;
    inst->targetPos = target;
    
    Vector3 dir = Vector3Subtract(target, startPos);
    dir.y = 0.0f;
    float dist = Vector3Length(dir);
    if (dist > 0.001f) {
        inst->direction = Vector3Scale(dir, 1.0f / dist);
    } else {
        inst->direction = (Vector3){0, 0, 1};
    }
    
    // Vector vuông góc để xác định chiều rộng sóng
    inst->perp = (Vector3){-inst->direction.z, 0.0f, inst->direction.x};
    
    inst->timer = 0.0f;
    inst->castDuration = 0.6f;
    inst->dissolveTimer = 0.0f;
    
    inst->damage = params.damage;
    inst->speed = 85.0f;
    inst->width = 80.0f;
    inst->height = 32.0f;
    inst->curlAmount = 0.8f;
    inst->lastEmitterTime = 0.0f;
    
    // Spawn VFX tụ lực ban đầu
    SpawnCastEffect(startPos, EFFECT_PRESET_WATER_SPLASH, 2.5f);
    VFXLight_Spawn(startPos, ELEMENT_COLOR_WATER, 60.0f, inst->castDuration);
}

void UpdateTsunamiSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    ForceField_Clear(&s_forceField);
    
    for (int i = 0; i < MAX_TSUNAMI_INSTANCES; i++) {
        TsunamiInstance *inst = &s_instances[i];
        if (inst->state == STATE_INACTIVE) continue;
        
        inst->timer += dt;
        
        if (inst->state == STATE_CASTING) {
            // ForceField hút hạt nước về tâm
            ForceField_AddLayer(&s_forceField, (ForceLayer){
                .type = FORCE_VORTEX,
                .origin = inst->startPos,
                .direction = (Vector3){0, 1, 0},
                .strength = 45.0f,
                .radius = 60.0f,
                .falloff = 1.0f
            });
            
            // Spawn mist & water droplets tụ lực
            ParticleConfig p = {0};
            p.position = Vector3Add(inst->startPos, (Vector3){GetRandomValue(-40, 40), 5.0f, GetRandomValue(-40, 40)});
            p.velocity = (Vector3){0, (float)GetRandomValue(5, 20), 0};
            p.colorStart = WHITE; 
            p.colorEnd = ELEMENT_COLOR_WATER;
            p.radius = (float)GetRandomValue(3, 10) * 0.1f;
            p.lifetime = 0.4f;
            p.forceField = &s_forceField;
            SpawnParticle(p);
            
            // Build mặt nước cuộn trào tại vị trí cast
            WavePlaneConfig poolCfg = ProceduralMesh_DefaultWavePlaneConfig();
            poolCfg.amplitude = Math_Mix(0.0f, 3.0f, SmoothStep01(inst->timer / inst->castDuration));
            ProceduralMesh_BuildWavePlane(&inst->poolMesh, inst->startPos, inst->width, inst->width, 
                                          24, 24, inst->timer, &poolCfg);
            
            if (inst->timer >= inst->castDuration) {
                inst->state = STATE_MOVING;
            }
        } 
        else if (inst->state == STATE_MOVING) {
            // Di chuyển sóng
            inst->currentPos = Vector3Add(inst->currentPos, Vector3Scale(inst->direction, inst->speed * dt));
            
            // Build Main Wave (Mesh cập nhật mỗi frame)
            CurlingWaveConfig waveCfg = ProceduralMesh_DefaultCurlingWaveConfig();
            waveCfg.height = inst->height;
            waveCfg.archWidth = inst->width;
            waveCfg.curlAmount = inst->curlAmount;
            ProceduralMesh_BuildCurlingWave(&inst->waveMesh, inst->currentPos, inst->perp, &waveCfg, 16, 32);
            
            // Build Base Pool chạy theo chân sóng
            WavePlaneConfig poolCfg = ProceduralMesh_DefaultWavePlaneConfig();
            poolCfg.amplitude = 4.0f;
            ProceduralMesh_BuildWavePlane(&inst->poolMesh, inst->currentPos, inst->width + 20.0f, 40.0f, 
                                          24, 12, inst->timer, &poolCfg);
                                          
            // ----------------------------------------------------
            // HỆ THỐNG PARTICLE KHI SÓNG DI CHUYỂN
            // ----------------------------------------------------
            
            // 1. Chân sóng (Base): Để lại wake bọt trắng bằng Emitter
            if (inst->timer - inst->lastEmitterTime > 0.15f) {
                Emitter_AttachToPoint(EMITTER_WATER_SPURT, inst->currentPos, 40.0f, 0.6f);
                inst->lastEmitterTime = inst->timer;
            }
            
            // 2. Chân sóng (Front Splash): Bắn liên tục lên trên/phía trước
            ParticleRadialBurstConfig baseBurst = {
                .countMin = 4, .countMax = 8,
                .speedMin = 40.0f, .speedMax = 90.0f,
                .radiusMin = 1.5f, .radiusMax = 4.0f,
                .lifetimeMin = 0.3f, .lifetimeMax = 0.6f,
                .pitchRange = 35.0f, .upwardBias = 1.2f,
                .colorStart = WHITE, .colorEnd = ELEMENT_COLOR_WATER
            };
            Vector3 frontBase = Vector3Add(inst->currentPos, Vector3Scale(inst->direction, 10.0f));
            frontBase = Vector3Add(frontBase, Vector3Scale(inst->perp, (float)GetRandomValue(-35, 35)));
            ParticleSystem_SpawnRadialBurst(frontBase, 1.0f, &baseBurst);
            
            // 3. Đỉnh sóng (Crest): Dọc theo mép cong
            float wOff = (float)GetRandomValue((int)(-inst->width * 0.45f), (int)(inst->width * 0.45f));
            Vector3 crestPos = Vector3Add(inst->currentPos, Vector3Scale(inst->perp, wOff));
            crestPos.y += inst->height;
            crestPos = Vector3Add(crestPos, Vector3Scale(inst->direction, inst->height * inst->curlAmount * 0.5f)); 
            
            ParticleConfig crestP = {0};
            crestP.position = crestPos;
            crestP.velocity = Vector3Add(Vector3Scale(inst->direction, 80.0f), (Vector3){0, -10.0f, 0});
            crestP.colorStart = WHITE;
            crestP.colorEnd = ColorAlpha(ELEMENT_COLOR_WATER, 0.5f);
            crestP.radius = (float)GetRandomValue(3, 7);
            crestP.lifetime = 0.45f;
            SpawnParticle(crestP);
            
            // 4. Mép sóng (Edges): Bắn nước sang hai bên
            for (int side = -1; side <= 1; side += 2) {
                ParticleConfig edgeP = {0};
                edgeP.position = Vector3Add(inst->currentPos, Vector3Scale(inst->perp, side * (inst->width * 0.5f)));
                edgeP.velocity = Vector3Add(Vector3Scale(inst->perp, side * 120.0f), (Vector3){0, 40.0f, 0});
                edgeP.colorStart = WHITE; 
                edgeP.colorEnd = ELEMENT_COLOR_WATER;
                edgeP.radius = (float)GetRandomValue(2, 5);
                edgeP.lifetime = 0.5f;
                SpawnParticle(edgeP);
            }
            
            // Va chạm
            if (Vector3Distance(inst->currentPos, inst->targetPos) < 15.0f || inst->timer > 3.0f) {
                inst->state = STATE_IMPACT;
            }
        } 
        else if (inst->state == STATE_IMPACT) {
            // AAA Impact Burst
            ImpactBurstConfig impactCfg = {
                .distortEnabled = true, .distortRadius = 120.0f, .distortStrength = 0.25f, .distortLife = 0.7f, .distortSpeed = 40.0f,
                .decalEnabled = false, // Sẽ dùng manual SpawnGroundDecal để dễ control preset
                .lightEnabled = true, .lightColor = ELEMENT_COLOR_WATER, .lightRadius = 150.0f, .lightLife = 0.6f,
                .particlesEnabled = true,
                .particles = {
                    .countMin = 40, .countMax = 70,
                    .speedMin = 100.0f, .speedMax = 250.0f, // Thuộc range ambient/burst cho impact
                    .radiusMin = 2.0f, .radiusMax = 8.0f,
                    .lifetimeMin = 0.5f, .lifetimeMax = 1.2f,
                    .pitchRange = 90.0f, .upwardBias = 1.0f,
                    .colorStart = WHITE, .colorEnd = ELEMENT_COLOR_WATER
                }
            };
            
            VFX_TriggerImpactBurst(inst->currentPos, 1.0f, &impactCfg);
            SpawnGroundDecal(DECAL_PRESET_WATER_SPLASH, inst->currentPos, 110.0f, 4.0f);
            
            ApplyAoEDamage(inst->currentPos, inst->width * 0.6f, inst->damage, 150.0f);
            CameraFX_Shake(0.8f);
            PlayImpactSound(EFFECT_PRESET_WATER_SPLASH);
            
            inst->state = STATE_DISSOLVE;
            inst->dissolveTimer = 0.0f;
        } 
        else if (inst->state == STATE_DISSOLVE) {
            inst->dissolveTimer += dt;
            
            // Thu nhỏ vũng nước dần
            WavePlaneConfig poolCfg = ProceduralMesh_DefaultWavePlaneConfig();
            poolCfg.amplitude = Math_Mix(4.0f, 0.0f, inst->dissolveTimer / 1.5f);
            ProceduralMesh_BuildWavePlane(&inst->poolMesh, inst->currentPos, inst->width + 20.0f, 40.0f, 
                                          24, 12, inst->timer, &poolCfg);
                                          
            if (inst->dissolveTimer >= 1.5f) {
                inst->state = STATE_INACTIVE;
            }
        }
    }
}

void DrawTsunamiSkill(void) {
    if (s_shader.id == 0) return;
    
    SkillManager_BeginShader(s_shader);
    
    // Gán đồng phục base color & emissive.
    // alpha gần 1.0: sóng cuộn là mesh hai mặt (không backface culling) nên
    // mặt trước/sau của khúc cuộn chồng lên nhau trong view — alpha thấp
    // (0.8) khiến 2 lớp blend chồng làm cả khối nhìn xuyên thấu rõ rệt.
    Color baseC = ColorAlpha(ELEMENT_COLOR_WATER, 0.97f);
    Vector4 baseColorVec = { baseC.r/255.0f, baseC.g/255.0f, baseC.b/255.0f, baseC.a/255.0f };
    SetShaderValue(s_shader, s_locBaseColor, &baseColorVec, SHADER_UNIFORM_VEC4);
    
    Color emC = WHITE;
    Vector4 emColorVec = { emC.r/255.0f, emC.g/255.0f, emC.b/255.0f, emC.a/255.0f };
    SetShaderValue(s_shader, s_locEmissiveColor, &emColorVec, SHADER_UNIFORM_VEC4);
    
    FlowMap_Apply(&s_flowMap, s_shader, GetTime());
    
    for (int i = 0; i < MAX_TSUNAMI_INSTANCES; i++) {
        TsunamiInstance *inst = &s_instances[i];
        if (inst->state == STATE_INACTIVE) continue;
        
        rlColor4ub(255, 255, 255, 255);
        
        if (inst->state == STATE_CASTING) {
            float dissolve = 0.0f;
            SetShaderValue(s_shader, s_locDissolve, &dissolve, SHADER_UNIFORM_FLOAT);
            ProceduralMesh_DrawWavePlane(&inst->poolMesh, WHITE);
        }
        else if (inst->state == STATE_MOVING) {
            float dissolve = 0.0f;
            SetShaderValue(s_shader, s_locDissolve, &dissolve, SHADER_UNIFORM_FLOAT);
            ProceduralMesh_DrawCurlingWave(&inst->waveMesh, WHITE);
            ProceduralMesh_DrawWavePlane(&inst->poolMesh, WHITE);
        }
        else if (inst->state == STATE_DISSOLVE) {
            // Fade out mượt mà cả thân sóng (tan nhanh) và vũng nước (tan chậm)
            float waveDissolve = SmoothStep01(inst->dissolveTimer / 0.4f);
            float poolDissolve = SmoothStep01(inst->dissolveTimer / 1.5f);
            
            if (waveDissolve < 1.0f) {
                SetShaderValue(s_shader, s_locDissolve, &waveDissolve, SHADER_UNIFORM_FLOAT);
                ProceduralMesh_DrawCurlingWave(&inst->waveMesh, WHITE);
            }
            
            SetShaderValue(s_shader, s_locDissolve, &poolDissolve, SHADER_UNIFORM_FLOAT);
            ProceduralMesh_DrawWavePlane(&inst->poolMesh, WHITE);
        }
    }
    
    SkillManager_EndShader();
}

void UnloadTsunamiSkill(void) {
    // Để trống theo chuẩn kiến trúc, ResourceManager sẽ tự giải phóng Shader
}

bool IsTsunamiSkillCoiling(void) {
    return false;
}

int GetTsunamiSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles) {
    int count = 0;
    for (int i = 0; i < MAX_TSUNAMI_INSTANCES && count < maxProjectiles; i++) {
        if (s_instances[i].state == STATE_MOVING) {
            outProjectiles[count].position = s_instances[i].currentPos;
            outProjectiles[count].radius = s_instances[i].width * 0.45f;
            outProjectiles[count].active = true;
            count++;
        }
    }
    return count;
}

void DeactivateTsunamiProjectile(int index) {
    int count = 0;
    for (int i = 0; i < MAX_TSUNAMI_INSTANCES; i++) {
        if (s_instances[i].state == STATE_MOVING) {
            if (count == index) {
                s_instances[i].state = STATE_IMPACT;
                break;
            }
            count++;
        }
    }
}