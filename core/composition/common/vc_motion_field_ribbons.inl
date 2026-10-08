#include "core/emitter/emitter_sinks.h"
#include "core/motion/motion_fields.h"

/* Integration fixture: one field, independent births and two attachment modes.
 * This owns choreography/resources only; all chain movement is in TrailSystem. */
#define VC_MOTION_RIBBON_CAPACITY 2
typedef struct {
    bool active, released;
    Vector3 origin, stone, velocity;
    float age;
    TrailAttachmentHandle attachment;
    MotionFieldHandle field;
    int freeRibbon, tiedRibbon;
    EmissionHandle emission;
    EmissionParticleSink sink;
    ParticleConfig particle;
} VC_MotionFieldRibbons;
static VC_MotionFieldRibbons s_motionFieldRibbons[VC_MOTION_RIBBON_CAPACITY];
static const MotionBodyProfile s_motionRibbonParticleBody={
    .inverseMassKg=250, .gravityScale=.08f, .linearDragPerSecond=.15f,
    .windCouplingHz=2, .windSusceptibility=1, .terminalSpeedMps=3
};
static const MotionBodyProfile s_motionRibbonStoneBody={
    .inverseMassKg=1, .gravityScale=.06f
};

static void VC_MotionFieldRibbons_Clear(VC_MotionFieldRibbons *s)
{
    Emission_Destroy(s->emission);
    ParticleManager_DestroyEmitter(s->sink.emitter);
    KillTrail(s->freeRibbon); KillTrail(s->tiedRibbon);
    MotionFields_Stop(s->field);
    TrailAttachment_Destroy(s->attachment);
    s->emission=EMISSION_HANDLE_INVALID;
    s->sink.emitter=PARTICLE_EMITTER_INVALID;
    s->freeRibbon=s->tiedRibbon=-1;
    s->field=MOTION_FIELD_INVALID; s->attachment=0;
}

static bool VC_MotionFieldRibbons_Start(VC_MotionFieldRibbons *s)
{
    s->age=0; s->released=false;
    s->stone=Vector3Add(s->origin,(Vector3){-.5f,1.0f,0});
    s->velocity=(Vector3){.5f,1.0f,0};
    s->attachment=TrailAttachment_Create(MatrixTranslate(s->stone.x,s->stone.y,s->stone.z));
    FieldDesc field=MotionField_Default();
    field.volume.radiusM=4;
    field.lifetime.durationSec=4.2f;
    field.transform.position=s->stone;
    field.flow.enabled=true;
    field.flow.velocityMps=(Vector3){.6f,.25f,.2f};
    field.forceLawCount=1;
    field.forceLaws[0].type=FORCE_LAW_ACCELERATION;
    field.forceLaws[0].accelerationMps2=(Vector3){0,.5f,0};
    s->field=MotionFields_CreateField(&field);

    TrailRibbonConfig ribbon=TrailRibbon_Default();
    ribbon.nodeCount=32; ribbon.lengthM=1.25f; ribbon.widthM=.12f;
    ribbon.lifetimeSec=4.2f;
    ribbon.material.body=s_motionRibbonParticleBody;
    ribbon.material.bendCompliance=.02f;
    ribbon.initialVelocity=s->velocity;
    ribbon.tailDirection=(Vector3){-1,-.35f,0};
    ribbon.mode=TRAIL_RIBBON_HEAD_ANCHORED; ribbon.attachment=s->attachment;
    ribbon.color=VC_WithAlpha(VFX_Material(VC_MAT_WATER)->body,235);
    s->tiedRibbon=TrailRibbon_Spawn(&ribbon);
    ribbon.mode=TRAIL_RIBBON_FREE; ribbon.attachment=0;
    ribbon.headPosition=Vector3Add(s->stone,(Vector3){.1f,.6f,.5f});
    ribbon.color=VC_WithAlpha(VFX_Material(VC_MAT_WOOD)->body,235);
    s->freeRibbon=TrailRibbon_Spawn(&ribbon);

    s->particle=(ParticleConfig){0};
    s->particle.radius=.035f; s->particle.lifetime=2;
    s->particle.colorStart=VFX_Material(VC_MAT_WATER)->body;
    s->particle.colorEnd=VC_WithAlpha(s->particle.colorStart,0);
    s->particle.physics.dynamics=&s_motionRibbonParticleBody;
    s->particle.render.appearance=VFX_APPEARANCE_NORMAL;
    ParticleEmitterDesc desc={0};
    desc.particle=s->particle; desc.debugName="Motion field ribbons";
    s->sink.emitter=ParticleManager_CreateEmitter(&desc);
    EmissionConfig emission={0};
    emission.kind=EMISSION_PARTICLE; emission.schedule=EMISSION_TIMED_COUNT;
    emission.count=80; emission.duration=3.5f;
    emission.sink=EmissionSink_Particle; emission.sinkUser=&s->sink;
    emission.spawnTemplate=&s->particle;
    s->emission=Emission_Create(&emission,s->stone);
    return s->attachment && s->field!=MOTION_FIELD_INVALID && s->tiedRibbon>=0 &&
        s->freeRibbon>=0 && s->sink.emitter!=PARTICLE_EMITTER_INVALID && s->emission;
}

int VFX_ComposeMotionFieldRibbons(Vector3 pos)
{
    for(int i=0;i<VC_MOTION_RIBBON_CAPACITY;i++) if(!s_motionFieldRibbons[i].active) {
        VC_MotionFieldRibbons *s=&s_motionFieldRibbons[i];
        memset(s,0,sizeof(*s)); s->origin=pos;
        s->freeRibbon=s->tiedRibbon=-1; s->sink.emitter=PARTICLE_EMITTER_INVALID;
        if(!VC_MotionFieldRibbons_Start(s)) {VC_MotionFieldRibbons_Clear(s);return -1;}
        s->active=true; return i;
    }
    return -1;
}

void VFX_KillMotionFieldRibbons(int handle)
{
    if(handle<0 || handle>=VC_MOTION_RIBBON_CAPACITY || !s_motionFieldRibbons[handle].active) return;
    VC_MotionFieldRibbons_Clear(&s_motionFieldRibbons[handle]);
    s_motionFieldRibbons[handle].active=false;
}

static void VC_MotionFieldRibbons_Update(float dt)
{
    if(dt<=0) return;
    for(int i=0;i<VC_MOTION_RIBBON_CAPACITY;i++) {
        VC_MotionFieldRibbons *s=&s_motionFieldRibbons[i];
        if(!s->active) continue;
        s->age+=dt;
        if(s->age>=4) {
            VC_MotionFieldRibbons_Clear(s);
            if(!VC_MotionFieldRibbons_Start(s)) {VC_MotionFieldRibbons_Clear(s);s->active=false;}
            continue;
        }
        s->velocity=MotionBody_AdvanceVelocity(s->velocity,&s_motionRibbonStoneBody,
            (Vector3){0},(Vector3){0},(Vector3){0},dt);
        s->stone=Vector3Add(s->stone,Vector3Scale(s->velocity,dt));
        TrailAttachment_Update(s->attachment,MatrixTranslate(s->stone.x,s->stone.y,s->stone.z),dt,false);
        FieldTransform transform=MotionField_Default().transform;
        transform.position=s->stone; transform.frameVelocityMps=s->velocity;
        MotionFields_SetTransform(s->field,&transform);
        Emission_Step(s->emission,s->stone,dt);
        if(!s->released && s->age>=2) {
            TrailRibbon_ReleaseHead(s->tiedRibbon);s->released=true;
        }
    }
}

static void VC_MotionFieldRibbons_Draw3D(Camera3D cam)
{
    (void)cam;
    VFXRenderScope scope=VFXRender_BeginDraw(VFX_RENDER_PASS_BODY,VFX_SURFACE_ALPHA,true);
    for(int i=0;i<VC_MOTION_RIBBON_CAPACITY;i++) if(s_motionFieldRibbons[i].active)
        DrawCoreSphere(s_motionFieldRibbons[i].stone,.1f,8,12,VFX_Material(VC_MAT_EARTH)->body);
    VFXRender_EndDraw(&scope);
}
