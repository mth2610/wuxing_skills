// fire.inl — Master include for fire element VFX sub-modules
// Included once by visual_composer.c

static ColorGradient s_fireBodyGrad = {0};  
static ColorGradient s_fireCoreGrad = {0};  
static ColorGradient s_fireSmokeGrad = {0}; 
static SkillCurve s_flameShape = {0};       
static SkillCurve s_smokeShape = {0};       
static bool s_fireGradInit = false;
static ForceField s_flameFld = {0}; 

static void FireFlow_InitShared(void)
{
    if (s_fireGradInit)
        return;

    ColorGradient_AddStop(&s_fireBodyGrad, 0.0f, (Color){255, 235, 170, 235});
    ColorGradient_AddStop(&s_fireBodyGrad, 0.25f, (Color){255, 180, 70, 220});
    ColorGradient_AddStop(&s_fireBodyGrad, 0.6f, (Color){235, 90, 20, 150});
    ColorGradient_AddStop(&s_fireBodyGrad, 0.85f, (Color){130, 35, 12, 60});
    ColorGradient_AddStop(&s_fireBodyGrad, 1.0f, (Color){60, 20, 10, 0});

    ColorGradient_AddStop(&s_fireCoreGrad, 0.0f, (Color){255, 255, 235, 255});
    ColorGradient_AddStop(&s_fireCoreGrad, 0.35f, (Color){255, 230, 130, 230});
    ColorGradient_AddStop(&s_fireCoreGrad, 0.75f, (Color){255, 150, 45, 120});
    ColorGradient_AddStop(&s_fireCoreGrad, 1.0f, (Color){200, 80, 20, 0});

    ColorGradient_AddStop(&s_fireSmokeGrad, 0.0f, (Color){85, 70, 60, 0});
    ColorGradient_AddStop(&s_fireSmokeGrad, 0.25f, (Color){70, 60, 55, 90});
    ColorGradient_AddStop(&s_fireSmokeGrad, 1.0f, (Color){35, 32, 30, 0});

    FloatCurve_AddStop(&s_flameShape, 0.0f, 0.55f);
    FloatCurve_AddStop(&s_flameShape, 0.2f, 1.0f);
    FloatCurve_AddStop(&s_flameShape, 0.65f, 0.6f);
    FloatCurve_AddStop(&s_flameShape, 1.0f, 0.0f);

    FloatCurve_AddStop(&s_smokeShape, 0.0f, 0.4f);
    FloatCurve_AddStop(&s_smokeShape, 1.0f, 1.6f);

    ForceField_AddLayer(&s_flameFld, (ForceLayer){
                                         .type = FORCE_GRAVITY_DIR,
                                         .direction = (Vector3){0.0f, 1.0f, 0.0f},
                                         .strength = 2.2f});
    ForceField_AddLayer(&s_flameFld, (ForceLayer){
                                         .type = FORCE_NOISE_CURL,
                                         .strength = 0.55f,
                                         .noiseScale = 0.9f,
                                         .noiseSpeed = 2.2f});

    s_fireGradInit = true;
}

static void FireFlow_EmitPacket(Vector3 axisPos, float discR, float height,
                                float converge, bool hot, float sizeScale)
{
    float a = Random01() * 2.0f * PI;
    float r = discR * sqrtf(Random01()); 
    Vector3 spawn = {axisPos.x + cosf(a) * r, axisPos.y, axisPos.z + sinf(a) * r};

    float life = (0.3f + Random01() * 0.25f) * (0.7f + 0.5f * height);
    float upSpeed = (height / fmaxf(life, 0.05f)) * (0.75f + Random01() * 0.4f);

    Vector3 inward = {-cosf(a) * r * converge / fmaxf(life, 0.05f), upSpeed,
                      -sinf(a) * r * converge / fmaxf(life, 0.05f)};

    SpawnParticle((ParticleConfig){
        .position = spawn,
        .velocity = inward,
        .radius = (hot ? 0.030f : 0.045f) * sizeScale * (0.8f + Random01() * 0.45f),
        .lifetime = life,
        .gradient = hot ? &s_fireCoreGrad : &s_fireBodyGrad,
        .radiusCurve = &s_flameShape,
        .forceField = &s_flameFld});
}

#include "fireball.inl"
#include "flame_wisp.inl"
#include "fire_pillar.inl"
#include "flame_breath.inl"
#include "burning_ground.inl"
#include "fire_whirl.inl"
#include "fire_funnel.inl"
// @gen:fire_includes begin
// @gen:fire_includes end
