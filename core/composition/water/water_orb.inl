// water_orb.inl — Water orb: a coherent liquid projectile driven ENTIRELY by
// force fields. No PBD, no fluid-impact dependency.
//
// A sphere-shell burst is kept coherent by a gravity-point + vortex field while
// nudged by curl noise, its centre lerped along a caller-supplied start->target
// path. On arrival the force field swaps to an impact crown (radial-axis
// centrifugal impulse in the contact plane, plus a directional push, vortex and
// noise), then to a settle field (gravity + viscosity) that flattens survivors
// into a puddle. The pool self-releases once the settle phase expires.
//
// Rendered through the generic SSF bridge only: particles are emitted on a
// PARTICLE_RENDER_SURFACE_INPUT stream, surfaced via particle manager, and the
// surface stream is submitted per frame through `FluidSurface_SubmitParticleStream`.
// There is no fluid surface shading specific to this file — it reuses the same
// bridge as any other surface-input emitter.

#define WATER_ORB_MAX       2
#define WATER_ORB_PARTICLES 2000

typedef enum { WATER_ORB_FLIGHT, WATER_ORB_IMPACT, WATER_ORB_SETTLE } WaterOrbPhase;

typedef struct {
    Vector3 start, target, center, velocity, hitNormal, impactVelocity;
    float age, phaseAge, travelTime, radius;
    Color body, glow, soft;
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
        .origin=orb->center, .strength=52.0f, .radius=orb->radius*2.2f, .falloff=1.0f});
    ForceField_AddLayer(&orb->field, (ForceLayer){.type=FORCE_VORTEX,
        .origin=orb->center, .direction=direction, .strength=0.85f, .radius=orb->radius*1.8f});
    ForceField_AddLayer(&orb->field, (ForceLayer){.type=FORCE_NOISE_CURL,
        .strength=0.18f, .noiseScale=2.4f, .noiseSpeed=1.1f});
    ForceField_AddLayer(&orb->field, (ForceLayer){.type=FORCE_VISCOSITY, .strength=0.10f});
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

static void WaterOrb_SetSettleField(WaterOrb *orb)
{
    ForceField_Clear(&orb->field);
    ForceField_AddLayer(&orb->field, (ForceLayer){.type=FORCE_GRAVITY_DIR,
        .direction={0.0f,-1.0f,0.0f}, .strength=9.81f});
    ForceField_AddLayer(&orb->field, (ForceLayer){.type=FORCE_VISCOSITY, .strength=5.5f});
}

/* One-shot composition entry. Start -> target path, defaults tuned for the
 * arena (metre scale): 0.72 s flight, 0.44 m body, world-up receiver. */
void VFX_ComposeWaterOrb(Vector3 start, Vector3 target)
{
    const VFX_ElementMaterial *water=VFX_Material(VC_MAT_WATER);
    WaterOrb *orb=&s_waterOrbs[s_nextWaterOrb++ % WATER_ORB_MAX];
    WaterOrb_Clear(orb);
    Vector3 flight=Vector3Subtract(target,start);
    float length=Vector3Length(flight);
    Vector3 direction=length>0.0001f?Vector3Scale(flight,1.0f/length):(Vector3){0,0,1};
    *orb=(WaterOrb){.start=start,.target=target,.center=start,
        .velocity=Vector3Scale(flight,1.0f/0.72f),.travelTime=0.72f,
        .radius=0.44f,.hitNormal=(Vector3){0,1,0},
        .body=water->body,.glow=water->glow,.soft=water->soft,
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
    for (int i=0;i<WATER_ORB_PARTICLES;++i) {
        float u=((float)i+0.5f)/(float)WATER_ORB_PARTICLES;
        float y=1.0f-2.0f*u;
        float ring=sqrtf(fmaxf(0.0f,1.0f-y*y));
        float angle=2.39996323f*(float)i;
        float shell=0.28f+0.52f*cbrtf((float)((i*37)%WATER_ORB_PARTICLES)/(float)WATER_ORB_PARTICLES);
        Vector3 offset={cosf(angle)*ring*shell*0.44f, y*shell*0.44f, sinf(angle)*ring*shell*0.44f};
        s_waterOrbSpawn[i]=(ParticleConfig){.position=Vector3Add(start,offset),.velocity=orb->velocity,
            .colorStart=VC_WithAlpha(orb->soft,0),.colorEnd=VC_WithAlpha(orb->body,0),
            .radius=0.44f*0.09f,.lifetime=0.72f+0.92f,.forceField=&orb->field,
            .forceAxisOrigin=target,.forceAxisDir=orb->hitNormal};
    }
    ParticleManager_EmitBatch(orb->emitter,s_waterOrbSpawn,WATER_ORB_PARTICLES);
    FluidSurface_SetMaterialColors(orb->body,orb->glow,orb->soft);
    FluidSurface_SetReconstructionRadius(0.44f*0.09f);
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
        } else if (orb->phase==WATER_ORB_IMPACT && orb->phaseAge>=0.22f) {
            orb->phase=WATER_ORB_SETTLE; orb->phaseAge=0.0f; WaterOrb_SetSettleField(orb);
        } else if (orb->phase==WATER_ORB_SETTLE && orb->phaseAge>=0.55f) {
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
        if (orb->active && ParticleManager_GetSurfaceStream(orb->emitter,&stream))
            FluidSurface_SubmitParticleStream(&stream);
    }
}
