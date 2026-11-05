#include "maps/toolkit/grass_material.h"
#include "core/resource_manager.h"
#include "environment/environment_system.h"
#include "core/vfx_light.h"
#include "core/gfx_quality.h"
#include "raymath.h"
#include "rlgl.h"
#include <stddef.h>

static unsigned int s_cachedShaderId = 0;
static int s_locBaseTileSize = -1;
static int s_locDetailTileSize = -1;
static int s_locMaskNoiseScale = -1;
static int s_locColorVarScale = -1;
static int s_locLightDir = -1;
static int s_locLightColor = -1;
static int s_locAmbientColor = -1;

Shader GrassMaterial_GetShader(void)
{
  Shader shader = ResourceManager_LoadShader("maps/toolkit/shaders/grass_material.vs",
                                             "maps/toolkit/shaders/grass_material.fs");

  // Same shader.locs[] gotcha as PropLit_GetShader (CORE_ISSUES.md Item 36):
  // Track by shader.id so hot-reloads re-bind cleanly and VFXLight registration
  // only runs once per shader lifetime instead of every frame.
  if (shader.id != 0 && shader.locs != NULL &&
      (s_cachedShaderId != shader.id || shader.locs[SHADER_LOC_MATRIX_MODEL] < 0))
  {
    shader.locs[SHADER_LOC_VERTEX_POSITION] =
        GetShaderLocationAttrib(shader, "vertexPosition");
    shader.locs[SHADER_LOC_VERTEX_TEXCOORD01] =
        GetShaderLocationAttrib(shader, "vertexTexCoord");
    shader.locs[SHADER_LOC_VERTEX_NORMAL] =
        GetShaderLocationAttrib(shader, "vertexNormal");

    shader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(shader, "mvp");
    shader.locs[SHADER_LOC_MATRIX_MODEL] =
        GetShaderLocation(shader, "matModel");
    shader.locs[SHADER_LOC_COLOR_DIFFUSE] =
        GetShaderLocation(shader, "colDiffuse");

    shader.locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(shader, "texture0");
    shader.locs[SHADER_LOC_MAP_NORMAL] = GetShaderLocation(shader, "texture2");
    shader.locs[SHADER_LOC_MAP_ROUGHNESS] =
        GetShaderLocation(shader, "texture3");

    s_locBaseTileSize = GetShaderLocation(shader, "u_baseTileSize");
    s_locDetailTileSize = GetShaderLocation(shader, "u_detailTileSize");
    s_locMaskNoiseScale = GetShaderLocation(shader, "u_maskNoiseScale");
    s_locColorVarScale = GetShaderLocation(shader, "u_colorVarScale");
    s_locLightDir = GetShaderLocation(shader, "u_lightDir");
    s_locLightColor = GetShaderLocation(shader, "u_lightColor");
    s_locAmbientColor = GetShaderLocation(shader, "u_ambientColor");

    VFXLight_RegisterShader(shader);
    s_cachedShaderId = shader.id;
  }
  return shader;
}

Material GrassMaterial_Make(GrassMaterialConfig config)
{
  Material mat = LoadMaterialDefault();
  mat.shader = GrassMaterial_GetShader();

  if (config.grassBase.id != 0)
    mat.maps[MATERIAL_MAP_DIFFUSE].texture = config.grassBase;
  mat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;

  if (config.grassDetail.id != 0)
    mat.maps[MATERIAL_MAP_NORMAL].texture = config.grassDetail;
  if (config.dirt.id != 0)
    mat.maps[MATERIAL_MAP_ROUGHNESS].texture = config.dirt;

  if (mat.shader.id != 0)
  {
    BeginShaderMode(mat.shader);
    if (s_locBaseTileSize >= 0)
      SetShaderValue(mat.shader, s_locBaseTileSize, &config.baseTileSize,
                     SHADER_UNIFORM_FLOAT);
    if (s_locDetailTileSize >= 0)
      SetShaderValue(mat.shader, s_locDetailTileSize, &config.detailTileSize,
                     SHADER_UNIFORM_FLOAT);
    if (s_locMaskNoiseScale >= 0)
      SetShaderValue(mat.shader, s_locMaskNoiseScale, &config.maskNoiseScale,
                     SHADER_UNIFORM_FLOAT);
    if (s_locColorVarScale >= 0)
      SetShaderValue(mat.shader, s_locColorVarScale, &config.colorVarScale,
                     SHADER_UNIFORM_FLOAT);
    EndShaderMode();
  }

  return mat;
}

void GrassMaterial_UpdateLighting(void)
{
  Shader shader = GrassMaterial_GetShader();
  if (shader.id == 0)
    return;

  rlDrawRenderBatchActive();
  BeginShaderMode(shader);

  if (s_locLightDir >= 0)
  {
    Vector3 lightDir = Vector3Negate(Environment_GetSunDirection());
    Matrix shaderFromWorld = rlGetMatrixTransform();
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

  EndShaderMode();
}