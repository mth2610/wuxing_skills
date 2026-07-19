#include "core/surface_material.h"
#include "core/resource_manager.h"
#include "core/gfx_quality.h"
#include "environment/environment_system.h"
#include "environment/env_shadow.h"
#include "raymath.h"
#include <stddef.h> // NULL

static Shader s_shader;
static bool   s_ready = false;

// Per-frame uniform locations
static int s_locQualityTier;
static int s_locSunToLight, s_locSunColor, s_locSky, s_locGround, s_locViewPos;
static int s_locFogColor, s_locFogStart, s_locFogEnd, s_locFogEnabled;
static int s_locMatcapTex, s_locHasMatcap, s_locMatcapAmount;
static int s_locNormalMap, s_locHasNormalMap;
static int s_locAniso, s_locAnisoShininess, s_locSssStrength, s_locSssPower;
static int s_locLightVP, s_locShadowMap, s_locShadowEnabled;

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
    s_shader.locs[SHADER_LOC_MATRIX_VIEW]   = GetShaderLocation(s_shader, "matView"); // P3c matcap
    s_shader.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(s_shader, "colDiffuse");
    s_shader.locs[SHADER_LOC_VERTEX_TANGENT] = GetShaderLocationAttrib(s_shader, "vertexTangent"); // P5a

    s_locQualityTier = GetShaderLocation(s_shader, "u_qualityTier");
    s_locSunToLight = GetShaderLocation(s_shader, "u_sunToLight");
    s_locSunColor   = GetShaderLocation(s_shader, "u_sunColor");
    s_locSky        = GetShaderLocation(s_shader, "u_skyColor");
    s_locGround     = GetShaderLocation(s_shader, "u_groundColor");
    s_locViewPos    = GetShaderLocation(s_shader, "u_viewPos");
    s_locFogColor   = GetShaderLocation(s_shader, "u_fogColor");
    s_locFogStart   = GetShaderLocation(s_shader, "u_fogStart");
    s_locFogEnd     = GetShaderLocation(s_shader, "u_fogEnd");
    s_locFogEnabled = GetShaderLocation(s_shader, "u_fogEnabled");
    s_locMatcapTex     = GetShaderLocation(s_shader, "matcapTex");
    s_locHasMatcap     = GetShaderLocation(s_shader, "u_hasMatcap");
    s_locMatcapAmount  = GetShaderLocation(s_shader, "u_matcapAmount");
    s_locNormalMap      = GetShaderLocation(s_shader, "normalMap");
    s_locHasNormalMap   = GetShaderLocation(s_shader, "u_hasNormalMap");
    s_locAniso          = GetShaderLocation(s_shader, "u_aniso");
    s_locAnisoShininess = GetShaderLocation(s_shader, "u_anisoShininess");
    s_locSssStrength    = GetShaderLocation(s_shader, "u_sssStrength");
    s_locSssPower       = GetShaderLocation(s_shader, "u_sssPower");
    s_locLightVP        = GetShaderLocation(s_shader, "u_lightVP");
    s_locShadowMap      = GetShaderLocation(s_shader, "shadowMap");
    s_locShadowEnabled  = GetShaderLocation(s_shader, "u_shadowEnabled");

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

    // Real Shading P1d — default emissive off; no visual change until a
    // future per-model setter opts a material into a glow tint.
    Vector3 emissiveColor = { 0.0f, 0.0f, 0.0f };
    SetShaderValue(s_shader, GetShaderLocation(s_shader, "u_emissiveColor"), &emissiveColor, SHADER_UNIFORM_VEC3);

    // Real Shading P3c — matcap off by default; SetMatcapActive turns it on
    // around specific draw calls.
    float hasMatcap = 0.0f;
    SetShaderValue(s_shader, s_locHasMatcap, &hasMatcap, SHADER_UNIFORM_FLOAT);

    // Real Shading P5 — HIGH-tier extras off by default (normal map/aniso/SSS
    // setters opt a specific draw call in; zero visual/perf change otherwise).
    float zero = 0.0f;
    SetShaderValue(s_shader, s_locHasNormalMap, &zero, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shader, s_locAniso,        &zero, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shader, s_locSssStrength,  &zero, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shader, s_locShadowEnabled,&zero, SHADER_UNIFORM_FLOAT);

    s_ready = true;
}

Shader SurfaceMaterial_GetShader(void) { return s_shader; }

void SurfaceMaterial_Apply(Model *model) {
    if (!s_ready || model == NULL) return;
    for (int i = 0; i < model->materialCount; i++) {
        model->materials[i].shader = s_shader;
    }
    // Real Shading P5a — ensure tangents exist for HIGH-tier normal mapping
    // even on models whose export didn't author them (no-op cost if the mesh
    // lacks texcoords/normals; GenMeshTangents warns and returns in that case).
    for (int i = 0; i < model->meshCount; i++) {
        GenMeshTangents(&model->meshes[i]);
    }
}

void SurfaceMaterial_UpdateFrame(Camera3D camera) {
    if (!s_ready) return;

    int tier = (int)GfxQuality_Get();
    SetShaderValue(s_shader, s_locQualityTier, &tier, SHADER_UNIFORM_INT);

    // environment sun direction points the way light TRAVELS (downward); the
    // shader wants the direction from the surface toward the sun.
    Vector3 sunToLight = Vector3Normalize(Vector3Negate(Environment_GetSunDirection()));
    Vector3 sunColor   = ColorToVec3(Environment_GetSunColor());
    Vector3 sky        = ColorToVec3(Environment_GetSkyAmbient());
    Vector3 ground     = ColorToVec3(Environment_GetGroundAmbient());
    Vector3 viewPos    = camera.position;

    SetShaderValue(s_shader, s_locSunToLight, &sunToLight, SHADER_UNIFORM_VEC3);
    SetShaderValue(s_shader, s_locSunColor,   &sunColor,   SHADER_UNIFORM_VEC3);
    SetShaderValue(s_shader, s_locSky,        &sky,        SHADER_UNIFORM_VEC3);
    SetShaderValue(s_shader, s_locGround,     &ground,     SHADER_UNIFORM_VEC3);
    SetShaderValue(s_shader, s_locViewPos,    &viewPos,    SHADER_UNIFORM_VEC3);

    EnvFogConfig fog = Environment_GetFogConfig();
    Vector3 fogColor = ColorToVec3(fog.color);
    float fogEnabled = fog.enabled ? 1.0f : 0.0f;
    SetShaderValue(s_shader, s_locFogColor,   &fogColor,    SHADER_UNIFORM_VEC3);
    SetShaderValue(s_shader, s_locFogStart,   &fog.start,   SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shader, s_locFogEnd,     &fog.end,     SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shader, s_locFogEnabled, &fogEnabled,  SHADER_UNIFORM_FLOAT);

    // Real Shading P6 — shadow map (HIGH tier only in-shader; harmless to
    // push always, EnvShadow_GetLightVP/GetShadowMap return stale-but-valid
    // data when disabled since the uniform is gated by u_shadowEnabled).
    float shadowEnabled = EnvShadow_IsEnabled() ? 1.0f : 0.0f;
    SetShaderValue(s_shader, s_locShadowEnabled, &shadowEnabled, SHADER_UNIFORM_FLOAT);
    if (EnvShadow_IsEnabled()) {
        Matrix lightVP = EnvShadow_GetLightVP();
        SetShaderValueMatrix(s_shader, s_locLightVP, lightVP);
        SetShaderValueTexture(s_shader, s_locShadowMap, EnvShadow_GetShadowMap());
    }
}

void SurfaceMaterial_SetMatcapActive(Texture2D matcap, float amount) {
    if (!s_ready) return;
    float hasMatcap = 1.0f;
    SetShaderValueTexture(s_shader, s_locMatcapTex, matcap);
    SetShaderValue(s_shader, s_locHasMatcap,    &hasMatcap, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shader, s_locMatcapAmount, &amount,    SHADER_UNIFORM_FLOAT);
}

void SurfaceMaterial_ClearMatcap(void) {
    if (!s_ready) return;
    float hasMatcap = 0.0f;
    SetShaderValue(s_shader, s_locHasMatcap, &hasMatcap, SHADER_UNIFORM_FLOAT);
}

void SurfaceMaterial_SetNormalMapActive(Texture2D normalMap) {
    if (!s_ready) return;
    float has = 1.0f;
    SetShaderValueTexture(s_shader, s_locNormalMap, normalMap);
    SetShaderValue(s_shader, s_locHasNormalMap, &has, SHADER_UNIFORM_FLOAT);
}

void SurfaceMaterial_ClearNormalMap(void) {
    if (!s_ready) return;
    float has = 0.0f;
    SetShaderValue(s_shader, s_locHasNormalMap, &has, SHADER_UNIFORM_FLOAT);
}

void SurfaceMaterial_SetAniso(float anisoShininess) {
    if (!s_ready) return;
    float on = 1.0f;
    SetShaderValue(s_shader, s_locAniso,          &on,             SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shader, s_locAnisoShininess, &anisoShininess, SHADER_UNIFORM_FLOAT);
}

void SurfaceMaterial_ClearAniso(void) {
    if (!s_ready) return;
    float off = 0.0f;
    SetShaderValue(s_shader, s_locAniso, &off, SHADER_UNIFORM_FLOAT);
}

void SurfaceMaterial_SetSSS(float strength, float power) {
    if (!s_ready) return;
    SetShaderValue(s_shader, s_locSssStrength, &strength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shader, s_locSssPower,     &power,    SHADER_UNIFORM_FLOAT);
}

void SurfaceMaterial_ClearSSS(void) {
    if (!s_ready) return;
    float zero = 0.0f;
    SetShaderValue(s_shader, s_locSssStrength, &zero, SHADER_UNIFORM_FLOAT);
}

void SurfaceMaterial_BeginShadowCast(Model model, Shader depthShader) {
    for (int i = 0; i < model.materialCount; i++) model.materials[i].shader = depthShader;
}

void SurfaceMaterial_EndShadowCast(Model model) {
    if (!s_ready) return;
    for (int i = 0; i < model.materialCount; i++) model.materials[i].shader = s_shader;
}
