#include "core/liquid/liquid_metaball3d.h"
#include "core/resource_manager.h"
#include "core/scene_targets.h"
#include "core/vfx_render.h"
#include "core/geometry/procedural_mesh_utils.h"
#include "environment/environment_system.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

static Vector4 s_blobs[LIQUID_METABALL3D_MAX_BLOBS];
static int s_blobCount = 0;
static LiquidDesc s_activeMaterial = {0};
static float s_activeThreshold = 0.50f;
static bool s_hasCustomMaterial = false;

static Shader s_shader = {0};
static bool s_initialized = false;

static int s_locBlobs = -1;
static int s_locBlobCount = -1;
static int s_locBoxMin = -1;
static int s_locBoxMax = -1;
static int s_locThreshold = -1;
static int s_locCameraPos = -1;
static int s_locViewProj = -1;
static int s_locInvView = -1;
static int s_locResolution = -1;
static int s_locSceneColor = -1;
static int s_locSceneDepth = -1;
static int s_locHasSceneColor = -1;
static int s_locHasSceneDepth = -1;
static int s_locBodyColor = -1;
static int s_locGlowColor = -1;
static int s_locSoftColor = -1;
static int s_locIor = -1;
static int s_locRoughness = -1;
static int s_locOpacityPerMeter = -1;
static int s_locEmission = -1;
static int s_locLiquidClass = -1;
static int s_locSunDir = -1;
static int s_locSunColor = -1;
static int s_locSkyAmbient = -1;
static int s_locGroundAmbient = -1;
static int s_locTime = -1;
static int s_locCameraForward = -1;

void LiquidMetaball3D_Init(void)
{
    if (s_initialized) return;

    s_shader = ResourceManager_LoadShader("core/liquid/shaders/liquid_metaball3d.vs",
                                          "core/liquid/shaders/liquid_metaball3d.fs");
    if (s_shader.id == 0) return;

    s_locBlobs = GetShaderLocation(s_shader, "u_blobs[0]");
    if (s_locBlobs < 0) s_locBlobs = GetShaderLocation(s_shader, "u_blobs");
    s_locBlobCount = GetShaderLocation(s_shader, "u_blobCount");
    s_locBoxMin = GetShaderLocation(s_shader, "u_boxMin");
    s_locBoxMax = GetShaderLocation(s_shader, "u_boxMax");
    s_locThreshold = GetShaderLocation(s_shader, "u_threshold");
    s_locCameraPos = GetShaderLocation(s_shader, "u_cameraPos");
    s_locCameraForward = GetShaderLocation(s_shader, "u_cameraForward");
    s_locViewProj = GetShaderLocation(s_shader, "u_viewProj");
    s_locInvView = GetShaderLocation(s_shader, "u_invView");
    s_locResolution = GetShaderLocation(s_shader, "u_resolution");
    s_locSceneColor = GetShaderLocation(s_shader, "u_sceneColor");
    s_locSceneDepth = GetShaderLocation(s_shader, "u_sceneDepth");
    s_locHasSceneColor = GetShaderLocation(s_shader, "u_hasSceneColor");
    s_locHasSceneDepth = GetShaderLocation(s_shader, "u_hasSceneDepth");
    s_locBodyColor = GetShaderLocation(s_shader, "u_bodyColor");
    s_locGlowColor = GetShaderLocation(s_shader, "u_glowColor");
    s_locSoftColor = GetShaderLocation(s_shader, "u_softColor");
    s_locIor = GetShaderLocation(s_shader, "u_ior");
    s_locRoughness = GetShaderLocation(s_shader, "u_roughness");
    s_locOpacityPerMeter = GetShaderLocation(s_shader, "u_opacityPerMeter");
    s_locEmission = GetShaderLocation(s_shader, "u_emission");
    s_locLiquidClass = GetShaderLocation(s_shader, "u_liquidClass");
    s_locSunDir = GetShaderLocation(s_shader, "u_sunDir");
    s_locSunColor = GetShaderLocation(s_shader, "u_sunColor");
    s_locSkyAmbient = GetShaderLocation(s_shader, "u_skyAmbient");
    s_locGroundAmbient = GetShaderLocation(s_shader, "u_groundAmbient");
    s_locTime = GetShaderLocation(s_shader, "u_time");

    s_initialized = true;
}

void LiquidMetaball3D_Unload(void)
{
    s_initialized = false;
    s_blobCount = 0;
}

void LiquidMetaball3D_Reset(void)
{
    s_blobCount = 0;
    s_hasCustomMaterial = false;
    s_activeThreshold = 0.50f;
}

void LiquidMetaball3D_SetMaterial(const LiquidDesc *material)
{
    if (material) {
        s_activeMaterial = *material;
        s_hasCustomMaterial = true;
    }
}

void LiquidMetaball3D_SetThreshold(float threshold)
{
    if (threshold > 0.05f && threshold < 0.99f) {
        s_activeThreshold = threshold;
    }
}

void LiquidMetaball3D_RegisterBlob(Vector3 position, float radius)
{
    if (s_blobCount >= LIQUID_METABALL3D_MAX_BLOBS || radius <= 0.001f) return;
    s_blobs[s_blobCount++] = (Vector4){position.x, position.y, position.z, radius};
    SceneTargets_RequestSceneSnapshot();
    SceneTargets_RequestSoftDepthRegion((Rectangle){0, 0, (float)GetScreenWidth(), (float)GetScreenHeight()});
}

void LiquidMetaball3D_RegisterBlobs(const Vector3 *positions, const float *radii, int count)
{
    if (!positions || !radii || count <= 0) return;
    for (int i = 0; i < count; ++i) {
        if (s_blobCount >= LIQUID_METABALL3D_MAX_BLOBS) break;
        if (radii[i] > 0.001f) {
            s_blobs[s_blobCount++] = (Vector4){positions[i].x, positions[i].y, positions[i].z, radii[i]};
        }
    }
    SceneTargets_RequestSceneSnapshot();
    SceneTargets_RequestSoftDepthRegion((Rectangle){0, 0, (float)GetScreenWidth(), (float)GetScreenHeight()});
}

bool LiquidMetaball3D_HasBlobs(void)
{
    return s_blobCount > 0;
}

int LiquidMetaball3D_GetBlobCount(void)
{
    return s_blobCount;
}

void LiquidMetaball3D_Draw(Camera3D camera, const LiquidDesc *material, float threshold)
{
    if (!s_initialized) LiquidMetaball3D_Init();
    if (s_blobCount <= 0 || s_shader.id == 0) return;

    if (threshold <= 0.05f || threshold >= 0.99f) {
        threshold = s_activeThreshold;
    }
    if (!material && s_hasCustomMaterial) {
        material = &s_activeMaterial;
    }

    // Calculate AABB bounding box of all blobs
    Vector3 boxMin = (Vector3){s_blobs[0].x - s_blobs[0].w * 1.35f,
                              s_blobs[0].y - s_blobs[0].w * 1.35f,
                              s_blobs[0].z - s_blobs[0].w * 1.35f};
    Vector3 boxMax = (Vector3){s_blobs[0].x + s_blobs[0].w * 1.35f,
                              s_blobs[0].y + s_blobs[0].w * 1.35f,
                              s_blobs[0].z + s_blobs[0].w * 1.35f};

    for (int i = 1; i < s_blobCount; ++i) {
        float r = s_blobs[i].w * 1.35f;
        boxMin.x = fminf(boxMin.x, s_blobs[i].x - r);
        boxMin.y = fminf(boxMin.y, s_blobs[i].y - r);
        boxMin.z = fminf(boxMin.z, s_blobs[i].z - r);
        boxMax.x = fmaxf(boxMax.x, s_blobs[i].x + r);
        boxMax.y = fmaxf(boxMax.y, s_blobs[i].y + r);
        boxMax.z = fmaxf(boxMax.z, s_blobs[i].z + r);
    }

    Vector3 boxCenter = Vector3Scale(Vector3Add(boxMin, boxMax), 0.5f);
    Vector3 boxSize = Vector3Subtract(boxMax, boxMin);
    // Pad slightly to prevent ray clipping
    boxSize.x = fmaxf(boxSize.x + 0.15f, 0.4f);
    boxSize.y = fmaxf(boxSize.y + 0.15f, 0.4f);
    boxSize.z = fmaxf(boxSize.z + 0.15f, 0.4f);
    boxMin = Vector3Subtract(boxCenter, Vector3Scale(boxSize, 0.5f));
    boxMax = Vector3Add(boxCenter, Vector3Scale(boxSize, 0.5f));

    // Request snapshots from SceneTargets
    SceneTargets_RequestSceneSnapshot();
    SceneTargets_RequestSoftDepthRegion((Rectangle){0, 0, (float)GetScreenWidth(), (float)GetScreenHeight()});

    Texture2D sceneColor = SceneTargets_GetSceneSnapshotTexture();
    Texture2D sceneDepth = SceneTargets_GetDepthTexture();

    int hasSceneColor = (sceneColor.id != 0) ? 1 : 0;
    int hasSceneDepth = (sceneDepth.id != 0) ? 1 : 0;

    // View-projection matrix matching MyBeginMode3D's near=1.0 and far=1000.0
    float aspect = (float)GetScreenWidth() / (float)fmaxf((float)GetScreenHeight(), 1.0f);
    Matrix view = GetCameraMatrix(camera);
    double top = 1.0 * tan(camera.fovy * 0.5 * DEG2RAD);
    double right = top * aspect;
    Matrix proj = MatrixFrustum(-right, right, -top, top, 1.0f, 1000.0f);
    Matrix viewProj = MatrixMultiply(view, proj);
    Vector3 camForward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));

    Vector2 resolution = (Vector2){(float)SceneTargets_GetWidth(), (float)SceneTargets_GetHeight()};
    Vector3 sunDir = Vector3Normalize(Vector3Negate(Environment_GetSunDirection()));
    Color sunCol = Environment_GetSunColor();
    Vector3 sunColor = (Vector3){sunCol.r / 255.0f, sunCol.g / 255.0f, sunCol.b / 255.0f};
    sunColor = Vector3Scale(sunColor, Environment_GetSunIntensity());

    LiquidDesc defaultMat = LiquidSurface_ProfileDesc(LIQUID_MOTION_WATER);
    const LiquidDesc *mat = material ? material : (s_hasCustomMaterial ? &s_activeMaterial : &defaultMat);

    Vector4 bodyColor = ColorNormalize(mat->body);
    Vector4 glowColor = ColorNormalize(mat->glow);
    Vector4 softColor = ColorNormalize(mat->soft);
    float ior = mat->ior > 1.0f ? mat->ior : 1.333f;
    float roughness = mat->roughnessScale > 0.0f ? mat->roughnessScale * 0.20f : 0.20f;
    float opacity = mat->opacityPerMetre > 0.0f ? mat->opacityPerMetre : 1.8f;
    float emission = mat->emission;
    int liquidClass = (int)mat->liquidClass;
    float time = (float)GetTime();

    VFXRender_BeginPass(VFX_RENDER_PASS_BODY);
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ALPHA);

    // RULE: Under rlvk, Shader argument to SetShaderValue does not select program.
    // Activate target program with BeginShaderMode, keep uniform uploads + draws in scope, then end it.
    BeginShaderMode(s_shader);

    if (s_locBlobs >= 0) SetShaderValueV(s_shader, s_locBlobs, s_blobs, SHADER_UNIFORM_VEC4, s_blobCount);
    if (s_locBlobCount >= 0) SetShaderValue(s_shader, s_locBlobCount, &s_blobCount, SHADER_UNIFORM_INT);
    if (s_locBoxMin >= 0) SetShaderValue(s_shader, s_locBoxMin, &boxMin, SHADER_UNIFORM_VEC3);
    if (s_locBoxMax >= 0) SetShaderValue(s_shader, s_locBoxMax, &boxMax, SHADER_UNIFORM_VEC3);
    if (s_locThreshold >= 0) SetShaderValue(s_shader, s_locThreshold, &threshold, SHADER_UNIFORM_FLOAT);
    if (s_locCameraPos >= 0) SetShaderValue(s_shader, s_locCameraPos, &camera.position, SHADER_UNIFORM_VEC3);
    if (s_locCameraForward >= 0) SetShaderValue(s_shader, s_locCameraForward, &camForward, SHADER_UNIFORM_VEC3);
    Matrix invView = MatrixInvert(view);
    if (s_locViewProj >= 0) SetShaderValueMatrix(s_shader, s_locViewProj, viewProj);
    if (s_locInvView >= 0) SetShaderValueMatrix(s_shader, s_locInvView, invView);
    if (s_locResolution >= 0) SetShaderValue(s_shader, s_locResolution, &resolution, SHADER_UNIFORM_VEC2);

    if (s_locHasSceneColor >= 0) SetShaderValue(s_shader, s_locHasSceneColor, &hasSceneColor, SHADER_UNIFORM_INT);
    if (s_locHasSceneDepth >= 0) SetShaderValue(s_shader, s_locHasSceneDepth, &hasSceneDepth, SHADER_UNIFORM_INT);

    if (hasSceneColor && s_locSceneColor >= 0) SetShaderValueTexture(s_shader, s_locSceneColor, sceneColor);
    if (hasSceneDepth && s_locSceneDepth >= 0) SetShaderValueTexture(s_shader, s_locSceneDepth, sceneDepth);

    if (s_locBodyColor >= 0) SetShaderValue(s_shader, s_locBodyColor, &bodyColor, SHADER_UNIFORM_VEC4);
    if (s_locGlowColor >= 0) SetShaderValue(s_shader, s_locGlowColor, &glowColor, SHADER_UNIFORM_VEC4);
    if (s_locSoftColor >= 0) SetShaderValue(s_shader, s_locSoftColor, &softColor, SHADER_UNIFORM_VEC4);
    if (s_locIor >= 0) SetShaderValue(s_shader, s_locIor, &ior, SHADER_UNIFORM_FLOAT);
    if (s_locRoughness >= 0) SetShaderValue(s_shader, s_locRoughness, &roughness, SHADER_UNIFORM_FLOAT);
    if (s_locOpacityPerMeter >= 0) SetShaderValue(s_shader, s_locOpacityPerMeter, &opacity, SHADER_UNIFORM_FLOAT);
    if (s_locEmission >= 0) SetShaderValue(s_shader, s_locEmission, &emission, SHADER_UNIFORM_FLOAT);
    if (s_locLiquidClass >= 0) SetShaderValue(s_shader, s_locLiquidClass, &liquidClass, SHADER_UNIFORM_INT);

    if (s_locSunDir >= 0) SetShaderValue(s_shader, s_locSunDir, &sunDir, SHADER_UNIFORM_VEC3);
    if (s_locSunColor >= 0) SetShaderValue(s_shader, s_locSunColor, &sunColor, SHADER_UNIFORM_VEC3);

    Color skyAmbCol = Environment_GetSkyAmbient();
    Vector3 skyAmbient = (Vector3){skyAmbCol.r / 255.0f, skyAmbCol.g / 255.0f, skyAmbCol.b / 255.0f};
    Color gndAmbCol = Environment_GetGroundAmbient();
    Vector3 groundAmbient = (Vector3){gndAmbCol.r / 255.0f, gndAmbCol.g / 255.0f, gndAmbCol.b / 255.0f};
    if (s_locSkyAmbient >= 0) SetShaderValue(s_shader, s_locSkyAmbient, &skyAmbient, SHADER_UNIFORM_VEC3);
    if (s_locGroundAmbient >= 0) SetShaderValue(s_shader, s_locGroundAmbient, &groundAmbient, SHADER_UNIFORM_VEC3);

    if (s_locTime >= 0) SetShaderValue(s_shader, s_locTime, &time, SHADER_UNIFORM_FLOAT);

    bool camInside = (camera.position.x >= boxMin.x && camera.position.x <= boxMax.x &&
                      camera.position.y >= boxMin.y && camera.position.y <= boxMax.y &&
                      camera.position.z >= boxMin.z && camera.position.z <= boxMax.z);
    rlEnableBackfaceCulling();
    if (camInside) {
        rlSetCullFace(RL_CULL_FACE_FRONT);
    } else {
        rlSetCullFace(RL_CULL_FACE_BACK);
    }
    rlEnableDepthTest();
    rlDisableDepthMask();

    DrawCoreCube(boxCenter, boxSize.x, boxSize.y, boxSize.z, WHITE);

    rlSetCullFace(RL_CULL_FACE_BACK);
    rlEnableDepthMask();

    EndShaderMode();

    EndBlendMode();
    rlDrawRenderBatchActive();
    VFXRender_EndPass();

    // Consume blobs for this frame
    LiquidMetaball3D_Reset();
}
