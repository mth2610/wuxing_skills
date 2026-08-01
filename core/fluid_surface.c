#include "core/fluid_surface.h"
#include "core/resource_manager.h"
#include "core/screen_distort.h"
#include "rlgl.h"
#include <stddef.h>

typedef struct { Vector3 position; float radius; } FluidSurfaceParticle;
static FluidSurfaceParticle s_particles[FLUID_SURFACE_MAX_PARTICLES];
static int s_count;
static RenderTexture2D s_capture, s_smoothA, s_smoothB;
static Shader s_smooth, s_composite;
static int s_texelLoc, s_thicknessLoc, s_sceneLoc, s_sceneDepthLoc, s_hasDepthLoc;

static RenderTexture2D FluidSurface_LoadDepthTarget(int w, int h) {
    RenderTexture2D t = {0}; t.id = rlLoadFramebuffer();
    if (!t.id) return t;
    rlEnableFramebuffer(t.id);
    t.texture.id = rlLoadTexture(NULL,w,h,RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,1);
    t.texture.width=w; t.texture.height=h; t.texture.format=RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8; t.texture.mipmaps=1;
    t.depth.id=rlLoadTextureDepth(w,h,false); t.depth.width=w; t.depth.height=h; t.depth.mipmaps=1;
    rlFramebufferAttach(t.id,t.texture.id,RL_ATTACHMENT_COLOR_CHANNEL0,RL_ATTACHMENT_TEXTURE2D,0);
    rlFramebufferAttach(t.id,t.depth.id,RL_ATTACHMENT_DEPTH,RL_ATTACHMENT_TEXTURE2D,0);
    if (!rlFramebufferComplete(t.id)) TraceLog(LOG_WARNING,"FluidSurface: depth target incomplete");
    rlDisableFramebuffer(); SetTextureFilter(t.texture,TEXTURE_FILTER_BILINEAR); return t;
}
void FluidSurface_Init(int width,int height) {
    int w = width/2, h=height/2;
    s_capture=FluidSurface_LoadDepthTarget(w,h); s_smoothA=LoadRenderTexture(w,h); s_smoothB=LoadRenderTexture(w,h);
    s_smooth=ResourceManager_LoadShader(NULL,"core/shaders/fluid_depth_smooth.fs");
    s_composite=ResourceManager_LoadShader(NULL,"core/shaders/fluid_surface.fs");
    s_texelLoc=GetShaderLocation(s_smooth,"u_texel");
    s_thicknessLoc=GetShaderLocation(s_composite,"u_thicknessTex"); s_sceneLoc=GetShaderLocation(s_composite,"u_sceneTex");
    s_sceneDepthLoc=GetShaderLocation(s_composite,"u_sceneDepthTex"); s_hasDepthLoc=GetShaderLocation(s_composite,"u_hasSceneDepth");
}
void FluidSurface_Unload(void) { UnloadRenderTexture(s_capture); UnloadRenderTexture(s_smoothA); UnloadRenderTexture(s_smoothB); }
void FluidSurface_RegisterParticle(Vector3 p,float r) { if(s_count<FLUID_SURFACE_MAX_PARTICLES) s_particles[s_count++]=(FluidSurfaceParticle){p,r}; }
void FluidSurface_Capture(Camera3D camera) {
    if(!s_count) return;
    BeginTextureMode(s_capture); ClearBackground(BLANK); BeginMode3D(camera);
    for(int i=0;i<s_count;i++) DrawSphere(s_particles[i].position,s_particles[i].radius,WHITE);
    EndMode3D(); EndTextureMode();
    Vector2 texel={1.0f/s_capture.texture.width,1.0f/s_capture.texture.height};
    BeginTextureMode(s_smoothA); ClearBackground(WHITE); BeginShaderMode(s_smooth); SetShaderValue(s_smooth,s_texelLoc,&texel,SHADER_UNIFORM_VEC2); DrawTextureRec(s_capture.depth,(Rectangle){0,0,s_capture.depth.width,-s_capture.depth.height},(Vector2){0,0},WHITE); EndShaderMode(); EndTextureMode();
    BeginTextureMode(s_smoothB); ClearBackground(WHITE); BeginShaderMode(s_smooth); DrawTextureRec(s_smoothA.texture,(Rectangle){0,0,s_smoothA.texture.width,-s_smoothA.texture.height},(Vector2){0,0},WHITE); EndShaderMode(); EndTextureMode();
}
void FluidSurface_Composite(void) {
    if(!s_count) return;
    Vector2 texel={1.0f/s_smoothB.texture.width,1.0f/s_smoothB.texture.height}; int slot1=1,slot2=2,slot3=3;
    Texture2D scene=ScreenDistort_GetSceneTexture(), sceneDepth=ScreenDistort_GetRawDepthTexture(); int has=sceneDepth.id?1:0;
    BeginBlendMode(BLEND_ALPHA); BeginShaderMode(s_composite); SetShaderValue(s_composite,GetShaderLocation(s_composite,"u_texel"),&texel,SHADER_UNIFORM_VEC2); SetShaderValue(s_composite,s_thicknessLoc,&slot1,SHADER_UNIFORM_INT); SetShaderValue(s_composite,s_sceneLoc,&slot2,SHADER_UNIFORM_INT); SetShaderValue(s_composite,s_sceneDepthLoc,&slot3,SHADER_UNIFORM_INT); SetShaderValue(s_composite,s_hasDepthLoc,&has,SHADER_UNIFORM_INT);
    rlActiveTextureSlot(slot1); rlEnableTexture(s_capture.texture.id); rlActiveTextureSlot(slot2); rlEnableTexture(scene.id); if(has){rlActiveTextureSlot(slot3);rlEnableTexture(sceneDepth.id);} rlActiveTextureSlot(0);
    DrawTexturePro(s_smoothB.texture,(Rectangle){0,0,s_smoothB.texture.width,-s_smoothB.texture.height},(Rectangle){0,0,GetRenderWidth(),GetRenderHeight()},(Vector2){0,0},0,WHITE); EndShaderMode(); EndBlendMode(); s_count=0;
}
