#include "core/surface_material.h"
#include "core/resource_manager.h"
#include "environment/environment_system.h"
#include "raymath.h"
#include <stddef.h> // NULL

static Shader s_shader;
static bool   s_ready = false;

// Per-frame uniform locations
static int s_locSunToLight, s_locSunColor, s_locAmbient, s_locViewPos;
static int s_locFogColor, s_locFogStart, s_locFogEnd, s_locFogEnabled;

static inline Vector3 ColorToVec3(Color c) {
    return (Vector3){ c.r / 255.0f, c.g / 255.0f, c.b / 255.0f };
}

void SurfaceMaterial_Init(void) {
    s_shader = ResourceManager_LoadShader("core/shaders/surface_lit.vs",
                                          "core/shaders/surface_lit.fs");
    // Standard raylib attribute/uniform names auto-resolve; wire the ones
    // raylib needs to auto-update every DrawMesh (model/normal matrices, tint).
    s_shader.locs[SHADER_LOC_MATRIX_MODEL]  = GetShaderLocation(s_shader, "matModel");
    s_shader.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(s_shader, "matNormal");
    s_shader.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(s_shader, "colDiffuse");

    s_locSunToLight = GetShaderLocation(s_shader, "u_sunToLight");
    s_locSunColor   = GetShaderLocation(s_shader, "u_sunColor");
    s_locAmbient    = GetShaderLocation(s_shader, "u_ambientColor");
    s_locViewPos    = GetShaderLocation(s_shader, "u_viewPos");
    s_locFogColor   = GetShaderLocation(s_shader, "u_fogColor");
    s_locFogStart   = GetShaderLocation(s_shader, "u_fogStart");
    s_locFogEnd     = GetShaderLocation(s_shader, "u_fogEnd");
    s_locFogEnabled = GetShaderLocation(s_shader, "u_fogEnabled");

    // Material constants — the stylized "moonlight" identity. Cool blue-white
    // rim traces the silhouette; a tight Blinn sheen adds a wet/silk highlight.
    // Kept below 1.0 so the rim reads as a moonlight edge, NOT a bloom halo
    // (values > 1 cross the HDR bloom threshold and glow the whole silhouette).
    Vector3 rimColor    = { 0.34f, 0.46f, 0.72f }; // cool moonlight
    float   rimPower    = 4.0f;                     // thin edge
    float   rimStrength = 0.35f;
    float   specStrength = 0.30f;
    float   shininess    = 48.0f;
    SetShaderValue(s_shader, GetShaderLocation(s_shader, "u_rimColor"),   &rimColor,    SHADER_UNIFORM_VEC3);
    SetShaderValue(s_shader, GetShaderLocation(s_shader, "u_rimPower"),   &rimPower,    SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shader, GetShaderLocation(s_shader, "u_rimStrength"),&rimStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shader, GetShaderLocation(s_shader, "u_specStrength"),&specStrength,SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shader, GetShaderLocation(s_shader, "u_shininess"),  &shininess,   SHADER_UNIFORM_FLOAT);

    s_ready = true;
}

Shader SurfaceMaterial_GetShader(void) { return s_shader; }

void SurfaceMaterial_Apply(Model *model) {
    if (!s_ready || model == NULL) return;
    for (int i = 0; i < model->materialCount; i++) {
        model->materials[i].shader = s_shader;
    }
}

void SurfaceMaterial_UpdateFrame(Camera3D camera) {
    if (!s_ready) return;

    // environment sun direction points the way light TRAVELS (downward); the
    // shader wants the direction from the surface toward the sun.
    Vector3 sunToLight = Vector3Normalize(Vector3Negate(Environment_GetSunDirection()));
    Vector3 sunColor   = ColorToVec3(Environment_GetSunColor());
    Vector3 ambient    = ColorToVec3(Environment_GetAmbientColor());
    Vector3 viewPos    = camera.position;

    SetShaderValue(s_shader, s_locSunToLight, &sunToLight, SHADER_UNIFORM_VEC3);
    SetShaderValue(s_shader, s_locSunColor,   &sunColor,   SHADER_UNIFORM_VEC3);
    SetShaderValue(s_shader, s_locAmbient,    &ambient,    SHADER_UNIFORM_VEC3);
    SetShaderValue(s_shader, s_locViewPos,    &viewPos,    SHADER_UNIFORM_VEC3);

    EnvFogConfig fog = Environment_GetFogConfig();
    Vector3 fogColor = ColorToVec3(fog.color);
    float fogEnabled = fog.enabled ? 1.0f : 0.0f;
    SetShaderValue(s_shader, s_locFogColor,   &fogColor,    SHADER_UNIFORM_VEC3);
    SetShaderValue(s_shader, s_locFogStart,   &fog.start,   SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shader, s_locFogEnd,     &fog.end,     SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shader, s_locFogEnabled, &fogEnabled,  SHADER_UNIFORM_FLOAT);
}
