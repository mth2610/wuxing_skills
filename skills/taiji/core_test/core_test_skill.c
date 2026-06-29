#include "skills/taiji/core_test/core_test_skill.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "raymath.h"
#include "rlgl.h"
#include "core/resource_manager.h"
#include "core/particle_system.h"
#include "core/particle_radial_burst.h"
#include "core/force_field.h"
#include "core/vfx_light.h"
#include "core/screen_distort.h"
#include "core/procedural_mesh_utils.h"
#include "core/color_gradient.h"
#include "core/camera_fx.h"
#include "core/skill_manager.h"

#ifndef PI
#define PI 3.1415926535f
#endif

// ── Hằng số ──────────────────────────────────────────────────────
#define MAX_ORBS        4
#define ORB_RADIUS      16.0f
#define ORB_LIFETIME    7.0f
#define DISSOLVE_AT     5.5f   // bắt đầu dissolve khi còn lại N giây

// ── State ─────────────────────────────────────────────────────────
typedef struct {
    bool     active;
    Vector3  position;
    float    lifetime;
    float    dissolve;   // [0..1] — u_dissolve gửi xuống shader
    float    sizeScale;
    ForceField vortex;
} CoreOrb;

static CoreOrb s_orbs[MAX_ORBS];

static Shader  s_shader;
static int     s_dissolveLoc;
static int     s_phaseLoc;

// Gradient chung cho hạt orbiting
static ColorGradient s_orbGrad;

extern Camera3D camera;

// ── Init ──────────────────────────────────────────────────────────
void InitCoreTestSkill(int screenWidth, int screenHeight) {
    (void)screenWidth; (void)screenHeight;

    s_shader = ResourceManager_LoadShader(
        "skills/taiji/core_test/core_test.vs",
        "skills/taiji/core_test/core_test.fs"
    );
    s_dissolveLoc = GetShaderLocation(s_shader, "u_dissolve");
    s_phaseLoc    = GetShaderLocation(s_shader, "u_phase");

    ColorGradient_StandardFade(&s_orbGrad, ELEMENT_COLOR_TAIJI, 0.3f, 0.6f);

    for (int i = 0; i < MAX_ORBS; i++) s_orbs[i].active = false;
}

// ── Cast ──────────────────────────────────────────────────────────
void CastCoreTestSkill(Vector3 startPos, Vector3 target, SkillParams params) {
    (void)startPos;

    int slot = -1;
    for (int i = 0; i < MAX_ORBS; i++) {
        if (!s_orbs[i].active) { slot = i; break; }
    }
    if (slot == -1) return;

    CoreOrb *o = &s_orbs[slot];
    o->active    = true;
    o->position  = target;
    o->lifetime  = ORB_LIFETIME;
    o->dissolve  = 0.0f;
    o->sizeScale = (params.sizeScale > 0.0f) ? params.sizeScale : 1.0f;

    // ForceField: vortex xoáy + hút vào tâm + drag
    ForceField_Clear(&o->vortex);
    ForceField_AddLayer(&o->vortex, (ForceLayer){
        .type      = FORCE_VORTEX,
        .origin    = target,
        .direction = (Vector3){0.0f, 1.0f, 0.0f},
        .strength  = 380.0f,
        .radius    = ORB_RADIUS * 5.0f * o->sizeScale,
        .falloff   = 0.0f,
    });
    ForceField_AddLayer(&o->vortex, (ForceLayer){
        .type     = FORCE_GRAVITY_POINT,
        .origin   = target,
        .strength = 220.0f,
        .radius   = ORB_RADIUS * 4.5f * o->sizeScale,
        .falloff  = 1.0f,
    });
    ForceField_AddLayer(&o->vortex, (ForceLayer){
        .type = FORCE_DRAG, .strength = 1.2f
    });

    // TEST WindZone: gió xoáy nhẹ tự động áp lên toàn bộ particle
    WindZone_Set((Vector3){0.3f, 0.7f, 0.2f}, 90.0f, 40.0f, 0.016f);

    // Radial burst ban đầu
    static ParticleRadialBurstConfig burst = {0};
    burst.countMin    = 40; burst.countMax    = 60;
    burst.speedMin    = 120.0f; burst.speedMax = 250.0f;
    burst.radiusMin   = 3.0f;  burst.radiusMax = 6.0f;
    burst.lifetimeMin = 1.8f;  burst.lifetimeMax = 3.0f;
    burst.pitchRange  = 60.0f; burst.upwardBias  = 30.0f;
    burst.gradient    = &s_orbGrad;
    burst.forceField  = &o->vortex;
    ParticleSystem_SpawnRadialBurst(target, o->sizeScale, &burst);

    // Flash: light + distort + camera shake
    VFXLight_Spawn(target, ELEMENT_COLOR_TAIJI, 100.0f * o->sizeScale, 0.5f);
    ScreenDistort_Add(target, 90.0f * o->sizeScale, 0.15f, 0.6f, 200.0f);
    CameraFX_Shake(0.35f);
}

// ── Update ────────────────────────────────────────────────────────
void UpdateCoreTestSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    (void)enemyPos; (void)enemyRadius;

    bool anyActive = false;

    for (int i = 0; i < MAX_ORBS; i++) {
        CoreOrb *o = &s_orbs[i];
        if (!o->active) continue;
        anyActive = true;

        o->lifetime -= dt;

        // Dissolve tiến dần theo ease-in
        float dissolveWindow = ORB_LIFETIME - DISSOLVE_AT;
        if (o->lifetime < dissolveWindow) {
            float t  = 1.0f - (o->lifetime / dissolveWindow);
            o->dissolve = t * t;
        }

        if (o->lifetime <= 0.0f) {
            o->active = false;
            continue;
        }

        // Spawn hạt orbiting liên tục (WindZone auto-apply được test ở đây)
        static ParticleConfig orb = {0};
        orb.colorStart  = ELEMENT_COLOR_TAIJI;
        orb.colorEnd    = (Color){60, 10, 120, 0};
        orb.gradient    = &s_orbGrad;
        orb.radius      = 2.0f + (float)GetRandomValue(0, 25) / 10.0f;
        orb.lifetime    = 1.0f + (float)GetRandomValue(0, 80) / 100.0f;
        orb.forceField  = &o->vortex;

        float ang = GetTime() * 2.5f + (float)i * (PI * 0.5f);
        float r   = ORB_RADIUS * o->sizeScale * (1.6f + (float)GetRandomValue(-20, 40) / 100.0f);
        orb.position = (Vector3){
            o->position.x + cosf(ang) * r,
            o->position.y + (float)GetRandomValue(0, 30),
            o->position.z + sinf(ang) * r,
        };
        orb.velocity = (Vector3){
            sinf(ang) * 100.0f,
            (float)GetRandomValue(50, 160),
            -cosf(ang) * 100.0f,
        };
        SpawnParticle(orb);

        // Ánh sáng bập bùng giữ sống suốt phase active
        float pulse = 50.0f + sinf(GetTime() * 5.0f) * 18.0f;
        VFXLight_Spawn(o->position, ELEMENT_COLOR_TAIJI,
                       pulse * o->sizeScale, 0.05f);
    }

    // Tắt WindZone khi không còn orb nào active
    if (!anyActive && WindZone_IsActive()) {
        WindZone_Clear();
    }
}

// ── Draw ──────────────────────────────────────────────────────────
void DrawCoreTestSkill(void) {
    for (int i = 0; i < MAX_ORBS; i++) {
        CoreOrb *o = &s_orbs[i];
        if (!o->active) continue;

        float phase = 1.0f - (o->lifetime / ORB_LIFETIME);

        // Gửi uniforms skill-specific SAU BeginShader
        SkillManager_BeginShader(s_shader);
        SetShaderValue(s_shader, s_dissolveLoc, &o->dissolve, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_shader, s_phaseLoc,    &phase,       SHADER_UNIFORM_FLOAT);

        BeginBlendMode(BLEND_ALPHA);
        rlDisableBackfaceCulling();
        rlColor4ub(255, 255, 255, 255);

        float r = ORB_RADIUS * o->sizeScale;
        DrawCoreSphere(o->position, r, 24, 24, WHITE);

        rlEnableBackfaceCulling();
        EndBlendMode();
        SkillManager_EndShader();
    }
}

// ── Unload ────────────────────────────────────────────────────────
void UnloadCoreTestSkill(void) {
    WindZone_Clear();
    for (int i = 0; i < MAX_ORBS; i++) s_orbs[i].active = false;
    // ResourceManager tự unload shader khi shutdown
}

// ── Stubs ─────────────────────────────────────────────────────────
bool IsCoreTestSkillCoiling(void) { return false; }

int GetCoreTestSkillProjectiles(SkillProjectile *out, int max) {
    (void)out; (void)max;
    return 0;
}

void DeactivateCoreTestProjectile(int index) { (void)index; }
