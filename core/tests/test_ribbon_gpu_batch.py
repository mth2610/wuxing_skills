#!/usr/bin/env python3
"""Run production GPU ribbon submission controls with mocked device calls.

This verifies staging, upload contents and dispatch submission, not shader
execution, GPU timing or rendered appearance.
"""
import pathlib
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = (ROOT / 'core/trails/trail_ribbon_gpu.c').read_text()

def extract(name):
    match = re.search(r'^void '+name+r'\(', SOURCE, re.M)
    start = SOURCE.index('{', match.start())
    end, depth = start + 1, 1
    while depth:
        depth += (SOURCE[end] == '{') - (SOURCE[end] == '}')
        end += 1
    return SOURCE[match.start():end]

STUBS = r'''
#include "raylib.h"
typedef struct {Vector3 position,target,up;float fovy;int projection;} Camera3D;
#include "core/trails/trail_ribbon_gpu.h"
#include "core/motion/motion_wind_gpu.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define RL_SHADER_UNIFORM_FLOAT 1
#define RL_SHADER_UNIFORM_INT 2
static bool s_ready;
static unsigned int s_nodes=1,s_params=2,s_bodies=3,s_scene=4,s_wind=5,s_terrain=6,s_compute=7;
static TrailRibbonGpuParams s_slots[TRAIL_RIBBON_GPU_CAPACITY];
static MotionGpuScene s_sceneSnapshot;
static float s_time,s_updateDt;
static bool s_updatePending;
static unsigned int s_terrainVersion;
static int s_computeDt=10,s_computeTime=11,s_computeSlot=12;
static TrailRibbonGpuParams uploaded[TRAIL_RIBBON_GPU_CAPACITY];
static int uploads[8],dispatches,groups,binds,snapshots,uniformSlot;
static unsigned int paramBytes,sceneBytes;
static float uniformDt,uniformTime;
static WindTerrainGrid grid;
void MotionFields_PackGpu(MotionGpuScene *out){memset(out,0,sizeof(*out));out->meta[0]=2;snapshots++;}
WindMacroConfig Wind_GetMacro(void){return (WindMacroConfig){0};}
const VorticleData *Wind_GetActiveVorticles(int *n){*n=0;return NULL;}
int Wind_GetPublishedMotionAirflowCount(void){return 0;}
WindGuidingGust Wind_GetGuidingWindState(void){return (WindGuidingGust){0};}
const WindTerrainGrid *Wind_GetTerrainGrid(void){return &grid;}
static void rlUpdateShaderBuffer(unsigned int id,const void *data,unsigned int size,unsigned int offset){
 assert(id>0&&id<8&&offset==0);uploads[id]++;
 if(id==s_params){assert(size<=sizeof(uploaded));paramBytes=size;memset(uploaded,0x5a,sizeof(uploaded));memcpy(uploaded,data,size);}
 if(id==s_scene)sceneBytes=size;
}
static void rlEnableShader(unsigned int id){assert(id==s_compute);}
static void rlDisableShader(void){}
static void rlSetUniform(int loc,const void *value,int type,int count){
 assert(count==1);
 if(loc==s_computeDt){assert(type==RL_SHADER_UNIFORM_FLOAT);uniformDt=*(const float*)value;}
 else if(loc==s_computeTime){assert(type==RL_SHADER_UNIFORM_FLOAT);uniformTime=*(const float*)value;}
 else {assert(loc==s_computeSlot&&type==RL_SHADER_UNIFORM_INT);uniformSlot=*(const int*)value;}
}
static void rlBindShaderBuffer(unsigned int id,unsigned int binding){
 assert((id==s_nodes&&binding==0)||(id==s_params&&binding==1)||(id==s_wind&&binding==3)||(id==s_terrain&&binding==4)||(id==s_scene&&binding==5)||(id==s_bodies&&binding==6));binds++;
}
static void rlComputeShaderDispatch(unsigned int x,unsigned int y,unsigned int z){assert(y==1&&z==1);dispatches++;groups=x;}
static void Reset(void){
 memset(s_slots,0,sizeof(s_slots));memset(uploaded,0,sizeof(uploaded));memset(uploads,0,sizeof(uploads));
 s_ready=true;s_time=s_updateDt=0;s_updatePending=false;s_terrainVersion=~0u;grid=(WindTerrainGrid){.version=42};
 dispatches=groups=binds=snapshots=paramBytes=sceneBytes=0;uniformSlot=99;
}
static void Slot(int i){s_slots[i]=(TrailRibbonGpuParams){.meta={8,16,1,1}};}
static void Near(float a,float b){assert(fabsf(a-b)<.000001f);}
'''
MAIN = r'''
int main(void){
 TrailRibbonAnchor anchor={.valid=true,.position={1,2,3},.velocity={4,5,6},.discontinuity=true};
 Reset();Slot(0);Slot(3);Slot(9);
 TrailRibbonGpu_BeginUpdate(.02f,12);
 assert(snapshots==1&&uploads[s_scene]==1&&uploads[s_wind]==1&&uploads[s_terrain]==1);
 assert(sceneBytes==offsetof(MotionGpuScene,fields)+2*sizeof(MotionGpuField));
 TrailRibbonGpu_Update(0,.02f,&anchor);TrailRibbonGpu_Update(3,.02f,&anchor);TrailRibbonGpu_Update(9,.02f,&anchor);
 assert(!dispatches&&!uploads[s_params]&&!binds); /* Every Update stages only. */
 TrailRibbonGpu_EndUpdate();assert(dispatches==1&&groups==10&&uploads[s_params]==1&&binds==6&&uniformSlot==-1);
 assert(paramBytes==10*sizeof(TrailRibbonGpuParams));Near(uniformDt,.02f);Near(uniformTime,12);
 Near(uploaded[3].anchor.x,1);Near(uploaded[3].anchorVelocity.z,6);Near(uploaded[3].anchorVelocity.w,1);
 assert(uploaded[3].meta[2]==1&&uploaded[3].meta[3]==1);
 TrailRibbonGpu_EndUpdate();assert(dispatches==1&&uploads[s_params]==1);
 TrailRibbonGpu_BeginUpdate(.03f,13);TrailRibbonGpu_Update(0,.03f,NULL);TrailRibbonGpu_EndUpdate();
 assert(dispatches==2&&uploads[s_terrain]==1); /* Same terrain version uses cached upload. */
 assert(uploaded[0].meta[2]==0&&uploaded[0].anchor.w==0);
 grid.version++;TrailRibbonGpu_BeginUpdate(.03f,14);assert(uploads[s_terrain]==2);
 TrailRibbonGpu_EndUpdate();assert(dispatches==2); /* No staged update. */
 Reset();Slot(0);Slot(4);Slot(63);
 TrailRibbonGpu_BeginUpdate(.01f,1);TrailRibbonGpu_Update(0,.01f,&anchor);TrailRibbonGpu_Update(4,.01f,&anchor);TrailRibbonGpu_Update(63,.01f,&anchor);
 TrailRibbonGpu_Kill(4);TrailRibbonGpu_Kill(63);TrailRibbonGpu_EndUpdate();
 assert(dispatches==1&&groups==1&&paramBytes==sizeof(TrailRibbonGpuParams));
 Slot(63);TrailRibbonGpu_BeginUpdate(.01f,2);TrailRibbonGpu_Update(63,.01f,&anchor);TrailRibbonGpu_EndUpdate();
 assert(dispatches==2&&groups==64&&paramBytes==sizeof(s_slots));
 TrailRibbonGpuParams zero={0};assert(!memcmp(&uploaded[4],&zero,sizeof(zero))); /* Killed holes must upload inactive controls. */
 Reset();Slot(2);TrailRibbonGpu_BeginUpdate(.01f,1);
 TrailRibbonAnchor release={.valid=true,.velocity={7,8,9}};
 TrailRibbonGpu_Release(2,&release);TrailRibbonGpu_Update(2,.01f,&anchor);
 assert(s_slots[2].meta[2]==0);Near(s_slots[2].anchorVelocity.x,7);Near(s_slots[2].compliance.z,1);
 TrailRibbonGpu_EndUpdate();assert(dispatches==1);Near(uploaded[2].anchorVelocity.z,9);Near(uploaded[2].compliance.z,1);Near(s_slots[2].compliance.z,0);
 TrailRibbonGpu_EndUpdate();assert(dispatches==1);
 TrailRibbonGpu_BeginUpdate(.01f,2);TrailRibbonGpu_Update(2,.01f,&anchor);TrailRibbonGpu_EndUpdate();
 assert(dispatches==2);Near(uploaded[2].compliance.z,0);Near(uploaded[2].anchorVelocity.x,4); /* Release velocity is one submission only. */
 Reset();Slot(0);Slot(3);TrailRibbonGpu_BeginUpdate(.01f,1);
 MotionPathTransport transport={.field=0xf1234567u,.laneOffset={0,.2f,-.3f}};
 TrailRibbonGpu_SetPathTransport(3,&transport,2.75f);
 assert(!dispatches&&!uploads[s_params]);
 TrailRibbonGpu_EndUpdate();assert(dispatches==1&&uploads[s_params]==1&&groups==4);
 assert(uploaded[3].meta[1]==0xf1234567u&&uploaded[3].meta[3]==3);
 assert(uploaded[0].meta[1]==16&&uploaded[0].meta[3]==1);
 Near(uploaded[3].compliance.w,2.75f);Near(uploaded[3].anchor.y,.2f);Near(uploaded[3].anchor.z,-.3f);
 transport.respondToField=true;transport.speedMps=2.25f;
 TrailRibbonGpu_BeginUpdate(.01f,2);TrailRibbonGpu_SetPathTransport(3,&transport,3);
 TrailRibbonGpu_EndUpdate();assert(dispatches==2&&uploaded[3].meta[3]==7);
 Near(uploaded[3].anchorVelocity.w,2.25f);assert(uploaded[0].meta[3]==1);
 Reset();TrailRibbonGpu_BeginUpdate(.01f,1);TrailRibbonGpu_EndUpdate();assert(!dispatches&&!snapshots);
 Slot(1);s_ready=false;TrailRibbonGpu_BeginUpdate(.01f,1);TrailRibbonGpu_Update(1,.01f,&anchor);TrailRibbonGpu_EndUpdate();assert(!dispatches&&!snapshots);
 s_ready=true;
 const float invalid[]={0,-1,NAN,INFINITY};
 for(int i=0;i<4;i++){
   TrailRibbonGpu_BeginUpdate(invalid[i],1);TrailRibbonGpu_Update(1,.01f,&anchor);TrailRibbonGpu_EndUpdate();assert(!dispatches);
   TrailRibbonGpu_BeginUpdate(.01f,1);TrailRibbonGpu_Update(1,invalid[i],&anchor);TrailRibbonGpu_EndUpdate();assert(!dispatches);
 }
 TrailRibbonGpu_BeginUpdate(.01f,NAN);TrailRibbonGpu_Update(1,.01f,&anchor);TrailRibbonGpu_EndUpdate();assert(!dispatches);
 TrailRibbonGpu_BeginUpdate(.01f,1);TrailRibbonGpu_Update(-1,.01f,&anchor);TrailRibbonGpu_Update(64,.01f,&anchor);TrailRibbonGpu_Update(2,.01f,&anchor);TrailRibbonGpu_EndUpdate();assert(!dispatches);
 TrailRibbonGpu_BeginUpdate(.01f,1);TrailRibbonGpu_Update(1,.01f,&anchor);TrailRibbonGpu_Kill(1);TrailRibbonGpu_EndUpdate();assert(!dispatches&&!uploads[s_params]);
 puts("Production ribbon GPU batching: staging, sparse ranges, killed holes, single release and invalid/no-work guards PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='wuxing-ribbon-gpu-batch-') as temp:
    p = pathlib.Path(temp)
    names = ['TrailRibbonGpu_BeginUpdate', 'TrailRibbonGpu_Update',
             'TrailRibbonGpu_SetPathTransport', 'TrailRibbonGpu_EndUpdate', 'TrailRibbonGpu_Release', 'TrailRibbonGpu_Kill']
    (p/'test.c').write_text(STUBS + '\n'.join(extract(name) for name in names) + MAIN)
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-I'+str(ROOT),
                    '-I'+str(ROOT/'core/tests/stubs'), str(p/'test.c'), '-lm',
                    '-o', str(p/'test')], check=True)
    subprocess.run([str(p/'test')], check=True)
