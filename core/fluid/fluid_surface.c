#include "core/fluid/fluid_surface.h"
#include "core/resource_manager.h"
#include "core/screen_distort.h"
#include "core/particles/particle_manager.h"
#include "core/fluid/fluid_pbd_gpu.h"
#include "core/particles/gpu/particle_gpu_legacy.h"
#include "core/gfx_quality.h"
#include "core/vfx_light.h"
#include "environment/environment_system.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>

#define FLUID_OPTICAL_POINT_LIGHTS 4

typedef struct { Vector3 position, radii; int material; } FluidSurfaceParticle;
static FluidSurfaceParticle s_particles[FLUID_SURFACE_MAX_PARTICLES];
static int s_count;
static ParticleRenderStream s_gpuStreams[16];
static int s_gpuStreamMaterial[16];
static int s_gpuStreamCount;

/* --- The liquid table ---------------------------------------------------- */
static FluidLiquidDesc s_materials[FLUID_SURFACE_MATERIAL_SLOTS];
static unsigned int s_materialUse[FLUID_SURFACE_MATERIAL_SLOTS]; /* recency stamp */
static unsigned int s_materialClock = 1;
static int s_materialCount;
static int s_currentMaterial;
static Texture2D s_surfaceTex;
static RenderTexture2D s_capture, s_captureBack, s_thickness, s_thicknessScratch, s_smoothA, s_smoothB;
static RenderTexture2D s_sceneCopy;   // refraction source, see FluidSurface_LoadColorTarget
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

/* --- Cost gates (FluidSurface_RequestBody) ------------------------------- */
static Camera3D s_lastCamera;          /* previous frame's, from Capture */
static bool     s_hasCamera;
/* Wall-clock stamp of the last COMPLETED composite, and of the last claim on
 * the single-owner reconstruction radius.
 *
 * Deliberately timestamps rather than per-frame flags. A flag rolled inside
 * FluidSurface_Composite LATCHES: main.c only calls Composite when something
 * was submitted, so the moment the gate rejects everything, the state that
 * feeds the gate stops being updated and the gate can never reopen. That is
 * not hypothetical — it shipped for one build and deleted the water ring
 * outright, because a single slow start-up frame tripped the budget test and
 * nothing was ever able to clear it again. A stamp that ages out cannot latch,
 * whether or not the function that sets it is reached. */
static double   s_surfaceRunStamp = -1000.0;
static double   s_radiusOwnerStamp = -1000.0;
static int      s_frameOwnerPriority = -1;
/* How long a stamp stays "current". Several frames at any playable rate, so a
 * body that skips a frame does not lose its claim, and short enough that a
 * finished effect releases it well inside a blink. */
#define FLUID_GATE_STAMP_TTL 0.10

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

/* BeginMode3D's twin, with THIS module's frustum instead of raylib's.
 *
 * raylib's BeginMode3D builds its projection from RL_CULL_DISTANCE_NEAR, which
 * is 0.01. FluidSurface_MakeProjection — the matrix the composite INVERTS to
 * turn captured depth back into a view position — uses near = 1.0, matching
 * main.c's MyBeginMode3D (which documents why: below ~1.0 this project's
 * rlFrustum renders blank).
 *
 * Anything in the capture that lets the RASTERIZER produce its depth
 * (gl_FragCoord.z — i.e. the CPU ellipsoid path) therefore wrote depth on a
 * 0.01-near frustum and had it decoded on a 1.0-near one. The error is not
 * subtle: a body 7.5 m from the camera writes 0.99868 under near=0.01, and
 * inverting that under near=1.0 puts it at 428 m. Every pixel then failed the
 * scene-occlusion test (`frontGap <= -0.002`) and was discarded, and the few
 * that survived reconstructed their normals off a depth signal compressed into
 * the last thousandth of the R32F range, which is the speckle.
 *
 * The GPU splat paths never noticed because they compute depth themselves from
 * a u_projection built with near = 1.0 and write gl_FragDepth, so the water ring
 * and the PBD crown were correct throughout and the defect stayed invisible —
 * this CPU path had no fixture until LIQUID BENCH.
 *
 * rlFrustum is called with the same numbers rather than multiplying
 * s_fluidProjection in: rlvk stores matrices transposed relative to raymath, so
 * rlMultMatrixf(MatrixToFloat(MatrixFrustum(...))) is NOT the same matrix as
 * rlFrustum(...) with those arguments. */
static void FluidSurface_BeginCaptureMode3D(Camera3D camera) {
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

static void FluidSurface_EndCaptureMode3D(void) {
    rlDrawRenderBatchActive();
    rlMatrixMode(RL_PROJECTION);
    rlPopMatrix();
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
    rlDisableDepthTest();
}

/* `carriesMaterial` selects RGBA32F over R32F.
 *
 * Only the FRONT capture needs it: the composite reads the winning fragment's
 * liquid-table slot out of .b, and the back capture is only ever subtracted
 * from the front to get a thickness. The extra channels are 12 more bytes per
 * pixel on ONE target, which is the price of two liquids on screen at once —
 * the alternative (a second rasterization of the same splat cloud into a
 * separate id target) costs a whole geometry pass.
 *
 * The format cannot be RG32F: raylib's pixel-format list has R32, RGB32 and
 * RGBA32 and nothing in between. Nor can it be half-float — see below. */
static RenderTexture2D FluidSurface_LoadDepthTargetEx(int w, int h, bool carriesMaterial) {
    RenderTexture2D t = {0}; t.id = rlLoadFramebuffer();
    if (!t.id) return t;
    rlEnableFramebuffer(t.id);
    // Capture depth in colour, rather than sampling the FBO depth attachment.
    // MoltenVK (used by rlvk on macOS) does not expose sampled FBO depth textures.
    // R32F is also essential here: non-linear device depth loses visible
    // surface gradients when stored in half-float near depth 1.
    int format = carriesMaterial ? RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32
                                 : RL_PIXELFORMAT_UNCOMPRESSED_R32;
    t.texture.id = rlLoadTexture(NULL,w,h,format,1);
    t.texture.width=w; t.texture.height=h; t.texture.format=format; t.texture.mipmaps=1;
    t.depth.id=rlLoadTextureDepth(w,h,false); t.depth.width=w; t.depth.height=h; t.depth.mipmaps=1;
    rlFramebufferAttach(t.id,t.texture.id,RL_ATTACHMENT_COLOR_CHANNEL0,RL_ATTACHMENT_TEXTURE2D,0);
    rlFramebufferAttach(t.id,t.depth.id,RL_ATTACHMENT_DEPTH,RL_ATTACHMENT_TEXTURE2D,0);
    if (!rlFramebufferComplete(t.id)) TraceLog(LOG_WARNING,"FluidSurface: depth target incomplete");
    rlDisableFramebuffer();
    /* The material id must NOT be interpolated: a bilinear tap that straddles a
     * boundary between slot 0 and slot 2 returns 1, which is a different liquid
     * entirely. The composite rounds, so a POINT filter keeps every tap on a
     * real slot. Depth loses nothing by it — every consumer samples texel
     * centres. */
    SetTextureFilter(t.texture, carriesMaterial ? TEXTURE_FILTER_POINT
                                                : TEXTURE_FILTER_BILINEAR);
    return t;
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

/* FluidSurface_RegisterEllipsoid takes three radii and the capture used to
 * average them into one, so the API promised anisotropy and drew a sphere.
 * A unit sphere under a non-uniform scale IS the ellipsoid, front and back —
 * and the scale is positive on every axis, so the winding (and therefore the
 * front-face cull the back pass relies on) is unaffected. */
static void FluidSurface_DrawEllipsoid(const FluidSurfaceParticle *sp) {
    rlPushMatrix();
    /* rlPushMatrix() does NOT hand back an identity. It only redirects writes to
     * the software `transform` matrix and SAVES whatever that global already
     * held; nothing ever clears it. Measured here: at this point in the frame it
     * holds a leftover VIEW matrix (translation -13.25 on Z = the camera
     * distance), so every sphere was drawn view-transformed TWICE and the three
     * bench bodies landed off the top and bottom of the screen while their
     * registered world positions were provably correct.
     *
     * The invariant "transform returns to identity because pop restores the
     * pre-push value" only holds while pushes are strictly LIFO — and PROJECTION
     * and MODELVIEW pushes share ONE stack in both rlgl and rlvk, so any
     * interleaving breaks it. Do not rely on it: state the identity.
     *
     * Nothing else in the SSF capture noticed because every other input (GPU
     * splat streams, PBD) is an immediate-mode billboard that never touches the
     * matrix stack, and this CPU ellipsoid path had no fixture until now. */
    rlLoadIdentity();
    rlTranslatef(sp->position.x, sp->position.y, sp->position.z);
    rlScalef(sp->radii.x, sp->radii.y, sp->radii.z);
    /* Lower-LOD sphere is fast enough for O(100s) CPU particles. */
    DrawSphereEx((Vector3){0.0f, 0.0f, 0.0f}, 1.0f, 6, 6, WHITE);
    rlPopMatrix();
}


void FluidSurface_Init(int width,int height) {
    /* Keep the authored High surface at native resolution while its optical
     * look is being judged. R32F prevents the former zoom-dependent depth
     * bands; lower tiers retain the cheaper reconstruction path. */
    float scale = GfxQuality_Get() >= GFX_HIGH ? 1.0f :
                  (GfxQuality_Get() >= GFX_MED ? 0.75f : 0.50f);
    int w = (int)(width*scale), h=(int)(height*scale);
    s_capture=FluidSurface_LoadDepthTargetEx(w,h,true);
    /* Same kind of target as the front capture: dual-depth thickness is the
     * difference of two depth reductions over the same splat cloud. */
    s_captureBack=FluidSurface_LoadDepthTargetEx(w,h,false);
    /* LoadRenderTexture defaults to RGBA8. That silently reduced smoothed
     * device depth to 256 levels, turning shallow liquid into zoom-dependent
     * horizontal contour bands. Scalar R32F costs the same four bytes/pixel. */
    /* Thickness runs at HALF the surface resolution.
     *
     * Measured: the two Gaussian passes over it cost 3.3 ms of a 16.7 ms frame at
     * native resolution, while the back capture and the resolve that produce it
     * together cost 0.5 ms — the measurement is nearly free and the smoothing was
     * the entire bill. Thickness is a low-frequency quantity with no silhouettes
     * to preserve (which is why it gets a plain Gaussian at all, per Green 2010),
     * so a quarter of the pixels loses nothing that survives the blur. The
     * composite's bilinear tap upsamples it for free.
     *
     * The DEPTH targets stay native: the surface normal is reconstructed from
     * them and that is not low-frequency. */
    int tw = w/2 > 1 ? w/2 : 1, th = h/2 > 1 ? h/2 : 1;
    s_thickness=FluidSurface_LoadScalarTarget(tw,th);
    s_thicknessScratch=FluidSurface_LoadScalarTarget(tw,th);
    s_smoothA=FluidSurface_LoadScalarTarget(w,h);
    s_smoothB=FluidSurface_LoadScalarTarget(w,h);
    s_captureShader=ResourceManager_LoadShader(NULL,"core/fluid/shaders/fluid_capture.fs");
    s_captureBackShader=ResourceManager_LoadShader(NULL,"core/fluid/shaders/fluid_capture_back.fs");
    s_thicknessResolve=ResourceManager_LoadShader(NULL,"core/fluid/shaders/fluid_thickness_resolve.fs");
    s_thicknessBlur=ResourceManager_LoadShader(NULL,"core/fluid/shaders/fluid_thickness_blur.fs");
    s_smooth=ResourceManager_LoadShader(NULL,"core/fluid/shaders/fluid_depth_narrow_range.fs");
    s_composite=ResourceManager_LoadShader(NULL,"core/fluid/shaders/fluid_surface.fs");
    Image img = GenImageGradientRadial(64, 64, 0.0f, WHITE, BLANK);
    s_surfaceTex = LoadTextureFromImage(img); UnloadImage(img);
    SetTextureFilter(s_surfaceTex, TEXTURE_FILTER_BILINEAR);
    s_texelLoc=GetShaderLocation(s_smooth,"u_texel");
    s_dirLoc=GetShaderLocation(s_smooth,"u_direction");
    s_fillLoc=GetShaderLocation(s_smooth,"u_fillHoles");
    s_smoothProjectionLoc=GetShaderLocation(s_smooth,"u_projection");
    s_smoothInverseProjectionLoc=GetShaderLocation(s_smooth,"u_inverseProjection");
    s_kernelRadiusLoc=GetShaderLocation(s_smooth,"u_kernelRadius");
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
    /* Seed the whole table with the default water, so a frame that binds nothing
     * still has a valid liquid in every slot the capture could name. */
    {
        FluidLiquidDesc water=FluidSurface_DielectricDesc(s_materialBody,s_materialGlow,s_materialSoft);
        for (int i=0;i<FLUID_SURFACE_MATERIAL_SLOTS;i++) { s_materials[i]=water; s_materialUse[i]=0; }
        s_materialCount=1; s_currentMaterial=0; s_materialUse[0]=++s_materialClock;
    }
}
void FluidSurface_Unload(void) { UnloadTexture(s_surfaceTex); UnloadRenderTexture(s_capture); UnloadRenderTexture(s_captureBack); UnloadRenderTexture(s_thickness); UnloadRenderTexture(s_thicknessScratch); UnloadRenderTexture(s_smoothA); UnloadRenderTexture(s_smoothB); if(s_sceneCopy.id) { UnloadRenderTexture(s_sceneCopy); s_sceneCopy=(RenderTexture2D){0}; } }
/* Screen-space radius, in pixels, of a sphere of `worldRadius` at `center`.
 *
 * Uses the vertical field of view, which is the axis raylib's fovy names and
 * the one that does not change with the window's aspect ratio. */
static float FluidSurface_ProjectedRadiusPx(Vector3 center, float worldRadius) {
    if (!s_hasCamera) return 1e9f;   /* no camera yet: admit, decide next frame */
    Vector3 toBody = Vector3Subtract(center, s_lastCamera.position);
    float distance = Vector3Length(toBody);
    if (s_lastCamera.projection == CAMERA_ORTHOGRAPHIC) {
        float halfHeight = s_lastCamera.fovy * 0.5f;
        if (halfHeight <= 0.0001f) return 1e9f;
        return worldRadius / halfHeight * (float)GetScreenHeight() * 0.5f;
    }
    /* Behind or through the camera: no meaningful projection, and a body the
     * camera is inside is emphatically not small. */
    if (distance <= worldRadius) return 1e9f;
    float halfFovTan = tanf(s_lastCamera.fovy * 0.5f * DEG2RAD);
    if (halfFovTan <= 0.0001f) return 1e9f;
    return worldRadius / (distance * halfFovTan) * (float)GetScreenHeight() * 0.5f;
}

bool FluidSurface_RequestBody(FluidSurfacePriority priority, Vector3 center,
                              float worldRadius) {
    /* A minion never gets a screen-space surface, at any size, in any frame. */
    if (priority <= FLUID_PRIORITY_MINION) return false;

    /* Over budget: only a boss ultimate still gets one. Checked before size so
     * an over-budget frame cannot be talked into SSF by a large body.
     *
     * GetFrameTime() is read live rather than cached at composite time — see
     * the latch note on s_surfaceRunStamp. It already reports the PREVIOUS
     * frame's duration, which is the number this test wants. */
    if (GetFrameTime()*1000.0f > FLUID_SURFACE_BUDGET_MS && priority < FLUID_PRIORITY_ULTIMATE)
        return false;

    /* A basic attack may only JOIN a running surface — never switch one on.
     * This is the rule that makes basic attacks affordable at all: the frame's
     * fixed cost is already being paid by something else, so the marginal cost
     * is the splat area. "Running" is measured on the PREVIOUS frame so the
     * answer does not depend on which composer happens to be called first. */
    if (priority <= FLUID_PRIORITY_BASIC &&
        GetTime() - s_surfaceRunStamp > FLUID_GATE_STAMP_TTL) return false;

    if (FluidSurface_ProjectedRadiusPx(center, worldRadius)
        < FLUID_SURFACE_MIN_PROJECTED_RADIUS_PX) return false;

    if ((int)priority > s_frameOwnerPriority) s_frameOwnerPriority = (int)priority;
    return true;
}

FluidLiquidDesc FluidSurface_DielectricDesc(Color body, Color glow, Color soft) {
    FluidLiquidDesc d = {0};
    d.body=body; d.glow=glow; d.soft=soft;
    d.liquidClass=FLUID_LIQUID_DIELECTRIC;
    d.emission=0.0f;
    d.ior=1.333f;            /* water */
    d.roughnessScale=1.0f;
    d.opacityPerMetre=0.0f;  /* the shader's own FLUID_TURBIDITY_PER_M is enough */
    d.foam=1.0f;
    return d;
}

static bool FluidSurface_DescEqual(const FluidLiquidDesc *a, const FluidLiquidDesc *b) {
    return a->body.r==b->body.r && a->body.g==b->body.g && a->body.b==b->body.b &&
           a->glow.r==b->glow.r && a->glow.g==b->glow.g && a->glow.b==b->glow.b &&
           a->soft.r==b->soft.r && a->soft.g==b->soft.g && a->soft.b==b->soft.b &&
           a->liquidClass==b->liquidClass &&
           a->emission==b->emission && a->ior==b->ior &&
           a->roughnessScale==b->roughnessScale &&
           a->opacityPerMetre==b->opacityPerMetre && a->foam==b->foam;
}

int FluidSurface_BindMaterial(const FluidLiquidDesc *desc) {
    if (!desc) return s_currentMaterial;
    FluidLiquidDesc d = *desc;
    if (d.ior <= 1.0f) d.ior = 1.333f;
    if (d.roughnessScale <= 0.0f) d.roughnessScale = 1.0f;
    if (d.opacityPerMetre < 0.0f) d.opacityPerMetre = 0.0f;
    for (int i=0;i<s_materialCount;i++) {
        if (FluidSurface_DescEqual(&s_materials[i], &d)) {
            s_materialUse[i]=++s_materialClock;
            s_currentMaterial=i;
            return i;
        }
    }
    int slot;
    if (s_materialCount < FLUID_SURFACE_MATERIAL_SLOTS) {
        slot = s_materialCount++;
    } else {
        /* Least recently used. A live body whose slot is stolen changes colour;
         * it never reads out of range, because every slot always holds a valid
         * liquid. Touching a slot happens on bind AND on submit (see Capture),
         * so a body that is still on screen keeps its stamp fresh. */
        slot = 0;
        for (int i=1;i<FLUID_SURFACE_MATERIAL_SLOTS;i++)
            if (s_materialUse[i] < s_materialUse[slot]) slot = i;
    }
    s_materials[slot]=d;
    s_materialUse[slot]=++s_materialClock;
    s_currentMaterial=slot;
    return slot;
}

int FluidSurface_CurrentMaterial(void) { return s_currentMaterial; }

void FluidSurface_SetMaterialColors(Color body, Color glow, Color soft) {
    FluidLiquidDesc d = FluidSurface_DielectricDesc(body, glow, soft);
    FluidSurface_BindMaterial(&d);
    s_materialBody=body;
    s_materialGlow=glow;
    s_materialSoft=soft;
}
/* Still ONE radius for the whole capture, so it needs an owner.
 *
 * The priority is passed in rather than read off the frame's running maximum:
 * that maximum already includes every body admitted so far, so a LOWER-priority
 * body submitting later would compare itself against its own frame's high-water
 * mark and win. The caller states its own rank or does not participate. */
static int s_radiusOwnerPriority = -1;
void FluidSurface_SetReconstructionRadiusFor(FluidSurfacePriority priority, float radius) {
    if(radius<=0.0001f) return;
    /* An owner that has not claimed recently has stopped casting; its rank must
     * not outrank a live body forever. Same anti-latch reasoning as the gates. */
    bool ownerCurrent = (GetTime() - s_radiusOwnerStamp) <= FLUID_GATE_STAMP_TTL;
    if(ownerCurrent && (int)priority < s_radiusOwnerPriority) return;
    s_radiusOwnerPriority=(int)priority;
    s_radiusOwnerStamp=GetTime();
    s_reconstructionRadius=radius;
}
/* Ungated callers keep the old unconditional behaviour: last writer wins. */
void FluidSurface_SetReconstructionRadius(float radius) {
    if(radius<=0.0001f) return;
    s_radiusOwnerPriority=-1;
    s_radiusOwnerStamp=-1000.0;
    s_reconstructionRadius=radius;
}
void FluidSurface_RegisterParticle(Vector3 p,float r) { FluidSurface_RegisterEllipsoid(p,(Vector3){r,r,r}); }
void FluidSurface_RegisterEllipsoid(Vector3 p,Vector3 radii) { if(s_count<FLUID_SURFACE_MAX_PARTICLES) s_particles[s_count++]=(FluidSurfaceParticle){p,radii,s_currentMaterial}; }
bool FluidSurface_SubmitParticleStream(const ParticleRenderStream *stream) {
    if (!stream || stream->mode != PARTICLE_RENDER_SURFACE_INPUT) return false;
    if (stream->backend == PARTICLE_RENDER_BACKEND_CPU) {
        ParticleSurfaceSample samples[FLUID_SURFACE_MAX_PARTICLES];
        int count = ParticleManager_CopySurfaceSamples(stream, samples, FLUID_SURFACE_MAX_PARTICLES);
        for (int i = 0; i < count; ++i) FluidSurface_RegisterParticle(samples[i].position, samples[i].radius);
        return count > 0;
    }
    if (s_gpuStreamCount >= (int)(sizeof(s_gpuStreams)/sizeof(s_gpuStreams[0]))) return false;
    s_gpuStreamMaterial[s_gpuStreamCount] = s_currentMaterial;
    s_gpuStreams[s_gpuStreamCount++] = *stream;
    return true;
}
bool FluidSurface_HasPending(void) { return s_count > 0 || s_gpuStreamCount > 0 || FluidPBDGPU_IsActive(); }
/* Keep the recency stamps of every slot that is actually on screen fresh, so
 * FluidSurface_BindMaterial's LRU eviction can only ever take a liquid that
 * nothing is drawing. Called once per capture, before any rasterization. */
static void FluidSurface_TouchLiveMaterials(void) {
    for (int i=0;i<s_gpuStreamCount;i++) {
        int slot=s_gpuStreamMaterial[i];
        if (slot>=0 && slot<FLUID_SURFACE_MATERIAL_SLOTS) s_materialUse[slot]=++s_materialClock;
    }
    for (int i=0;i<s_count;i++) {
        int slot=s_particles[i].material;
        if (slot>=0 && slot<FLUID_SURFACE_MATERIAL_SLOTS) s_materialUse[slot]=++s_materialClock;
    }
    if (FluidPBDGPU_IsActive()) {
        int slot=FluidPBDGPU_GetMaterial();
        if (slot>=0 && slot<FLUID_SURFACE_MATERIAL_SLOTS) s_materialUse[slot]=++s_materialClock;
    }
}

void FluidSurface_Capture(Camera3D camera) {
    if(!FluidSurface_HasPending()) return;
    FluidSurface_TouchLiveMaterials();
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
    s_lastCamera=camera; s_hasCamera=true;
    s_fluidView=MatrixLookAt(camera.position,camera.target,camera.up);
    s_fluidProjection=FluidSurface_MakeProjection(camera);
    BeginTextureMode(s_capture); ClearBackground((Color){255,0,0,0}); FluidSurface_BeginCaptureMode3D(camera);
    /* One uniform per stream, not per splat: each stream is one draw, and the
     * slot it carries is fixed at submit time. */
    for (int i=0;i<s_gpuStreamCount;i++) {
        GpuParticleSystem_SetSurfaceMaterialId((float)s_gpuStreamMaterial[i]);
        ParticleManager_DrawSurfaceStream(&s_gpuStreams[i], camera, s_surfaceTex);
    }
    GpuParticleSystem_SetSurfaceMaterialId((float)FluidPBDGPU_GetMaterial());
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
        /* Grouped BY SLOT rather than drawn in registration order: u_materialId
         * is a uniform, so a per-particle value would need a batch flush per
         * sphere. Four passes over a list of at most 384 costs nothing next to
         * that, and every pass draws only its own slot's particles. */
        int materialLoc = GetShaderLocation(s_captureShader, "u_materialId");
        for (int slot = 0; slot < FLUID_SURFACE_MATERIAL_SLOTS; ++slot) {
            bool any = false;
            for (int i = 0; i < s_count && !any; i++) any = (s_particles[i].material == slot);
            if (!any) continue;
            BeginShaderMode(s_captureShader);
            float slotValue = (float)slot;
            if (materialLoc >= 0)
                SetShaderValue(s_captureShader, materialLoc, &slotValue, SHADER_UNIFORM_FLOAT);
            for (int i = 0; i < s_count; i++)
                if (s_particles[i].material == slot) FluidSurface_DrawEllipsoid(&s_particles[i]);
            EndShaderMode();
        }
    }
    FluidSurface_EndCaptureMode3D(); EndTextureMode();

    Vector2 texel={1.0f/s_capture.texture.width,1.0f/s_capture.texture.height};
    Matrix captureInverseProjection=MatrixInvert(s_fluidProjection);

    {
        /* --- Back depth: the far side of the same cloud. ---
         * Cleared to ZERO, not to 1: this pass reduces with MAX (the shaders
         * write the complement of the depth so an ordinary depth test keeps the
         * FARTHEST fragment), so "nothing here" must be the smallest possible
         * value, the opposite of the front pass's convention. */
        BeginTextureMode(s_captureBack); ClearBackground(BLANK); FluidSurface_BeginCaptureMode3D(camera);
        for (int i=0;i<s_gpuStreamCount;i++) ParticleManager_DrawSurfaceBackStream(&s_gpuStreams[i], camera);
        FluidPBDGPU_DrawSurfaceBackDepth(camera);
        if (s_count > 0) {
            /* Real geometry, so the far surface is a culling choice rather than
             * a second analytic root. The flush is required: raylib batches
             * geometry and would otherwise rasterize these spheres under
             * whichever cull state happened to be current at flush time. */
            rlDrawRenderBatchActive();
            rlSetCullFace(RL_CULL_FACE_FRONT);
            BeginShaderMode(s_captureBackShader);
            for (int i = 0; i < s_count; i++) FluidSurface_DrawEllipsoid(&s_particles[i]);
            EndShaderMode();
            rlDrawRenderBatchActive();
            rlSetCullFace(RL_CULL_FACE_BACK);
        }
        FluidSurface_EndCaptureMode3D(); EndTextureMode();

        /* T = z_back - z_front, then a plain Gaussian (Green 2010), both at the
         * thickness target's own half resolution. */
        BeginTextureMode(s_thickness); ClearBackground(BLANK);
        BeginShaderMode(s_thicknessResolve);
        SetShaderValueMatrix(s_thicknessResolve,s_resolveInverseProjectionLoc,captureInverseProjection);
        SetShaderValueTexture(s_thicknessResolve,s_resolveBackLoc,s_captureBack.texture);
        /* Pro, not Rec: the source is a NATIVE-resolution capture and the target
         * is half that, and DrawTextureRec sizes its quad from the SOURCE. That
         * puts UV 0..0.5 across the whole viewport — the resolve then reads only
         * the top-left quarter of the capture and the surface vanishes from
         * wherever it actually is. */
        DrawTexturePro(s_capture.texture,
                       (Rectangle){0,0,(float)s_capture.texture.width,-(float)s_capture.texture.height},
                       (Rectangle){0,0,(float)s_thickness.texture.width,(float)s_thickness.texture.height},
                       (Vector2){0,0},0.0f,WHITE);
        EndShaderMode(); EndTextureMode();

        /* Radii are HALVED with the resolution so the blur covers the same world
         * distance it did at native res — the point is to pay less, not to smooth
         * less. */
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
    /* A CEILING now, not the radius itself: the filter derives its own reach per
     * pixel from the kernel's projected size (fluid_depth_narrow_range.fs), so
     * this only bounds what a close-up body may cost. The old values WERE the
     * radius, and 10 texels could not span a kernel that projects to forty —
     * which is why a nearby body reconstructed as a heap of separate beads. */
    int filterRadius=GfxQuality_Get()>=GFX_HIGH?28:
                     (GfxQuality_Get()>=GFX_MED?14:7);
    Matrix inverseProjection=captureInverseProjection;
    /* Two rounds, not four. The filter's reach is now derived from the kernel's
     * projected size rather than a fixed 10 texels, so one round already spans a
     * splat — and each extra round re-estimates its own slope from the previous
     * round's output, which is a feedback loop that amplifies whatever ripple
     * survived the last pass. Four rounds of that is where the standing bands
     * came from. Halving them also halves the filter's cost. */
    int reconstructionRounds=GfxQuality_Get()>=GFX_HIGH?2:
                             (GfxQuality_Get()>=GFX_MED?2:1);
    /* The TRUE 2D kernel at HIGH; the separable pair below at MED and LOW.
     *
     * Running the passes separately showed what the separation costs: the
     * horizontal pass alone smears the reconstructed normal into horizontal
     * ribbons, the vertical pass alone into vertical ones, and the residue of
     * both is a cross-hatch hugging the silhouette. The 2D kernel has no axis to
     * streak along and the striations are gone on both fixtures.
     *
     * It is not free — roughly +1-2 ms at HIGH on this machine, though the pass
     * count halves, which is why it is not the multiple-times regression the tap
     * count alone would suggest. MED is the ANDROID default (GfxQuality_Default),
     * and a several-hundred-tap loop of dependent fetches on a Mali tiler is
     * exactly the thing that cannot be checked from here, so the mobile tiers
     * keep the cheap path until someone runs it on a device.
     *
     * Two rounds, not one: a single 2D round leaves the tube visibly lumpy. */
    if (GfxQuality_Get() >= GFX_HIGH) {
        /* True 2D: ONE pass per round instead of two, so the rounds alternate
         * targets and must land in s_smoothB, which is what the composite reads. */
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
            SetShaderValue(s_smooth,s_filterRadiusLoc,&filterRadius,SHADER_UNIFORM_INT);
            SetShaderValue(s_smooth,s_filter2DLoc,&two,SHADER_UNIFORM_INT);
            SetShaderValueMatrix(s_smooth,s_smoothProjectionLoc,s_fluidProjection);
            SetShaderValueMatrix(s_smooth,s_smoothInverseProjectionLoc,inverseProjection);
            SetShaderValue(s_smooth,s_fillLoc,&fillHoles,SHADER_UNIFORM_INT);
            DrawTextureRec(source,(Rectangle){0,0,source.width,-source.height},(Vector2){0,0},WHITE);
            EndShaderMode(); EndTextureMode();
        }
        reconstructionRounds = 0;
    }
    { int zero=0; SetShaderValue(s_smooth,s_filter2DLoc,&zero,SHADER_UNIFORM_INT); }
    for (int iteration=0; iteration<reconstructionRounds; ++iteration) {
        Vector2 horizontal={1.0f,0.0f}, vertical={0.0f,1.0f};
        Texture2D source = iteration ? s_smoothB.texture : s_capture.texture;
        int fillHoles=iteration==0;
        BeginTextureMode(s_smoothA); ClearBackground(WHITE); BeginShaderMode(s_smooth); SetShaderValue(s_smooth,s_texelLoc,&texel,SHADER_UNIFORM_VEC2); SetShaderValue(s_smooth,s_dirLoc,&horizontal,SHADER_UNIFORM_VEC2); SetShaderValue(s_smooth,s_kernelRadiusLoc,&s_reconstructionRadius,SHADER_UNIFORM_FLOAT); SetShaderValue(s_smooth,s_filterRadiusLoc,&filterRadius,SHADER_UNIFORM_INT); SetShaderValueMatrix(s_smooth,s_smoothProjectionLoc,s_fluidProjection); SetShaderValueMatrix(s_smooth,s_smoothInverseProjectionLoc,inverseProjection); SetShaderValue(s_smooth,s_fillLoc,&fillHoles,SHADER_UNIFORM_INT); DrawTextureRec(source,(Rectangle){0,0,source.width,-source.height},(Vector2){0,0},WHITE); EndShaderMode(); EndTextureMode();
        fillHoles=0;
        BeginTextureMode(s_smoothB); ClearBackground(WHITE); BeginShaderMode(s_smooth); SetShaderValue(s_smooth,s_dirLoc,&vertical,SHADER_UNIFORM_VEC2); SetShaderValue(s_smooth,s_fillLoc,&fillHoles,SHADER_UNIFORM_INT); DrawTextureRec(s_smoothA.texture,(Rectangle){0,0,s_smoothA.texture.width,-s_smoothA.texture.height},(Vector2){0,0},WHITE); EndShaderMode(); EndTextureMode();
    }
}
void FluidSurface_Composite(void) {
    if(!FluidSurface_HasPending()) { s_frameOwnerPriority=-1; return; }
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
    /* The whole table, every frame. Four slots of four vec4s is 256 bytes; the
     * alternative (upload only what changed) would need the shader to know which
     * slots are stale, and the capture names slots by index. */
    float materialBody[FLUID_SURFACE_MATERIAL_SLOTS*4];
    float materialGlow[FLUID_SURFACE_MATERIAL_SLOTS*4];
    float materialSoft[FLUID_SURFACE_MATERIAL_SLOTS*4];
    float materialOptics[FLUID_SURFACE_MATERIAL_SLOTS*4];
    for (int i=0;i<FLUID_SURFACE_MATERIAL_SLOTS;i++) {
        const FluidLiquidDesc *m=&s_materials[i];
        Vector3 body=FluidSurface_ColorToVec3(m->body);
        Vector3 glow=FluidSurface_ColorToVec3(m->glow);
        Vector3 soft=FluidSurface_ColorToVec3(m->soft);
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
    SetShaderValue(s_composite,s_compositeKernelRadiusLoc,&s_reconstructionRadius,SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_composite,s_sunDirectionLoc,&sunDirectionView,SHADER_UNIFORM_VEC3);
    SetShaderValue(s_composite,s_sunColorLoc,&sunColor,SHADER_UNIFORM_VEC3);
    SetShaderValue(s_composite,s_skyAmbientLoc,&skyAmbient,SHADER_UNIFORM_VEC3);
    SetShaderValue(s_composite,s_groundAmbientLoc,&groundAmbient,SHADER_UNIFORM_VEC3);
    SetShaderValueV(s_composite,s_materialBodyLoc,materialBody,SHADER_UNIFORM_VEC4,FLUID_SURFACE_MATERIAL_SLOTS);
    SetShaderValueV(s_composite,s_materialGlowLoc,materialGlow,SHADER_UNIFORM_VEC4,FLUID_SURFACE_MATERIAL_SLOTS);
    SetShaderValueV(s_composite,s_materialSoftLoc,materialSoft,SHADER_UNIFORM_VEC4,FLUID_SURFACE_MATERIAL_SLOTS);
    SetShaderValueV(s_composite,s_materialOpticsLoc,materialOptics,SHADER_UNIFORM_VEC4,FLUID_SURFACE_MATERIAL_SLOTS);
    SetShaderValue(s_composite,s_timeLoc,&opticalTime,SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_composite,s_pointLightCountLoc,&pointLightCount,SHADER_UNIFORM_INT);
    SetShaderValueV(s_composite,s_pointLightPosLoc,pointLightPos,
                    SHADER_UNIFORM_VEC4,FLUID_OPTICAL_POINT_LIGHTS);
    SetShaderValueV(s_composite,s_pointLightColorLoc,pointLightColor,
                    SHADER_UNIFORM_VEC4,FLUID_OPTICAL_POINT_LIGHTS);
    /* Raylib owns the sampler slots.  Manual rlActiveTextureSlot bindings are
     * not reliably reflected into Shader objects on every backend. */
    SetShaderValueTexture(s_composite,s_thicknessLoc,s_thickness.texture);
    SetShaderValueTexture(s_composite,s_materialIdTexLoc,s_capture.texture);
    SetShaderValueTexture(s_composite,s_sceneLoc,scene);
    if(has) SetShaderValueTexture(s_composite,s_sceneDepthLoc,sceneDepth);
    Texture2D depthSource=s_smoothB.texture;
    DrawTexturePro(depthSource,(Rectangle){0,0,depthSource.width,-depthSource.height},(Rectangle){0,0,GetRenderWidth(),GetRenderHeight()},(Vector2){0,0},0,WHITE);
    EndShaderMode();
    EndBlendMode();
    s_count=0;
    s_gpuStreamCount=0;
    /* Reaching this line is what "the surface is running" means to a basic
     * attack asking to join one. */
    s_surfaceRunStamp=GetTime();
    s_frameOwnerPriority=-1;
}
