#include "core/liquid/liquid_body_recipe.h"
#include <stdio.h>
#include <string.h>

/* Narrow data-bookkeeping stubs: all phase/source/contact mathematics below
 * executes the production recipe. No particle integration is mirrored. */
void ForceField_Clear(ForceField *field) { *field=(ForceField){0}; }
bool ForceField_AddLayer(ForceField *field,ForceLayer layer)
{
    if (field->layerCount>=FORCE_FIELD_MAX_LAYERS) return false;
    field->layers[field->layerCount++]=layer;
    return true;
}

#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n",__LINE__,#x); bad++; } } while (0)
static int s_queryCount;
static bool QuerySlope(float x,float z,Vector3 *point,Vector3 *normal,void *data)
{
    float slope=*(float *)data;
    s_queryCount++;
    *point=(Vector3){x,x*slope,z};
    *normal=(Vector3){-slope,1,0};
    return true;
}

int main(void)
{
    int bad=0;
    LiquidBodyRecipe water={.motion=LiquidMotion_Get(LIQUID_MOTION_WATER),
        .radius=0.345f,.kernelRadius=0.0322f};
    LiquidBodyRecipe mud=water; mud.motion=LiquidMotion_Get(LIQUID_MOTION_MUD);
    Vector3 incoming={1.4f,-6.5f,0.8f},up={0,1,0};
    Vector3 outgoing=LiquidBody_ContactVelocity(incoming,up,0.1f);
    CHECK(outgoing.x==incoming.x && outgoing.z==incoming.z);
    CHECK(fabsf(outgoing.y-0.65f)<1e-5f);
    outgoing=LiquidBody_ContactVelocity((Vector3){-4,3,7},(Vector3){2,0,0},0.25f);
    CHECK(outgoing.x==1.0f && outgoing.y==3.0f && outgoing.z==7.0f);
    float crown=LiquidBodyRecipe_CrownDuration(&water,incoming,up);
    CHECK(crown>0.65f && crown<water.motion.lifetime);
    CHECK(crown>LiquidBodyRecipe_CrownDuration(&mud,incoming,up));
    CHECK(LiquidBodyRecipe_PhaseAt(0.20f,crown)==LIQUID_BODY_IMPACT);
    /* Former global 0.14-s transition had already applied resting viscosity. */
    CHECK(LiquidBodyRecipe_PhaseAt(0.20f,water.motion.impactDuration)==LIQUID_BODY_SETTLE);
    CHECK(LiquidBodyRecipe_PhaseAt(crown,crown)==LIQUID_BODY_SETTLE);
    CHECK(LiquidBodyRecipe_IntegrationPhaseAt(0.15f,1.0f/30.0f,0.14f)==LIQUID_BODY_IMPACT);
    CHECK(LiquidBodyRecipe_IntegrationPhaseAt(0.18f,1.0f/30.0f,0.14f)==LIQUID_BODY_SETTLE);
    LiquidBodyContext context={.receiverNormal=up,.incomingVelocity=incoming,.phaseAge=0.2f};
    ForceField field;
    LiquidBodyRecipe_BuildField(&water,LIQUID_BODY_IMPACT,&context,false,&field);
    CHECK(field.layerCount<=FORCE_FIELD_MAX_LAYERS);
    CHECK(field.layers[0].type==FORCE_GRAVITY_DIR && field.layers[0].direction.y==-1.0f);
    CHECK(field.layers[0].strength==9.81f);
    float initialExpansion=water.motion.splashField;
    CHECK(field.layers[1].strength<0 && -field.layers[1].strength<initialExpansion);
    LiquidBodyRecipe_BuildField(&water,LIQUID_BODY_SETTLE,&context,false,&field);
    CHECK(field.layers[field.layerCount-1].type==FORCE_RECEIVER_PLANE);
    CHECK(field.layers[field.layerCount-1].strength==0.0f);
    LiquidBodyRecipe_BuildField(&water,LIQUID_BODY_IMPACT,&context,true,&field);
    for (int i=0;i<field.layerCount;++i) CHECK(field.layers[i].type!=FORCE_RADIAL_AXIS);
    context.applyContactImpulse=true;
    context.phaseAge=0.10f; context.stepSeconds=1.0f/60.0f;
    LiquidBodyRecipe_BuildField(&water,LIQUID_BODY_IMPACT,&context,false,&field);
    CHECK(field.layers[0].direction.y>0.0f);
    context.phaseAge=0.20f;
    LiquidBodyRecipe_BuildField(&water,LIQUID_BODY_IMPACT,&context,false,&field);
    CHECK(field.layers[0].direction.y==-1.0f && field.layers[0].strength==9.81f);
    float impulse[3]={0},expansion[3]={0};
    const int rates[3]={30,60,120};
    for (int rate=0;rate<3;++rate) {
        float step=1.0f/(float)rates[rate];
        for (int tick=0;tick<rates[rate];++tick) {
            context.phaseAge=(float)(tick+1)*step;
            context.stepSeconds=step;
            /* Includes a timestep crossing the authored impulse and body-phase
             * boundary; the field keeps the final partial impact interval. */
            LiquidBodyPhase phase=LiquidBodyRecipe_IntegrationPhaseAt(context.phaseAge,step,
                                                                      water.motion.impactDuration);
            LiquidBodyRecipe_BuildField(&water,phase,&context,false,&field);
            impulse[rate]+=(field.layers[0].direction.y*field.layers[0].strength+9.81f)*step;
            expansion[rate]+=LiquidBodyRecipe_ExpansionWeight(context.phaseAge,step,
                                water.motion.impactDuration)*step;
        }
        float expected=LiquidBodyRecipe_LaunchNormal(&water,incoming,up)-incoming.y;
        CHECK(fabsf(impulse[rate]-expected)<1e-4f);
    }
    CHECK(fabsf(impulse[0]-impulse[1])<1e-4f && fabsf(impulse[1]-impulse[2])<1e-4f);
    CHECK(fabsf(expansion[0]-expansion[1])<1e-5f && fabsf(expansion[1]-expansion[2])<1e-5f);
    int cores=0;
    for (int i=0;i<64;++i) {
        Vector3 p=LiquidBodyRecipe_VolumeOffset(0.44f,i,64,17u);
        CHECK(LiquidBody_Dot(p,p)<=0.44f*0.44f+1e-6f);
        LiquidBodySeed seed=LiquidBodyRecipe_CrownSeed(&water,(Vector3){0},up,incoming,i,64,0u);
        LiquidBodySeed repeat=LiquidBodyRecipe_CrownSeed(&water,(Vector3){0},up,incoming,i,64,0u);
        CHECK(seed.position.x==repeat.position.x && seed.velocity.y==repeat.velocity.y);
        CHECK(seed.position.y>=water.kernelRadius && seed.velocity.y>0.0f);
        if (seed.core) cores++;
    }
    CHECK(cores==16);
    float slope=1.0f; Vector3 point,normal;
    s_queryCount=0;
    CHECK(LiquidBodyRecipe_SweepGround((Vector3){0,2,0},(Vector3){3,2,0},0.1f,
        QuerySlope,&slope,&point,&normal));
    CHECK(point.x>1.9f && point.x<2.0f && fabsf(point.y-point.x)<1e-5f);
    CHECK(s_queryCount<=15 && normal.x<0 && normal.y>0);
    slope=0;
    CHECK(!LiquidBodyRecipe_SweepGround((Vector3){0,2,0},(Vector3){3,2,0},0.1f,
        QuerySlope,&slope,&point,&normal));
    CHECK(LiquidBodyRecipe_SweepGround((Vector3){0,2,0},(Vector3){0,-8,0},0.1f,
        QuerySlope,&slope,&point,&normal));
    CHECK(point.y==0 && normal.y==1);
    printf("liquid body recipe production math: %s\n",bad?"FAIL":"PASS");
    return bad!=0;
}
