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
#include "core/time_fx.h"
#include "core/map_manager.h"
#include "core/wind/wind_system.h"
#include "core/force_field.h"
#include "core/motion/motion_body.h"
#include "core/composition/wood/vc_wood_botanical_math.h"
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
    MotionReceiver motionReceiver;
    float   mass;       // In kg (default ~0.005kg)
    float   densityKgM3;
    float   projectedAreaM2;
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
    p.mass = 0;
    p.bodyMaterial = BODY_LAMINA_LEAF_FRESH;
    p.densityKgM3 = 0;
    p.size = 0; // Species-resolved blade scale.
    p.growth = 1.0f;
    p.lifetime = 8.0f;
    p.seed = 445566;
    return p;
}

int VFX_FoliageSystem_SpawnCluster(const VFX_FoliageSpawnParams *params)
{
    if (params == NULL || params->count <= 0 || !isfinite(params->size) || (params->kind==BOTANICAL_KIND_FLOWER_HEAD && params->size <= 0) ||
        !isfinite(params->mass) || !isfinite(params->densityKgM3) ||
        (params->kind != BOTANICAL_KIND_FLOWER_HEAD &&
         (params->bodyMaterial < BODY_LAMINA_LEAF_DRY || params->bodyMaterial > BODY_LAMINA_PETAL_FRESH))) return 0;
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
        float sizeM=params->kind==BOTANICAL_KIND_LEAF ?
            Botanical_ResolveLeafSize(params->size,params->leafShape) :
            params->kind==BOTANICAL_KIND_PETAL ?
            Botanical_ResolvePetalSize(params->size,params->flowerType) : params->size;
        p->scale = sizeM * (0.85f + 0.30f * Foliage_Randf(&rng));
        p->growth = params->growth;
        p->wither = 0.0f;
        if (params->kind == BOTANICAL_KIND_FLOWER_HEAD) {
            /* Compound blossom proxy retained separately: not a sheet petal. */
            p->mass = params->mass > 0 ? params->mass : 0.005f;
            p->densityKgM3 = params->densityKgM3 > 0 ? params->densityKgM3 : 600;
            p->projectedAreaM2 = 0.5f*p->scale*p->scale;
            p->dragCoeff = 1.15f;
        } else {
            BodyLaminaPreset materialId=params->bodyMaterial;
            ThinLaminaMaterial material=BodyLaminaMaterial_Preset(materialId);
            float area=params->kind==BOTANICAL_KIND_LEAF ?
                Botanical_LeafPlanformArea(p->scale,p->leafShape) :
                Botanical_PetalPlanformArea(p->scale,p->flowerType);
            BodyPhysicalProperties physical=BodyPhysicalProperties_Lamina(area,&material);
            p->mass=params->mass>0?params->mass:physical.massKg;
            p->densityKgM3=params->densityKgM3>0?params->densityKgM3:physical.densityKgM3;
            p->projectedAreaM2=physical.projectedAreaM2;
            p->dragCoeff=physical.dragCoefficient;
        }
        if (!isfinite(p->mass) || p->mass <= 0 || !isfinite(1/p->mass)) {
            p->active=false; continue;
        }
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
            p->motionReceiver = (MotionReceiver){0};
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
            p->motionReceiver = (MotionReceiver){0};
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
            p->motionReceiver = (MotionReceiver){0};
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
    float time = (float)TimeFX_Elapsed();

    const ForceField *activeFF = (externalForceField != NULL) ? externalForceField : (s_foliageHasForceField ? &s_foliageActiveForceField : NULL);

    for (int i = 0; i < VFX_FOLIAGE_POOL_CAPACITY; i++)
    {
        VFX_FoliageParticle *p = &s_foliagePool[i];
        if (!p->active) continue;

        p->age += dt;

        ParticleDynamicsProfile body = {.inverseMassKg=1/p->mass,
            .gravityScale=1,.densityKgM3=p->densityKgM3,.windSusceptibility=1,
            .aerodynamicAreaM2=p->projectedAreaM2,
            .aerodynamicDragCoefficient=p->dragCoeff,.airDensityKgM3=1.225f};
        ReceiverConstraints constraints = {.mode=RECEIVER_FREE,.permittedAxes={1,1,1}};

        if(p->state==BOTANICAL_STATE_SETTLED) {
            BodyPhysicalProperties physicalBody = MotionBody_GetPhysicalProperties(&body);
            MediumProperties medium = MotionBody_GetMediumProperties(&body);
            medium.velocityMps = Wind_EvaluateBackgroundVelocity(p->pos,time);
            FieldSample sample;
            MotionFields_SampleBody(p->pos,p->vel,&physicalBody,&medium,&constraints,
                0,MOTION_RECEIVER_FOLIAGE,&p->motionReceiver,&sample);
            if((p->motionReceiver.guide && !p->motionReceiver.arrived) ||
                sample.accelerationMps2.y + sample.forceNewtons.y*body.inverseMassKg>9.81f ||
                sample.mediumVelocityMps.y>1.0f) {
                p->state=BOTANICAL_STATE_FREE;p->groundRestTimer=0;
            }
        }
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
            /* Free botanical bodies consume the same independent spatial
             * fields as particles. Area/mass preserves their leaf response;
             * orientation, flutter and terrain remain foliage-specific. */
            p->flutterPhase += dt*p->flutterSpeed;
            Vector3 flutter={(float)cosf(p->rot.y)*sinf(p->flutterPhase)*1.8f,0,
                             (float)sinf(p->rot.y)*sinf(p->flutterPhase)*1.8f};
            float remaining=fminf(dt,0.25f);
            bool destroyed=false;
            while(remaining>1e-6f) {
                float step=fminf(remaining,1.0f/120.0f);remaining-=step;
                MotionArrivalProfile post;
                if(p->motionReceiver.arrived&&MotionFields_GetArrival(p->motionReceiver.guide,&post)&&post.overrideDynamics) {
                    float inverseMass=body.inverseMassKg;body=post.dynamics;
                    if(body.inverseMassKg<=0)body.inverseMassKg=inverseMass;
                }
                BodyPhysicalProperties physicalBody = MotionBody_GetPhysicalProperties(&body);
                MediumProperties medium = MotionBody_GetMediumProperties(&body);
                medium.velocityMps = Wind_EvaluateBackgroundVelocity(p->pos,time);
                FieldSample sample;
                MotionFields_SampleBody(p->pos,p->vel,&physicalBody,&medium,&constraints,
                    step,MOTION_RECEIVER_FOLIAGE,&p->motionReceiver,&sample);
                Vector3 resolvedAir = sample.mediumIsAbsolute ? sample.mediumVelocityMps :
                    MotionVec_Add(medium.velocityMps,sample.mediumVelocityMps);
                Vector3 accel=MotionVec_Scale(flutter,
                    Botanical_FlutterAirWeight(MotionVec_Sub(resolvedAir,p->vel)));
                if(activeFF)accel=MotionVec_Add(accel,ForceField_Evaluate(activeFF,p->pos,p->vel,time,(Vector3){0},(Vector3){0,1,0}));
                p->vel=MotionBody_AdvanceFieldVelocity(p->vel,&body,accel,
                    (Vector3){0},&sample,medium.velocityMps,step);
                Vector3 previous=p->pos;p->pos=MotionVec_Add(p->pos,MotionVec_Scale(p->vel,step));
                bool arrived=p->motionReceiver.arrived;
                MotionArrivalMode action=MotionFields_AdvanceReceiver(&p->motionReceiver,previous,p->pos,p->vel);
                if(!arrived&&p->motionReceiver.arrived) {
                    MotionArrivalProfile arrival;
                    if(MotionFields_GetArrival(p->motionReceiver.guide,&arrival))
                        p->vel=ParticleDynamics_ApplyImpulse(p->vel,arrival.impulseNs,body.inverseMassKg);
                    if(action==MOTION_ARRIVAL_DESTROY){destroyed=true;break;}
                }
            }
            if(destroyed){p->active=false;s_foliageActiveCount--;continue;}

            // Integrate rotation tumbling
            p->rot = Vector3Add(p->rot, Vector3Scale(p->rotVel, dt));
            p->rotVel = Vector3Scale(p->rotVel, powf(0.985f, dt*60.0f)); // Air rotational damping

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
            p->motionReceiver = (MotionReceiver){0};
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

static inline void Foliage_GetFoliageColors(VFX_WoodVineStyle style, VFX_BotanicalKind kind,
                                            VFX_WoodFlowerType flowerType, float wither,
                                            Color *outBase, Color *outTip, Color *outAccent)
{
    Color cBase, cTip, cAccent;
    if (kind == BOTANICAL_KIND_LEAF)
    {
        switch (style)
        {
            case WOOD_VINE_STYLE_BLOOD_BRAMBLE:
                cBase   = (Color){160, 22, 38, 255};
                cTip    = (Color){255, 120, 145, 255};
                cAccent = (Color){255, 210, 225, 255}; // Radiant ruby chi spine
                break;
            case WOOD_VINE_STYLE_GOLDEN_AMBER:
                cBase   = (Color){195, 140, 28, 255};
                cTip    = (Color){255, 235, 110, 255};
                cAccent = (Color){255, 252, 190, 255}; // Radiant solar gold spine
                break;
            case WOOD_VINE_STYLE_WITHER_GHOST:
                cBase   = (Color){120, 95, 145, 255};
                cTip    = (Color){220, 180, 255, 255};
                cAccent = (Color){245, 230, 255, 255}; // Spectral amethyst chi spine
                break;
            case WOOD_VINE_STYLE_JADE_EMERALD:
            default:
                cBase   = (Color){18, 145, 72, 255};    // Rich celestial emerald
                cTip    = (Color){140, 255, 205, 255};  // Luminous spirit aqua edge
                cAccent = (Color){205, 255, 240, 255};  // Incandescent jade chi spine
                break;
        }

        if (wither > 0.02f)
        {
            float w = wither;
            Color autumnGold = (Color){205, 155, 38, 255};
            Color deadBrown  = (Color){85, 52, 25, 255};
            float p1 = w < 0.5f ? (w * 2.0f) : 1.0f;
            float p2 = w > 0.5f ? ((w - 0.5f) * 2.0f) : 0.0f;
            cBase.r = (unsigned char)(cBase.r * (1.0f - p1) + autumnGold.r * p1);
            cBase.g = (unsigned char)(cBase.g * (1.0f - p1) + autumnGold.g * p1);
            cBase.b = (unsigned char)(cBase.b * (1.0f - p1) + autumnGold.b * p1);
            cBase.r = (unsigned char)(cBase.r * (1.0f - p2) + deadBrown.r * p2);
            cBase.g = (unsigned char)(cBase.g * (1.0f - p2) + deadBrown.r * p2);
            cBase.b = (unsigned char)(cBase.b * (1.0f - p2) + deadBrown.r * p2);
            cTip = cBase;
            cAccent = cBase;
        }
    }
    else // Flower head or individual falling petal — NEVER GREEN!
    {
        if (flowerType == WOOD_FLOWER_TYPE_LOTUS)
        {
            // SACRED LOTUS (Hoa Sen): Celestial White-Pink base to vivid Lotus Rose tip + golden stamen core
            if (style == WOOD_VINE_STYLE_BLOOD_BRAMBLE) {
                cBase   = (Color){185, 25, 42, 255};
                cTip    = (Color){255, 110, 140, 255};
                cAccent = (Color){255, 220, 80, 255};
            } else if (style == WOOD_VINE_STYLE_GOLDEN_AMBER) {
                cBase   = (Color){255, 245, 190, 255};
                cTip    = (Color){255, 225, 90, 255};
                cAccent = (Color){255, 248, 140, 255};
            } else if (style == WOOD_VINE_STYLE_WITHER_GHOST) {
                cBase   = (Color){215, 200, 245, 255};
                cTip    = (Color){220, 185, 255, 255};
                cAccent = (Color){150, 240, 255, 255};
            } else {
                cBase   = (Color){255, 245, 250, 255};  // Translucent pearl blush base
                cTip    = (Color){255, 125, 185, 255};  // Radiant lotus rose magenta
                cAccent = (Color){255, 225, 55, 255};   // Glowing golden core
            }
        }
        else if (flowerType == WOOD_FLOWER_TYPE_ORCHID)
        {
            // CELESTIAL ORCHID (Hoa Lan): Silky Porcelain to Royal Orchid Violet
            if (style == WOOD_VINE_STYLE_BLOOD_BRAMBLE) {
                cBase   = (Color){245, 210, 220, 255};
                cTip    = (Color){255, 110, 145, 255};
                cAccent = (Color){255, 220, 90, 255};
            } else if (style == WOOD_VINE_STYLE_GOLDEN_AMBER) {
                cBase   = (Color){255, 248, 220, 255};
                cTip    = (Color){255, 215, 70, 255};
                cAccent = (Color){255, 230, 120, 255};
            } else if (style == WOOD_VINE_STYLE_WITHER_GHOST) {
                cBase   = (Color){230, 220, 250, 255};
                cTip    = (Color){215, 180, 255, 255};
                cAccent = (Color){140, 240, 255, 255};
            } else {
                cBase   = (Color){252, 248, 255, 255};
                cTip    = (Color){225, 115, 255, 255};
                cAccent = (Color){255, 225, 75, 255};
            }
        }
        else // WOOD_FLOWER_TYPE_PLUM (Hoa Mai / Hoa Đào)
        {
            // PLUM BLOSSOM (Hoa Mai / Đào): Soft Peach Pink or Golden Apricot
            if (style == WOOD_VINE_STYLE_BLOOD_BRAMBLE) {
                cBase   = (Color){255, 225, 230, 255};
                cTip    = (Color){255, 125, 155, 255};
                cAccent = (Color){255, 215, 65, 255};
            } else if (style == WOOD_VINE_STYLE_GOLDEN_AMBER) {
                cBase   = (Color){255, 250, 195, 255};
                cTip    = (Color){255, 230, 80, 255};
                cAccent = (Color){255, 150, 20, 255};
            } else if (style == WOOD_VINE_STYLE_WITHER_GHOST) {
                cBase   = (Color){235, 225, 245, 255};
                cTip    = (Color){225, 200, 255, 255};
                cAccent = (Color){140, 240, 255, 255};
            } else {
                cBase   = (Color){255, 245, 248, 255};
                cTip    = (Color){255, 145, 195, 255};
                cAccent = (Color){255, 225, 60, 255};
            }
        }

        if (wither > 0.05f)
        {
            float w = wither;
            Color wiltBrown = (Color){95, 60, 35, 255};
            cBase.r = (unsigned char)(cBase.r * (1.0f - w) + wiltBrown.r * w);
            cBase.g = (unsigned char)(cBase.g * (1.0f - w) + wiltBrown.g * w);
            cBase.b = (unsigned char)(cBase.b * (1.0f - w) + wiltBrown.b * w);
            cTip.r  = (unsigned char)(cTip.r * (1.0f - w) + wiltBrown.r * w);
            cTip.g  = (unsigned char)(cTip.g * (1.0f - w) + wiltBrown.r * w);
            cTip.b  = (unsigned char)(cTip.b * (1.0f - w) + wiltBrown.b * w);
        }
    }
    *outBase = cBase;
    *outTip = cTip;
    *outAccent = cAccent;
}

static inline Color Foliage_GetBaseColor(VFX_WoodVineStyle style, VFX_BotanicalKind kind)
{
    Color cBase, cTip, cAccent;
    Foliage_GetFoliageColors(style, kind, WOOD_FLOWER_TYPE_LOTUS, 0.0f, &cBase, &cTip, &cAccent);
    return cBase;
}

static void Foliage_RenderSingleLeaf(const VFX_FoliageParticle *p, Color cBase, Color cTip, Color cVein, bool isShadowPass)
{
    float sz = p->scale * p->growth;
    if (sz <= 0.005f) return;

    Vector3 forward, right, up;

    if (p->state == BOTANICAL_STATE_ATTACHED)
    {
        forward = p->anchorTangent;
        up      = p->anchorNormal;
        right   = Vector3CrossProduct(up, forward);
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

    Vector3 sunDir = Vector3Normalize(Environment_GetSunDirection());
    unsigned char alphaByte = (unsigned char)(p->alpha * 255.0f);

    float leafLen = sz;
    Vector3 vRoot = p->pos;

    // Tiny petiole stem
    if (!isShadowPass)
    {
        Vector3 vStemBase = Vector3Add(vRoot, Vector3Scale(forward, -leafLen * 0.22f));
        float stemLit = 0.35f + 0.65f * fmaxf(Vector3DotProduct(up, sunDir), 0.0f);
        rlColor4ub((unsigned char)(cVein.r * stemLit * 0.8f), (unsigned char)(cVein.g * stemLit * 0.8f), (unsigned char)(cVein.b * stemLit * 0.8f), alphaByte);
        rlNormal3f(up.x, up.y, up.z);
        Vector3 sLeft  = Vector3Add(vStemBase, Vector3Scale(right, -leafLen*.003f));
        Vector3 sRight = Vector3Add(vStemBase, Vector3Scale(right,  leafLen*.003f));
        rlVertex3f(sLeft.x, sLeft.y, sLeft.z);
        rlVertex3f(sRight.x, sRight.y, sRight.z);
        rlVertex3f(vRoot.x, vRoot.y, vRoot.z);
    }

    if (p->leafShape == WOOD_LEAF_SHAPE_WILLOW || p->leafShape == WOOD_LEAF_SHAPE_OVAL)
    {
        BotanicalProfile profile=p->leafShape==WOOD_LEAF_SHAPE_WILLOW ?
            Botanical_ProfileWillow():Botanical_ProfileOval();
        float lengthM=leafLen*(p->leafShape==WOOD_LEAF_SHAPE_WILLOW?1.45f:1.05f);
        float flutter=sinf(p->flutterPhase)*(p->state==BOTANICAL_STATE_FREE?.04f:.015f);
        Botanical_RenderBladeMesh(vRoot,forward,right,up,lengthM,&profile,
            profile.curl*.6f+flutter,profile.fold*.5f,.012f,
            cBase,ColorLerp(cBase,cTip,.45f),cTip,cVein,sunDir,false,isShadowPass,alphaByte);
    }
    else
    {
        // MAPLE: 3-lobed notched palmate silhouette with prominent sinus notches
        cVein=ColorLerp(cBase,cVein,.14f);
        cTip=ColorLerp(cBase,cTip,.65f);
        float mLen = leafLen * 0.95f;
        float mW   = mLen * 0.52f;

        Vector3 vSpineMid  = Vector3Add(vRoot, Vector3Scale(forward, mLen * 0.45f));
        Vector3 vTip       = Vector3Add(vRoot, Vector3Scale(forward, mLen * 1.05f));
        Vector3 vLeftLobe  = Vector3Add(vRoot, Vector3Add(Vector3Scale(forward, mLen * 0.60f), Vector3Scale(right, -mW * 1.15f)));
        Vector3 vRightLobe = Vector3Add(vRoot, Vector3Add(Vector3Scale(forward, mLen * 0.60f), Vector3Scale(right,  mW * 1.15f)));
        Vector3 vNotchL    = Vector3Add(vRoot, Vector3Add(Vector3Scale(forward, mLen * 0.42f), Vector3Scale(right, -mW * 0.42f)));
        Vector3 vNotchR    = Vector3Add(vRoot, Vector3Add(Vector3Scale(forward, mLen * 0.42f), Vector3Scale(right,  mW * 0.42f)));
        Vector3 vBaseL     = Vector3Add(vRoot, Vector3Scale(right, -mW * 0.35f));
        Vector3 vBaseR     = Vector3Add(vRoot, Vector3Scale(right,  mW * 0.35f));

        Vector3 nC = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(vNotchL, vRoot), Vector3Subtract(vTip, vRoot)));
        Vector3 nL = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(vBaseL, vRoot), Vector3Subtract(vLeftLobe, vRoot)));
        Vector3 nR = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(vRightLobe, vRoot), Vector3Subtract(vBaseR, vRoot)));

        if (!isShadowPass)
        {
            float dC = 0.36f + 0.64f * Botanical_WrapDiffuse(nC, sunDir) + 0.38f * Botanical_WrapDiffuse(Vector3Negate(nC), sunDir);
            float dL = 0.36f + 0.64f * Botanical_WrapDiffuse(nL, sunDir) + 0.38f * Botanical_WrapDiffuse(Vector3Negate(nL), sunDir);
            float dR = 0.36f + 0.64f * Botanical_WrapDiffuse(nR, sunDir) + 0.38f * Botanical_WrapDiffuse(Vector3Negate(nR), sunDir);

            // Center lobe
            rlColor4ub((unsigned char)(cVein.r * dC), (unsigned char)(cVein.g * dC), (unsigned char)(cVein.b * dC), alphaByte);
            rlNormal3f(nC.x, nC.y, nC.z); rlVertex3f(vSpineMid.x, vSpineMid.y, vSpineMid.z);
            rlColor4ub((unsigned char)(cBase.r * dC), (unsigned char)(cBase.g * dC), (unsigned char)(cBase.b * dC), alphaByte);
            rlNormal3f(nC.x, nC.y, nC.z); rlVertex3f(vNotchL.x, vNotchL.y, vNotchL.z);
            rlColor4ub((unsigned char)(cTip.r * dC),  (unsigned char)(cTip.g * dC),  (unsigned char)(cTip.b * dC), alphaByte);
            rlNormal3f(nC.x, nC.y, nC.z); rlVertex3f(vTip.x, vTip.y, vTip.z);

            rlColor4ub((unsigned char)(cVein.r * dC), (unsigned char)(cVein.g * dC), (unsigned char)(cVein.b * dC), alphaByte);
            rlNormal3f(nC.x, nC.y, nC.z); rlVertex3f(vSpineMid.x, vSpineMid.y, vSpineMid.z);
            rlColor4ub((unsigned char)(cTip.r * dC),  (unsigned char)(cTip.g * dC),  (unsigned char)(cTip.b * dC), alphaByte);
            rlNormal3f(nC.x, nC.y, nC.z); rlVertex3f(vTip.x, vTip.y, vTip.z);
            rlColor4ub((unsigned char)(cBase.r * dC), (unsigned char)(cBase.g * dC), (unsigned char)(cBase.b * dC), alphaByte);
            rlNormal3f(nC.x, nC.y, nC.z); rlVertex3f(vNotchR.x, vNotchR.y, vNotchR.z);

            // Left lobe
            rlColor4ub((unsigned char)(cVein.r * dL), (unsigned char)(cVein.g * dL), (unsigned char)(cVein.b * dL), alphaByte);
            rlNormal3f(nL.x, nL.y, nL.z); rlVertex3f(vRoot.x, vRoot.y, vRoot.z);
            rlColor4ub((unsigned char)(cBase.r * dL), (unsigned char)(cBase.g * dL), (unsigned char)(cBase.b * dL), alphaByte);
            rlNormal3f(nL.x, nL.y, nL.z); rlVertex3f(vBaseL.x, vBaseL.y, vBaseL.z);
            rlColor4ub((unsigned char)(cTip.r * dL),  (unsigned char)(cTip.g * dL),  (unsigned char)(cTip.b * dL), alphaByte);
            rlNormal3f(nL.x, nL.y, nL.z); rlVertex3f(vLeftLobe.x, vLeftLobe.y, vLeftLobe.z);

            rlColor4ub((unsigned char)(cVein.r * dL), (unsigned char)(cVein.g * dL), (unsigned char)(cVein.b * dL), alphaByte);
            rlNormal3f(nL.x, nL.y, nL.z); rlVertex3f(vRoot.x, vRoot.y, vRoot.z);
            rlColor4ub((unsigned char)(cTip.r * dL),  (unsigned char)(cTip.g * dL),  (unsigned char)(cTip.b * dL), alphaByte);
            rlNormal3f(nL.x, nL.y, nL.z); rlVertex3f(vLeftLobe.x, vLeftLobe.y, vLeftLobe.z);
            rlColor4ub((unsigned char)(cBase.r * dL), (unsigned char)(cBase.g * dL), (unsigned char)(cBase.b * dL), alphaByte);
            rlNormal3f(nL.x, nL.y, nL.z); rlVertex3f(vNotchL.x, vNotchL.y, vNotchL.z);

            // Right lobe
            rlColor4ub((unsigned char)(cVein.r * dR), (unsigned char)(cVein.g * dR), (unsigned char)(cVein.b * dR), alphaByte);
            rlNormal3f(nR.x, nR.y, nR.z); rlVertex3f(vRoot.x, vRoot.y, vRoot.z);
            rlColor4ub((unsigned char)(cTip.r * dR),  (unsigned char)(cTip.g * dR),  (unsigned char)(cTip.b * dR), alphaByte);
            rlNormal3f(nR.x, nR.y, nR.z); rlVertex3f(vRightLobe.x, vRightLobe.y, vRightLobe.z);
            rlColor4ub((unsigned char)(cBase.r * dR), (unsigned char)(cBase.g * dR), (unsigned char)(cBase.b * dR), alphaByte);
            rlNormal3f(nR.x, nR.y, nR.z); rlVertex3f(vBaseR.x, vBaseR.y, vBaseR.z);

            rlColor4ub((unsigned char)(cVein.r * dR), (unsigned char)(cVein.g * dR), (unsigned char)(cVein.b * dR), alphaByte);
            rlNormal3f(nR.x, nR.y, nR.z); rlVertex3f(vRoot.x, vRoot.y, vRoot.z);
            rlColor4ub((unsigned char)(cBase.r * dR), (unsigned char)(cBase.g * dR), (unsigned char)(cBase.b * dR), alphaByte);
            rlNormal3f(nR.x, nR.y, nR.z); rlVertex3f(vNotchR.x, vNotchR.y, vNotchR.z);
            rlColor4ub((unsigned char)(cTip.r * dR),  (unsigned char)(cTip.g * dR),  (unsigned char)(cTip.b * dR), alphaByte);
            rlNormal3f(nR.x, nR.y, nR.z); rlVertex3f(vRightLobe.x, vRightLobe.y, vRightLobe.z);
        }
        else
        {
            rlVertex3f(vSpineMid.x, vSpineMid.y, vSpineMid.z); rlVertex3f(vNotchL.x, vNotchL.y, vNotchL.z); rlVertex3f(vTip.x, vTip.y, vTip.z);
            rlVertex3f(vSpineMid.x, vSpineMid.y, vSpineMid.z); rlVertex3f(vTip.x, vTip.y, vTip.z); rlVertex3f(vNotchR.x, vNotchR.y, vNotchR.z);
            rlVertex3f(vRoot.x, vRoot.y, vRoot.z); rlVertex3f(vBaseL.x, vBaseL.y, vBaseL.z); rlVertex3f(vLeftLobe.x, vLeftLobe.y, vLeftLobe.z);
            rlVertex3f(vRoot.x, vRoot.y, vRoot.z); rlVertex3f(vLeftLobe.x, vLeftLobe.y, vLeftLobe.z); rlVertex3f(vNotchL.x, vNotchL.y, vNotchL.z);
            rlVertex3f(vRoot.x, vRoot.y, vRoot.z); rlVertex3f(vRightLobe.x, vRightLobe.y, vRightLobe.z); rlVertex3f(vBaseR.x, vBaseR.y, vBaseR.z);
            rlVertex3f(vRoot.x, vRoot.y, vRoot.z); rlVertex3f(vNotchR.x, vNotchR.y, vNotchR.z); rlVertex3f(vRightLobe.x, vRightLobe.y, vRightLobe.z);
        }
    }
}

static void Foliage_RenderSingleFlower(const VFX_FoliageParticle *p, Color cBase, Color cTip, Color cStamen, bool isShadowPass)
{
    float sz = p->scale * p->growth;
    if (sz <= 0.005f) return;

    Vector3 forward, right, up;

    if (p->state == BOTANICAL_STATE_ATTACHED)
    {
        forward = p->anchorNormal;
        right   = p->anchorTangent;
        up      = Vector3CrossProduct(forward, right);
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

    Vector3 sunDir = Vector3Normalize(Environment_GetSunDirection());
    unsigned char alphaByte = (unsigned char)(p->alpha * 255.0f);
    Vector3 center = p->pos;

    // ── CASE 1: Individual Airborne Tumbling Petal (BOTANICAL_KIND_PETAL) ────
    if (p->kind == BOTANICAL_KIND_PETAL)
    {
        BotanicalProfile prof=Botanical_DetachedPetalProfile(p->flowerType);

        float petalLen = sz * 1.25f;
        float curve = petalLen * 0.16f;
        Color cMid = (Color){
            (unsigned char)((cBase.r + cTip.r) / 2),
            (unsigned char)((cBase.g + cTip.g) / 2),
            (unsigned char)((cBase.b + cTip.b) / 2),
            alphaByte
        };
        Color cGlowRim = cStamen;

        Botanical_RenderSinglePetalMesh(
            center, forward, right, up, petalLen, &prof, curve,
            cBase, cMid, cTip, cGlowRim, sunDir, isShadowPass, alphaByte
        );
        return;
    }

    // ── CASE 2: Full Blossom Head (BOTANICAL_KIND_FLOWER_HEAD) ────────────────
    float bloomFactor = p->growth;
    float blossomScale = sz;

    // 3D Morphological Calyx Sepals (Lá Đài Bảo Vệ Nụ & Khung Nở)
    float calyxProg = bloomFactor;
    float calyxSpread = 0.08f + calyxProg * 1.20f;
    float calyxLen = blossomScale * 0.45f;
    float calyxW   = calyxLen * 0.40f;
    Color sepalBase = (Color){24, 90, 42, alphaByte};
    Color sepalTip  = (Color){60, 185, 95, alphaByte};
    Botanical_RenderCalyxSepals(
        center, forward, right, up,
        calyxLen, calyxW, calyxSpread,
        sepalBase, sepalTip,
        sunDir, isShadowPass, alphaByte
    );

    if (p->flowerType == WOOD_FLOWER_TYPE_LOTUS)
    {
        const int outerCount = 6;
        const int innerCount = 5;
        const int layers = 2;

        for (int layer = 0; layer < layers; layer++)
        {
            int pCount = (layer == 0) ? outerCount : innerCount;
            float layerScale = (layer == 0) ? blossomScale : blossomScale * 0.75f;
            float layerSpread = (layer == 0) ? (10.0f + bloomFactor * 60.0f) * DEG2RAD : (8.0f + bloomFactor * 30.0f) * DEG2RAD;
            float layerOffset = (layer == 1) ? (PI / (float)pCount) : 0.0f;
            float petalW = layerScale * 0.48f;

            for (int k = 0; k < pCount; k++)
            {
                float ang = ((float)k / (float)pCount) * 2.0f * PI + layerOffset;
                Vector3 radDir = Vector3Normalize(Vector3Add(Vector3Scale(right, cosf(ang)), Vector3Scale(up, sinf(ang))));
                Vector3 pDir   = Vector3Normalize(Vector3Add(Vector3Scale(forward, cosf(layerSpread)), Vector3Scale(radDir, sinf(layerSpread))));
                Vector3 pSide  = Vector3Normalize(Vector3CrossProduct(pDir, forward));

                Vector3 vBase  = center;
                Vector3 vMid   = Vector3Add(center, Vector3Scale(pDir, layerScale * 0.52f));
                Vector3 vLeft  = Vector3Add(vMid, Vector3Scale(pSide, -petalW * 0.5f));
                Vector3 vRight = Vector3Add(vMid, Vector3Scale(pSide,  petalW * 0.5f));
                Vector3 vTip   = Vector3Add(center, Vector3Scale(pDir, layerScale));

                Vector3 norm = Vector3Normalize(Vector3CrossProduct(pSide, pDir));
                if (!isShadowPass)
                {
                    float diff = 0.36f + 0.64f * fmaxf(Vector3DotProduct(norm, sunDir), 0.0f) + 0.40f * fmaxf(-Vector3DotProduct(norm, sunDir), 0.0f);
                    rlColor4ub((unsigned char)(cBase.r * diff), (unsigned char)(cBase.g * diff), (unsigned char)(cBase.b * diff), alphaByte);
                    rlNormal3f(norm.x, norm.y, norm.z); rlVertex3f(vBase.x, vBase.y, vBase.z);
                    rlColor4ub((unsigned char)(cTip.r * diff),  (unsigned char)(cTip.g * diff),  (unsigned char)(cTip.b * diff), alphaByte);
                    rlNormal3f(norm.x, norm.y, norm.z); rlVertex3f(vLeft.x, vLeft.y, vLeft.z);
                    rlColor4ub((unsigned char)(cTip.r * diff),  (unsigned char)(cTip.g * diff),  (unsigned char)(cTip.b * diff), alphaByte);
                    rlNormal3f(norm.x, norm.y, norm.z); rlVertex3f(vRight.x, vRight.y, vRight.z);

                    rlColor4ub((unsigned char)(cTip.r * diff), (unsigned char)(cTip.g * diff), (unsigned char)(cTip.b * diff), alphaByte);
                    rlNormal3f(norm.x, norm.y, norm.z); rlVertex3f(vLeft.x, vLeft.y, vLeft.z);
                    rlNormal3f(norm.x, norm.y, norm.z); rlVertex3f(vTip.x, vTip.y, vTip.z);
                    rlNormal3f(norm.x, norm.y, norm.z); rlVertex3f(vRight.x, vRight.y, vRight.z);
                }
                else
                {
                    rlVertex3f(vBase.x, vBase.y, vBase.z); rlVertex3f(vLeft.x, vLeft.y, vLeft.z); rlVertex3f(vRight.x, vRight.y, vRight.z);
                    rlVertex3f(vLeft.x, vLeft.y, vLeft.z); rlVertex3f(vTip.x, vTip.y, vTip.z); rlVertex3f(vRight.x, vRight.y, vRight.z);
                }
            }
        }

        // Golden central receptacle pod
        if (!isShadowPass && bloomFactor > 0.2f)
        {
            float podR = blossomScale * 0.22f;
            Vector3 podCenter = Vector3Add(center, Vector3Scale(forward, podR * 0.6f));
            for (int i = 0; i < 6; i++)
            {
                float a1 = ((float)i / 6.0f) * 2.0f * PI;
                float a2 = ((float)(i + 1) / 6.0f) * 2.0f * PI;
                Vector3 p1 = Vector3Add(podCenter, Vector3Add(Vector3Scale(right, cosf(a1) * podR), Vector3Scale(up, sinf(a1) * podR)));
                Vector3 p2 = Vector3Add(podCenter, Vector3Add(Vector3Scale(right, cosf(a2) * podR), Vector3Scale(up, sinf(a2) * podR)));
                rlColor4ub(cStamen.r, cStamen.g, cStamen.b, alphaByte);
                rlVertex3f(podCenter.x, podCenter.y, podCenter.z);
                rlVertex3f(p1.x, p1.y, p1.z);
                rlVertex3f(p2.x, p2.y, p2.z);
            }
        }
    }
    else if (p->flowerType == WOOD_FLOWER_TYPE_ORCHID)
    {
        float oScale = blossomScale * 1.1f;
        float oSpread = (12.0f + bloomFactor * 52.0f) * DEG2RAD;
        float angles[5]  = { 0.0f, 85.0f * DEG2RAD, -85.0f * DEG2RAD, 140.0f * DEG2RAD, -140.0f * DEG2RAD };
        float lengths[5] = { oScale * 1.05f, oScale * 0.95f, oScale * 0.95f, oScale * 0.85f, oScale * 0.85f };
        float widths[5]  = { oScale * 0.40f, oScale * 0.46f, oScale * 0.46f, oScale * 0.36f, oScale * 0.36f };

        for (int k = 0; k < 5; k++)
        {
            float ang = angles[k];
            Vector3 radDir = Vector3Normalize(Vector3Add(Vector3Scale(right, cosf(ang)), Vector3Scale(up, sinf(ang))));
            Vector3 pDir   = Vector3Normalize(Vector3Add(Vector3Scale(forward, cosf(oSpread)), Vector3Scale(radDir, sinf(oSpread))));
            Vector3 pSide  = Vector3Normalize(Vector3CrossProduct(pDir, forward));

            Vector3 vBase  = center;
            Vector3 vMid   = Vector3Add(center, Vector3Scale(pDir, lengths[k] * 0.52f));
            Vector3 vLeft  = Vector3Add(vMid, Vector3Scale(pSide, -widths[k] * 0.5f));
            Vector3 vRight = Vector3Add(vMid, Vector3Scale(pSide,  widths[k] * 0.5f));
            Vector3 vTip   = Vector3Add(center, Vector3Scale(pDir, lengths[k]));

            Vector3 norm = Vector3Normalize(Vector3CrossProduct(pSide, pDir));
            if (!isShadowPass)
            {
                float diff = 0.36f + 0.64f * fmaxf(Vector3DotProduct(norm, sunDir), 0.0f) + 0.40f * fmaxf(-Vector3DotProduct(norm, sunDir), 0.0f);
                rlColor4ub((unsigned char)(cBase.r * diff), (unsigned char)(cBase.g * diff), (unsigned char)(cBase.b * diff), alphaByte);
                rlNormal3f(norm.x, norm.y, norm.z); rlVertex3f(vBase.x, vBase.y, vBase.z);
                rlColor4ub((unsigned char)(cTip.r * diff),  (unsigned char)(cTip.g * diff),  (unsigned char)(cTip.b * diff), alphaByte);
                rlNormal3f(norm.x, norm.y, norm.z); rlVertex3f(vLeft.x, vLeft.y, vLeft.z);
                rlColor4ub((unsigned char)(cTip.r * diff),  (unsigned char)(cTip.g * diff),  (unsigned char)(cTip.b * diff), alphaByte);
                rlNormal3f(norm.x, norm.y, norm.z); rlVertex3f(vRight.x, vRight.y, vRight.z);

                rlColor4ub((unsigned char)(cTip.r * diff), (unsigned char)(cTip.g * diff), (unsigned char)(cTip.b * diff), alphaByte);
                rlNormal3f(norm.x, norm.y, norm.z); rlVertex3f(vLeft.x, vLeft.y, vLeft.z);
                rlNormal3f(norm.x, norm.y, norm.z); rlVertex3f(vTip.x, vTip.y, vTip.z);
                rlNormal3f(norm.x, norm.y, norm.z); rlVertex3f(vRight.x, vRight.y, vRight.z);
            }
            else
            {
                rlVertex3f(vBase.x, vBase.y, vBase.z); rlVertex3f(vLeft.x, vLeft.y, vLeft.z); rlVertex3f(vRight.x, vRight.y, vRight.z);
                rlVertex3f(vLeft.x, vLeft.y, vLeft.z); rlVertex3f(vTip.x, vTip.y, vTip.z); rlVertex3f(vRight.x, vRight.y, vRight.z);
            }
        }

        // Orchid Labellum Lip
        if (bloomFactor > 0.15f)
        {
            float lipLen = oScale * 1.2f;
            float lipW   = oScale * 0.65f;
            Vector3 lipDir = Vector3Normalize(Vector3Add(Vector3Scale(forward, 0.45f), Vector3Scale(up, -0.85f)));
            Vector3 lipSide = Vector3Normalize(Vector3CrossProduct(lipDir, forward));

            Vector3 lBase  = center;
            Vector3 lMid   = Vector3Add(center, Vector3Scale(lipDir, lipLen * 0.50f));
            Vector3 lLeft  = Vector3Add(lMid, Vector3Scale(lipSide, -lipW * 0.5f));
            Vector3 lRight = Vector3Add(lMid, Vector3Scale(lipSide,  lipW * 0.5f));
            Vector3 lTip   = Vector3Add(center, Vector3Scale(lipDir, lipLen));

            if (!isShadowPass)
            {
                rlColor4ub(cBase.r, cBase.g, cBase.b, alphaByte);
                rlVertex3f(lBase.x, lBase.y, lBase.z);
                rlColor4ub(cStamen.r, cStamen.g, cStamen.b, alphaByte);
                rlVertex3f(lLeft.x, lLeft.y, lLeft.z);
                rlColor4ub(cStamen.r, cStamen.g, cStamen.b, alphaByte);
                rlVertex3f(lRight.x, lRight.y, lRight.z);

                rlColor4ub(cStamen.r, cStamen.g, cStamen.b, alphaByte);
                rlVertex3f(lLeft.x, lLeft.y, lLeft.z);
                rlColor4ub(cTip.r, cTip.g, cTip.b, alphaByte);
                rlVertex3f(lTip.x, lTip.y, lTip.z);
                rlColor4ub(cStamen.r, cStamen.g, cStamen.b, alphaByte);
                rlVertex3f(lRight.x, lRight.y, lRight.z);
            }
            else
            {
                rlVertex3f(lBase.x, lBase.y, lBase.z); rlVertex3f(lLeft.x, lLeft.y, lLeft.z); rlVertex3f(lRight.x, lRight.y, lRight.z);
                rlVertex3f(lLeft.x, lLeft.y, lLeft.z); rlVertex3f(lTip.x, lTip.y, lTip.z); rlVertex3f(lRight.x, lRight.y, lRight.z);
            }
        }
    }
    else
    {
        // PLUM BLOSSOM (Hoa Mai / Hoa Đào): 5 rounded silky petals + central stamen burst
        const int pCount = 5;
        float spreadAngle = (10.0f + bloomFactor * 62.0f) * DEG2RAD;
        float petalLen = blossomScale * 0.95f;
        float petalW   = petalLen * 0.66f;

        for (int k = 0; k < pCount; k++)
        {
            float ang = ((float)k / (float)pCount) * 2.0f * PI;
            Vector3 radDir = Vector3Normalize(Vector3Add(Vector3Scale(right, cosf(ang)), Vector3Scale(up, sinf(ang))));
            Vector3 pDir   = Vector3Normalize(Vector3Add(Vector3Scale(forward, cosf(spreadAngle)), Vector3Scale(radDir, sinf(spreadAngle))));
            Vector3 pSide  = Vector3Normalize(Vector3CrossProduct(pDir, forward));

            Vector3 vBase  = center;
            Vector3 vMid   = Vector3Add(center, Vector3Scale(pDir, petalLen * 0.50f));
            Vector3 vLeft  = Vector3Add(vMid, Vector3Scale(pSide, -petalW * 0.5f));
            Vector3 vRight = Vector3Add(vMid, Vector3Scale(pSide,  petalW * 0.5f));
            Vector3 vTip   = Vector3Add(center, Vector3Scale(pDir, petalLen));

            Vector3 norm = Vector3Normalize(Vector3CrossProduct(pSide, pDir));
            if (!isShadowPass)
            {
                float diff = 0.36f + 0.64f * fmaxf(Vector3DotProduct(norm, sunDir), 0.0f) + 0.40f * fmaxf(-Vector3DotProduct(norm, sunDir), 0.0f);
                rlColor4ub((unsigned char)(cBase.r * diff), (unsigned char)(cBase.g * diff), (unsigned char)(cBase.b * diff), alphaByte);
                rlNormal3f(norm.x, norm.y, norm.z); rlVertex3f(vBase.x, vBase.y, vBase.z);
                rlColor4ub((unsigned char)(cTip.r * diff),  (unsigned char)(cTip.g * diff),  (unsigned char)(cTip.b * diff), alphaByte);
                rlNormal3f(norm.x, norm.y, norm.z); rlVertex3f(vLeft.x, vLeft.y, vLeft.z);
                rlColor4ub((unsigned char)(cTip.r * diff),  (unsigned char)(cTip.g * diff),  (unsigned char)(cTip.b * diff), alphaByte);
                rlNormal3f(norm.x, norm.y, norm.z); rlVertex3f(vRight.x, vRight.y, vRight.z);

                rlColor4ub((unsigned char)(cTip.r * diff), (unsigned char)(cTip.g * diff), (unsigned char)(cTip.b * diff), alphaByte);
                rlNormal3f(norm.x, norm.y, norm.z); rlVertex3f(vLeft.x, vLeft.y, vLeft.z);
                rlNormal3f(norm.x, norm.y, norm.z); rlVertex3f(vTip.x, vTip.y, vTip.z);
                rlNormal3f(norm.x, norm.y, norm.z); rlVertex3f(vRight.x, vRight.y, vRight.z);
            }
            else
            {
                rlVertex3f(vBase.x, vBase.y, vBase.z); rlVertex3f(vLeft.x, vLeft.y, vLeft.z); rlVertex3f(vRight.x, vRight.y, vRight.z);
                rlVertex3f(vLeft.x, vLeft.y, vLeft.z); rlVertex3f(vTip.x, vTip.y, vTip.z); rlVertex3f(vRight.x, vRight.y, vRight.z);
            }
        }

        // Radiating stamen filaments
        if (!isShadowPass && bloomFactor > 0.20f)
        {
            float stamenR = petalLen * 0.32f;
            for (int i = 0; i < 8; i++)
            {
                float a = ((float)i / 8.0f) * 2.0f * PI;
                Vector3 fBase = center;
                Vector3 fTip  = Vector3Add(center, Vector3Add(
                    Vector3Scale(right, cosf(a) * stamenR),
                    Vector3Add(Vector3Scale(up, sinf(a) * stamenR), Vector3Scale(forward, stamenR * 1.1f))));
                rlColor4ub(cStamen.r, cStamen.g, cStamen.b, alphaByte);
                rlVertex3f(fBase.x, fBase.y, fBase.z);
                rlVertex3f(fTip.x, fTip.y, fTip.z);
                rlVertex3f(fBase.x, fBase.y, fBase.z);
            }
        }
    }
}

void VFX_FoliageSystem_Draw(void)
{
    if (s_foliageActiveCount <= 0) return;

    rlDisableBackfaceCulling();
    BeginBlendMode(BLEND_ALPHA);
    rlBegin(RL_TRIANGLES);

    for (int i = 0; i < VFX_FOLIAGE_POOL_CAPACITY; i++)
    {
        const VFX_FoliageParticle *p = &s_foliagePool[i];
        if (!p->active || p->alpha <= 0.01f || p->growth <= 0.01f) continue;

        rlCheckRenderBatchLimit(48);

        Color cBase, cTip, cAccent;
        Foliage_GetFoliageColors(p->style, p->kind, p->flowerType, p->wither, &cBase, &cTip, &cAccent);

        if (p->kind == BOTANICAL_KIND_LEAF) {
            Foliage_RenderSingleLeaf(p, cBase, cTip, cAccent, false);
        } else {
            Foliage_RenderSingleFlower(p, cBase, cTip, cAccent, false);
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
    rlBegin(RL_TRIANGLES);

    Color shadowCol = WHITE;

    for (int i = 0; i < VFX_FOLIAGE_POOL_CAPACITY; i++)
    {
        const VFX_FoliageParticle *p = &s_foliagePool[i];
        if (!p->active || p->alpha <= 0.15f || p->growth <= 0.05f) continue;

        rlCheckRenderBatchLimit(48);

        if (p->kind == BOTANICAL_KIND_LEAF) {
            Foliage_RenderSingleLeaf(p, shadowCol, shadowCol, shadowCol, true);
        } else {
            Foliage_RenderSingleFlower(p, shadowCol, shadowCol, shadowCol, true);
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
    p.mass = mass > 0 ? mass : 0;
    p.initialVelocity = (Vector3){0, 0.8f, 0};
    p.velocitySpread = 1.6f;
    p.size = 0; // Species-resolved blade scale.
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
    p.mass = mass > 0 ? mass : 0;
    p.bodyMaterial = BODY_LAMINA_PETAL_FRESH;
    p.initialVelocity = (Vector3){0, 0.5f, 0};
    p.velocitySpread = 1.2f;
    p.size = 0; // Species-resolved petal scale.
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
    p.size = 0; // Species-resolved blade scale.
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
