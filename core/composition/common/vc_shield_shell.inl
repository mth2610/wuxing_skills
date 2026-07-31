// P4 — ShieldShell: a handle-owned 3D membrane, not a billboard sphere.
#include "core/tuning.h"
#include "core/flow_map.h"

#define VFX_SHIELD_SHELL_MAX 8
#define SHIELD_SHELL_RINGS 20
#define SHIELD_SHELL_SLICES 20

typedef struct {
    bool active, stopping, hasSurface;
    Vector3 pos;
    VC_MaterialId mat;
    VFX_ShieldSurface surface;
    FlowMap flow;
    float radius, target, level, elapsed, phase;
} VC_ShieldShell;

typedef struct {
    Shader shader;
    int bodyColor, glowColor, bodyTex, maskTex, useFlow, useMask;
    int maskTiling;
    int opacity;
} VC_ShieldShader;

static VC_ShieldShell s_shieldShells[VFX_SHIELD_SHELL_MAX];
static PlasmaMaterial s_shieldPlasma;
static VC_ShieldShader s_shieldShader = {0};
static bool s_shieldInit = false;
static int s_shieldSerial = 0;
static float s_shieldOpacity = 1.0f;
static float s_shieldRim = 1.0f;
static float s_shieldHdrFlow = 5.0f;

static float ShieldShell_Clamp01(float value)
{ return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); }
static float ShieldShell_Clamp(float value, float minimum, float maximum)
{ return value < minimum ? minimum : (value > maximum ? maximum : value); }

static void ShieldShell_InitShared(void)
{
    if (s_shieldInit) return;
    PlasmaMaterial_Load(&s_shieldPlasma, &(PlasmaMaterialParams){
        .baseColor = WHITE, .wispColor = WHITE, .noiseScale = 3.2f,
        .noiseSpeed = 0.55f, .fresnelPower = 3.4f, .rimStrength = 1.15f,
        .emissive = 5.0f, .opacity = 0.72f, .displaceAmp = 0.035f,
    });
    s_shieldShader.shader = ResourceManager_LoadShader("core/shaders/plasma_shell.vs",
                                                         "core/shaders/shield_shell.fs");
    Shader shader = s_shieldShader.shader;
    s_shieldShader.bodyColor = GetShaderLocation(shader, "u_bodyColor");
    s_shieldShader.glowColor = GetShaderLocation(shader, "u_glowColor");
    s_shieldShader.bodyTex = GetShaderLocation(shader, "u_bodyTex");
    s_shieldShader.maskTex = GetShaderLocation(shader, "u_maskTex");
    s_shieldShader.useFlow = GetShaderLocation(shader, "u_useFlow");
    s_shieldShader.useMask = GetShaderLocation(shader, "u_useMask");
    s_shieldShader.maskTiling = GetShaderLocation(shader, "u_maskTiling");
    s_shieldShader.opacity = GetShaderLocation(shader, "u_opacity");
    Tuning_RegisterFloat("shield_shell_opacity", &s_shieldOpacity, 1.0f);
    Tuning_RegisterFloat("shield_shell_rim", &s_shieldRim, 1.0f);
    Tuning_RegisterFloat("shield_shell_hdr_flow", &s_shieldHdrFlow, 5.0f);
    s_shieldInit = true;
}

static void ShieldShell_SetSurface(VC_ShieldShell *shield, const VFX_ShieldSurface *surface)
{
    shield->surface = surface ? *surface : (VFX_ShieldSurface){0};
    shield->hasSurface = surface != NULL && surface->body.id != 0;
    shield->flow = (FlowMap){0};
    if (!shield->hasSurface) return;

    Texture2D flowTex = shield->surface.flowMap.id != 0
        ? shield->surface.flowMap : shield->surface.body;
    shield->flow = FlowMap_Create(s_shieldShader.shader, flowTex, "uTime");
    shield->flow.cfg.speed = shield->surface.flowSpeed;
    shield->flow.cfg.strength = shield->surface.flowStrength;
    shield->flow.cfg.tiling = shield->surface.flowTiling > 0.0f
        ? shield->surface.flowTiling : 1.0f;
}

int VFX_ShieldShell_SpawnEx(Vector3 pos, VC_MaterialId mat, float radius,
                             float intensity, const VFX_ShieldSurface *surface)
{
    ShieldShell_InitShared();
    for (int i = 0; i < VFX_SHIELD_SHELL_MAX; ++i) if (!s_shieldShells[i].active) {
        s_shieldShells[i] = (VC_ShieldShell){
            .active = true, .pos = pos, .mat = mat,
            .radius = radius > 0.0f ? radius : 0.9f,
            .target = ShieldShell_Clamp01(intensity), .level = 0.0f,
            .phase = (float)(s_shieldSerial++) * 2.39996323f,
        };
        ShieldShell_SetSurface(&s_shieldShells[i], surface);
        return i;
    }
    return -1;
}

int VFX_ShieldShell_Spawn(Vector3 pos, VC_MaterialId mat, float radius, float intensity)
{ return VFX_ShieldShell_SpawnEx(pos, mat, radius, intensity, NULL); }

int VFX_ComposeShieldShell(Vector3 pos, VC_MaterialId mat, float radius, float intensity)
{ return VFX_ShieldShell_Spawn(pos, mat, radius, intensity); }

void VFX_ShieldShell_SetTransform(int handle, Vector3 pos)
{ if (handle >= 0 && handle < VFX_SHIELD_SHELL_MAX && s_shieldShells[handle].active) s_shieldShells[handle].pos = pos; }
void VFX_ShieldShell_SetIntensity(int handle, float intensity01)
{ if (handle >= 0 && handle < VFX_SHIELD_SHELL_MAX && s_shieldShells[handle].active) { s_shieldShells[handle].target = ShieldShell_Clamp01(intensity01); s_shieldShells[handle].stopping = false; } }
void VFX_ShieldShell_SetSurface(int handle, const VFX_ShieldSurface *surface)
{ if (handle >= 0 && handle < VFX_SHIELD_SHELL_MAX && s_shieldShells[handle].active) ShieldShell_SetSurface(&s_shieldShells[handle], surface); }
void VFX_ShieldShell_Stop(int handle)
{ if (handle >= 0 && handle < VFX_SHIELD_SHELL_MAX && s_shieldShells[handle].active) { s_shieldShells[handle].stopping = true; s_shieldShells[handle].target = 0.0f; } }
void VFX_KillShieldShell(int handle)
{ if (handle >= 0 && handle < VFX_SHIELD_SHELL_MAX) s_shieldShells[handle].active = false; }

static void VC_ShieldShell_Update(float dt)
{
    for (int i = 0; i < VFX_SHIELD_SHELL_MAX; ++i) {
        VC_ShieldShell *shield = &s_shieldShells[i];
        if (!shield->active) continue;
        shield->elapsed += dt;
        shield->level += (shield->target - shield->level) * (1.0f - expf(-dt * 7.0f));
        if (shield->stopping && shield->level < 0.004f) shield->active = false;
    }
}

static void ShieldShell_BeginTextured(const VC_ShieldShell *shield, const VFX_ElementMaterial *mat,
                                      float opacity)
{
    Shader shader = s_shieldShader.shader;
    SkillManager_BeginShader(shader);
    Vector4 body = ColorNormalize(mat->body), glow = ColorNormalize(VC_Whiten(mat->glow, 0.35f));
    int useFlow = shield->surface.flowMap.id != 0, useMask = shield->surface.mask.id != 0;
    Texture2D maskTex = useMask ? shield->surface.mask : shield->surface.body;
    float maskTiling = shield->surface.maskTiling > 0.0f ? shield->surface.maskTiling : 1.0f;
    if (s_shieldShader.bodyColor >= 0) SetShaderValue(shader, s_shieldShader.bodyColor, &body, SHADER_UNIFORM_VEC4);
    if (s_shieldShader.glowColor >= 0) SetShaderValue(shader, s_shieldShader.glowColor, &glow, SHADER_UNIFORM_VEC4);
    if (s_shieldShader.bodyTex >= 0) SetShaderValueTexture(shader, s_shieldShader.bodyTex, shield->surface.body);
    if (s_shieldShader.maskTex >= 0) SetShaderValueTexture(shader, s_shieldShader.maskTex, maskTex);
    if (s_shieldShader.useFlow >= 0) SetShaderValue(shader, s_shieldShader.useFlow, &useFlow, SHADER_UNIFORM_INT);
    if (s_shieldShader.useMask >= 0) SetShaderValue(shader, s_shieldShader.useMask, &useMask, SHADER_UNIFORM_INT);
    if (s_shieldShader.maskTiling >= 0) SetShaderValue(shader, s_shieldShader.maskTiling, &maskTiling, SHADER_UNIFORM_FLOAT);
    if (s_shieldShader.opacity >= 0) SetShaderValue(shader, s_shieldShader.opacity, &opacity, SHADER_UNIFORM_FLOAT);
    FlowMap_Apply(&shield->flow, shader, (float)GetTime());
    // Immediate rlgl geometry needs its current material texture set as well
    // as the named sampler bindings above; otherwise a backend may batch it as
    // an untextured sphere while the flow uniforms are technically valid.
    rlSetTexture(shield->surface.body.id);
}

static void ShieldShell_DrawPass(void)
{
    for (int i = 0; i < VFX_SHIELD_SHELL_MAX; ++i) {
        VC_ShieldShell *shield = &s_shieldShells[i];
        if (!shield->active || shield->level <= 0.004f) continue;
        const VFX_ElementMaterial *mat = VFX_Material(shield->mat);
        float pulse = VC_Breathe(shield->elapsed + shield->phase, 1.15f, 0.035f);
        float radius = shield->radius * (0.94f + 0.06f * shield->level) * pulse;
        float opacity = shield->level * s_shieldOpacity;
        if (shield->hasSurface && s_shieldShader.shader.id != 0) {
            ShieldShell_BeginTextured(shield, mat, opacity);
            DrawCoreSphere(shield->pos, radius, SHIELD_SHELL_RINGS, SHIELD_SHELL_SLICES, WHITE);
            rlDrawRenderBatchActive();
            SkillManager_EndShader();
            rlSetTexture(0);
        } else {
            s_shieldPlasma.params.baseColor = mat->body;
            s_shieldPlasma.params.wispColor = VC_Whiten(mat->glow, 0.72f);
            s_shieldPlasma.params.opacity = opacity * 0.72f;
            s_shieldPlasma.params.rimStrength = 1.15f * s_shieldRim;
            s_shieldPlasma.params.emissive = ShieldShell_Clamp(s_shieldHdrFlow, 0.0f, 8.0f);
            PlasmaMaterial_Begin(s_shieldPlasma);
            DrawCoreSphere(shield->pos, radius, SHIELD_SHELL_RINGS, SHIELD_SHELL_SLICES, WHITE);
            rlDrawRenderBatchActive();
            PlasmaMaterial_End();
        }
    }
}

static void VC_ShieldShell_Draw3D(Camera3D cam)
{
    (void)cam;
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    // A translucent shell must only draw the camera-facing membrane. Rendering
    // both sides doubles every broad wisp into a milky white veil.
    rlEnableBackfaceCulling();
    // The entire membrane uses conventional alpha compositing. It must never
    // darken the scene as a side effect of its contrast treatment.
    BeginBlendMode(BLEND_ALPHA);
    ShieldShell_DrawPass();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();
    rlDrawRenderBatchActive();
}
