#include "core/trails/trail_ribbon_gpu.h"
#include "core/trails/trail_ribbon.h"
#include "core/motion/motion_wind_gpu.h"
#include "core/resource_manager.h"
#include "core/shading/shader_preprocessor.h"
#include <stddef.h>
#include <string.h>
#if defined(GRAPHICS_API_VULKAN) || defined(WUXING_USE_VULKAN)
#include "third_party/vulkan/rlvk.h"
#include "raymath.h"

static bool s_attempted, s_ready;
static unsigned int s_nodes, s_params, s_bodies, s_scene, s_wind, s_terrain;
static unsigned int s_compute, s_vao, s_vbo;
static Shader s_draw;
static TrailRibbonGpuParams s_slots[TRAIL_RIBBON_GPU_CAPACITY];
static MotionGpuScene s_sceneSnapshot;
static float s_time, s_updateDt;
static bool s_updatePending;
static unsigned int s_terrainVersion;
static int s_computeDt, s_computeTime, s_computeSlot;
static int s_drawMvp,s_drawCamera,s_drawRight,s_drawSlot,s_drawCount,s_drawWidth,s_drawColor;

static bool RibbonBufferValid(unsigned int buffer) {return buffer && buffer!=~0u;}
bool TrailRibbonGpu_IsAvailable(void) {
    if(s_attempted) return s_ready;
    s_attempted=true;
    char *source=ShaderPreprocessor_Load("core/trails/shaders/trail_ribbon.comp");
    if(!source) return false;
    unsigned int shader=rlLoadShader(source,RL_COMPUTE_SHADER);
    MemFree(source);
    if(!RibbonBufferValid(shader)) return false;
    s_compute=rlLoadShaderProgramCompute(shader);rlUnloadShader(shader);
    if(!RibbonBufferValid(s_compute)) return false;
    s_draw=ResourceManager_LoadShader("core/trails/shaders/trail_ribbon_gpu.vs",
                                      "core/trails/shaders/trail_ribbon_gpu.fs");
    if(!RibbonBufferValid(s_draw.id)||s_draw.id==rlGetShaderIdDefault()) {TrailRibbonGpu_Unload();s_attempted=true;return false;}
    s_nodes=rlLoadShaderBuffer(TRAIL_RIBBON_GPU_CAPACITY*TRAIL_RIBBON_MAX_NODES*sizeof(TrailRibbonGpuNode),NULL,RL_DYNAMIC_DRAW);
    s_params=rlLoadShaderBuffer(sizeof(s_slots),s_slots,RL_DYNAMIC_DRAW);
    s_bodies=rlLoadShaderBuffer(TRAIL_RIBBON_GPU_CAPACITY*TRAIL_RIBBON_MAX_NODES*sizeof(MotionGpuBody),NULL,RL_DYNAMIC_DRAW);
    s_scene=rlLoadShaderBuffer(sizeof(MotionGpuScene),NULL,RL_DYNAMIC_DRAW);
    s_wind=rlLoadShaderBuffer(sizeof(WindGPU),NULL,RL_DYNAMIC_DRAW);
    s_terrain=rlLoadShaderBuffer(sizeof(WindTerrainGPU),NULL,RL_DYNAMIC_DRAW);
    const float quad[]={-1,-1,0, 1,-1,0, 1,1,0, -1,-1,0, 1,1,0, -1,1,0};
    s_vao=rlLoadVertexArray();rlEnableVertexArray(s_vao);
    s_vbo=rlLoadVertexBuffer(quad,sizeof(quad),false);
    rlSetVertexAttribute(0,3,RL_FLOAT,false,0,0);rlEnableVertexAttribute(0);
    rlDisableVertexArray();rlDisableVertexBuffer();
    if(!RibbonBufferValid(s_nodes)||!RibbonBufferValid(s_params)||!RibbonBufferValid(s_bodies)||
       !RibbonBufferValid(s_scene)||!RibbonBufferValid(s_wind)||!RibbonBufferValid(s_terrain)||!s_vao||!s_vbo) {
        TrailRibbonGpu_Unload();s_attempted=true;return false;
    }
    s_computeDt=rlGetLocationUniform(s_compute,"u_dt");
    s_computeTime=rlGetLocationUniform(s_compute,"u_time");
    s_computeSlot=rlGetLocationUniform(s_compute,"u_slot");
    s_drawMvp=GetShaderLocation(s_draw,"mvp");
    s_drawCamera=GetShaderLocation(s_draw,"u_camera");
    s_drawRight=GetShaderLocation(s_draw,"u_right");
    s_drawSlot=GetShaderLocation(s_draw,"u_slot");
    s_drawCount=GetShaderLocation(s_draw,"u_count");
    s_drawWidth=GetShaderLocation(s_draw,"u_width");
    s_drawColor=GetShaderLocation(s_draw,"u_color");
    if(s_computeDt<0||s_computeTime<0||s_computeSlot<0||s_drawMvp<0||s_drawCamera<0||s_drawRight<0||
       s_drawSlot<0||s_drawCount<0||s_drawWidth<0||s_drawColor<0) {
        TrailRibbonGpu_Unload();s_attempted=true;return false;
    }
    s_terrainVersion=~0u;s_ready=true;
    return true;
}
int TrailRibbonGpu_Spawn(const TrailRibbonState *state,const TrailRibbonMaterial *material) {
    if(!state||!material||state->count<2||state->count>TRAIL_RIBBON_MAX_NODES||!TrailRibbonGpu_IsAvailable()) return -1;
    for(int slot=0;slot<TRAIL_RIBBON_GPU_CAPACITY;slot++) if(!s_slots[slot].meta[3]) {
        TrailRibbonGpuNode nodes[TRAIL_RIBBON_MAX_NODES]={0};
        MotionGpuBody bodies[TRAIL_RIBBON_MAX_NODES]={0};
        for(int i=0;i<state->count;i++) {
            nodes[i].positionRest=MotionGpu_V4(state->position[i],state->restLength[i]);
            nodes[i].velocity=MotionGpu_V4(state->velocity[i],i==0?state->accumulator:0);
            nodes[i].previous=MotionGpu_V4(state->position[i],0);
            bodies[i]=MotionGpu_PackBodyForReceiver(&material->body,(Vector3){0},(Vector3){0},true,0,
                material->receiverMask?material->receiverMask:MOTION_RECEIVER_TRAIL);
        }
        TrailRibbonGpuParams p={0};
        p.meta[0]=(unsigned int)state->count;
        p.meta[1]=(unsigned int)(material->constraintIterations<1?1:material->constraintIterations>32?32:material->constraintIterations);
        p.meta[2]=state->mode==TRAIL_RIBBON_HEAD_ANCHORED;p.meta[3]=1;
        p.compliance=(Vector4){material->stretchCompliance,material->bendCompliance,0,0};
        s_slots[slot]=p;
        rlUpdateShaderBuffer(s_nodes,nodes,sizeof(nodes),slot*sizeof(nodes));
        rlUpdateShaderBuffer(s_bodies,bodies,sizeof(bodies),slot*sizeof(bodies));
        rlUpdateShaderBuffer(s_params,&p,sizeof(p),slot*sizeof(p));
        return slot;
    }
    return -1;
}
void TrailRibbonGpu_BeginUpdate(float dt,float time) {
    s_updatePending=false; s_updateDt=0;
    if(!s_ready||!isfinite(dt)||dt<=0||!isfinite(time)) return;
    bool any=false;
    for(int i=0;i<TRAIL_RIBBON_GPU_CAPACITY;i++) if(s_slots[i].meta[3]) {any=true;break;}
    if(!any) return;
    s_time=time; s_updateDt=dt;
    MotionFields_PackGpu(&s_sceneSnapshot);
    rlUpdateShaderBuffer(s_scene,&s_sceneSnapshot,
        offsetof(MotionGpuScene,fields)+s_sceneSnapshot.meta[0]*sizeof(MotionGpuField),0);
    WindGPU wind;MotionGpu_PackWind(&wind);rlUpdateShaderBuffer(s_wind,&wind,sizeof(wind),0);
    const WindTerrainGrid *grid=Wind_GetTerrainGrid();
    if(grid->version!=s_terrainVersion) {
        WindTerrainGPU terrain;MotionGpu_PackWindTerrain(grid,&terrain);
        rlUpdateShaderBuffer(s_terrain,&terrain,sizeof(terrain),0);s_terrainVersion=grid->version;
    }
}
void TrailRibbonGpu_Update(int slot,float dt,const TrailRibbonAnchor *anchor) {
    if(!s_ready||slot<0||slot>=TRAIL_RIBBON_GPU_CAPACITY||!s_slots[slot].meta[3]||!isfinite(dt)||dt<=0) return;
    TrailRibbonGpuParams *p=&s_slots[slot];
    if(p->meta[2] && (!anchor||!anchor->valid)) p->meta[2]=0;
    p->anchor=anchor?MotionGpu_V4(anchor->position,anchor->valid?1:0):(Vector4){0};
    if(p->compliance.z<0.5f)
        p->anchorVelocity=anchor?MotionGpu_V4(anchor->velocity,anchor->discontinuity?1:0):(Vector4){0};
    s_updatePending=true;
}
void TrailRibbonGpu_SetPathTransport(int slot,const MotionPathTransport *transport,float distanceM) {
    if(!s_ready||slot<0||slot>=TRAIL_RIBBON_GPU_CAPACITY||!s_slots[slot].meta[3]||!transport||!transport->field) return;
    TrailRibbonGpuParams *p=&s_slots[slot];
    p->meta[1]=transport->field;p->meta[3]=3;
    p->compliance.w=distanceM;p->anchor=MotionGpu_V4(transport->laneOffset,0);
    s_updatePending=true;
}
void TrailRibbonGpu_EndUpdate(void) {
    if(!s_ready || !s_updatePending || s_updateDt<=0) return;
    s_updatePending=false;
    int count=TRAIL_RIBBON_GPU_CAPACITY;
    while(count>0 && !s_slots[count-1].meta[3]) --count;
    if(!count) return;
    rlUpdateShaderBuffer(s_params,s_slots,(unsigned int)(count*sizeof(s_slots[0])),0);
    int allSlots=-1;
    rlEnableShader(s_compute);
    rlSetUniform(s_computeDt,&s_updateDt,RL_SHADER_UNIFORM_FLOAT,1);
    rlSetUniform(s_computeTime,&s_time,RL_SHADER_UNIFORM_FLOAT,1);
    rlSetUniform(s_computeSlot,&allSlots,RL_SHADER_UNIFORM_INT,1);
    rlBindShaderBuffer(s_nodes,0);rlBindShaderBuffer(s_params,1);
    rlBindShaderBuffer(s_wind,3);rlBindShaderBuffer(s_terrain,4);
    rlBindShaderBuffer(s_scene,5);rlBindShaderBuffer(s_bodies,6);
    rlComputeShaderDispatch((unsigned int)count,1,1);rlDisableShader();
    for(int i=0;i<count;i++) s_slots[i].compliance.z=0;
}
void TrailRibbonGpu_Release(int slot,const TrailRibbonAnchor *anchor) {
    if(slot>=0&&slot<TRAIL_RIBBON_GPU_CAPACITY) {
        s_slots[slot].meta[2]=0;
        if(anchor&&anchor->valid) {
            s_slots[slot].anchorVelocity=MotionGpu_V4(anchor->velocity,0);
            s_slots[slot].compliance.z=1;
        }
    }
}
void TrailRibbonGpu_Kill(int slot) {
    if(slot>=0&&slot<TRAIL_RIBBON_GPU_CAPACITY) s_slots[slot]=(TrailRibbonGpuParams){0};
}
void TrailRibbonGpu_Draw(int slot,Camera3D camera,float width,Color color,Texture2D texture) {
    if(!s_ready||slot<0||slot>=TRAIL_RIBBON_GPU_CAPACITY||!s_slots[slot].meta[3]) return;
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ALPHA);BeginShaderMode(s_draw);
    /* Nodes are world-space. The game may keep its camera in rlgl's pushed
     * transform rather than modelview, so take the view from the camera. */
    Matrix mvp=MatrixMultiply(GetCameraMatrix(camera),rlGetMatrixProjection());
    Vector3 right=Vector3Normalize(Vector3CrossProduct(camera.up,Vector3Subtract(camera.position,camera.target)));
    float rgba[]={color.r/255.f,color.g/255.f,color.b/255.f,color.a/255.f};
    int count=(int)s_slots[slot].meta[0];
    SetShaderValueMatrix(s_draw,s_drawMvp,mvp);
    SetShaderValue(s_draw,s_drawCamera,&camera.position,SHADER_UNIFORM_VEC3);
    SetShaderValue(s_draw,s_drawRight,&right,SHADER_UNIFORM_VEC3);
    SetShaderValue(s_draw,s_drawSlot,&slot,SHADER_UNIFORM_INT);
    SetShaderValue(s_draw,s_drawCount,&count,SHADER_UNIFORM_INT);
    SetShaderValue(s_draw,s_drawWidth,&width,SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_draw,s_drawColor,rgba,SHADER_UNIFORM_VEC4);
    rlBindShaderBuffer(s_nodes,0);
    rlActiveTextureSlot(0);rlEnableTexture(texture.id?texture.id:rlGetTextureIdDefault());
    rlDisableBackfaceCulling();rlDisableDepthMask();
    rlEnableShader(s_draw.id);rlEnableVertexArray(s_vao);
    rlDrawVertexArrayInstanced(0,6,count-1);
    rlDisableVertexArray();rlDisableShader();
    rlEnableDepthMask();rlEnableBackfaceCulling();rlDisableTexture();
    EndShaderMode();EndBlendMode();
}
static Shader s_appearanceShader;
static int s_appearanceLocs[11];
void TrailRibbonGpu_DrawAppearance(int slot,Camera3D camera,float width,Texture2D texture,
    const TrailEntity *trail,int layerFilter,const Vector4 *colors,const float *widths) {
    if(!s_ready||slot<0||slot>=TRAIL_RIBBON_GPU_CAPACITY||!s_slots[slot].meta[3]) return;
    if(!s_appearanceShader.id) {
        s_appearanceShader=ResourceManager_LoadShader(
            "core/trails/shaders/trail_ribbon_material_gpu.vs","core/trails/shaders/trail_deform.fs");
        const char *names[]={"mvp","u_camera","u_right","u_slot","u_count","u_width","u_nodeArc","u_ribbonMode","u_fixedNormal","u_nodeColor","u_nodeWidth"};
        for(int i=0;i<11;i++) s_appearanceLocs[i]=GetShaderLocation(s_appearanceShader,names[i]);
    }
    Shader sh=s_appearanceShader;if(!sh.id) return;
    rlDrawRenderBatchActive();
    BeginBlendMode(layerFilter==0?BLEND_ALPHA:trail->blendMode);BeginShaderMode(sh);
    TrailRibbon_BindAppearance(sh,trail,camera,layerFilter);
    Matrix mvp=MatrixMultiply(GetCameraMatrix(camera),rlGetMatrixProjection());
    Vector3 right=Vector3Normalize(Vector3CrossProduct(camera.up,Vector3Subtract(camera.position,camera.target)));
    int count=(int)s_slots[slot].meta[0],one=1;
    SetShaderValueMatrix(sh,s_appearanceLocs[0],mvp);
    SetShaderValue(sh,s_appearanceLocs[1],&camera.position,SHADER_UNIFORM_VEC3);
    SetShaderValue(sh,s_appearanceLocs[2],&right,SHADER_UNIFORM_VEC3);
    SetShaderValue(sh,s_appearanceLocs[3],&slot,SHADER_UNIFORM_INT);
    SetShaderValue(sh,s_appearanceLocs[4],&count,SHADER_UNIFORM_INT);
    SetShaderValue(sh,s_appearanceLocs[5],&width,SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh,s_appearanceLocs[6],&one,SHADER_UNIFORM_INT);
    SetShaderValue(sh,s_appearanceLocs[7],&trail->ribbonMode,SHADER_UNIFORM_INT);
    SetShaderValue(sh,s_appearanceLocs[8],&trail->fixedNormal,SHADER_UNIFORM_VEC3);
    SetShaderValueV(sh,s_appearanceLocs[9],colors,SHADER_UNIFORM_VEC4,count);
    SetShaderValueV(sh,s_appearanceLocs[10],widths,SHADER_UNIFORM_FLOAT,count);
    rlBindShaderBuffer(s_nodes,0);rlActiveTextureSlot(0);
    rlEnableTexture(texture.id?texture.id:rlGetTextureIdDefault());
    rlDisableBackfaceCulling();rlDisableDepthMask();
    rlEnableShader(sh.id);rlEnableVertexArray(s_vao);rlDrawVertexArrayInstanced(0,6,count-1);
    rlDisableVertexArray();rlDisableShader();rlEnableDepthMask();rlEnableBackfaceCulling();rlDisableTexture();
    EndShaderMode();EndBlendMode();
}
void TrailRibbonGpu_Unload(void) {
    s_appearanceShader=(Shader){0};
    unsigned int buffers[]={s_nodes,s_params,s_bodies,s_scene,s_wind,s_terrain};
    for(int i=0;i<6;i++) if(RibbonBufferValid(buffers[i])) rlUnloadShaderBuffer(buffers[i]);
    if(s_vao) rlUnloadVertexArray(s_vao);
    if(s_vbo) rlUnloadVertexBuffer(s_vbo);
    if(s_compute) rlUnloadShaderProgram(s_compute);
    s_nodes=s_params=s_bodies=s_scene=s_wind=s_terrain=s_compute=s_vao=s_vbo=0;
    memset(s_slots,0,sizeof(s_slots));s_ready=false;s_attempted=false;
    s_updatePending=false; s_updateDt=0;
}
#else
bool TrailRibbonGpu_IsAvailable(void) {return false;}
int TrailRibbonGpu_Spawn(const TrailRibbonState *s,const TrailRibbonMaterial *m) {(void)s;(void)m;return -1;}
void TrailRibbonGpu_BeginUpdate(float dt,float time) {(void)dt;(void)time;}
void TrailRibbonGpu_Update(int slot,float dt,const TrailRibbonAnchor *a) {(void)slot;(void)dt;(void)a;}
void TrailRibbonGpu_SetPathTransport(int slot,const MotionPathTransport *t,float d) {(void)slot;(void)t;(void)d;}
void TrailRibbonGpu_EndUpdate(void) {}
void TrailRibbonGpu_Release(int slot,const TrailRibbonAnchor *a) {(void)slot;(void)a;}
void TrailRibbonGpu_Kill(int slot) {(void)slot;}
void TrailRibbonGpu_Draw(int slot,Camera3D c,float w,Color color,Texture2D t) {(void)slot;(void)c;(void)w;(void)color;(void)t;}
void TrailRibbonGpu_Unload(void) {}
void TrailRibbonGpu_DrawAppearance(int slot,Camera3D camera,float width,Texture2D texture,
    const TrailEntity *trail,int layerFilter,const Vector4 *colors,const float *widths) {
    (void)slot;(void)camera;(void)width;(void)texture;(void)trail;(void)layerFilter;(void)colors;(void)widths;
}
#endif
