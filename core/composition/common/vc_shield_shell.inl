// ShieldShell — one transparent glass sphere with a shared Fresnel rim.
// The legacy plasma/flow/mask payload remains source-compatible, but is not
// sampled: a shield is a clean protective volume, not a second particle system.
#include "core/tuning.h"

#define VFX_SHIELD_SHELL_MAX 8
// 32x32 keeps the silhouette round at the shield's usual screen size while
// staying modest (2,048 triangles per sphere) and avoiding scene-texture tricks.
#define SHIELD_SHELL_RINGS 32
#define SHIELD_SHELL_SLICES 32

typedef struct {
    bool active, stopping;
    Vector3 pos;
    VC_MaterialId mat;
    float radius, target, level, elapsed;
} VC_ShieldShell;

typedef struct {
    Shader shader;
    int bodyColor, rimColor, opacity, fresnelPower, rimStrength, emissionOnly;
    int lightDirView, wallPass;
} VC_ShieldShader;

static VC_ShieldShell s_shieldShells[VFX_SHIELD_SHELL_MAX];
static VC_ShieldShader s_shieldShader = {0};
static bool s_shieldInit = false;
static float s_shieldOpacity = 1.0f;
static float s_shieldRim = 1.35f;
static float s_shieldFresnelPower = 3.2f;

static float ShieldShell_Clamp01(float value)
{ return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); }

static void ShieldShell_InitShared(void)
{
    if (s_shieldInit) return;

    s_shieldShader.shader = ResourceManager_LoadShader(
        "core/shaders/glass_shell.vs", "core/shaders/glass_shell.fs");
    s_shieldShader.bodyColor = GetShaderLocation(s_shieldShader.shader, "u_bodyColor");
    s_shieldShader.rimColor = GetShaderLocation(s_shieldShader.shader, "u_rimColor");
    s_shieldShader.opacity = GetShaderLocation(s_shieldShader.shader, "u_opacity");
    s_shieldShader.fresnelPower = GetShaderLocation(s_shieldShader.shader, "u_fresnelPower");
    s_shieldShader.rimStrength = GetShaderLocation(s_shieldShader.shader, "u_rimStrength");
    s_shieldShader.emissionOnly = GetShaderLocation(s_shieldShader.shader, "u_emissionOnly");
    s_shieldShader.lightDirView = GetShaderLocation(s_shieldShader.shader, "u_lightDirView");
    s_shieldShader.wallPass = GetShaderLocation(s_shieldShader.shader, "u_wallPass");

    Tuning_RegisterFloat("shield_shell_opacity", &s_shieldOpacity, 1.0f);
    Tuning_RegisterFloat("shield_shell_rim", &s_shieldRim, 1.35f);
    Tuning_RegisterFloat("shield_shell_fresnel_power", &s_shieldFresnelPower, 3.2f);
    s_shieldInit = true;
}

int VFX_ShieldShell_SpawnEx(Vector3 pos, VC_MaterialId mat, float radius,
                             float intensity, const VFX_ShieldSurface *surface)
{
    (void)surface;
    ShieldShell_InitShared();
    for (int i = 0; i < VFX_SHIELD_SHELL_MAX; ++i)
    {
        if (s_shieldShells[i].active) continue;
        s_shieldShells[i] = (VC_ShieldShell){
            .active = true, .stopping = false, .pos = pos, .mat = mat,
            .radius = radius > 0.0f ? radius : 0.9f,
            .target = ShieldShell_Clamp01(intensity), .level = 0.0f, .elapsed = 0.0f
        };
        return i;
    }
    return -1;
}

int VFX_ShieldShell_Spawn(Vector3 pos, VC_MaterialId mat, float radius, float intensity)
{ return VFX_ShieldShell_SpawnEx(pos, mat, radius, intensity, NULL); }

int VFX_ComposeShieldShell(Vector3 pos, VC_MaterialId mat, float radius, float intensity)
{ return VFX_ShieldShell_Spawn(pos, mat, radius, intensity); }

void VFX_ShieldShell_SetTransform(int handle, Vector3 pos)
{
    if (handle >= 0 && handle < VFX_SHIELD_SHELL_MAX && s_shieldShells[handle].active)
        s_shieldShells[handle].pos = pos;
}

void VFX_ShieldShell_SetIntensity(int handle, float intensity01)
{
    if (handle >= 0 && handle < VFX_SHIELD_SHELL_MAX && s_shieldShells[handle].active)
    {
        s_shieldShells[handle].target = ShieldShell_Clamp01(intensity01);
        s_shieldShells[handle].stopping = false;
    }
}

void VFX_ShieldShell_SetSurface(int handle, const VFX_ShieldSurface *surface)
{ (void)handle; (void)surface; }

void VFX_ShieldShell_Stop(int handle)
{
    if (handle >= 0 && handle < VFX_SHIELD_SHELL_MAX && s_shieldShells[handle].active)
    {
        s_shieldShells[handle].stopping = true;
        s_shieldShells[handle].target = 0.0f;
    }
}

void VFX_KillShieldShell(int handle)
{ if (handle >= 0 && handle < VFX_SHIELD_SHELL_MAX) s_shieldShells[handle].active = false; }

static void VC_ShieldShell_Update(float dt)
{
    for (int i = 0; i < VFX_SHIELD_SHELL_MAX; ++i)
    {
        VC_ShieldShell *shield = &s_shieldShells[i];
        if (!shield->active) continue;
        shield->elapsed += dt;
        shield->level += (shield->target - shield->level) * (1.0f - expf(-dt * 7.0f));
        if (shield->stopping && shield->level < 0.004f) shield->active = false;
    }
}

static void ShieldShell_DrawPass(bool emissionOnly)
{
    for (int i = 0; i < VFX_SHIELD_SHELL_MAX; ++i)
    {
        VC_ShieldShell *shield = &s_shieldShells[i];
        if (!shield->active || shield->level <= 0.004f) continue;

        const VFX_ElementMaterial *material = VFX_Material(shield->mat);
        Vector4 bodyColor = ColorNormalize(material->body);
        Vector4 rimColor = ColorNormalize(material->glow);
        float pulse = VC_Breathe(shield->elapsed, 1.15f, 0.012f);
        float radius = shield->radius * (0.99f + 0.01f * pulse);
        float opacity = ShieldShell_Clamp01(shield->level * s_shieldOpacity);
        int emission = emissionOnly ? 1 : 0;

        SetShaderValue(s_shieldShader.shader, s_shieldShader.bodyColor,
                       &bodyColor, SHADER_UNIFORM_VEC4);
        SetShaderValue(s_shieldShader.shader, s_shieldShader.rimColor,
                       &rimColor, SHADER_UNIFORM_VEC4);
        SetShaderValue(s_shieldShader.shader, s_shieldShader.opacity,
                       &opacity, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_shieldShader.shader, s_shieldShader.fresnelPower,
                       &s_shieldFresnelPower, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_shieldShader.shader, s_shieldShader.rimStrength,
                       &s_shieldRim, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_shieldShader.shader, s_shieldShader.emissionOnly,
                       &emission, SHADER_UNIFORM_INT);

        DrawCoreSphere(shield->pos, radius, SHIELD_SHELL_RINGS,
                       SHIELD_SHELL_SLICES, WHITE);
    }
}

static void VC_ShieldShell_Draw3D(Camera3D cam)
{
    ShieldShell_InitShared();

    // Immediate-mode sphere normals are view-space. Convert the environment
    // key light once per draw so the glass highlight follows the actual camera
    // orientation instead of dotting a world-space light with view-space N.
    Vector3 forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, cam.up));
    Vector3 camUp = Vector3CrossProduct(right, forward);
    Vector3 lightWorld = Vector3Negate(Environment_GetSunDirection());
    Vector3 lightView = {
        Vector3DotProduct(lightWorld, right),
        Vector3DotProduct(lightWorld, camUp),
        -Vector3DotProduct(lightWorld, forward)
    };
    lightView = Vector3Normalize(lightView);

    VFXRenderScope body = VFXRender_BeginDraw(
        VFX_RENDER_PASS_BODY, VFX_SURFACE_ALPHA, false);
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    SkillManager_BeginShader(s_shieldShader.shader);
    if (s_shieldShader.lightDirView >= 0)
        SetShaderValue(s_shieldShader.shader, s_shieldShader.lightDirView,
                       &lightView, SHADER_UNIFORM_VEC3);
    // A glass volume has two interfaces. Composite the back wall first, then
    // the front wall; a single back-face-culled draw is only a one-sided shell.
    rlSetCullFace(RL_CULL_FACE_FRONT);
    if (s_shieldShader.wallPass >= 0) {
        int wall = 0;
        SetShaderValue(s_shieldShader.shader, s_shieldShader.wallPass,
                       &wall, SHADER_UNIFORM_INT);
    }
    ShieldShell_DrawPass(false);
    rlDrawRenderBatchActive();
    rlSetCullFace(RL_CULL_FACE_BACK);
    if (s_shieldShader.wallPass >= 0) {
        int wall = 1;
        SetShaderValue(s_shieldShader.shader, s_shieldShader.wallPass,
                       &wall, SHADER_UNIFORM_INT);
    }
    ShieldShell_DrawPass(false);
    rlDrawRenderBatchActive();
    SkillManager_EndShader();
    rlSetCullFace(RL_CULL_FACE_BACK);
    VFXRender_EndDraw(&body);

    VFXRenderScope emission = VFXRender_BeginDraw(
        VFX_RENDER_PASS_EMISSION, VFX_SURFACE_ADDITIVE, false);
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    SkillManager_BeginShader(s_shieldShader.shader);
    if (s_shieldShader.lightDirView >= 0)
        SetShaderValue(s_shieldShader.shader, s_shieldShader.lightDirView,
                       &lightView, SHADER_UNIFORM_VEC3);
    ShieldShell_DrawPass(true);
    rlDrawRenderBatchActive();
    SkillManager_EndShader();
    VFXRender_EndDraw(&emission);
}
