#include "skills/water/frost_blossom_rain_skill/frost_blossom_rain_skill.h"
#include "core/resource_manager.h"
#include "core/particle_system.h"
#include "core/color_gradient.h"
#include "core/decal_system.h"
#include "core/vfx_light.h"
#include "core/procedural_mesh_utils.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#define MAX_STORMS 16
#define MAX_BLOSSOMS_PER_STORM 24
#define STORM_RADIUS 26.0f

typedef struct {
    Vector3 position;
    Vector3 velocity;
    float size;
    float rotation;
    float rotSpeed;
    bool active;
} RainBlossom;

typedef struct {
    Vector3 center;
    float duration;
    float maxDuration;
    float spawnTimer;
    float damageTimer;
    float scale;
    float damage;
    float knockback;
    RainBlossom blossoms[MAX_BLOSSOMS_PER_STORM];
    bool active;
} FrostBlossomStorm;

static FrostBlossomStorm s_storms[MAX_STORMS];
static Texture2D s_causticsTex;
static Texture2D s_crackTex;
static Shader s_shader;
static int s_uTimeLoc, s_uViewPosLoc, s_uDissolveLoc;
static ColorGradient s_snowGrad;
static ForceField s_snowField;

extern Camera3D camera; // Dùng camera góc nhìn của engine

void InitFrostBlossomRainSkill(int w, int h) {
    s_causticsTex = ResourceManager_LoadTexture("assets/textures/water_caustics.png");
    s_crackTex = ResourceManager_LoadTexture("assets/textures/crack.png");

    s_shader = ResourceManager_LoadShader(
        "skills/water/frost_blossom_rain_skill/frost_blossom_rain.vs",
        "skills/water/frost_blossom_rain_skill/frost_blossom_rain.fs"
    );

    s_uTimeLoc = GetShaderLocation(s_shader, "u_time");
    s_uViewPosLoc = GetShaderLocation(s_shader, "viewPos");
    s_uDissolveLoc = GetShaderLocation(s_shader, "u_dissolve");

    // Dọn dẹp trạng thái
    for (int i = 0; i < MAX_STORMS; i++) {
        s_storms[i].active = false;
        for (int j = 0; j < MAX_BLOSSOMS_PER_STORM; j++) {
            s_storms[i].blossoms[j].active = false;
        }
    }

    // Gradient màu tuyết rơi (trắng lam nhạt sang xanh nhạt trong suốt)
    s_snowGrad.count = 0;
    ColorGradient_AddStop(&s_snowGrad, 0.00f, (Color){ 255, 255, 255, 255 });
    ColorGradient_AddStop(&s_snowGrad, 0.20f, (Color){ 200, 235, 255, 255 });
    ColorGradient_AddStop(&s_snowGrad, 0.60f, (Color){ 130, 210, 255, 180 });
    ColorGradient_AddStop(&s_snowGrad, 1.00f, (Color){ 41, 128, 185, 0 });

    // Cấu hình trường lực tuyết rơi lả lướt (Gió thổi ngang nhẹ + Curl Noise + Trọng lực rơi chậm)
    ForceField_Clear(&s_snowField);
    // Gió ngang thổi nhẹ góc chéo
    ForceField_AddLayer(&s_snowField, (ForceLayer){
        .type = FORCE_WIND,
        .direction = { 0.5f, 0.0f, 0.2f },
        .strength = 18.0f
    });
    // Trọng lực kéo hạt tuyết rơi xuống chậm rãi mềm mại
    ForceField_AddLayer(&s_snowField, (ForceLayer){
        .type = FORCE_GRAVITY_DIR,
        .direction = { 0.0f, -1.0f, 0.0f },
        .strength = 15.0f
    });
    // Curl noise tạo chuyển động uốn éo lơ lửng của bông tuyết
    ForceField_AddLayer(&s_snowField, (ForceLayer){
        .type = FORCE_NOISE_CURL,
        .strength = 28.0f,
        .noiseScale = 0.08f,
        .noiseSpeed = 2.5f
    });
    s_snowField.layerCount = 3;
}

void CastFrostBlossomRainSkill(Vector3 start, Vector3 target, SkillParams p) {
    for (int i = 0; i < MAX_STORMS; i++) {
        if (!s_storms[i].active) {
            float sizeScale = (p.sizeScale > 0.0f) ? p.sizeScale : 1.0f;
            
            s_storms[i].center = target;
            s_storms[i].duration = 6.5f; // Tăng thời gian bão tuyết lên 6.5 giây để tuyết rơi lơ lửng kịp chạm đất
            s_storms[i].maxDuration = 6.5f;
            s_storms[i].spawnTimer = 0.0f;
            s_storms[i].damageTimer = 0.0f;
            s_storms[i].scale = sizeScale;
            // Sát thương yếu như chiêu Thúy Yên nhưng dồn sát thương liên tục
            s_storms[i].damage = Skill_CalculateDamage(SKILL_CAT_AOE_CONTROL, p);
            s_storms[i].knockback = Skill_CalculateKnockback(SKILL_CAT_AOE_CONTROL, p);
            s_storms[i].active = true;

            for (int j = 0; j < MAX_BLOSSOMS_PER_STORM; j++) {
                s_storms[i].blossoms[j].active = false;
            }

            // Spawn một vụ nổ ánh sáng băng giá nhẹ báo hiệu cast chiêu
            VFXLight_Spawn(target, (Color){ 100, 200, 255, 255 }, STORM_RADIUS * sizeScale * 1.5f, 0.8f);

            // Sinh Decal sương gió tuyết trắng mờ ảo dưới chân
            Vector3 decalPos = { target.x, target.y + 0.02f, target.z };
            DecalSystem_Add(decalPos, (float)(rand() % 360), STORM_RADIUS * sizeScale * 2.4f, ResourceManager_LoadTexture("assets/textures/dust_wind.png"), 6.5f, ColorAlpha(WHITE, 0.16f));
            break;
        }
    }
}

void UpdateFrostBlossomRainSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    float time = (float)GetTime();

    for (int i = 0; i < MAX_STORMS; i++) {
        if (!s_storms[i].active) continue;
        FrostBlossomStorm *storm = &s_storms[i];

        storm->duration -= dt;
        if (storm->duration <= 0.0f) {
            storm->active = false;
            continue;
        }

        // 1. Gây sát thương dồn liên tục (tick rate 0.22 giây một lần) + làm chậm kẻ địch
        storm->damageTimer -= dt;
        if (storm->damageTimer <= 0.0f) {
            float distSqr = Vector3DistanceSqr(storm->center, enemyPos);
            float checkRadius = STORM_RADIUS * storm->scale;
            if (distSqr <= checkRadius * checkRadius) {
                // Sát thương tick yếu (15% sát thương cơ bản mỗi lần chạm)
                ApplyAoEDamage(storm->center, checkRadius, storm->damage * 0.15f, storm->knockback * 0.05f);
                // Khống chế làm chậm (Slow debuff 2.5 giây)
                AddSlowToEnemy(2.5f);
            }
            storm->damageTimer = 0.22f;
        }

        // 2. Spawn các bông hoa lê tuyết rơi từ trên trời xuống xối xả
        storm->spawnTimer -= dt;
        if (storm->spawnTimer <= 0.0f) {
            for (int j = 0; j < MAX_BLOSSOMS_PER_STORM; j++) {
                if (!storm->blossoms[j].active) {
                    float rDist = (float)rand() / (float)RAND_MAX * (STORM_RADIUS * storm->scale * 0.95f);
                    float rAngle = (float)rand() / (float)RAND_MAX * 2.0f * PI;

                    Vector3 spawnPos = {
                        storm->center.x + cosf(rAngle) * rDist,
                        storm->center.y + (float)(rand() % 40 + 140), // Nâng độ cao rơi cực đại lên 140 - 180 unit (đứng từ trên trời rơi xuống)
                        storm->center.z + sinf(rAngle) * rDist
                    };

                    storm->blossoms[j].position = spawnPos;
                    // Chao chao xiên xiên tự nhiên theo gió chéo nhẹ
                    float xWind = ((float)rand() / (float)RAND_MAX - 0.5f) * 4.5f;
                    float zWind = ((float)rand() / (float)RAND_MAX - 0.5f) * 4.5f;
                    // Vận tốc rơi 45-65 unit/s để bông tuyết rơi từ 160 unit xuống đất mất ~3 giây
                    storm->blossoms[j].velocity = (Vector3){ xWind, -((float)(rand() % 20 + 45)), zWind }; 
                    
                    // Bông tuyết xen kẽ to nhỏ phong phú
                    storm->blossoms[j].size = ((float)rand() / (float)RAND_MAX * 1.35f + 0.25f) * storm->scale; 
                    storm->blossoms[j].rotation = (float)rand() / (float)RAND_MAX * 360.0f;
                    storm->blossoms[j].rotSpeed = ((float)rand() / (float)RAND_MAX * 90.0f - 45.0f);
                    storm->blossoms[j].active = true;
                    break;
                }
            }
            // Rơi thong thả rải rác từng bông chứ không ồ ạt
            storm->spawnTimer = 0.26f;
        }

        // 3. Cập nhật chuyển động từng bông hoa tuyết rơi
        for (int j = 0; j < MAX_BLOSSOMS_PER_STORM; j++) {
            if (!storm->blossoms[j].active) continue;
            RainBlossom *b = &storm->blossoms[j];

            b->position = Vector3Add(b->position, Vector3Scale(b->velocity, dt));
            b->rotation += b->rotSpeed * dt;

            // Spawn hạt bụi tuyết nhỏ rơi sau lưng tạo vệt đuôi khí động học (Accent Trails)
            if (rand() % 100 < 45) {
                Vector3 tOffset = {
                    b->position.x + ((float)rand() / (float)RAND_MAX - 0.5f) * b->size * 0.5f,
                    b->position.y + 0.8f,
                    b->position.z + ((float)rand() / (float)RAND_MAX - 0.5f) * b->size * 0.5f
                };

                Vector3 pVel = {
                    ((float)rand() / (float)RAND_MAX - 0.5f) * 8.0f,
                    -b->velocity.y * 0.15f, // tạt nhẹ ngược chiều bay
                    ((float)rand() / (float)RAND_MAX - 0.5f) * 8.0f
                };

                ParticleConfig pCfg = {
                    .position = tOffset,
                    .velocity = pVel,
                    .radius = ((float)rand() / (float)RAND_MAX * 0.8f + 0.3f) * storm->scale * 2.2f,
                    .lifetime = (float)rand() / (float)RAND_MAX * 0.5f + 0.3f,
                    .forceField = &s_snowField,
                    .gradient = &s_snowGrad
                };
                SpawnParticle(pCfg);
            }

            // Va chạm đất (Y <= 0.0f)
            if (b->position.y <= 0.0f) {
                Vector3 hitPos = { b->position.x, 0.01f, b->position.z };

                // Tạo chớp sáng nổ băng nhỏ dưới đất
                VFXLight_Spawn(hitPos, ELEMENT_COLOR_WATER, b->size * 5.2f, 0.35f);

                // Không dùng decal nứt đất nham nhở lúc tuyết rơi chạm đất để giữ mặt đất sạch sẽ tự nhiên
                // Chỉ nổ chớp sáng và văng vụn tuyết là đủ đẹp mắt
                rlColor4ub(255, 255, 255, 255);

                // Sinh ra các hạt vụn băng văng tung tóe
                int debrisCount = rand() % 3 + 4;
                for (int k = 0; k < debrisCount; k++) {
                    float a = (float)k / (float)debrisCount * 2.0f * PI;
                    float speed = (float)(rand() % 25 + 20) * storm->scale;

                    Vector3 dVel = {
                        cosf(a) * speed,
                        (float)(rand() % 35 + 25) * storm->scale,
                        sinf(a) * speed
                    };

                    ParticleConfig dCfg = {
                        .position = hitPos,
                        .velocity = dVel,
                        .radius = ((float)rand() / (float)RAND_MAX * 0.9f + 0.4f) * storm->scale * 2.6f,
                        .lifetime = (float)rand() / (float)RAND_MAX * 0.6f + 0.4f,
                        .forceField = &s_snowField,
                        .gradient = &s_snowGrad
                    };
                    SpawnParticle(dCfg);
                }

                b->active = false; // Thu hồi bông tuyết rơi
            }
        }

        // 4. Sinh các bông tuyết bay bão lơ lửng xung quanh vùng ảnh hưởng cao vút của cơn bão
        if (rand() % 100 < 35) {
            float rAngle = (float)rand() / (float)RAND_MAX * 2.0f * PI;
            float rDist = (float)rand() / (float)RAND_MAX * STORM_RADIUS * storm->scale;
            Vector3 pPos = {
                storm->center.x + cosf(rAngle) * rDist,
                storm->center.y + (float)(rand() % 168 + 2.0f), // Phủ hạt tuyết lơ lửng cao đến 170 unit (bao trọn không gian đứng khổng lồ)
                storm->center.z + sinf(rAngle) * rDist
            };

            Vector3 pVel = {
                -sinf(rAngle) * 12.0f * storm->scale,
                -8.0f - (float)(rand() % 10),
                cosf(rAngle) * 12.0f * storm->scale
            };

            ParticleConfig auraCfg = {
                .position = pPos,
                .velocity = pVel,
                .radius = ((float)rand() / (float)RAND_MAX * 1.2f + 0.4f) * storm->scale * 2.5f,
                .lifetime = (float)rand() / (float)RAND_MAX * 1.2f + 0.8f,
                .forceField = &s_snowField,
                .gradient = &s_snowGrad
            };
            SpawnParticle(auraCfg);
        }
    }
}

void DrawFrostBlossomRainSkill(void) {
    float time = (float)GetTime();

    rlDisableDepthMask();
    SkillManager_BeginShader(s_shader);

    SetShaderValue(s_shader, s_uTimeLoc, &time, SHADER_UNIFORM_FLOAT);
    
    // Đồng bộ camera vị trí cho tính toán Fresnel pha lê băng
    float cameraPos[3] = { camera.position.x, camera.position.y, camera.position.z };
    SetShaderValue(s_shader, s_uViewPosLoc, cameraPos, SHADER_UNIFORM_VEC3);

    for (int i = 0; i < MAX_STORMS; i++) {
        if (!s_storms[i].active) continue;
        FrostBlossomStorm *storm = &s_storms[i];

        // Độ tan rã của bão khi sắp tan
        float dissolve = 0.0f;
        if (storm->duration < 0.6f) {
            dissolve = 1.0f - (storm->duration / 0.6f);
        }
        SetShaderValue(s_shader, s_uDissolveLoc, &dissolve, SHADER_UNIFORM_FLOAT);

        // Vẽ các đóa hoa tuyết băng rơi xuống bằng Procedural Meshes
        for (int j = 0; j < MAX_BLOSSOMS_PER_STORM; j++) {
            if (!storm->blossoms[j].active) continue;
            RainBlossom *b = &storm->blossoms[j];

            rlPushMatrix();
            rlTranslatef(b->position.x, b->position.y, b->position.z);
            // Xoay tự do theo 3 hướng để bông tuyết chao chao chao lượn lơ lửng trong không trung
            rlRotatef(b->rotation, 0.3f, 0.8f, 0.5f);
            
            // ĐẶT LẠI MÀU ĐỈNH LÀM SẠCH (Rule 7: Reset Vertex Colors)
            rlColor4ub(255, 255, 255, 255);

            // Vẽ bông tuyết dẹt lục giác
            DrawCorePlanePolygon((Vector3){0, 0, 0}, b->size, 6, WHITE);

            rlPopMatrix();
        }
    }

    SkillManager_EndShader();
    rlEnableDepthMask();
}

void UnloadFrostBlossomRainSkill(void) {
    // Tài nguyên tự động được ResourceManager giải phóng
}

int GetFrostBlossomRainSkillProjectiles(SkillProjectile *out, int max) {
    int count = 0;
    for (int i = 0; i < MAX_STORMS; i++) {
        if (!s_storms[i].active) continue;
        for (int j = 0; j < MAX_BLOSSOMS_PER_STORM; j++) {
            if (s_storms[i].blossoms[j].active && count < max) {
                out[count].position = s_storms[i].blossoms[j].position;
                out[count].radius = s_storms[i].blossoms[j].size;
                out[count].active = true;
                count++;
            }
        }
    }
    return count;
}

void DeactivateFrostBlossomRainProjectile(int index) {
    // Tự động giải trừ một bông tuyết cụ thể khi va chạm ngoài tầm
    int count = 0;
    for (int i = 0; i < MAX_STORMS; i++) {
        if (!s_storms[i].active) continue;
        for (int j = 0; j < MAX_BLOSSOMS_PER_STORM; j++) {
            if (s_storms[i].blossoms[j].active) {
                if (count == index) {
                    s_storms[i].blossoms[j].active = false;
                    return;
                }
                count++;
            }
        }
    }
}
