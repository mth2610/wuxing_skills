#ifndef CORE_LIQUID_BODY_RECIPE_H
#define CORE_LIQUID_BODY_RECIPE_H

#include "core/liquid/liquid_motion.h"
#include "core/force_field.h"
#include <math.h>

/* A recipe assembles existing particle/ForceField primitives. It owns no
 * particle state and performs no simulation or neighbour solve. The field
 * supplied by its owner must outlive every particle using it. */
typedef enum {
    LIQUID_BODY_FLIGHT,
    LIQUID_BODY_IMPACT,
    LIQUID_BODY_SETTLE
} LiquidBodyPhase;

typedef struct {
    LiquidMotionDesc motion;
    float radius;
    float kernelRadius;
} LiquidBodyRecipe;

typedef struct {
    Vector3 center;
    Vector3 receiverPoint;
    Vector3 receiverNormal;
    Vector3 incomingVelocity;
    float phaseAge;          // seconds since contact at the integration interval end
    float stepSeconds;       // current particle-integration interval; zero = sample only
    /* A continuing flight stream needs one bounded contact impulse. An
     * impact source already seeds its outgoing velocities and leaves this off. */
    bool applyContactImpulse;
} LiquidBodyContext;

typedef struct {
    Vector3 position;
    Vector3 velocity;
    bool core;
} LiquidBodySeed;

typedef bool (*LiquidBodyGroundQueryFn)(float x,float z,Vector3 *point,
                                       Vector3 *normal,void *userData);

/* Bounded terrain sweep: eight segment probes locate the first sampled
 * crossing, then six bisections refine it. This is not a mesh/prop sweep and
 * cannot guarantee discovery of features narrower than a probe interval. */
static inline bool LiquidBodyRecipe_SweepGround(Vector3 from,Vector3 to,float radius,
    LiquidBodyGroundQueryFn query,void *userData,Vector3 *outPoint,Vector3 *outNormal)
{
    if (!query || !outPoint || !outNormal) return false;
    float dx=to.x-from.x,dy=to.y-from.y,dz=to.z-from.z;
    int steps=(int)ceilf(sqrtf(dx*dx+dy*dy+dz*dz)/fmaxf(radius*4.0f,0.25f));
    if (steps<1) steps=1;
    if (steps>8) steps=8;
    float lastT=0.0f;
    for (int step=0;step<=steps;++step) {
        float t=(float)step/(float)steps;
        Vector3 sample={from.x+(to.x-from.x)*t,from.y+(to.y-from.y)*t,
                        from.z+(to.z-from.z)*t};
        Vector3 point,normal;
        if (!query(sample.x,sample.z,&point,&normal,userData)) { lastT=t; continue; }
        float length=sqrtf(normal.x*normal.x+normal.y*normal.y+normal.z*normal.z);
        if (length<1e-6f) normal=(Vector3){0,1,0};
        else { normal.x/=length; normal.y/=length; normal.z/=length; }
        float gap=(sample.x-point.x)*normal.x+(sample.y-point.y)*normal.y+
                  (sample.z-point.z)*normal.z-fmaxf(radius,0.0f);
        if (gap>0.0f) { lastT=t; continue; }
        float lo=lastT,hi=t;
        for (int refine=0;refine<6 && hi>lo;++refine) {
            float mid=(lo+hi)*0.5f;
            Vector3 p={from.x+(to.x-from.x)*mid,from.y+(to.y-from.y)*mid,
                       from.z+(to.z-from.z)*mid};
            Vector3 surface,n;
            if (!query(p.x,p.z,&surface,&n,userData)) { lo=mid; continue; }
            float nl=sqrtf(n.x*n.x+n.y*n.y+n.z*n.z);
            if (nl<1e-6f) n=(Vector3){0,1,0};
            else { n.x/=nl; n.y/=nl; n.z/=nl; }
            float distance=(p.x-surface.x)*n.x+(p.y-surface.y)*n.y+
                           (p.z-surface.z)*n.z-fmaxf(radius,0.0f);
            if (distance>0.0f) lo=mid;
            else { hi=mid; point=surface; normal=n; }
        }
        Vector3 center={from.x+(to.x-from.x)*hi,from.y+(to.y-from.y)*hi,
                        from.z+(to.z-from.z)*hi};
        float planeDistance=(center.x-point.x)*normal.x+
                            (center.y-point.y)*normal.y+(center.z-point.z)*normal.z;
        *outPoint=(Vector3){center.x-normal.x*planeDistance,
                           center.y-normal.y*planeDistance,
                           center.z-normal.z*planeDistance};
        *outNormal=normal;
        return true;
    }
    return false;
}

static inline float LiquidBody_Dot(Vector3 a, Vector3 b)
{ return a.x*b.x+a.y*b.y+a.z*b.z; }

static inline Vector3 LiquidBody_Normal(Vector3 n)
{
    float length=sqrtf(LiquidBody_Dot(n,n));
    if (length<1e-6f) return (Vector3){0,1,0};
    return (Vector3){n.x/length,n.y/length,n.z/length};
}

/* Normal-only inelastic response. Contact friction belongs to the receiver
 * layer; an incoming body's tangential momentum is not attenuated twice. */
static inline Vector3 LiquidBody_ContactVelocity(Vector3 v, Vector3 normal,
                                                float restitution)
{
    normal=LiquidBody_Normal(normal);
    float speed=LiquidBody_Dot(v,normal);
    if (speed>=0.0f) return v;
    float impulse=-speed*(1.0f+fminf(fmaxf(restitution,0.0f),1.0f));
    return (Vector3){v.x+normal.x*impulse,v.y+normal.y*impulse,v.z+normal.z*impulse};
}

/* Artistic redistribution of normal kinetic energy into a crown, with
 * profile-dependent lift/spread. Both coefficients are below one and their
 * squared sum is below one for the authored profiles. */
static inline float LiquidBodyRecipe_LaunchNormal(const LiquidBodyRecipe *recipe,
                                                  Vector3 incoming, Vector3 normal)
{
    normal=LiquidBody_Normal(normal);
    float speed=LiquidBody_Dot(incoming,normal);
    if (speed>=0.0f) return speed;
    return -speed*fmaxf(recipe->motion.restitution,0.55f*recipe->motion.normalLift);
}

/* This is a body phase envelope, not evidence that every GPU particle has
 * contacted its receiver. Per-particle resting shape is contact-owned. */
static inline float LiquidBodyRecipe_CrownDuration(const LiquidBodyRecipe *recipe,
                                                  Vector3 incoming, Vector3 normal)
{
    float flight=2.0f*LiquidBodyRecipe_LaunchNormal(recipe,incoming,normal)/9.81f;
    return fminf(fmaxf(recipe->motion.impactDuration,flight),
                 fmaxf(recipe->motion.impactDuration,recipe->motion.lifetime*0.75f));
}

static inline LiquidBodyPhase LiquidBodyRecipe_PhaseAt(float age, float crownDuration)
{ return age<crownDuration?LIQUID_BODY_IMPACT:LIQUID_BODY_SETTLE; }

/* The displayed phase may already be SETTLE while the last integration
 * interval still overlaps IMPACT. Keep that clipped interval's force. */
static inline LiquidBodyPhase LiquidBodyRecipe_IntegrationPhaseAt(float age,float step,
                                                                 float crownDuration)
{ return LiquidBodyRecipe_PhaseAt(age-fmaxf(step,0.0f),crownDuration); }

/* Exact overlap with the authored contact-impulse window. Averaging over the
 * current interval prevents a timestep crossing the cutoff from adding or
 * deleting a whole frame of impulse. */
static inline float LiquidBodyRecipe_ImpulseWeight(float age,float step,float duration)
{
    if (step<=0.0f) return age>=0.0f && age<duration?1.0f:0.0f;
    float overlap=fminf(fmaxf(age,0.0f),duration)-fmaxf(age-step,0.0f);
    return fmaxf(overlap,0.0f)/step;
}

static inline float LiquidBodyRecipe_ExpansionWeight(float age,float step,float duration)
{
    float tau=fmaxf(duration,0.08f);
    if (step<=0.0f) return expf(-fmaxf(age,0.0f)/tau);
    float start=fmaxf(age-step,0.0f),end=fmaxf(age,0.0f);
    return tau*(expf(-start/tau)-expf(-end/tau))/step;
}

/* Low-discrepancy volume sampling is deterministic and does not consume the
 * game's global RNG. seed rotates the source without changing its density. */
static inline Vector3 LiquidBodyRecipe_VolumeOffset(float radius, int index,
                                                   int count, unsigned int seed)
{
    if (count<=0) return (Vector3){0};
    float y=1.0f-2.0f*((float)index+0.5f)/(float)count;
    float ring=sqrtf(fmaxf(0.0f,1.0f-y*y));
    float angle=2.39996323f*(float)index+(float)(seed%1024u)*0.00613592315f;
    float volume=cbrtf(((float)((index*73+(int)(seed%31u))%count)+0.5f)/(float)count);
    return (Vector3){cosf(angle)*ring*radius*volume,y*radius*volume,
                     sinf(angle)*ring*radius*volume};
}

static inline LiquidBodySeed LiquidBodyRecipe_CrownSeed(const LiquidBodyRecipe *recipe,
    Vector3 point, Vector3 normal, Vector3 incoming, int index, int count,
    unsigned int seed)
{
    normal=LiquidBody_Normal(normal);
    Vector3 ref=fabsf(normal.y)<0.95f?(Vector3){0,1,0}:(Vector3){1,0,0};
    Vector3 tangent=LiquidBody_Normal((Vector3){ref.y*normal.z-ref.z*normal.y,
        ref.z*normal.x-ref.x*normal.z,ref.x*normal.y-ref.y*normal.x});
    Vector3 bitangent={normal.y*tangent.z-normal.z*tangent.y,
        normal.z*tangent.x-normal.x*tangent.z,normal.x*tangent.y-normal.y*tangent.x};
    float angle=2.39996323f*(float)index+(float)(seed%1024u)*0.00613592315f;
    float radial=sqrtf(((float)index+0.5f)/(float)(count>0?count:1));
    float height=((float)((index*37+(int)(seed%17u))%97)+0.5f)/97.0f;
    Vector3 ring={tangent.x*cosf(angle)+bitangent.x*sinf(angle),
        tangent.y*cosf(angle)+bitangent.y*sinf(angle),
        tangent.z*cosf(angle)+bitangent.z*sinf(angle)};
    bool core=(index&3)==0;
    float speed=LiquidBody_Dot(incoming,normal);
    Vector3 lateral={incoming.x-normal.x*speed,incoming.y-normal.y*speed,
                     incoming.z-normal.z*speed};
    float spread=fabsf(speed)*0.60f*recipe->motion.splashVelocity*(0.55f+0.45f*radial);
    float lift=LiquidBodyRecipe_LaunchNormal(recipe,incoming,normal)*(0.55f+0.45f*height);
    if (core) { spread*=0.18f; lift*=0.22f; }
    float offset=recipe->radius*(core?0.035f+0.58f*radial:0.10f+0.43f*radial);
    float above=recipe->kernelRadius+recipe->radius*0.36f*height;
    return (LiquidBodySeed){
        .position={point.x+ring.x*offset+normal.x*above,
                   point.y+ring.y*offset+normal.y*above,
                   point.z+ring.z*offset+normal.z*above},
        .velocity={lateral.x+ring.x*spread+normal.x*lift,
                   lateral.y+ring.y*spread+normal.y*lift,
                   lateral.z+ring.z*spread+normal.z*lift},.core=core};
}

static inline void LiquidBodyRecipe_BuildField(const LiquidBodyRecipe *recipe,
    LiquidBodyPhase phase, const LiquidBodyContext *context, bool core,
    ForceField *field)
{
    ForceField_Clear(field);
    const LiquidMotionDesc *motion=&recipe->motion;
    if (phase==LIQUID_BODY_FLIGHT) {
        ForceField_AddLayer(field,(ForceLayer){.type=FORCE_GRAVITY_POINT,
            .origin=context->center,.strength=32.0f+motion->gatherStrength*3.0f,
            .radius=recipe->radius*2.2f,.falloff=1.0f});
        ForceField_AddLayer(field,(ForceLayer){.type=FORCE_VORTEX,
            .origin=context->center,.direction=LiquidBody_Normal(context->incomingVelocity),
            .strength=0.45f+motion->tangentRetention*0.55f,.radius=recipe->radius*1.8f});
        ForceField_AddLayer(field,(ForceLayer){.type=FORCE_NOISE_CURL,
            .strength=motion->turbulence*0.13f,.noiseScale=1.05f/fmaxf(recipe->radius,0.01f),
            .noiseSpeed=1.1f});
        ForceField_AddLayer(field,(ForceLayer){.type=FORCE_VISCOSITY,
            .strength=motion->impactViscosity*0.03f});
        return;
    }
    Vector3 acceleration={0,-9.81f,0};
    if (phase==LIQUID_BODY_IMPACT && context->applyContactImpulse) {
        Vector3 normal=LiquidBody_Normal(context->receiverNormal);
        float speed=LiquidBody_Dot(context->incomingVelocity,normal);
        float launch=LiquidBodyRecipe_LaunchNormal(recipe,context->incomingVelocity,normal);
        float impulse=(launch-speed)/fmaxf(motion->impactDuration,0.08f)*
            LiquidBodyRecipe_ImpulseWeight(context->phaseAge,context->stepSeconds,motion->impactDuration);
        acceleration.x+=normal.x*impulse;
        acceleration.y+=normal.y*impulse;
        acceleration.z+=normal.z*impulse;
    }
    float gravity=sqrtf(LiquidBody_Dot(acceleration,acceleration));
    ForceField_AddLayer(field,(ForceLayer){.type=FORCE_GRAVITY_DIR,
        .direction=LiquidBody_Normal(acceleration),.strength=gravity});
    if (!core) {
        float expansion=LiquidBodyRecipe_ExpansionWeight(context->phaseAge,
            context->stepSeconds,motion->impactDuration);
        ForceField_AddLayer(field,(ForceLayer){.type=FORCE_RADIAL_AXIS,
            .strength=phase==LIQUID_BODY_IMPACT?-motion->splashField*expansion:motion->gatherStrength,
            .radius=phase==LIQUID_BODY_IMPACT?recipe->radius*3.1f:0.0f,
            .falloff=phase==LIQUID_BODY_IMPACT?2.0f:0.0f});
    }
    if (phase==LIQUID_BODY_IMPACT)
        ForceField_AddLayer(field,(ForceLayer){.type=FORCE_NOISE_CURL,
            .strength=motion->turbulence*(core?0.55f:1.0f),
            .noiseScale=1.2f/fmaxf(recipe->radius,0.01f),.noiseSpeed=2.0f});
    ForceField_AddLayer(field,(ForceLayer){.type=FORCE_VISCOSITY,
        .strength=phase==LIQUID_BODY_IMPACT?motion->impactViscosity:motion->settleViscosity});
    ForceField_AddLayer(field,(ForceLayer){.type=FORCE_RECEIVER_PLANE,
        .origin=context->receiverPoint,.direction=LiquidBody_Normal(context->receiverNormal),
        .strength=phase==LIQUID_BODY_IMPACT?motion->restitution:0.0f,
        .falloff=motion->tangentRetention});
}

/* A motion-derived screen-footprint hint, not a world-collision/culling bound.
 * It never proves contact or the distribution of unread GPU state. */
static inline float LiquidBodyRecipe_EstimateRadius(const LiquidBodyRecipe *recipe,
                                                   Vector3 incoming, float age)
{
    float t=fmaxf(age,0.0f), damping=fmaxf(recipe->motion.impactViscosity,0.01f);
    float travel=(1.0f-expf(-damping*t))/damping;
    float speed=sqrtf(LiquidBody_Dot(incoming,incoming));
    return recipe->radius+recipe->kernelRadius+
        speed*travel+recipe->motion.splashField*recipe->motion.impactDuration*travel;
}

#endif
