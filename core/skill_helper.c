#include "core/skill_helper.h"
#include "core/skill_manager.h"
#include "core/vfx_light.h"
#include "core/decal_system.h"
#include "core/screen_distort.h"
#include "core/camera_fx.h"
#include "core/particle_system.h"
#include "core/force_field.h"
#include "core/procedural_mesh_utils.h"
#include "core/resource_manager.h"
#include "raymath.h"
#include "rlgl.h"
#include <stdlib.h>
#include <math.h>

#define MAX_VOLUMES 32
#define MAX_EMITTERS 32

static DamageVolume s_volumes[MAX_VOLUMES];
static ParticleEmitter s_emitters[MAX_EMITTERS];

static ColorGradient s_fireGrad;
static ColorGradient s_snowGrad;
static ColorGradient s_waterGrad;
static ColorGradient s_lightningGrad;

static ForceField s_fireFld;
static ForceField s_snowFld;
static ForceField s_waterFld;
static ForceField s_lightningFld;

static bool s_helpersInitialized = false;

// Khai báo camera để tính suy giảm rung lắc
extern Camera3D camera;

// Khởi tạo các gradient và trường lực cho Emitter Presets
static void InitHelperResources(void) {
    if (s_helpersInitialized) return;

    // 1. Fire Resources
    s_fireGrad.count = 0;
    ColorGradient_AddStop(&s_fireGrad, 0.0f, WHITE);
    ColorGradient_AddStop(&s_fireGrad, 0.2f, (Color){ 255, 180, 50, 255 });
    ColorGradient_AddStop(&s_fireGrad, 0.7f, (Color){ 230, 60, 10, 255 });
    ColorGradient_AddStop(&s_fireGrad, 1.0f, (Color){ 30, 30, 30, 0 });

    ForceField_Clear(&s_fireFld);
    ForceField_AddLayer(&s_fireFld, (ForceLayer){ .type = FORCE_WIND, .direction = {0.0f, 1.0f, 0.0f}, .strength = 18.0f });
    ForceField_AddLayer(&s_fireFld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 10.0f, .noiseScale = 0.08f, .noiseSpeed = 1.5f });

    // 2. Snow Resources
    s_snowGrad.count = 0;
    ColorGradient_AddStop(&s_snowGrad, 0.0f, WHITE);
    ColorGradient_AddStop(&s_snowGrad, 0.5f, (Color){ 180, 220, 255, 255 });
    ColorGradient_AddStop(&s_snowGrad, 1.0f, (Color){ 150, 200, 255, 0 });

    ForceField_Clear(&s_snowFld);
    ForceField_AddLayer(&s_snowFld, (ForceLayer){ .type = FORCE_WIND, .direction = {0.5f, -1.0f, 0.2f}, .strength = 12.0f });
    ForceField_AddLayer(&s_snowFld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 6.0f, .noiseScale = 0.1f, .noiseSpeed = 0.8f });

    // 3. Water Resources
    s_waterGrad.count = 0;
    ColorGradient_AddStop(&s_waterGrad, 0.0f, (Color){ 200, 230, 255, 255 });
    ColorGradient_AddStop(&s_waterGrad, 0.4f, (Color){ 50, 150, 255, 255 });
    ColorGradient_AddStop(&s_waterGrad, 0.8f, (Color){ 10, 60, 180, 180 });
    ColorGradient_AddStop(&s_waterGrad, 1.0f, (Color){ 5, 20, 100, 0 });

    ForceField_Clear(&s_waterFld);
    ForceField_AddLayer(&s_waterFld, (ForceLayer){ .type = FORCE_GRAVITY_DIR, .direction = {0.0f, -1.0f, 0.0f}, .strength = 35.0f });
    ForceField_AddLayer(&s_waterFld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 15.0f, .noiseScale = 0.05f, .noiseSpeed = 2.0f });

    // 4. Lightning Resources
    s_lightningGrad.count = 0;
    ColorGradient_AddStop(&s_lightningGrad, 0.0f, WHITE);
    ColorGradient_AddStop(&s_lightningGrad, 0.3f, (Color){ 220, 200, 255, 255 });
    ColorGradient_AddStop(&s_lightningGrad, 0.7f, (Color){ 140, 50, 255, 255 });
    ColorGradient_AddStop(&s_lightningGrad, 1.0f, (Color){ 80, 10, 200, 0 });

    ForceField_Clear(&s_lightningFld);
    ForceField_AddLayer(&s_lightningFld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 45.0f, .noiseScale = 0.2f, .noiseSpeed = 5.0f });

    s_helpersInitialized = true;
}

// 1. Effect Preset Implementation
void SpawnImpactEffect(Vector3 pos, EffectPresetType preset, float scale) {
    InitHelperResources();

    switch (preset) {
        case EFFECT_PRESET_FIRE_EXPLOSION: {
            ScreenDistort_Add(pos, 55.0f * scale, 0.35f, 0.35f, 100.0f);
            CameraFX_Shake(0.38f);
            VFXLight_Spawn(pos, (Color){ 255, 120, 20, 255 }, 65.0f * scale, 0.5f);
            SpawnGroundDecal(DECAL_PRESET_BURN, pos, 22.0f * scale, 5.0f);

            // Sinh hạt lửa tung tóe
            int count = (int)(45 * scale);
            for (int i = 0; i < count; i++) {
                float a = (float)i / count * 2.0f * PI;
                float speed = (float)(rand() % 35 + 20) * scale;
                Vector3 vel = { cosf(a) * speed, (float)(rand() % 45 + 15) * scale, sinf(a) * speed };
                SpawnParticle((ParticleConfig){
                    .position = pos,
                    .velocity = vel,
                    .radius = ((float)rand() / (float)RAND_MAX * 1.8f + 0.8f) * scale,
                    .lifetime = (float)rand() / (float)RAND_MAX * 0.8f + 0.4f,
                    .gradient = &s_fireGrad,
                    .forceField = &s_fireFld
                });
            }
            break;
        }
        case EFFECT_PRESET_ICE_SHATTER: {
            ScreenDistort_Add(pos, 35.0f * scale, 0.22f, 0.25f, 60.0f);
            CameraFX_Shake(0.20f);
            VFXLight_Spawn(pos, (Color){ 140, 210, 255, 255 }, 45.0f * scale, 0.4f);
            SpawnGroundDecal(DECAL_PRESET_ICE, pos, 18.0f * scale, 4.0f);

            int count = (int)(30 * scale);
            for (int i = 0; i < count; i++) {
                float a = (float)i / count * 2.0f * PI;
                float speed = (float)(rand() % 25 + 10) * scale;
                Vector3 vel = { cosf(a) * speed, (float)(rand() % 30 + 10) * scale, sinf(a) * speed };
                SpawnParticle((ParticleConfig){
                    .position = pos,
                    .velocity = vel,
                    .radius = ((float)rand() / (float)RAND_MAX * 1.5f + 0.5f) * scale,
                    .lifetime = (float)rand() / (float)RAND_MAX * 0.7f + 0.3f,
                    .gradient = &s_snowGrad,
                    .forceField = &s_snowFld
                });
            }
            break;
        }
        case EFFECT_PRESET_WATER_SPLASH: {
            VFXLight_Spawn(pos, (Color){ 80, 180, 255, 255 }, 40.0f * scale, 0.45f);
            SpawnGroundDecal(DECAL_PRESET_WATER, pos, 15.0f * scale, 3.5f);

            int count = (int)(35 * scale);
            for (int i = 0; i < count; i++) {
                float a = (float)i / count * 2.0f * PI;
                float speed = (float)(rand() % 30 + 15) * scale;
                Vector3 vel = { cosf(a) * speed * 0.7f, (float)(rand() % 50 + 20) * scale, sinf(a) * speed * 0.7f };
                SpawnParticle((ParticleConfig){
                    .position = pos,
                    .velocity = vel,
                    .radius = ((float)rand() / (float)RAND_MAX * 2.0f + 0.6f) * scale,
                    .lifetime = (float)rand() / (float)RAND_MAX * 0.9f + 0.3f,
                    .gradient = &s_waterGrad,
                    .forceField = &s_waterFld
                });
            }
            break;
        }
        case EFFECT_PRESET_LIGHTNING_IMPACT: {
            ScreenDistort_Add(pos, 45.0f * scale, 0.4f, 0.2f, 150.0f);
            CameraFX_Shake(0.35f);
            VFXLight_Spawn(pos, (Color){ 200, 150, 255, 255 }, 55.0f * scale, 0.35f);
            SpawnGroundDecal(DECAL_PRESET_TAIJI_LIGHTNING, pos, 14.0f * scale, 3.0f);

            int count = (int)(25 * scale);
            for (int i = 0; i < count; i++) {
                Vector3 vel = { ((float)rand() / (float)RAND_MAX - 0.5f) * 65.0f * scale,
                                ((float)rand() / (float)RAND_MAX) * 75.0f * scale,
                                ((float)rand() / (float)RAND_MAX - 0.5f) * 65.0f * scale };
                SpawnParticle((ParticleConfig){
                    .position = pos,
                    .velocity = vel,
                    .radius = ((float)rand() / (float)RAND_MAX * 1.4f + 0.4f) * scale,
                    .lifetime = (float)rand() / (float)RAND_MAX * 0.5f + 0.25f,
                    .gradient = &s_lightningGrad,
                    .forceField = &s_lightningFld
                });
            }
            break;
        }
        case EFFECT_PRESET_EARTH_CRACK: {
            ScreenDistort_Add(pos, 30.0f * scale, 0.25f, 0.4f, 50.0f);
            CameraFX_Shake(0.42f);
            VFXLight_Spawn(pos, (Color){ 180, 140, 100, 255 }, 35.0f * scale, 0.6f);
            SpawnGroundDecal(DECAL_PRESET_CRACK, pos, 25.0f * scale, 5.5f);
            break;
        }
    }
}

// 2. Damage Volume Implementation
void DamageVolume_Init(void) {
    for (int i = 0; i < MAX_VOLUMES; i++) {
        s_volumes[i].active = false;
    }
}

void DamageVolume_Update(float dt) {
    for (int i = 0; i < MAX_VOLUMES; i++) {
        if (!s_volumes[i].active) continue;

        s_volumes[i].timer += dt;
        s_volumes[i].tickTimer += dt;

        if (s_volumes[i].tickTimer >= s_volumes[i].tickInterval) {
            s_volumes[i].tickTimer = 0.0f;
            float damage = s_volumes[i].damagePerSecond * s_volumes[i].tickInterval;
            ApplyAoEDamage(s_volumes[i].center, s_volumes[i].radius, damage, 0.0f);
        }

        if (s_volumes[i].timer >= s_volumes[i].duration) {
            s_volumes[i].active = false;
        }
    }
}

int SpawnDamageVolume(DamageVolume config) {
    for (int i = 0; i < MAX_VOLUMES; i++) {
        if (!s_volumes[i].active) {
            s_volumes[i] = config;
            s_volumes[i].timer = 0.0f;
            s_volumes[i].tickTimer = 0.0f;
            s_volumes[i].active = true;
            return i;
        }
    }
    return -1;
}

void DamageVolume_Unload(void) {
    DamageVolume_Init();
}

// 3. Skill Timeline Implementation
void Timeline_Start(SkillTimeline *t, float duration) {
    t->current = 0.0f;
    t->duration = duration;
}

bool Timeline_Event(SkillTimeline *t, float triggerTime, float dt) {
    return (t->current - dt < triggerTime) && (t->current >= triggerTime);
}

bool Timeline_Finished(SkillTimeline *t) {
    return t->current >= t->duration;
}

// 4. Particle Emitter System Implementation
void EmitterSystem_Init(void) {
    InitHelperResources();
    for (int i = 0; i < MAX_EMITTERS; i++) {
        s_emitters[i].active = false;
    }
}

void EmitterSystem_Update(float dt) {
    for (int i = 0; i < MAX_EMITTERS; i++) {
        if (!s_emitters[i].active) continue;

        s_emitters[i].timer += dt;
        if (s_emitters[i].timer >= s_emitters[i].duration) {
            s_emitters[i].active = false;
            continue;
        }

        s_emitters[i].spawnAccum += s_emitters[i].rate * dt;
        int count = (int)s_emitters[i].spawnAccum;
        s_emitters[i].spawnAccum -= count;

        ColorGradient *grad = NULL;
        ForceField *fld = NULL;

        switch (s_emitters[i].type) {
            case EMITTER_FIRE: grad = &s_fireGrad; fld = &s_fireFld; break;
            case EMITTER_SNOW: grad = &s_snowGrad; fld = &s_snowFld; break;
            case EMITTER_WATER_SPURT: grad = &s_waterGrad; fld = &s_waterFld; break;
            case EMITTER_SHOCKED_SPARKS: grad = &s_lightningGrad; fld = &s_lightningFld; break;
        }

        for (int k = 0; k < count; k++) {
            Vector3 offset = {
                ((float)rand() / (float)RAND_MAX - 0.5f) * 2.0f,
                ((float)rand() / (float)RAND_MAX - 0.5f) * 2.0f,
                ((float)rand() / (float)RAND_MAX - 0.5f) * 2.0f
            };
            Vector3 pos = Vector3Add(s_emitters[i].pos, offset);
            Vector3 vel = {
                ((float)rand() / (float)RAND_MAX - 0.5f) * 10.0f,
                ((float)rand() / (float)RAND_MAX * 15.0f + 10.0f),
                ((float)rand() / (float)RAND_MAX - 0.5f) * 10.0f
            };

            SpawnParticle((ParticleConfig){
                .position = pos,
                .velocity = vel,
                .radius = (float)GetRandomValue(8, 20) / 10.0f,
                .lifetime = (float)GetRandomValue(5, 12) / 10.0f,
                .gradient = grad,
                .forceField = fld
            });
        }
    }
}

int Emitter_AttachToPoint(EmitterPreset type, Vector3 pos, float ratePerSecond, float duration) {
    for (int i = 0; i < MAX_EMITTERS; i++) {
        if (!s_emitters[i].active) {
            s_emitters[i].type = type;
            s_emitters[i].pos = pos;
            s_emitters[i].rate = ratePerSecond;
            s_emitters[i].duration = duration;
            s_emitters[i].timer = 0.0f;
            s_emitters[i].spawnAccum = 0.0f;
            s_emitters[i].active = true;
            return i;
        }
    }
    return -1;
}

void Emitter_Stop(int emitterId) {
    if (emitterId >= 0 && emitterId < MAX_EMITTERS) {
        s_emitters[emitterId].active = false;
    }
}

void EmitterSystem_Unload(void) {
    EmitterSystem_Init();
}

static void DrawCorePyramid(Vector3 pos, float radius, float height, Color color) {
    Vector3 top = { pos.x, pos.y + height, pos.z };
    Vector3 base[4];
    for (int i = 0; i < 4; i++) {
        float angle = i * 0.5f * 3.14159265f;
        base[i] = (Vector3){ pos.x + cosf(angle) * radius, pos.y, pos.z + sinf(angle) * radius };
    }

    rlColor4ub(color.r, color.g, color.b, color.a);
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < 4; i++) {
        int next = (i + 1) % 4;
        
        // Tính face normal cho mặt bên
        Vector3 edge1 = Vector3Subtract(base[next], base[i]);
        Vector3 edge2 = Vector3Subtract(top, base[i]);
        Vector3 normal = Vector3Normalize(Vector3CrossProduct(edge1, edge2));
        
        rlNormal3f(normal.x, normal.y, normal.z);
        rlVertex3f(base[i].x, base[i].y, base[i].z);
        rlVertex3f(base[next].x, base[next].y, base[next].z);
        rlVertex3f(top.x, top.y, top.z);
    }
    
    // Đáy hình vuông (2 tam giác)
    Vector3 edgeB1 = Vector3Subtract(base[1], base[0]);
    Vector3 edgeB2 = Vector3Subtract(base[3], base[0]);
    Vector3 baseNormal = Vector3Normalize(Vector3CrossProduct(edgeB2, edgeB1)); // Hướng xuống dưới
    rlNormal3f(baseNormal.x, baseNormal.y, baseNormal.z);
    
    rlVertex3f(base[0].x, base[0].y, base[0].z);
    rlVertex3f(base[2].x, base[2].y, base[2].z);
    rlVertex3f(base[1].x, base[1].y, base[1].z);
    
    rlVertex3f(base[0].x, base[0].y, base[0].z);
    rlVertex3f(base[3].x, base[3].y, base[3].z);
    rlVertex3f(base[2].x, base[2].y, base[2].z);
    rlEnd();
}

static void DrawCoreTetrahedron(Vector3 pos, float radius, float height, Color color) {
    Vector3 top = { pos.x, pos.y + height, pos.z };
    Vector3 base[3];
    for (int i = 0; i < 3; i++) {
        float angle = i * (2.0f * 3.14159265f / 3.0f);
        base[i] = (Vector3){ pos.x + cosf(angle) * radius, pos.y, pos.z + sinf(angle) * radius };
    }

    rlColor4ub(color.r, color.g, color.b, color.a);
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < 3; i++) {
        int next = (i + 1) % 3;
        
        // Tính face normal cho mặt bên
        Vector3 edge1 = Vector3Subtract(base[next], base[i]);
        Vector3 edge2 = Vector3Subtract(top, base[i]);
        Vector3 normal = Vector3Normalize(Vector3CrossProduct(edge1, edge2));
        
        rlNormal3f(normal.x, normal.y, normal.z);
        rlVertex3f(base[i].x, base[i].y, base[i].z);
        rlVertex3f(base[next].x, base[next].y, base[next].z);
        rlVertex3f(top.x, top.y, top.z);
    }
    
    // Đáy tam giác
    Vector3 edgeB1 = Vector3Subtract(base[1], base[0]);
    Vector3 edgeB2 = Vector3Subtract(base[2], base[0]);
    Vector3 baseNormal = Vector3Normalize(Vector3CrossProduct(edgeB2, edgeB1));
    rlNormal3f(baseNormal.x, baseNormal.y, baseNormal.z);
    
    rlVertex3f(base[0].x, base[0].y, base[0].z);
    rlVertex3f(base[2].x, base[2].y, base[2].z);
    rlVertex3f(base[1].x, base[1].y, base[1].z);
    rlEnd();
}

// 5. Mesh Preset Implementation
void DrawEffectMesh(MeshPresetType type, Vector3 pos, Vector3 scale, Color color) {
    // Đảm bảo reset vertex color để shader chạy đúng
    rlColor4ub(color.r, color.g, color.b, color.a);

    switch (type) {
        case MESH_PRESET_DISC:
            DrawCorePlanePolygon(pos, scale.x, 24, color);
            break;
        case MESH_PRESET_RING:
            DrawCoreTorus(pos, scale.x * 0.85f, scale.x, 16, 24, color);
            break;
        case MESH_PRESET_CONE:
            DrawCoreCone(pos, scale.x, scale.y, 16, color);
            break;
        case MESH_PRESET_TORNADO: {
            for (int i = 0; i < 3; i++) {
                float r = scale.x * (1.0f + i * 0.15f);
                DrawCoreCylinder(pos, (Vector3){pos.x, pos.y + scale.y, pos.z}, r * 0.5f, r, 12, color);
            }
            break;
        }
        case MESH_PRESET_CYLINDER:
            DrawCoreCylinder(pos, (Vector3){pos.x, pos.y + scale.y, pos.z}, scale.x, scale.x, 16, color);
            break;
        case MESH_PRESET_SPHERE:
            DrawCoreSphere(pos, scale.x, 16, 16, color);
            break;
        case MESH_PRESET_SHOCKWAVE:
            DrawCoreTorus(pos, scale.x * 0.92f, scale.x, 8, 24, color);
            break;
        case MESH_PRESET_PYRAMID:
            DrawCorePyramid(pos, scale.x, scale.y, color);
            break;
        case MESH_PRESET_TETRAHEDRON:
            DrawCoreTetrahedron(pos, scale.x, scale.y, color);
            break;
    }
}

// 6. Shader Material System Implementation
EffectMaterial Material_Load(MaterialPreset preset) {
    EffectMaterial mat = {0};
    mat.preset = preset;

    switch (preset) {
        case MATERIAL_FIRE:
            mat.shader = ResourceManager_LoadShader("skills/fire/fire_wildfire/fire_wildfire.vs",
                                                   "skills/fire/fire_wildfire/fire_wildfire.fs");
            break;
        case MATERIAL_ICE:
            mat.shader = ResourceManager_LoadShader("skills/water/frost_blossom_rain_skill/frost_blossom_rain.vs",
                                                   "skills/water/frost_blossom_rain_skill/frost_blossom_rain.fs");
            break;
        case MATERIAL_WATER:
            mat.shader = ResourceManager_LoadShader("skills/water/water_stream/tube.vs",
                                                   "skills/water/water_stream/tube.fs");
            break;
        case MATERIAL_PORTAL:
            mat.shader = ResourceManager_LoadShader("skills/taiji/yin_yang_orb/yin_yang_orb.vs",
                                                   "skills/taiji/yin_yang_orb/yin_yang_orb.fs");
            break;
    }

    mat.uTimeLoc = GetShaderLocation(mat.shader, "u_time");
    mat.uDissolveLoc = GetShaderLocation(mat.shader, "u_dissolve");
    return mat;
}

void Material_SetFloat(EffectMaterial *mat, const char *uniformName, float val) {
    int loc = GetShaderLocation(mat->shader, uniformName);
    if (loc >= 0) {
        SetShaderValue(mat->shader, loc, &val, SHADER_UNIFORM_FLOAT);
    }
}

void Material_Begin(EffectMaterial mat) {
    SkillManager_BeginShader(mat.shader);
    float time = (float)GetTime();
    if (mat.uTimeLoc >= 0) {
        SetShaderValue(mat.shader, mat.uTimeLoc, &time, SHADER_UNIFORM_FLOAT);
    }
}

void Material_End(void) {
    SkillManager_EndShader();
}

// 7. Ground Decal Implementation
void SpawnGroundDecal(DecalPresetType type, Vector3 pos, float radius, float duration) {
    Texture2D tex = {0};
    Color tint = WHITE;

    switch (type) {
        // Earth
        case DECAL_PRESET_CRACK:
            tex = ResourceManager_LoadTexture("assets/textures/crack.png");
            tint = ColorAlpha(ELEMENT_COLOR_EARTH, 0.7f);
            break;
        case DECAL_PRESET_EARTH_SHATTER:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_stone_shatter.png");
            tint = ColorAlpha(ELEMENT_COLOR_EARTH, 0.75f);
            break;
        case DECAL_PRESET_EARTH_RUNE:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_earth_rune.png");
            tint = ColorAlpha(ELEMENT_COLOR_EARTH, 0.85f);
            break;

        // Fire
        case DECAL_PRESET_BURN:
            tex = ResourceManager_LoadTexture("assets/textures/scorch_mark.png");
            tint = ColorAlpha(ELEMENT_COLOR_FIRE, 0.65f);
            break;
        case DECAL_PRESET_FIRE_LAVA:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_lava_crack.png");
            tint = ColorAlpha(ELEMENT_COLOR_FIRE, 0.8f);
            break;

        // Water
        case DECAL_PRESET_WATER:
            tex = ResourceManager_LoadTexture("assets/textures/water_caustics.png");
            tint = ColorAlpha(ELEMENT_COLOR_WATER, 0.5f);
            break;
        case DECAL_PRESET_WATER_SPLASH:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_splash_ring.png");
            tint = ColorAlpha(ELEMENT_COLOR_WATER, 0.6f);
            break;
        case DECAL_PRESET_WATER_RIPPLE:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_water_ripple.png");
            tint = ColorAlpha(ELEMENT_COLOR_WATER, 0.5f);
            break;
        case DECAL_PRESET_ICE:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_frost_ring.png");
            tint = ColorAlpha(WHITE, 0.55f);
            break;

        // Wood
        case DECAL_PRESET_WOOD_ROOT:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_root_mark.png");
            tint = ColorAlpha(ELEMENT_COLOR_WOOD, 0.75f);
            break;
        case DECAL_PRESET_WOOD_MOSS:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_moss_stain.png");
            tint = ColorAlpha(ELEMENT_COLOR_WOOD, 0.6f);
            break;

        // Metal
        case DECAL_PRESET_METAL_SLASH:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_slash_mark.png");
            tint = ColorAlpha(ELEMENT_COLOR_METAL, 0.8f);
            break;
        case DECAL_PRESET_METAL_CRATER:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_impact_crater.png");
            tint = ColorAlpha(ELEMENT_COLOR_METAL, 0.75f);
            break;
        case DECAL_PRESET_METAL_RUNE:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_metal_rune.png");
            tint = ColorAlpha(ELEMENT_COLOR_METAL, 0.85f);
            break;

        // Taiji
        case DECAL_PRESET_TAIJI_RING:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_taiji_ring.png");
            tint = ColorAlpha(ELEMENT_COLOR_TAIJI, 0.8f);
            break;
        case DECAL_PRESET_TAIJI_LIGHTNING:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_lightning_char.png");
            tint = ColorAlpha(ELEMENT_COLOR_TAIJI, 0.85f);
            break;
        case DECAL_PRESET_TAIJI_WIND:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_wind_groove.png");
            tint = ColorAlpha(ELEMENT_COLOR_TAIJI, 0.5f);
            break;

        // Generic — no element tint baked in, caller adjusts via radius/duration only
        case DECAL_PRESET_GENERIC_IMPACT_RING:
            tex = ResourceManager_LoadTexture("assets/textures/generic/impact_ring.png");
            tint = ColorAlpha(WHITE, 0.7f);
            break;
        case DECAL_PRESET_GENERIC_GLOW:
            tex = ResourceManager_LoadTexture("assets/textures/generic/glow_circle.png");
            tint = ColorAlpha(WHITE, 0.4f);
            break;
        case DECAL_PRESET_GENERIC_SHADOW:
            tex = ResourceManager_LoadTexture("assets/textures/generic/shadow_blob.png");
            tint = ColorAlpha(BLACK, 0.5f);
            break;
    }

    Vector3 decalPos = { pos.x, pos.y + 0.02f, pos.z };
    DecalSystem_Add(decalPos, (float)GetRandomValue(0, 360), radius, tex, duration, tint);
}

// 8. Camera Impulse Implementation
void CameraFX_AddImpulse(Vector3 origin, CameraImpulse impulse) {
    float dist = Vector3Distance(origin, camera.target);
    float factor = 1.0f / (1.0f + dist * impulse.falloff);
    float resultTrauma = impulse.magnitude * factor;
    if (resultTrauma > 0.05f) {
        CameraFX_Shake(resultTrauma);
    }
}

// 9. ForceField Preset Implementation
ForceField ForceField_CreatePreset(ForceFieldPreset preset) {
    ForceField fld;
    ForceField_Clear(&fld);

    switch (preset) {
        case FORCE_PRESET_FIRE_UPDRAFT:
            ForceField_AddLayer(&fld, (ForceLayer){ .type = FORCE_WIND, .direction = {0.0f, 1.0f, 0.0f}, .strength = 20.0f });
            ForceField_AddLayer(&fld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 12.0f, .noiseScale = 0.06f, .noiseSpeed = 1.2f });
            break;
        case FORCE_PRESET_SNOW_BLIZZARD:
            ForceField_AddLayer(&fld, (ForceLayer){ .type = FORCE_WIND, .direction = {0.6f, -1.0f, 0.3f}, .strength = 14.0f });
            ForceField_AddLayer(&fld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 8.0f, .noiseScale = 0.08f, .noiseSpeed = 0.9f });
            break;
        case FORCE_PRESET_WATER_VORTEX:
            ForceField_AddLayer(&fld, (ForceLayer){ .type = FORCE_GRAVITY_DIR, .direction = {0.0f, -1.0f, 0.0f}, .strength = 30.0f });
            ForceField_AddLayer(&fld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 18.0f, .noiseScale = 0.05f, .noiseSpeed = 2.2f });
            break;
    }
    return fld;
}

// 10. Skill Builder Implementation
void SkillBuilder_Start(SkillBuildContext *ctx, Vector3 target, float scale) {
    ctx->target = target;
    ctx->scale = scale;
    ctx->hasExplosion = false;
    ctx->hasDecal = false;
    ctx->hasDamageVolume = false;
}

void SkillBuilder_AddExplosion(SkillBuildContext *ctx, EffectPresetType vfx) {
    ctx->hasExplosion = true;
    ctx->explosionEffect = vfx;
}

void SkillBuilder_AddDecal(SkillBuildContext *ctx, DecalPresetType decal, float radius, float duration) {
    ctx->hasDecal = true;
    ctx->decalType = decal;
    ctx->decalRadius = radius;
    ctx->decalDuration = duration;
}

void SkillBuilder_AddDamageVolume(SkillBuildContext *ctx, float radius, float dps, float duration) {
    ctx->hasDamageVolume = true;
    ctx->damageRadius = radius;
    ctx->damageDps = dps;
    ctx->damageDuration = duration;
}

void SkillBuilder_Build(SkillBuildContext *ctx) {
    if (ctx->hasExplosion) {
        SpawnImpactEffect(ctx->target, ctx->explosionEffect, ctx->scale);
    }
    if (ctx->hasDecal) {
        SpawnGroundDecal(ctx->decalType, ctx->target, ctx->decalRadius * ctx->scale, ctx->decalDuration);
    }
    if (ctx->hasDamageVolume) {
        DamageVolume vol = {
            .shape = SHAPE_CIRCLE,
            .center = ctx->target,
            .radius = ctx->damageRadius * ctx->scale,
            .damagePerSecond = ctx->damageDps,
            .tickInterval = 0.25f,
            .duration = ctx->damageDuration
        };
        SpawnDamageVolume(vol);
    }
}
