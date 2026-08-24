// FlowShield — an independent water-energy shell.  The current BubbleShield
// stays in vc_shield_shell.inl; this variant owns a textured membrane and
// never derives transparency from screen position or view angle.
#include "core/tuning.h"
#include "core/gfx_quality.h"

#define VFX_FLOW_SHIELD_MAX 6

typedef struct {
    bool active, stopping;
    Vector3 pos;
    VC_MaterialId mat;
    float radius, target, level, elapsed;
} VC_FlowShield;

typedef struct {
    Shader shader;
    int bodyColor, rimColor, opacity, bodyOpacity, emissionGain, emissionOnly;
    int flowTex, sceneTex, hasScene, depthTex, hasDepth, depthEnabled, contactThickness;
    int time, flowSpeed, flowStrength, flowTiling, refractionStrength;
} VC_FlowShieldShader;

static VC_FlowShield s_flowShields[VFX_FLOW_SHIELD_MAX];
static VC_FlowShieldShader s_flowShieldShader = {0};
static Texture2D s_flowShieldMembrane = {0};
static Texture2D s_flowShieldFlowMap = {0};
static bool s_flowShieldInit = false;
static float s_flowShieldOpacity = 0.82f;
// MAGIC's base 3x emission is intentionally restrained for generic spells.
// This is a luminous shell, so give its fine flow filaments enough HDR energy
// to cross the bloom threshold without raising their alpha coverage.
static float s_flowShieldGlow = 1.15f;
static float s_flowShieldSpeed = 0.22f;
static float s_flowShieldStrength = 0.16f;
static float s_flowShieldTiling = 1.15f;
static float s_flowShieldRefraction = 0.014f;
static float s_flowShieldDepthEnabled = 1.0f;
static float s_flowShieldContactThickness = 0.62f;

static float FlowShield_Clamp01(float value)
{ return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); }

static void FlowShield_InitShared(void)
{
    if (s_flowShieldInit) return;

    s_flowShieldShader.shader = ResourceManager_LoadShader(
        "core/shaders/glass_shell.vs", "core/shaders/shield_flow_shell.fs");
    // Low-frequency cloud density makes the reference's broad liquid currents.
    // The flow map below advects it; no line/cell texture is allowed to become
    // a grid or a uniformly sparkling shell.
    s_flowShieldMembrane = ResourceManager_LoadTexture("assets/textures/cloud_noise.png");
    s_flowShieldFlowMap = ResourceManager_LoadTexture(
        "assets/textures/energy_volume_flow.png");
    SetTextureWrap(s_flowShieldMembrane, TEXTURE_WRAP_REPEAT);
    SetTextureWrap(s_flowShieldFlowMap, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(s_flowShieldMembrane, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(s_flowShieldFlowMap, TEXTURE_FILTER_BILINEAR);

    s_flowShieldShader.bodyColor = GetShaderLocation(s_flowShieldShader.shader, "u_bodyColor");
    s_flowShieldShader.rimColor = GetShaderLocation(s_flowShieldShader.shader, "u_rimColor");
    s_flowShieldShader.opacity = GetShaderLocation(s_flowShieldShader.shader, "u_opacity");
    s_flowShieldShader.bodyOpacity = GetShaderLocation(s_flowShieldShader.shader, "u_bodyOpacity");
    s_flowShieldShader.emissionGain = GetShaderLocation(s_flowShieldShader.shader, "u_emissionGain");
    s_flowShieldShader.emissionOnly = GetShaderLocation(s_flowShieldShader.shader, "u_emissionOnly");
    s_flowShieldShader.flowTex = GetShaderLocation(s_flowShieldShader.shader, "u_flowTex");
    s_flowShieldShader.sceneTex = GetShaderLocation(s_flowShieldShader.shader, "u_sceneTex");
    s_flowShieldShader.hasScene = GetShaderLocation(s_flowShieldShader.shader, "u_hasScene");
    s_flowShieldShader.depthTex = GetShaderLocation(s_flowShieldShader.shader, "u_depthTex");
    s_flowShieldShader.hasDepth = GetShaderLocation(s_flowShieldShader.shader, "u_hasDepth");
    s_flowShieldShader.depthEnabled = GetShaderLocation(s_flowShieldShader.shader, "u_depthEnabled");
    s_flowShieldShader.contactThickness = GetShaderLocation(s_flowShieldShader.shader, "u_contactThickness");
    s_flowShieldShader.time = GetShaderLocation(s_flowShieldShader.shader, "u_time");
    s_flowShieldShader.flowSpeed = GetShaderLocation(s_flowShieldShader.shader, "u_flowSpeed");
    s_flowShieldShader.flowStrength = GetShaderLocation(s_flowShieldShader.shader, "u_flowStrength");
    s_flowShieldShader.flowTiling = GetShaderLocation(s_flowShieldShader.shader, "u_flowTiling");
    s_flowShieldShader.refractionStrength = GetShaderLocation(s_flowShieldShader.shader, "u_refractionStrength");

    Tuning_RegisterFloat("flow_shield_opacity", &s_flowShieldOpacity, 0.82f);
    Tuning_RegisterFloat("flow_shield_glow", &s_flowShieldGlow, 1.15f);
    Tuning_RegisterFloat("flow_shield_flow_speed", &s_flowShieldSpeed, 0.22f);
    Tuning_RegisterFloat("flow_shield_flow_strength", &s_flowShieldStrength, 0.16f);
    Tuning_RegisterFloat("flow_shield_flow_tiling", &s_flowShieldTiling, 1.15f);
    Tuning_RegisterFloat("flow_shield_refraction", &s_flowShieldRefraction, 0.014f);
    Tuning_RegisterFloat("flow_shield_depth_enabled", &s_flowShieldDepthEnabled, 1.0f);
    Tuning_RegisterFloat("flow_shield_contact_thickness", &s_flowShieldContactThickness, 0.62f);
    s_flowShieldInit = true;
}

int VFX_FlowShield_Spawn(Vector3 pos, VC_MaterialId mat, float radius, float intensity)
{
    FlowShield_InitShared();
    for (int i = 0; i < VFX_FLOW_SHIELD_MAX; ++i)
    {
        if (s_flowShields[i].active) continue;
        s_flowShields[i] = (VC_FlowShield){
            .active = true, .pos = pos, .mat = mat,
            .radius = radius > 0.0f ? radius : 0.9f,
            .target = FlowShield_Clamp01(intensity), .level = 0.0f
        };
        return i;
    }
    return -1;
}

int VFX_ComposeFlowShield(Vector3 pos, VC_MaterialId mat, float radius, float intensity)
{ return VFX_FlowShield_Spawn(pos, mat, radius, intensity); }

void VFX_FlowShield_SetTransform(int handle, Vector3 pos)
{
    if (handle >= 0 && handle < VFX_FLOW_SHIELD_MAX && s_flowShields[handle].active)
        s_flowShields[handle].pos = pos;
}

void VFX_FlowShield_SetIntensity(int handle, float intensity01)
{
    if (handle >= 0 && handle < VFX_FLOW_SHIELD_MAX && s_flowShields[handle].active)
    {
        s_flowShields[handle].target = FlowShield_Clamp01(intensity01);
        s_flowShields[handle].stopping = false;
    }
}

void VFX_FlowShield_Stop(int handle)
{
    if (handle >= 0 && handle < VFX_FLOW_SHIELD_MAX && s_flowShields[handle].active)
    {
        s_flowShields[handle].stopping = true;
        s_flowShields[handle].target = 0.0f;
    }
}

void VFX_KillFlowShield(int handle)
{ if (handle >= 0 && handle < VFX_FLOW_SHIELD_MAX) s_flowShields[handle].active = false; }

static void VC_FlowShield_Update(float dt)
{
    bool anyActive = false;
    for (int i = 0; i < VFX_FLOW_SHIELD_MAX; ++i)
    {
        VC_FlowShield *shield = &s_flowShields[i];
        if (!shield->active) continue;
        anyActive = true;
        shield->elapsed += dt;
        shield->level += (shield->target - shield->level) * (1.0f - expf(-dt * 7.0f));
        if (shield->stopping && shield->level < 0.004f) shield->active = false;
    }
    if (anyActive) SceneTargets_RequestSceneSnapshot();
}

static void FlowShield_BindInputs(void)
{
    Texture2D scene = SceneTargets_GetSceneSnapshotTexture();
    int hasScene = scene.id != 0 ? 1 : 0;
    SetShaderValue(s_flowShieldShader.shader, s_flowShieldShader.hasScene,
                   &hasScene, SHADER_UNIFORM_INT);
    if (hasScene)
        SetShaderValueTexture(s_flowShieldShader.shader, s_flowShieldShader.sceneTex, scene);
    Texture2D depth = SceneTargets_GetDepthTexture();
    int hasDepth = (depth.id != 0 && s_flowShieldDepthEnabled > 0.5f) ? 1 : 0;
    SetShaderValue(s_flowShieldShader.shader, s_flowShieldShader.hasDepth,
                   &hasDepth, SHADER_UNIFORM_INT);
    if (hasDepth)
        SetShaderValueTexture(s_flowShieldShader.shader, s_flowShieldShader.depthTex, depth);
    SetShaderValue(s_flowShieldShader.shader, s_flowShieldShader.depthEnabled,
                   &s_flowShieldDepthEnabled, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_flowShieldShader.shader, s_flowShieldShader.contactThickness,
                   &s_flowShieldContactThickness, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_flowShieldShader.shader, s_flowShieldShader.refractionStrength,
                   &s_flowShieldRefraction, SHADER_UNIFORM_FLOAT);
}

/* Same contract as the glass shell: `pos` is the ground point and the
 * sphere sits three quarters above it. See vc_shield_shell.inl. */
/* Centre height as a multiple of the radius: 0.5 => three quarters above
 * ground. 1.0 would make the sphere tangent to it. */
/* Both shells live in the same translation unit (visual_composer.c pulls every
 * .inl in), so this is guarded rather than defined twice. */
#ifndef SHIELD_BURIED_LIFT
#define SHIELD_BURIED_LIFT 0.5f
#endif

static Vector3 FlowShieldCentre(Vector3 groundPos, float radius)
{
    groundPos.y += radius * SHIELD_BURIED_LIFT;
    return groundPos;
}

static void FlowShield_DrawPass(bool emissionOnly)
{
    int activeCount = 0;
    for (int i = 0; i < VFX_FLOW_SHIELD_MAX; ++i)
        if (s_flowShields[i].active) activeCount++;
    const int rings = ShieldShell_Rings(activeCount);

    for (int i = 0; i < VFX_FLOW_SHIELD_MAX; ++i)
    {
        VC_FlowShield *shield = &s_flowShields[i];
        if (!shield->active || shield->level <= 0.004f) continue;
        const VFX_ElementMaterial *material = VFX_Material(shield->mat);
        Vector4 bodyColor = ColorNormalize(material->body);
        Vector4 rimColor = ColorNormalize(material->glow);
        VFXResolvedAppearance appearance = VFXAppearance_Resolve(
            VFX_APPEARANCE_MAGIC,
            (VFXResolvedAppearance){ VFX_SURFACE_ALPHA, VFX_CONTRAST_MAGIC,
                                     1.0f, 1.0f, 0.82f, true });
        float opacity = shield->level * s_flowShieldOpacity;
        float bodyOpacity = emissionOnly ? 0.0f : appearance.bodyOpacity;
        float emissionGain = emissionOnly
                                 ? appearance.emissionIntensity * s_flowShieldGlow
                                 : 0.0f;
        int emission = emissionOnly ? 1 : 0;

        SetShaderValue(s_flowShieldShader.shader, s_flowShieldShader.bodyColor,
                       &bodyColor, SHADER_UNIFORM_VEC4);
        SetShaderValue(s_flowShieldShader.shader, s_flowShieldShader.rimColor,
                       &rimColor, SHADER_UNIFORM_VEC4);
        SetShaderValue(s_flowShieldShader.shader, s_flowShieldShader.opacity,
                       &opacity, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_flowShieldShader.shader, s_flowShieldShader.bodyOpacity,
                       &bodyOpacity, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_flowShieldShader.shader, s_flowShieldShader.emissionGain,
                       &emissionGain, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_flowShieldShader.shader, s_flowShieldShader.emissionOnly,
                       &emission, SHADER_UNIFORM_INT);
        SetShaderValue(s_flowShieldShader.shader, s_flowShieldShader.time,
                       &shield->elapsed, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_flowShieldShader.shader, s_flowShieldShader.flowSpeed,
                       &s_flowShieldSpeed, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_flowShieldShader.shader, s_flowShieldShader.flowStrength,
                       &s_flowShieldStrength, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_flowShieldShader.shader, s_flowShieldShader.flowTiling,
                       &s_flowShieldTiling, SHADER_UNIFORM_FLOAT);
        SetShaderValueTexture(s_flowShieldShader.shader, s_flowShieldShader.flowTex,
                              s_flowShieldFlowMap);
        DrawCoreSphere(FlowShieldCentre(shield->pos, shield->radius),
                       shield->radius, rings, rings, WHITE);
    }
}

void VFX_FlowShield_DrawRefraction(Camera3D cam)
{
    (void)cam;
    FlowShield_InitShared();
    VFXResolvedAppearance appearance;
    VFXResolvedAppearance legacy = {
        VFX_SURFACE_ALPHA, VFX_CONTRAST_MAGIC, 1.0f, 1.0f, 0.82f, true
    };

    VFXRenderScope body = VFXRender_BeginAppearance(
        VFX_RENDER_PASS_BODY, VFX_APPEARANCE_MAGIC, legacy, false, &appearance);
    rlDrawRenderBatchActive();
    SkillManager_BeginShader(s_flowShieldShader.shader);
    FlowShield_BindInputs();
    rlSetTexture(s_flowShieldMembrane.id);
    /* FAR WALL FIRST, THEN NEAR — two culled submissions, not one unculled one.
     *
     * This used to be a single rlDisableBackfaceCulling() draw, with the shader
     * classifying each fragment by gl_FrontFacing. That gives both walls into one
     * submission, but it gives them NO ORDER: a closed transparent volume has two
     * surfaces over almost every pixel, and with depth-write off the winner is
     * whichever the mesh happened to emit last. Measured by colouring
     * gl_FrontFacing and sign(normal.y) into two channels, the sphere came out in
     * three bands — far-upper, near-upper, near-lower — and the FAR-LOWER wall
     * never appeared at all. The band boundary is where the emission order flips,
     * which on a sphere projects to an ellipse: the hard seam across the middle.
     *
     * Culling per pass restores the order. The old comment here warned that
     * "culling a wall by render state is fragile: a later pass can inherit the
     * previous cull face" — true, and the answer is to SET the face explicitly on
     * every pass rather than to avoid culling. Both passes still flush around the
     * state change: ENGINE_LANDMINES says the batch-flush rule covers cull face,
     * not just depth. */
    rlEnableBackfaceCulling();
    rlSetCullFace(RL_CULL_FACE_FRONT);   /* keep back faces: the FAR wall */
    rlDrawRenderBatchActive();
    FlowShield_DrawPass(false);
    rlDrawRenderBatchActive();
    rlSetCullFace(RL_CULL_FACE_BACK);    /* keep front faces: the NEAR wall */
    rlDrawRenderBatchActive();
    FlowShield_DrawPass(false);
    rlDrawRenderBatchActive();
    rlDrawRenderBatchActive();
    rlSetTexture(0);
    rlEnableBackfaceCulling();
    SkillManager_EndShader();
    VFXRender_EndDraw(&body);

    VFXRenderScope emission = VFXRender_BeginAppearance(
        VFX_RENDER_PASS_EMISSION, VFX_APPEARANCE_MAGIC, legacy, false, &appearance);
    rlDrawRenderBatchActive();
    SkillManager_BeginShader(s_flowShieldShader.shader);
    FlowShield_BindInputs();
    rlSetTexture(s_flowShieldMembrane.id);
    /* FAR WALL FIRST, THEN NEAR — two culled submissions, not one unculled one.
     *
     * This used to be a single rlDisableBackfaceCulling() draw, with the shader
     * classifying each fragment by gl_FrontFacing. That gives both walls into one
     * submission, but it gives them NO ORDER: a closed transparent volume has two
     * surfaces over almost every pixel, and with depth-write off the winner is
     * whichever the mesh happened to emit last. Measured by colouring
     * gl_FrontFacing and sign(normal.y) into two channels, the sphere came out in
     * three bands — far-upper, near-upper, near-lower — and the FAR-LOWER wall
     * never appeared at all. The band boundary is where the emission order flips,
     * which on a sphere projects to an ellipse: the hard seam across the middle.
     *
     * Culling per pass restores the order. The old comment here warned that
     * "culling a wall by render state is fragile: a later pass can inherit the
     * previous cull face" — true, and the answer is to SET the face explicitly on
     * every pass rather than to avoid culling. Both passes still flush around the
     * state change: ENGINE_LANDMINES says the batch-flush rule covers cull face,
     * not just depth. */
    rlEnableBackfaceCulling();
    rlSetCullFace(RL_CULL_FACE_FRONT);   /* keep back faces: the FAR wall */
    rlDrawRenderBatchActive();
    FlowShield_DrawPass(true);
    rlDrawRenderBatchActive();
    rlSetCullFace(RL_CULL_FACE_BACK);    /* keep front faces: the NEAR wall */
    rlDrawRenderBatchActive();
    FlowShield_DrawPass(true);
    rlDrawRenderBatchActive();
    rlDrawRenderBatchActive();
    rlSetTexture(0);
    rlEnableBackfaceCulling();
    SkillManager_EndShader();
    VFXRender_EndDraw(&emission);

}

/* Snapshot refraction can only run after the complete 3D scene was copied.
 * This generated archetype hook deliberately stays in the 3D pass so Update
 * / Draw wiring remains structural; it has no geometry submission itself. */
static void VC_FlowShield_Draw3D(Camera3D cam)
{
    // Like ShieldShell, request the depth region inside the main 3D pass. The
    // post-3D refraction pass then samples that snapshot for the true terrain
    // intersection; a UV or world-height ring cannot follow uneven ground.
    if (s_flowShieldDepthEnabled <= 0.5f) return;
    const float screenH = (float)GetScreenHeight();
    const float halfFovy = cam.fovy * 0.5f * DEG2RAD;
    bool hasBounds = false;
    Rectangle bounds = {0};
    for (int i = 0; i < VFX_FLOW_SHIELD_MAX; ++i)
    {
        const VC_FlowShield *shield = &s_flowShields[i];
        if (!shield->active) continue;
        Vector3 centrePos = FlowShieldCentre(shield->pos, shield->radius);
        float distance = Vector3Length(Vector3Subtract(centrePos, cam.position));
        if (distance <= 0.001f) continue;
        Vector2 centre = GetWorldToScreen(centrePos, cam);
        float radiusPx = shield->radius * screenH / (2.0f * distance * tanf(halfFovy));
        Rectangle r = {centre.x - radiusPx, centre.y - radiusPx, radiusPx * 2.0f, radiusPx * 2.0f};
        if (!hasBounds) { bounds = r; hasBounds = true; }
        else {
            float x1 = fmaxf(bounds.x + bounds.width, r.x + r.width);
            float y1 = fmaxf(bounds.y + bounds.height, r.y + r.height);
            bounds.x = fminf(bounds.x, r.x); bounds.y = fminf(bounds.y, r.y);
            bounds.width = x1 - bounds.x; bounds.height = y1 - bounds.y;
        }
    }
    if (hasBounds) SceneTargets_RequestSoftDepthRegion(bounds);
}
