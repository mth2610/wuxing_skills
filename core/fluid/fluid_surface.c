#include "core/fluid/fluid_surface.h"
#include "core/resource_manager.h"
#include "core/screen_distort.h"
#include "core/particles/particle_manager.h"
#include "core/fluid/fluid_pbd_gpu.h"
#include "core/gfx_quality.h"
#include "core/vfx_light.h"
#include "environment/environment_system.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>

#define FLUID_OPTICAL_POINT_LIGHTS 4

typedef struct { Vector3 position, radii; } FluidSurfaceParticle;
static FluidSurfaceParticle s_particles[FLUID_SURFACE_MAX_PARTICLES];
static int s_count;
static ParticleRenderStream s_gpuStreams[16];
static int s_gpuStreamCount;
static Texture2D s_surfaceTex;
static RenderTexture2D s_capture, s_thickness, s_smoothA, s_smoothB;
static RenderTexture2D s_sceneCopy;   // refraction source, see FluidSurface_LoadColorTarget
static Shader s_captureShader, s_thicknessShader, s_smooth, s_composite;
static int s_texelLoc, s_dirLoc, s_depthRangeLoc, s_fillLoc;
static int s_smoothProjectionLoc, s_smoothInverseProjectionLoc;
static int s_kernelRadiusLoc, s_filterRadiusLoc;
static int s_compositeTexelLoc, s_sceneTexelLoc, s_thicknessLoc, s_sceneLoc, s_sceneDepthLoc, s_hasDepthLoc;
static int s_projectionLoc, s_inverseProjectionLoc, s_viewToWorldLoc, s_qualityTierLoc;
static int s_sunDirectionLoc, s_sunColorLoc, s_skyAmbientLoc, s_groundAmbientLoc;
static int s_pointLightCountLoc, s_pointLightPosLoc, s_pointLightColorLoc;
static int s_materialBodyLoc, s_materialGlowLoc, s_materialSoftLoc;
static int s_timeLoc;
static Matrix s_fluidView, s_fluidProjection;
static Color s_materialBody = {41, 128, 185, 255};
static Color s_materialGlow = {80, 180, 255, 255};
static Color s_materialSoft = {160, 225, 255, 255};
static float s_reconstructionRadius = 0.022f;

static Vector3 FluidSurface_ColorToVec3(Color color) {
    return (Vector3){
        color.r / 255.0f,
        color.g / 255.0f,
        color.b / 255.0f
    };
}

static Vector3 FluidSurface_TransformDirection(Vector3 direction, Matrix matrix) {
    return (Vector3){
        matrix.m0*direction.x + matrix.m4*direction.y + matrix.m8*direction.z,
        matrix.m1*direction.x + matrix.m5*direction.y + matrix.m9*direction.z,
        matrix.m2*direction.x + matrix.m6*direction.y + matrix.m10*direction.z
    };
}

/* Match main.c::MyBeginMode3D exactly.  Fluid and raw scene depth are only
 * comparable when both were produced by the same projection. */
static Matrix FluidSurface_MakeProjection(Camera3D camera) {
    float aspect=(float)GetScreenWidth()/(float)GetScreenHeight();
    if (camera.projection == CAMERA_ORTHOGRAPHIC) {
        double top=camera.fovy*0.5;
        double right=top*aspect;
        return MatrixOrtho(-right,right,-top,top,0.0001,150.0);
    }
    double top=tan(camera.fovy*0.5*DEG2RAD);
    double right=top*aspect;
    return MatrixFrustum(-right,right,-top,top,1.0,1000.0);
}

static RenderTexture2D FluidSurface_LoadDepthTarget(int w, int h) {
    RenderTexture2D t = {0}; t.id = rlLoadFramebuffer();
    if (!t.id) return t;
    rlEnableFramebuffer(t.id);
    // Capture depth in colour, rather than sampling the FBO depth attachment.
    // MoltenVK (used by rlvk on macOS) does not expose sampled FBO depth textures.
    // R32F is also essential here: non-linear device depth loses visible
    // surface gradients when stored in half-float near depth 1.
    t.texture.id = rlLoadTexture(NULL,w,h,RL_PIXELFORMAT_UNCOMPRESSED_R32,1);
    t.texture.width=w; t.texture.height=h; t.texture.format=RL_PIXELFORMAT_UNCOMPRESSED_R32; t.texture.mipmaps=1;
    t.depth.id=rlLoadTextureDepth(w,h,false); t.depth.width=w; t.depth.height=h; t.depth.mipmaps=1;
    rlFramebufferAttach(t.id,t.texture.id,RL_ATTACHMENT_COLOR_CHANNEL0,RL_ATTACHMENT_TEXTURE2D,0);
    rlFramebufferAttach(t.id,t.depth.id,RL_ATTACHMENT_DEPTH,RL_ATTACHMENT_TEXTURE2D,0);
    if (!rlFramebufferComplete(t.id)) TraceLog(LOG_WARNING,"FluidSurface: depth target incomplete");
    rlDisableFramebuffer(); SetTextureFilter(t.texture,TEXTURE_FILTER_BILINEAR); return t;
}

/* A private copy of the scene, in the scene's own format, for the refraction tap.
 *
 * The SSF composite samples "what is behind the water" while it draws the water.
 * Until 2026-08-10 those were two different images: `ScreenDistort_BeginVFXBody()`
 * bound a separate `vfxBodyTex` layer, so sampling `renderTex.texture` was safe.
 * Retiring the split layers (b03b7b6) made the body pass bind `renderTex` itself —
 * the very texture `ScreenDistort_GetSceneTexture()` returns — so the composite
 * began sampling its own colour attachment. That is undefined in GL and a
 * read/write hazard in Vulkan; the refraction tap stops returning the background,
 * and the water collapses to its own opaque terms (in-scatter + specular), i.e.
 * cyan plastic with a silver rim. Copying the scene during Capture, which runs
 * BEFORE the body pass binds anything, restores a well-defined source without
 * resurrecting the retired layer targets. */
static RenderTexture2D FluidSurface_LoadColorTarget(int w, int h, int format) {
    RenderTexture2D t = {0};
    t.id = rlLoadFramebuffer();
    if (!t.id) return t;
    rlEnableFramebuffer(t.id);
    t.texture.id = rlLoadTexture(NULL, w, h, format, 1);
    t.texture.width = w;
    t.texture.height = h;
    t.texture.format = format;
    t.texture.mipmaps = 1;
    rlFramebufferAttach(t.id, t.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0,
                        RL_ATTACHMENT_TEXTURE2D, 0);
    if (!rlFramebufferComplete(t.id))
        TraceLog(LOG_WARNING, "FluidSurface: scene copy target incomplete");
    rlDisableFramebuffer();
    SetTextureFilter(t.texture, TEXTURE_FILTER_BILINEAR);
    return t;
}

static RenderTexture2D FluidSurface_LoadScalarTarget(int w, int h) {
    RenderTexture2D t={0};
    t.id=rlLoadFramebuffer();
    if(!t.id) return t;
    rlEnableFramebuffer(t.id);
    t.texture.id=rlLoadTexture(NULL,w,h,RL_PIXELFORMAT_UNCOMPRESSED_R32,1);
    t.texture.width=w;
    t.texture.height=h;
    t.texture.format=RL_PIXELFORMAT_UNCOMPRESSED_R32;
    t.texture.mipmaps=1;
    rlFramebufferAttach(t.id,t.texture.id,RL_ATTACHMENT_COLOR_CHANNEL0,
                        RL_ATTACHMENT_TEXTURE2D,0);
    if(!rlFramebufferComplete(t.id))
        TraceLog(LOG_WARNING,"FluidSurface: scalar target incomplete");
    rlDisableFramebuffer();
    SetTextureFilter(t.texture,TEXTURE_FILTER_BILINEAR);
    return t;
}

void FluidSurface_Init(int width,int height) {
    /* Keep the authored High surface at native resolution while its optical
     * look is being judged. R32F prevents the former zoom-dependent depth
     * bands; lower tiers retain the cheaper reconstruction path. */
    float scale = GfxQuality_Get() >= GFX_HIGH ? 1.0f :
                  (GfxQuality_Get() >= GFX_MED ? 0.75f : 0.50f);
    int w = (int)(width*scale), h=(int)(height*scale);
    s_capture=FluidSurface_LoadDepthTarget(w,h);
    /* LoadRenderTexture defaults to RGBA8. That silently reduced smoothed
     * device depth to 256 levels, turning shallow liquid into zoom-dependent
     * horizontal contour bands. Scalar R32F costs the same four bytes/pixel. */
    s_thickness=FluidSurface_LoadScalarTarget(w,h);
    s_smoothA=FluidSurface_LoadScalarTarget(w,h);
    s_smoothB=FluidSurface_LoadScalarTarget(w,h);
    s_captureShader=ResourceManager_LoadShader(NULL,"core/fluid/shaders/fluid_capture.fs");
    s_thicknessShader=ResourceManager_LoadShader(NULL,"core/fluid/shaders/fluid_surface_thickness_mesh.fs");
    s_smooth=ResourceManager_LoadShader(NULL,"core/fluid/shaders/fluid_depth_narrow_range.fs");
    s_composite=ResourceManager_LoadShader(NULL,"core/fluid/shaders/fluid_surface.fs");
    Image img = GenImageGradientRadial(64, 64, 0.0f, WHITE, BLANK);
    s_surfaceTex = LoadTextureFromImage(img); UnloadImage(img);
    SetTextureFilter(s_surfaceTex, TEXTURE_FILTER_BILINEAR);
    s_texelLoc=GetShaderLocation(s_smooth,"u_texel");
    s_dirLoc=GetShaderLocation(s_smooth,"u_direction");
    s_depthRangeLoc=GetShaderLocation(s_smooth,"u_depthRange");
    s_fillLoc=GetShaderLocation(s_smooth,"u_fillHoles");
    s_smoothProjectionLoc=GetShaderLocation(s_smooth,"u_projection");
    s_smoothInverseProjectionLoc=GetShaderLocation(s_smooth,"u_inverseProjection");
    s_kernelRadiusLoc=GetShaderLocation(s_smooth,"u_kernelRadius");
    s_filterRadiusLoc=GetShaderLocation(s_smooth,"u_filterRadius");
    s_compositeTexelLoc=GetShaderLocation(s_composite,"u_texel");
    s_sceneTexelLoc=GetShaderLocation(s_composite,"u_sceneTexel");
    s_thicknessLoc=GetShaderLocation(s_composite,"u_thicknessTex"); s_sceneLoc=GetShaderLocation(s_composite,"u_sceneTex");
    s_sceneDepthLoc=GetShaderLocation(s_composite,"u_sceneDepthTex"); s_hasDepthLoc=GetShaderLocation(s_composite,"u_hasSceneDepth");
    s_projectionLoc=GetShaderLocation(s_composite,"u_projection");
    s_inverseProjectionLoc=GetShaderLocation(s_composite,"u_inverseProjection");
    s_viewToWorldLoc=GetShaderLocation(s_composite,"u_viewToWorld");
    s_qualityTierLoc=GetShaderLocation(s_composite,"u_qualityTier");
    s_sunDirectionLoc=GetShaderLocation(s_composite,"u_sunDirectionView");
    s_sunColorLoc=GetShaderLocation(s_composite,"u_sunColor");
    s_skyAmbientLoc=GetShaderLocation(s_composite,"u_skyAmbient");
    s_groundAmbientLoc=GetShaderLocation(s_composite,"u_groundAmbient");
    s_pointLightCountLoc=GetShaderLocation(s_composite,"u_pointLightCount");
    s_pointLightPosLoc=GetShaderLocation(s_composite,"u_pointLightPosRadius");
    s_pointLightColorLoc=GetShaderLocation(s_composite,"u_pointLightColor");
    s_materialBodyLoc=GetShaderLocation(s_composite,"u_materialBody");
    s_materialGlowLoc=GetShaderLocation(s_composite,"u_materialGlow");
    s_materialSoftLoc=GetShaderLocation(s_composite,"u_materialSoft");
    s_timeLoc=GetShaderLocation(s_composite,"u_time");
}
void FluidSurface_Unload(void) { UnloadTexture(s_surfaceTex); UnloadRenderTexture(s_capture); UnloadRenderTexture(s_thickness); UnloadRenderTexture(s_smoothA); UnloadRenderTexture(s_smoothB); if(s_sceneCopy.id) { UnloadRenderTexture(s_sceneCopy); s_sceneCopy=(RenderTexture2D){0}; } }
void FluidSurface_SetMaterialColors(Color body, Color glow, Color soft) {
    s_materialBody=body;
    s_materialGlow=glow;
    s_materialSoft=soft;
}
void FluidSurface_SetReconstructionRadius(float radius) {
    if(radius>0.0001f) s_reconstructionRadius=radius;
}
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
    /* Snapshot the scene for the refraction tap while it is still only a source.
     * The composite runs inside ScreenDistort's body pass, which now binds the
     * scene target itself, so sampling it there would be sampling the attachment
     * being written. Keep the copy in the scene's own format so HDR survives. */
    Texture2D liveScene=ScreenDistort_GetSceneTexture();
    if(liveScene.id) {
        if(s_sceneCopy.id==0 || s_sceneCopy.texture.width!=liveScene.width ||
           s_sceneCopy.texture.height!=liveScene.height ||
           s_sceneCopy.texture.format!=liveScene.format) {
            if(s_sceneCopy.id) UnloadRenderTexture(s_sceneCopy);
            s_sceneCopy=FluidSurface_LoadColorTarget(liveScene.width,liveScene.height,
                                                     liveScene.format);
        }
        if(s_sceneCopy.id) {
            BeginTextureMode(s_sceneCopy);
            /* An exact copy: blending would fold the scene's own alpha into it,
             * and the negative source height is this file's RT->RT convention,
             * which leaves storage orientation identical to the source. */
            rlDisableColorBlend();
            DrawTextureRec(liveScene,
                           (Rectangle){0,0,(float)liveScene.width,-(float)liveScene.height},
                           (Vector2){0,0},WHITE);
            rlDrawRenderBatchActive();
            rlEnableColorBlend();
            EndTextureMode();
        }
    }
    s_fluidView=MatrixLookAt(camera.position,camera.target,camera.up);
    s_fluidProjection=FluidSurface_MakeProjection(camera);
    BeginTextureMode(s_capture); ClearBackground((Color){255,0,0,0}); BeginMode3D(camera);
    for (int i=0;i<s_gpuStreamCount;i++) ParticleManager_DrawSurfaceStream(&s_gpuStreams[i], camera, s_surfaceTex);
    FluidPBDGPU_DrawSurfaceDepth(camera);
    /* --- CPU particles registered via FluidSurface_RegisterParticle --- */
    /* These were previously stored but never rasterised into the FBO.    */
    if (s_count > 0) {
        /* Sphere impostors: draw screen-aligned quads per particle.       */
        /* The default fluid_capture.fs outputs gl_FragCoord.z which is   */
        /* the correct depth for geometry pushed through BeginMode3D,      */
        /* but we need the sphere front surface, not the flat quad.        */
        /* Use DrawSphereEx to rasterise correct sphere geometry per hemi. */
        /* Lower-LOD sphere is fast enough for O(100s) CPU particles.      */
        BeginShaderMode(s_captureShader);
        for (int i = 0; i < s_count; i++) {
            FluidSurfaceParticle *sp = &s_particles[i];
            float r = (sp->radii.x + sp->radii.y + sp->radii.z) / 3.0f;
            DrawSphereEx(sp->position, r, 6, 6, WHITE);
        }
        EndShaderMode();
    }
    EndMode3D(); EndTextureMode();
    BeginTextureMode(s_thickness); ClearBackground(BLANK); BeginMode3D(camera);
    rlDrawRenderBatchActive(); rlDisableDepthMask(); rlDisableDepthTest(); BeginBlendMode(BLEND_ADDITIVE);
    for (int i=0;i<s_gpuStreamCount;i++) ParticleManager_DrawSurfaceThicknessStream(&s_gpuStreams[i], camera);
    FluidPBDGPU_DrawSurfaceThickness(camera);
    if (s_count > 0) {
        BeginShaderMode(s_thicknessShader);
        for (int i = 0; i < s_count; i++) {
            FluidSurfaceParticle *sp = &s_particles[i];
            float r = (sp->radii.x + sp->radii.y + sp->radii.z) / 3.0f;
            DrawSphereEx(sp->position, r, 6, 6, WHITE);
        }
        EndShaderMode();
    }
    EndBlendMode(); rlDrawRenderBatchActive(); rlEnableDepthTest(); rlEnableDepthMask(); EndMode3D(); EndTextureMode();
    Vector2 texel={1.0f/s_capture.texture.width,1.0f/s_capture.texture.height};
    float depthRange=s_reconstructionRadius*9.0f;
    int filterRadius=GfxQuality_Get()>=GFX_HIGH?10:
                     (GfxQuality_Get()>=GFX_MED?5:3);
    Matrix inverseProjection=MatrixInvert(s_fluidProjection);
    /* HIGH spends four separable rounds to fully merge dense orb particles.
     * MED/LOW keep fewer rounds for splash and detached spray. */
    int reconstructionRounds=GfxQuality_Get()>=GFX_HIGH?4:
                             (GfxQuality_Get()>=GFX_MED?2:1);
    for (int iteration=0; iteration<reconstructionRounds; ++iteration) {
        Vector2 horizontal={1.0f,0.0f}, vertical={0.0f,1.0f};
        Texture2D source = iteration ? s_smoothB.texture : s_capture.texture;
        int fillHoles=iteration==0;
        BeginTextureMode(s_smoothA); ClearBackground(WHITE); BeginShaderMode(s_smooth); SetShaderValue(s_smooth,s_texelLoc,&texel,SHADER_UNIFORM_VEC2); SetShaderValue(s_smooth,s_dirLoc,&horizontal,SHADER_UNIFORM_VEC2); SetShaderValue(s_smooth,s_depthRangeLoc,&depthRange,SHADER_UNIFORM_FLOAT); SetShaderValue(s_smooth,s_kernelRadiusLoc,&s_reconstructionRadius,SHADER_UNIFORM_FLOAT); SetShaderValue(s_smooth,s_filterRadiusLoc,&filterRadius,SHADER_UNIFORM_INT); SetShaderValueMatrix(s_smooth,s_smoothProjectionLoc,s_fluidProjection); SetShaderValueMatrix(s_smooth,s_smoothInverseProjectionLoc,inverseProjection); SetShaderValue(s_smooth,s_fillLoc,&fillHoles,SHADER_UNIFORM_INT); DrawTextureRec(source,(Rectangle){0,0,source.width,-source.height},(Vector2){0,0},WHITE); EndShaderMode(); EndTextureMode();
        fillHoles=0;
        BeginTextureMode(s_smoothB); ClearBackground(WHITE); BeginShaderMode(s_smooth); SetShaderValue(s_smooth,s_dirLoc,&vertical,SHADER_UNIFORM_VEC2); SetShaderValue(s_smooth,s_fillLoc,&fillHoles,SHADER_UNIFORM_INT); DrawTextureRec(s_smoothA.texture,(Rectangle){0,0,s_smoothA.texture.width,-s_smoothA.texture.height},(Vector2){0,0},WHITE); EndShaderMode(); EndTextureMode();
    }
}
void FluidSurface_Composite(void) {
    if(!FluidSurface_HasPending()) return;
    Vector2 texel={1.0f/s_smoothB.texture.width,1.0f/s_smoothB.texture.height};
    Vector2 sceneTexel={1.0f/GetRenderWidth(),1.0f/GetRenderHeight()};
    /* The snapshot from Capture, never the live scene target: the body pass we are
     * drawing inside binds that same texture as its colour attachment. */
    Texture2D scene=s_sceneCopy.id?s_sceneCopy.texture:ScreenDistort_GetSceneTexture();
    Texture2D sceneDepth=ScreenDistort_GetRawDepthTexture(); int has=sceneDepth.id?1:0;
    Matrix inverseProjection=MatrixInvert(s_fluidProjection);
    Matrix viewToWorld=MatrixInvert(s_fluidView);
    int qualityTier=(int)GfxQuality_Get();

    Vector3 sunToLight=Vector3Normalize(Vector3Negate(Environment_GetSunDirection()));
    Vector3 sunDirectionView=Vector3Normalize(
        FluidSurface_TransformDirection(sunToLight,s_fluidView));
    Vector3 sunColor=FluidSurface_ColorToVec3(Environment_GetSunColor());
    Vector3 skyAmbient=FluidSurface_ColorToVec3(Environment_GetSkyAmbient());
    Vector3 groundAmbient=FluidSurface_ColorToVec3(Environment_GetGroundAmbient());
    Vector3 materialBody=FluidSurface_ColorToVec3(s_materialBody);
    Vector3 materialGlow=FluidSurface_ColorToVec3(s_materialGlow);
    Vector3 materialSoft=FluidSurface_ColorToVec3(s_materialSoft);
    float opticalTime=(float)GetTime();

    int lightBudget=qualityTier>=GFX_HIGH?FLUID_OPTICAL_POINT_LIGHTS:
                    (qualityTier>=GFX_MED?2:0);
    VFXLightData lights[FLUID_OPTICAL_POINT_LIGHTS];
    int pointLightCount=0;
    if (lightBudget>0) VFXLight_GetActive(lights,&pointLightCount,lightBudget);
    if (pointLightCount>lightBudget) pointLightCount=lightBudget;
    float pointLightPos[FLUID_OPTICAL_POINT_LIGHTS*4]={0};
    float pointLightColor[FLUID_OPTICAL_POINT_LIGHTS*4]={0};
    for (int i=0;i<pointLightCount;i++) {
        Vector3 viewPosition=Vector3Transform(lights[i].position,s_fluidView);
        float intensity=lights[i].color.a/255.0f;
        pointLightPos[i*4+0]=viewPosition.x;
        pointLightPos[i*4+1]=viewPosition.y;
        pointLightPos[i*4+2]=viewPosition.z;
        pointLightPos[i*4+3]=lights[i].radius;
        pointLightColor[i*4+0]=lights[i].color.r/255.0f*intensity;
        pointLightColor[i*4+1]=lights[i].color.g/255.0f*intensity;
        pointLightColor[i*4+2]=lights[i].color.b/255.0f*intensity;
        pointLightColor[i*4+3]=1.0f;
    }

    BeginBlendMode(BLEND_ALPHA);
    BeginShaderMode(s_composite);
    SetShaderValue(s_composite,s_compositeTexelLoc,&texel,SHADER_UNIFORM_VEC2);
    SetShaderValue(s_composite,s_sceneTexelLoc,&sceneTexel,SHADER_UNIFORM_VEC2);
    SetShaderValue(s_composite,s_hasDepthLoc,&has,SHADER_UNIFORM_INT);
    SetShaderValueMatrix(s_composite,s_projectionLoc,s_fluidProjection);
    SetShaderValueMatrix(s_composite,s_inverseProjectionLoc,inverseProjection);
    SetShaderValueMatrix(s_composite,s_viewToWorldLoc,viewToWorld);
    SetShaderValue(s_composite,s_qualityTierLoc,&qualityTier,SHADER_UNIFORM_INT);
    SetShaderValue(s_composite,s_sunDirectionLoc,&sunDirectionView,SHADER_UNIFORM_VEC3);
    SetShaderValue(s_composite,s_sunColorLoc,&sunColor,SHADER_UNIFORM_VEC3);
    SetShaderValue(s_composite,s_skyAmbientLoc,&skyAmbient,SHADER_UNIFORM_VEC3);
    SetShaderValue(s_composite,s_groundAmbientLoc,&groundAmbient,SHADER_UNIFORM_VEC3);
    SetShaderValue(s_composite,s_materialBodyLoc,&materialBody,SHADER_UNIFORM_VEC3);
    SetShaderValue(s_composite,s_materialGlowLoc,&materialGlow,SHADER_UNIFORM_VEC3);
    SetShaderValue(s_composite,s_materialSoftLoc,&materialSoft,SHADER_UNIFORM_VEC3);
    SetShaderValue(s_composite,s_timeLoc,&opticalTime,SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_composite,s_pointLightCountLoc,&pointLightCount,SHADER_UNIFORM_INT);
    SetShaderValueV(s_composite,s_pointLightPosLoc,pointLightPos,
                    SHADER_UNIFORM_VEC4,FLUID_OPTICAL_POINT_LIGHTS);
    SetShaderValueV(s_composite,s_pointLightColorLoc,pointLightColor,
                    SHADER_UNIFORM_VEC4,FLUID_OPTICAL_POINT_LIGHTS);
    /* Raylib owns the sampler slots.  Manual rlActiveTextureSlot bindings are
     * not reliably reflected into Shader objects on every backend. */
    SetShaderValueTexture(s_composite,s_thicknessLoc,s_thickness.texture);
    SetShaderValueTexture(s_composite,s_sceneLoc,scene);
    if(has) SetShaderValueTexture(s_composite,s_sceneDepthLoc,sceneDepth);
    DrawTexturePro(s_smoothB.texture,(Rectangle){0,0,s_smoothB.texture.width,-s_smoothB.texture.height},(Rectangle){0,0,GetRenderWidth(),GetRenderHeight()},(Vector2){0,0},0,WHITE);
    EndShaderMode();
    EndBlendMode();
    s_count=0;
    s_gpuStreamCount=0;
}
