#include "core_test_skill.h"
#include "core/composition/visual_composer.h"
#include "core/geometry/procedural_mesh_utils.h"
#include "core/decal_system.h"
#include "raymath.h"
#include "core/vfx_light.h"

#include "core/vfx_proc_ray.h"

// ----------------------------------------------------------------------------
// STATE
// ----------------------------------------------------------------------------
static bool s_active = false;
static float s_time = 0.0f;
static Vector3 s_center = {0};

typedef struct {
    int rayId;
    float timer;
    Vector3 start;
    Vector3 end;
    bool active;
} TestBolt;

#define MAX_TEST_BOLTS 5
static TestBolt s_bolts[MAX_TEST_BOLTS] = {0};

// ----------------------------------------------------------------------------
// LIFECYCLE
// ----------------------------------------------------------------------------

void InitCoreTestSkill(int screenWidth, int screenHeight) {
    s_active = false;
}

void UnloadCoreTestSkill(void) {
    s_active = false;
}

void CastCoreTestSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    s_active = true;
    s_time = 0.0f;
    s_center = target;
    
    for (int i = 0; i < MAX_TEST_BOLTS; i++) {
        s_bolts[i].active = false;
    }
}

void UpdateCoreTestSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    if (!s_active) return;
    
    // Test Group 2: Particle & Effect Spawning (Lặp lại mỗi 2 giây để dễ quan sát)
    if ((int)(s_time / 2.0f) < (int)((s_time + dt) / 2.0f)) {
        // Khói cuộn
        VFX_ComposeSmokePuff(Vector3Add(s_center, (Vector3){-3.5f, 0, 0}), 1.0f);
        // Vệt khói
        VFX_ComposeSmokeTrail(Vector3Add(s_center, (Vector3){-3.5f, 0, 0}), Vector3Add(s_center, (Vector3){-3.5f, 2.5f, 0}), 1.0f);
        // Vết nứt đất dài (đặt ngay trước mặt, chạy thẳng về trước)
        VFX_ComposeFissureStreak(Vector3Add(s_center, (Vector3){-1.5f, 0, 2.0f}), Vector3Add(s_center, (Vector3){-1.5f, 0, 8.0f}), 0.6f);
        
        
        // 1 tia sét duy nhất giáng xuống ở trung tâm
        s_bolts[0].active = true;
        s_bolts[0].timer = 0.5f; // Sống 0.5s
        
        s_bolts[0].end = Vector3Add(s_center, (Vector3){-1.5f, 0.0f, 0.0f});
        s_bolts[0].start = Vector3Add(s_bolts[0].end, (Vector3){0, 10.0f, 0});
        
        s_bolts[0].rayId = SpawnProcBolt(ProcRay_BoltLightningConfig(), 1.0f);
        
        VFXLight_Spawn(s_bolts[0].end, (Color){0, 200, 255, 255}, 4.0f, 0.2f, 0);
    }
    
    for (int i = 0; i < MAX_TEST_BOLTS; i++) {
        if (s_bolts[i].active) {
            s_bolts[i].timer -= dt;
            if (s_bolts[i].timer <= 0.0f) {
                ProcBolt_Kill(s_bolts[i].rayId);
                s_bolts[i].active = false;
            } else {
                ProcBolt_Update(s_bolts[i].rayId, s_bolts[i].start, s_bolts[i].end, 1.0f, dt);
                // Flash mờ dần
                float brightness = s_bolts[i].timer / 0.5f;
                ProcBolt_SetBrightness(s_bolts[i].rayId, brightness);
            }
        }
    }
    
    s_time += dt;
    if (s_time > 15.0f) {
        s_active = false; // Tự động biến mất sau 15s
        for (int i = 0; i < MAX_TEST_BOLTS; i++) {
            if (s_bolts[i].active) ProcBolt_Kill(s_bolts[i].rayId);
        }
    }
}

void DrawCoreTestSkill(void) {
    if (!s_active) return;
    
    float progress = fminf(s_time / 1.0f, 1.0f);
    
    // 1. Cột đá (Giữ lại theo yêu cầu)
    VFX_ComposeStonePillar(Vector3Add(s_center, (Vector3){-1.5f, 0, -2.0f}), progress);
    
    // 6. Cục đá (tròn, không còn lỗi)
    VFX_ComposeBoulder(Vector3Add(s_center, (Vector3){3.5f, 0, -1.0f}));
    
    // 3. Tinh thể băng
    VFX_ComposeIceCrystal(Vector3Add(s_center, (Vector3){-3.5f, 0, 2.0f}), 1337);
    
    // 4. Vũng nước ma thuật
    VFX_ComposeMagicPuddle(Vector3Add(s_center, (Vector3){0, 0, -2.5f}));
    
    // 5. Quả cầu lửa (nhỏ hơn, thấp hơn)
    VFX_ComposeFireball(Vector3Add(s_center, (Vector3){3.5f, 0.0f, 2.0f}), s_time);
    
    // Vẽ mưa sét
    for (int i = 0; i < MAX_TEST_BOLTS; i++) {
        if (s_bolts[i].active) {
            ProcBolt_Draw(s_bolts[i].rayId, camera);
        }
    }
}

void DrawCoreTestSkillDebugHUD(void) {}

bool IsCoreTestSkillCoiling(void) { return false; }
int GetCoreTestSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles) { return 0; }
void DeactivateCoreTestProjectile(int index) {}
void CoreTestSkill_ForceActivate(int agentId, Vector3 spherePos) {}
void CoreTestSkill_TriggerReadback(void) {}
bool CoreTestSkill_GetReadback(int sampleIndex, float *outSceneLinear, float *outFragLinear, float *outDiff) { return false; }
