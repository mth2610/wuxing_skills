#!/usr/bin/env python3
"""Execute the production vegetation receiver with moving borrowed Wind sources."""
import pathlib
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / "maps/toolkit/map_props_nature.inl"

def function(source, name):
    match = re.search(r"^(?:static|void)[^\n]*\b" + name + r"\(", source, re.M)
    assert match, name
    start = source.index("{", match.start())
    depth, end = 1, start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]

STUBS = r"""
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef struct {float x,y;} Vector2;
typedef struct {float x,y,z;} Vector3;
typedef struct {unsigned char r,g,b,a;} Color;
typedef struct {int id;} Texture2D;
typedef struct {int placeholder;} WindMacroConfig;
typedef struct {Vector3 forceNewtons,accelerationMps2,mediumVelocityMps;bool hasDragForce;} FieldSample;
typedef struct {float massKg,densityKgM3,volumeM3,projectedAreaM2,dragCoefficient,immersionFraction;} BodyPhysicalProperties;
typedef struct {float densityKgM3,dynamicViscosityPaS;Vector3 velocityMps,gravityMps2;} MediumProperties;
#define RECEIVER_ROOTED 1
typedef struct {int mode;Vector3 permittedAxes;} ReceiverConstraints;
typedef enum {VORTICLE_LINEAR_GUST,VORTICLE_RADIAL_BLAST,VORTICLE_VORTEX,VORTICLE_TURBULENCE} VorticleType;
typedef struct {Vector3 position,direction;float radius,strength;VorticleType type;float lifetime,maxLifetime,inwardPull;bool active;} VorticleData;
#define NATURE_INTERACTION_RESOLUTION 64
#define NATURE_INTERACTION_PIXEL_COUNT 4096
#define LOG_INFO 1
#define NATURE_MACRO_RESOLUTION 16
static const float kNatureMacroWorldSize=64;
static bool s_natureMacroReady;
static void TraceLog(int level,const char *format,...){(void)level;(void)format;}
static const float kNatureInteractionWorldSize=18.0f,kNatureInteractionMaxBend=.55f,kNatureWindBendPerMps=.035f,kNatureWindSampleHeight=.25f;
static Color s_natureWindPixels[4096],s_natureInteractionPixels[4096],s_natureInteractionScratch[4096],s_natureInteractionUploaded[4096];
static bool s_natureInteractionOpen=true,s_natureInteractionReady=true,s_natureInteractionUploadValid,s_natureWindReceiverReady;
static bool s_natureWindTraceVisibleUploaded,s_natureWindTraceShadowUploaded,s_natureWindImpactEnabled;
static Vector2 s_natureInteractionCenter,s_natureWindImpactCenter,s_natureWindImpactDirection;
static float s_natureWindImpactRadius,s_natureWindImpactStrength,s_natureWindImpactAge;
static Vector2 s_natureForceBend[4096],s_natureForceVelocity[4096],s_natureForceBendScratch[4096],s_natureForceVelocityScratch[4096];
static const float kNatureCanopyMassKg=.003f;
static float s_natureForceDt;static bool s_natureForceAwake;
#define MOTION_RECEIVER_FOLIAGE 2
static bool anchoredActive, anchoredFlowOnly, anchoredAccelerationOnly, anchoredDragOnly;static int anchoredQueries;
static bool MotionFields_GetAnchoredBounds(unsigned mask,Vector3 *min,Vector3 *max){assert(mask==MOTION_RECEIVER_FOLIAGE);*min=(Vector3){2.5f,0,.5f};*max=(Vector3){5.5f,2,3.5f};return anchoredActive;}
static void MotionFields_SampleBody(Vector3 p,Vector3 v,const BodyPhysicalProperties *body,const MediumProperties *medium,const ReceiverConstraints *constraints,float dt,unsigned mask,void *receiver,FieldSample *sample){
 (void)v;assert(body->massKg==.003f&&dt>0&&mask==MOTION_RECEIVER_FOLIAGE&&!receiver);
 assert(fabsf(body->projectedAreaM2-.003f)<1e-8f&&body->dragCoefficient==1.2f);
 assert(medium->densityKgM3==1.225f&&medium->velocityMps.x==0&&medium->velocityMps.y==0&&medium->velocityMps.z==0);
 assert(constraints->mode==RECEIVER_ROOTED&&constraints->permittedAxes.x==1&&constraints->permittedAxes.y==0&&constraints->permittedAxes.z==1);
 anchoredQueries++;*sample=anchoredFlowOnly?(FieldSample){.mediumVelocityMps={3,0,0}}:anchoredAccelerationOnly?(FieldSample){.accelerationMps2={p.x<4?.22f/.003f:-.22f/.003f,0,0}}:(FieldSample){.forceNewtons={p.x<4?.22f:-.22f,0,0}};
 if(anchoredDragOnly)*sample=(FieldSample){.mediumVelocityMps={1000,0,0},.hasDragForce=true};
}
static float s_natureWindTraceNextTime;
static int s_natureWindTraceDominantSlot=-1,s_natureWindImpactType,uploads;
static WindMacroConfig s_natureWindMacro;
static Texture2D s_natureInteractionTexture={1};
static VorticleData sources[3];static int sourceCount;
static bool Nature_WindTraceEnabled(void){return false;}

static void Nature_UpdateMacroCache(float time){(void)time;}
static double GetTime(void){return 0;}
static float terrainY=.3f;
static float MapManager_GetGroundHeightAt(float x,float z){(void)x;(void)z;return terrainY;}
static WindMacroConfig Wind_GetMacro(void){return (WindMacroConfig){0};}
static const VorticleData *Wind_GetActiveVorticles(int *count){*count=sourceCount;return sources;}
/* Wind math is separately covered by Core's parity tests. This fixture isolates
   receiver acceptance, spatial composition, moving centres and upload state. */
static Vector3 Wind_EvaluateVorticleVelocity(const VorticleData *s,Vector3 p,float time){
    (void)time;float dx=p.x-s->position.x,dz=p.z-s->position.z;
    float r=sqrtf(dx*dx+dz*dz);if(r>=s->radius)return(Vector3){0};
    float k=(1-r/s->radius)*s->strength;
    if(s->type==VORTICLE_VORTEX){float dy=p.y-s->position.y;return(Vector3){(dy*s->direction.z-dz*s->direction.y)*k,0,(dx*s->direction.y-dy*s->direction.x)*k};}
    return(Vector3){s->direction.x*k,0,s->direction.z*k};
}
static void UpdateTexture(Texture2D t,const void *p){assert(t.id&&p);uploads++;}
"""
MAIN = r"""
static void begin(void){s_natureInteractionOpen=true;for(int i=0;i<4096;i++)s_natureWindPixels[i]=Nature_EmptyInteractionPixel();}
int main(void){
 sources[0]=(VorticleData){.position={4,1,2},.direction={0,0,1},.radius=1.6f,.strength=3,.type=VORTICLE_LINEAR_GUST,.lifetime=1,.maxLifetime=1,.active=true};
 sourceCount=1;begin();MapProp_AddNatureWindVorticles(2);
 assert(s_natureWindReceiverReady&&s_natureWindImpactEnabled);
 assert(s_natureWindImpactAge==0&&s_natureWindImpactStrength>.17f&&s_natureWindImpactStrength<.31f);
 assert(s_natureWindImpactCenter.x==4&&s_natureWindImpactCenter.y==2);
 assert(s_natureWindImpactDirection.y>.99f);
 sources[0].strength=100;begin();MapProp_AddNatureWindVorticles(2);
 assert(s_natureWindImpactStrength==kNatureInteractionMaxBend);
 sources[0].strength=3;
 sources[1]=sources[0];sources[1].type=VORTICLE_VORTEX;sources[1].direction=(Vector3){0,1,0};sources[1].strength=1.6f;sourceCount=2;
 begin();MapProp_AddNatureWindVorticles(2);int left=0,right=0;
 for(int i=0;i<4096;i++){Vector2 bend=Nature_DecodeInteractionPixel(s_natureWindPixels[i]);left+=bend.x<-.001f;right+=bend.x>.001f;}
 assert(left>0&&right>0);MapProp_EndNatureInteraction();assert(uploads==1);
 sources[0].position.x=7;sources[1].position.x=7;
 begin();MapProp_AddNatureWindVorticles(3);assert(s_natureWindImpactCenter.x==7);MapProp_EndNatureInteraction();assert(uploads==2);
 sources[1].direction=(Vector3){0,0,1};
 begin();MapProp_AddNatureWindVorticles(3.5f);int horizontalSwirl=0;
 for(int i=0;i<4096;i++)horizontalSwirl+=Nature_DecodeInteractionPixel(s_natureWindPixels[i]).x<-.001f;
 assert(horizontalSwirl>0); /* Old source-height plane erased this component. */
 terrainY=NAN;begin();MapProp_AddNatureWindVorticles(3.6f);
 for(int i=0;i<4096;i++)assert(s_natureWindPixels[i].b==0);
 terrainY=.3f;
 begin();sourceCount=0;s_natureWindImpactEnabled=false;MapProp_AddNatureWindVorticles(4);MapProp_EndNatureInteraction();assert(!s_natureWindImpactEnabled&&uploads==3);
 s_natureInteractionCenter=(Vector2){4,2};s_natureForceDt=1.0f/60;anchoredActive=true;
 for(int i=0;i<30;i++){begin();Nature_AddAnchoredMotion();}
 int positiveCells=0,negativeCells=0;
 for(int i=0;i<4096;i++){
   positiveCells+=s_natureForceBend[i].x>.1f;
   negativeCells+=s_natureForceBend[i].x<-.1f;
 }
 assert(positiveCells>0&&negativeCells>0&&anchoredQueries>0&&anchoredQueries<30*4096);
 anchoredActive=false;int previousQueries=anchoredQueries;
 for(int i=0;i<180;i++){begin();Nature_AddAnchoredMotion();}
 assert(anchoredQueries==previousQueries&&!s_natureForceAwake);
 anchoredActive=true;anchoredAccelerationOnly=true;
 for(int i=0;i<30;i++){begin();Nature_AddAnchoredMotion();}
 int accelMovedCells=0;
 for(int i=0;i<4096;i++)accelMovedCells+=fabsf(s_natureForceBend[i].x)>.1f;
 assert(accelMovedCells>0);
 anchoredAccelerationOnly=false;anchoredFlowOnly=true;
 for(int i=0;i<30;i++){begin();Nature_AddAnchoredMotion();}
 int airMovedCells=0;
 for(int i=0;i<4096;i++)airMovedCells+=s_natureForceBend[i].x>.02f;
 assert(airMovedCells>0);
 memset(s_natureForceBend,0,sizeof(s_natureForceBend));
 memset(s_natureForceVelocity,0,sizeof(s_natureForceVelocity));
 anchoredDragOnly=true;begin();Nature_AddAnchoredMotion();
 for(int i=0;i<4096;i++)assert(s_natureForceBend[i].x==0);
 anchoredDragOnly=false;
 Vector3 hugeAir=Nature_AnchoredAirForce((Vector3){0},(Vector3){1000,0,0},1.0f/120);
 assert(isfinite(hugeAir.x)&&hugeAir.x>0&&hugeAir.x*(1.0f/120)/kNatureCanopyMassKg<=1000);
 assert(Nature_AnchoredAirForce((Vector3){1,0,0},(Vector3){0},1.0f/120).x==0);
 Vector2 positive={0},negative={0},positiveVel={0},negativeVel={0};
 for(int i=0;i<60;i++){
   Nature_AdvanceAnchoredCell(&positive,&positiveVel,(Vector3){.22f,0,0},1.0f/120);
   Nature_AdvanceAnchoredCell(&negative,&negativeVel,(Vector3){-.22f,0,0},1.0f/120);
 }
 assert(positive.x>.4f&&positive.x<=kNatureInteractionMaxBend+.00001f);
 assert(negative.x<-.4f&&negative.x>=-kNatureInteractionMaxBend-.00001f);
 bool reversed=false;
 for(int i=0;i<240;i++){
   Nature_AdvanceAnchoredCell(&positive,&positiveVel,(Vector3){0},1.0f/120);
   reversed|=positive.x<-.03f;
 }
 assert(reversed);
 int oldIndex=20*64+20,newIndex=20*64+19;
 s_natureForceBend[oldIndex]=(Vector2){.4f,.1f};
 s_natureForceVelocity[oldIndex]=(Vector2){-.2f,.3f};
 Vector2 center=s_natureInteractionCenter;center.x+=kNatureInteractionWorldSize/64;
 Nature_ScrollAndDecayInteraction(center,0);
 assert(s_natureForceBend[newIndex].x==.4f&&s_natureForceVelocity[newIndex].y==.3f);
 puts("PASS: moving continuous gust, spatial swirl, ground-relative horizontal-axis swirl, zero-age acceptance, texture upload/removal, typed Newton/acceleration strike, opposite recovery, target airflow, no duplicate drag and world-fixed scroll");
}
"""

def main():
    source=SOURCE.read_text()
    # These production diagnostics must accept both actual coordinate contracts.
    assert source.count("(worldFromShaderSpaceLoc >= 0 || worldOffsetLoc >= 0)") == 2
    # Use the real production local compliance rather than a mirrored budget.
    compliance=re.search(r"static const float kNatureWindBendPerMps = ([0-9.]+f);", source).group(1)
    stubs=STUBS.replace("kNatureWindBendPerMps=.035f", "kNatureWindBendPerMps="+compliance)
    names=("Nature_EmptyInteractionPixel","Nature_DecodeInteractionPixel","Nature_EncodeInteractionPixel","Nature_ScrollAndDecayInteraction","Nature_AdvanceAnchoredCell","Nature_AnchoredAirForce","Nature_AddAnchoredMotion","Nature_UpdateDominantWindImpact","MapProp_AddNatureWindVorticles","MapProp_EndNatureInteraction")
    with tempfile.TemporaryDirectory(prefix="wuxing-nature-wind-") as temp:
        path=pathlib.Path(temp)
        (path/"test.c").write_text(stubs+"\n"+"\n".join(function(source,n) for n in names)+MAIN)
        subprocess.run(["cc","-std=c99","-Wall","-Wextra",str(path/"test.c"),"-lm","-o",str(path/"test")],check=True)
        subprocess.run([str(path/"test")],check=True)
if __name__=="__main__":main()
