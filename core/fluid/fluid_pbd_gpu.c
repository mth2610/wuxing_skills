#include "core/fluid/fluid_pbd_gpu.h"
#include "rlgl.h"
#include <math.h>
#include <string.h>

#define GPU_GRID_CELLS (64*32*64)
typedef struct { float px,py,pz,radius, vx,vy,vz,life; } GPUFluidParticle;
static unsigned int s_stateA,s_stateB,s_heads,s_next,s_program;
static bool s_active;

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
    s_active=s_stateA&&s_stateB&&s_heads&&s_next; return s_active;
}
bool FluidPBDGPU_IsActive(void) { return s_active; }
void FluidPBDGPU_SpawnImpact(Vector3 point, Vector3 normal, Vector3 impulse, float force01, float scale)
{
    if(!s_active) return;
    GPUFluidParticle particles[FLUID_PBD_GPU_MAX_PARTICLES]; memset(particles,0,sizeof(particles));
    int count=2048; float radius=scale*0.032f;
    for(int i=0;i<count;i++) { float a=(float)(i*2.39996323f); float r=sqrtf((float)i/(float)count)*radius*15.0f; particles[i]=(GPUFluidParticle){point.x+cosf(a)*r,point.y+radius*(1.0f+(float)(i%7)*0.2f),point.z+sinf(a)*r,radius,impulse.x*(1.5f+force01*2.0f)+cosf(a),impulse.y*(1.5f+force01*2.0f),impulse.z*(1.5f+force01*2.0f)+sinf(a),4.0f}; }
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
