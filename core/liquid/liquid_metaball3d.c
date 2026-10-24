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

typedef struct {
    Vector4 blobs[LIQUID_METABALL3D_MAX_BLOBS];
    int count;
    LiquidDesc material;
    float threshold;
    bool hasMaterial;
    bool hasThreshold;
} LiquidMetaball3DBody;

static LiquidMetaball3DBody s_bodies[LIQUID_METABALL3D_MAX_BODIES];
static int s_bodyCount = 0;
static int s_currentBody = 0;
static LiquidDesc s_fallbackMaterial = {0};
static float s_fallbackThreshold = 0.50f;
static bool s_hasFallbackMaterial = false;

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
static int s_locStepCount = -1;

void LiquidMetaball3D_Init(void)
{
    if (s_initialized) return;

    s_shader = ResourceManager_LoadShader("core/liquid/shaders/liquid_metaball3d.vs",
                                          "core/liquid/shaders/liquid_metaball3d.fs");
    if (s_shader.id == 0 || s_shader.id == rlGetShaderIdDefault()) return;

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
    s_locStepCount = GetShaderLocation(s_shader, "u_stepCount");

    s_initialized = true;
}

void LiquidMetaball3D_Unload(void)
{
    s_initialized = false;
    s_bodyCount = 0;
    s_currentBody = 0;
}

void LiquidMetaball3D_Reset(void)
{
    s_bodyCount = 0;
    s_currentBody = 0;
    s_hasFallbackMaterial = false;
    s_fallbackThreshold = 0.50f;
}

static LiquidMetaball3DBody *LiquidMetaball3D_CurrentBody(void)
{
    if (s_bodyCount <= 0) {
        s_bodyCount = 1;
        s_bodies[0].count = 0;
        s_bodies[0].hasMaterial = false;
        s_bodies[0].hasThreshold = false;
        s_bodies[0].threshold = 0.50f;
    }
    if (s_currentBody < 0) s_currentBody = 0;
    if (s_currentBody >= s_bodyCount) s_currentBody = s_bodyCount - 1;
    return &s_bodies[s_currentBody];
}

int LiquidMetaball3D_BeginBody(const LiquidDesc *material, float threshold)
{
    int index;
    if (s_bodyCount <= 0) {
        index = 0;
        s_bodyCount = 1;
    } else if (s_bodies[s_bodyCount - 1].count == 0) {
        /* Reuse an empty tail: a body begun and then abandoned (a rejected
         * spawn) must not consume a slot for the rest of the frame. */
        index = s_bodyCount - 1;
    } else if (s_bodyCount < LIQUID_METABALL3D_MAX_BODIES) {
        index = s_bodyCount++;
    } else {
        /* Out of slots: append to the last body instead of silently dropping
         * the geometry. Its optics stay the last body's, which is wrong but
         * visible — dropping is invisible and reads as a rendering bug. */
        index = LIQUID_METABALL3D_MAX_BODIES - 1;
    }

    LiquidMetaball3DBody *body = &s_bodies[index];
    body->count = 0;
    body->hasMaterial = (material != NULL);
    if (material) body->material = *material;
    body->hasThreshold = (threshold > 0.05f && threshold < 0.99f);
    body->threshold = body->hasThreshold ? threshold : 0.50f;
    s_currentBody = index;
    return index;
}

void LiquidMetaball3D_EndBody(void)
{
    /* Nothing to flush: blobs are already stored per body, and the next
     * BeginBody moves past this one because it is no longer empty. Kept as an
     * explicit terminator so a caller does not have to know that. */
}

void LiquidMetaball3D_SetMaterial(const LiquidDesc *material)
{
    if (material) {
        s_fallbackMaterial = *material;
        s_hasFallbackMaterial = true;
    }
}

void LiquidMetaball3D_SetThreshold(float threshold)
{
    if (threshold > 0.05f && threshold < 0.99f) {
        s_fallbackThreshold = threshold;
    }
}

void LiquidMetaball3D_RegisterBlob(Vector3 position, float radius)
{
    if (radius <= 0.001f) return;
    LiquidMetaball3DBody *body = LiquidMetaball3D_CurrentBody();
    if (body->count >= LIQUID_METABALL3D_MAX_BLOBS) return;
    body->blobs[body->count++] = (Vector4){position.x, position.y, position.z, radius};
}

void LiquidMetaball3D_RegisterBlobs(const Vector3 *positions, const float *radii, int count)
{
    if (!positions || !radii || count <= 0) return;
    for (int i = 0; i < count; ++i) {
        LiquidMetaball3D_RegisterBlob(positions[i], radii[i]);
    }
}

bool LiquidMetaball3D_HasBlobs(void)
{
    for (int i = 0; i < s_bodyCount; ++i)
        if (s_bodies[i].count > 0) return true;
    return false;
}

int LiquidMetaball3D_GetBlobCount(void)
{
    int total = 0;
    for (int i = 0; i < s_bodyCount; ++i) total += s_bodies[i].count;
    return total;
}

/* Bounding box and coarse march resolution for ONE body. */
static void LiquidMetaball3D_BodyBounds(const LiquidMetaball3DBody *body,
                                        Vector3 *outMin, Vector3 *outMax,
                                        Vector3 *outCenter, Vector3 *outSize,
                                        int *outSteps)
{
    const Vector4 *blob = body->blobs;
    const float pad = 1.35f;
    float minRadius = blob[0].w;
    Vector3 boxMin = (Vector3){blob[0].x - blob[0].w * pad,
                               blob[0].y - blob[0].w * pad,
                               blob[0].z - blob[0].w * pad};
    Vector3 boxMax = (Vector3){blob[0].x + blob[0].w * pad,
                               blob[0].y + blob[0].w * pad,
                               blob[0].z + blob[0].w * pad};

    for (int i = 1; i < body->count; ++i) {
        float r = blob[i].w * pad;
        minRadius = fminf(minRadius, blob[i].w);
        boxMin.x = fminf(boxMin.x, blob[i].x - r);
        boxMin.y = fminf(boxMin.y, blob[i].y - r);
        boxMin.z = fminf(boxMin.z, blob[i].z - r);
        boxMax.x = fmaxf(boxMax.x, blob[i].x + r);
        boxMax.y = fmaxf(boxMax.y, blob[i].y + r);
        boxMax.z = fmaxf(boxMax.z, blob[i].z + r);
    }

    Vector3 boxCenter = Vector3Scale(Vector3Add(boxMin, boxMax), 0.5f);
    Vector3 boxSize = Vector3Subtract(boxMax, boxMin);
    // Pad slightly to prevent ray clipping
    boxSize.x = fmaxf(boxSize.x + 0.15f, 0.4f);
    boxSize.y = fmaxf(boxSize.y + 0.15f, 0.4f);
    boxSize.z = fmaxf(boxSize.z + 0.15f, 0.4f);

    /* Coarse march resolution derived from the SMALLEST blob, then refined by
     * the bisection in the fragment shader.
     *
     * A fixed 48 steps across a 1.2 m body sampled the field every 2.5 cm even
     * though the field has no feature smaller than a 0.2 m kernel — six times
     * the field evaluations for the same crossing. 0.75 * the smallest radius
     * cannot step over a kernel's own support, and the 5-step bisection then
     * resolves the crossing to span/12/32.
     *
     * The box DIAGONAL, not the per-pixel segment, so the count is stable
     * frame to frame: a per-pixel count would make the surface's sampling
     * shimmer as the camera moves. */
    float diagonal = sqrtf(boxSize.x * boxSize.x + boxSize.y * boxSize.y +
                           boxSize.z * boxSize.z);
    float step = fmaxf(minRadius * 0.75f, 0.02f);
    int steps = (int)ceilf(diagonal / step);
    if (steps < 12) steps = 12;
    if (steps > 48) steps = 48;

    *outMin = boxMin;
    *outMax = boxMax;
    *outCenter = boxCenter;
    *outSize = boxSize;
    *outSteps = steps;
}

void LiquidMetaball3D_Draw(Camera3D camera, const LiquidDesc *material, float threshold)
{
    if (!s_initialized) LiquidMetaball3D_Init();
    if (s_shader.id == 0 || !LiquidMetaball3D_HasBlobs()) {
        LiquidMetaball3D_Reset();
        return;
    }

    const LiquidDesc *fallback = material ? material
                                : (s_hasFallbackMaterial ? &s_fallbackMaterial : NULL);
    float fallbackThreshold = (threshold > 0.05f && threshold < 0.99f)
                                  ? threshold : s_fallbackThreshold;
    LiquidDesc defaultMat = LiquidSurface_ProfileDesc(LIQUID_MOTION_WATER);

    // Request snapshots ONCE for the whole frame, not once per blob.
    SceneTargets_RequestSceneSnapshot();
    SceneTargets_RequestSoftDepthRegion((Rectangle){0, 0, (float)GetScreenWidth(), (float)GetScreenHeight()});

    Texture2D sceneColor = SceneTargets_GetSceneSnapshotTexture();
    Texture2D sceneDepth = SceneTargets_GetDepthTexture();

    int hasSceneColor = (sceneColor.id != 0) ? 1 : 0;
    int hasSceneDepth = (sceneDepth.id != 0) ? 1 : 0;

    // View-projection matrix matching MyBeginMode3D's near=1.0 and far=1000.0
    float aspect = (float)GetScreenWidth() / (float)fmaxf((float)GetScreenHeight(), 1.0f);
    Matrix view = GetCameraMatrix(camera);
    double top = tan(camera.fovy * 0.5 * DEG2RAD);
    double right = top * aspect;
    Matrix proj = MatrixFrustum(-right, right, -top, top, 1.0f, 1000.0f);
    Matrix viewProj = MatrixMultiply(view, proj);
    Matrix invView = MatrixInvert(view);
    Vector3 camForward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));

    Vector2 resolution = (Vector2){(float)SceneTargets_GetWidth(), (float)SceneTargets_GetHeight()};
    Vector3 sunDir = Vector3Normalize(Vector3Negate(Environment_GetSunDirection()));
    Color sunCol = Environment_GetSunColor();
    Vector3 sunColor = (Vector3){sunCol.r / 255.0f, sunCol.g / 255.0f, sunCol.b / 255.0f};
    sunColor = Vector3Scale(sunColor, Environment_GetSunIntensity());

    Color skyAmbCol = Environment_GetSkyAmbient();
    Vector3 skyAmbient = (Vector3){skyAmbCol.r / 255.0f, skyAmbCol.g / 255.0f, skyAmbCol.b / 255.0f};
    Color gndAmbCol = Environment_GetGroundAmbient();
    Vector3 groundAmbient = (Vector3){gndAmbCol.r / 255.0f, gndAmbCol.g / 255.0f, gndAmbCol.b / 255.0f};
    float time = (float)GetTime();

    VFXRender_BeginPass(VFX_RENDER_PASS_BODY);
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ALPHA);

    // RULE: Under rlvk, Shader argument to SetShaderValue does not select program.
    // Activate target program with BeginShaderMode, keep uniform uploads + draws in scope, then end it.
    BeginShaderMode(s_shader);

    /* Body-independent uniforms, uploaded once. */
    if (s_locCameraPos >= 0) SetShaderValue(s_shader, s_locCameraPos, &camera.position, SHADER_UNIFORM_VEC3);
    if (s_locCameraForward >= 0) SetShaderValue(s_shader, s_locCameraForward, &camForward, SHADER_UNIFORM_VEC3);
    if (s_locViewProj >= 0) SetShaderValueMatrix(s_shader, s_locViewProj, viewProj);
    if (s_locInvView >= 0) SetShaderValueMatrix(s_shader, s_locInvView, invView);
    if (s_locResolution >= 0) SetShaderValue(s_shader, s_locResolution, &resolution, SHADER_UNIFORM_VEC2);
    if (s_locHasSceneColor >= 0) SetShaderValue(s_shader, s_locHasSceneColor, &hasSceneColor, SHADER_UNIFORM_INT);
    if (s_locHasSceneDepth >= 0) SetShaderValue(s_shader, s_locHasSceneDepth, &hasSceneDepth, SHADER_UNIFORM_INT);
    if (hasSceneColor && s_locSceneColor >= 0) SetShaderValueTexture(s_shader, s_locSceneColor, sceneColor);
    if (hasSceneDepth && s_locSceneDepth >= 0) SetShaderValueTexture(s_shader, s_locSceneDepth, sceneDepth);
    if (s_locSunDir >= 0) SetShaderValue(s_shader, s_locSunDir, &sunDir, SHADER_UNIFORM_VEC3);
    if (s_locSunColor >= 0) SetShaderValue(s_shader, s_locSunColor, &sunColor, SHADER_UNIFORM_VEC3);
    if (s_locSkyAmbient >= 0) SetShaderValue(s_shader, s_locSkyAmbient, &skyAmbient, SHADER_UNIFORM_VEC3);
    if (s_locGroundAmbient >= 0) SetShaderValue(s_shader, s_locGroundAmbient, &groundAmbient, SHADER_UNIFORM_VEC3);
    if (s_locTime >= 0) SetShaderValue(s_shader, s_locTime, &time, SHADER_UNIFORM_FLOAT);

    rlEnableBackfaceCulling();
    rlEnableDepthTest();
    rlDisableDepthMask();

    for (int bi = 0; bi < s_bodyCount; ++bi) {
        const LiquidMetaball3DBody *body = &s_bodies[bi];
        if (body->count <= 0) continue;

        Vector3 boxMin, boxMax, boxCenter, boxSize;
        int steps = 24;
        LiquidMetaball3D_BodyBounds(body, &boxMin, &boxMax, &boxCenter, &boxSize, &steps);

        const LiquidDesc *mat = body->hasMaterial ? &body->material
                              : (fallback ? fallback : &defaultMat);
        float bodyThreshold = body->hasThreshold ? body->threshold : fallbackThreshold;

        Vector4 bodyColor = ColorNormalize(mat->body);
        Vector4 glowColor = ColorNormalize(mat->glow);
        Vector4 softColor = ColorNormalize(mat->soft);
        float ior = mat->ior > 1.0f ? mat->ior : 1.333f;
        float roughness = mat->roughnessScale > 0.0f ? mat->roughnessScale * 0.20f : 0.20f;
        float opacity = mat->opacityPerMetre > 0.0f ? mat->opacityPerMetre : 1.8f;
        float emission = mat->emission;
        int liquidClass = (int)mat->liquidClass;

        if (s_locBlobs >= 0) SetShaderValueV(s_shader, s_locBlobs, body->blobs, SHADER_UNIFORM_VEC4, body->count);
        if (s_locBlobCount >= 0) SetShaderValue(s_shader, s_locBlobCount, &body->count, SHADER_UNIFORM_INT);
        if (s_locBoxMin >= 0) SetShaderValue(s_shader, s_locBoxMin, &boxMin, SHADER_UNIFORM_VEC3);
        if (s_locBoxMax >= 0) SetShaderValue(s_shader, s_locBoxMax, &boxMax, SHADER_UNIFORM_VEC3);
        if (s_locThreshold >= 0) SetShaderValue(s_shader, s_locThreshold, &bodyThreshold, SHADER_UNIFORM_FLOAT);
        if (s_locStepCount >= 0) SetShaderValue(s_shader, s_locStepCount, &steps, SHADER_UNIFORM_INT);
        if (s_locBodyColor >= 0) SetShaderValue(s_shader, s_locBodyColor, &bodyColor, SHADER_UNIFORM_VEC4);
        if (s_locGlowColor >= 0) SetShaderValue(s_shader, s_locGlowColor, &glowColor, SHADER_UNIFORM_VEC4);
        if (s_locSoftColor >= 0) SetShaderValue(s_shader, s_locSoftColor, &softColor, SHADER_UNIFORM_VEC4);
        if (s_locIor >= 0) SetShaderValue(s_shader, s_locIor, &ior, SHADER_UNIFORM_FLOAT);
        if (s_locRoughness >= 0) SetShaderValue(s_shader, s_locRoughness, &roughness, SHADER_UNIFORM_FLOAT);
        if (s_locOpacityPerMeter >= 0) SetShaderValue(s_shader, s_locOpacityPerMeter, &opacity, SHADER_UNIFORM_FLOAT);
        if (s_locEmission >= 0) SetShaderValue(s_shader, s_locEmission, &emission, SHADER_UNIFORM_FLOAT);
        if (s_locLiquidClass >= 0) SetShaderValue(s_shader, s_locLiquidClass, &liquidClass, SHADER_UNIFORM_INT);

        bool camInside = (camera.position.x >= boxMin.x && camera.position.x <= boxMax.x &&
                          camera.position.y >= boxMin.y && camera.position.y <= boxMax.y &&
                          camera.position.z >= boxMin.z && camera.position.z <= boxMax.z);
        rlSetCullFace(camInside ? RL_CULL_FACE_FRONT : RL_CULL_FACE_BACK);
        DrawCoreCube(boxCenter, boxSize.x, boxSize.y, boxSize.z, WHITE);
    }

    rlSetCullFace(RL_CULL_FACE_BACK);
    rlEnableDepthMask();

    EndShaderMode();

    EndBlendMode();
    rlDrawRenderBatchActive();
    VFXRender_EndPass();

    // Consume this frame's bodies
    LiquidMetaball3D_Reset();
}
