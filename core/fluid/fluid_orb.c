#include "core/fluid/fluid_orb.h"

#include "core/fluid/fluid_impact.h"
#include "core/fluid/fluid_surface.h"
#include "core/force_field.h"
#include "core/particles/particle_manager.h"
#include "core/presets/vc_material.h"
#include "raymath.h"
#include <math.h>

#define FLUID_WATER_ORB_MAX 2
#define FLUID_WATER_ORB_PARTICLES 2000

typedef enum { ORB_FLIGHT, ORB_IMPACT, ORB_SETTLE } FluidWaterOrbPhase;
typedef struct {
    Vector3 start, target, center, velocity, hitNormal, impactVelocity;
    float age, phaseAge, travelTime, radius, force01;
    Color body, glow, soft;
    ForceField field;
    ParticleEmitterHandle emitter;
    FluidWaterOrbPhase phase;
    bool active;
} FluidWaterOrb;

static FluidWaterOrb s_orbs[FLUID_WATER_ORB_MAX];
static int s_nextOrb;
/* Shared only while spawning: ParticleManager_EmitBatch consumes it
 * immediately, avoiding a multi-megabyte stack frame. */
static ParticleConfig s_spawnParticles[FLUID_WATER_ORB_PARTICLES];

static bool FluidWaterOrb_ColorUnset(Color c)
{ return c.r == 0 && c.g == 0 && c.b == 0; }

static void FluidWaterOrb_Clear(FluidWaterOrb *orb)
{
    if (orb->emitter != PARTICLE_EMITTER_INVALID)
        ParticleManager_DestroyEmitter(orb->emitter);
    orb->emitter = PARTICLE_EMITTER_INVALID;
    orb->active = false;
}

static void FluidWaterOrb_SetFlightField(FluidWaterOrb *orb, Vector3 direction)
{
    ForceField_Clear(&orb->field);
    ForceField_AddLayer(&orb->field, (ForceLayer){.type=FORCE_GRAVITY_POINT,
        .origin=orb->center, .strength=52.0f, .radius=orb->radius*2.2f, .falloff=1.0f});
    ForceField_AddLayer(&orb->field, (ForceLayer){.type=FORCE_VORTEX,
        .origin=orb->center, .direction=direction, .strength=0.85f, .radius=orb->radius*1.8f});
    ForceField_AddLayer(&orb->field, (ForceLayer){.type=FORCE_NOISE_CURL,
        .strength=0.18f, .noiseScale=2.4f, .noiseSpeed=1.1f});
    ForceField_AddLayer(&orb->field, (ForceLayer){.type=FORCE_VISCOSITY, .strength=0.10f});
}

static void FluidWaterOrb_SetImpactField(FluidWaterOrb *orb)
{
    ForceField_Clear(&orb->field);
    float normalSpeed=Vector3DotProduct(orb->velocity,orb->hitNormal);
    Vector3 tangent=Vector3Subtract(orb->velocity,Vector3Scale(orb->hitNormal,normalSpeed));
    float rebound=fmaxf(0.0f,-normalSpeed)*0.48f+1.1f;
    orb->impactVelocity=Vector3Add(Vector3Scale(tangent,0.72f),
        Vector3Scale(orb->hitNormal,rebound));
    float speed=Vector3Length(orb->impactVelocity);
    /* The radial-axis field is a centrifugal impulse in the contact plane,
     * never a spherical blast. Its axis is the receiver normal. */
    ForceField_AddLayer(&orb->field, (ForceLayer){.type=FORCE_RADIAL_AXIS,
        .strength=-42.0f, .radius=orb->radius*2.8f, .falloff=1.0f});
    ForceField_AddLayer(&orb->field, (ForceLayer){.type=FORCE_GRAVITY_DIR,
        .direction=speed>0.0001f?Vector3Scale(orb->impactVelocity,1.0f/speed):orb->hitNormal,
        .strength=speed/0.18f});
    ForceField_AddLayer(&orb->field, (ForceLayer){.type=FORCE_VORTEX_AXIS,
        .strength=7.0f, .radius=orb->radius*2.4f});
    ForceField_AddLayer(&orb->field, (ForceLayer){.type=FORCE_NOISE_CURL,
        .strength=38.0f, .noiseScale=5.2f, .noiseSpeed=6.5f});
    ForceField_AddLayer(&orb->field, (ForceLayer){.type=FORCE_VISCOSITY, .strength=11.5f});
}

static void FluidWaterOrb_SetSettleField(FluidWaterOrb *orb)
{
    ForceField_Clear(&orb->field);
    ForceField_AddLayer(&orb->field, (ForceLayer){.type=FORCE_GRAVITY_DIR,
        .direction={0.0f,-1.0f,0.0f}, .strength=9.81f});
    ForceField_AddLayer(&orb->field, (ForceLayer){.type=FORCE_VISCOSITY, .strength=5.5f});
}

void FluidWaterOrb_Spawn(const FluidWaterOrbEvent *event)
{
    if (!event) return;
    FluidWaterOrb *orb=&s_orbs[s_nextOrb++ % FLUID_WATER_ORB_MAX];
    FluidWaterOrb_Clear(orb);
    const VFX_ElementMaterial *water=VFX_Material(VC_MAT_WATER);
    float travelTime=event->travelTime>0.0f?event->travelTime:0.65f;
    float radius=event->radius>0.0f?event->radius:0.42f;
    Vector3 flight=Vector3Subtract(event->target,event->start);
    float length=Vector3Length(flight);
    Vector3 hitNormal=Vector3LengthSqr(event->hitNormal)>0.0001f
        ?Vector3Normalize(event->hitNormal):(Vector3){0,1,0};
    Vector3 direction=length>0.0001f?Vector3Scale(flight,1.0f/length):(Vector3){0,0,1};
    *orb=(FluidWaterOrb){.start=event->start,.target=event->target,.center=event->start,
        .velocity=Vector3Scale(flight,1.0f/travelTime),.travelTime=travelTime,.radius=radius,
        .hitNormal=hitNormal,
        .force01=Clamp(event->force01,0.0f,1.0f),
        .body=FluidWaterOrb_ColorUnset(event->bodyColor)?water->body:event->bodyColor,
        .glow=FluidWaterOrb_ColorUnset(event->glowColor)?water->glow:event->glowColor,
        .soft=FluidWaterOrb_ColorUnset(event->softColor)?water->soft:event->softColor,
        .emitter=PARTICLE_EMITTER_INVALID,.phase=ORB_FLIGHT,.active=true};
    FluidWaterOrb_SetFlightField(orb,direction);
    ParticleEmitterDesc desc={0};
    desc.simulationPolicy=PARTICLE_SIM_AUTO;
    desc.renderMode=PARTICLE_RENDER_SURFACE_INPUT;
    desc.moduleFlags=PARTICLE_MODULE_FORCE_FIELD;
    desc.debugName="FluidWaterOrb SSF batch";
    desc.particle=(ParticleConfig){.forceField=&orb->field};
    orb->emitter=ParticleManager_CreateEmitter(&desc);
    if (orb->emitter==PARTICLE_EMITTER_INVALID) { orb->active=false; return; }
    for (int i=0;i<FLUID_WATER_ORB_PARTICLES;++i) {
        float u=((float)i+0.5f)/(float)FLUID_WATER_ORB_PARTICLES;
        float y=1.0f-2.0f*u;
        float ring=sqrtf(fmaxf(0.0f,1.0f-y*y));
        float angle=2.39996323f*(float)i;
        float shell=0.28f+0.52f*cbrtf((float)((i*37)%FLUID_WATER_ORB_PARTICLES)/(float)FLUID_WATER_ORB_PARTICLES);
        Vector3 offset={cosf(angle)*ring*shell*radius, y*shell*radius, sinf(angle)*ring*shell*radius};
        s_spawnParticles[i]=(ParticleConfig){.position=Vector3Add(event->start,offset),.velocity=orb->velocity,
            .colorStart=VC_WithAlpha(orb->soft,0),.colorEnd=VC_WithAlpha(orb->body,0),
            .radius=radius*0.09f,.lifetime=travelTime+0.92f,.forceField=&orb->field,
            .forceAxisOrigin=event->target,.forceAxisDir=hitNormal};
    }
    ParticleManager_EmitBatch(orb->emitter,s_spawnParticles,FLUID_WATER_ORB_PARTICLES);
    FluidSurface_SetMaterialColors(orb->body,orb->glow,orb->soft);
    FluidSurface_SetReconstructionRadius(radius*0.09f);
}

void FluidWaterOrb_Update(float dt)
{
    for (int i=0;i<FLUID_WATER_ORB_MAX;++i) {
        FluidWaterOrb *orb=&s_orbs[i]; if (!orb->active) continue;
        orb->age+=dt; orb->phaseAge+=dt;
        if (orb->phase==ORB_FLIGHT) {
            float t=Clamp(orb->age/orb->travelTime,0.0f,1.0f);
            orb->center=Vector3Lerp(orb->start,orb->target,t);
            orb->field.layers[0].origin=orb->center;
            orb->field.layers[1].origin=orb->center;
            if (t<1.0f) continue;
            FluidImpact_SpawnWater(&(FluidImpactEvent){.hitPoint=orb->target,.hitNormal=orb->hitNormal,
                .impulseDirection=Vector3Normalize(orb->velocity),.initialVelocity=orb->velocity,
                .force01=orb->force01,.scale=orb->radius*1.7f,.bodyColor=orb->body,
                .glowColor=orb->glow,.softColor=orb->soft,.forceFieldOnly=true,.externalBody=true});
            orb->phase=ORB_IMPACT; orb->phaseAge=0.0f; FluidWaterOrb_SetImpactField(orb);
        } else if (orb->phase==ORB_IMPACT && orb->phaseAge>=0.22f) {
            orb->phase=ORB_SETTLE; orb->phaseAge=0.0f; FluidWaterOrb_SetSettleField(orb);
        } else if (orb->phase==ORB_SETTLE && orb->phaseAge>=0.55f) {
            FluidWaterOrb_Clear(orb);
        }
    }
}

void FluidWaterOrb_Draw(void)
{
    for (int i=0;i<FLUID_WATER_ORB_MAX;++i) {
        FluidWaterOrb *orb=&s_orbs[i]; ParticleRenderStream stream;
        if (orb->active && ParticleManager_GetSurfaceStream(orb->emitter,&stream))
            FluidSurface_SubmitParticleStream(&stream);
    }
}

void FluidWaterOrb_GetStats(int *active,int *max)
{
    int count=0; for (int i=0;i<FLUID_WATER_ORB_MAX;++i) if(s_orbs[i].active) count++;
    if(active)*active=count; if(max)*max=FLUID_WATER_ORB_MAX;
}
