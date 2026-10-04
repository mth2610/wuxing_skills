#include "core/liquid/liquid_body_recipe.h"

// water_orb.inl — Water orb: a coherent liquid projectile driven ENTIRELY by
// force fields. No PBD, no fluid-impact dependency.
//
// A sphere-shell burst is kept coherent by a gravity-point + vortex field while
// nudged by curl noise, its centre lerped along a caller-supplied start->target
// path. On arrival the force field swaps to an impact crown (radial-axis
// centrifugal impulse in the contact plane, plus a directional push, vortex and
// noise + a receiver plane), then to a settle field (gravity + gather +
// viscosity + receiver) that flattens survivors into a puddle. The pool
// self-releases once the profile's settle phase expires.
//
// Rendered through the generic SSF bridge only: particles are emitted on a
// PARTICLE_RENDER_SURFACE_INPUT stream, surfaced via particle manager, and the
// surface stream is submitted per frame through `LiquidSurface_SubmitParticleStream`.
// There is no fluid surface shading specific to this file — it reuses the same
// bridge as any other surface-input emitter.

#define WATER_ORB_MAX       2
#define WATER_ORB_PARTICLES 2048

typedef enum { WATER_ORB_FLIGHT, WATER_ORB_IMPACT, WATER_ORB_SETTLE } WaterOrbPhase;

typedef struct {
    Vector3 start, target, center, velocity, hitNormal, impactVelocity;
    float age, phaseAge, travelTime, radius, crownDuration;
    Color body, glow, soft;
    LiquidDesc material;
    LiquidMotionDesc motion;
    LiquidBodyRecipe recipe;
    ForceField field;
    ParticleEmitterHandle emitter;
    WaterOrbPhase phase;
    bool surfaceAdmitted, active;
} WaterOrb;

static WaterOrb s_waterOrbs[WATER_ORB_MAX];
static int s_nextWaterOrb = 0;
/* Shared only while spawning: ParticleManager_EmitBatch consumes it
 * immediately, avoiding a multi-megabyte stack frame. */
static ParticleConfig s_waterOrbSpawn[WATER_ORB_PARTICLES];

static bool WaterOrb_ColorUnset(Color c)
{ return c.r == 0 && c.g == 0 && c.b == 0; }

static void WaterOrb_Clear(WaterOrb *orb)
{
    if (orb->active && orb->emitter != PARTICLE_EMITTER_INVALID)
        ParticleManager_DestroyEmitter(orb->emitter);
    orb->emitter = PARTICLE_EMITTER_INVALID;
    orb->active = false;
}

static void WaterOrb_SetPhaseField(WaterOrb *orb, LiquidBodyPhase phase,float stepSeconds)
{
    LiquidBodyContext context={.center=orb->center,.receiverPoint=orb->target,
        .receiverNormal=orb->hitNormal,.incomingVelocity=orb->velocity,
        .phaseAge=orb->phaseAge,.stepSeconds=stepSeconds,.applyContactImpulse=true};
    LiquidBodyRecipe_BuildField(&orb->recipe,phase,&context,false,&orb->field);
}

/* One-shot composition entry. Start -> target path, defaults tuned for the
 * arena (metre scale): 0.72 s flight, 0.44 m body, world-up receiver. */
void VFX_LiquidOrb_Spawn(Vector3 start, Vector3 target, LiquidMotionProfile profile)
{
    LiquidDesc material=LiquidSurface_ProfileDesc(profile);
    LiquidMotionDesc motion=LiquidMotion_Get(profile);
    WaterOrb *orb=NULL;
    for (int i=0;i<WATER_ORB_MAX;++i) {
        int slot=(s_nextWaterOrb+i)%WATER_ORB_MAX;
        if (!s_waterOrbs[slot].active &&
            !ParticleManager_IsForceFieldInUse(&s_waterOrbs[slot].field)) {
            orb=&s_waterOrbs[slot]; s_nextWaterOrb=(slot+1)%WATER_ORB_MAX; break;
        }
    }
    if (!orb) return; /* Retired GPU particles may still borrow their fields. */
    WaterOrb_Clear(orb);
    Vector3 flight=Vector3Subtract(target,start);
    *orb=(WaterOrb){.start=start,.target=target,.center=start,
        .velocity=Vector3Scale(flight,1.0f/0.72f),.travelTime=0.72f,
        .radius=0.44f,.hitNormal=(Vector3){0,1,0},
        .body=material.body,.glow=material.glow,.soft=material.soft,
        .material=material,.motion=motion,
        .emitter=PARTICLE_EMITTER_INVALID,.phase=WATER_ORB_FLIGHT,.active=true};
    orb->recipe=(LiquidBodyRecipe){.motion=motion,.radius=orb->radius,
        .kernelRadius=orb->radius*0.09f};
    orb->crownDuration=LiquidBodyRecipe_CrownDuration(&orb->recipe,orb->velocity,orb->hitNormal);
    WaterOrb_SetPhaseField(orb,LIQUID_BODY_FLIGHT,0.0f);
    bool forceSurface=getenv("WUXING_LIQUID_FORCE_SURFACE") &&
                      atoi(getenv("WUXING_LIQUID_FORCE_SURFACE"))!=0;
    /* The seed envelope fits inside this authored radius. Admission is decided
     * once per source; a running body never re-tests its own frame cost. */
    orb->surfaceAdmitted=forceSurface ||
        LiquidSurface_RequestBody(LIQUID_PRIORITY_CAST,start,orb->radius,false);
    ParticleEmitterDesc desc={0};
    bool cpuOnly=getenv("WUXING_LIQUID_CPU_ONLY") && atoi(getenv("WUXING_LIQUID_CPU_ONLY"))!=0;
    desc.simulationPolicy=cpuOnly?PARTICLE_SIM_CPU_ONLY:PARTICLE_SIM_AUTO;
    desc.renderMode=orb->surfaceAdmitted?PARTICLE_RENDER_SURFACE_INPUT:PARTICLE_RENDER_BILLBOARD;
    desc.moduleFlags=PARTICLE_MODULE_FORCE_FIELD;
    desc.debugName="WaterOrb SSF batch";
    desc.particle=(ParticleConfig){.forceField=&orb->field};
    orb->emitter=ParticleManager_CreateEmitter(&desc);
    if (orb->emitter==PARTICLE_EMITTER_INVALID) { orb->active=false; return; }
    const ParticleGPUCaps *caps=ParticleSystem_GetGPUCaps();
    int count=caps->computeShader && !cpuOnly ? (GfxQuality_Get()>=GFX_HIGH?WATER_ORB_PARTICLES:
                                     GfxQuality_Get()<=GFX_LOW?768:1536) :
                                    (GfxQuality_Get()<=GFX_LOW?192:384);
    for (int i=0;i<count;++i) {
        Vector3 offset=LiquidBodyRecipe_VolumeOffset(orb->radius*0.80f,i,count,0u);
        s_waterOrbSpawn[i]=(ParticleConfig){.position=Vector3Add(start,offset),.velocity=orb->velocity,
            .colorStart=VC_WithAlpha(orb->soft,orb->surfaceAdmitted?0:150),.colorEnd=VC_WithAlpha(orb->body,0),
            .radius=0.44f*0.09f,.lifetime=orb->travelTime+motion.lifetime,
            .forceField=&orb->field,
            .forceAxisOrigin=target,.forceAxisDir=orb->hitNormal};
    }
    ParticleManager_EmitBatch(orb->emitter,s_waterOrbSpawn,count);
    if (orb->surfaceAdmitted) {
        LiquidSurface_BindMaterial(&orb->material);
        LiquidSurface_SetReconstructionRadius(0.44f*0.09f);
    }
}

void VFX_ComposeWaterOrb(Vector3 start, Vector3 target)
{
    /* Shipping/default behaviour stays water. The existing WATER ORB fixture
     * can exercise every force-field/material profile without adding five
     * near-identical sandbox fixtures: 0 water, 1 poison, 2 mud, 3 lava,
     * 4 liquid metal. */
    LiquidMotionProfile profile=LIQUID_MOTION_WATER;
    const char *profileOverride=getenv("WUXING_LIQUID_ORB_PROFILE");
    if (!profileOverride) profileOverride=getenv("WUXING_FLUID_ORB_PROFILE");
    if (profileOverride) {
        int value=atoi(profileOverride);
        if (value>=LIQUID_MOTION_WATER && value<=LIQUID_MOTION_LIQUID_METAL)
            profile=(LiquidMotionProfile)value;
    }
    VFX_LiquidOrb_Spawn(start,target,profile);
}

static void WaterOrb_Update(float dt)
{
    for (int i=0;i<WATER_ORB_MAX;++i) {
        WaterOrb *orb=&s_waterOrbs[i]; if (!orb->active) continue;
        orb->age+=dt;
        if (orb->age<orb->travelTime) {
            float t=Clamp(orb->age/orb->travelTime,0.0f,1.0f);
            orb->center=Vector3Lerp(orb->start,orb->target,t);
            orb->field.layers[0].origin=orb->center;
            orb->field.layers[1].origin=orb->center;
            continue;
        }
        orb->center=orb->target;
        orb->phaseAge=fmaxf(orb->age-orb->travelTime,0.0f);
        if (orb->phaseAge>=orb->motion.lifetime) { WaterOrb_Clear(orb); continue; }
        orb->phase=orb->phaseAge<orb->crownDuration?WATER_ORB_IMPACT:WATER_ORB_SETTLE;
        /* Keep the last partial impact interval so clipped impulse is applied
         * even when the timestep crosses the body phase boundary. */
        LiquidBodyPhase fieldPhase=LiquidBodyRecipe_IntegrationPhaseAt(orb->phaseAge,dt,orb->crownDuration);
        WaterOrb_SetPhaseField(orb,fieldPhase,dt);
    }
}

/* SSF submission step — runs in the screen-space composite phase, immediately
 * before LiquidSurface_HasPending(). Registering here keeps a flight-only orb
 * visible before it has produced any decal or impact. */
static void WaterOrb_SubmitSurface(void)
{
    for (int i=0;i<WATER_ORB_MAX;++i) {
        WaterOrb *orb=&s_waterOrbs[i]; ParticleRenderStream stream;
        if (orb->active && orb->surfaceAdmitted && ParticleManager_GetSurfaceStream(orb->emitter,&stream)) {
            LiquidSurface_BindMaterial(&orb->material);
            /* Flight is a compact dense body; impact/settle can occupy the
             * whole crown. The SSF uses this only to skip a redundant second
             * HIGH reconstruction round when the projected footprint is small. */
            float surfaceRadius=orb->phase==WATER_ORB_FLIGHT?orb->radius:
                LiquidBodyRecipe_EstimateRadius(&orb->recipe,orb->velocity,orb->phaseAge);
            LiquidSurface_HintBody(orb->center,surfaceRadius);
            LiquidSurface_SubmitParticleStream(&stream);
        }
    }
}
