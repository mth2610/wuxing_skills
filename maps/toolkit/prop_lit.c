#include "maps/toolkit/prop_lit.h"
#include "maps/toolkit/map_shadow.h"
#include "core/resource_manager.h"
#include "post_process/camera_context.h"
#include "core/vfx_light.h"
#include "environment/environment_system.h"
#include "raymath.h"
#include "rlgl.h"
#include <stddef.h>

static unsigned int s_cachedShaderId = 0;
static int s_locCloudWorldFromShader = -1;
static int s_locLightDir = -1;
static int s_locLightColor = -1;
static int s_locAmbientColor = -1;
static int s_locViewPos = -1;

Shader PropLit_GetShader(void)
{
  Shader shader = ResourceManager_LoadShader("maps/toolkit/shaders/prop_lit.vs",
                                             "maps/toolkit/shaders/prop_lit.fs");

  // CORE_ISSUES.md Item 36 gotcha: LoadShaderFromMemory (what
  // ResourceManager_LoadShader wraps) only auto-binds a small fixed set of
  // default uniform/attribute names, and "matModel" is confirmed NOT among
  // them. Track by shader.id so hot-reloads or shader recreations re-bind
  // all attribute, sampler, and cached uniform locations cleanly.
  if (shader.id != 0 && shader.locs != NULL &&
      (s_cachedShaderId != shader.id || shader.locs[SHADER_LOC_MATRIX_MODEL] < 0))
  {
    shader.locs[SHADER_LOC_VERTEX_POSITION] =
        GetShaderLocationAttrib(shader, "vertexPosition");
    shader.locs[SHADER_LOC_VERTEX_TEXCOORD01] =
        GetShaderLocationAttrib(shader, "vertexTexCoord");
    shader.locs[SHADER_LOC_VERTEX_NORMAL] =
        GetShaderLocationAttrib(shader, "vertexNormal");
    shader.locs[SHADER_LOC_VERTEX_TANGENT] =
        GetShaderLocationAttrib(shader, "vertexTangent");

    shader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(shader, "mvp");
    shader.locs[SHADER_LOC_MATRIX_MODEL] =
        GetShaderLocation(shader, "matModel");
    shader.locs[SHADER_LOC_COLOR_DIFFUSE] =
        GetShaderLocation(shader, "colDiffuse");
    shader.locs[SHADER_LOC_MAP_DIFFUSE] =
        GetShaderLocation(shader, "texture0");
    shader.locs[SHADER_LOC_MAP_NORMAL] =
        GetShaderLocation(shader, "texture2");
    shader.locs[SHADER_LOC_MAP_ROUGHNESS] =
        GetShaderLocation(shader, "texture3");
    shader.locs[SHADER_LOC_MAP_METALNESS] =
        GetShaderLocation(shader, "u_cloudNoise");

    s_locCloudWorldFromShader = GetShaderLocation(shader, "u_cloudWorldFromShader");
    s_locLightDir = GetShaderLocation(shader, "u_lightDir");
    s_locLightColor = GetShaderLocation(shader, "u_lightColor");
    s_locAmbientColor = GetShaderLocation(shader, "u_ambientColor");
    s_locViewPos = GetShaderLocation(shader, "u_viewPos");

    MapShadow_ConfigureShader(shader);

    // Đợt E / E2 — opt into the VFX point-light pool.
    VFXLight_RegisterShader(shader);

    s_cachedShaderId = shader.id;
  }
  return shader;
}

Material PropLit_MakeMaterial(Texture2D diffuse, Texture2D normal,
                              Texture2D roughness)
{
  Material mat = LoadMaterialDefault();
  mat.shader = PropLit_GetShader();

  if (diffuse.id != 0)
    mat.maps[MATERIAL_MAP_DIFFUSE].texture = diffuse;
  mat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;

  if (normal.id != 0)
    mat.maps[MATERIAL_MAP_NORMAL].texture = normal;
  if (roughness.id != 0)
    mat.maps[MATERIAL_MAP_ROUGHNESS].texture = roughness;

  Texture2D cloudNoise = Environment_GetCloudShadowFrame().noiseTexture;
  if (cloudNoise.id != 0)
    mat.maps[MATERIAL_MAP_METALNESS].texture = cloudNoise;

  MapShadow_AttachMaterial(&mat);

  return mat;
}

void PropLit_UpdateLighting(void)
{
  Shader shader = PropLit_GetShader();
  if (shader.id == 0)
    return;

  rlDrawRenderBatchActive();
  BeginShaderMode(shader);
  Environment_BindCloudShadowShader(shader);

  Matrix shaderFromWorld = rlGetMatrixTransform();
  if (s_locCloudWorldFromShader >= 0)
  {
    SetShaderValueMatrix(shader, s_locCloudWorldFromShader,
                         MatrixInvert(shaderFromWorld));
  }

  if (s_locLightDir >= 0)
  {
    // Environment_GetSunDirection() is the direction light TRAVELS (sky->ground);
    // dot(normal, lightDir) requires the surface->light direction.
    Vector3 lightDir = Vector3Negate(Environment_GetSunDirection());
    // Transform direction vector by upper-left 3x3 of shaderFromWorld without translation
    Vector3 viewLightDir = {
        shaderFromWorld.m0 * lightDir.x + shaderFromWorld.m4 * lightDir.y + shaderFromWorld.m8 * lightDir.z,
        shaderFromWorld.m1 * lightDir.x + shaderFromWorld.m5 * lightDir.y + shaderFromWorld.m9 * lightDir.z,
        shaderFromWorld.m2 * lightDir.x + shaderFromWorld.m6 * lightDir.y + shaderFromWorld.m10 * lightDir.z};
    viewLightDir = Vector3Normalize(viewLightDir);
    SetShaderValue(shader, s_locLightDir, &viewLightDir, SHADER_UNIFORM_VEC3);
  }

  if (s_locLightColor >= 0)
  {
    Vector4 c = ColorNormalize(Environment_GetSunColor());
    Vector3 rgb = Vector3Scale((Vector3){c.x, c.y, c.z}, Environment_GetSunIntensity());
    SetShaderValue(shader, s_locLightColor, &rgb, SHADER_UNIFORM_VEC3);
  }

  if (s_locAmbientColor >= 0)
  {
    Vector4 c = ColorNormalize(Environment_GetAmbientColor());
    Vector3 rgb = {c.x, c.y, c.z};
    SetShaderValue(shader, s_locAmbientColor, &rgb, SHADER_UNIFORM_VEC3);
  }

  if (s_locViewPos >= 0)
  {
    Vector3 viewPos = Vector3Transform(camera.position, shaderFromWorld);
    SetShaderValue(shader, s_locViewPos, &viewPos, SHADER_UNIFORM_VEC3);
  }

  MapShadow_UpdateShader(shader);
  EndShaderMode();
}