// liquid_bench.inl — five profile-driven SSF liquids in one force-field lab.
//
// Every body owns one persistent surface-input emitter and one stable
// ForceField. All GPU particles are advanced by the generic particle pool's
// single compute dispatch; five streams only separate their optical materials.

#define LIQUID_BENCH_BODIES 5
#define LIQUID_BENCH_GPU_HIGH_PER_BODY 128
#define LIQUID_BENCH_GPU_MED_PER_BODY 96
#define LIQUID_BENCH_GPU_LOW_PER_BODY 64
#define LIQUID_BENCH_CPU_PER_BODY 48
#define LIQUID_BENCH_CPU_TOTAL 240
#define LIQUID_BENCH_MAX_PER_BODY LIQUID_BENCH_GPU_HIGH_PER_BODY

#define LIQUID_BENCH_FLIGHT_SECONDS 0.72f
#define LIQUID_BENCH_IMPACT_SECONDS 0.34f
#define LIQUID_BENCH_SETTLE_SECONDS 1.84f
#define LIQUID_BENCH_CYCLE_SECONDS \
    (LIQUID_BENCH_FLIGHT_SECONDS+LIQUID_BENCH_IMPACT_SECONDS+LIQUID_BENCH_SETTLE_SECONDS)

typedef enum {
    LIQUID_BENCH_FLIGHT,
    LIQUID_BENCH_IMPACT,
    LIQUID_BENCH_SETTLE
} LiquidBenchPhase;

typedef struct {
    Vector3 start;
    Vector3 target;
    Vector3 center;
    FluidLiquidDesc material;
    FluidMotionDesc motion;
    ForceField field;
    ParticleEmitterHandle emitter;
    LiquidBenchPhase phase;
    bool active;
} LiquidBenchRuntime;

static LiquidBenchRuntime s_liquidBench[LIQUID_BENCH_BODIES];
static ParticleConfig s_liquidBenchSpawn[LIQUID_BENCH_MAX_PER_BODY];
static Vector3 s_liquidBenchOrigin;
static float s_liquidBenchSpacing=1.1f;
static float s_liquidBenchAge;
static float s_liquidBenchPlayback=1.0f;
static int s_liquidBenchMissedFrames=3;
static bool s_liquidBenchActive;

static void LiquidBench_Clear(void)
{
    for (int i=0;i<LIQUID_BENCH_BODIES;++i) {
        if (s_liquidBench[i].active &&
            s_liquidBench[i].emitter!=PARTICLE_EMITTER_INVALID)
            ParticleManager_DestroyEmitter(s_liquidBench[i].emitter);
        s_liquidBench[i].emitter=PARTICLE_EMITTER_INVALID;
        s_liquidBench[i].active=false;
    }
    s_liquidBenchActive=false;
}

static void LiquidBench_SetFlightField(LiquidBenchRuntime *body)
{
    ForceField_Clear(&body->field);
    ForceField_AddLayer(&body->field,(ForceLayer){.type=FORCE_GRAVITY_POINT,
        .origin=body->center,.strength=26.0f+body->motion.gatherStrength*2.4f,
        .radius=0.92f,.falloff=1.0f});
    ForceField_AddLayer(&body->field,(ForceLayer){.type=FORCE_VORTEX,
        .origin=body->center,.direction={0.0f,1.0f,0.0f},
        .strength=0.35f+body->motion.tangentRetention*0.55f,.radius=0.68f});
    ForceField_AddLayer(&body->field,(ForceLayer){.type=FORCE_NOISE_CURL,
        .strength=body->motion.turbulence*0.12f,
        .noiseScale=2.8f,.noiseSpeed=1.2f});
    ForceField_AddLayer(&body->field,(ForceLayer){.type=FORCE_VISCOSITY,
        .strength=body->motion.impactViscosity*0.025f});
}

static void LiquidBench_SetImpactField(LiquidBenchRuntime *body)
{
    ForceField_Clear(&body->field);
    ForceField_AddLayer(&body->field,(ForceLayer){.type=FORCE_RADIAL_AXIS,
        .strength=-body->motion.splashField,.radius=0.86f,.falloff=1.0f});
    ForceField_AddLayer(&body->field,(ForceLayer){.type=FORCE_GRAVITY_DIR,
        .direction={0.0f,1.0f,0.0f},.strength=body->motion.normalLift*4.8f});
    ForceField_AddLayer(&body->field,(ForceLayer){.type=FORCE_VORTEX_AXIS,
        .strength=2.0f+body->motion.tangentRetention*4.5f,.radius=0.78f});
    ForceField_AddLayer(&body->field,(ForceLayer){.type=FORCE_NOISE_CURL,
        .strength=body->motion.turbulence*5.5f,
        .noiseScale=5.0f,.noiseSpeed=6.0f});
    ForceField_AddLayer(&body->field,(ForceLayer){.type=FORCE_VISCOSITY,
        .strength=body->motion.impactViscosity*2.5f});
    ForceField_AddLayer(&body->field,(ForceLayer){.type=FORCE_RECEIVER_PLANE,
        .origin=body->target,.direction={0.0f,1.0f,0.0f},
        .strength=body->motion.restitution,.falloff=body->motion.tangentRetention});
}

static void LiquidBench_SetSettleField(LiquidBenchRuntime *body)
{
    ForceField_Clear(&body->field);
    ForceField_AddLayer(&body->field,(ForceLayer){.type=FORCE_GRAVITY_DIR,
        .direction={0.0f,-1.0f,0.0f},.strength=9.81f});
    ForceField_AddLayer(&body->field,(ForceLayer){.type=FORCE_RADIAL_AXIS,
        .strength=body->motion.gatherStrength,.radius=0.0f,.falloff=0.0f});
    ForceField_AddLayer(&body->field,(ForceLayer){.type=FORCE_NOISE_CURL,
        .strength=body->motion.turbulence*0.22f,
        .noiseScale=3.2f,.noiseSpeed=0.8f});
    ForceField_AddLayer(&body->field,(ForceLayer){.type=FORCE_VISCOSITY,
        .strength=body->motion.settleViscosity});
    ForceField_AddLayer(&body->field,(ForceLayer){.type=FORCE_RECEIVER_PLANE,
        .origin=body->target,.direction={0.0f,1.0f,0.0f},.strength=0.0f,
        .falloff=body->motion.tangentRetention});
}

static int LiquidBench_ParticleCount(bool compute)
{
    if (!compute) return LIQUID_BENCH_CPU_PER_BODY;
    if (GfxQuality_Get()>=GFX_HIGH) return LIQUID_BENCH_GPU_HIGH_PER_BODY;
    if (GfxQuality_Get()>=GFX_MED) return LIQUID_BENCH_GPU_MED_PER_BODY;
    return LIQUID_BENCH_GPU_LOW_PER_BODY;
}

static void LiquidBench_SpawnBody(int index,FluidMotionProfile profile,
                                  Vector3 target,bool compute)
{
    LiquidBenchRuntime *body=&s_liquidBench[index];
    FluidMotionDesc motion=FluidMotion_Get(profile);
    FluidLiquidDesc material=FluidSurface_ProfileDesc(profile);
    Vector3 start=Vector3Add(target,(Vector3){0.0f,1.22f,0.0f});
    *body=(LiquidBenchRuntime){.start=start,.target=target,.center=start,
        .material=material,.motion=motion,.emitter=PARTICLE_EMITTER_INVALID,
        .phase=LIQUID_BENCH_FLIGHT,.active=true};
    LiquidBench_SetFlightField(body);

    ParticleEmitterDesc desc={0};
    desc.simulationPolicy=PARTICLE_SIM_AUTO;
    desc.renderMode=PARTICLE_RENDER_SURFACE_INPUT;
    desc.moduleFlags=PARTICLE_MODULE_FORCE_FIELD;
    desc.debugName="LiquidBench force-field SSF";
    desc.particle=(ParticleConfig){.forceField=&body->field};
    body->emitter=ParticleManager_CreateEmitter(&desc);
    if (body->emitter==PARTICLE_EMITTER_INVALID) { body->active=false; return; }

    int count=LiquidBench_ParticleCount(compute);
    float kernel=compute?(count>=LIQUID_BENCH_GPU_HIGH_PER_BODY?0.086f:
                         count>=LIQUID_BENCH_GPU_MED_PER_BODY?0.094f:0.106f):0.112f;
    Vector3 velocity=Vector3Scale(Vector3Subtract(target,start),
                                  1.0f/LIQUID_BENCH_FLIGHT_SECONDS);
    for (int i=0;i<count;++i) {
        float u=((float)i+0.5f)/(float)count;
        float y=1.0f-2.0f*u;
        float ring=sqrtf(fmaxf(0.0f,1.0f-y*y));
        float angle=2.39996323f*(float)i;
        float volume=cbrtf(((float)((i*73+index*31)%count)+0.5f)/(float)count);
        float radius=0.30f*volume;
        Vector3 offset={cosf(angle)*ring*radius,y*radius*0.88f,
                        sinf(angle)*ring*radius};
        s_liquidBenchSpawn[i]=(ParticleConfig){
            .position=Vector3Add(start,offset),.velocity=velocity,
            .colorStart=VC_WithAlpha(material.soft,0),
            .colorEnd=VC_WithAlpha(material.body,0),
            .radius=kernel,.lifetime=LIQUID_BENCH_CYCLE_SECONDS+0.12f,
            .forceField=&body->field,.forceAxisOrigin=target,
            .forceAxisDir={0.0f,1.0f,0.0f}};
    }
    ParticleManager_EmitBatch(body->emitter,s_liquidBenchSpawn,count);
}

static void LiquidBench_SpawnAll(void)
{
    static const FluidMotionProfile profiles[LIQUID_BENCH_BODIES]={
        FLUID_MOTION_WATER,FLUID_MOTION_POISON,FLUID_MOTION_MUD,
        FLUID_MOTION_LAVA,FLUID_MOTION_LIQUID_METAL};
    const ParticleGPUCaps *caps=ParticleSystem_GetGPUCaps();
    bool compute=caps->computeShader;
    for (int i=0;i<LIQUID_BENCH_BODIES;++i) {
        Vector3 target={s_liquidBenchOrigin.x+(float)(i-2)*s_liquidBenchSpacing,
                        s_liquidBenchOrigin.y+0.075f,s_liquidBenchOrigin.z};
        LiquidBench_SpawnBody(i,profiles[i],target,compute);
    }
    s_liquidBenchAge=0.0f;
    s_liquidBenchActive=true;
}

static void LiquidBench_Update(float dt)
{
    if (!s_liquidBenchActive) return;
    if (++s_liquidBenchMissedFrames>2) { LiquidBench_Clear(); return; }
    s_liquidBenchAge+=dt*s_liquidBenchPlayback;
    if (s_liquidBenchAge>=LIQUID_BENCH_CYCLE_SECONDS) {
        LiquidBench_Clear();
        LiquidBench_SpawnAll();
        return;
    }
    for (int i=0;i<LIQUID_BENCH_BODIES;++i) {
        LiquidBenchRuntime *body=&s_liquidBench[i];
        if (!body->active) continue;
        if (s_liquidBenchAge<LIQUID_BENCH_FLIGHT_SECONDS) {
            float t=Clamp(s_liquidBenchAge/LIQUID_BENCH_FLIGHT_SECONDS,0.0f,1.0f);
            t=t*t*(3.0f-2.0f*t);
            body->center=Vector3Lerp(body->start,body->target,t);
            body->field.layers[0].origin=body->center;
            body->field.layers[1].origin=body->center;
        } else if (s_liquidBenchAge<LIQUID_BENCH_FLIGHT_SECONDS+
                                           LIQUID_BENCH_IMPACT_SECONDS) {
            if (body->phase!=LIQUID_BENCH_IMPACT) {
                body->phase=LIQUID_BENCH_IMPACT;
                body->center=body->target;
                LiquidBench_SetImpactField(body);
            }
        } else if (body->phase!=LIQUID_BENCH_SETTLE) {
            body->phase=LIQUID_BENCH_SETTLE;
            LiquidBench_SetSettleField(body);
        }
    }
}

static void LiquidBench_SubmitSurface(void)
{
    for (int i=0;i<LIQUID_BENCH_BODIES;++i) {
        LiquidBenchRuntime *body=&s_liquidBench[i];
        ParticleRenderStream stream;
        if (!body->active ||
            !ParticleManager_GetSurfaceStream(body->emitter,&stream)) continue;
        FluidSurface_BindMaterial(&body->material);
        float extent=body->phase==LIQUID_BENCH_FLIGHT?0.38f:
                     body->phase==LIQUID_BENCH_IMPACT?0.94f:0.72f;
        FluidSurface_HintBody(body->center,extent);
        FluidSurface_SubmitParticleStream(&stream);
    }
}

/* Continuous fixture entry. It only maintains a heartbeat; simulation and SSF
 * submission live in the engine's normal update/composite phases. */
void VFX_ComposeLiquidBench(Vector3 center,float spacing,float t01)
{
    if (spacing<=0.0f) spacing=1.1f;
    t01=Clamp(t01,0.0f,1.0f);
    bool moved=!s_liquidBenchActive ||
        Vector3Distance(center,s_liquidBenchOrigin)>0.01f ||
        fabsf(spacing-s_liquidBenchSpacing)>0.01f;
    s_liquidBenchOrigin=center;
    s_liquidBenchSpacing=spacing;
    s_liquidBenchPlayback=0.65f+0.35f*t01;
    s_liquidBenchMissedFrames=0;
    if (moved) {
        LiquidBench_Clear();
        LiquidBench_SpawnAll();
    }
    /* CPU fallback: five times 48 = 240, below FluidSurface's 384-particle
     * copy ceiling. GPU High: 640 particles, still one compute dispatch. */
    FluidSurface_SetReconstructionRadius(0.112f);
}
