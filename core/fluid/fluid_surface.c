#include "core/fluid/fluid_surface.h"
#include "core/resource_manager.h"
#include "core/screen_distort.h"
#include "core/particles/particle_manager.h"
#include "core/fluid/fluid_pbd_gpu.h"
#include "rlgl.h"
#include <stddef.h>

typedef struct { Vector3 position, radii; } FluidSurfaceParticle;
static FluidSurfaceParticle s_particles[FLUID_SURFACE_MAX_PARTICLES];
static int s_count;
static ParticleRenderStream s_gpuStreams[16];
static int s_gpuStreamCount;
static Texture2D s_surfaceTex;
static RenderTexture2D s_capture, s_thickness, s_smoothA, s_smoothB;
static Shader s_captureShader, s_thicknessShader, s_smooth, s_composite;
static int s_texelLoc, s_dirLoc, s_sigmaLoc, s_thicknessLoc, s_sceneLoc, s_sceneDepthLoc, s_hasDepthLoc;

static RenderTexture2D FluidSurface_LoadDepthTarget(int w, int h) {
    RenderTexture2D t = {0}; t.id = rlLoadFramebuffer();
    if (!t.id) return t;
    rlEnableFramebuffer(t.id);
    // Capture depth in colour, rather than sampling the FBO depth attachment.
    // MoltenVK (used by rlvk on macOS) does not expose sampled FBO depth textures.
    t.texture.id = rlLoadTexture(NULL,w,h,RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16,1);
    t.texture.width=w; t.texture.height=h; t.texture.format=RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16; t.texture.mipmaps=1;
    t.depth.id=rlLoadTextureDepth(w,h,false); t.depth.width=w; t.depth.height=h; t.depth.mipmaps=1;
    rlFramebufferAttach(t.id,t.texture.id,RL_ATTACHMENT_COLOR_CHANNEL0,RL_ATTACHMENT_TEXTURE2D,0);
    rlFramebufferAttach(t.id,t.depth.id,RL_ATTACHMENT_DEPTH,RL_ATTACHMENT_TEXTURE2D,0);
    if (!rlFramebufferComplete(t.id)) TraceLog(LOG_WARNING,"FluidSurface: depth target incomplete");
    rlDisableFramebuffer(); SetTextureFilter(t.texture,TEXTURE_FILTER_BILINEAR); return t;
}
void FluidSurface_Init(int width,int height) {
    int w = width/2, h=height/2;
    s_capture=FluidSurface_LoadDepthTarget(w,h); s_thickness=LoadRenderTexture(w,h); s_smoothA=LoadRenderTexture(w,h); s_smoothB=LoadRenderTexture(w,h);
    s_captureShader=ResourceManager_LoadShader(NULL,"core/fluid/shaders/fluid_capture.fs");
    s_thicknessShader=ResourceManager_LoadShader(NULL,"core/fluid/shaders/fluid_surface_thickness_mesh.fs");
    s_smooth=ResourceManager_LoadShader(NULL,"core/fluid/shaders/fluid_depth_bilateral.fs");
    s_composite=ResourceManager_LoadShader(NULL,"core/fluid/shaders/fluid_surface.fs");
    Image img = GenImageGradientRadial(64, 64, 0.0f, WHITE, BLANK);
    s_surfaceTex = LoadTextureFromImage(img); UnloadImage(img);
    SetTextureFilter(s_surfaceTex, TEXTURE_FILTER_BILINEAR);
    s_texelLoc=GetShaderLocation(s_smooth,"u_texel"); s_dirLoc=GetShaderLocation(s_smooth,"u_direction"); s_sigmaLoc=GetShaderLocation(s_smooth,"u_depthSigma");
    s_thicknessLoc=GetShaderLocation(s_composite,"u_thicknessTex"); s_sceneLoc=GetShaderLocation(s_composite,"u_sceneTex");
    s_sceneDepthLoc=GetShaderLocation(s_composite,"u_sceneDepthTex"); s_hasDepthLoc=GetShaderLocation(s_composite,"u_hasSceneDepth");
}
void FluidSurface_Unload(void) { UnloadTexture(s_surfaceTex); UnloadRenderTexture(s_capture); UnloadRenderTexture(s_thickness); UnloadRenderTexture(s_smoothA); UnloadRenderTexture(s_smoothB); }
void FluidSurface_RegisterParticle(Vector3 p,float r) { FluidSurface_RegisterEllipsoid(p,(Vector3){r,r,r}); }
void FluidSurface_RegisterEllipsoid(Vector3 p,Vector3 radii) { if(s_count<FLUID_SURFACE_MAX_PARTICLES) s_particles[s_count++]=(FluidSurfaceParticle){p,radii}; }
bool FluidSurface_SubmitParticleStream(const ParticleRenderStream *stream) {
    if (!stream || stream->mode != PARTICLE_RENDER_SURFACE_INPUT) return false;
    if (stream->backend == PARTICLE_RENDER_BACKEND_CPU) {
        ParticleSurfaceSample samples[FLUID_SURFACE_MAX_PARTICLES];
        int count = ParticleManager_CopySurfaceSamples(stream, samples, FLUID_SURFACE_MAX_PARTICLES);
        for (int i = 0; i < count; ++i) FluidSurface_RegisterParticle(samples[i].position, samples[i].radius);
        return count > 0;
    }
    if (s_gpuStreamCount >= (int)(sizeof(s_gpuStreams)/sizeof(s_gpuStreams[0]))) return false;
    s_gpuStreams[s_gpuStreamCount++] = *stream;
    return true;
}
bool FluidSurface_HasPending(void) { return s_count > 0 || s_gpuStreamCount > 0 || FluidPBDGPU_IsActive(); }
void FluidSurface_Capture(Camera3D camera) {
    if(!FluidSurface_HasPending()) return;
    BeginTextureMode(s_capture); ClearBackground((Color){255,0,0,0}); BeginMode3D(camera);
    for (int i=0;i<s_gpuStreamCount;i++) ParticleManager_DrawSurfaceStream(&s_gpuStreams[i], camera, s_surfaceTex);
    FluidPBDGPU_DrawSurfaceDepth(camera);
    EndMode3D(); EndTextureMode();
    BeginTextureMode(s_thickness); ClearBackground(BLANK); BeginMode3D(camera);
    rlDrawRenderBatchActive(); rlDisableDepthMask(); rlDisableDepthTest(); BeginBlendMode(BLEND_ADDITIVE);
    for (int i=0;i<s_gpuStreamCount;i++) ParticleManager_DrawSurfaceThicknessStream(&s_gpuStreams[i], camera);
    FluidPBDGPU_DrawSurfaceThickness(camera);
    EndBlendMode(); rlDrawRenderBatchActive(); rlEnableDepthTest(); rlEnableDepthMask(); EndMode3D(); EndTextureMode();
    Vector2 texel={1.0f/s_capture.texture.width,1.0f/s_capture.texture.height};
    float sigma = 0.035f;
    for (int iteration=0; iteration<3; ++iteration) {
        Vector2 horizontal={1.0f,0.0f}, vertical={0.0f,1.0f};
        Texture2D source = iteration ? s_smoothB.texture : s_capture.texture;
        BeginTextureMode(s_smoothA); ClearBackground(WHITE); BeginShaderMode(s_smooth); SetShaderValue(s_smooth,s_texelLoc,&texel,SHADER_UNIFORM_VEC2); SetShaderValue(s_smooth,s_dirLoc,&horizontal,SHADER_UNIFORM_VEC2); SetShaderValue(s_smooth,s_sigmaLoc,&sigma,SHADER_UNIFORM_FLOAT); DrawTextureRec(source,(Rectangle){0,0,source.width,-source.height},(Vector2){0,0},WHITE); EndShaderMode(); EndTextureMode();
        BeginTextureMode(s_smoothB); ClearBackground(WHITE); BeginShaderMode(s_smooth); SetShaderValue(s_smooth,s_dirLoc,&vertical,SHADER_UNIFORM_VEC2); DrawTextureRec(s_smoothA.texture,(Rectangle){0,0,s_smoothA.texture.width,-s_smoothA.texture.height},(Vector2){0,0},WHITE); EndShaderMode(); EndTextureMode();
    }
}
void FluidSurface_Composite(void) {
    if(!FluidSurface_HasPending()) return;
    Vector2 texel={1.0f/s_smoothB.texture.width,1.0f/s_smoothB.texture.height}; int slot1=1,slot2=2,slot3=3;
    Texture2D scene=ScreenDistort_GetSceneTexture(), sceneDepth=ScreenDistort_GetRawDepthTexture(); int has=sceneDepth.id?1:0;
    BeginBlendMode(BLEND_ALPHA); BeginShaderMode(s_composite); SetShaderValue(s_composite,GetShaderLocation(s_composite,"u_texel"),&texel,SHADER_UNIFORM_VEC2); SetShaderValue(s_composite,s_thicknessLoc,&slot1,SHADER_UNIFORM_INT); SetShaderValue(s_composite,s_sceneLoc,&slot2,SHADER_UNIFORM_INT); SetShaderValue(s_composite,s_sceneDepthLoc,&slot3,SHADER_UNIFORM_INT); SetShaderValue(s_composite,s_hasDepthLoc,&has,SHADER_UNIFORM_INT);
    rlActiveTextureSlot(slot1); rlEnableTexture(s_thickness.texture.id); rlActiveTextureSlot(slot2); rlEnableTexture(scene.id); if(has){rlActiveTextureSlot(slot3);rlEnableTexture(sceneDepth.id);} rlActiveTextureSlot(0);
    DrawTexturePro(s_smoothB.texture,(Rectangle){0,0,s_smoothB.texture.width,-s_smoothB.texture.height},(Rectangle){0,0,GetRenderWidth(),GetRenderHeight()},(Vector2){0,0},0,WHITE); EndShaderMode(); EndBlendMode(); s_count=0; s_gpuStreamCount=0;
}
