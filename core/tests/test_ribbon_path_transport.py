#!/usr/bin/env python3
"""Exercise the production ribbon update with CPU transport and GPU staging."""
import pathlib,re,subprocess,tempfile
ROOT=pathlib.Path(__file__).resolve().parents[2]
s=(ROOT/'core/trails/trail_ribbon.c').read_text()
m=re.search(r'^bool TrailRibbonSystem_Update\(',s,re.M)
a=s.index('{',m.start());b=a+1;depth=1
while depth:
    depth+=(s[b]=='{')-(s[b]=='}');b+=1
code=s[m.start():b]
preamble=r'''
#include "raylib.h"
typedef struct {int placeholder;} Material;
typedef int BlendMode;
typedef struct {int placeholder;} Mesh;
typedef struct {int meshCount;} Model;
typedef struct {Vector3 position,target,up;float fovy;int projection;} Camera3D;
enum {BLEND_ALPHA,BLEND_ADDITIVE,BLEND_ALPHA_PREMULTIPLY};
#include "core/trails/trail_ribbon.h"
#include <assert.h>
#include <stdio.h>
typedef struct {TrailRibbonConfig config;TrailRibbonState state;float pathDistanceM;int gpuSlot;} ModernRibbon;
static ModernRibbon ribbon;
static MotionPath path;
static bool live=true;
static int copies,staged,forces;
static float uploaded;
static ModernRibbon *FindRibbon(int id){return id==1?&ribbon:NULL;}
bool MotionFields_GetPathTransport(MotionFieldHandle h,MotionPathTransportSnapshot *v){
 if(!live||h!=123)return false;*v=(MotionPathTransportSnapshot){.path=&path,.transform=FieldTransform_Identity()};return true;
}
static void TrailRibbonGpu_SetPathTransport(int slot,const MotionPathTransport *t,float d){assert(slot==4&&t->field==123);staged++;uploaded=d;}
TrailEntity *GetTrail(int id){(void)id;return NULL;}
static void CopyRenderHistory(ModernRibbon *r,TrailEntity *t){(void)r;(void)t;copies++;}
static bool SampleAnchor(const ModernRibbon *r,TrailRibbonAnchor *a){(void)r;(void)a;forces++;return false;}
static void TrailRibbonGpu_Update(int slot,float dt,const TrailRibbonAnchor *a){(void)slot;(void)dt;(void)a;forces++;}
static void SampleMotion(void *u,Vector3 p,Vector3 v,float dt,int n,float o,FieldSample *s,Vector3 *air){(void)u;(void)p;(void)v;(void)dt;(void)n;(void)o;(void)s;(void)air;forces++;}
'''
main=r'''
int main(void){
 Vector3 points[]={{0,0,0},{5,0,0}};assert(MotionPath_Build(&path,points,2));
 ribbon=(ModernRibbon){.config={.lengthM=1,.pathTransport={.field=123,.speedMps=2}},.gpuSlot=-1};
 assert(TrailRibbon_Initialize(&ribbon.state,3,(Vector3){0},(Vector3){-1,0,0},1,(Vector3){0},TRAIL_RIBBON_FREE));
 assert(!TrailRibbonSystem_Update(2,.1f));
 assert(TrailRibbonSystem_Update(1,.25f));
 assert(fabsf(ribbon.state.position[0].x-.5f)<1e-6f);
 assert(fabsf(ribbon.state.position[1].x)<1e-6f&&fabsf(ribbon.state.position[2].x)<1e-6f);
 assert(TrailRibbonSystem_Update(1,.5f));
 assert(fabsf(ribbon.state.position[0].x-1.5f)<1e-6f&&fabsf(ribbon.state.position[2].x-.5f)<1e-6f);
 assert(copies==2&&!forces&&!staged);
 live=false;assert(TrailRibbonSystem_Update(1,1));assert(ribbon.pathDistanceM==1.5f&&copies==2);
 live=true;ribbon.gpuSlot=4;Vector3 old=ribbon.state.position[0];
 assert(TrailRibbonSystem_Update(1,.25f));assert(staged==1&&uploaded==2&&copies==2&&!forces);
 assert(ribbon.state.position[0].x==old.x); /* GPU geometry stays resident. */
 ribbon.config.pathTransport.speedMps=0;assert(TrailRibbonSystem_Update(1,.25f));assert(uploaded==2);
 ribbon.config.pathTransport.speedMps=2;assert(TrailRibbonSystem_Update(1,10));assert(uploaded==6);
 ribbon.gpuSlot=-1;assert(TrailRibbonSystem_Update(1,.1f));
 for(int n=0;n<3;n++) assert(ribbon.state.position[n].x==5); /* Tail drains into B. */
 assert(!forces);
 puts("Production ribbon spline update: growth, ordered tail, expiry freeze, drain and GPU-only staging PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='wuxing-ribbon-transport-') as tmp:
    p=pathlib.Path(tmp);(p/'test.c').write_text(preamble+code+main)
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-I'+str(ROOT),'-I'+str(ROOT/'core/tests/stubs'),str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
