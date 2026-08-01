#include "core/fluid/fluid_pbd_gpu.h"
#include "rlgl.h"
#include "raymath.h"
#include "core/resource_manager.h"
#include <math.h>
#include <string.h>

#define GPU_GRID_CELLS (64*32*64)
typedef struct { float px,py,pz,radius, vx,vy,vz,life; } GPUFluidParticle;
static unsigned int s_stateA,s_stateB,s_heads,s_next,s_program;
static bool s_active;
static int s_particleCount;
static unsigned int s_vao,s_vbo;
static Shader s_depthShader,s_thicknessShader;

static unsigned int FluidPBDGPU_LoadCompute(void)
{
    char *src=LoadFileText("core/fluid/shaders/fluid_pbd_gpu.comp"); if(!src) return 0;
    unsigned int shader=rlLoadShader(src,RL_COMPUTE_SHADER); UnloadFileText(src);
    if(!shader) return 0; unsigned int program=rlLoadShaderProgramCompute(shader); rlUnloadShader(shader); return program;
}
bool FluidPBDGPU_Init(void)
{
    if(s_active) return true;
    s_program=FluidPBDGPU_LoadCompute(); if(!s_program) return false;
    s_stateA=rlLoadShaderBuffer(sizeof(GPUFluidParticle)*FLUID_PBD_GPU_MAX_PARTICLES,NULL,RL_DYNAMIC_DRAW);
    s_stateB=rlLoadShaderBuffer(sizeof(GPUFluidParticle)*FLUID_PBD_GPU_MAX_PARTICLES,NULL,RL_DYNAMIC_DRAW);
    s_heads=rlLoadShaderBuffer(sizeof(int)*GPU_GRID_CELLS,NULL,RL_DYNAMIC_DRAW);
    s_next=rlLoadShaderBuffer(sizeof(int)*FLUID_PBD_GPU_MAX_PARTICLES,NULL,RL_DYNAMIC_DRAW);
    s_depthShader=ResourceManager_LoadShader("core/fluid/shaders/fluid_pbd_surface.vs","core/fluid/shaders/fluid_capture_particle.fs");
    s_thicknessShader=ResourceManager_LoadShader("core/fluid/shaders/fluid_pbd_surface.vs","core/fluid/shaders/fluid_surface_thickness.fs");
    static const float quad[]={-1,-1,0,1,-1,0,1,1,0,-1,-1,0,1,1,0,-1,1,0};
    s_vao=rlLoadVertexArray(); rlEnableVertexArray(s_vao); s_vbo=rlLoadVertexBuffer(quad,sizeof(quad),false); rlSetVertexAttribute(0,3,RL_FLOAT,0,0,0); rlEnableVertexAttribute(0); rlDisableVertexArray();
    s_active=s_stateA&&s_stateB&&s_heads&&s_next; return s_active;
}
bool FluidPBDGPU_IsActive(void) { return s_active; }
void FluidPBDGPU_SpawnImpact(Vector3 point, Vector3 normal, Vector3 impulse, float force01, float scale)
{
    if(!s_active) return;
    GPUFluidParticle particles[FLUID_PBD_GPU_MAX_PARTICLES]; memset(particles,0,sizeof(particles));
    int count=2048; s_particleCount=count; float radius=scale*0.012f;
    normal=Vector3LengthSqr(normal)>0.00001f?Vector3Normalize(normal):(Vector3){0,1,0};
    for(int i=0;i<count;i++) {
        float t=(float)i/(float)(count-1), a=(float)i*2.39996323f;
        float ring=sqrtf(t), radialSpeed=scale*(0.7f+2.8f*ring*force01);
        float lift=scale*(2.0f+3.8f*(1.0f-ring)+force01*1.6f);
        /* Dense core + rising crown: never seed the volume as a ground disc. */
        float x=point.x+cosf(a)*ring*scale*0.20f;
        float z=point.z+sinf(a)*ring*scale*0.20f;
        float y=point.y+radius*2.0f+ring*scale*0.20f+(1.0f-ring)*scale*0.08f;
        particles[i]=(GPUFluidParticle){x,y,z,radius,
            impulse.x+cosf(a)*radialSpeed+normal.x*lift,
            impulse.y+normal.y*lift,
            impulse.z+sinf(a)*radialSpeed+normal.z*lift,4.0f};
    }
    rlUpdateShaderBuffer(s_stateA,particles,sizeof(GPUFluidParticle)*count,0);
    rlUpdateShaderBuffer(s_stateB,particles,sizeof(GPUFluidParticle)*count,0);
}
void FluidPBDGPU_Update(float dt,float groundY)
{
    if(!s_active) return; unsigned int groups=(FLUID_PBD_GPU_MAX_PARTICLES+255)/256;
    rlEnableShader(s_program); int phase=0,count=2048,cells=GPU_GRID_CELLS; float cell=.095f;
    rlBindShaderBuffer(s_stateA,0); rlBindShaderBuffer(s_stateB,1); rlBindShaderBuffer(s_heads,2); rlBindShaderBuffer(s_next,3);
    int loc=rlGetLocationUniform(s_program,"u_phase"); rlSetUniform(loc,&phase,RL_SHADER_UNIFORM_INT,1); loc=rlGetLocationUniform(s_program,"u_gridCellCount"); rlSetUniform(loc,&cells,RL_SHADER_UNIFORM_INT,1); rlComputeShaderDispatch((cells+255)/256,1,1);
    phase=1; rlSetUniform(rlGetLocationUniform(s_program,"u_phase"),&phase,RL_SHADER_UNIFORM_INT,1); rlSetUniform(rlGetLocationUniform(s_program,"u_particleCount"),&count,RL_SHADER_UNIFORM_INT,1); rlSetUniform(rlGetLocationUniform(s_program,"u_dt"),&dt,RL_SHADER_UNIFORM_FLOAT,1); rlSetUniform(rlGetLocationUniform(s_program,"u_cellSize"),&cell,RL_SHADER_UNIFORM_FLOAT,1); rlSetUniform(rlGetLocationUniform(s_program,"u_groundY"),&groundY,RL_SHADER_UNIFORM_FLOAT,1); rlComputeShaderDispatch(groups,1,1);
    for(int i=0;i<4;i++){ phase=2; rlSetUniform(rlGetLocationUniform(s_program,"u_phase"),&phase,RL_SHADER_UNIFORM_INT,1); rlComputeShaderDispatch(groups,1,1); unsigned int t=s_stateA;s_stateA=s_stateB;s_stateB=t; rlBindShaderBuffer(s_stateA,0);rlBindShaderBuffer(s_stateB,1); }
    rlDisableShader();
}
void FluidPBDGPU_Unload(void) { if(s_stateA)rlUnloadShaderBuffer(s_stateA);if(s_stateB)rlUnloadShaderBuffer(s_stateB);if(s_heads)rlUnloadShaderBuffer(s_heads);if(s_next)rlUnloadShaderBuffer(s_next);if(s_program)rlUnloadShaderProgram(s_program);s_stateA=s_stateB=s_heads=s_next=s_program=0;s_active=false; }
unsigned int FluidPBDGPU_GetStateBuffer(void) { return s_stateA; }
int FluidPBDGPU_GetParticleCount(void) { return s_particleCount; }

static void FluidPBDGPU_Draw(Camera3D camera, Shader shader)
{
    if(!s_active||s_particleCount<=0) return;
    Matrix view=MatrixLookAt(camera.position,camera.target,camera.up); float aspect=(float)GetScreenWidth()/(float)GetScreenHeight(); double top=tan(camera.fovy*0.5*DEG2RAD); Matrix projection=MatrixFrustum(-top*aspect,top*aspect,-top,top,1.0,1000.0);
    BeginShaderMode(shader); int loc=GetShaderLocation(shader,"u_view"); if(loc>=0)SetShaderValueMatrix(shader,loc,view); loc=GetShaderLocation(shader,"u_projection");if(loc>=0)SetShaderValueMatrix(shader,loc,projection);
    rlBindShaderBuffer(s_stateA,0); rlEnableShader(shader.id); rlEnableVertexArray(s_vao); rlDrawVertexArrayInstanced(0,6,s_particleCount); rlDisableVertexArray(); rlDisableShader(); EndShaderMode();
}
void FluidPBDGPU_DrawSurfaceDepth(Camera3D camera){FluidPBDGPU_Draw(camera,s_depthShader);}
void FluidPBDGPU_DrawSurfaceThickness(Camera3D camera){FluidPBDGPU_Draw(camera,s_thicknessShader);}
