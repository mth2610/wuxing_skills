/* Behavioral integration guard for EffectMaterial's opt-in VFX path.
 * It compiles the real material_system.c against call-recording Raylib/Core
 * doubles. No GPU or GLSL executes here; the test observes loader selection,
 * uniforms and render/shader scope ordering. */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "raylib.h"

typedef enum {
    VFX_SURFACE_ALPHA = 0,
    VFX_SURFACE_ADDITIVE,
    VFX_SURFACE_PREMULTIPLIED
} VFXSurfaceMode;

typedef enum {
    VFX_RENDER_PASS_BODY = 0,
    VFX_RENDER_PASS_EMISSION
} VFXRenderPass;

typedef struct {
    bool active;
    bool depthWrite;
    VFXRenderPass pass;
    VFXSurfaceMode surface;
} VFXRenderScope;

#define CORE_VFX_RENDER_H
VFXRenderScope VFXRender_BeginDraw(VFXRenderPass pass,
                                   VFXSurfaceMode surface,
                                   bool depthWrite);
void VFXRender_EndDraw(VFXRenderScope *scope);
const char *VFXRender_OutputDefines(VFXSurfaceMode surface);

#define RESOURCE_MANAGER_H
Texture2D ResourceManager_LoadTexture(const char *path);
Shader ResourceManager_LoadShader(const char *vsPath, const char *fsPath);
Shader ResourceManager_LoadShaderVariant(const char *vsPath, const char *fsPath,
                                         const char *defines);

#define SKILL_MANAGER_H
#define ELEMENT_COLOR_WATER (Color){ 41, 128, 185, 255 }
#define ELEMENT_COLOR_FIRE  (Color){ 231, 76, 60, 255 }
#define ELEMENT_COLOR_EARTH (Color){ 230, 126, 34, 255 }
#define ELEMENT_COLOR_METAL (Color){ 149, 165, 166, 255 }
#define ELEMENT_COLOR_TAIJI (Color){ 155, 89, 182, 255 }
void SkillManager_BeginShader(Shader shader);
void SkillManager_EndShader(void);

enum {
    SHADER_UNIFORM_FLOAT = 0,
    SHADER_UNIFORM_VEC4,
    SHADER_UNIFORM_INT
};

int GetShaderLocation(Shader shader, const char *name);
void SetShaderValue(Shader shader, int loc, const void *value, int uniformType);
void SetShaderValueTexture(Shader shader, int loc, Texture2D texture);
Vector4 ColorNormalize(Color color);
void rlSetTexture(unsigned int id);
void rlDrawRenderBatchActive(void);

#include "../material/material_system.c"

enum {
    EVENT_RENDER_BEGIN = 1,
    EVENT_SHADER_BEGIN,
    EVENT_SHADER_END,
    EVENT_RENDER_END
};

static int failures;
static int events[16];
static int eventCount;
static int plainLoads;
static int variantLoads;
static const char *lastDefines;
static const char *lastVsPath;
static const char *lastFsPath;
static VFXSurfaceMode lastSurface;
static VFXRenderPass lastPass;
static bool lastDepthWrite;
static float bodyOpacityValue;
static Vector4 emissionColorValue;
static float emissionIntensityValue;
static float coreMaskValue;

static void Check(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void ResetTrace(void)
{
    memset(events, 0, sizeof(events));
    eventCount = 0;
    bodyOpacityValue = -1.0f;
    emissionColorValue = (Vector4){-1.0f, -1.0f, -1.0f, -1.0f};
    emissionIntensityValue = -1.0f;
    coreMaskValue = -1.0f;
}

Texture2D ResourceManager_LoadTexture(const char *path)
{
    (void)path;
    return (Texture2D){0};
}

Shader ResourceManager_LoadShader(const char *vsPath, const char *fsPath)
{
    (void)vsPath;
    (void)fsPath;
    plainLoads++;
    return (Shader){11u, NULL};
}

Shader ResourceManager_LoadShaderVariant(const char *vsPath, const char *fsPath,
                                         const char *defines)
{
    variantLoads++;
    lastVsPath = vsPath;
    lastFsPath = fsPath;
    lastDefines = defines;
    return (Shader){22u, NULL};
}

int GetShaderLocation(Shader shader, const char *name)
{
    (void)shader;
    if (strcmp(name, "u_vfxBodyOpacity") == 0) return 101;
    if (strcmp(name, "u_vfxEmissionColor") == 0) return 102;
    if (strcmp(name, "u_vfxEmissionIntensity") == 0) return 103;
    if (strcmp(name, "u_vfxCoreMask") == 0) return 104;
    return -1;
}

void SetShaderValue(Shader shader, int loc, const void *value, int uniformType)
{
    (void)shader;
    (void)uniformType;
    if (loc == 101) bodyOpacityValue = *(const float *)value;
    if (loc == 102) emissionColorValue = *(const Vector4 *)value;
    if (loc == 103) emissionIntensityValue = *(const float *)value;
    if (loc == 104) coreMaskValue = *(const float *)value;
}

void SetShaderValueTexture(Shader shader, int loc, Texture2D texture)
{
    (void)shader;
    (void)loc;
    (void)texture;
}

Vector4 ColorNormalize(Color color)
{
    return (Vector4){color.r / 255.0f, color.g / 255.0f,
                     color.b / 255.0f, color.a / 255.0f};
}

void rlSetTexture(unsigned int id) { (void)id; }
void rlDrawRenderBatchActive(void) {}

void SkillManager_BeginShader(Shader shader)
{
    (void)shader;
    events[eventCount++] = EVENT_SHADER_BEGIN;
}

void SkillManager_EndShader(void)
{
    events[eventCount++] = EVENT_SHADER_END;
}

const char *VFXRender_OutputDefines(VFXSurfaceMode surface)
{
    switch (surface) {
        case VFX_SURFACE_ADDITIVE: return "#define OUTPUT_EMISSION 1\n";
        case VFX_SURFACE_PREMULTIPLIED: return "#define OUTPUT_PREMULTIPLIED 1\n";
        case VFX_SURFACE_ALPHA:
        default: return "#define OUTPUT_BODY 1\n";
    }
}

VFXRenderScope VFXRender_BeginDraw(VFXRenderPass pass,
                                   VFXSurfaceMode surface,
                                   bool depthWrite)
{
    events[eventCount++] = EVENT_RENDER_BEGIN;
    lastPass = pass;
    lastSurface = surface;
    lastDepthWrite = depthWrite;
    return (VFXRenderScope){true, depthWrite, pass, surface};
}

void VFXRender_EndDraw(VFXRenderScope *scope)
{
    events[eventCount++] = EVENT_RENDER_END;
    scope->active = false;
}

int main(void)
{
    EffectMaterial legacy;
    EffectMaterial material;
    VFXRenderScope scope = {0};
    EffectMaterialParams params = {0};
    EffectMaterialVFXOutput output = {
        .surface = VFX_SURFACE_ADDITIVE,
        .bodyOpacity = 0.35f,
        .emissionColor = (Color){64, 128, 255, 255},
        .emissionIntensity = 4.5f,
        .coreMask = 0.8f
    };

    Material_LoadCustom(&legacy, &params);
    Check(plainLoads == 1 && variantLoads == 0,
          "legacy loader must keep the non-permuted shader path");
    Check(!legacy.vfxOutputEnabled,
          "legacy material must not opt into VFX render ownership");
    Check(!Material_BeginVFX(legacy, VFX_RENDER_PASS_BODY, false, &scope),
          "VFX scope must reject a legacy material");
    Check(!Material_BeginVFX(legacy, VFX_RENDER_PASS_BODY, false, NULL),
          "VFX scope must reject a missing output scope");
    Check(eventCount == 0, "rejected legacy scope must not change render state");

    Material_LoadCustomVFX(&material, &params, &output);
    Check(variantLoads == 1 && material.shader.id == 22u,
          "VFX loader must load a shader permutation");
    Check(lastDefines != NULL && strcmp(lastDefines, "#define OUTPUT_EMISSION 1\n") == 0,
          "additive surface must select OUTPUT_EMISSION");
    Check(material.vfxOutputEnabled && material.surface == VFX_SURFACE_ADDITIVE,
          "loaded material must retain its selected surface");

    ResetTrace();
    Check(!Material_BeginVFX(material, VFX_RENDER_PASS_BODY, false, &scope),
          "additive output must be rejected from the BODY pass");
    Check(eventCount == 0,
          "a surface/pass mismatch must not change render or shader state");

    Check(Material_BeginVFX(material, VFX_RENDER_PASS_EMISSION, false, &scope),
          "surface-aware material must open a VFX scope");
    Check(eventCount == 2 && events[0] == EVENT_RENDER_BEGIN &&
              events[1] == EVENT_SHADER_BEGIN,
          "render state must begin before shader/uniform application");
    Check(lastPass == VFX_RENDER_PASS_EMISSION &&
              lastSurface == VFX_SURFACE_ADDITIVE && !lastDepthWrite,
          "runtime scope must receive the requested pass, surface and depth policy");
    Check(fabsf(bodyOpacityValue - 0.35f) < 0.0001f,
          "body opacity must reach the shader");
    Check(fabsf(emissionColorValue.x - 64.0f / 255.0f) < 0.0001f &&
              fabsf(emissionColorValue.y - 128.0f / 255.0f) < 0.0001f &&
              fabsf(emissionColorValue.z - 1.0f) < 0.0001f,
          "emission hue must be normalized independently of HDR intensity");
    Check(fabsf(emissionIntensityValue - 4.5f) < 0.0001f &&
              fabsf(coreMaskValue - 0.8f) < 0.0001f,
          "HDR intensity and core mask must reach the shader unchanged");

    Material_EndVFX(&scope);
    Check(eventCount == 4 && events[2] == EVENT_SHADER_END &&
              events[3] == EVENT_RENDER_END,
          "shader must end before unified render state is restored");
    Check(!scope.active, "ending the draw must consume the active scope");

    output.surface = VFX_SURFACE_PREMULTIPLIED;
    Material_LoadCustomShaderVFX(&material, &params,
                                 "custom_surface.vs", "custom_surface.fs", &output);
    Check(strcmp(lastDefines, "#define OUTPUT_PREMULTIPLIED 1\n") == 0,
          "premultiplied surface must select OUTPUT_PREMULTIPLIED");
    Check(strcmp(lastVsPath, "custom_surface.vs") == 0 &&
              strcmp(lastFsPath, "custom_surface.fs") == 0,
          "custom VFX loader must preserve the caller's shader paths");

    output.surface = (VFXSurfaceMode)99;
    Material_LoadCustomVFX(&material, &params, &output);
    Check(material.surface == VFX_SURFACE_ALPHA &&
              strcmp(lastDefines, "#define OUTPUT_BODY 1\n") == 0,
          "invalid surfaces must fall back to a matching alpha BODY pair");

    puts(failures ? "material VFX runtime: FAIL" : "material VFX runtime: PASS");
    return failures != 0;
}
