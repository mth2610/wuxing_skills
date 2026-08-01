#include "core/fluid/fluid_pbd_gpu.h"
#include "rlgl.h"
#include "raymath.h"
#include "core/resource_manager.h"
#include "core/gfx_quality.h"
#include <math.h>
#include <string.h>

#define GPU_GRID_CELLS (32*32*32)
#define FLUID_PBD_LIFETIME 2.50f
/* Three vec4s: keep the C stride identical to GLSL std430. */
typedef struct {
    float px,py,pz,radius;
    float vx,vy,vz,life;
    float phase,seed,hasImpacted,pad;
} GPUFluidParticle;
static unsigned int s_stateA,s_stateB,s_heads,s_next,s_program;
static bool s_active;
static bool s_initAttempted;
static int s_particleCount;
static Vector3 s_receiverPoint, s_receiverNormal;
static float s_impactAge;
static float s_gridCellSize;
static unsigned int s_vao,s_vbo;
static Shader s_depthShader,s_thicknessShader;

static float FluidPBDGPU_Rand01(unsigned int value)
{
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return (float)(value & 0x00ffffffU) / 16777216.0f;
}

static unsigned int FluidPBDGPU_LoadCompute(void)
{
    char *src=LoadFileText("core/fluid/shaders/fluid_pbd_gpu.comp"); if(!src) return 0;
    unsigned int shader=rlLoadShader(src,RL_COMPUTE_SHADER); UnloadFileText(src);
    if(!shader) return 0; unsigned int program=rlLoadShaderProgramCompute(shader); rlUnloadShader(shader); return program;
}
bool FluidPBDGPU_Init(void)
{
    if(s_active) return true;
    if(s_initAttempted) return false;
    s_initAttempted=true;
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
/* `s_active` means resources exist.  SSF must instead follow live particles:
 * otherwise a dead impact still pays two capture passes plus six filters every
 * frame forever. */
bool FluidPBDGPU_IsActive(void) { return s_active && s_particleCount > 0; }
void FluidPBDGPU_SpawnImpact(Vector3 point, Vector3 normal, Vector3 impulse, float force01, float scale)
{
    if(!s_active) return;
    GPUFluidParticle particles[FLUID_PBD_GPU_MAX_PARTICLES]; memset(particles,0,sizeof(particles));
    GfxQuality quality=GfxQuality_Get();
    int count=quality<=GFX_LOW?1024:(quality>=GFX_HIGH?2048:1536);
    s_particleCount=count;
    /* Keep PBD kernels below random-close-packing density.  The old 0.012 m
     * radius injected separation energy at spawn, which looked like a boiling
     * source sphere.  Surface rendering expands these kernels independently. */
    float radius=scale*(quality<=GFX_LOW?0.010f:(quality>=GFX_HIGH?0.0085f:0.0092f));
    /* The largest pair support is < 0.044*scale.  A 0.046*scale cell still
     * guarantees that all interacting neighbours are in the surrounding 27
     * cells, while avoiding the old 9.5 cm cell's dense O(n^2)-like buckets.
     * A symmetric 32^3 domain keeps this valid for walls and ceilings too. */
    s_gridCellSize=fmaxf(scale,0.10f)*0.046f;
    normal=Vector3LengthSqr(normal)>0.00001f?Vector3Normalize(normal):(Vector3){0,1,0};
    s_receiverPoint=point;
    s_receiverNormal=normal;
    s_impactAge=0.0f;
    /* The volume and crown live in the receiver's tangent frame.  Keeping this
     * basis here (rather than in the surface shader) makes impacts on walls,
     * slopes and ceilings physically coherent. */
    Vector3 reference=fabsf(normal.y)<0.95f?(Vector3){0,1,0}:(Vector3){1,0,0};
    Vector3 tangent=Vector3Normalize(Vector3CrossProduct(reference,normal));
    Vector3 bitangent=Vector3Normalize(Vector3CrossProduct(normal,tangent));
    /* 2,048 splats of this radius occupy roughly a 0.15 m sphere.  Starting
     * with less space forces the Jacobi separation to explode the volume. */
    float volumeRadius=scale*(0.145f+0.035f*force01);
    /* The event is submitted at contact, but seed the complete virtual water
     * body a few frames upstream.  It then reaches the receiver as one mass
     * instead of materialising as a symmetric sphere at the hit point. */
    float speed=Vector3Length(impulse);
    Vector3 travelDir=speed>0.001f?Vector3Scale(impulse,1.0f/speed):Vector3Negate(normal);
    float preImpactDistance=Clamp(speed*0.055f,scale*0.18f,scale*0.45f);
    Vector3 seedCenter=Vector3Subtract(point,Vector3Scale(travelDir,preImpactDistance));
    for(int i=0;i<count;i++) {
        /* Low-discrepancy points fill a compact ball, rather than placing every
         * particle on a ring.  It is deliberately a short distance above the
         * receiver so the supplied incoming velocity produces the first impact. */
        unsigned int seed=(unsigned int)i*747796405U + 2891336453U;
        float z=1.0f-2.0f*FluidPBDGPU_Rand01(seed);
        float a=FluidPBDGPU_Rand01(seed+1U)*2.0f*PI;
        float shell=cbrtf(FluidPBDGPU_Rand01(seed+2U));
        float xy=sqrtf(fmaxf(0.0f,1.0f-z*z));
        Vector3 volumeDirection=Vector3Add(Vector3Add(Vector3Scale(tangent,cosf(a)*xy),
            Vector3Scale(bitangent,sinf(a)*xy)),Vector3Scale(normal,z));
        Vector3 offset=Vector3Scale(volumeDirection,shell*volumeRadius);
        /* A perfectly isotropic seed survives as a screen-space circle because
         * this lightweight solver has separation but no full density pressure.
         * Compress it along travel and perturb the shell so the incoming mass
         * already has an authored, liquid-like irregular silhouette. */
        float axial=Vector3DotProduct(offset,travelDir);
        Vector3 lateral=Vector3Subtract(offset,Vector3Scale(travelDir,axial));
        float lobe=0.82f+0.30f*FluidPBDGPU_Rand01(seed+3U);
        offset=Vector3Add(Vector3Scale(lateral,lobe),
                          Vector3Scale(travelDir,axial*0.58f));
        Vector3 position=Vector3Add(seedCenter,offset);
        /* Do not manufacture lift or radial velocity at spawn.  The collision
         * phase converts the real normal impulse into a crown at contact. */
        Vector3 velocity=impulse;
        /* Phase follows the water volume instead of an unrelated random roll:
         * a coherent 42% core remains the body, the surrounding 50% shell
         * forms the crown, and only the outer 8% becomes fast hero droplets.
         * Randomly marking 82% of the entire ball as crown/jet made even its
         * centre disperse and left too few samples for a connected surface. */
        float phase=shell<0.749f?0.0f:(shell<0.973f?1.0f:2.0f);
        float particleSeed=FluidPBDGPU_Rand01(seed+5U);
        /* A monodisperse PBD population crystallises into near-hexagonal rows
         * after repeated separation solves. Those rows line up the identical
         * per-splat highlights into the visible parallel bands. Real spray is
         * polydisperse, so vary the physical constraint radius as well as the
         * independent optical kernel. */
        float physicalScale=0.80f+0.40f*FluidPBDGPU_Rand01(seed+6U);
        float particleRadius=radius*physicalScale;
        float opticalJitter=FluidPBDGPU_Rand01(seed+7U);
        particles[i]=(GPUFluidParticle){position.x,position.y,position.z,particleRadius,
            velocity.x,velocity.y,velocity.z,FLUID_PBD_LIFETIME,
            phase,particleSeed,0.0f,opticalJitter};
    }
    rlUpdateShaderBuffer(s_stateA,particles,sizeof(GPUFluidParticle)*count,0);
    rlUpdateShaderBuffer(s_stateB,particles,sizeof(GPUFluidParticle)*count,0);
}
void FluidPBDGPU_Update(float dt,float groundY)
{
    (void)groundY;
    if(!s_active || s_particleCount<=0) return;
    s_impactAge+=dt;
    if(s_impactAge >= FLUID_PBD_LIFETIME) { s_particleCount=0; return; }
    unsigned int groups=(s_particleCount+255)/256;
    rlEnableShader(s_program); int phase=0,count=s_particleCount,cells=GPU_GRID_CELLS; float cell=s_gridCellSize;
    rlBindShaderBuffer(s_stateA,0); rlBindShaderBuffer(s_stateB,1); rlBindShaderBuffer(s_heads,2); rlBindShaderBuffer(s_next,3);
    int loc=rlGetLocationUniform(s_program,"u_phase"); rlSetUniform(loc,&phase,RL_SHADER_UNIFORM_INT,1); loc=rlGetLocationUniform(s_program,"u_gridCellCount"); rlSetUniform(loc,&cells,RL_SHADER_UNIFORM_INT,1); rlComputeShaderDispatch((cells+255)/256,1,1);
    phase=1; rlSetUniform(rlGetLocationUniform(s_program,"u_phase"),&phase,RL_SHADER_UNIFORM_INT,1); rlSetUniform(rlGetLocationUniform(s_program,"u_particleCount"),&count,RL_SHADER_UNIFORM_INT,1); rlSetUniform(rlGetLocationUniform(s_program,"u_dt"),&dt,RL_SHADER_UNIFORM_FLOAT,1); rlSetUniform(rlGetLocationUniform(s_program,"u_cellSize"),&cell,RL_SHADER_UNIFORM_FLOAT,1);
    rlSetUniform(rlGetLocationUniform(s_program,"u_receiverPoint"),&s_receiverPoint.x,RL_SHADER_UNIFORM_VEC3,1); rlSetUniform(rlGetLocationUniform(s_program,"u_receiverNormal"),&s_receiverNormal.x,RL_SHADER_UNIFORM_VEC3,1); rlSetUniform(rlGetLocationUniform(s_program,"u_gridOrigin"),&s_receiverPoint.x,RL_SHADER_UNIFORM_VEC3,1); rlSetUniform(rlGetLocationUniform(s_program,"u_impactAge"),&s_impactAge,RL_SHADER_UNIFORM_FLOAT,1); rlComputeShaderDispatch(groups,1,1);
    /* The grid is built from B during integration, so B must become the solver
     * source before Jacobi reads it.  This was previously one state behind. */
    unsigned int t=s_stateA;s_stateA=s_stateB;s_stateB=t; rlBindShaderBuffer(s_stateA,0);rlBindShaderBuffer(s_stateB,1);
    int iterations=GfxQuality_Get()<=GFX_LOW?2:(GfxQuality_Get()>=GFX_HIGH?4:3);
    /* Once every per-particle viscous tail has ended, continuing Jacobi only
     * crystallises equal-radius kernels into visible rows. Integration still
     * owns lifetime, but the stationary puddle no longer pays grid solves. */
    if(s_impactAge>=1.24f) iterations=0;
    int solverIterationLoc=rlGetLocationUniform(s_program,"u_solverIteration");
    for(int i=0;i<iterations;i++){
        /* Rebuild from the current ping-pong source before every Jacobi pass.
         * The old code reused heads from an earlier buffer, creating unstable
         * force at the original impact point. */
        phase=0; rlSetUniform(rlGetLocationUniform(s_program,"u_phase"),&phase,RL_SHADER_UNIFORM_INT,1); rlComputeShaderDispatch((cells+255)/256,1,1);
        phase=3; rlSetUniform(rlGetLocationUniform(s_program,"u_phase"),&phase,RL_SHADER_UNIFORM_INT,1); rlComputeShaderDispatch(groups,1,1);
        phase=2; rlSetUniform(rlGetLocationUniform(s_program,"u_phase"),&phase,RL_SHADER_UNIFORM_INT,1); rlSetUniform(solverIterationLoc,&i,RL_SHADER_UNIFORM_INT,1); rlComputeShaderDispatch(groups,1,1);
        t=s_stateA;s_stateA=s_stateB;s_stateB=t; rlBindShaderBuffer(s_stateA,0);rlBindShaderBuffer(s_stateB,1);
    }
    rlDisableShader();
}
void FluidPBDGPU_Unload(void) { if(s_stateA)rlUnloadShaderBuffer(s_stateA);if(s_stateB)rlUnloadShaderBuffer(s_stateB);if(s_heads)rlUnloadShaderBuffer(s_heads);if(s_next)rlUnloadShaderBuffer(s_next);if(s_program)rlUnloadShaderProgram(s_program);s_stateA=s_stateB=s_heads=s_next=s_program=0;s_active=false;s_initAttempted=false; }
unsigned int FluidPBDGPU_GetStateBuffer(void) { return s_stateA; }
int FluidPBDGPU_GetParticleCount(void) { return s_particleCount; }

static void FluidPBDGPU_Draw(Camera3D camera, Shader shader)
{
    if(!s_active||s_particleCount<=0) return;
    Matrix view=MatrixLookAt(camera.position,camera.target,camera.up);
    float aspect=(float)GetScreenWidth()/(float)GetScreenHeight();
    Matrix projection;
    if(camera.projection==CAMERA_ORTHOGRAPHIC) {
        double top=camera.fovy*0.5;
        projection=MatrixOrtho(-top*aspect,top*aspect,-top,top,0.0001,150.0);
    } else {
        double top=tan(camera.fovy*0.5*DEG2RAD);
        projection=MatrixFrustum(-top*aspect,top*aspect,-top,top,1.0,1000.0);
    }
    BeginShaderMode(shader); int loc=GetShaderLocation(shader,"u_view"); if(loc>=0)SetShaderValueMatrix(shader,loc,view); loc=GetShaderLocation(shader,"u_projection");if(loc>=0)SetShaderValueMatrix(shader,loc,projection);
    rlBindShaderBuffer(s_stateA,0); rlEnableShader(shader.id); rlEnableVertexArray(s_vao); rlDrawVertexArrayInstanced(0,6,s_particleCount); rlDisableVertexArray(); rlDisableShader(); EndShaderMode();
}
void FluidPBDGPU_DrawSurfaceDepth(Camera3D camera){FluidPBDGPU_Draw(camera,s_depthShader);}
void FluidPBDGPU_DrawSurfaceThickness(Camera3D camera){FluidPBDGPU_Draw(camera,s_thicknessShader);}
