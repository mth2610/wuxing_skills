#include "core/vfx_layered_field.h"
#include "core/geometry/procedural_mesh_utils.h"
#include "core/resource_manager.h"
#include "rlgl.h"
#include <stddef.h>

static Shader s_annulusShader = {0};
static int s_locPass = -1;
static int s_locDebug = -1;
static int s_locInner = -1;
static int s_locOuter = -1;
static int s_locTime = -1;
static int s_locPhase = -1;
static int s_locOpacity = -1;
static int s_locEmission = -1;
static int s_locBodyColor = -1;
static int s_locEdgeColor = -1;
static int s_locAccentColor = -1;
static int s_locEmissionColor = -1;
static int s_locLobeCenters = -1;
static int s_locLobeWidths = -1;
static int s_locLobeCount = -1;

static void VFXLayeredAnnulus_Init(void)
{
    if (s_annulusShader.id != 0) return;
    s_annulusShader = ResourceManager_LoadShader(NULL,
        "core/shaders/vfx_layered_annulus.fs");
    s_locPass = GetShaderLocation(s_annulusShader, "u_semanticPass");
    s_locDebug = GetShaderLocation(s_annulusShader, "u_debugLayer");
    s_locInner = GetShaderLocation(s_annulusShader, "u_innerRadius");
    s_locOuter = GetShaderLocation(s_annulusShader, "u_outerRadius");
    s_locTime = GetShaderLocation(s_annulusShader, "u_fieldTime");
    s_locPhase = GetShaderLocation(s_annulusShader, "u_phase");
    s_locOpacity = GetShaderLocation(s_annulusShader, "u_opacity");
    s_locEmission = GetShaderLocation(s_annulusShader, "u_emissionStrength");
    s_locBodyColor = GetShaderLocation(s_annulusShader, "u_bodyColor");
    s_locEdgeColor = GetShaderLocation(s_annulusShader, "u_edgeColor");
    s_locAccentColor = GetShaderLocation(s_annulusShader, "u_accentColor");
    s_locEmissionColor = GetShaderLocation(s_annulusShader, "u_emissionColor");
    s_locLobeCenters = GetShaderLocation(s_annulusShader, "u_lobeCenters");
    s_locLobeWidths = GetShaderLocation(s_annulusShader, "u_lobeWidths");
    s_locLobeCount = GetShaderLocation(s_annulusShader, "u_lobeCount");
}

static Vector4 ColorVector(Color color)
{
    return (Vector4){color.r / 255.0f, color.g / 255.0f,
                     color.b / 255.0f, color.a / 255.0f};
}

static void VFXLayeredAnnulus_Draw(const VFXLayeredAnnulusParams *params,
                                   int semanticPass)
{
    if (params == NULL || params->halfSize <= 0.0f || params->opacity <= 0.0f)
        return;
    VFXLayeredAnnulus_Init();
    if (s_annulusShader.id == 0) return;

    Vector4 body = ColorVector(params->bodyColor);
    Vector4 edge = ColorVector(params->edgeColor);
    Vector4 accent = ColorVector(params->accentColor);
    Vector4 emission = ColorVector(params->emissionColor);
    int lobeCount = params->lobeCount;
    if (lobeCount < 0) lobeCount = 0;
    if (lobeCount > 4) lobeCount = 4;

    rlDrawRenderBatchActive();
    BeginShaderMode(s_annulusShader);
    /* rlvk SetShaderValue targets the active shader, so every field uniform is
     * deliberately uploaded inside this scope. */
    SetShaderValue(s_annulusShader, s_locPass, &semanticPass, SHADER_UNIFORM_INT);
    SetShaderValue(s_annulusShader, s_locDebug, &params->debugLayer, SHADER_UNIFORM_INT);
    SetShaderValue(s_annulusShader, s_locInner, &params->innerRadius, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_annulusShader, s_locOuter, &params->outerRadius, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_annulusShader, s_locTime, &params->time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_annulusShader, s_locPhase, &params->phase, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_annulusShader, s_locOpacity, &params->opacity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_annulusShader, s_locEmission, &params->emissionStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_annulusShader, s_locBodyColor, &body, SHADER_UNIFORM_VEC4);
    SetShaderValue(s_annulusShader, s_locEdgeColor, &edge, SHADER_UNIFORM_VEC4);
    SetShaderValue(s_annulusShader, s_locAccentColor, &accent, SHADER_UNIFORM_VEC4);
    SetShaderValue(s_annulusShader, s_locEmissionColor, &emission, SHADER_UNIFORM_VEC4);
    SetShaderValue(s_annulusShader, s_locLobeCenters, &params->lobeCenters, SHADER_UNIFORM_VEC4);
    SetShaderValue(s_annulusShader, s_locLobeWidths, &params->lobeWidths, SHADER_UNIFORM_VEC4);
    SetShaderValue(s_annulusShader, s_locLobeCount, &lobeCount, SHADER_UNIFORM_INT);

    DrawCoreOrientedQuad(params->center, params->normal, params->halfSize, WHITE);
    rlDrawRenderBatchActive();
    EndShaderMode();
}

void VFXLayeredAnnulus_DrawBody(const VFXLayeredAnnulusParams *params)
{
    VFXLayeredAnnulus_Draw(params, 0);
}

void VFXLayeredAnnulus_DrawEmission(const VFXLayeredAnnulusParams *params)
{
    VFXLayeredAnnulus_Draw(params, 1);
}
