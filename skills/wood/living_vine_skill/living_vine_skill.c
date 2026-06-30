// Living Vine — Wood element.
// A massive enchanted vine erupts from the ground, grows organically toward the
// target while twisting through the air, coils around the enemy, then dissolves
// into leaves, roots and green energy. Built entirely from CORE_API primitives:
// no Raylib primitive meshes, no straight geometry, no mirrored symmetry.

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include "skills/wood/living_vine_skill/living_vine_skill.h"
#include "core/skill_manager.h"          // ApplyAoEDamage, SkillManager_BeginShader, ELEMENT_COLOR_WOOD
#include "core/skill_helper.h"           // SpawnCastEffect/Impact, SpawnGroundDecal, PlayCastSound, CameraFX_AddImpulse
#include "core/force_field.h"            // ForceField
#include "core/particle_system.h"        // SpawnParticle
#include "core/particle_radial_burst.h"  // ParticleSystem_SpawnRadialBurst
#include "core/procedural_mesh_utils.h"  // Tube mesh
#include "core/path_spline.h"            // GetBezierPoint
#include "core/resource_manager.h"       // shader load
#include "core/vfx_light.h"              // VFXLight_Spawn
#include "core/screen_distort.h"         // ScreenDistort_Add
#include "core/camera_fx.h"              // CameraFX_Shake
#include "core/utils_math.h"             // Math_Mix, SmoothStep01, Random01

#ifndef PI
#define PI 3.1415926535f
#endif

// ----------------------------------------------------------------------------
// Tunables (respect CORE_API scaling rules)
// ----------------------------------------------------------------------------
#define CAST_DURATION      0.65f
#define GROW_DURATION      1.15f
#define IMPACT_DURATION    0.30f
#define ACTIVE_DURATION    2.60f
#define DISSOLVE_DURATION  1.50f

#define VINE_BASE_RADIUS   15.0f   // mesh radius 10-20f
#define TUBE_SEGMENTS      26
#define TUBE_RADIAL        11

// ----------------------------------------------------------------------------
// State
// ----------------------------------------------------------------------------
typedef enum { LV_IDLE, LV_CASTING, LV_GROWING, LV_IMPACT, LV_ACTIVE, LV_DISSOLVE } LVState;

typedef struct {
    LVState state;
    float   stateTimer;
    float   growth;       // 0..1 main-body growth (head leads body)
    float   dissolve;     // 0..1 shader dissolve
    float   tighten;      // active-phase coil tightening 0..1

    Vector3 start, target;
    float   length;
    float   sizeScale, damage;
    bool    dealtDamage;

    // organic path control — per-cast randomized, swayed by time
    float   latPhase1, latPhase2, latAmp1, latAmp2;
    float   archHeight;
    Vector3 perp;

    Vector3 tip;          // current growth tip (for collision feed)
    bool    coiling;      // exposed via IsLivingVineSkillCoiling

    Vector3 hitPos;        // where the vine actually settled/wrapped (hit OR miss point)
    bool    actuallyHit;   // true only if the enemy was within range at impact

    TubeMeshData mainMesh;
    bool    active;
} VineInstance;

static VineInstance s_v;
static ForceField   s_force;
static Shader       s_shader;
static int          s_locDissolve = -1;
static int          s_locUvLength = -1;
static int          s_locWoodColor = -1;
static Vector3      s_woodColorVec;
static bool         s_initialized = false;

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------
static inline Vector3 ColorToVec3(Color c) {
    return (Vector3){ c.r / 255.0f, c.g / 255.0f, c.b / 255.0f };
}

static float RandRange(float a, float b) { return a + Random01() * (b - a); }

// Build the 4 Bezier control points of the main vine for a given time.
// The body bends, sways and arcs upward — never a straight line.
static void BuildMainControlPoints(float t, Vector3 *p0, Vector3 *p1, Vector3 *p2, Vector3 *p3) {
    Vector3 s = s_v.start;
    Vector3 e = s_v.target;
    Vector3 perp = s_v.perp;

    // Time-driven sway so the standing vine keeps breathing/twisting.
    float sway1 = sinf(t * 1.3f + s_v.latPhase1) * s_v.latAmp1;
    float sway2 = sinf(t * 1.7f + s_v.latPhase2) * s_v.latAmp2;
    float arc   = s_v.archHeight;

    *p0 = (Vector3){ s.x, s.y - 20.0f, s.z };   // erupts from just underground
    *p1 = (Vector3){
        Math_Mix(s.x, e.x, 0.33f) + perp.x * sway1,
        s.y + arc * 0.55f,
        Math_Mix(s.z, e.z, 0.33f) + perp.z * sway1
    };

    if (s_v.coiling) {
        // Wrapped around the target: the tail swings into a tightening
        // circular arc around hitPos instead of approaching it head-on —
        // this is what reads as the vine coiling around the enemy.
        Vector3 c = s_v.hitPos;
        float coilR = (45.0f + 25.0f * sinf(t * 1.1f)) * s_v.sizeScale * (1.0f - 0.5f * s_v.tighten);
        float ang = t * 2.0f * (s_v.latAmp1 >= 0.0f ? 1.0f : -1.0f);
        *p2 = (Vector3){ c.x + cosf(ang) * coilR, c.y + 18.0f * s_v.sizeScale, c.z + sinf(ang) * coilR };
        *p3 = c;
    } else {
        *p2 = (Vector3){
            Math_Mix(s.x, e.x, 0.66f) - perp.x * sway2,
            s.y + arc * 0.40f,
            Math_Mix(s.z, e.z, 0.66f) - perp.z * sway2
        };
        *p3 = e;
    }
}

// Growth in organic bursts: smooth base + small surges, tip runs ahead of body.
static float GrowthBurst(float lin) {
    float g = SmoothStep01(lin);
    g += 0.05f * sinf(lin * PI * 3.0f);   // little growth surges
    if (g < 0.0f) g = 0.0f;
    if (g > 1.0f) g = 1.0f;
    return g;
}

static void ConfigureVineTube(TubeMeshConfig *cfg) {
    *cfg = ProceduralMesh_DefaultTubeConfig();
    cfg->headGrowth     = 1.15f;   // tip slightly fatter / leads
    cfg->tailTaperMin   = 0.55f;
    cfg->tailTaperMax   = 0.85f;
    // Light organic wobble — enough to feel alive, not chaotic.
    cfg->wobbleAmplitude = 2.5f;
    cfg->wobbleFrequency = 1.4f;
    cfg->wobbleSpeed     = 1.2f;
    cfg->deform1Amp      = 0.8f;
    cfg->deform1FreqT    = 3.0f;
    cfg->deform1FreqPhi  = 2.0f;
    cfg->deform1Speed    = 0.8f;
    cfg->deform2Amp      = 0.0f;
    cfg->deform2FreqT    = 0.0f;
    cfg->deform2FreqPhi  = 0.0f;
    cfg->deform2Speed    = 0.0f;
}

// Continuous particle emission near a point: leaves, pollen, spores, dust, bark.
static void EmitVineParticles(Vector3 pos, float intensity) {
    Color wood = ELEMENT_COLOR_WOOD;
    int n = (int)(intensity * 4.0f);
    for (int i = 0; i < n; i++) {
        Vector3 jit = { RandRange(-12, 12), RandRange(-6, 18), RandRange(-12, 12) };
        Vector3 p = Vector3Add(pos, jit);
        int kind = GetRandomValue(0, 4);
        ParticleConfig pc = {0};
        pc.position = p;
        pc.forceField = &s_force;
        switch (kind) {
            case 0: // tiny leaf
                pc.velocity   = (Vector3){ RandRange(-25,25), RandRange(10,40), RandRange(-25,25) };
                pc.colorStart = ColorAlpha(wood, 0.95f);
                pc.colorEnd   = ColorAlpha(ColorLerp(wood, BLACK, 0.6f), 0.0f);
                pc.radius     = RandRange(6, 10);
                pc.lifetime   = RandRange(1.3f, 2.2f);
                break;
            case 1: // glowing spore (rises)
                pc.velocity   = (Vector3){ RandRange(-8,8), RandRange(25,55), RandRange(-8,8) };
                pc.colorStart = ColorAlpha(ColorLerp(wood, WHITE, 0.5f), 0.9f);
                pc.colorEnd   = ColorAlpha(wood, 0.0f);
                pc.radius     = RandRange(2.5f, 4.5f);
                pc.lifetime   = RandRange(1.6f, 2.6f);
                break;
            case 2: // pollen
                pc.velocity   = (Vector3){ RandRange(-20,20), RandRange(0,25), RandRange(-20,20) };
                pc.colorStart = ColorAlpha(ColorLerp(wood, WHITE, 0.35f), 0.7f);
                pc.colorEnd   = ColorAlpha(wood, 0.0f);
                pc.radius     = RandRange(1.5f, 3.0f);
                pc.lifetime   = RandRange(1.0f, 1.8f);
                break;
            case 3: // wood dust / bark chip
                pc.velocity   = (Vector3){ RandRange(-30,30), RandRange(-10,20), RandRange(-30,30) };
                pc.colorStart = ColorAlpha(ColorLerp(wood, (Color){60,40,20,255}, 0.7f), 0.85f);
                pc.colorEnd   = ColorAlpha((Color){40,28,15,255}, 0.0f);
                pc.radius     = RandRange(3, 6);
                pc.lifetime   = RandRange(0.8f, 1.6f);
                break;
            default: // hanging root that fades
                pc.velocity   = (Vector3){ RandRange(-6,6), RandRange(-25,-5), RandRange(-6,6) };
                pc.colorStart = ColorAlpha(ColorLerp(wood, BLACK, 0.4f), 0.8f);
                pc.colorEnd   = ColorAlpha(ColorLerp(wood, BLACK, 0.7f), 0.0f);
                pc.radius     = RandRange(2, 4);
                pc.lifetime   = RandRange(1.0f, 2.0f);
                break;
        }
        SpawnParticle(pc);
    }
}

// Layered ForceField centred on the tip: vortex + gentle inward pull + drag + noise.
// Ambient regime (5-60f) so particles orbit the vine instead of flinging off-screen.
static void UpdateForceField(Vector3 tip) {
    ForceField_Clear(&s_force);
    ForceField_AddLayer(&s_force, (ForceLayer){
        .type = FORCE_VORTEX, .origin = tip, .direction = (Vector3){0,1,0},
        .strength = 9.0f, .radius = 160.0f, .falloff = 1.0f });
    ForceField_AddLayer(&s_force, (ForceLayer){
        .type = FORCE_GRAVITY_POINT, .origin = tip,
        .strength = 22.0f, .radius = 170.0f, .falloff = 1.0f });
    ForceField_AddLayer(&s_force, (ForceLayer){
        .type = FORCE_DRAG, .strength = 0.55f });
    ForceField_AddLayer(&s_force, (ForceLayer){
        .type = FORCE_NOISE_PERLIN, .strength = 16.0f, .noiseScale = 0.02f, .noiseSpeed = 1.0f });
}

// ----------------------------------------------------------------------------
// Lifecycle
// ----------------------------------------------------------------------------
void InitLivingVineSkill(int screenWidth, int screenHeight) {
    (void)screenWidth; (void)screenHeight;
    s_shader = ResourceManager_LoadShader(
        "skills/wood/living_vine_skill/living_vine.vs",
        "skills/wood/living_vine_skill/living_vine.fs");
    s_locDissolve  = GetShaderLocation(s_shader, "u_dissolve");
    s_locUvLength  = GetShaderLocation(s_shader, "u_uvLength");
    s_locWoodColor = GetShaderLocation(s_shader, "u_woodColor");
    s_woodColorVec = ColorToVec3((Color)ELEMENT_COLOR_WOOD);
    ForceField_Clear(&s_force);
    s_v.state = LV_IDLE;
    s_v.active = false;
    s_initialized = true;
}

void CastLivingVineSkill(Vector3 startPos, Vector3 target, SkillParams params) {
    if (!s_initialized) return;

    s_v = (VineInstance){0};
    s_v.start = startPos;
    s_v.target = target;
    s_v.sizeScale = (params.sizeScale > 0.0f) ? params.sizeScale : 1.0f;
    s_v.damage = (params.damage > 0.0f) ? params.damage : 40.0f;

    Vector3 dir = Vector3Subtract(target, startPos);
    dir.y = 0.0f;
    s_v.length = Vector3Length(dir);
    if (s_v.length < 1.0f) { s_v.length = 1.0f; dir = (Vector3){1,0,0}; }
    Vector3 dn = Vector3Scale(dir, 1.0f / s_v.length);
    s_v.perp = (Vector3){ -dn.z, 0.0f, dn.x };   // horizontal perpendicular

    // Per-cast organic randomization (asymmetric sway, no mirrored geometry).
    s_v.latAmp1   = RandRange(0.05f, 0.09f) * s_v.length;
    s_v.latAmp2   = RandRange(0.04f, 0.07f) * s_v.length;
    s_v.latPhase1 = RandRange(0.0f, 2.0f * PI);
    s_v.latPhase2 = RandRange(0.0f, 2.0f * PI);
    s_v.archHeight = RandRange(0.12f, 0.20f) * s_v.length;

    s_v.state = LV_CASTING;
    s_v.stateTimer = 0.0f;
    s_v.active = true;
    s_v.coiling = false;
    s_v.tip = startPos;

    // --- CAST PHASE: ground bloom, root decal, inward gather, green light ---
    float sc = s_v.sizeScale;
    SpawnCastEffect(startPos, EFFECT_PRESET_WOOD_BLOOM, sc);
    PlayCastSound(EFFECT_PRESET_WOOD_BLOOM);
    SpawnGroundDecal(DECAL_PRESET_WOOD_ROOT, startPos, 70.0f * sc, CAST_DURATION + GROW_DURATION + ACTIVE_DURATION);
    VFXLight_Spawn(startPos, ELEMENT_COLOR_WOOD, 90.0f * sc, CAST_DURATION + 0.3f);
    UpdateForceField(startPos);
}

void UpdateLivingVineSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    if (!s_initialized || !s_v.active) return;
    s_v.stateTimer += dt;
    float t = (float)GetTime();
    float sc = s_v.sizeScale;

    switch (s_v.state) {
        case LV_CASTING: {
            // Leaves swirl inward, particles attracted to the gather point.
            UpdateForceField(s_v.start);
            EmitVineParticles(s_v.start, 0.6f);
            if (s_v.stateTimer >= CAST_DURATION) {
                s_v.state = LV_GROWING;
                s_v.stateTimer = 0.0f;
                // Emergence burst: dirt, broken roots, bark fragments, leaves, dust.
                ParticleRadialBurstConfig burst = {0};
                burst.countMin = 24; burst.countMax = 40;
                burst.speedMin = 80; burst.speedMax = 220;       // burst speed 100-300f range
                burst.radiusMin = 4; burst.radiusMax = 9;
                burst.lifetimeMin = 0.7f; burst.lifetimeMax = 1.6f;
                burst.pitchRange = 0.5f; burst.upwardBias = 0.8f;
                burst.colorStart = ColorLerp(ELEMENT_COLOR_WOOD, (Color){70,45,22,255}, 0.5f);
                burst.colorEnd   = ColorAlpha((Color){45,30,16,255}, 0.0f);
                burst.forceField = &s_force;
                ParticleSystem_SpawnRadialBurst(s_v.start, sc, &burst);
                CameraFX_Shake(0.25f);
            }
        } break;

        case LV_GROWING: {
            float lin = s_v.stateTimer / GROW_DURATION;
            s_v.growth = GrowthBurst(lin);
            Vector3 p0,p1,p2,p3;
            BuildMainControlPoints(t, &p0,&p1,&p2,&p3);
            s_v.tip = GetBezierPoint(p0,p1,p2,p3, s_v.growth);
            UpdateForceField(s_v.tip);

            // Continuous emission at the tip and along the grown body.
            EmitVineParticles(s_v.tip, 1.0f);
            Vector3 bodyP = GetBezierPoint(p0,p1,p2,p3, s_v.growth * RandRange(0.2f, 0.9f));
            EmitVineParticles(bodyP, 0.5f);

            // Tip reached the target -> impact. A real hit only counts if the
            // enemy is actually within range of the tip; otherwise the vine
            // simply finishes growing and coils at its own aimed end point —
            // it never bends or homes toward the enemy on a miss.
            float reach = Vector3Distance(s_v.tip, enemyPos);
            bool actuallyHit = (reach <= enemyRadius + VINE_BASE_RADIUS * sc);
            if (s_v.growth >= 0.999f || actuallyHit) {
                s_v.state = LV_IMPACT;
                s_v.stateTimer = 0.0f;
                s_v.growth = 1.0f;
                s_v.coiling = true;

                // --- HIT/SETTLE: wrap at the impact point (enemy if actually hit,
                // otherwise the vine's own aimed end point), root burst, leaf burst ---
                Vector3 hit = actuallyHit ? enemyPos : s_v.target;
                s_v.hitPos = hit;
                s_v.actuallyHit = actuallyHit;
                SpawnImpactEffect(hit, EFFECT_PRESET_WOOD_BLOOM, sc * 1.2f);
                PlayImpactSound(EFFECT_PRESET_WOOD_BLOOM);
                SpawnGroundDecal(DECAL_PRESET_WOOD_ROOT, hit, 85.0f * sc, ACTIVE_DURATION + DISSOLVE_DURATION);
                ScreenDistort_Add(hit, 90.0f * sc, 0.18f, 0.5f, 8.0f);     // screen distortion
                VFXLight_Spawn(hit, ColorLerp(ELEMENT_COLOR_WOOD, WHITE, 0.4f), 130.0f * sc, ACTIVE_DURATION);
                CameraFX_Shake(0.5f);

                if (!s_v.dealtDamage) {
                    ApplyAoEDamage(hit, enemyRadius + 60.0f * sc, s_v.damage, 120.0f);
                    s_v.dealtDamage = true;
                }

                // Radial leaf + wood-splinter explosion.
                ParticleRadialBurstConfig leaf = {0};
                leaf.countMin = 30; leaf.countMax = 48;
                leaf.speedMin = 120; leaf.speedMax = 260;
                leaf.radiusMin = 5; leaf.radiusMax = 11;
                leaf.lifetimeMin = 0.9f; leaf.lifetimeMax = 1.9f;
                leaf.pitchRange = 0.7f; leaf.upwardBias = 0.5f;
                leaf.colorStart = ELEMENT_COLOR_WOOD;
                leaf.colorEnd   = ColorAlpha(ColorLerp(ELEMENT_COLOR_WOOD, BLACK, 0.6f), 0.0f);
                leaf.forceField = &s_force;
                ParticleSystem_SpawnRadialBurst(hit, sc, &leaf);
            }
        } break;

        case LV_IMPACT: {
            UpdateForceField(s_v.hitPos);
            EmitVineParticles(s_v.hitPos, 1.0f);
            if (s_v.stateTimer >= IMPACT_DURATION) {
                s_v.state = LV_ACTIVE;
                s_v.stateTimer = 0.0f;
            }
        } break;

        case LV_ACTIVE: {
            // Slowly tighten the coil; the vine subtly breathes (sway handled in path).
            s_v.tighten = SmoothStep01(s_v.stateTimer / ACTIVE_DURATION);
            // Only follow the enemy if the vine actually connected — a miss
            // stays coiled at its own settled point, it never snaps onto the enemy.
            if (s_v.actuallyHit) { s_v.target = enemyPos; s_v.hitPos = enemyPos; }
            UpdateForceField(s_v.hitPos);
            // Constantly falling leaves + rising spores; never static.
            EmitVineParticles(s_v.hitPos, 0.7f);
            EmitVineParticles(Vector3Lerp(s_v.start, s_v.hitPos, 0.6f), 0.4f);
            if (s_v.stateTimer >= ACTIVE_DURATION) {
                s_v.state = LV_DISSOLVE;
                s_v.stateTimer = 0.0f;
                s_v.coiling = false;
            }
        } break;

        case LV_DISSOLVE: {
            // The vine dries: glow fades, bark cracks, body dissolves to particles,
            // roots sink back underground.
            s_v.dissolve = SmoothStep01(s_v.stateTimer / DISSOLVE_DURATION);
            UpdateForceField(s_v.hitPos);
            EmitVineParticles(Vector3Lerp(s_v.start, s_v.hitPos, RandRange(0.0f,1.0f)), 0.5f * (1.0f - s_v.dissolve));
            if (s_v.stateTimer >= DISSOLVE_DURATION) {
                s_v.active = false;
                s_v.state = LV_IDLE;
            }
        } break;

        default: break;
    }

    (void)enemyRadius;
}

// Render the vine body with the single continuous shader (no phase popping).
static void DrawVineMesh(TubeMeshData *m, float uvLen) {
    SetShaderValue(s_shader, s_locDissolve, &s_v.dissolve, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shader, s_locUvLength, &uvLen, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shader, s_locWoodColor, &s_woodColorVec, SHADER_UNIFORM_VEC3);
    ProceduralMesh_DrawTube(m, uvLen);
}

void DrawLivingVineSkill(void) {
    if (!s_initialized || !s_v.active) return;
    if (s_v.state == LV_CASTING) return;   // nothing solid until it erupts

    float t = (float)GetTime();
    TubeMeshConfig cfg; ConfigureVineTube(&cfg);
    float uvLen = s_v.length * 0.04f;
    if (uvLen < 1.0f) uvLen = 1.0f;

    // Single body, built every frame (animated wobble/twist/sway/coil).
    Vector3 p0,p1,p2,p3;
    BuildMainControlPoints(t, &p0,&p1,&p2,&p3);
    float flow = (s_v.state == LV_GROWING) ? s_v.growth : 1.0f;
    ProceduralMesh_BuildTube(&s_v.mainMesh, p0,p1,p2,p3,
        VINE_BASE_RADIUS * s_v.sizeScale, flow, t, TUBE_SEGMENTS, TUBE_RADIAL, &cfg);

    BeginBlendMode(BLEND_ALPHA);
    SkillManager_BeginShader(s_shader);
        DrawVineMesh(&s_v.mainMesh, uvLen);
    SkillManager_EndShader();
    EndBlendMode();
}

void UnloadLivingVineSkill(void) {
    // ResourceManager owns the shader — never unload here.
    s_v.active = false;
    s_v.state = LV_IDLE;
    s_initialized = false;
}

bool IsLivingVineSkillCoiling(void) {
    return s_v.active && s_v.coiling;
}

int GetLivingVineSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles) {
    if (!s_v.active || maxProjectiles < 1) return 0;
    // Only the flying growth tip feeds engine collision.
    if (s_v.state != LV_GROWING) return 0;
    outProjectiles[0].position = s_v.tip;
    outProjectiles[0].radius   = VINE_BASE_RADIUS * s_v.sizeScale;
    outProjectiles[0].active   = true;
    return 1;
}

void DeactivateLivingVineProjectile(int index) {
    (void)index;
    // Tip consumed by a collision: jump straight to impact handling next frame.
    if (s_v.active && s_v.state == LV_GROWING) {
        s_v.growth = 1.0f;
    }
}
