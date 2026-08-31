#include "core/gas/gas_system.h"

#include "core/gas/gas_sim.h"
#include "core/gfx_quality.h"
#include "core/resource_manager.h"
#include "core/scene_targets.h"
#include "raymath.h"
#include "rlgl.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define GAS_ATLAS_WIDTH 256
#define GAS_ATLAS_HEIGHT 128
#define GAS_ATLAS_TILES_X 8
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
} GasShaderLocations;

static GasSim s_sim;
static GasSimConfig s_config;
static GasVolumeDesc s_desc;
static GasVolumeHandle s_handle;
static int s_nextHandle = 1;
static float s_lifetime;
static float s_accumulator;
static float s_fixedStep = 1.0f / 15.0f;
static bool s_initialized;
static bool s_atlasDirty;
static bool s_prepared;
static unsigned char s_atlasPixels[GAS_ATLAS_WIDTH * GAS_ATLAS_HEIGHT * 4];
static Texture2D s_atlas;
static RenderTexture2D s_raymarchTarget;
static Shader s_raymarchShader;
static GasShaderLocations s_locations;

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

static void GasSystem_SelectGrid(int *width, int *height, int *depth,
                                 int *renderDivisor, int *steps) {
    GfxQuality quality = GfxQuality_Get();
    if (quality >= GFX_HIGH) {
        *width = 28; *height = 32; *depth = 28;
        *renderDivisor = 3; *steps = 40;
        s_fixedStep = 1.0f / 20.0f;
    } else if (quality >= GFX_MED) {
        *width = 20; *height = 28; *depth = 20;
        *renderDivisor = 4; *steps = 24;
        s_fixedStep = 1.0f / 15.0f;
    } else {
        *width = 16; *height = 24; *depth = 16;
        *renderDivisor = 4; *steps = 16;
        s_fixedStep = 1.0f / 10.0f;
    }
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
}

static void GasSystem_UploadAtlas(void) {
    if (!s_atlasDirty || !s_atlas.id) return;
    memset(s_atlasPixels, 0, sizeof(s_atlasPixels));
    for (int z = 0; z < s_sim.depth; ++z) {
        int tileX = z % GAS_ATLAS_TILES_X;
        int tileY = z / GAS_ATLAS_TILES_X;
        for (int y = 0; y < s_sim.height; ++y) {
            for (int x = 0; x < s_sim.width; ++x) {
                int source = (z * s_sim.height + y) * s_sim.width + x;
                int atlasX = tileX * s_sim.width + x;
                int atlasY = tileY * s_sim.height + y;
                int target = (atlasY * GAS_ATLAS_WIDTH + atlasX) * 4;
                s_atlasPixels[target + 0] = (unsigned char)(fminf(1.0f, fmaxf(0.0f, s_sim.density[source])) * 255.0f + 0.5f);
                s_atlasPixels[target + 1] = (unsigned char)(fminf(1.0f, fmaxf(0.0f, s_sim.temperature[source])) * 255.0f + 0.5f);
                s_atlasPixels[target + 2] = (unsigned char)(fminf(1.0f, fmaxf(0.0f, s_sim.reaction[source])) * 255.0f + 0.5f);
                s_atlasPixels[target + 3] = 255;
            }
        }
    }
    UpdateTexture(s_atlas, s_atlasPixels);
    s_atlasDirty = false;
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
        desc.bodyColor = (Color){92, 98, 106, 255};
        desc.emissionColor = BLACK;
        desc.emissionGain = 0.0f;
    }
    return desc;
}

void GasSystem_Init(int width, int height) {
    if (s_initialized) return;
    int gridWidth, gridHeight, gridDepth, renderDivisor, unusedSteps;
    GasSystem_SelectGrid(&gridWidth, &gridHeight, &gridDepth,
                         &renderDivisor, &unusedSteps);
    GasSim_Init(&s_sim, gridWidth, gridHeight, gridDepth);
    s_config = GasSim_DefaultConfig();

    memset(s_atlasPixels, 0, sizeof(s_atlasPixels));
    Image atlasImage = {0};
    atlasImage.data = s_atlasPixels;
    atlasImage.width = GAS_ATLAS_WIDTH;
    atlasImage.height = GAS_ATLAS_HEIGHT;
    atlasImage.mipmaps = 1;
    atlasImage.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    s_atlas = LoadTextureFromImage(atlasImage);
    SetTextureFilter(s_atlas, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(s_atlas, TEXTURE_WRAP_CLAMP);

    int targetWidth = width / renderDivisor;
    int targetHeight = height / renderDivisor;
    if (targetWidth < 1) targetWidth = 1;
    if (targetHeight < 1) targetHeight = 1;
    int format = SceneTargets_IsHDR() ? RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16
                                      : RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    s_raymarchTarget = GasSystem_LoadColorTarget(targetWidth, targetHeight, format);
    s_raymarchShader = ResourceManager_LoadShader(NULL, "core/gas/shaders/gas_volume.fs");
    GasSystem_CacheLocations();
    s_initialized = true;
    TraceLog(LOG_INFO, "GasSystem: %dx%dx%d grid, %dx%d raymarch target",
             gridWidth, gridHeight, gridDepth, targetWidth, targetHeight);
}

GasVolumeHandle GasVolume_Create(const GasVolumeDesc *desc) {
    if (!s_initialized || desc == NULL) return GAS_VOLUME_INVALID;
    if (desc->size.x <= 0.0f || desc->size.y <= 0.0f || desc->size.z <= 0.0f)
        return GAS_VOLUME_INVALID;
    if (s_handle != GAS_VOLUME_INVALID && desc->priority < s_desc.priority)
        return GAS_VOLUME_INVALID;
    if (desc->priority < GAS_PRIORITY_ULTIMATE && GetFrameTime() > 0.030f)
        return GAS_VOLUME_INVALID;

    s_desc = *desc;
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
    if (!s_initialized || s_handle == GAS_VOLUME_INVALID || dt <= 0.0f) return;
    if (dt > 0.25f) dt = 0.25f;
    s_lifetime -= dt;
    if (s_lifetime <= 0.0f) {
        GasVolume_Destroy(s_handle);
        return;
    }
    s_accumulator += dt;
    int substeps = 0;
    while (s_accumulator >= s_fixedStep && substeps < GAS_MAX_SUBSTEPS) {
        GasSim_Step(&s_sim, s_fixedStep, &s_config);
        s_accumulator -= s_fixedStep;
        ++substeps;
        s_atlasDirty = true;
    }
    if (substeps == GAS_MAX_SUBSTEPS && s_accumulator > s_fixedStep)
        s_accumulator = s_fixedStep;
    if (GasSim_GetTotalDensity(&s_sim) < 0.001f && s_lifetime < s_desc.lifetime * 0.5f)
        GasVolume_Destroy(s_handle);
}

bool GasSystem_HasPending(void) {
    return s_initialized && s_handle != GAS_VOLUME_INVALID &&
           GasSim_GetTotalDensity(&s_sim) > 0.001f;
}

void GasSystem_Prepare(Camera3D camera) {
    s_prepared = false;
    if (!GasSystem_HasPending() || !s_raymarchTarget.id || !s_raymarchShader.id) return;
    GasSystem_UploadAtlas();

    Matrix projection = GasSystem_MakeProjection(camera);
    Matrix inverseProjection = MatrixInvert(projection);
    Matrix viewToWorld = MatrixInvert(GetCameraMatrix(camera));
    Vector3 cameraForward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 volumeHalf = Vector3Scale(s_desc.size, 0.5f);
    Vector3 volumeMin = Vector3Subtract(s_desc.center, volumeHalf);
    Vector3 volumeMax = Vector3Add(s_desc.center, volumeHalf);
    Vector3 gridSize = {(float)s_sim.width, (float)s_sim.height, (float)s_sim.depth};
    Vector2 atlasInvSize = {1.0f / GAS_ATLAS_WIDTH, 1.0f / GAS_ATLAS_HEIGHT};
    Vector3 bodyColor = GasSystem_Color3(s_desc.bodyColor);
    Vector3 emissionColor = GasSystem_Color3(s_desc.emissionColor);
    Texture2D sceneDepth = SceneTargets_GetRawDepthTexture();
    int hasDepth = sceneDepth.id ? 1 : 0;
    int orthographic = camera.projection == CAMERA_ORTHOGRAPHIC ? 1 : 0;
    int tilesX = GAS_ATLAS_TILES_X;
    int kind = (int)s_desc.kind;
    int qualityTier = (int)GfxQuality_Get();
    int gridWidth, gridHeight, gridDepth, divisor, steps;
    GasSystem_SelectGrid(&gridWidth, &gridHeight, &gridDepth, &divisor, &steps);
    (void)gridWidth; (void)gridHeight; (void)gridDepth; (void)divisor;

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
    if (hasDepth)
        SetShaderValueTexture(s_raymarchShader, s_locations.sceneDepth, sceneDepth);
    DrawTexturePro(s_atlas,
                   (Rectangle){0, 0, (float)s_atlas.width, (float)s_atlas.height},
                   (Rectangle){0, 0, (float)s_raymarchTarget.texture.width,
                               (float)s_raymarchTarget.texture.height},
                   (Vector2){0, 0}, 0.0f, WHITE);
    EndShaderMode();
    EndTextureMode();
    s_prepared = true;
}

void GasSystem_Composite(void) {
    if (!s_prepared) return;
    s_prepared = false;
    BeginBlendMode(BLEND_ALPHA_PREMULTIPLY);
    DrawTexturePro(s_raymarchTarget.texture,
                   (Rectangle){0, 0, (float)s_raymarchTarget.texture.width,
                               -(float)s_raymarchTarget.texture.height},
                   (Rectangle){0, 0, (float)GetRenderWidth(), (float)GetRenderHeight()},
                   (Vector2){0, 0}, 0.0f, WHITE);
    EndBlendMode();
}

void GasSystem_Unload(void) {
    if (!s_initialized) return;
    if (s_atlas.id) UnloadTexture(s_atlas);
    if (s_raymarchTarget.id) UnloadRenderTexture(s_raymarchTarget);
    s_atlas = (Texture2D){0};
    s_raymarchTarget = (RenderTexture2D){0};
    s_handle = GAS_VOLUME_INVALID;
    s_initialized = false;
    s_prepared = false;
}
