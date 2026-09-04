#include "core/gas/gas_system.h"

#include "core/gas/gas_sim.h"
#include "core/gfx_quality.h"
#include "core/resource_manager.h"
#include "core/scene_targets.h"
#include "core/tuning.h"
#include "core/gas/gas_perf_internal.inl"
#include "core/gas/gas_optics_internal.inl"
#include "core/gas/gas_profile_internal.inl"
#include "raymath.h"
#include "rlgl.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define GAS_ATLAS_MAX_WIDTH 256
#define GAS_ATLAS_MAX_HEIGHT 128
#define GAS_MAX_SUBSTEPS 3

typedef struct GasShaderLocations {
    int atlasInvSize;
    int gridSize;
    int tilesX;
    int sceneDepth;
    int hasSceneDepth;
    int inverseProjection;
    int viewToWorld;
    int cameraPosition;
    int cameraForward;
    int orthographic;
    int volumeMin;
    int volumeMax;
    int steps;
    int densityScale;
    int bodyColor;
    int emissionColor;
    int emissionGain;
    int kind;
    int qualityTier;
    int bgLuma;
    int hasBgLuma;
    int bgAdapt;
    int detailStrength;
    int shadowStrength;
} GasShaderLocations;

static GasSim s_sim;
static GasSimConfig s_config;
static GasVolumeDesc s_desc;
static GasVolumeHandle s_handle;
static int s_nextHandle = 1;
static float s_lifetime;
static float s_accumulator;
static bool s_initialized;
static bool s_atlasDirty;
static bool s_prepared;
static unsigned char s_atlasPixels[GAS_ATLAS_MAX_WIDTH * GAS_ATLAS_MAX_HEIGHT * 4];
static Texture2D s_atlas;
static RenderTexture2D s_raymarchTarget;
static RenderTexture2D s_denoiseTarget;
static Shader s_raymarchShader;
static Shader s_denoiseShader;
static int s_denoiseTexelSizeLoc = -1;
static GasShaderLocations s_locations;
static GasPerfWindow s_perfWindow;
static GasPerfFrameSample s_perfFrame;
static GasPerfStats s_perfStats;
static bool s_perfFrameActive;
static bool s_perfTuningReady;
static float s_perfLog;
static float s_bgAdapt = 1.0f;
static GasOpticalControls s_optics;
static GasQualityProfile s_profile;
static int s_screenWidth;
static int s_screenHeight;
static int s_deferredTier = -1;

static Vector3 GasSystem_Color3(Color color) {
    return (Vector3){color.r / 255.0f, color.g / 255.0f, color.b / 255.0f};
}

static RenderTexture2D GasSystem_LoadColorTarget(int width, int height, int format) {
    RenderTexture2D target = {0};
    target.id = rlLoadFramebuffer();
    if (!target.id) return target;
    rlEnableFramebuffer(target.id);
    target.texture.id = rlLoadTexture(NULL, width, height, format, 1);
    target.texture.width = width;
    target.texture.height = height;
    target.texture.format = format;
    target.texture.mipmaps = 1;
    rlFramebufferAttach(target.id, target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0,
                        RL_ATTACHMENT_TEXTURE2D, 0);
    if (!rlFramebufferComplete(target.id))
        TraceLog(LOG_WARNING, "GasSystem: raymarch target incomplete");
    rlDisableFramebuffer();
    SetTextureFilter(target.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(target.texture, TEXTURE_WRAP_CLAMP);
    return target;
}

static Matrix GasSystem_MakeProjection(Camera3D camera) {
    float aspect = (float)GetScreenWidth() / (float)GetScreenHeight();
    if (camera.projection == CAMERA_ORTHOGRAPHIC) {
        double top = camera.fovy * 0.5;
        double right = top * aspect;
        return MatrixOrtho(-right, right, -top, top, 0.0001, 150.0);
    }
    double top = tan(camera.fovy * 0.5 * DEG2RAD);
    double right = top * aspect;
    return MatrixFrustum(-right, right, -top, top, 1.0, 1000.0);
}

static void GasSystem_CacheLocations(void) {
    s_locations.atlasInvSize = GetShaderLocation(s_raymarchShader, "u_atlasInvSize");
    s_locations.gridSize = GetShaderLocation(s_raymarchShader, "u_gridSize");
    s_locations.tilesX = GetShaderLocation(s_raymarchShader, "u_tilesX");
    s_locations.sceneDepth = GetShaderLocation(s_raymarchShader, "u_sceneDepthTex");
    s_locations.hasSceneDepth = GetShaderLocation(s_raymarchShader, "u_hasSceneDepth");
    s_locations.inverseProjection = GetShaderLocation(s_raymarchShader, "u_inverseProjection");
    s_locations.viewToWorld = GetShaderLocation(s_raymarchShader, "u_viewToWorld");
    s_locations.cameraPosition = GetShaderLocation(s_raymarchShader, "u_cameraPosition");
    s_locations.cameraForward = GetShaderLocation(s_raymarchShader, "u_cameraForward");
    s_locations.orthographic = GetShaderLocation(s_raymarchShader, "u_orthographic");
    s_locations.volumeMin = GetShaderLocation(s_raymarchShader, "u_volumeMin");
    s_locations.volumeMax = GetShaderLocation(s_raymarchShader, "u_volumeMax");
    s_locations.steps = GetShaderLocation(s_raymarchShader, "u_steps");
    s_locations.densityScale = GetShaderLocation(s_raymarchShader, "u_densityScale");
    s_locations.bodyColor = GetShaderLocation(s_raymarchShader, "u_bodyColor");
    s_locations.emissionColor = GetShaderLocation(s_raymarchShader, "u_emissionColor");
    s_locations.emissionGain = GetShaderLocation(s_raymarchShader, "u_emissionGain");
    s_locations.kind = GetShaderLocation(s_raymarchShader, "u_kind");
    s_locations.qualityTier = GetShaderLocation(s_raymarchShader, "u_qualityTier");
    s_locations.bgLuma = GetShaderLocation(s_raymarchShader, "u_bgLuma");
    s_locations.hasBgLuma = GetShaderLocation(s_raymarchShader, "u_hasBgLuma");
    s_locations.bgAdapt = GetShaderLocation(s_raymarchShader, "u_bgAdapt");
    s_locations.detailStrength = GetShaderLocation(s_raymarchShader, "u_detailStrength");
    s_locations.shadowStrength = GetShaderLocation(s_raymarchShader, "u_shadowStrength");
}

static void GasSystem_RebuildProfile(int requestedTier) {
    if (s_atlas.id) UnloadTexture(s_atlas);
    if (s_raymarchTarget.id) UnloadRenderTexture(s_raymarchTarget);
    if (s_denoiseTarget.id) UnloadRenderTexture(s_denoiseTarget);
    s_atlas = (Texture2D){0};
    s_raymarchTarget = (RenderTexture2D){0};
    s_denoiseTarget = (RenderTexture2D){0};

    s_profile = GasQualityProfile_Make(requestedTier, s_screenWidth, s_screenHeight);
    GasSim_Init(&s_sim, s_profile.gridWidth, s_profile.gridHeight,
                s_profile.gridDepth);
    memset(s_atlasPixels, 0, s_profile.atlasBytes);
    Image atlasImage = {0};
    atlasImage.data = s_atlasPixels;
    atlasImage.width = s_profile.atlasWidth;
    atlasImage.height = s_profile.atlasHeight;
    atlasImage.mipmaps = 1;
    atlasImage.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    s_atlas = LoadTextureFromImage(atlasImage);
    SetTextureFilter(s_atlas, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(s_atlas, TEXTURE_WRAP_CLAMP);

    int format = SceneTargets_IsHDR() ? RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16
                                      : RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    s_raymarchTarget = GasSystem_LoadColorTarget(s_profile.targetWidth,
                                                 s_profile.targetHeight, format);
    s_denoiseTarget = GasSystem_LoadColorTarget(s_profile.targetWidth,
                                                s_profile.targetHeight, format);
    s_atlasDirty = false;
    s_prepared = false;
    s_accumulator = 0.0f;
    GasSystem_ResetPerfStats();
    TraceLog(LOG_INFO,
             "GasSystem: tier %d, %dx%dx%d grid, %dx%d atlas (%u bytes), "
             "%dx%d target, %d steps",
             s_profile.effectiveTier, s_profile.gridWidth, s_profile.gridHeight,
             s_profile.gridDepth, s_profile.atlasWidth, s_profile.atlasHeight,
             s_profile.atlasBytes, s_profile.targetWidth, s_profile.targetHeight,
             s_profile.raymarchSteps);
}

static void GasSystem_ReconfigureIfIdle(void) {
    int requestedTier = (int)GfxQuality_Get();
    if (!GasQualityProfile_NeedsRebuild(s_profile, requestedTier)) {
        s_deferredTier = -1;
        return;
    }
    if (s_handle != GAS_VOLUME_INVALID) {
        int effective = requestedTier < 1 ? 1 :
                        (requestedTier > 3 ? 3 : requestedTier);
        if (effective != s_deferredTier) {
            s_deferredTier = effective;
            TraceLog(LOG_INFO,
                     "GasSystem: tier %d deferred until the active volume retires",
                     effective);
        }
        return;
    }
    GasSystem_RebuildProfile(requestedTier);
    s_deferredTier = -1;
}

static unsigned int GasSystem_UploadAtlas(void) {
    if (!s_atlasDirty || !s_atlas.id) return 0;
    memset(s_atlasPixels, 0, s_profile.atlasBytes);
    for (int z = 0; z < s_sim.depth; ++z) {
        int tileX = z % s_profile.atlasTilesX;
        int tileY = z / s_profile.atlasTilesX;
        for (int y = 0; y < s_sim.height; ++y) {
            for (int x = 0; x < s_sim.width; ++x) {
                int source = (z * s_sim.height + y) * s_sim.width + x;
                int atlasX = tileX * s_sim.width + x;
                int atlasY = tileY * s_sim.height + y;
                int target = (atlasY * s_atlas.width + atlasX) * 4;
                s_atlasPixels[target + 0] = (unsigned char)(fminf(1.0f, fmaxf(0.0f, s_sim.density[source])) * 255.0f + 0.5f);
                s_atlasPixels[target + 1] = (unsigned char)(fminf(1.0f, fmaxf(0.0f, s_sim.temperature[source])) * 255.0f + 0.5f);
                s_atlasPixels[target + 2] = (unsigned char)(fminf(1.0f, fmaxf(0.0f, s_sim.reaction[source])) * 255.0f + 0.5f);
                s_atlasPixels[target + 3] = 255;
            }
        }
    }
    UpdateTexture(s_atlas, s_atlasPixels);
    s_atlasDirty = false;
    return s_profile.atlasBytes;
}

static void GasSystem_EnsurePerfTuning(void) {
    if (s_perfTuningReady) return;
    s_perfTuningReady = true;
    const char *env = getenv("WUXING_GAS_PERF");
    s_perfLog = (env != NULL && env[0] != '\0' && env[0] != '0') ? 1.0f : 0.0f;
    Tuning_RegisterFloat("gas_perf_log", &s_perfLog, s_perfLog);
    Tuning_RegisterFloat("gas_bg_adapt", &s_bgAdapt, 1.0f);
}

GasVolumeDesc GasVolume_Preset(GasKind kind) {
    GasVolumeDesc desc = {0};
    desc.kind = kind;
    desc.priority = GAS_PRIORITY_CAST;
    desc.size = (Vector3){4.0f, 6.0f, 4.0f};
    desc.lifetime = 5.0f;
    desc.densityScale = 1.8f;
    desc.buoyancy = 3.0f;
    desc.smokeWeight = 0.2f;
    desc.turbulence = 2.4f;
    desc.densityDissipation = 0.35f;
    desc.temperatureDissipation = 1.0f;
    desc.reactionDissipation = 1.8f;
    if (kind == GAS_FIRE) {
        desc.bodyColor = (Color){72, 35, 22, 255};
        desc.emissionColor = (Color){255, 104, 18, 255};
        desc.emissionGain = 4.0f;
        desc.buoyancy = 4.5f;
        desc.turbulence = 3.6f;
        /* Keep reaction close enough to density that a rising flame retains
         * colour before handing off to smoke; the former 1.8 rate left only
         * 24% as much reaction as density after one second. */
        desc.reactionDissipation = 0.95f;
    } else if (kind == GAS_ENERGY) {
        desc.bodyColor = (Color){28, 38, 78, 255};
        desc.emissionColor = (Color){78, 176, 255, 255};
        desc.emissionGain = 3.0f;
        desc.buoyancy = 1.2f;
        desc.turbulence = 1.6f;
        desc.smokeWeight = 0.0f;
        desc.densityDissipation = 0.22f;
        desc.reactionDissipation = 0.65f;
    } else {
        desc.bodyColor = (Color){214, 218, 224, 255};
        desc.emissionColor = BLACK;
        desc.emissionGain = 0.0f;
    }
    GasOpticalControls optics = GasOpticalControls_Resolve((int)kind, 0.0f, 0.0f, 0.0f);
    desc.detailStrength = optics.detailStrength;
    desc.shadowStrength = optics.shadowStrength;
    desc.backgroundAdapt = optics.backgroundAdapt;
    return desc;
}

void GasSystem_Init(int width, int height) {
    if (s_initialized) return;
    s_screenWidth = width;
    s_screenHeight = height;
    s_config = GasSim_DefaultConfig();
    s_raymarchShader = ResourceManager_LoadShader(NULL, "core/gas/shaders/gas_volume.fs");
    s_denoiseShader = ResourceManager_LoadShader(NULL, "core/gas/shaders/gas_denoise.fs");
    s_denoiseTexelSizeLoc = GetShaderLocation(s_denoiseShader, "u_texelSize");
    GasSystem_CacheLocations();
    GasSystem_RebuildProfile((int)GfxQuality_Get());
    s_initialized = true;
}

GasVolumeHandle GasVolume_Create(const GasVolumeDesc *desc) {
    if (!s_initialized || desc == NULL) return GAS_VOLUME_INVALID;
    GasSystem_ReconfigureIfIdle();
    if (desc->size.x <= 0.0f || desc->size.y <= 0.0f || desc->size.z <= 0.0f)
        return GAS_VOLUME_INVALID;
    if (s_handle != GAS_VOLUME_INVALID && desc->priority < s_desc.priority)
        return GAS_VOLUME_INVALID;
    if (desc->priority < GAS_PRIORITY_ULTIMATE && GetFrameTime() > 0.030f)
        return GAS_VOLUME_INVALID;

    s_desc = *desc;
    s_optics = GasOpticalControls_Resolve((int)s_desc.kind,
                                          s_desc.detailStrength,
                                          s_desc.shadowStrength,
                                          s_desc.backgroundAdapt);
    s_desc.detailStrength = s_optics.detailStrength;
    s_desc.shadowStrength = s_optics.shadowStrength;
    s_desc.backgroundAdapt = s_optics.backgroundAdapt;
    if (s_desc.lifetime <= 0.0f) s_desc.lifetime = 5.0f;
    if (s_desc.densityScale <= 0.0f) s_desc.densityScale = 1.0f;
    s_lifetime = s_desc.lifetime;
    s_accumulator = 0.0f;
    s_config = GasSim_DefaultConfig();
    s_config.buoyancy = s_desc.buoyancy;
    s_config.smokeWeight = s_desc.smokeWeight;
    s_config.vorticityStrength = fmaxf(0.0f, s_desc.turbulence);
    s_config.densityDissipation = s_desc.densityDissipation;
    s_config.temperatureDissipation = s_desc.temperatureDissipation;
    s_config.reactionDissipation = s_desc.reactionDissipation;
    GasSim_Clear(&s_sim);
    s_handle = s_nextHandle++;
    if (s_nextHandle <= 0) s_nextHandle = 1;
    s_atlasDirty = true;
    s_prepared = false;
    return s_handle;
}

void GasVolume_Destroy(GasVolumeHandle handle) {
    if (handle == GAS_VOLUME_INVALID || handle != s_handle) return;
    s_handle = GAS_VOLUME_INVALID;
    s_lifetime = 0.0f;
    s_prepared = false;
    GasSim_Clear(&s_sim);
}

static void GasSystem_DenoiseRaymarch(void) {
    if (!s_raymarchTarget.id || !s_denoiseTarget.id || !s_denoiseShader.id)
        return;
    Vector2 texelSize = {
        1.0f / (float)s_raymarchTarget.texture.width,
        1.0f / (float)s_raymarchTarget.texture.height
    };
    BeginTextureMode(s_denoiseTarget);
    ClearBackground(BLANK);
    BeginShaderMode(s_denoiseShader);
    SetShaderValue(s_denoiseShader, s_denoiseTexelSizeLoc, &texelSize,
                   SHADER_UNIFORM_VEC2);
    rlDisableColorBlend();
    DrawTexturePro(s_raymarchTarget.texture,
                   (Rectangle){0, 0, (float)s_raymarchTarget.texture.width,
                               -(float)s_raymarchTarget.texture.height},
                   (Rectangle){0, 0, (float)s_denoiseTarget.texture.width,
                               (float)s_denoiseTarget.texture.height},
                   (Vector2){0, 0}, 0.0f, WHITE);
    rlDrawRenderBatchActive();
    rlEnableColorBlend();
    EndShaderMode();
    EndTextureMode();
}

bool GasVolume_IsAlive(GasVolumeHandle handle) {
    return handle != GAS_VOLUME_INVALID && handle == s_handle && s_lifetime > 0.0f;
}

void GasVolume_Inject(GasVolumeHandle handle, const GasInjection *injection) {
    if (!GasVolume_IsAlive(handle) || injection == NULL || injection->radius <= 0.0f)
        return;
    Vector3 minimum = Vector3Subtract(s_desc.center, Vector3Scale(s_desc.size, 0.5f));
    GasSimInjection source = {0};
    source.position.x = (injection->position.x - minimum.x) / s_desc.size.x;
    source.position.y = (injection->position.y - minimum.y) / s_desc.size.y;
    source.position.z = (injection->position.z - minimum.z) / s_desc.size.z;
    float shortest = fminf(s_desc.size.x, fminf(s_desc.size.y, s_desc.size.z));
    source.radius = injection->radius / shortest;
    source.velocity.x = injection->velocity.x * (float)(s_sim.width - 1) / s_desc.size.x;
    source.velocity.y = injection->velocity.y * (float)(s_sim.height - 1) / s_desc.size.y;
    source.velocity.z = injection->velocity.z * (float)(s_sim.depth - 1) / s_desc.size.z;
    source.density = injection->density;
    source.temperature = injection->temperature;
    source.reaction = injection->reaction;
    GasSim_InjectSphere(&s_sim, source);
    s_atlasDirty = true;
}

void GasSystem_Update(float dt) {
    if (!s_initialized) return;
    GasSystem_ReconfigureIfIdle();
    if (s_handle == GAS_VOLUME_INVALID || dt <= 0.0f) return;
    s_perfFrame = (GasPerfFrameSample){0};
    s_perfFrameActive = true;
    double updateStart = GetTime();
    if (dt > 0.25f) dt = 0.25f;
    s_lifetime -= dt;
    if (s_lifetime <= 0.0f) {
        GasVolume_Destroy(s_handle);
        s_perfFrame.updateCpuMs = (float)((GetTime() - updateStart) * 1000.0);
        return;
    }
    s_accumulator += dt;
    int substeps = 0;
    while (s_accumulator >= s_profile.fixedStep && substeps < GAS_MAX_SUBSTEPS) {
        GasSim_Step(&s_sim, s_profile.fixedStep, &s_config);
        s_accumulator -= s_profile.fixedStep;
        ++substeps;
        s_atlasDirty = true;
    }
    if (substeps == GAS_MAX_SUBSTEPS && s_accumulator > s_profile.fixedStep)
        s_accumulator = s_profile.fixedStep;
    if (GasSim_GetTotalDensity(&s_sim) < 0.001f && s_lifetime < s_desc.lifetime * 0.5f)
        GasVolume_Destroy(s_handle);
    s_perfFrame.simSubsteps = substeps;
    s_perfFrame.updateCpuMs = (float)((GetTime() - updateStart) * 1000.0);
}

bool GasSystem_HasPending(void) {
    return s_initialized && s_handle != GAS_VOLUME_INVALID &&
           GasSim_GetTotalDensity(&s_sim) > 0.001f;
}

void GasSystem_Prepare(Camera3D camera) {
    s_prepared = false;
    if (!GasSystem_HasPending() || !s_raymarchTarget.id || !s_raymarchShader.id) return;
    GasSystem_EnsurePerfTuning();
    if (!s_perfFrameActive) {
        s_perfFrame = (GasPerfFrameSample){0};
        s_perfFrameActive = true;
    }
    double uploadStart = GetTime();
    unsigned int uploadBytes = GasSystem_UploadAtlas();
    if (uploadBytes > 0) {
        s_perfFrame.atlasUploadCpuMs =
            (float)((GetTime() - uploadStart) * 1000.0);
        s_perfFrame.atlasUploadBytes = uploadBytes;
    }
    double raymarchStart = GetTime();

    Matrix projection = GasSystem_MakeProjection(camera);
    Matrix inverseProjection = MatrixInvert(projection);
    Matrix viewToWorld = MatrixInvert(GetCameraMatrix(camera));
    Vector3 cameraForward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 volumeHalf = Vector3Scale(s_desc.size, 0.5f);
    Vector3 volumeMin = Vector3Subtract(s_desc.center, volumeHalf);
    Vector3 volumeMax = Vector3Add(s_desc.center, volumeHalf);
    Vector3 gridSize = {(float)s_sim.width, (float)s_sim.height, (float)s_sim.depth};
    Vector2 atlasInvSize = {1.0f / (float)s_atlas.width,
                            1.0f / (float)s_atlas.height};
    Vector3 bodyColor = GasSystem_Color3(s_desc.bodyColor);
    Vector3 emissionColor = GasSystem_Color3(s_desc.emissionColor);
    Texture2D sceneDepth = SceneTargets_GetRawDepthTexture();
    int hasDepth = sceneDepth.id ? 1 : 0;
    int orthographic = camera.projection == CAMERA_ORTHOGRAPHIC ? 1 : 0;
    int tilesX = s_profile.atlasTilesX;
    int kind = (int)s_desc.kind;
    int qualityTier = s_profile.effectiveTier;
    Texture2D bgLuma = SceneTargets_GetBackgroundLuma();
    int hasBgLuma = bgLuma.id != 0 ? 1 : 0;
    float bgAdapt = fmaxf(0.0f, fminf(s_bgAdapt, 1.0f)) *
                    s_optics.backgroundAdapt;
    int steps = s_profile.raymarchSteps;

    BeginTextureMode(s_raymarchTarget);
    ClearBackground(BLANK);
    BeginShaderMode(s_raymarchShader);
    SetShaderValue(s_raymarchShader, s_locations.atlasInvSize, &atlasInvSize, SHADER_UNIFORM_VEC2);
    SetShaderValue(s_raymarchShader, s_locations.gridSize, &gridSize, SHADER_UNIFORM_VEC3);
    SetShaderValue(s_raymarchShader, s_locations.tilesX, &tilesX, SHADER_UNIFORM_INT);
    SetShaderValue(s_raymarchShader, s_locations.hasSceneDepth, &hasDepth, SHADER_UNIFORM_INT);
    SetShaderValueMatrix(s_raymarchShader, s_locations.inverseProjection, inverseProjection);
    SetShaderValueMatrix(s_raymarchShader, s_locations.viewToWorld, viewToWorld);
    SetShaderValue(s_raymarchShader, s_locations.cameraPosition, &camera.position, SHADER_UNIFORM_VEC3);
    SetShaderValue(s_raymarchShader, s_locations.cameraForward, &cameraForward, SHADER_UNIFORM_VEC3);
    SetShaderValue(s_raymarchShader, s_locations.orthographic, &orthographic, SHADER_UNIFORM_INT);
    SetShaderValue(s_raymarchShader, s_locations.volumeMin, &volumeMin, SHADER_UNIFORM_VEC3);
    SetShaderValue(s_raymarchShader, s_locations.volumeMax, &volumeMax, SHADER_UNIFORM_VEC3);
    SetShaderValue(s_raymarchShader, s_locations.steps, &steps, SHADER_UNIFORM_INT);
    SetShaderValue(s_raymarchShader, s_locations.densityScale, &s_desc.densityScale, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_raymarchShader, s_locations.bodyColor, &bodyColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(s_raymarchShader, s_locations.emissionColor, &emissionColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(s_raymarchShader, s_locations.emissionGain, &s_desc.emissionGain, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_raymarchShader, s_locations.kind, &kind, SHADER_UNIFORM_INT);
    SetShaderValue(s_raymarchShader, s_locations.qualityTier, &qualityTier, SHADER_UNIFORM_INT);
    SetShaderValue(s_raymarchShader, s_locations.hasBgLuma, &hasBgLuma, SHADER_UNIFORM_INT);
    SetShaderValue(s_raymarchShader, s_locations.bgAdapt, &bgAdapt, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_raymarchShader, s_locations.detailStrength,
                   &s_optics.detailStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_raymarchShader, s_locations.shadowStrength,
                   &s_optics.shadowStrength, SHADER_UNIFORM_FLOAT);
    if (hasBgLuma)
        SetShaderValueTexture(s_raymarchShader, s_locations.bgLuma, bgLuma);
    if (hasDepth)
        SetShaderValueTexture(s_raymarchShader, s_locations.sceneDepth, sceneDepth);
    rlDisableColorBlend();
    DrawTexturePro(s_atlas,
                   (Rectangle){0, 0, (float)s_atlas.width, (float)s_atlas.height},
                   (Rectangle){0, 0, (float)s_raymarchTarget.texture.width,
                               (float)s_raymarchTarget.texture.height},
                   (Vector2){0, 0}, 0.0f, WHITE);
    rlDrawRenderBatchActive();
    rlEnableColorBlend();
    EndShaderMode();
    EndTextureMode();
    GasSystem_DenoiseRaymarch();
    s_perfFrame.raymarchSubmitCpuMs =
        (float)((GetTime() - raymarchStart) * 1000.0);
    s_perfFrame.gridWidth = s_sim.width;
    s_perfFrame.gridHeight = s_sim.height;
    s_perfFrame.gridDepth = s_sim.depth;
    s_perfFrame.raymarchWidth = s_raymarchTarget.texture.width;
    s_perfFrame.raymarchHeight = s_raymarchTarget.texture.height;
    s_perfFrame.raymarchSteps = steps;
    s_prepared = true;
}

void GasSystem_Composite(void) {
    if (!s_prepared) return;
    s_prepared = false;
    double compositeStart = GetTime();
    Texture2D compositeTexture = s_denoiseTarget.id ? s_denoiseTarget.texture :
                                                       s_raymarchTarget.texture;
    BeginBlendMode(BLEND_ALPHA_PREMULTIPLY);
    DrawTexturePro(compositeTexture,
                   (Rectangle){0, 0, (float)compositeTexture.width,
                               -(float)compositeTexture.height},
                   (Rectangle){0, 0, (float)GetRenderWidth(), (float)GetRenderHeight()},
                   (Vector2){0, 0}, 0.0f, WHITE);
    EndBlendMode();
    s_perfFrame.compositeSubmitCpuMs =
        (float)((GetTime() - compositeStart) * 1000.0);

    GasSystem_EnsurePerfTuning();
    bool published = GasPerfWindow_Add(&s_perfWindow, &s_perfFrame,
                                       GetFrameTime(), &s_perfStats);
    s_perfFrameActive = false;
    if (published && s_perfLog >= 0.5f) {
        TraceLog(LOG_INFO,
                 "GAS perf: update %.3fms upload %.3fms ray-submit %.3fms "
                 "composite-submit %.3fms | substeps %.2f uploads %u/%u "
                 "bytes %llu | grid %dx%dx%d target %dx%d steps %d tap-max %llu",
                 s_perfStats.updateCpuMsAvg, s_perfStats.atlasUploadCpuMsAvg,
                 s_perfStats.raymarchSubmitCpuMsAvg,
                 s_perfStats.compositeSubmitCpuMsAvg, s_perfStats.simSubstepsAvg,
                 s_perfStats.atlasUploads, s_perfStats.sampleFrames,
                 s_perfStats.atlasUploadBytesTotal, s_perfStats.gridWidth,
                 s_perfStats.gridHeight, s_perfStats.gridDepth,
                 s_perfStats.raymarchWidth, s_perfStats.raymarchHeight,
                 s_perfStats.raymarchSteps,
                 s_perfStats.raymarchAtlasTapUpperBound);
    }
}

GasPerfStats GasSystem_GetPerfStats(void) {
    return s_perfStats;
}

void GasSystem_ResetPerfStats(void) {
    GasPerfWindow_Reset(&s_perfWindow);
    s_perfFrame = (GasPerfFrameSample){0};
    s_perfStats = (GasPerfStats){0};
    s_perfFrameActive = false;
}

void GasSystem_Unload(void) {
    if (!s_initialized) return;
    if (s_atlas.id) UnloadTexture(s_atlas);
    if (s_raymarchTarget.id) UnloadRenderTexture(s_raymarchTarget);
    if (s_denoiseTarget.id) UnloadRenderTexture(s_denoiseTarget);
    s_atlas = (Texture2D){0};
    s_raymarchTarget = (RenderTexture2D){0};
    s_denoiseTarget = (RenderTexture2D){0};
    s_handle = GAS_VOLUME_INVALID;
    s_initialized = false;
    s_prepared = false;
    s_profile = (GasQualityProfile){0};
    s_deferredTier = -1;
    GasSystem_ResetPerfStats();
}
