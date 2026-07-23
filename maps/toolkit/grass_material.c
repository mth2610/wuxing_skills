#include "maps/toolkit/grass_material.h"
#include "core/resource_manager.h"
#include "environment/environment_system.h"
#include "core/vfx_light.h"
#include "core/gfx_quality.h"
#include "raymath.h"
#include <stddef.h>

Shader GrassMaterial_GetShader(void) {
  Shader shader = ResourceManager_LoadShader("maps/toolkit/shaders/grass_material.vs",
                                              "maps/toolkit/shaders/grass_material.fs");
  VFXLight_RegisterShader(shader);   // Đợt E / E2 — main.c binds each frame

  // Same shader.locs[] gotcha as PropLit_GetShader (CORE_ISSUES.md Item 36):
  // LoadShaderFromMemory only auto-binds a small fixed default set of
  // uniform/attribute names, and this shader is assigned into a Model's
  // Material slot and drawn via plain DrawModel()/DrawModelEx() — so
  // raylib's own DrawMesh() reads shader.locs[SHADER_LOC_MATRIX_MODEL] (and
  // the vertex-attribute locs below) directly. Fix all of them explicitly
  // by name once, rather than trust the incomplete auto-bind list.
  //
  // Unlike Item 37's version, this material DOES have textures (3 of them),
  // reusing prop_lit.c's texture0/texture2/texture3 sampler-name convention
  // (MATERIAL_MAP_DIFFUSE/NORMAL/ROUGHNESS slots as generic texture-slot
  // carriers, not for their raylib-semantic meaning) — so the map locs are
  // fixed to real uniform locations here, not left at -1.
  static bool s_locsFixed = false;
  if (!s_locsFixed && shader.id != 0 && shader.locs != NULL) {
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

    // grassBase -> texture0 (MATERIAL_MAP_DIFFUSE slot)
    // grassDetail -> texture2 (MATERIAL_MAP_NORMAL slot, reused as a plain
    //   grayscale-grain carrier, not an actual normal map)
    // dirt -> texture3 (MATERIAL_MAP_ROUGHNESS slot, reused as a plain
    //   diffuse-photo carrier, not an actual roughness map)
    shader.locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(shader, "texture0");
    shader.locs[SHADER_LOC_MAP_NORMAL] = GetShaderLocation(shader, "texture2");
    shader.locs[SHADER_LOC_MAP_ROUGHNESS] =
        GetShaderLocation(shader, "texture3");

    s_locsFixed = true;
  }
  return shader;
}

Material GrassMaterial_Make(GrassMaterialConfig config) {
  // LoadMaterialDefault() gives a Material with a properly allocated
  // maps[MAX_MATERIAL_MAPS] array (raylib-internal allocation, not ours —
  // core's "no malloc" rule is about our own code, not raylib's own Load*
  // APIs). A hand-built Material{0} would leave maps == NULL and crash the
  // first time DrawMesh() indexes into it.
  Material mat = LoadMaterialDefault();
  mat.shader = GrassMaterial_GetShader();

  mat.maps[MATERIAL_MAP_DIFFUSE].texture = config.grassBase;
  mat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE; // colDiffuse base; DrawModelEx's tint multiplies into this per-draw
  mat.maps[MATERIAL_MAP_NORMAL].texture = config.grassDetail;
  mat.maps[MATERIAL_MAP_ROUGHNESS].texture = config.dirt;

  // baseTileSize/detailTileSize/maskNoiseScale/colorVarScale are static
  // per-material (unlike lighting, which is pushed every frame by
  // GrassMaterial_UpdateLighting) — set them once here as shader uniforms.
  int baseTileLoc = GetShaderLocation(mat.shader, "u_baseTileSize");
  if (baseTileLoc >= 0)
    SetShaderValue(mat.shader, baseTileLoc, &config.baseTileSize,
                   SHADER_UNIFORM_FLOAT);

  int detailTileLoc = GetShaderLocation(mat.shader, "u_detailTileSize");
  if (detailTileLoc >= 0)
    SetShaderValue(mat.shader, detailTileLoc, &config.detailTileSize,
                   SHADER_UNIFORM_FLOAT);

  int maskScaleLoc = GetShaderLocation(mat.shader, "u_maskNoiseScale");
  if (maskScaleLoc >= 0)
    SetShaderValue(mat.shader, maskScaleLoc, &config.maskNoiseScale,
                   SHADER_UNIFORM_FLOAT);

  int colorVarLoc = GetShaderLocation(mat.shader, "u_colorVarScale");
  if (colorVarLoc >= 0)
    SetShaderValue(mat.shader, colorVarLoc, &config.colorVarScale,
                   SHADER_UNIFORM_FLOAT);

  return mat;
}

void GrassMaterial_UpdateLighting(void) {
  Shader shader = GrassMaterial_GetShader();
  if (shader.id == 0)
    return;


  int lightDirLoc = GetShaderLocation(shader, "u_lightDir");
  if (lightDirLoc >= 0) {
    // Same negation convention as PropLit_UpdateLighting/
    // SkillManager_BeginShader (CORE_ISSUES.md Item 10):
    // Environment_GetSunDirection() is the direction light TRAVELS
    // (sky->ground); shaders' dot(normal, lightDir) convention needs the
    // opposite, surface->light direction.
    Vector3 lightDir = Vector3Negate(Environment_GetSunDirection());
    SetShaderValue(shader, lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);
  }

  int lightColorLoc = GetShaderLocation(shader, "u_lightColor");
  if (lightColorLoc >= 0) {
    Vector4 c = ColorNormalize(Environment_GetSunColor());
    Vector3 rgb = {c.x, c.y, c.z};
    SetShaderValue(shader, lightColorLoc, &rgb, SHADER_UNIFORM_VEC3);
  }

  int ambientLoc = GetShaderLocation(shader, "u_ambientColor");
  if (ambientLoc >= 0) {
    Vector4 c = ColorNormalize(Environment_GetAmbientColor());
    Vector3 rgb = {c.x, c.y, c.z};
    SetShaderValue(shader, ambientLoc, &rgb, SHADER_UNIFORM_VEC3);
  }
}
