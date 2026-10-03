#include "core/volumetric/volumetric_fog.h"
#include "core/volumetric/volumetric_fog_distance.h"
#include "core/volumetric/fog_blue_noise.h"
#include "environment/environment_system.h"
#include "environment/env_shadow.h"
#include "core/scene_targets.h"
#include "core/gfx_quality.h"
#include "core/tuning.h"
#include "core/time_fx.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <stddef.h>

static bool s_enabled = true;
static bool s_ready = false;
static float s_tuningEnabled = 1.0f;
static bool s_tuningRegistered = false;
static float s_godRayIntensity = 1.0f;
static float s_distantCoverage = 1.0f;

static int s_fullWidth = 0;
static int s_fullHeight = 0;
static int s_lowWidth = 0;
static int s_lowHeight = 0;

static RenderTexture2D s_volumetricTarget;
static Shader          s_raymarchShader;
static Shader          s_compositeShader;
static Texture2D       s_jitterTex;

// Uniform locations — raymarch
static int s_locDepthTex;
static int s_locShadowMap;
static int s_locInvViewProj;
static int s_locLightVP;
static int s_locCamPos;
static int s_locSunDir;
static int s_locSunColor;
static int s_locFogColor;
static int s_locFogDensity;
static int s_locFogStart;
static int s_locFogFocus;
static int s_locFogForward;
static int s_locViewForward;
static int s_locFogSpan;
static int s_locDistantCoverage;
static int s_locHeightFalloff;
static int s_locBaseAltitude;
static int s_locSigmoidEnabled;
static int s_locSigmoidParams;
static int s_locMieAnisotropy;
static int s_locGodRayIntensity;
static int s_locMaxDist;
static int s_locStepCount;
static int s_locScreenRes;
static int s_locJitterTex;
static int s_locJitterTexel;
static int s_locTime;
static int s_locVolumeCount;
static int s_locVolPosShape;
static int s_locVolExtentsDense;
static int s_locVolColorSoft;

// Uniform locations — composite
static int s_locCompVolTex;
static int s_locCompFullDepth;
static int s_locCompLowDepth;
static int s_locCompLowTexel;
static int s_locCompDepthThreshold;

static inline Vector3 ColorToV3(Color c) {
    return (Vector3){ (float)c.r / 255.0f, (float)c.g / 255.0f, (float)c.b / 255.0f };
}

void VolumetricFog_Init(int width, int height) {
    if (s_ready) VolumetricFog_Unload();

    s_fullWidth = width;
    s_fullHeight = height;
    s_lowWidth = width / 2;
    s_lowHeight = height / 2;
    if (s_lowWidth < 1) s_lowWidth = 1;
    if (s_lowHeight < 1) s_lowHeight = 1;

    s_volumetricTarget = LoadRenderTexture(s_lowWidth, s_lowHeight);
    SetTextureFilter(s_volumetricTarget.texture, TEXTURE_FILTER_BILINEAR);

    // Image borrows immutable ranks; LoadTextureFromImage copies them to GPU.
    Image samplingImage = {.data = (void *)kFogBlueNoise,
        .width = FOG_BLUE_NOISE_SIZE, .height = FOG_BLUE_NOISE_SIZE,
        .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_GRAYSCALE};
    s_jitterTex = LoadTextureFromImage(samplingImage);
    if (s_jitterTex.id != 0) {
        SetTextureFilter(s_jitterTex, TEXTURE_FILTER_POINT);
        SetTextureWrap(s_jitterTex, TEXTURE_WRAP_REPEAT);
    }

    s_raymarchShader = LoadShader(NULL, "core/volumetric/shaders/volumetric_fog.fs");
    s_locDepthTex          = GetShaderLocation(s_raymarchShader, "u_depthTex");
    s_locShadowMap         = GetShaderLocation(s_raymarchShader, "u_shadowMap");
    s_locInvViewProj       = GetShaderLocation(s_raymarchShader, "u_invViewProj");
    s_locLightVP           = GetShaderLocation(s_raymarchShader, "u_lightVP");
    s_locCamPos            = GetShaderLocation(s_raymarchShader, "u_camPos");
    s_locSunDir            = GetShaderLocation(s_raymarchShader, "u_sunDir");
    s_locSunColor          = GetShaderLocation(s_raymarchShader, "u_sunColor");
    s_locFogColor          = GetShaderLocation(s_raymarchShader, "u_fogColor");
    s_locFogDensity        = GetShaderLocation(s_raymarchShader, "u_fogDensity");
    s_locFogStart          = GetShaderLocation(s_raymarchShader, "u_fogStart");
    s_locFogFocus = GetShaderLocation(s_raymarchShader, "u_fogFocus");
    s_locViewForward = GetShaderLocation(s_raymarchShader, "u_viewForward");
    s_locFogForward = GetShaderLocation(s_raymarchShader, "u_fogForward");
    s_locFogSpan = GetShaderLocation(s_raymarchShader, "u_fogSpan");
    s_locDistantCoverage = GetShaderLocation(s_raymarchShader, "u_distantCoverage");
    s_locHeightFalloff     = GetShaderLocation(s_raymarchShader, "u_heightFalloff");
    s_locBaseAltitude      = GetShaderLocation(s_raymarchShader, "u_baseAltitude");
    s_locSigmoidEnabled    = GetShaderLocation(s_raymarchShader, "u_sigmoidEnabled");
    s_locSigmoidParams     = GetShaderLocation(s_raymarchShader, "u_sigmoidParams");
    s_locMieAnisotropy     = GetShaderLocation(s_raymarchShader, "u_mieAnisotropy");
    s_locGodRayIntensity   = GetShaderLocation(s_raymarchShader, "u_godRayIntensity");
    s_locMaxDist           = GetShaderLocation(s_raymarchShader, "u_maxDist");
    s_locStepCount         = GetShaderLocation(s_raymarchShader, "u_stepCount");
    s_locScreenRes         = GetShaderLocation(s_raymarchShader, "u_screenResolution");
    s_locJitterTex         = GetShaderLocation(s_raymarchShader, "u_jitterTex");
    s_locJitterTexel       = GetShaderLocation(s_raymarchShader, "u_jitterTexel");
    s_locTime              = GetShaderLocation(s_raymarchShader, "u_time");
    s_locVolumeCount       = GetShaderLocation(s_raymarchShader, "u_volumeCount");
    s_locVolPosShape       = GetShaderLocation(s_raymarchShader, "u_volPosShape");
    if (s_locVolPosShape < 0) s_locVolPosShape = GetShaderLocation(s_raymarchShader, "u_volPosShape[0]");
    s_locVolExtentsDense   = GetShaderLocation(s_raymarchShader, "u_volExtentsDense");
    if (s_locVolExtentsDense < 0) s_locVolExtentsDense = GetShaderLocation(s_raymarchShader, "u_volExtentsDense[0]");
    s_locVolColorSoft      = GetShaderLocation(s_raymarchShader, "u_volColorSoft");
    if (s_locVolColorSoft < 0) s_locVolColorSoft = GetShaderLocation(s_raymarchShader, "u_volColorSoft[0]");

    s_compositeShader = LoadShader(NULL, "core/volumetric/shaders/volumetric_composite.fs");
    s_locCompVolTex        = GetShaderLocation(s_compositeShader, "u_volumetricTex");
    s_locCompFullDepth     = GetShaderLocation(s_compositeShader, "u_fullResDepthTex");
    s_locCompLowDepth      = GetShaderLocation(s_compositeShader, "u_lowResDepthTex");
    s_locCompLowTexel      = GetShaderLocation(s_compositeShader, "u_lowResTexel");
    s_locCompDepthThreshold= GetShaderLocation(s_compositeShader, "u_depthThreshold");

    s_ready = true;
}

void VolumetricFog_Unload(void) {
    if (!s_ready) return;
    UnloadRenderTexture(s_volumetricTarget);
    UnloadShader(s_raymarchShader);
    UnloadShader(s_compositeShader);
    if (s_jitterTex.id != 0) UnloadTexture(s_jitterTex);
    s_jitterTex = (Texture2D){0};
    s_ready = false;
}

void VolumetricFog_Resize(int width, int height) {
    if (!s_ready) return;
    VolumetricFog_Init(width, height);
}

bool VolumetricFog_IsEnabled(void) { return s_enabled && s_tuningEnabled > 0.5f; }
void VolumetricFog_SetEnabled(bool enabled) { s_enabled = enabled; }

void  VolumetricFog_SetGodRayIntensity(float intensity) { s_godRayIntensity = intensity; }
float VolumetricFog_GetGodRayIntensity(void) { return s_godRayIntensity; }

void VolumetricFog_SetDistantCoverage(float areaRatio) {
    s_distantCoverage = VolumetricFog_ClampDistantCoverage(areaRatio);
}
float VolumetricFog_GetDistantCoverage(void) { return s_distantCoverage; }

void VolumetricFog_PreFrame(void) {
    if (!s_tuningRegistered) {
        s_tuningRegistered = true;
        Tuning_RegisterFloat("volumetric_fog_enabled", &s_tuningEnabled, 1.0f);
    }
    if (!s_ready || !VolumetricFog_IsEnabled()) return;
    if (GfxQuality_Get() <= GFX_LOW) return;
    AtmosphereProfile atmos = Environment_GetAtmosphereProfile();
    if (!atmos.enabled) return;
    SceneTargets_RequestSoftDepthRegion((Rectangle){ 0, 0, (float)s_fullWidth, (float)s_fullHeight });
}

void VolumetricFog_Render(Camera3D camera) {
    if (!s_ready || !VolumetricFog_IsEnabled()) return;

    GfxQuality tier = GfxQuality_Get();
    if (tier <= GFX_LOW) return; // Keep mobile/low-end lightweight (runs forward height fog instead)

    AtmosphereProfile atmos = Environment_GetAtmosphereProfile();
    if (!atmos.enabled) return;

    Texture2D sceneDepth = SceneTargets_GetDepthTexture();
    Texture2D shadowMap = EnvShadow_GetShadowMap();
    Matrix lightVP = EnvShadow_GetLightVP();

    // Compute camera inverse View-Projection matrix matching main.c MyBeginMode3D
    Matrix matView = MatrixLookAt(camera.position, camera.target, camera.up);
    float aspect = (float)s_fullWidth / (float)s_fullHeight;
    Matrix matProj;
    if (camera.projection == CAMERA_ORTHOGRAPHIC) {
        double top = camera.fovy * 0.5;
        double right = top * aspect;
        matProj = MatrixOrtho(-right, right, -top, top, 0.0001, 150.0);
    } else {
        double top = tan(camera.fovy * 0.5 * DEG2RAD);
        double right = top * aspect;
        matProj = MatrixFrustum(-right, right, -top, top, 1.0, 1000.0);
    }
    Matrix matViewProj = MatrixMultiply(matView, matProj);
    Matrix invViewProj = MatrixInvert(matViewProj);

    Vector3 sunDir   = Environment_GetSunDirection();
    Vector3 sunColor = ColorToV3(Environment_GetSunColor());
    Vector3 fogColor = ColorToV3(atmos.color);

    int stepCount = (tier >= GFX_HIGH) ? 20 : 14;
    float maxDist = (atmos.end > 0.0f) ? atmos.end : 120.0f;
    float fogStart = VolumetricFog_EffectiveStart(
        atmos.start, Vector3Distance(camera.position, camera.target));
    Vector3 viewForward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 fogForward = Vector3Normalize((Vector3){viewForward.x, 0.0f, viewForward.z});
    float halfViewHeight = camera.projection == CAMERA_ORTHOGRAPHIC ? camera.fovy * 0.5f
        : Vector3Distance(camera.position, camera.target) * tanf(camera.fovy * DEG2RAD * 0.5f);
    float fogSpan = atmos.start > 3.0f
        ? VolumetricFog_GroundSpan(halfViewHeight, viewForward.y) : 0.0f;
    Vector2 screenRes = { (float)s_lowWidth, (float)s_lowHeight };
    float godRay = s_godRayIntensity;
    float time = TimeFX_Elapsed();

    float heightFalloff  = atmos.density.heightFalloff;
    float baseAltitude   = atmos.density.baseAltitude;
    float sigmoidEnabled = atmos.density.enableSigmoidLayer ? 1.0f : 0.0f;
    Vector3 sigmoidParams= { atmos.density.layerAltitude, atmos.density.layerThickness, atmos.density.layerDensity };
    float mieAniso       = atmos.optics.mieAnisotropy;
    float density        = atmos.density.baseDensity;

    // Truy xuất tối đa 4 khối sương mù cục bộ (hồ nước, hoa, rừng, skill VFX)
    // Ưu tiên các khối sương mù nhất thời (transient VFX) để chiêu thức luôn hiển thị
    Vector4 volPosShape[4] = {0};
    Vector4 volExtentsDense[4] = {0};
    Vector4 volColorSoft[4] = {0};
    int volCount = 0;
    int totalActive = FogVolume_GetActiveCount();

    // 1. Nhặt các khối sương nhất thời (transient / skill VFX) với fade-in/fade-out mượt mà
    for (int i = 0; i < totalActive && volCount < 4; i++) {
        const LocalFogVolume *v = FogVolume_GetByIndex(i);
        if (v && v->maxLifetime > 0.0f) {
            float effDensity = v->density;
            float lifeRatio = v->lifetime / v->maxLifetime;
            float fadeIn = (lifeRatio > 0.85f) ? (1.0f - lifeRatio) / 0.15f : 1.0f;
            float fadeOut = (lifeRatio < 0.25f) ? (lifeRatio / 0.25f) : 1.0f;
            effDensity *= (fadeIn < fadeOut ? fadeIn : fadeOut);

            volPosShape[volCount] = (Vector4){ v->position.x, v->position.y, v->position.z, (float)v->shape };
            volExtentsDense[volCount] = (Vector4){ v->extents.x, v->extents.y, v->extents.z, effDensity };
            volColorSoft[volCount] = (Vector4){ (float)v->color.r / 255.0f, (float)v->color.g / 255.0f, (float)v->color.b / 255.0f, v->edgeSoftness };
            volCount++;
        }
    }

    // 2. Nhặt tiếp các khối sương cố định (hồ nước, hoa, rừng) cho đến khi đủ 4
    for (int i = 0; i < totalActive && volCount < 4; i++) {
        const LocalFogVolume *v = FogVolume_GetByIndex(i);
        if (v && v->maxLifetime <= 0.0f) {
            volPosShape[volCount] = (Vector4){ v->position.x, v->position.y, v->position.z, (float)v->shape };
            volExtentsDense[volCount] = (Vector4){ v->extents.x, v->extents.y, v->extents.z, v->density };
            volColorSoft[volCount] = (Vector4){ (float)v->color.r / 255.0f, (float)v->color.g / 255.0f, (float)v->color.b / 255.0f, v->edgeSoftness };
            volCount++;
        }
    }

    // --- PASS 1: Low-Res Volumetric Raymarch ---
    BeginTextureMode(s_volumetricTarget);
    ClearBackground(BLANK);

    BeginShaderMode(s_raymarchShader);
    Vector2 jitterTexel = {0};
    if (s_jitterTex.id != 0) {
        jitterTexel = (Vector2){1.0f / s_jitterTex.width, 1.0f / s_jitterTex.height};
        if (s_locJitterTex >= 0)
            SetShaderValueTexture(s_raymarchShader, s_locJitterTex, s_jitterTex);
    }
    if (s_locJitterTexel >= 0)
        SetShaderValue(s_raymarchShader, s_locJitterTexel, &jitterTexel, SHADER_UNIFORM_VEC2);
    SetShaderValueMatrix(s_raymarchShader, s_locInvViewProj, invViewProj);
    SetShaderValueMatrix(s_raymarchShader, s_locLightVP, lightVP);
    SetShaderValue(s_raymarchShader, s_locCamPos, &camera.position, SHADER_UNIFORM_VEC3);
    SetShaderValue(s_raymarchShader, s_locSunDir, &sunDir, SHADER_UNIFORM_VEC3);
    SetShaderValue(s_raymarchShader, s_locSunColor, &sunColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(s_raymarchShader, s_locFogColor, &fogColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(s_raymarchShader, s_locFogDensity, &density, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_raymarchShader, s_locFogStart, &fogStart, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_raymarchShader, s_locFogFocus, &camera.target, SHADER_UNIFORM_VEC3);
    SetShaderValue(s_raymarchShader, s_locFogForward, &fogForward, SHADER_UNIFORM_VEC3);
    SetShaderValue(s_raymarchShader, s_locViewForward, &viewForward, SHADER_UNIFORM_VEC3);
    SetShaderValue(s_raymarchShader, s_locFogSpan, &fogSpan, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_raymarchShader, s_locDistantCoverage, &s_distantCoverage, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_raymarchShader, s_locHeightFalloff, &heightFalloff, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_raymarchShader, s_locBaseAltitude, &baseAltitude, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_raymarchShader, s_locSigmoidEnabled, &sigmoidEnabled, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_raymarchShader, s_locSigmoidParams, &sigmoidParams, SHADER_UNIFORM_VEC3);
    SetShaderValue(s_raymarchShader, s_locMieAnisotropy, &mieAniso, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_raymarchShader, s_locGodRayIntensity, &godRay, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_raymarchShader, s_locMaxDist, &maxDist, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_raymarchShader, s_locStepCount, &stepCount, SHADER_UNIFORM_INT);
    SetShaderValue(s_raymarchShader, s_locScreenRes, &screenRes, SHADER_UNIFORM_VEC2);
    if (s_locTime >= 0) SetShaderValue(s_raymarchShader, s_locTime, &time, SHADER_UNIFORM_FLOAT);
    if (s_locVolumeCount >= 0) SetShaderValue(s_raymarchShader, s_locVolumeCount, &volCount, SHADER_UNIFORM_INT);
    if (s_locVolPosShape >= 0) SetShaderValueV(s_raymarchShader, s_locVolPosShape, volPosShape, SHADER_UNIFORM_VEC4, 4);
    if (s_locVolExtentsDense >= 0) SetShaderValueV(s_raymarchShader, s_locVolExtentsDense, volExtentsDense, SHADER_UNIFORM_VEC4, 4);
    if (s_locVolColorSoft >= 0) SetShaderValueV(s_raymarchShader, s_locVolColorSoft, volColorSoft, SHADER_UNIFORM_VEC4, 4);

    SetShaderValueTexture(s_raymarchShader, s_locDepthTex, sceneDepth);
    SetShaderValueTexture(s_raymarchShader, s_locShadowMap, shadowMap);

    Rectangle srcRect = { 0.0f, 0.0f, (float)sceneDepth.width, -(float)sceneDepth.height };
    Rectangle dstRect = { 0.0f, 0.0f, (float)s_lowWidth, (float)s_lowHeight };
    DrawTexturePro(sceneDepth, srcRect, dstRect, (Vector2){ 0, 0 }, 0.0f, WHITE);

    EndShaderMode();
    EndTextureMode();

    // --- PASS 2: Depth-Aware Bilateral Upsample & Composite into Scene Target ---
    SceneTargets_BeginVFXBody();

    BeginBlendMode(BLEND_ALPHA_PREMULTIPLY);
    BeginShaderMode(s_compositeShader);

    Vector2 lowTexel = { 1.0f / (float)s_lowWidth, 1.0f / (float)s_lowHeight };
    float depthThreshold = 2.0f;
    SetShaderValue(s_compositeShader, s_locCompLowTexel, &lowTexel, SHADER_UNIFORM_VEC2);
    SetShaderValue(s_compositeShader, s_locCompDepthThreshold, &depthThreshold, SHADER_UNIFORM_FLOAT);

    SetShaderValueTexture(s_compositeShader, s_locCompVolTex, s_volumetricTarget.texture);
    SetShaderValueTexture(s_compositeShader, s_locCompFullDepth, sceneDepth);
    SetShaderValueTexture(s_compositeShader, s_locCompLowDepth, sceneDepth);

    Rectangle volSrcRect = { 0.0f, 0.0f, (float)s_lowWidth, -(float)s_lowHeight };
    Rectangle fullDstRect = { 0.0f, 0.0f, (float)s_fullWidth, (float)s_fullHeight };
    DrawTexturePro(s_volumetricTarget.texture, volSrcRect, fullDstRect, (Vector2){ 0, 0 }, 0.0f, WHITE);

    EndShaderMode();
    EndBlendMode();

    SceneTargets_EndVFXLayer();
}
