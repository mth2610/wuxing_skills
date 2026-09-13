#include "core/fluid/fluid_impact.h"

#include "core/particles/particle_manager.h"
#include "core/decals/decal_system.h"
#include "core/force_field.h"
#include "core/fluid/fluid_surface.h"
#include "core/fluid/fluid_pbd_gpu.h"
#include "core/gfx_quality.h"
#include "core/map_manager.h"
#include "core/presets/vc_material.h"
#include "core/resource_manager.h"
#include "raymath.h"
#include "rlgl.h"
#include <stddef.h>
#include <math.h>

#define FLUID_HERO_MAX_BOUNCES 2
#define FLUID_SECONDARY_MARKS_PER_FRAME 2
#define FLUID_WET_MARK_MAX 32
#define FLUID_FORCE_BODY_MAX_PARTICLES 768

typedef struct {
    Vector3 position, velocity;
    float radius, life;
    unsigned char bounces;
    ParticleEmitterHandle surfaceEmitter;
    FluidLiquidDesc material;
    bool active;
} FluidHeroDroplet;

typedef struct {
    Vector3 position, normal;
    float radius, life, maxLife;
    bool active;
} FluidWetMark;

typedef struct {
    ForceField field;
    ForceField coreField;
    ParticleEmitterHandle emitter;
    FluidLiquidDesc material;
    FluidMotionDesc motion;
    Vector3 point, normal;
    float age, scale, kernelRadius;
    bool settling, active;
} FluidForceBody;

static FluidHeroDroplet s_hero[FLUID_IMPACT_MAX_HERO_DROPLETS];
static int s_nextHero = 0;
static int s_secondaryMarksThisFrame = 0;
static FluidWetMark s_wetMarks[FLUID_WET_MARK_MAX];
static int s_nextWetMark = 0;
static FluidForceBody s_forceBodies[FLUID_IMPACT_MAX_BODIES];
static ParticleConfig s_forceBodySpawn[FLUID_FORCE_BODY_MAX_PARTICLES];
static FluidImpactCollisionQueryFn s_collisionQuery = NULL;
static void *s_collisionUserData = NULL;
static ForceField s_gravity;
static bool s_gravityReady = false;
static Color s_fluidBody;
static Color s_fluidGlow;
static Color s_fluidSoft;
static FluidLiquidDesc s_fluidMaterial;

static float FluidImpact_Rand01(void)
{
    return (float)GetRandomValue(0, 10000) / 10000.0f;
}

static bool FluidImpact_ColorIsUnset(Color color)
{
    return color.r == 0 && color.g == 0 && color.b == 0;
}

static void FluidImpact_Basis(Vector3 normal, Vector3 *outTangent, Vector3 *outBitangent)
{
    Vector3 reference = fabsf(normal.y) < 0.95f ? (Vector3){0.0f, 1.0f, 0.0f}
                                                  : (Vector3){1.0f, 0.0f, 0.0f};
    *outTangent = Vector3Normalize(Vector3CrossProduct(reference, normal));
    *outBitangent = Vector3Normalize(Vector3CrossProduct(normal, *outTangent));
}

static void FluidImpact_InitGravity(void)
{
    if (s_gravityReady) return;
    ForceField_Clear(&s_gravity);
    ForceField_AddLayer(&s_gravity, (ForceLayer){
        .type = FORCE_GRAVITY_DIR, .direction = {0.0f, -1.0f, 0.0f}, .strength = 9.81f
    });
    s_gravityReady = true;
}

static void FluidImpact_SetBodyImpactField(FluidForceBody *body)
{
    ForceField_Clear(&body->field);
    ForceField_Clear(&body->coreField);
    ForceField_AddLayer(&body->field, (ForceLayer){.type=FORCE_GRAVITY_DIR,
        .direction={0.0f,-1.0f,0.0f}, .strength=9.81f});
    ForceField_AddLayer(&body->field, (ForceLayer){.type=FORCE_RADIAL_AXIS,
        .strength=-body->motion.splashField, .radius=body->scale*0.85f, .falloff=2.0f});
    ForceField_AddLayer(&body->field, (ForceLayer){.type=FORCE_NOISE_CURL,
        .strength=body->motion.turbulence, .noiseScale=4.0f/body->scale, .noiseSpeed=2.0f});
    ForceField_AddLayer(&body->field, (ForceLayer){.type=FORCE_VISCOSITY,
        .strength=body->motion.impactViscosity});
    ForceField_AddLayer(&body->field, (ForceLayer){.type=FORCE_RECEIVER_PLANE,
        .origin=body->point, .direction=body->normal,
        .strength=body->motion.restitution,
        .falloff=body->motion.tangentRetention});
    /* The body core omits radial expansion, otherwise every particle is
       evacuated into the crown and the reconstructed surface becomes a torus. */
    ForceField_AddLayer(&body->coreField, (ForceLayer){.type=FORCE_GRAVITY_DIR,
        .direction={0.0f,-1.0f,0.0f}, .strength=9.81f});
    ForceField_AddLayer(&body->coreField, (ForceLayer){.type=FORCE_NOISE_CURL,
        .strength=body->motion.turbulence*0.55f,
        .noiseScale=4.0f/body->scale, .noiseSpeed=2.0f});
    ForceField_AddLayer(&body->coreField, (ForceLayer){.type=FORCE_VISCOSITY,
        .strength=body->motion.impactViscosity});
    ForceField_AddLayer(&body->coreField, (ForceLayer){.type=FORCE_RECEIVER_PLANE,
        .origin=body->point, .direction=body->normal,
        .strength=body->motion.restitution,
        .falloff=body->motion.tangentRetention});
}

static void FluidImpact_SetBodySettleField(FluidForceBody *body)
{
    ForceField_Clear(&body->field);
    ForceField_Clear(&body->coreField);
    ForceField_AddLayer(&body->field, (ForceLayer){.type=FORCE_GRAVITY_DIR,
        .direction={0.0f,-1.0f,0.0f}, .strength=9.81f});
    /* A weak attraction to the receiver-normal axis bounds the puddle without
     * paying for particle neighbours or pulling it away from the receiver. */
    ForceField_AddLayer(&body->field, (ForceLayer){.type=FORCE_RADIAL_AXIS,
        .strength=body->motion.gatherStrength, .radius=body->scale*1.15f, .falloff=2.0f});
    ForceField_AddLayer(&body->field, (ForceLayer){.type=FORCE_VISCOSITY,
        .strength=body->motion.settleViscosity});
    ForceField_AddLayer(&body->field, (ForceLayer){.type=FORCE_RECEIVER_PLANE,
        .origin=body->point, .direction=body->normal, .strength=0.0f,
        .falloff=body->motion.tangentRetention});
    /* The stationary core only needs damping and contact; the shell's gather
       layer closes over it without collapsing the entire puddle to one point. */
    ForceField_AddLayer(&body->coreField, (ForceLayer){.type=FORCE_GRAVITY_DIR,
        .direction={0.0f,-1.0f,0.0f}, .strength=9.81f});
    ForceField_AddLayer(&body->coreField, (ForceLayer){.type=FORCE_VISCOSITY,
        .strength=body->motion.settleViscosity});
    ForceField_AddLayer(&body->coreField, (ForceLayer){.type=FORCE_RECEIVER_PLANE,
        .origin=body->point, .direction=body->normal, .strength=0.0f,
        .falloff=body->motion.tangentRetention});
}

static bool FluidImpact_SpawnForceBody(const FluidImpactEvent *event,
                                       Vector3 normal, Vector3 outgoing,
                                       float force01, float scale)
{
    FluidForceBody *body = NULL;
    for (int i = 0; i < FLUID_IMPACT_MAX_BODIES; ++i) {
        if (!s_forceBodies[i].active) { body = &s_forceBodies[i]; break; }
    }
    if (!body) return false;

    *body = (FluidForceBody){.emitter=PARTICLE_EMITTER_INVALID,
        .material=s_fluidMaterial,
        .motion=FluidMotion_Get(event->motionProfile),
        .point=event->hitPoint, .normal=normal, .scale=scale,
        .kernelRadius=scale*0.028f, .active=true};
    FluidImpact_SetBodyImpactField(body);

    ParticleEmitterDesc desc={0};
    desc.simulationPolicy=PARTICLE_SIM_AUTO;
    desc.renderMode=PARTICLE_RENDER_SURFACE_INPUT;
    desc.moduleFlags=PARTICLE_MODULE_FORCE_FIELD;
    desc.debugName="FluidImpact force body";
    desc.particle=(ParticleConfig){.forceField=&body->field};
    body->emitter=ParticleManager_CreateEmitter(&desc);
    if (body->emitter==PARTICLE_EMITTER_INVALID) { body->active=false; return false; }

    const ParticleGPUCaps *caps=ParticleSystem_GetGPUCaps();
    int count=caps->computeShader ? (GfxQuality_Get()>=GFX_HIGH?768:512) :
                                  (GfxQuality_Get()<=GFX_LOW?192:320);
    Vector3 tangent,bitangent; FluidImpact_Basis(normal,&tangent,&bitangent);
    float outgoingNormal=Vector3DotProduct(outgoing,normal);
    Vector3 outgoingTangent=Vector3Subtract(outgoing,Vector3Scale(normal,outgoingNormal));
    for (int i=0;i<count;++i) {
        /* Keep a slow central population under the crown.  A pure expanding disk
           reconstructs as a clean torus and leaves no particles to form a puddle. */
        bool coreParticle=(i&3)==0;
        float angle=2.39996323f*(float)i+(FluidImpact_Rand01()-0.5f)*0.20f;
        float radial=coreParticle?sqrtf(FluidImpact_Rand01()):
                                  sqrtf(((float)i+0.5f)/(float)count);
        float height=FluidImpact_Rand01();
        Vector3 ring=Vector3Add(Vector3Scale(tangent,cosf(angle)),
                                Vector3Scale(bitangent,sinf(angle)));
        float radialOffset=coreParticle ? scale*(0.008f+0.20f*radial) :
                                          scale*(0.035f+0.15f*radial);
        Vector3 position=Vector3Add(event->hitPoint,
            Vector3Add(Vector3Scale(ring,radialOffset),
                       Vector3Scale(normal,body->kernelRadius+scale*0.12f*height)));
        float splash=scale*(0.65f+2.0f*force01)*body->motion.splashVelocity*
                     (0.45f+0.55f*FluidImpact_Rand01());
        if (coreParticle) splash*=0.18f;
        Vector3 velocity=Vector3Add(Vector3Scale(outgoingTangent,0.62f),
            Vector3Add(Vector3Scale(ring,splash),
                       Vector3Scale(normal,scale*(0.35f+1.25f*force01)*
                                    body->motion.normalLift*(coreParticle?0.25f+0.35f*height:
                                                                          0.4f+height))));
        s_forceBodySpawn[i]=(ParticleConfig){.position=position,.velocity=velocity,
            .colorStart=VC_WithAlpha(s_fluidSoft,0),.colorEnd=VC_WithAlpha(s_fluidBody,0),
            .radius=body->kernelRadius,.lifetime=body->motion.lifetime,
            .forceField=coreParticle?&body->coreField:&body->field,
            .forceAxisOrigin=event->hitPoint,.forceAxisDir=normal};
    }
    ParticleManager_EmitBatch(body->emitter,s_forceBodySpawn,count);
    return true;
}

static int FluidImpact_BackgroundCount(float force01)
{
    const ParticleGPUCaps *caps = ParticleSystem_GetGPUCaps();
    int base = caps->computeShader ? 12 : 4;
    int extra = caps->computeShader ? 20 : 8;
    if (GfxQuality_Get() <= GFX_LOW) { base = 3; extra = 5; }
    return base + (int)(extra * force01);
}

static void FluidImpact_EmitBackground(const ParticleConfig *particle)
{
    ParticleEmitterDesc desc = {0};
    desc.simulationPolicy = PARTICLE_SIM_AUTO;
    desc.renderMode = PARTICLE_RENDER_BILLBOARD; /* detached micro-spray */
    desc.particle = *particle;
    desc.moduleFlags = PARTICLE_MODULE_GRAVITY | PARTICLE_MODULE_DRAG |
                       PARTICLE_MODULE_COLOR_OVER_LIFE | PARTICLE_MODULE_SIZE_OVER_LIFE |
                       PARTICLE_MODULE_VELOCITY_STRETCH | PARTICLE_MODULE_FORCE_FIELD;
    desc.debugName = "FluidImpact background";
    ParticleEmitterHandle emitter = ParticleManager_CreateEmitter(&desc);
    if (emitter != PARTICLE_EMITTER_INVALID) {
        ParticleManager_Emit(emitter, 1);
        ParticleManager_DestroyEmitter(emitter);
    }
}

static int FluidImpact_HeroCount(float force01)
{
    int base = GfxQuality_Get() <= GFX_LOW ? 3 : 5;
    int extra = GfxQuality_Get() >= GFX_HIGH ? 9 : 5;
    return base + (int)(extra * force01);
}

static ParticleEmitterHandle FluidImpact_CreateSurfaceEmitter(Vector3 position, Vector3 velocity,
                                                               float radius, float lifetime)
{
    const VFX_ElementMaterial *water = VFX_Material(VC_MAT_WATER);
    ParticleEmitterDesc desc = {0};
    desc.simulationPolicy = PARTICLE_SIM_AUTO;
    desc.renderMode = PARTICLE_RENDER_SURFACE_INPUT;
    /* The separate hero pool owns gameplay collision/residue. The visual
     * surface is therefore free to use direct GPU raster on capable devices. */
    desc.moduleFlags = PARTICLE_MODULE_GRAVITY | PARTICLE_MODULE_DRAG;
    desc.debugName = "FluidImpact hero surface";
    /* Surface input is capture-only. Alpha must remain zero so a routing
     * failure cannot ever expose its underlying billboard quad in the main
     * particle pass; the SSF capture shader writes its own procedural disc. */
    desc.particle = (ParticleConfig){ .position=position, .velocity=velocity,
        .colorStart=VC_WithAlpha(water->soft, 0), .colorEnd=VC_WithAlpha(water->body, 0), .radius=radius,
        .lifetime=lifetime, .forceField=&s_gravity };
    ParticleEmitterHandle h = ParticleManager_CreateEmitter(&desc);
    if (h != PARTICLE_EMITTER_INVALID) ParticleManager_Emit(h, 1);
    return h;
}

static void FluidImpact_AddResidue(Vector3 point, Vector3 normal, float radius, float opacity)
{
    if (Vector3LengthSqr(normal) < 0.0001f) normal = (Vector3){0.0f, 1.0f, 0.0f};
    FluidWetMark *mark = &s_wetMarks[s_nextWetMark++ % FLUID_WET_MARK_MAX];
    *mark = (FluidWetMark){.position = Vector3Add(point, Vector3Scale(Vector3Normalize(normal), 0.008f)),
                            .normal = Vector3Normalize(normal), .radius = radius,
                            .life = 3.5f, .maxLife = 3.5f, .active = true};
    (void)opacity;
}

static bool FluidImpact_DefaultGroundHit(Vector3 from, Vector3 to, float radius,
                                         FluidImpactCollision *outHit)
{
    float groundY = MapManager_GetGroundHeightAt(to.x, to.z);
    if (from.y - radius >= groundY && to.y - radius <= groundY) {
        Vector3 pos, normal;
        MapManager_SampleGroundSurfaceAt(to.x, to.z, &pos, &normal);
        outHit->position = pos;
        outHit->normal = Vector3LengthSqr(normal) > 0.0001f ? Vector3Normalize(normal)
                                                            : (Vector3){0.0f, 1.0f, 0.0f};
        return true;
    }
    return false;
}

static bool FluidImpact_QueryCollision(Vector3 from, Vector3 to, float radius,
                                       FluidImpactCollision *outHit)
{
    if (s_collisionQuery)
        return s_collisionQuery(from, to, radius, outHit, s_collisionUserData);
    return FluidImpact_DefaultGroundHit(from, to, radius, outHit);
}

static void FluidImpact_SpawnMicroSplash(Vector3 point, Vector3 normal, float radius,
                                         Color body, Color soft)
{
    Vector3 tangent, bitangent;
    FluidImpact_Basis(normal, &tangent, &bitangent);
    for (int i = 0; i < 3; ++i) {
        float angle = ((float)GetRandomValue(0, 359)) * DEG2RAD;
        Vector3 radial = Vector3Add(Vector3Scale(tangent, cosf(angle)),
                                    Vector3Scale(bitangent, sinf(angle)));
        ParticleConfig p = {0};
        p.position = Vector3Add(point, Vector3Scale(normal, radius + 0.01f));
        p.velocity = Vector3Add(Vector3Scale(normal, 1.0f + 0.4f * i),
                                Vector3Scale(radial, 0.8f + 0.25f * i));
        p.colorStart = VC_WithAlpha(soft, 150);
        p.colorEnd = VC_WithAlpha(body, 0);
        p.radius = radius * 0.55f;
        p.lifetime = 0.22f;
        p.forceField = &s_gravity;
        p.stretchStrength = 0.12f;
        p.stretchMinSpeed = 0.20f;
        FluidImpact_EmitBackground(&p);
    }
}

void FluidImpact_SetCollisionQuery(FluidImpactCollisionQueryFn query, void *userData)
{
    s_collisionQuery = query;
    s_collisionUserData = userData;
}

void FluidImpact_SpawnWater(const FluidImpactEvent *event)
{
    if (!event) return;
    FluidImpact_InitGravity();
    Vector3 normal = event->hitNormal;
    if (Vector3LengthSqr(normal) < 0.0001f) normal = (Vector3){0.0f, 1.0f, 0.0f};
    normal = Vector3Normalize(normal);
    Vector3 impulse = event->impulseDirection;
    if (Vector3LengthSqr(impulse) < 0.0001f) impulse = normal;
    impulse = Vector3Normalize(impulse);
    float force01 = Clamp(event->force01, 0.0f, 1.0f);
    float scale = event->scale > 0.0f ? event->scale : 1.0f;
    FluidLiquidDesc profileMaterial=FluidSurface_ProfileDesc(event->motionProfile);
    s_fluidBody = FluidImpact_ColorIsUnset(event->bodyColor)
                ? profileMaterial.body : event->bodyColor;
    s_fluidGlow = FluidImpact_ColorIsUnset(event->glowColor)
                ? profileMaterial.glow : event->glowColor;
    s_fluidSoft = FluidImpact_ColorIsUnset(event->softColor)
                ? profileMaterial.soft : event->softColor;
    s_fluidMaterial=profileMaterial;
    s_fluidMaterial.body=s_fluidBody;
    s_fluidMaterial.glow=s_fluidGlow;
    s_fluidMaterial.soft=s_fluidSoft;
    /* A hero cast by default. The gate decides whether this impact is worth a
     * screen-space surface at all; when it says no, the ballistic droplets and
     * residue below still render as ordinary particles, which is exactly the
     * fallback the cost design asks for. */
    /* An impact is one-shot: it asks once, here, and the body it spawns lives
     * out its authored lifetime on that answer. Nothing re-asks or strobes. */
    bool useSurface = FluidSurface_RequestBody(FLUID_PRIORITY_CAST, event->hitPoint,
                                               scale * 0.30f, false);
    if (useSurface) {
        FluidSurface_BindMaterial(&s_fluidMaterial);
        FluidSurface_SetReconstructionRadiusFor(FLUID_PRIORITY_CAST, scale*0.022f);
    }
    Vector3 incoming = event->initialVelocity;
    if (Vector3LengthSqr(incoming) < 0.0001f)
        incoming = Vector3Scale(impulse, scale*(2.0f + force01*4.0f));
    /* Preserve the actual momentum magnitude, then apply an inelastic splash
     * rebound only when the water body was travelling into the receiver. */
    float intoSurface = Vector3DotProduct(incoming, normal);
    Vector3 outgoing = intoSurface < 0.0f ? Vector3Subtract(incoming, Vector3Scale(normal, intoSurface*1.35f)) : incoming;
    FluidImpact_AddResidue(event->hitPoint, normal,
                           scale * (0.30f + 0.65f * force01), force01);
    if (event->externalBody) return;

    /* PBD remains available for explicit experiments. It is lazy, single-body
     * today, and an occupied/unavailable solver falls back to the force path. */
    bool pbdRequested = !event->forceFieldOnly &&
                        event->backend == FLUID_IMPACT_BACKEND_PBD;
    bool gpuFluid = useSurface && pbdRequested &&
                    !FluidPBDGPU_IsActive() && FluidPBDGPU_Init();
    /* GPU PBD owns the receiver response.  Feeding it `outgoing` here made the
     * seed rebound before it touched the plane, which reads as a conical blast
     * instead of an incoming water body striking the receiver. */
    if (gpuFluid) FluidPBDGPU_SpawnImpact(event->hitPoint, normal, incoming, force01, scale);
    bool forceBody = useSurface && !gpuFluid &&
                     FluidImpact_SpawnForceBody(event,normal,outgoing,force01,scale);
    Vector3 tangent, bitangent;
    FluidImpact_Basis(normal, &tangent, &bitangent);

    /* If no coherent body was admitted, retain the bounded legacy droplets so
     * the impact remains visible without paying for SSF. */
    for (int i = 0, n = (gpuFluid || forceBody) ? 0 : FluidImpact_HeroCount(force01); i < n; ++i) {
        float angle = ((float)GetRandomValue(0, 359)) * DEG2RAD;
        float spread = 0.35f + ((float)GetRandomValue(0, 1000) / 1000.0f) * 0.65f;
        Vector3 radial = Vector3Add(Vector3Scale(tangent, cosf(angle)), Vector3Scale(bitangent, sinf(angle)));
        float speed = scale * (1.8f + 3.2f * force01) * spread;
        FluidHeroDroplet *d = &s_hero[s_nextHero++ % FLUID_IMPACT_MAX_HERO_DROPLETS];
        if (d->active && d->surfaceEmitter != PARTICLE_EMITTER_INVALID)
            ParticleManager_DestroyEmitter(d->surfaceEmitter);
        Vector3 start = Vector3Add(event->hitPoint, Vector3Scale(normal, 0.03f));
        Vector3 velocity = Vector3Add(Vector3Add(Vector3Scale(impulse, speed * 0.75f),
                                                  Vector3Scale(radial, speed * 0.65f)),
                                      Vector3Scale(normal, speed * 0.45f));
        float radius = scale * (0.025f + ((float)GetRandomValue(0, 1000) / 1000.0f) * 0.04f);
        float lifetime = 0.35f + ((float)GetRandomValue(0, 1000) / 1000.0f) * 0.45f;
        *d = (FluidHeroDroplet){
            .position = start, .velocity = velocity, .radius = radius, .life = lifetime,
            .surfaceEmitter = FluidImpact_CreateSurfaceEmitter(start, velocity, radius * 1.75f, lifetime),
            .material = s_fluidMaterial,
            .active = true
        };
    }

    for (int i = 0, n = (gpuFluid || forceBody) ? 0 : FluidImpact_BackgroundCount(force01); i < n; ++i) {
        float angle = ((float)GetRandomValue(0, 359)) * DEG2RAD;
        Vector3 radial = Vector3Add(Vector3Scale(tangent, cosf(angle)), Vector3Scale(bitangent, sinf(angle)));
        ParticleConfig p = {0};
        p.position = Vector3Add(event->hitPoint, Vector3Scale(normal, 0.025f));
        p.velocity = Vector3Add(Vector3Scale(impulse, scale * (1.2f + force01 * 2.0f)),
                                Vector3Add(Vector3Scale(radial, scale * (0.8f + force01)),
                                           Vector3Scale(normal, scale * 1.0f)));
        p.colorStart = VC_WithAlpha(s_fluidSoft, 170);
        p.colorEnd = VC_WithAlpha(s_fluidBody, 0);
        p.radius = scale * 0.022f; p.lifetime = 0.35f;
        p.forceField = &s_gravity; p.stretchStrength = 0.18f; p.stretchMinSpeed = 0.20f;
        FluidImpact_EmitBackground(&p);
    }
}

void FluidImpact_Update(float dt)
{
    FluidImpact_InitGravity();
    if (FluidPBDGPU_IsActive()) FluidPBDGPU_Update(dt, 0.0f);
    for (int i=0;i<FLUID_IMPACT_MAX_BODIES;++i) {
        FluidForceBody *body=&s_forceBodies[i];
        if (!body->active) continue;
        body->age+=dt;
        if (!body->settling && body->age>=body->motion.impactDuration) {
            body->settling=true;
            FluidImpact_SetBodySettleField(body);
        }
        if (body->age>=body->motion.lifetime) {
            if (body->emitter!=PARTICLE_EMITTER_INVALID)
                ParticleManager_DestroyEmitter(body->emitter);
            body->emitter=PARTICLE_EMITTER_INVALID;
            body->active=false;
        }
    }
    s_secondaryMarksThisFrame = 0;
    for (int i = 0; i < FLUID_IMPACT_MAX_HERO_DROPLETS; ++i) {
        FluidHeroDroplet *d = &s_hero[i];
        if (!d->active) continue;
        d->life -= dt;
        if (d->life <= 0.0f) { if (d->surfaceEmitter != PARTICLE_EMITTER_INVALID) ParticleManager_DestroyEmitter(d->surfaceEmitter); d->active = false; continue; }
        Vector3 from = d->position;
        d->velocity.y -= 9.81f * dt;
        d->velocity = Vector3Scale(d->velocity, fmaxf(0.0f, 1.0f - 1.4f * dt));
        Vector3 to = Vector3Add(from, Vector3Scale(d->velocity, dt));
        FluidImpactCollision hit;
        if (!FluidImpact_QueryCollision(from, to, d->radius, &hit)) { d->position = to; continue; }
        if (Vector3LengthSqr(hit.normal) < 0.0001f) hit.normal = (Vector3){0.0f, 1.0f, 0.0f};
        hit.normal = Vector3Normalize(hit.normal);
        d->position = Vector3Add(hit.position, Vector3Scale(hit.normal, d->radius + 0.004f));
        float normalSpeed = Vector3DotProduct(d->velocity, hit.normal);
        if (normalSpeed < 0.0f) d->velocity = Vector3Subtract(d->velocity,
            Vector3Scale(hit.normal, normalSpeed * 1.35f));
        d->velocity = Vector3Scale(d->velocity, 0.52f);
        d->bounces++;
        FluidImpact_SpawnMicroSplash(hit.position,hit.normal,d->radius,
                                     d->material.body,d->material.soft);
        if (s_secondaryMarksThisFrame < FLUID_SECONDARY_MARKS_PER_FRAME) {
            FluidImpact_AddResidue(hit.position, hit.normal, d->radius * 5.0f, 0.35f);
            s_secondaryMarksThisFrame++;
        }
        if (d->bounces >= FLUID_HERO_MAX_BOUNCES || Vector3Length(d->velocity) < 0.45f) {
            if (d->surfaceEmitter != PARTICLE_EMITTER_INVALID) ParticleManager_DestroyEmitter(d->surfaceEmitter);
            d->active = false;
        }
    }
}

void FluidImpact_Draw(void)
{
    for (int i=0;i<FLUID_IMPACT_MAX_BODIES;++i) {
        FluidForceBody *body=&s_forceBodies[i];
        ParticleRenderStream stream;
        if (!body->active || body->emitter==PARTICLE_EMITTER_INVALID) continue;
        FluidSurface_BindMaterial(&body->material);
        FluidSurface_SetReconstructionRadiusFor(FLUID_PRIORITY_CAST,body->kernelRadius);
        if (ParticleManager_GetSurfaceStream(body->emitter,&stream))
            FluidSurface_SubmitParticleStream(&stream);
    }
    for (int i = 0; i < FLUID_IMPACT_MAX_HERO_DROPLETS; ++i) {
        FluidHeroDroplet *d = &s_hero[i];
        if (!d->active || d->surfaceEmitter == PARTICLE_EMITTER_INVALID) continue;
        ParticleRenderStream stream;
        FluidSurface_BindMaterial(&d->material);
        FluidSurface_SetReconstructionRadiusFor(FLUID_PRIORITY_CAST,d->radius*1.75f);
        if (ParticleManager_GetSurfaceStream(d->surfaceEmitter, &stream)) FluidSurface_SubmitParticleStream(&stream);
    }
}

void FluidImpact_GetStats(int *active, int *max)
{
    int count = 0; for (int i = 0; i < FLUID_IMPACT_MAX_HERO_DROPLETS; ++i) if (s_hero[i].active) count++;
    for (int i=0;i<FLUID_IMPACT_MAX_BODIES;++i) if (s_forceBodies[i].active) count++;
    if (FluidPBDGPU_IsActive()) count++;
    if (active) *active = count;
    if (max) *max = FLUID_IMPACT_MAX_HERO_DROPLETS+FLUID_IMPACT_MAX_BODIES+1;
}
