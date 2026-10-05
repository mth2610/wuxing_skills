// ============================================================================
// VC_WOOD_FOLIAGE_SYSTEM.INL — Universal Botanical Foliage & Petal Simulation
//
// Dual-State Architecture:
//   1. ATTACHED: Anchored on vines, trees, branches. Biological growth, bud/bloom,
//      and harmonic wind sway.
//   2. FREE: Physical airborne simulation. Gravity (9.81 m/s²), planar aerofoil
//      flutter/glide, environmental wind, external force fields (swirl / vortex /
//      homing stream), terrain collision, and ground settling.
//
// Performance:
//   - Fixed-size memory pool (2048 instances), zero heap allocation per frame.
//   - Quad-batched rendering with SSS wrap lighting and shadow map depth pass.
// ============================================================================

#ifndef VC_WOOD_FOLIAGE_SYSTEM_INL
#define VC_WOOD_FOLIAGE_SYSTEM_INL

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "environment/env_shadow.h"
#include "environment/environment_system.h"
#include "core/composition/visual_composer.h"
#include "core/map_manager.h"
#include "core/wind/wind_system.h"
#include "core/force_field.h"
#include <math.h>
#include <stdlib.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

#define VFX_FOLIAGE_POOL_CAPACITY 2048

typedef struct {
    bool                active;
    VFX_BotanicalKind   kind;
    VFX_WoodLeafShape   leafShape;
    VFX_WoodFlowerType  flowerType;
    VFX_WoodVineStyle   style;
    VFX_BotanicalState  state;

    // 3D Motion & Physics
    Vector3 pos;
    Vector3 vel;
    Vector3 rot;        // Pitch, Yaw, Roll (radians)
    Vector3 rotVel;     // Angular tumble velocity
    float   scale;
    float   growth;     // 0..1
    float   wither;     // 0..1
    float   mass;       // In kg (default ~0.005kg)
    float   dragCoeff;  // Aerodynamic planar drag
    float   flutterPhase;
    float   flutterSpeed;

    // Attached anchor specifics
    Vector3 anchorNormal;
    Vector3 anchorTangent;
    float   stemRadius;

    // Lifetime & Dissolve
    float   age;
    float   maxLifetime;
    float   groundRestTimer;
    float   alpha;
    unsigned int seed;
} VFX_FoliageParticle;

static VFX_FoliageParticle s_foliagePool[VFX_FOLIAGE_POOL_CAPACITY];
static int  s_foliageActiveCount = 0;
static bool s_foliageSystemInitialized = false;

static ForceField s_foliageActiveForceField = {0};
static bool       s_foliageHasForceField = false;
static Vector3    s_foliageHomingTarget = {0};
static bool       s_foliageHomingActive = false;
static float      s_foliageHomingSuction = 0.0f;
static float      s_foliageHomingSwirl = 0.0f;

static inline float Foliage_Randf(unsigned int *seed)
{
    *seed = (*seed * 1664525u + 1013904223u);
    return (float)(*seed & 0x00FFFFFF) / (float)0x01000000;
}

static inline float Foliage_Smoothstep(float edge0, float edge1, float x)
{
    float t = (x - edge0) / (edge1 - edge0);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

void VFX_FoliageSystem_Init(void)
{
    if (s_foliageSystemInitialized) return;
    for (int i = 0; i < VFX_FOLIAGE_POOL_CAPACITY; i++) {
        s_foliagePool[i].active = false;
    }
    s_foliageActiveCount = 0;
    s_foliageHasForceField = false;
    s_foliageHomingActive = false;
    s_foliageSystemInitialized = true;
}

void VFX_FoliageSystem_Reset(void)
{
    for (int i = 0; i < VFX_FOLIAGE_POOL_CAPACITY; i++) {
        s_foliagePool[i].active = false;
    }
    s_foliageActiveCount = 0;
    s_foliageHasForceField = false;
    s_foliageHomingActive = false;
}

int VFX_FoliageSystem_GetActiveCount(void)
{
    return s_foliageActiveCount;
}

VFX_FoliageSpawnParams VFX_FoliageSpawnParams_Default(void)
{
    VFX_FoliageSpawnParams p;
    p.kind = BOTANICAL_KIND_LEAF;
    p.leafShape = WOOD_LEAF_SHAPE_OVAL;
    p.flowerType = WOOD_FLOWER_TYPE_LOTUS;
    p.style = WOOD_VINE_STYLE_JADE_EMERALD;
    p.origin = (Vector3){0, 1.0f, 0};
    p.radius = 1.0f;
    p.count = 32;
    p.attached = false;
    p.sockets = NULL;
    p.socketCount = 0;
    p.initialVelocity = (Vector3){0, 0.5f, 0};
    p.velocitySpread = 1.2f;
    p.mass = 0.005f;
    p.size = 0.16f;
    p.growth = 1.0f;
    p.lifetime = 8.0f;
    p.seed = 445566;
    return p;
}

int VFX_FoliageSystem_SpawnCluster(const VFX_FoliageSpawnParams *params)
{
    if (params == NULL || params->count <= 0) return 0;
    VFX_FoliageSystem_Init();

    int spawned = 0;
    unsigned int rng = params->seed ? params->seed : 9876543u;

    int totalToSpawn = params->count;
    if (params->attached && params->sockets != NULL && params->socketCount > 0)
    {
        totalToSpawn = (params->count < params->socketCount) ? params->count : params->socketCount;
    }

    for (int i = 0; i < VFX_FOLIAGE_POOL_CAPACITY && spawned < totalToSpawn; i++)
    {
        if (s_foliagePool[i].active) continue;

        VFX_FoliageParticle *p = &s_foliagePool[i];
        p->active = true;
        p->kind = params->kind;
        p->leafShape = params->leafShape;
        p->flowerType = params->flowerType;
        p->style = params->style;
        p->scale = params->size * (0.85f + 0.30f * Foliage_Randf(&rng));
        p->growth = params->growth;
        p->wither = 0.0f;
        p->mass = params->mass > 1e-4f ? params->mass : 0.005f;
        p->dragCoeff = (params->kind == BOTANICAL_KIND_LEAF) ? 1.45f : 1.15f;
        p->flutterPhase = Foliage_Randf(&rng) * 2.0f * PI;
        p->flutterSpeed = 3.5f + Foliage_Randf(&rng) * 2.5f;
        p->age = 0.0f;
        p->maxLifetime = params->lifetime > 0.1f ? params->lifetime : 8.0f;
        p->groundRestTimer = 0.0f;
        p->alpha = 1.0f;
        p->seed = rng;

        if (params->attached && params->sockets != NULL && spawned < params->socketCount)
        {
            const VFX_BotanicalSocket *sock = &params->sockets[spawned];
            p->state = BOTANICAL_STATE_ATTACHED;
            p->pos = sock->pos;
            p->vel = (Vector3){0, 0, 0};
            p->anchorNormal = sock->normal;
            p->anchorTangent = sock->tangent;
            p->stemRadius = sock->stemRadius;
            p->rot = (Vector3){
                atan2f(sock->normal.z, sock->normal.x),
                asinf(fmaxf(-1.0f, fminf(1.0f, sock->normal.y))),
                Foliage_Randf(&rng) * 0.5f - 0.25f
            };
            p->rotVel = (Vector3){0, 0, 0};
        }
        else
        {
            p->state = BOTANICAL_STATE_FREE;
            float r = params->radius * sqrtf(Foliage_Randf(&rng));
            float theta = Foliage_Randf(&rng) * 2.0f * PI;
            float phi = (Foliage_Randf(&rng) - 0.5f) * PI;

            p->pos = (Vector3){
                params->origin.x + r * cosf(phi) * cosf(theta),
                params->origin.y + r * sinf(phi),
                params->origin.z + r * cosf(phi) * sinf(theta)
            };

            float spread = params->velocitySpread;
            p->vel = (Vector3){
                params->initialVelocity.x + (Foliage_Randf(&rng) * 2.0f - 1.0f) * spread,
                params->initialVelocity.y + (Foliage_Randf(&rng) * 2.0f - 1.0f) * spread,
                params->initialVelocity.z + (Foliage_Randf(&rng) * 2.0f - 1.0f) * spread
            };

            p->rot = (Vector3){
                Foliage_Randf(&rng) * 2.0f * PI,
                Foliage_Randf(&rng) * 2.0f * PI,
                Foliage_Randf(&rng) * 2.0f * PI
            };
            p->rotVel = (Vector3){
                (Foliage_Randf(&rng) * 2.0f - 1.0f) * 4.5f,
                (Foliage_Randf(&rng) * 2.0f - 1.0f) * 5.5f,
                (Foliage_Randf(&rng) * 2.0f - 1.0f) * 3.5f
            };
        }

        spawned++;
        s_foliageActiveCount++;
    }

    return spawned;
}

int VFX_FoliageSystem_DetachInRadius(Vector3 center, float radius, Vector3 impulse)
{
    int detached = 0;
    float rSq = radius * radius;

    for (int i = 0; i < VFX_FOLIAGE_POOL_CAPACITY; i++)
    {
        if (!s_foliagePool[i].active) continue;
        if (s_foliagePool[i].state != BOTANICAL_STATE_ATTACHED) continue;

        Vector3 diff = Vector3Subtract(s_foliagePool[i].pos, center);
        float dSq = diff.x * diff.x + diff.y * diff.y + diff.z * diff.z;

        if (dSq <= rSq)
        {
            VFX_FoliageParticle *p = &s_foliagePool[i];
            p->state = BOTANICAL_STATE_FREE;
            unsigned int rng = p->seed;

            // Inherit outward fling + random tumbling velocity
            float scatter = 0.85f;
            p->vel = (Vector3){
                impulse.x + (Foliage_Randf(&rng) * 2.0f - 1.0f) * scatter,
                impulse.y + (0.5f + Foliage_Randf(&rng) * 0.8f) * scatter,
                impulse.z + (Foliage_Randf(&rng) * 2.0f - 1.0f) * scatter
            };
            p->rotVel = (Vector3){
                (Foliage_Randf(&rng) * 2.0f - 1.0f) * 6.0f,
                (Foliage_Randf(&rng) * 2.0f - 1.0f) * 7.0f,
                (Foliage_Randf(&rng) * 2.0f - 1.0f) * 5.0f
            };
            p->seed = rng;
            detached++;
        }
    }

    return detached;
}

void VFX_FoliageSystem_SetForceField(const ForceField *ff)
{
    if (ff != NULL) {
        s_foliageActiveForceField = *ff;
        s_foliageHasForceField = true;
    } else {
        s_foliageHasForceField = false;
    }
}

void VFX_FoliageSystem_ClearForceField(void)
{
    s_foliageHasForceField = false;
    s_foliageHomingActive = false;
    ForceField_Clear(&s_foliageActiveForceField);
}

void VFX_FoliageSystem_SetHomingTarget(Vector3 targetPos, float suctionStrength, float swirlStrength)
{
    s_foliageHomingTarget = targetPos;
    s_foliageHomingActive = true;
    s_foliageHomingSuction = suctionStrength;
    s_foliageHomingSwirl = swirlStrength;

    ForceField_Clear(&s_foliageActiveForceField);

    // Layer 0: Suction toward target point
    ForceLayer suctionLayer;
    suctionLayer.type = FORCE_GRAVITY_POINT;
    suctionLayer.origin = targetPos;
    suctionLayer.direction = (Vector3){0, 1.0f, 0};
    suctionLayer.strength = suctionStrength;
    suctionLayer.radius = 0.0f; // Infinite sphere
    suctionLayer.falloff = 1.0f; // Linear
    suctionLayer.noiseScale = 0.0f;
    suctionLayer.noiseSpeed = 0.0f;
    ForceField_AddLayer(&s_foliageActiveForceField, suctionLayer);

    // Layer 1: Swirling vortex around vertical axis passing through target
    ForceLayer vortexLayer;
    vortexLayer.type = FORCE_VORTEX;
    vortexLayer.origin = targetPos;
    vortexLayer.direction = (Vector3){0, 1.0f, 0};
    vortexLayer.strength = swirlStrength;
    vortexLayer.radius = 0.0f;
    vortexLayer.falloff = 1.0f;
    vortexLayer.noiseScale = 0.0f;
    vortexLayer.noiseSpeed = 0.0f;
    ForceField_AddLayer(&s_foliageActiveForceField, vortexLayer);

    // Layer 2: Natural curling aerodynamic turbulence
    ForceLayer curlLayer;
    curlLayer.type = FORCE_NOISE_CURL;
    curlLayer.origin = targetPos;
    curlLayer.direction = (Vector3){0, 1.0f, 0};
    curlLayer.strength = suctionStrength * 0.20f;
    curlLayer.radius = 0.0f;
    curlLayer.falloff = 0.0f;
    curlLayer.noiseScale = 0.8f;
    curlLayer.noiseSpeed = 1.5f;
    ForceField_AddLayer(&s_foliageActiveForceField, curlLayer);

    s_foliageHasForceField = true;
}

void VFX_FoliageSystem_ClearHomingTarget(void)
{
    s_foliageHomingActive = false;
    s_foliageHasForceField = false;
    ForceField_Clear(&s_foliageActiveForceField);
}

bool VFX_FoliageSystem_IsHomingActive(void)
{
    return s_foliageHomingActive;
}

void VFX_FoliageSystem_Update(float dt, const ForceField *externalForceField)
{
    if (dt <= 0.0001f || s_foliageActiveCount <= 0) return;
    float time = (float)GetTime();

    const ForceField *activeFF = (externalForceField != NULL) ? externalForceField : (s_foliageHasForceField ? &s_foliageActiveForceField : NULL);

    for (int i = 0; i < VFX_FOLIAGE_POOL_CAPACITY; i++)
    {
        VFX_FoliageParticle *p = &s_foliagePool[i];
        if (!p->active) continue;

        p->age += dt;

        if (p->state == BOTANICAL_STATE_ATTACHED)
        {
            // Organic unfurling & wind sway while anchored
            if (p->growth < 1.0f) {
                p->growth += dt * 1.25f;
                if (p->growth > 1.0f) p->growth = 1.0f;
            }
        }
        else if (p->state == BOTANICAL_STATE_FREE)
        {
            // -----------------------------------------------------------------
            // PHYSICAL SIMULATION: Gravity, Planar Air Drag, Wind & Force Field
            // -----------------------------------------------------------------
            Vector3 accel = (Vector3){0, -9.81f, 0}; // Real-world gravity

            // 1. Planar aerodynamic drag (leaves fall like flat gliders)
            float speedSq = p->vel.x * p->vel.x + p->vel.y * p->vel.y + p->vel.z * p->vel.z;
            if (speedSq > 1e-4f)
            {
                float speed = sqrtf(speedSq);
                float dragMag = 0.5f * 1.225f * p->dragCoeff * speedSq * (p->scale * 0.1f) / p->mass;
                if (dragMag > 25.0f) dragMag = 25.0f;

                Vector3 dragForce = Vector3Scale(p->vel, -dragMag / speed);
                accel = Vector3Add(accel, dragForce);
            }

            // 2. Leaf aerodynamic flutter & sideways gliding
            p->flutterPhase += dt * p->flutterSpeed;
            float flutter = sinf(p->flutterPhase) * 1.8f;
            accel.x += cosf(p->rot.y) * flutter;
            accel.z += sinf(p->rot.y) * flutter;

            // 3. Environmental forest wind (acceleration toward target air velocity)
            Vector3 windAccel = Wind_EvaluateAcceleration(p->pos, time, p->vel);
            accel = Vector3Add(accel, windAccel);

            // 4. External Force Fields (Whirlwind, Vortex, or Target Suction)
            if (activeFF != NULL)
            {
                Vector3 ffAccel = ForceField_Evaluate(activeFF, p->pos, p->vel, time, (Vector3){0}, (Vector3){0, 1.0f, 0});
                accel = Vector3Add(accel, ffAccel);
            }

            // Integrate velocity & position
            p->vel = Vector3Add(p->vel, Vector3Scale(accel, dt));
            p->pos = Vector3Add(p->pos, Vector3Scale(p->vel, dt));

            // Integrate rotation tumbling
            p->rot = Vector3Add(p->rot, Vector3Scale(p->rotVel, dt));
            p->rotVel = Vector3Scale(p->rotVel, 0.985f); // Air rotational damping

            // Homing strike impact check: dissolve upon reaching target center
            if (s_foliageHomingActive)
            {
                float dx = p->pos.x - s_foliageHomingTarget.x;
                float dy = p->pos.y - s_foliageHomingTarget.y;
                float dz = p->pos.z - s_foliageHomingTarget.z;
                if (dx * dx + dy * dy + dz * dz < 0.40f * 0.40f)
                {
                    p->alpha -= dt * 4.5f;
                    if (p->alpha <= 0.0f) {
                        p->active = false;
                        s_foliageActiveCount--;
                        continue;
                    }
                }
            }

            // 5. Terrain contact & ground settling
            float groundH = MapManager_GetGroundHeightAt(p->pos.x, p->pos.z);
            if (p->pos.y <= groundH + 0.015f)
            {
                p->pos.y = groundH + 0.012f;
                p->vel.y = 0.0f;
                p->vel.x *= 0.55f;
                p->vel.z *= 0.55f;

                // When speed drops below threshold, settle flat on the terrain
                if (fabsf(p->vel.x) + fabsf(p->vel.z) < 0.15f)
                {
                    p->vel = (Vector3){0, 0, 0};
                    p->rotVel = (Vector3){0, 0, 0};
                    p->rot.x = 0.0f;
                    p->rot.z = 0.0f;
                    p->state = BOTANICAL_STATE_SETTLED;
                }
            }
        }
        else if (p->state == BOTANICAL_STATE_SETTLED)
        {
            // If homing vortex activates, pick settled leaves back up!
            if (s_foliageHomingActive)
            {
                p->state = BOTANICAL_STATE_FREE;
                p->groundRestTimer = 0.0f;
                p->vel.y = 1.8f + Foliage_Randf(&p->seed) * 1.5f;
                continue;
            }

            // Resting on the earth
            p->groundRestTimer += dt;
            if (p->groundRestTimer > 3.5f)
            {
                p->alpha -= dt * 0.65f;
                if (p->alpha <= 0.0f) {
                    p->active = false;
                    s_foliageActiveCount--;
                    continue;
                }
            }
        }

        // Lifetime expiration for free/detached leaves
        if (p->state != BOTANICAL_STATE_ATTACHED && p->age >= p->maxLifetime)
        {
            p->alpha -= dt * 1.5f;
            if (p->alpha <= 0.0f) {
                p->active = false;
                s_foliageActiveCount--;
            }
        }
    }
}

static inline Color Foliage_GetBaseColor(VFX_WoodVineStyle style, VFX_BotanicalKind kind)
{
    if (kind == BOTANICAL_KIND_FLOWER_HEAD || kind == BOTANICAL_KIND_PETAL)
    {
        switch (style)
        {
            case WOOD_VINE_STYLE_BLOOD_BRAMBLE: return (Color){225, 38, 65, 255};
            case WOOD_VINE_STYLE_GOLDEN_AMBER:  return (Color){245, 185, 35, 255};
            case WOOD_VINE_STYLE_WITHER_GHOST:  return (Color){185, 135, 245, 255};
            case WOOD_VINE_STYLE_JADE_EMERALD:
            default:                            return (Color){45, 195, 125, 255};
        }
    }
    else
    {
        switch (style)
        {
            case WOOD_VINE_STYLE_BLOOD_BRAMBLE: return (Color){180, 28, 42, 255};
            case WOOD_VINE_STYLE_GOLDEN_AMBER:  return (Color){215, 155, 35, 255};
            case WOOD_VINE_STYLE_WITHER_GHOST:  return (Color){145, 120, 175, 255};
            case WOOD_VINE_STYLE_JADE_EMERALD:
            default:                            return (Color){38, 185, 85, 255};
        }
    }
}

static void Foliage_RenderSingleLeaf(const VFX_FoliageParticle *p, Color col, bool isShadowPass)
{
    float sz = p->scale * p->growth;
    if (sz <= 0.005f) return;

    Vector3 forward, right, up;

    if (p->state == BOTANICAL_STATE_ATTACHED)
    {
        forward = p->anchorTangent;
        right = Vector3CrossProduct(p->anchorNormal, forward);
        up = p->anchorNormal;
    }
    else
    {
        float cy = cosf(p->rot.y), sy = sinf(p->rot.y);
        float cp = cosf(p->rot.x), sp = sinf(p->rot.x);
        float cr = cosf(p->rot.z), sr = sinf(p->rot.z);

        forward = (Vector3){ cy * cp, sp, sy * cp };
        up      = (Vector3){ -sy * sr - cy * sp * cr, cp * cr, cy * sr - sy * sp * cr };
        right   = Vector3CrossProduct(up, forward);
    }

    forward = Vector3Normalize(forward);
    right   = Vector3Normalize(right);
    up      = Vector3Normalize(up);

    float halfW = sz * 0.32f;
    float halfL = sz * 0.65f;
    float vLift = sz * 0.12f; // V-crease keel

    Vector3 stem = Vector3Add(p->pos, Vector3Scale(forward, -halfL * 0.3f));
    Vector3 tip  = Vector3Add(p->pos, Vector3Scale(forward,  halfL * 0.7f));
    Vector3 mid  = Vector3Add(p->pos, Vector3Scale(up, -vLift));

    Vector3 leftWing  = Vector3Add(Vector3Add(p->pos, Vector3Scale(right, -halfW)), Vector3Scale(up, vLift));
    Vector3 rightWing = Vector3Add(Vector3Add(p->pos, Vector3Scale(right,  halfW)), Vector3Scale(up, vLift));

    unsigned char a = (unsigned char)(p->alpha * (float)col.a);
    Color drawCol = (Color){ col.r, col.g, col.b, a };

    // Left V-crease half
    rlColor4ub(drawCol.r, drawCol.g, drawCol.b, drawCol.a);
    rlVertex3f(stem.x, stem.y, stem.z);
    rlVertex3f(leftWing.x, leftWing.y, leftWing.z);
    rlVertex3f(tip.x, tip.y, tip.z);
    rlVertex3f(mid.x, mid.y, mid.z);

    // Right V-crease half
    rlColor4ub(drawCol.r, drawCol.g, drawCol.b, drawCol.a);
    rlVertex3f(stem.x, stem.y, stem.z);
    rlVertex3f(mid.x, mid.y, mid.z);
    rlVertex3f(tip.x, tip.y, tip.z);
    rlVertex3f(rightWing.x, rightWing.y, rightWing.z);
}

static void Foliage_RenderSingleFlower(const VFX_FoliageParticle *p, Color col, bool isShadowPass)
{
    float sz = p->scale * p->growth;
    if (sz <= 0.005f) return;

    Vector3 forward = (Vector3){0, 1, 0};
    Vector3 right   = (Vector3){1, 0, 0};
    Vector3 up      = (Vector3){0, 0, 1};

    if (p->state != BOTANICAL_STATE_ATTACHED)
    {
        float cy = cosf(p->rot.y), sy = sinf(p->rot.y);
        float cp = cosf(p->rot.x), sp = sinf(p->rot.x);
        forward = (Vector3){ cy * cp, sp, sy * cp };
        right   = (Vector3){ -sy, 0, cy };
        up      = Vector3CrossProduct(forward, right);
    }
    else
    {
        forward = p->anchorNormal;
        right   = p->anchorTangent;
        up      = Vector3CrossProduct(forward, right);
    }

    forward = Vector3Normalize(forward);
    right   = Vector3Normalize(right);
    up      = Vector3Normalize(up);

    int petalCount = (p->flowerType == WOOD_FLOWER_TYPE_LOTUS) ? 6 : 5;
    float petalR = sz * 0.55f;
    unsigned char a = (unsigned char)(p->alpha * (float)col.a);
    Color drawCol = (Color){ col.r, col.g, col.b, a };

    Vector3 center = p->pos;

    for (int k = 0; k < petalCount; k++)
    {
        float ang0 = ((float)k / (float)petalCount) * 2.0f * PI;
        float ang1 = (((float)k + 0.85f) / (float)petalCount) * 2.0f * PI;

        Vector3 p0 = Vector3Add(center, Vector3Add(Vector3Scale(right, cosf(ang0) * petalR), Vector3Scale(up, sinf(ang0) * petalR)));
        Vector3 p1 = Vector3Add(center, Vector3Add(Vector3Scale(right, cosf(ang1) * petalR), Vector3Scale(up, sinf(ang1) * petalR)));
        Vector3 tip = Vector3Add(Vector3Lerp(p0, p1, 0.5f), Vector3Scale(forward, petalR * 0.45f));

        rlColor4ub(drawCol.r, drawCol.g, drawCol.b, drawCol.a);
        rlVertex3f(center.x, center.y, center.z);
        rlVertex3f(p0.x, p0.y, p0.z);
        rlVertex3f(tip.x, tip.y, tip.z);
        rlVertex3f(p1.x, p1.y, p1.z);
    }
}

void VFX_FoliageSystem_Draw(void)
{
    if (s_foliageActiveCount <= 0) return;

    rlDisableBackfaceCulling();
    BeginBlendMode(BLEND_ALPHA);
    rlBegin(RL_QUADS);

    for (int i = 0; i < VFX_FOLIAGE_POOL_CAPACITY; i++)
    {
        const VFX_FoliageParticle *p = &s_foliagePool[i];
        if (!p->active || p->alpha <= 0.01f || p->growth <= 0.01f) continue;

        Color col = Foliage_GetBaseColor(p->style, p->kind);

        if (p->kind == BOTANICAL_KIND_LEAF) {
            Foliage_RenderSingleLeaf(p, col, false);
        } else {
            Foliage_RenderSingleFlower(p, col, false);
        }
    }

    rlEnd();
    EndBlendMode();
    rlEnableBackfaceCulling();
}

void VFX_FoliageSystem_DrawShadowPass(void)
{
    if (s_foliageActiveCount <= 0) return;

    rlDisableBackfaceCulling();
    rlBegin(RL_QUADS);

    Color shadowCol = WHITE;

    for (int i = 0; i < VFX_FOLIAGE_POOL_CAPACITY; i++)
    {
        const VFX_FoliageParticle *p = &s_foliagePool[i];
        if (!p->active || p->alpha <= 0.15f || p->growth <= 0.05f) continue;

        if (p->kind == BOTANICAL_KIND_LEAF) {
            Foliage_RenderSingleLeaf(p, shadowCol, true);
        } else {
            Foliage_RenderSingleFlower(p, shadowCol, true);
        }
    }

    rlEnd();
    rlEnableBackfaceCulling();
}

// ============================================================================
// HIGH-LEVEL CONVENIENCE BOTANICAL API (Skills, Maps & Environmental Events)
// ============================================================================

int VFX_Foliage_SpawnFreeLeaves(Vector3 center, float radius, int count, float mass, VFX_WoodVineStyle style)
{
    VFX_FoliageSpawnParams p = VFX_FoliageSpawnParams_Default();
    p.kind = BOTANICAL_KIND_LEAF;
    p.leafShape = (style == WOOD_VINE_STYLE_BLOOD_BRAMBLE) ? WOOD_LEAF_SHAPE_MAPLE : WOOD_LEAF_SHAPE_OVAL;
    p.style = style;
    p.origin = center;
    p.radius = radius;
    p.count = count;
    p.attached = false;
    p.mass = (mass > 1e-4f) ? mass : 0.005f;
    p.initialVelocity = (Vector3){0, 0.8f, 0};
    p.velocitySpread = 1.6f;
    p.size = 0.16f;
    p.growth = 1.0f;
    p.lifetime = 10.0f;
    p.seed = (unsigned int)(fabsf(center.x) * 1000.0f + fabsf(center.z) * 100.0f + (float)count);
    return VFX_FoliageSystem_SpawnCluster(&p);
}

int VFX_Foliage_SpawnFreePetals(Vector3 center, float radius, int count, float mass, VFX_WoodFlowerType flowerType, VFX_WoodVineStyle style)
{
    VFX_FoliageSpawnParams p = VFX_FoliageSpawnParams_Default();
    p.kind = BOTANICAL_KIND_PETAL;
    p.flowerType = flowerType;
    p.style = style;
    p.origin = center;
    p.radius = radius;
    p.count = count;
    p.attached = false;
    p.mass = (mass > 1e-4f) ? mass : 0.002f;
    p.initialVelocity = (Vector3){0, 0.5f, 0};
    p.velocitySpread = 1.2f;
    p.size = 0.13f;
    p.growth = 1.0f;
    p.lifetime = 12.0f;
    p.seed = (unsigned int)(fabsf(center.x) * 1234.0f + fabsf(center.z) * 567.0f + (float)count);
    return VFX_FoliageSystem_SpawnCluster(&p);
}

int VFX_Foliage_SpawnAttachedLeaves(const VFX_BotanicalSocket *sockets, int socketCount, VFX_WoodLeafShape shape, VFX_WoodVineStyle style)
{
    if (sockets == NULL || socketCount <= 0) return 0;
    VFX_FoliageSpawnParams p = VFX_FoliageSpawnParams_Default();
    p.kind = BOTANICAL_KIND_LEAF;
    p.leafShape = shape;
    p.style = style;
    p.sockets = sockets;
    p.socketCount = socketCount;
    p.count = socketCount;
    p.attached = true;
    p.size = 0.16f;
    p.growth = 0.05f; // Starts budding and grows out
    p.lifetime = 99999.0f; // Persistent while attached
    p.seed = 778899;
    return VFX_FoliageSystem_SpawnCluster(&p);
}

int VFX_Foliage_SpawnAttachedFlowers(const VFX_BotanicalSocket *sockets, int socketCount, VFX_WoodFlowerType type, VFX_WoodVineStyle style)
{
    if (sockets == NULL || socketCount <= 0) return 0;
    VFX_FoliageSpawnParams p = VFX_FoliageSpawnParams_Default();
    p.kind = BOTANICAL_KIND_FLOWER_HEAD;
    p.flowerType = type;
    p.style = style;
    p.sockets = sockets;
    p.socketCount = socketCount;
    p.count = socketCount;
    p.attached = true;
    p.size = 0.14f;
    p.growth = 0.05f; // Starts as bud and blooms
    p.lifetime = 99999.0f;
    p.seed = 334455;
    return VFX_FoliageSystem_SpawnCluster(&p);
}

int VFX_Foliage_DetachInRadius(Vector3 center, float radius, Vector3 impulse)
{
    return VFX_FoliageSystem_DetachInRadius(center, radius, impulse);
}

// Composition Archetype Dispatches (ticked & drawn from visual_composer.c)
void VC_WoodFoliage_Update(float dt)
{
    VFX_FoliageSystem_Update(dt, NULL);
}

void VC_WoodFoliage_Draw3D(Camera3D cam)
{
    (void)cam;
    VFX_FoliageSystem_Draw();
}

#endif // VC_WOOD_FOLIAGE_SYSTEM_INL
