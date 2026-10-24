#include "core/liquid/liquid_surface.h"
#include "core/time_fx.h"   // TimeFX_IsDeterministic — capture must not be load-shed by wall clock
#include "core/resource_manager.h"
#include "core/scene_targets.h"
#include "core/screen_distort.h"
#include "core/particles/particle_manager.h"
#include "core/liquid/liquid_pbd_gpu.h"
#include "core/liquid/liquid_capture_cpu.h"
#include "core/liquid/liquid_capture_budget.h"
#include "core/particles/gpu/particle_gpu_legacy.h"
#include "core/gfx_quality.h"
#include "core/presets/vc_material.h"
#include "core/vfx_light.h"
#include "environment/environment_system.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>

#define LIQUID_OPTICAL_POINT_LIGHTS 4
#define LIQUID_MAX_FRAME_REGIONS 8

typedef LiquidCaptureCPUInstance LiquidSurfaceParticle;
static LiquidSurfaceParticle s_particles[LIQUID_SURFACE_MAX_PARTICLES];
static int s_count;
static ParticleRenderStream s_gpuStreams[16];
static int s_gpuStreamMaterial[16];
static int s_gpuStreamCount;
static ParticleRenderStream s_cpuStreams[32];
static int s_cpuStreamMaterial[32], s_cpuStreamCount;
static float s_cpuStreamKernel[32];
static LiquidSurfaceStats s_stats;
static int s_streamRejected, s_directRequested;
static bool s_captureBatch=true, s_cpuImpostor=true, s_profile;
/* Set when a liquid shader compiled to the default fallback: every uniform write
 * then no-ops and the surface is not the surface. See LiquidSurface_Init. */
static bool s_shadersBroken;
static unsigned int s_profileFrames;
static RenderTexture2D s_occupancy;
static Shader s_occupancyShader;
static int s_occupancyLoc, s_useOccupancyLoc;
static bool s_useOccupancy=true;
LiquidSurfaceStats LiquidSurface_GetStats(void) { return s_stats; }
static double LiquidSurface_HostClock(void) { return s_profile?GetTime():0.0; }
static double LiquidSurface_HostElapsed(double start) { return s_profile?(GetTime()-start)*1000.0:0.0; }

/* --- The liquid table ---------------------------------------------------- */
static LiquidDesc s_materials[LIQUID_SURFACE_MATERIAL_SLOTS];
static unsigned int s_materialUse[LIQUID_SURFACE_MATERIAL_SLOTS]; /* recency stamp */
static unsigned int s_materialClock = 1;
static int s_materialCount;
static int s_currentMaterial;
static Texture2D s_surfaceTex;
static RenderTexture2D s_capture, s_captureBack, s_thickness, s_thicknessScratch, s_smoothA, s_smoothB;
static RenderTexture2D s_sceneCopy;   // refraction source, see LiquidSurface_LoadColorTarget
static Shader s_captureShader, s_captureBackShader, s_smooth, s_composite;
static Shader s_thicknessResolve, s_thicknessBlur;
static int s_resolveBackLoc, s_resolveInverseProjectionLoc;
static int s_blurTexelLoc, s_blurDirectionLoc, s_blurRadiusLoc;
static int s_texelLoc, s_dirLoc, s_fillLoc;
static int s_smoothProjectionLoc, s_smoothInverseProjectionLoc;
static int s_kernelRadiusLoc, s_filterRadiusLoc, s_filter2DLoc;
static int s_compositeTexelLoc, s_sceneTexelLoc, s_thicknessLoc, s_sceneLoc, s_sceneDepthLoc, s_hasDepthLoc;
static int s_materialIdTexLoc;
static int s_projectionLoc, s_inverseProjectionLoc, s_viewToWorldLoc, s_qualityTierLoc;
static int s_sunDirectionLoc, s_sunColorLoc, s_skyAmbientLoc, s_groundAmbientLoc;
static int s_pointLightCountLoc, s_pointLightPosLoc, s_pointLightColorLoc;
static int s_materialBodyLoc, s_materialGlowLoc, s_materialSoftLoc, s_materialOpticsLoc;
static int s_timeLoc;
static int s_compositeKernelRadiusLoc;
static Matrix s_fluidView, s_fluidProjection;
static Color s_materialBody = {41, 128, 185, 255};
static Color s_materialGlow = {80, 180, 255, 255};
static Color s_materialSoft = {160, 225, 255, 255};
static float s_reconstructionRadius = 0.022f;
static float s_submissionKernelRadius = 0.022f;
static float s_materialKernel[LIQUID_SURFACE_MATERIAL_SLOTS];
static int s_smoothMaterialLoc, s_smoothKernelsLoc, s_smoothMultiLoc, s_smoothSourceMaterialLoc;
static int s_compositeKernelsLoc;
static void LiquidSurface_RecordKernel(int slot) {
    if(slot>=0 && slot<LIQUID_SURFACE_MATERIAL_SLOTS)
        s_materialKernel[slot]=fmaxf(s_materialKernel[slot],s_submissionKernelRadius);
}
static float s_frameBodyRadiusPx = -1.0f;
static float s_frameKernelRadiusPx = -1.0f;

/* Multi-region world-space bounds for 5-6 simultaneous liquid VFX.
 * Keeps disjoint bodies in separate screen scissor rects instead of bloating
 * a single union box across the entire screen. */
typedef struct {
    Vector3 min;
    Vector3 max;
} LiquidWorldRegion;

static LiquidWorldRegion s_frameRegions[LIQUID_MAX_FRAME_REGIONS];
static int s_frameRegionCount = 0;
static bool s_frameBoundsValid;
static Vector3 s_frameBoundsMin, s_frameBoundsMax;

static int s_reconstructionRoundOverride;
static unsigned int s_reconstructionABSeed=0x5f3759dfu;
static int s_reconstructionABFrame, s_reconstructionABPrevRounds;
static double s_reconstructionABMs[3];
static int s_reconstructionABCount[3];

/* --- Cost gates (LiquidSurface_RequestBody) ------------------------------- */
static Camera3D s_lastCamera;          /* previous frame's, from Capture */
static bool     s_hasCamera;
static double   s_surfaceRunStamp = -1000.0;
static double   s_radiusOwnerStamp = -1000.0;
static int      s_frameOwnerPriority = -1;
#define LIQUID_GATE_STAMP_TTL 0.10

static Vector3 LiquidSurface_ColorToVec3(Color color) {
    return (Vector3){
        color.r / 255.0f,
        color.g / 255.0f,
        color.b / 255.0f
    };
}

static Vector3 LiquidSurface_TransformDirection(Vector3 direction, Matrix matrix) {
    return (Vector3){
        matrix.m0*direction.x + matrix.m4*direction.y + matrix.m8*direction.z,
        matrix.m1*direction.x + matrix.m5*direction.y + matrix.m9*direction.z,
        matrix.m2*direction.x + matrix.m6*direction.y + matrix.m10*direction.z
    };
}

/* Match main.c::MyBeginMode3D exactly. Fluid and raw scene depth are only
 * comparable when both were produced by the same projection. */
static Matrix LiquidSurface_MakeProjection(Camera3D camera) {
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

static void LiquidSurface_BeginCaptureMode3D(Camera3D camera) {
    rlDrawRenderBatchActive();
    rlMatrixMode(RL_PROJECTION);
    rlPushMatrix();
    rlLoadIdentity();
    float aspect = (float)GetScreenWidth()/(float)GetScreenHeight();
    if (camera.projection == CAMERA_ORTHOGRAPHIC) {
        double top = camera.fovy*0.5;
        double right = top*aspect;
        rlOrtho(-right, right, -top, top, 0.0001, 150.0);
    } else {
        double top = tan(camera.fovy*0.5*DEG2RAD);
        double right = top*aspect;
        rlFrustum(-right, right, -top, top, 1.0, 1000.0);
    }
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
    rlMultMatrixf(MatrixToFloat(s_fluidView));
    rlEnableDepthTest();
}

static void LiquidSurface_EndCaptureMode3D(void) {
    rlDrawRenderBatchActive();
    rlMatrixMode(RL_PROJECTION);
    rlPopMatrix();
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
    rlDisableDepthTest();
}

static RenderTexture2D LiquidSurface_LoadDepthTargetEx(int w, int h, bool carriesMaterial) {
    RenderTexture2D t = {0}; t.id = rlLoadFramebuffer();
    if (!t.id) return t;
    rlEnableFramebuffer(t.id);
    int format = carriesMaterial ? RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32
                                 : RL_PIXELFORMAT_UNCOMPRESSED_R32;
    t.texture.id = rlLoadTexture(NULL,w,h,format,1);
    t.texture.width=w; t.texture.height=h; t.texture.format=format; t.texture.mipmaps=1;
    t.depth.id=rlLoadTextureDepth(w,h,false); t.depth.width=w; t.depth.height=h; t.depth.mipmaps=1;
    rlFramebufferAttach(t.id,t.texture.id,RL_ATTACHMENT_COLOR_CHANNEL0,RL_ATTACHMENT_TEXTURE2D,0);
    rlFramebufferAttach(t.id,t.depth.id,RL_ATTACHMENT_DEPTH,RL_ATTACHMENT_TEXTURE2D,0);
    if (!rlFramebufferComplete(t.id)) TraceLog(LOG_WARNING,"LiquidSurface: depth target incomplete");
    rlDisableFramebuffer();
    SetTextureFilter(t.texture, carriesMaterial ? TEXTURE_FILTER_POINT
                                                : TEXTURE_FILTER_BILINEAR);
    return t;
}

static RenderTexture2D LiquidSurface_LoadColorTarget(int w, int h, int format) {
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
        TraceLog(LOG_WARNING, "LiquidSurface: scene copy target incomplete");
    rlDisableFramebuffer();
    SetTextureFilter(t.texture, TEXTURE_FILTER_BILINEAR);
    return t;
}

static RenderTexture2D LiquidSurface_LoadScalarTarget(int w, int h) {
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
        TraceLog(LOG_WARNING,"LiquidSurface: scalar target incomplete");
    rlDisableFramebuffer();
    SetTextureFilter(t.texture,TEXTURE_FILTER_BILINEAR);
    return t;
}

static void LiquidSurface_DrawEllipsoid(const LiquidSurfaceParticle *sp) {
    rlPushMatrix();
    rlLoadIdentity();
    rlTranslatef(sp->position.x, sp->position.y, sp->position.z);
    rlScalef(sp->radii.x, sp->radii.y, sp->radii.z);
    DrawSphereEx((Vector3){0.0f, 0.0f, 0.0f}, 1.0f, 6, 6, WHITE);
    rlPopMatrix();
}

static float s_captureScaleOverride;
static float LiquidSurface_TargetScale(void) {
    float scale=GfxQuality_Get() >= GFX_HIGH ? 0.85f :
                (GfxQuality_Get() >= GFX_MED ? 0.75f : 0.50f);
    if(s_captureScaleOverride>0.0f) scale=s_captureScaleOverride;
    if(scale<0.25f) scale=0.25f;
    if(scale>1.0f) scale=1.0f;
    return scale;
}

static void LiquidSurface_CreateTargets(int baseWidth,int baseHeight) {
    float scale=LiquidSurface_TargetScale();
    int w=(int)(baseWidth*scale), h=(int)(baseHeight*scale);
    if(w<1) w=1;
    if(h<1) h=1;
    if(s_capture.id) {
        UnloadRenderTexture(s_capture);
        UnloadRenderTexture(s_captureBack);
        UnloadRenderTexture(s_thickness);
        UnloadRenderTexture(s_thicknessScratch);
        UnloadRenderTexture(s_smoothA);
        UnloadRenderTexture(s_smoothB);
        UnloadRenderTexture(s_occupancy);
        s_capture=(RenderTexture2D){0}; s_captureBack=(RenderTexture2D){0};
        s_thickness=(RenderTexture2D){0}; s_thicknessScratch=(RenderTexture2D){0};
        s_smoothA=(RenderTexture2D){0}; s_smoothB=(RenderTexture2D){0};
        s_occupancy=(RenderTexture2D){0};
    }
    s_capture=LiquidSurface_LoadDepthTargetEx(w,h,true);
    s_captureBack=LiquidSurface_LoadDepthTargetEx(w,h,false);
    int tw = w/2 > 1 ? w/2 : 1, th = h/2 > 1 ? h/2 : 1;
    s_thickness=LiquidSurface_LoadScalarTarget(tw,th);
    s_thicknessScratch=LiquidSurface_LoadScalarTarget(tw,th);
    s_smoothA=LiquidSurface_LoadScalarTarget(w,h);
    s_smoothB=LiquidSurface_LoadScalarTarget(w,h);
    s_occupancy=LiquidSurface_LoadScalarTarget((w+7)/8,(h+7)/8);
    SetTextureFilter(s_occupancy.texture,TEXTURE_FILTER_POINT);
}

static void LiquidSurface_SyncTargets(void) {
    int baseWidth=SceneTargets_GetWidth(), baseHeight=SceneTargets_GetHeight();
    if(baseWidth<=0 || baseHeight<=0) { baseWidth=GetScreenWidth(); baseHeight=GetScreenHeight(); }
    if(baseWidth<1) baseWidth=1;
    if(baseHeight<1) baseHeight=1;
    float scale=LiquidSurface_TargetScale();
    int w=(int)(baseWidth*scale), h=(int)(baseHeight*scale);
    if(w<1) w=1;
    if(h<1) h=1;
    if(s_capture.id && s_capture.texture.width==w && s_capture.texture.height==h) return;
    LiquidSurface_CreateTargets(baseWidth,baseHeight);
}

void LiquidSurface_Init(int width,int height) {
    const char *batch=getenv("WUXING_LIQUID_CAPTURE_BATCH");
    const char *cpu=getenv("WUXING_LIQUID_CPU_IMPOSTOR");
    const char *roi=getenv("WUXING_LIQUID_OCCUPANCY");
    s_captureBatch=!batch || atoi(batch)!=0;
    s_cpuImpostor=(!cpu || atoi(cpu)!=0) && LiquidCaptureCPU_Init();
    /* Enable 8x8 tile occupancy culling by default unless explicitly disabled via WUXING_LIQUID_OCCUPANCY=0 */
    s_useOccupancy=!roi || atoi(roi)!=0;
    const char *profile=getenv("WUXING_LIQUID_PROFILE");
    s_profile=profile && atoi(profile)!=0;
    const char *capScale=getenv("WUXING_LIQUID_CAPTURE_SCALE");
    if(capScale && *capScale) s_captureScaleOverride=(float)atof(capScale);
    const char *roundOverride=getenv("WUXING_LIQUID_RECON_ROUNDS");
    if (!roundOverride) roundOverride=getenv("WUXING_FLUID_RECON_ROUNDS");
    s_reconstructionRoundOverride=roundOverride?atoi(roundOverride):0;
    if (s_reconstructionRoundOverride<1 || s_reconstructionRoundOverride>3)
        s_reconstructionRoundOverride=0;
    LiquidSurface_CreateTargets(width,height);
    s_occupancyShader=ResourceManager_LoadShader(NULL,"core/liquid/shaders/liquid_occupancy.fs");
    if(s_occupancyShader.id==rlGetShaderIdDefault()) s_useOccupancy=false;
    s_captureShader=ResourceManager_LoadShader(NULL,"core/liquid/shaders/liquid_capture.fs");
    s_captureBackShader=ResourceManager_LoadShader(NULL,"core/liquid/shaders/liquid_capture_back.fs");
    s_thicknessResolve=ResourceManager_LoadShader(NULL,"core/liquid/shaders/liquid_thickness_resolve.fs");
    s_thicknessBlur=ResourceManager_LoadShader(NULL,"core/liquid/shaders/liquid_thickness_blur.fs");
    s_smooth=ResourceManager_LoadShader(NULL,"core/liquid/shaders/liquid_depth_narrow_range.fs");
    s_composite=ResourceManager_LoadShader(NULL,"core/liquid/shaders/liquid_surface.fs");
    if(s_smooth.id==rlGetShaderIdDefault() || s_composite.id==rlGetShaderIdDefault() ||
       s_thicknessResolve.id==rlGetShaderIdDefault() || s_thicknessBlur.id==rlGetShaderIdDefault())
    {
        TraceLog(LOG_ERROR,"LIQUID: surface shader failed to compile — screen-space fluid disabled");
        s_shadersBroken=true;
    }
    Image img = GenImageGradientRadial(64, 64, 0.0f, WHITE, BLANK);
    s_surfaceTex = LoadTextureFromImage(img); UnloadImage(img);
    SetTextureFilter(s_surfaceTex, TEXTURE_FILTER_BILINEAR);
    s_texelLoc=GetShaderLocation(s_smooth,"u_texel");
    s_dirLoc=GetShaderLocation(s_smooth,"u_direction");
    s_fillLoc=GetShaderLocation(s_smooth,"u_fillHoles");
    s_occupancyLoc=GetShaderLocation(s_smooth,"u_occupancyTex");
    s_useOccupancyLoc=GetShaderLocation(s_smooth,"u_useOccupancy");
    s_smoothProjectionLoc=GetShaderLocation(s_smooth,"u_projection");
    s_smoothInverseProjectionLoc=GetShaderLocation(s_smooth,"u_inverseProjection");
    s_kernelRadiusLoc=GetShaderLocation(s_smooth,"u_kernelRadius");
    s_smoothMaterialLoc=GetShaderLocation(s_smooth,"u_materialIdTex");
    s_smoothKernelsLoc=GetShaderLocation(s_smooth,"u_materialKernels");
    s_smoothMultiLoc=GetShaderLocation(s_smooth,"u_multipleMaterials");
    s_smoothSourceMaterialLoc=GetShaderLocation(s_smooth,"u_sourceCarriesMaterial");
    s_compositeKernelsLoc=GetShaderLocation(s_composite,"u_materialKernels");
    s_filterRadiusLoc=GetShaderLocation(s_smooth,"u_filterRadius");
    s_filter2DLoc=GetShaderLocation(s_smooth,"u_filter2D");
    s_resolveBackLoc=GetShaderLocation(s_thicknessResolve,"u_backDepthTex");
    s_resolveInverseProjectionLoc=GetShaderLocation(s_thicknessResolve,"u_inverseProjection");
    s_blurTexelLoc=GetShaderLocation(s_thicknessBlur,"u_texel");
    s_blurDirectionLoc=GetShaderLocation(s_thicknessBlur,"u_direction");
    s_blurRadiusLoc=GetShaderLocation(s_thicknessBlur,"u_radius");
    s_compositeTexelLoc=GetShaderLocation(s_composite,"u_texel");
    s_sceneTexelLoc=GetShaderLocation(s_composite,"u_sceneTexel");
    s_thicknessLoc=GetShaderLocation(s_composite,"u_thicknessTex"); s_sceneLoc=GetShaderLocation(s_composite,"u_sceneTex");
    s_sceneDepthLoc=GetShaderLocation(s_composite,"u_sceneDepthTex"); s_hasDepthLoc=GetShaderLocation(s_composite,"u_hasSceneDepth");
    s_materialIdTexLoc=GetShaderLocation(s_composite,"u_materialIdTex");
    s_projectionLoc=GetShaderLocation(s_composite,"u_projection");
    s_inverseProjectionLoc=GetShaderLocation(s_composite,"u_inverseProjection");
    s_viewToWorldLoc=GetShaderLocation(s_composite,"u_viewToWorld");
    s_qualityTierLoc=GetShaderLocation(s_composite,"u_qualityTier");
    s_compositeKernelRadiusLoc=GetShaderLocation(s_composite,"u_kernelRadius");
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
    s_materialOpticsLoc=GetShaderLocation(s_composite,"u_materialOptics");
    s_timeLoc=GetShaderLocation(s_composite,"u_time");
    {
        LiquidDesc water=LiquidSurface_DielectricDesc(s_materialBody,s_materialGlow,s_materialSoft);
        for (int i=0;i<LIQUID_SURFACE_MATERIAL_SLOTS;i++) { s_materials[i]=water; s_materialUse[i]=0; }
        s_materialCount=1; s_currentMaterial=0; s_materialUse[0]=++s_materialClock;
    }
}

void LiquidSurface_Unload(void) {
    LiquidCaptureCPU_Unload();
    UnloadRenderTexture(s_occupancy);
    UnloadTexture(s_surfaceTex);
    UnloadRenderTexture(s_capture);
    UnloadRenderTexture(s_captureBack);
    UnloadRenderTexture(s_thickness);
    UnloadRenderTexture(s_thicknessScratch);
    UnloadRenderTexture(s_smoothA);
    UnloadRenderTexture(s_smoothB);
    if(s_sceneCopy.id) { UnloadRenderTexture(s_sceneCopy); s_sceneCopy=(RenderTexture2D){0}; }
}

static float LiquidSurface_ProjectedRadiusPx(Vector3 center, float worldRadius) {
    if (!s_hasCamera) return 1e9f;
    Vector3 toBody = Vector3Subtract(center, s_lastCamera.position);
    float distance = Vector3Length(toBody);
    if (s_lastCamera.projection == CAMERA_ORTHOGRAPHIC) {
        float halfHeight = s_lastCamera.fovy * 0.5f;
        if (halfHeight <= 0.0001f) return 1e9f;
        return worldRadius / halfHeight * (float)GetScreenHeight() * 0.5f;
    }
    if (distance <= worldRadius) return 1e9f;
    float halfFovTan = tanf(s_lastCamera.fovy * 0.5f * DEG2RAD);
    if (halfFovTan <= 0.0001f) return 1e9f;
    return worldRadius / (distance * halfFovTan) * (float)GetScreenHeight() * 0.5f;
}

bool LiquidSurface_RequestBody(LiquidSurfacePriority priority, Vector3 center,
                              float worldRadius, bool alreadyRunning) {
    if (priority <= LIQUID_PRIORITY_MINION) return false;
    if (!alreadyRunning && !TimeFX_IsDeterministic()
        && GetFrameTime()*1000.0f > LIQUID_SURFACE_BUDGET_MS
        && priority < LIQUID_PRIORITY_ULTIMATE)
        return false;
    if (priority <= LIQUID_PRIORITY_BASIC &&
        GetTime() - s_surfaceRunStamp > LIQUID_GATE_STAMP_TTL) return false;

    float admitRadius = alreadyRunning ? LIQUID_SURFACE_MIN_PROJECTED_RADIUS_PX * 0.6f
                                       : LIQUID_SURFACE_MIN_PROJECTED_RADIUS_PX;
    if (LiquidSurface_ProjectedRadiusPx(center, worldRadius) < admitRadius) return false;

    if ((int)priority > s_frameOwnerPriority) s_frameOwnerPriority = (int)priority;
    return true;
}

static void LiquidSurface_RecordWorldRegion(Vector3 bMin, Vector3 bMax) {
    if(!s_frameBoundsValid) {
        s_frameBoundsMin = bMin;
        s_frameBoundsMax = bMax;
        s_frameBoundsValid = true;
    } else {
        s_frameBoundsMin = Vector3Min(s_frameBoundsMin, bMin);
        s_frameBoundsMax = Vector3Max(s_frameBoundsMax, bMax);
    }

    const float mergeMargin = 0.45f;
    for (int i = 0; i < s_frameRegionCount; ++i) {
        LiquidWorldRegion *reg = &s_frameRegions[i];
        bool overlapX = (bMin.x <= reg->max.x + mergeMargin) && (bMax.x >= reg->min.x - mergeMargin);
        bool overlapY = (bMin.y <= reg->max.y + mergeMargin) && (bMax.y >= reg->min.y - mergeMargin);
        bool overlapZ = (bMin.z <= reg->max.z + mergeMargin) && (bMax.z >= reg->min.z - mergeMargin);
        if (overlapX && overlapY && overlapZ) {
            reg->min = Vector3Min(reg->min, bMin);
            reg->max = Vector3Max(reg->max, bMax);
            return;
        }
    }
    if (s_frameRegionCount < LIQUID_MAX_FRAME_REGIONS) {
        s_frameRegions[s_frameRegionCount++] = (LiquidWorldRegion){bMin, bMax};
    } else {
        LiquidWorldRegion *last = &s_frameRegions[LIQUID_MAX_FRAME_REGIONS - 1];
        last->min = Vector3Min(last->min, bMin);
        last->max = Vector3Max(last->max, bMax);
    }
}

void LiquidSurface_HintBody(Vector3 center, float worldRadius) {
    if (worldRadius <= 0.0f) return;
    float bodyPx=LiquidSurface_ProjectedRadiusPx(center,worldRadius);
    float kernelPx=LiquidSurface_ProjectedRadiusPx(center,s_reconstructionRadius);
    if (bodyPx>s_frameBodyRadiusPx) s_frameBodyRadiusPx=bodyPx;
    if (kernelPx>s_frameKernelRadiusPx) s_frameKernelRadiusPx=kernelPx;

    float pad=worldRadius*1.35f+0.35f;
    Vector3 bMin = {center.x-pad, center.y-pad, center.z-pad};
    Vector3 bMax = {center.x+pad, center.y+pad, center.z+pad};
    LiquidSurface_RecordWorldRegion(bMin, bMax);
}

static void LiquidSurface_ExtendFrameBounds(Vector3 position, float radius, bool exact) {
    float pad=exact?radius*1.5f+0.05f:radius*1.35f+0.35f;
    Vector3 bMin = {position.x-pad, position.y-pad, position.z-pad};
    Vector3 bMax = {position.x+pad, position.y+pad, position.z+pad};
    LiquidSurface_RecordWorldRegion(bMin, bMax);
}

LiquidDesc LiquidSurface_DielectricDesc(Color body, Color glow, Color soft) {
    LiquidDesc d = {0};
    d.body=body; d.glow=glow; d.soft=soft;
    d.liquidClass=LIQUID_CLASS_DIELECTRIC;
    d.emission=0.0f;
    d.ior=1.333f;
    d.roughnessScale=1.0f;
    d.opacityPerMetre=0.0f;
    d.foam=1.0f;
    return d;
}

LiquidDesc LiquidSurface_ProfileDesc(LiquidMotionProfile profile) {
    const VFX_ElementMaterial *m;
    LiquidDesc d;
    switch (profile) {
    case LIQUID_MOTION_POISON:
        m=VFX_Material(VC_MAT_POISON);
        (void)m;
        d=LiquidSurface_DielectricDesc((Color){150,215,55,255},
                                     (Color){190,255,45,255},
                                     (Color){225,255,130,255});
        d.ior=1.36f; d.roughnessScale=1.40f; d.opacityPerMetre=1.2f; d.foam=0.35f;
        return d;
    case LIQUID_MOTION_MUD:
        m=VFX_Material(VC_MAT_EARTH);
        d=LiquidSurface_DielectricDesc((Color){96,63,38,255},m->glow,
                                     (Color){130,92,58,255});
        d.ior=1.45f; d.roughnessScale=5.0f; d.opacityPerMetre=32.0f; d.foam=0.0f;
        return d;
    case LIQUID_MOTION_LAVA:
        m=VFX_Material(VC_MAT_FIRE); (void)m;
        d=LiquidSurface_DielectricDesc((Color){105,20,6,255},
                                     (Color){255,190,45,255},
                                     (Color){80,30,12,255});
        d.liquidClass=LIQUID_CLASS_EMISSIVE;
        d.emission=1.35f; d.ior=1.60f; d.roughnessScale=3.8f;
        d.opacityPerMetre=32.0f; d.foam=1.0f;
        return d;
    case LIQUID_MOTION_LIQUID_METAL:
        m=VFX_Material(VC_MAT_METAL); (void)m;
        d=LiquidSurface_DielectricDesc((Color){224,220,212,255},
                                     (Color){238,242,248,255},
                                     (Color){255,250,238,255});
        d.liquidClass=LIQUID_CLASS_CONDUCTOR;
        d.roughnessScale=0.80f; d.opacityPerMetre=60.0f; d.foam=0.0f;
        return d;
    case LIQUID_MOTION_WATER:
    default:
        m=VFX_Material(VC_MAT_WATER);
        return LiquidSurface_DielectricDesc(m->body,m->glow,m->soft);
    }
}

static bool LiquidSurface_DescEqual(const LiquidDesc *a, const LiquidDesc *b) {
    return a->body.r==b->body.r && a->body.g==b->body.g && a->body.b==b->body.b &&
           a->glow.r==b->glow.r && a->glow.g==b->glow.g && a->glow.b==b->glow.b &&
           a->soft.r==b->soft.r && a->soft.g==b->soft.g && a->soft.b==b->soft.b &&
           a->liquidClass==b->liquidClass &&
           a->emission==b->emission && a->ior==b->ior &&
           a->roughnessScale==b->roughnessScale &&
           a->opacityPerMetre==b->opacityPerMetre && a->foam==b->foam;
}

int LiquidSurface_BindMaterial(const LiquidDesc *desc) {
    if (!desc) return s_currentMaterial;
    LiquidDesc d = *desc;
    if (d.ior <= 1.0f) d.ior = 1.333f;
    if (d.roughnessScale <= 0.0f) d.roughnessScale = 1.0f;
    if (d.opacityPerMetre < 0.0f) d.opacityPerMetre = 0.0f;
    for (int i=0;i<s_materialCount;i++) {
        if (LiquidSurface_DescEqual(&s_materials[i], &d)) {
            s_materialUse[i]=++s_materialClock;
            s_currentMaterial=i;
            return i;
        }
    }
    int slot;
    if (s_materialCount < LIQUID_SURFACE_MATERIAL_SLOTS) {
        slot = s_materialCount++;
    } else {
        slot = 0;
        for (int i=1;i<LIQUID_SURFACE_MATERIAL_SLOTS;i++)
            if (s_materialUse[i] < s_materialUse[slot]) slot = i;
    }
    s_materials[slot]=d;
    s_materialUse[slot]=++s_materialClock;
    s_currentMaterial=slot;
    return slot;
}

int LiquidSurface_CurrentMaterial(void) { return s_currentMaterial; }

void LiquidSurface_SetMaterialColors(Color body, Color glow, Color soft) {
    LiquidDesc d = LiquidSurface_DielectricDesc(body, glow, soft);
    LiquidSurface_BindMaterial(&d);
    s_materialBody=body;
    s_materialGlow=glow;
    s_materialSoft=soft;
}

static int s_radiusOwnerPriority = -1;
void LiquidSurface_SetReconstructionRadiusFor(LiquidSurfacePriority priority, float radius) {
    if(radius<=0.0001f) return;
    s_submissionKernelRadius=radius;
    bool ownerCurrent = (GetTime() - s_radiusOwnerStamp) <= LIQUID_GATE_STAMP_TTL;
    if(ownerCurrent && (int)priority < s_radiusOwnerPriority) return;
    s_radiusOwnerPriority=(int)priority;
    s_radiusOwnerStamp=GetTime();
    s_reconstructionRadius=radius;
}

void LiquidSurface_SetReconstructionRadius(float radius) {
    if(radius<=0.0001f) return;
    s_submissionKernelRadius=radius;
    s_radiusOwnerPriority=-1;
    s_radiusOwnerStamp=-1000.0;
    s_reconstructionRadius=radius;
}

void LiquidSurface_RegisterParticle(Vector3 p,float r) { LiquidSurface_RegisterEllipsoid(p,(Vector3){r,r,r}); }
void LiquidSurface_RegisterEllipsoid(Vector3 p,Vector3 radii) {
    LiquidSurface_RecordKernel(s_currentMaterial);
    s_directRequested++;
    float extent=fmaxf(radii.x,fmaxf(radii.y,radii.z));
    LiquidSurface_ExtendFrameBounds(p,extent,true);
    if(s_count<LIQUID_SURFACE_MAX_PARTICLES)
        s_particles[s_count++]=(LiquidSurfaceParticle){p,radii,s_currentMaterial};
}

bool LiquidSurface_SubmitParticleStream(const ParticleRenderStream *stream) {
    if(!stream || stream->mode!=PARTICLE_RENDER_SURFACE_INPUT) return false;
    LiquidSurface_RecordKernel(s_currentMaterial);
    if(stream->backend==PARTICLE_RENDER_BACKEND_CPU) {
        if(s_cpuStreamCount>=32) { s_streamRejected++; return false; }
        s_cpuStreamMaterial[s_cpuStreamCount]=s_currentMaterial;
        s_cpuStreamKernel[s_cpuStreamCount]=s_submissionKernelRadius;
        s_cpuStreams[s_cpuStreamCount++]=*stream;
        return true;
    }
    if(s_gpuStreamCount>=(int)(sizeof(s_gpuStreams)/sizeof(s_gpuStreams[0]))) {
        s_streamRejected++; return false;
    }
    s_gpuStreamMaterial[s_gpuStreamCount]=s_currentMaterial;
    s_gpuStreams[s_gpuStreamCount++]=*stream;
    return true;
}

bool LiquidSurface_HasPending(void) { return !s_shadersBroken && (s_count>0 || s_cpuStreamCount>0 || s_gpuStreamCount>0 || LiquidPBDGPU_IsActive()); }

static void LiquidSurface_PrepareCPUStreams(void) {
    int demand[32], admitted[32];
    s_stats.cpuRequested=s_directRequested;
    for(int i=0;i<s_cpuStreamCount;i++) {
        demand[i]=ParticleManager_CountSurfaceSamples(&s_cpuStreams[i]);
        s_stats.cpuRequested+=demand[i];
    }
    LiquidCaptureBudget_Allocate(demand,s_cpuStreamCount,LIQUID_SURFACE_MAX_PARTICLES-s_count,admitted);
    ParticleSurfaceSample samples[LIQUID_SURFACE_MAX_PARTICLES];
    for(int i=0;i<s_cpuStreamCount;i++) {
        int count=ParticleManager_CopySurfaceSamplesSpaced(&s_cpuStreams[i],samples,admitted[i]);
        float opticalScale=LiquidCaptureBudget_OpticalScale(demand[i],count);
        int slot=s_cpuStreamMaterial[i];
        s_materialKernel[slot]=fmaxf(s_materialKernel[slot],s_cpuStreamKernel[i]*opticalScale);
        for(int j=0;j<count;j++) {
            float r=samples[j].radius*opticalScale;
            LiquidSurface_ExtendFrameBounds(samples[j].position, r, true);
            s_particles[s_count++]=(LiquidSurfaceParticle){samples[j].position,{r,r,r},s_cpuStreamMaterial[i]};
        }
    }
    s_stats.cpuAdmitted=s_count;
    s_stats.cpuDropped=s_stats.cpuRequested-s_count;
}

static void LiquidSurface_TouchLiveMaterials(void) {
    for (int i=0;i<s_gpuStreamCount;i++) {
        int slot=s_gpuStreamMaterial[i];
        if (slot>=0 && slot<LIQUID_SURFACE_MATERIAL_SLOTS) s_materialUse[slot]=++s_materialClock;
    }
    for (int i=0;i<s_count;i++) {
        int slot=s_particles[i].material;
        if (slot>=0 && slot<LIQUID_SURFACE_MATERIAL_SLOTS) s_materialUse[slot]=++s_materialClock;
    }
    if (LiquidPBDGPU_IsActive()) {
        int slot=LiquidPBDGPU_GetMaterial();
        if (slot>=0 && slot<LIQUID_SURFACE_MATERIAL_SLOTS) s_materialUse[slot]=++s_materialClock;
    }
}

static bool LiquidSurface_ProjectWorldBoxToRect(Vector3 bMin, Vector3 bMax,
                                                int targetW, int targetH,
                                                float marginPx, Rectangle *out) {
    Matrix vp = MatrixMultiply(s_fluidView, s_fluidProjection);
    float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
    for (int c = 0; c < 8; ++c) {
        float px = (c & 1) ? bMax.x : bMin.x;
        float py = (c & 2) ? bMax.y : bMin.y;
        float pz = (c & 4) ? bMax.z : bMin.z;
        float cx = vp.m0*px + vp.m4*py + vp.m8*pz + vp.m12;
        float cy = vp.m1*px + vp.m5*py + vp.m9*pz + vp.m13;
        float cw = vp.m3*px + vp.m7*py + vp.m11*pz + vp.m15;
        if (cw <= 0.001f) return false;
        float sx = (cx / cw * 0.5f + 0.5f) * (float)GetScreenWidth();
        float sy = (0.5f - cy / cw * 0.5f) * (float)GetScreenHeight();
        minX = fminf(minX, sx); maxX = fmaxf(maxX, sx);
        minY = fminf(minY, sy); maxY = fmaxf(maxY, sy);
    }
    float scaleX = (float)targetW / (float)fmaxf((float)GetScreenWidth(), 1.0f);
    float scaleY = (float)targetH / (float)fmaxf((float)GetScreenHeight(), 1.0f);
    float x0 = fmaxf(minX * scaleX - marginPx, 0.0f);
    float y0 = fmaxf(minY * scaleY - marginPx, 0.0f);
    float x1 = fminf(maxX * scaleX + marginPx, (float)targetW);
    float y1 = fminf(maxY * scaleY + marginPx, (float)targetH);
    if (x1 - x0 < 1.0f || y1 - y0 < 1.0f) return false;
    *out = (Rectangle){x0, y0, x1 - x0, y1 - y0};
    return true;
}

/* Build merged 2D screen rects for all active liquid bodies.
 * If any body straddles the near plane, returns 0 so caller falls back to full screen. */
static int LiquidSurface_BuildBodyRects(int targetW, int targetH, float marginPx,
                                        Rectangle *outRects, int maxRects) {
    if (!s_frameBoundsValid || s_frameRegionCount <= 0 || targetW <= 0 || targetH <= 0) return 0;
    int count = 0;
    for (int i = 0; i < s_frameRegionCount; ++i) {
        Rectangle r;
        if (!LiquidSurface_ProjectWorldBoxToRect(s_frameRegions[i].min, s_frameRegions[i].max,
                                                 targetW, targetH, marginPx, &r)) {
            return 0; /* straddles near plane: fall back to full screen */
        }
        bool merged = false;
        for (int j = 0; j < count; ++j) {
            Rectangle * dst = &outRects[j];
            bool overlapX = (r.x <= dst->x + dst->width + 4.0f) && (r.x + r.width >= dst->x - 4.0f);
            bool overlapY = (r.y <= dst->y + dst->height + 4.0f) && (r.y + r.height >= dst->y - 4.0f);
            if (overlapX && overlapY) {
                float x0 = fminf(dst->x, r.x);
                float y0 = fminf(dst->y, r.y);
                float x1 = fmaxf(dst->x + dst->width, r.x + r.width);
                float y1 = fmaxf(dst->y + dst->height, r.y + r.height);
                *dst = (Rectangle){x0, y0, x1 - x0, y1 - y0};
                merged = true;
                break;
            }
        }
        if (!merged && count < maxRects) {
            outRects[count++] = r;
        }
    }
    return count;
}

/* FIXED SCISSOR SYMMETRY:
 * Under rlvk with FBO passes or vertical flip mismatches, an asymmetric Y-scissor
 * clips the top/bottom half of the fluid body whenever FBO storage orientation
 * differs from top-down window coordinates. Expanding the scissor to cover both
 * the top-down rect AND its vertical mirror (or using direct sub-rect quads)
 * guarantees the body is NEVER sliced in half while still culling horizontal
 * empty space. Better yet, drawing sub-rect quads directly avoids scissor flip bugs! */
static void LiquidSurface_DrawTargetRects(Texture2D source, int dstW, int dstH,
                                          const Rectangle *rects, int rectCount) {
    /* Always draw full-quad with symmetric vertical scissor bounds so UV interpolation
     * (fragTexCoord 0..1) and FBO orientation remain 100% identical to full-screen mode,
     * while preventing any top/bottom clipping regardless of backend Y-flip. */
    if (!rects || rectCount <= 0) {
        DrawTexturePro(source,
                       (Rectangle){0, 0, (float)source.width, -(float)source.height},
                       (Rectangle){0, 0, (float)dstW, (float)dstH},
                       (Vector2){0, 0}, 0.0f, WHITE);
        return;
    }
    rlEnableScissorTest();
    for (int i = 0; i < rectCount; ++i) {
        int rx = (int)rects[i].x;
        int rw = (int)rects[i].width;
        int ryTop = (int)rects[i].y;
        int ryBot = dstH - (int)(rects[i].y + rects[i].height);
        int yMin = ryTop < ryBot ? ryTop : ryBot;
        int yMax = (ryTop + (int)rects[i].height) > (ryBot + (int)rects[i].height)
                   ? (ryTop + (int)rects[i].height)
                   : (ryBot + (int)rects[i].height);
        if (yMin < 0) yMin = 0;
        if (yMax > dstH) yMax = dstH;
        rlScissor(rx, yMin, rw, yMax - yMin);
        DrawTexturePro(source,
                       (Rectangle){0, 0, (float)source.width, -(float)source.height},
                       (Rectangle){0, 0, (float)dstW, (float)dstH},
                       (Vector2){0, 0}, 0.0f, WHITE);
        rlDrawRenderBatchActive();
    }
    rlDisableScissorTest();
}

void LiquidSurface_Capture(Camera3D camera) {
    if(!LiquidSurface_HasPending()) return;
    LiquidSurface_SyncTargets();
    s_stats=(LiquidSurfaceStats){0};
    s_stats.streamRejected=s_streamRejected;
    s_stats.gpuStreams=s_gpuStreamCount;
    LiquidSurface_PrepareCPUStreams();
    double stageStart=LiquidSurface_HostClock();
    if (s_reconstructionRoundOverride==3) {
        s_reconstructionABFrame++;
        if (s_reconstructionABFrame>12 && s_reconstructionABPrevRounds>0) {
            int r=s_reconstructionABPrevRounds;
            s_reconstructionABMs[r]+=GetFrameTime()*1000.0;
            s_reconstructionABCount[r]++;
            int total=s_reconstructionABCount[1]+s_reconstructionABCount[2];
            if (total==64)
                TraceLog(LOG_INFO,
                         "LIQUID_PERF recon interleaved samples=%d one=%.3f ms two=%.3f ms delta=%.3f ms",
                         total,
                         s_reconstructionABMs[1]/(double)s_reconstructionABCount[1],
                         s_reconstructionABMs[2]/(double)s_reconstructionABCount[2],
                         s_reconstructionABMs[2]/(double)s_reconstructionABCount[2]-
                         s_reconstructionABMs[1]/(double)s_reconstructionABCount[1]);
        }
    }
    LiquidSurface_TouchLiveMaterials();
    if(LiquidPBDGPU_IsActive()) {
        int slot=LiquidPBDGPU_GetMaterial();
        if(slot>=0 && slot<LIQUID_SURFACE_MATERIAL_SLOTS)
            s_materialKernel[slot]=fmaxf(s_materialKernel[slot],LiquidPBDGPU_GetReconstructionRadius());
    }
    int materialParticipants=0;
    for(int slot=0;slot<LIQUID_SURFACE_MATERIAL_SLOTS;slot++)
        if(s_materialKernel[slot]>0.0f) materialParticipants++;
    int multipleMaterials=materialParticipants>1;
    for(int slot=0;slot<LIQUID_SURFACE_MATERIAL_SLOTS;slot++)
        if(s_materialKernel[slot]<=0.0f) s_materialKernel[slot]=s_reconstructionRadius;

    Texture2D liveScene=SceneTargets_GetSceneTexture();
    if(liveScene.id) {
        if(s_sceneCopy.id==0 || s_sceneCopy.texture.width!=liveScene.width ||
           s_sceneCopy.texture.height!=liveScene.height ||
           s_sceneCopy.texture.format!=liveScene.format) {
            if(s_sceneCopy.id) UnloadRenderTexture(s_sceneCopy);
            s_sceneCopy=LiquidSurface_LoadColorTarget(liveScene.width,liveScene.height,
                                                     liveScene.format);
        }
        if(s_sceneCopy.id) {
            BeginTextureMode(s_sceneCopy);
            rlDisableColorBlend();
            DrawTextureRec(liveScene,
                           (Rectangle){0,0,(float)liveScene.width,-(float)liveScene.height},
                           (Vector2){0,0},WHITE);
            rlDrawRenderBatchActive();
            rlEnableColorBlend();
            EndTextureMode();
        }
    }
    s_stats.sceneCopyHostMs=LiquidSurface_HostElapsed(stageStart);
    stageStart=LiquidSurface_HostClock();
    s_lastCamera=camera; s_hasCamera=true;
    s_fluidView=MatrixLookAt(camera.position,camera.target,camera.up);
    s_fluidProjection=LiquidSurface_MakeProjection(camera);
    bool cpuPrepared=s_cpuImpostor;
    int cpuVisible=cpuPrepared?LiquidCaptureCPU_Prepare(s_particles,s_count,s_fluidView,s_fluidProjection):0;
    ParticleSurfaceCaptureStream streams[32];
    for(int i=0;i<s_gpuStreamCount;i++) streams[i]=(ParticleSurfaceCaptureStream){s_gpuStreams[i],(float)s_gpuStreamMaterial[i]};
    BeginTextureMode(s_capture); ClearBackground((Color){255,0,0,0}); LiquidSurface_BeginCaptureMode3D(camera);
    if(s_captureBatch && s_gpuStreamCount>0) {
        if(ParticleManager_DrawSurfaceStreams(streams,s_gpuStreamCount,camera,s_surfaceTex))
            s_stats.captureDraws++;
        s_stats.gpuCaptureInstances=ParticleManager_GetSurfaceCaptureInstanceCount();
    } else for(int i=0;i<s_gpuStreamCount;i++) {
        GpuParticleSystem_SetSurfaceMaterialId((float)s_gpuStreamMaterial[i]);
        ParticleManager_DrawSurfaceStream(&s_gpuStreams[i],camera,s_surfaceTex);
        s_stats.gpuCaptureInstances+=8192;
        s_stats.captureDraws++;
    }
    GpuParticleSystem_SetSurfaceMaterialId((float)LiquidPBDGPU_GetMaterial());
    LiquidPBDGPU_DrawSurfaceDepth(camera);
    if(LiquidPBDGPU_IsActive()) s_stats.captureDraws++;

    if(cpuPrepared) { if(cpuVisible>0) { LiquidCaptureCPU_Draw(false); s_stats.captureDraws++; } }
    else if (s_count > 0) {
        int materialLoc = GetShaderLocation(s_captureShader, "u_materialId");
        for (int slot = 0; slot < LIQUID_SURFACE_MATERIAL_SLOTS; ++slot) {
            bool any = false;
            for (int i = 0; i < s_count && !any; i++) any = (s_particles[i].material == slot);
            if (!any) continue;
            s_stats.captureDraws++;
            BeginShaderMode(s_captureShader);
            float slotValue = (float)slot;
            if (materialLoc >= 0)
                SetShaderValue(s_captureShader, materialLoc, &slotValue, SHADER_UNIFORM_FLOAT);
            for (int i = 0; i < s_count; i++)
                if (s_particles[i].material == slot) LiquidSurface_DrawEllipsoid(&s_particles[i]);
            EndShaderMode();
        }
    }
    LiquidSurface_EndCaptureMode3D(); EndTextureMode();

    s_stats.frontHostMs=LiquidSurface_HostElapsed(stageStart);
    stageStart=LiquidSurface_HostClock();
    Vector2 texel={1.0f/s_capture.texture.width,1.0f/s_capture.texture.height};
    Matrix captureInverseProjection=MatrixInvert(s_fluidProjection);

    {
        ParticleManager_SetSurfaceCaptureFrontDepth(s_capture.texture);
        LiquidCaptureCPU_SetFrontDepth(s_capture.texture);
        LiquidPBDGPU_SetSurfaceFrontDepth(s_capture.texture);
        BeginTextureMode(s_captureBack); ClearBackground(BLANK); LiquidSurface_BeginCaptureMode3D(camera);
        if(s_captureBatch && s_gpuStreamCount>0) {
            if(ParticleManager_DrawSurfaceBackStreams(streams,s_gpuStreamCount,camera))
                s_stats.captureDraws++;
        } else for(int i=0;i<s_gpuStreamCount;i++) {
            GpuParticleSystem_SetSurfaceMaterialId((float)s_gpuStreamMaterial[i]);
            ParticleManager_DrawSurfaceBackStream(&s_gpuStreams[i],camera);
            s_stats.captureDraws++;
        }
        LiquidPBDGPU_DrawSurfaceBackDepth(camera);
        if(LiquidPBDGPU_IsActive()) s_stats.captureDraws++;
        if(cpuPrepared) { if(cpuVisible>0) { LiquidCaptureCPU_Draw(true); s_stats.captureDraws++; } }
        else if (s_count > 0) {
            rlDrawRenderBatchActive();
            rlSetCullFace(RL_CULL_FACE_FRONT);
            for(int slot=0;slot<LIQUID_SURFACE_MATERIAL_SLOTS;slot++) {
                bool any=false;
                for(int i=0;i<s_count && !any;i++) any=s_particles[i].material==slot;
                if(!any) continue;
                BeginShaderMode(s_captureBackShader);
                float material=(float)slot, match=1.0f;
                SetShaderValue(s_captureBackShader,GetShaderLocation(s_captureBackShader,"u_materialId"),&material,SHADER_UNIFORM_FLOAT);
                SetShaderValue(s_captureBackShader,GetShaderLocation(s_captureBackShader,"u_matchFrontMaterial"),&match,SHADER_UNIFORM_FLOAT);
                SetShaderValueTexture(s_captureBackShader,GetShaderLocation(s_captureBackShader,"u_frontDepthTex"),s_capture.texture);
                for(int i=0;i<s_count;i++) if(s_particles[i].material==slot)
                    LiquidSurface_DrawEllipsoid(&s_particles[i]);
                EndShaderMode(); s_stats.captureDraws++;
            }
            rlDrawRenderBatchActive();
            rlSetCullFace(RL_CULL_FACE_BACK);
        }
        LiquidSurface_EndCaptureMode3D(); EndTextureMode();
        ParticleManager_SetSurfaceCaptureFrontDepth((Texture2D){0});
        LiquidCaptureCPU_SetFrontDepth((Texture2D){0});
        LiquidPBDGPU_SetSurfaceFrontDepth((Texture2D){0});

        s_stats.backHostMs=LiquidSurface_HostElapsed(stageStart);
        stageStart=LiquidSurface_HostClock();

        BeginTextureMode(s_thickness); ClearBackground(BLANK);
        BeginShaderMode(s_thicknessResolve);
        SetShaderValueMatrix(s_thicknessResolve,s_resolveInverseProjectionLoc,captureInverseProjection);
        SetShaderValueTexture(s_thicknessResolve,s_resolveBackLoc,s_captureBack.texture);
        DrawTexturePro(s_capture.texture,
                       (Rectangle){0,0,(float)s_capture.texture.width,-(float)s_capture.texture.height},
                       (Rectangle){0,0,(float)s_thickness.texture.width,(float)s_thickness.texture.height},
                       (Vector2){0,0},0.0f,WHITE);
        EndShaderMode(); EndTextureMode();

        int thicknessBlurRadius=GfxQuality_Get()>=GFX_HIGH?5:
                                (GfxQuality_Get()>=GFX_MED?3:2);
        Vector2 thicknessTexel={1.0f/s_thickness.texture.width,1.0f/s_thickness.texture.height};
        Vector2 horizontal={1.0f,0.0f}, vertical={0.0f,1.0f};
        BeginTextureMode(s_thicknessScratch); ClearBackground(BLANK); BeginShaderMode(s_thicknessBlur);
        SetShaderValue(s_thicknessBlur,s_blurTexelLoc,&thicknessTexel,SHADER_UNIFORM_VEC2);
        SetShaderValue(s_thicknessBlur,s_blurDirectionLoc,&horizontal,SHADER_UNIFORM_VEC2);
        SetShaderValue(s_thicknessBlur,s_blurRadiusLoc,&thicknessBlurRadius,SHADER_UNIFORM_INT);
        DrawTextureRec(s_thickness.texture,(Rectangle){0,0,(float)s_thickness.texture.width,-(float)s_thickness.texture.height},(Vector2){0,0},WHITE);
        EndShaderMode(); EndTextureMode();
        BeginTextureMode(s_thickness); ClearBackground(BLANK); BeginShaderMode(s_thicknessBlur);
        SetShaderValue(s_thicknessBlur,s_blurDirectionLoc,&vertical,SHADER_UNIFORM_VEC2);
        DrawTextureRec(s_thicknessScratch.texture,(Rectangle){0,0,(float)s_thicknessScratch.texture.width,-(float)s_thicknessScratch.texture.height},(Vector2){0,0},WHITE);
        EndShaderMode(); EndTextureMode();
    }
    s_stats.thicknessHostMs=LiquidSurface_HostElapsed(stageStart);
    stageStart=LiquidSurface_HostClock();

    int filterRadius=GfxQuality_Get()>=GFX_HIGH?28:
                     (GfxQuality_Get()>=GFX_MED?14:7);
    Matrix inverseProjection=captureInverseProjection;

    Rectangle bodyRects[LIQUID_MAX_FRAME_REGIONS];
    float pad=(float)filterRadius*2.0f+8.0f;
    int bodyRectCount = LiquidSurface_BuildBodyRects(s_capture.texture.width, s_capture.texture.height,
                                                     pad, bodyRects, LIQUID_MAX_FRAME_REGIONS);

    if(s_useOccupancy) {
        BeginTextureMode(s_occupancy); ClearBackground(BLANK);
        BeginShaderMode(s_occupancyShader);
        DrawTexturePro(s_capture.texture,
            (Rectangle){0,0,(float)s_capture.texture.width,-(float)s_capture.texture.height},
            (Rectangle){0,0,(float)s_occupancy.texture.width,(float)s_occupancy.texture.height},
            (Vector2){0,0},0,WHITE);
        EndShaderMode(); EndTextureMode();
    }

    bool compactHighBody=s_frameBodyRadiusPx>0.0f &&
                         s_frameBodyRadiusPx<=LIQUID_SURFACE_COMPACT_BODY_PX;
    int reconstructionRounds=GfxQuality_Get()>=GFX_HIGH?(compactHighBody?1:2):
                             (GfxQuality_Get()>=GFX_MED?2:1);
    if (s_reconstructionRoundOverride>=1 && s_reconstructionRoundOverride<=2 &&
        GfxQuality_Get()>=GFX_HIGH)
        reconstructionRounds=s_reconstructionRoundOverride;
    else if (s_reconstructionRoundOverride==3 && GfxQuality_Get()>=GFX_HIGH) {
        s_reconstructionABSeed=s_reconstructionABSeed*1664525u+1013904223u;
        reconstructionRounds=1+(int)((s_reconstructionABSeed>>31)&1u);
    }
    if (s_reconstructionRoundOverride==3)
        s_reconstructionABPrevRounds=reconstructionRounds;

    s_stats.reconstructionPasses=reconstructionRounds*(GfxQuality_Get()>=GFX_HIGH?1:2);
    if (GfxQuality_Get() >= GFX_HIGH) {
        int two=1; Vector2 noDirection={0.0f,0.0f};
        for (int iteration=0; iteration<reconstructionRounds; ++iteration) {
            Texture2D source = (iteration==0) ? s_capture.texture : s_smoothA.texture;
            bool last = (iteration == reconstructionRounds-1);
            RenderTexture2D dest = last ? s_smoothB : s_smoothA;
            int fillHoles = (iteration==0);
            BeginTextureMode(dest); ClearBackground(WHITE); BeginShaderMode(s_smooth);
            SetShaderValue(s_smooth,s_texelLoc,&texel,SHADER_UNIFORM_VEC2);
            SetShaderValue(s_smooth,s_dirLoc,&noDirection,SHADER_UNIFORM_VEC2);
            SetShaderValue(s_smooth,s_kernelRadiusLoc,&s_reconstructionRadius,SHADER_UNIFORM_FLOAT);
            SetShaderValueV(s_smooth,s_smoothKernelsLoc,s_materialKernel,SHADER_UNIFORM_FLOAT,LIQUID_SURFACE_MATERIAL_SLOTS);
            SetShaderValue(s_smooth,s_smoothMultiLoc,&multipleMaterials,SHADER_UNIFORM_INT);
            SetShaderValueTexture(s_smooth,s_smoothMaterialLoc,s_capture.texture);
            SetShaderValue(s_smooth,s_filterRadiusLoc,&filterRadius,SHADER_UNIFORM_INT);
            SetShaderValue(s_smooth,s_filter2DLoc,&two,SHADER_UNIFORM_INT);
            int sourceIsCapture=(iteration==0)?1:0;
            SetShaderValue(s_smooth,s_smoothSourceMaterialLoc,&sourceIsCapture,SHADER_UNIFORM_INT);
            SetShaderValueMatrix(s_smooth,s_smoothProjectionLoc,s_fluidProjection);
            SetShaderValueMatrix(s_smooth,s_smoothInverseProjectionLoc,inverseProjection);
            SetShaderValue(s_smooth,s_fillLoc,&fillHoles,SHADER_UNIFORM_INT);
            /* Keep occupancy active on ALL iterations so empty tiles early-exit immediately */
            int mask=s_useOccupancy?1:0;
            SetShaderValue(s_smooth,s_useOccupancyLoc,&mask,SHADER_UNIFORM_INT);
            SetShaderValueTexture(s_smooth,s_occupancyLoc,s_occupancy.texture);
            LiquidSurface_DrawTargetRects(source, dest.texture.width, dest.texture.height,
                                          bodyRects, bodyRectCount);
            EndShaderMode(); EndTextureMode();
        }
        reconstructionRounds = 0;
    }

    for (int iteration=0; iteration<reconstructionRounds; ++iteration) {
        Vector2 horizontal={1.0f,0.0f}, vertical={0.0f,1.0f};
        Texture2D source = iteration ? s_smoothB.texture : s_capture.texture;
        int fillHoles=iteration==0;
        int sourceIsCapture=iteration==0?1:0;
        int mask=s_useOccupancy?1:0;
        BeginTextureMode(s_smoothA); ClearBackground(WHITE); BeginShaderMode(s_smooth);
        SetShaderValueV(s_smooth,s_smoothKernelsLoc,s_materialKernel,SHADER_UNIFORM_FLOAT,LIQUID_SURFACE_MATERIAL_SLOTS);
        SetShaderValue(s_smooth,s_smoothMultiLoc,&multipleMaterials,SHADER_UNIFORM_INT);
        SetShaderValueTexture(s_smooth,s_smoothMaterialLoc,s_capture.texture);
        int zero=0; SetShaderValue(s_smooth,s_filter2DLoc,&zero,SHADER_UNIFORM_INT);
        SetShaderValue(s_smooth,s_useOccupancyLoc,&mask,SHADER_UNIFORM_INT);
        SetShaderValueTexture(s_smooth,s_occupancyLoc,s_occupancy.texture);
        SetShaderValue(s_smooth,s_texelLoc,&texel,SHADER_UNIFORM_VEC2);
        SetShaderValue(s_smooth,s_dirLoc,&horizontal,SHADER_UNIFORM_VEC2);
        SetShaderValue(s_smooth,s_kernelRadiusLoc,&s_reconstructionRadius,SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_smooth,s_filterRadiusLoc,&filterRadius,SHADER_UNIFORM_INT);
        SetShaderValueMatrix(s_smooth,s_smoothProjectionLoc,s_fluidProjection);
        SetShaderValueMatrix(s_smooth,s_smoothInverseProjectionLoc,inverseProjection);
        SetShaderValue(s_smooth,s_fillLoc,&fillHoles,SHADER_UNIFORM_INT);
        SetShaderValue(s_smooth,s_smoothSourceMaterialLoc,&sourceIsCapture,SHADER_UNIFORM_INT);
        LiquidSurface_DrawTargetRects(source, s_smoothA.texture.width, s_smoothA.texture.height,
                                      bodyRects, bodyRectCount);
        EndShaderMode(); EndTextureMode();

        fillHoles=0;
        BeginTextureMode(s_smoothB); ClearBackground(WHITE); BeginShaderMode(s_smooth);
        SetShaderValue(s_smooth,s_useOccupancyLoc,&mask,SHADER_UNIFORM_INT);
        SetShaderValueTexture(s_smooth,s_occupancyLoc,s_occupancy.texture);
        SetShaderValue(s_smooth,s_dirLoc,&vertical,SHADER_UNIFORM_VEC2);
        SetShaderValue(s_smooth,s_fillLoc,&fillHoles,SHADER_UNIFORM_INT);
        sourceIsCapture=0;
        SetShaderValue(s_smooth,s_smoothSourceMaterialLoc,&sourceIsCapture,SHADER_UNIFORM_INT);
        LiquidSurface_DrawTargetRects(s_smoothA.texture, s_smoothB.texture.width, s_smoothB.texture.height,
                                      bodyRects, bodyRectCount);
        EndShaderMode(); EndTextureMode();
    }
    s_stats.reconstructionHostMs=LiquidSurface_HostElapsed(stageStart);
}

void LiquidSurface_Composite(void) {
    if(!LiquidSurface_HasPending()) { s_frameOwnerPriority=-1; s_frameRegionCount=0; s_frameBoundsValid=false; return; }
    double stageStart=LiquidSurface_HostClock();
    Vector2 texel={1.0f/s_smoothB.texture.width,1.0f/s_smoothB.texture.height};
    Vector2 sceneTexel={1.0f/(float)SceneTargets_GetWidth(),1.0f/(float)SceneTargets_GetHeight()};
    Texture2D scene=s_sceneCopy.id?s_sceneCopy.texture:SceneTargets_GetSceneTexture();
    Texture2D sceneDepth=SceneTargets_GetRawDepthTexture(); int has=sceneDepth.id?1:0;
    Matrix inverseProjection=MatrixInvert(s_fluidProjection);
    Matrix viewToWorld=MatrixInvert(s_fluidView);
    int qualityTier=(int)GfxQuality_Get();

    Vector3 sunToLight=Vector3Normalize(Vector3Negate(Environment_GetSunDirection()));
    Vector3 sunDirectionView=Vector3Normalize(
        LiquidSurface_TransformDirection(sunToLight,s_fluidView));
    Vector3 sunColor=Vector3Scale(LiquidSurface_ColorToVec3(Environment_GetSunColor()),
                                Environment_GetSunIntensity());
    Vector3 skyAmbient=LiquidSurface_ColorToVec3(Environment_GetSkyAmbient());
    Vector3 groundAmbient=LiquidSurface_ColorToVec3(Environment_GetGroundAmbient());

    float materialBody[LIQUID_SURFACE_MATERIAL_SLOTS*4];
    float materialGlow[LIQUID_SURFACE_MATERIAL_SLOTS*4];
    float materialSoft[LIQUID_SURFACE_MATERIAL_SLOTS*4];
    float materialOptics[LIQUID_SURFACE_MATERIAL_SLOTS*4];
    for (int i=0;i<LIQUID_SURFACE_MATERIAL_SLOTS;i++) {
        const LiquidDesc *m=&s_materials[i];
        Vector3 body=LiquidSurface_ColorToVec3(m->body);
        Vector3 glow=LiquidSurface_ColorToVec3(m->glow);
        Vector3 soft=LiquidSurface_ColorToVec3(m->soft);
        materialBody[i*4+0]=body.x; materialBody[i*4+1]=body.y; materialBody[i*4+2]=body.z;
        materialBody[i*4+3]=(float)m->liquidClass;
        materialGlow[i*4+0]=glow.x; materialGlow[i*4+1]=glow.y; materialGlow[i*4+2]=glow.z;
        materialGlow[i*4+3]=m->emission;
        materialSoft[i*4+0]=soft.x; materialSoft[i*4+1]=soft.y; materialSoft[i*4+2]=soft.z;
        materialSoft[i*4+3]=m->foam;
        materialOptics[i*4+0]=m->ior;
        materialOptics[i*4+1]=m->roughnessScale;
        materialOptics[i*4+2]=m->opacityPerMetre;
        materialOptics[i*4+3]=0.0f;
    }
    float opticalTime=TimeFX_Elapsed();

    int lightBudget=qualityTier>=GFX_HIGH?LIQUID_OPTICAL_POINT_LIGHTS:
                    (qualityTier>=GFX_MED?2:0);
    VFXLightData lights[LIQUID_OPTICAL_POINT_LIGHTS];
    int pointLightCount=0;
    if (lightBudget>0) VFXLight_GetActive(lights,&pointLightCount,lightBudget);
    if (pointLightCount>lightBudget) pointLightCount=lightBudget;
    float pointLightPos[LIQUID_OPTICAL_POINT_LIGHTS*4]={0};
    float pointLightColor[LIQUID_OPTICAL_POINT_LIGHTS*4]={0};
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
    SetShaderValue(s_composite,s_compositeKernelRadiusLoc,&s_reconstructionRadius,SHADER_UNIFORM_FLOAT);
    SetShaderValueV(s_composite,s_compositeKernelsLoc,s_materialKernel,SHADER_UNIFORM_FLOAT,LIQUID_SURFACE_MATERIAL_SLOTS);
    SetShaderValue(s_composite,s_sunDirectionLoc,&sunDirectionView,SHADER_UNIFORM_VEC3);
    SetShaderValue(s_composite,s_sunColorLoc,&sunColor,SHADER_UNIFORM_VEC3);
    SetShaderValue(s_composite,s_skyAmbientLoc,&skyAmbient,SHADER_UNIFORM_VEC3);
    SetShaderValue(s_composite,s_groundAmbientLoc,&groundAmbient,SHADER_UNIFORM_VEC3);
    SetShaderValueV(s_composite,s_materialBodyLoc,materialBody,SHADER_UNIFORM_VEC4,LIQUID_SURFACE_MATERIAL_SLOTS);
    SetShaderValueV(s_composite,s_materialGlowLoc,materialGlow,SHADER_UNIFORM_VEC4,LIQUID_SURFACE_MATERIAL_SLOTS);
    SetShaderValueV(s_composite,s_materialSoftLoc,materialSoft,SHADER_UNIFORM_VEC4,LIQUID_SURFACE_MATERIAL_SLOTS);
    SetShaderValueV(s_composite,s_materialOpticsLoc,materialOptics,SHADER_UNIFORM_VEC4,LIQUID_SURFACE_MATERIAL_SLOTS);
    SetShaderValue(s_composite,s_timeLoc,&opticalTime,SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_composite,s_pointLightCountLoc,&pointLightCount,SHADER_UNIFORM_INT);
    SetShaderValueV(s_composite,s_pointLightPosLoc,pointLightPos,
                    SHADER_UNIFORM_VEC4,LIQUID_OPTICAL_POINT_LIGHTS);
    SetShaderValueV(s_composite,s_pointLightColorLoc,pointLightColor,
                    SHADER_UNIFORM_VEC4,LIQUID_OPTICAL_POINT_LIGHTS);
    SetShaderValueTexture(s_composite,s_thicknessLoc,s_thickness.texture);
    SetShaderValueTexture(s_composite,s_materialIdTexLoc,s_capture.texture);
    SetShaderValueTexture(s_composite,s_sceneLoc,scene);
    if(has) SetShaderValueTexture(s_composite,s_sceneDepthLoc,sceneDepth);
    Texture2D depthSource=s_smoothB.texture;

    Rectangle compositeRects[LIQUID_MAX_FRAME_REGIONS];
    int compositeRectCount = LiquidSurface_BuildBodyRects(SceneTargets_GetWidth(),
                                                          SceneTargets_GetHeight(), 4.0f,
                                                          compositeRects, LIQUID_MAX_FRAME_REGIONS);
    LiquidSurface_DrawTargetRects(depthSource, SceneTargets_GetWidth(), SceneTargets_GetHeight(),
                                  compositeRects, compositeRectCount);
    EndShaderMode();
    EndBlendMode();
    s_stats.compositeHostMs=LiquidSurface_HostElapsed(stageStart);
    if(s_profile && (++s_profileFrames%60)==0)
        TraceLog(LOG_INFO,"LIQUID_PROFILE host_ms copy=%.3f front=%.3f back=%.3f thickness=%.3f recon=%.3f composite=%.3f cpu=%d/%d dropped=%d gpu_streams=%d front_instances=%d draws=%d recon_passes=%d rejected=%d",
            s_stats.sceneCopyHostMs,s_stats.frontHostMs,s_stats.backHostMs,
            s_stats.thicknessHostMs,s_stats.reconstructionHostMs,s_stats.compositeHostMs,
            s_stats.cpuAdmitted,s_stats.cpuRequested,s_stats.cpuDropped,s_stats.gpuStreams,
            s_stats.gpuCaptureInstances,s_stats.captureDraws,s_stats.reconstructionPasses,s_stats.streamRejected);
    for(int slot=0;slot<LIQUID_SURFACE_MATERIAL_SLOTS;slot++) s_materialKernel[slot]=0.0f;
    s_count=0;
    s_cpuStreamCount=0;
    s_directRequested=0;
    s_streamRejected=0;
    s_gpuStreamCount=0;
    s_frameBodyRadiusPx=-1.0f;
    s_frameKernelRadiusPx=-1.0f;
    s_frameBoundsValid=false;
    s_frameRegionCount=0;
    s_surfaceRunStamp=GetTime();
    s_frameOwnerPriority=-1;
}
