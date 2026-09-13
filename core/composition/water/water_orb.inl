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
// surface stream is submitted per frame through `FluidSurface_SubmitParticleStream`.
// There is no fluid surface shading specific to this file — it reuses the same
// bridge as any other surface-input emitter.

#define WATER_ORB_MAX       2
#define WATER_ORB_PARTICLES 2048

typedef enum { WATER_ORB_FLIGHT, WATER_ORB_IMPACT, WATER_ORB_SETTLE } WaterOrbPhase;

typedef struct {
    Vector3 start, target, center, velocity, hitNormal, impactVelocity;
    float age, phaseAge, travelTime, radius;
    Color body, glow, soft;
    FluidLiquidDesc material;
    FluidMotionDesc motion;
    ForceField field;
    ParticleEmitterHandle emitter;
    WaterOrbPhase phase;
    bool active;
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
    if (orb->emitter != PARTICLE_EMITTER_INVALID)
        ParticleManager_DestroyEmitter(orb->emitter);
    orb->emitter = PARTICLE_EMITTER_INVALID;
    orb->active = false;
}

static void WaterOrb_SetFlightField(WaterOrb *orb, Vector3 direction)
{
    ForceField_Clear(&orb->field);
    ForceField_AddLayer(&orb->field, (ForceLayer){.type=FORCE_GRAVITY_POINT,
        .origin=orb->center, .strength=32.0f+orb->motion.gatherStrength*3.0f,
        .radius=orb->radius*2.2f, .falloff=1.0f});
    ForceField_AddLayer(&orb->field, (ForceLayer){.type=FORCE_VORTEX,
        .origin=orb->center, .direction=direction,
        .strength=0.45f+orb->motion.tangentRetention*0.55f,
        .radius=orb->radius*1.8f});
    ForceField_AddLayer(&orb->field, (ForceLayer){.type=FORCE_NOISE_CURL,
        .strength=orb->motion.turbulence*0.13f,
        .noiseScale=2.4f, .noiseSpeed=1.1f});
    ForceField_AddLayer(&orb->field, (ForceLayer){.type=FORCE_VISCOSITY,
        .strength=orb->motion.impactViscosity*0.03f});
}

static void WaterOrb_SetImpactField(WaterOrb *orb)
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
        .strength=-orb->motion.splashField,
        .radius=orb->radius*2.8f, .falloff=1.0f});
    ForceField_AddLayer(&orb->field, (ForceLayer){.type=FORCE_GRAVITY_DIR,
        .direction=speed>0.0001f?Vector3Scale(orb->impactVelocity,1.0f/speed):orb->hitNormal,
        .strength=speed/fmaxf(orb->motion.impactDuration,0.08f)});
    ForceField_AddLayer(&orb->field, (ForceLayer){.type=FORCE_VORTEX_AXIS,
        .strength=3.0f+orb->motion.tangentRetention*6.0f,
        .radius=orb->radius*2.4f});
    ForceField_AddLayer(&orb->field, (ForceLayer){.type=FORCE_NOISE_CURL,
        .strength=orb->motion.turbulence*8.0f,
        .noiseScale=5.2f, .noiseSpeed=6.5f});
    ForceField_AddLayer(&orb->field, (ForceLayer){.type=FORCE_VISCOSITY,
        .strength=orb->motion.impactViscosity*2.5f});
    ForceField_AddLayer(&orb->field, (ForceLayer){.type=FORCE_RECEIVER_PLANE,
        .origin=orb->target,.direction=orb->hitNormal,
        .strength=orb->motion.restitution,
        .falloff=orb->motion.tangentRetention});
}

static void WaterOrb_SetSettleField(WaterOrb *orb)
{
    ForceField_Clear(&orb->field);
    ForceField_AddLayer(&orb->field, (ForceLayer){.type=FORCE_GRAVITY_DIR,
        .direction={0.0f,-1.0f,0.0f}, .strength=9.81f});
    ForceField_AddLayer(&orb->field, (ForceLayer){.type=FORCE_RADIAL_AXIS,
        /* radius=0 keeps fast crown particles inside the gather contract;
           a finite cylinder strands anything that escaped during impact. */
        .strength=orb->motion.gatherStrength,.radius=0.0f,.falloff=0.0f});
    ForceField_AddLayer(&orb->field, (ForceLayer){.type=FORCE_VISCOSITY,
        .strength=orb->motion.settleViscosity});
    ForceField_AddLayer(&orb->field, (ForceLayer){.type=FORCE_RECEIVER_PLANE,
        .origin=orb->target,.direction=orb->hitNormal,.strength=0.0f,
        .falloff=orb->motion.tangentRetention});
}

/* One-shot composition entry. Start -> target path, defaults tuned for the
 * arena (metre scale): 0.72 s flight, 0.44 m body, world-up receiver. */
void VFX_FluidOrb_Spawn(Vector3 start, Vector3 target, FluidMotionProfile profile)
{
    FluidLiquidDesc material=FluidSurface_ProfileDesc(profile);
    FluidMotionDesc motion=FluidMotion_Get(profile);
    WaterOrb *orb=&s_waterOrbs[s_nextWaterOrb++ % WATER_ORB_MAX];
    WaterOrb_Clear(orb);
    Vector3 flight=Vector3Subtract(target,start);
    float length=Vector3Length(flight);
    Vector3 direction=length>0.0001f?Vector3Scale(flight,1.0f/length):(Vector3){0,0,1};
    *orb=(WaterOrb){.start=start,.target=target,.center=start,
        .velocity=Vector3Scale(flight,1.0f/0.72f),.travelTime=0.72f,
        .radius=0.44f,.hitNormal=(Vector3){0,1,0},
        .body=material.body,.glow=material.glow,.soft=material.soft,
        .material=material,.motion=motion,
        .emitter=PARTICLE_EMITTER_INVALID,.phase=WATER_ORB_FLIGHT,.active=true};
    WaterOrb_SetFlightField(orb,direction);
    ParticleEmitterDesc desc={0};
    desc.simulationPolicy=PARTICLE_SIM_AUTO;
    desc.renderMode=PARTICLE_RENDER_SURFACE_INPUT;
    desc.moduleFlags=PARTICLE_MODULE_FORCE_FIELD;
    desc.debugName="WaterOrb SSF batch";
    desc.particle=(ParticleConfig){.forceField=&orb->field};
    orb->emitter=ParticleManager_CreateEmitter(&desc);
    if (orb->emitter==PARTICLE_EMITTER_INVALID) { orb->active=false; return; }
    const ParticleGPUCaps *caps=ParticleSystem_GetGPUCaps();
    int count=caps->computeShader ? (GfxQuality_Get()>=GFX_HIGH?WATER_ORB_PARTICLES:
                                     GfxQuality_Get()<=GFX_LOW?768:1536) :
                                    (GfxQuality_Get()<=GFX_LOW?192:384);
    for (int i=0;i<count;++i) {
        float u=((float)i+0.5f)/(float)count;
        float y=1.0f-2.0f*u;
        float ring=sqrtf(fmaxf(0.0f,1.0f-y*y));
        float angle=2.39996323f*(float)i;
        float shell=0.28f+0.52f*cbrtf((float)((i*37)%count)/(float)count);
        Vector3 offset={cosf(angle)*ring*shell*0.44f, y*shell*0.44f, sinf(angle)*ring*shell*0.44f};
        s_waterOrbSpawn[i]=(ParticleConfig){.position=Vector3Add(start,offset),.velocity=orb->velocity,
            .colorStart=VC_WithAlpha(orb->soft,0),.colorEnd=VC_WithAlpha(orb->body,0),
            .radius=0.44f*0.09f,.lifetime=orb->travelTime+motion.lifetime,
            .forceField=&orb->field,
            .forceAxisOrigin=target,.forceAxisDir=orb->hitNormal};
    }
    ParticleManager_EmitBatch(orb->emitter,s_waterOrbSpawn,count);
    FluidSurface_BindMaterial(&orb->material);
    FluidSurface_SetReconstructionRadius(0.44f*0.09f);
}

void VFX_ComposeWaterOrb(Vector3 start, Vector3 target)
{
    /* Shipping/default behaviour stays water. The existing WATER ORB fixture
     * can exercise every force-field/material profile without adding five
     * near-identical sandbox fixtures: 0 water, 1 poison, 2 mud, 3 lava,
     * 4 liquid metal. */
    FluidMotionProfile profile=FLUID_MOTION_WATER;
    const char *profileOverride=getenv("WUXING_FLUID_ORB_PROFILE");
    if (profileOverride) {
        int value=atoi(profileOverride);
        if (value>=FLUID_MOTION_WATER && value<=FLUID_MOTION_LIQUID_METAL)
            profile=(FluidMotionProfile)value;
    }
    VFX_FluidOrb_Spawn(start,target,profile);
}

static void WaterOrb_Update(float dt)
{
    for (int i=0;i<WATER_ORB_MAX;++i) {
        WaterOrb *orb=&s_waterOrbs[i]; if (!orb->active) continue;
        orb->age+=dt; orb->phaseAge+=dt;
        if (orb->phase==WATER_ORB_FLIGHT) {
            float t=Clamp(orb->age/orb->travelTime,0.0f,1.0f);
            orb->center=Vector3Lerp(orb->start,orb->target,t);
            orb->field.layers[0].origin=orb->center;
            orb->field.layers[1].origin=orb->center;
            if (t<1.0f) continue;
            orb->phase=WATER_ORB_IMPACT; orb->phaseAge=0.0f; WaterOrb_SetImpactField(orb);
        } else if (orb->phase==WATER_ORB_IMPACT &&
                   orb->phaseAge>=orb->motion.impactDuration) {
            orb->phase=WATER_ORB_SETTLE; orb->phaseAge=0.0f; WaterOrb_SetSettleField(orb);
        } else if (orb->phase==WATER_ORB_SETTLE &&
                   orb->phaseAge>=orb->motion.lifetime-orb->motion.impactDuration) {
            WaterOrb_Clear(orb);
        }
    }
}

/* SSF submission step — runs in the screen-space composite phase, immediately
 * before FluidSurface_HasPending(). Registering here keeps a flight-only orb
 * visible before it has produced any decal or impact. */
static void WaterOrb_SubmitSurface(void)
{
    for (int i=0;i<WATER_ORB_MAX;++i) {
        WaterOrb *orb=&s_waterOrbs[i]; ParticleRenderStream stream;
        if (orb->active && ParticleManager_GetSurfaceStream(orb->emitter,&stream)) {
            FluidSurface_BindMaterial(&orb->material);
            /* Flight is a compact dense body; impact/settle can occupy the
             * whole crown. The SSF uses this only to skip a redundant second
             * HIGH reconstruction round when the projected footprint is small. */
            float surfaceRadius=orb->phase==WATER_ORB_FLIGHT?orb->radius:orb->radius*2.8f;
            FluidSurface_HintBody(orb->center,surfaceRadius);
            FluidSurface_SubmitParticleStream(&stream);
        }
    }
}
