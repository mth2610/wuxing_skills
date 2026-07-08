#include "core/prop_lit.h"
#include "core/resource_manager.h"
#include "core/camera_context.h"
#include "environment/environment_system.h"
#include "raymath.h"
#include <stddef.h>

Shader PropLit_GetShader(void) {
  Shader shader = ResourceManager_LoadShader("core/shaders/prop_lit.vs",
                                              "core/shaders/prop_lit.fs");

  // CORE_ISSUES.md Item 36 gotcha: LoadShaderFromMemory (what
  // ResourceManager_LoadShader wraps) only auto-binds a small fixed set of
  // default uniform/attribute names, and "matModel" is confirmed NOT among
  // them (see the comment in SkillManager_BeginShader, core/skill_manager.c
  // around line 1068). Every other custom shader in this codebase is bound
  // via SkillManager_BeginShader + immediate-mode/DrawMesh-adjacent draws
  // that either set matModel manually by name or never trigger raylib's
  // internal DrawMesh()-side auto-upload path at all. prop_lit is the first
  // shader here designed to be assigned straight into a Model's Material
  // slot and drawn with plain DrawModel()/DrawModelEx() — so raylib's own
  // DrawMesh() DOES read shader.locs[SHADER_LOC_MATRIX_MODEL] (and the
  // vertex-attribute / material-map-texture locs below) directly to decide
  // where to upload the model matrix / bind each texture unit. Leaving any
  // of these at their zero-initialized RL_CALLOC default (a value that
  // still passes a ">= 0" validity check without being the real uniform
  // location) risks silently overwriting whatever uniform actually lives at
  // that location. Fix all of them explicitly by name, once, rather than
  // trust the incomplete default auto-bind list.
  static bool s_locsFixed = false;
  if (!s_locsFixed && shader.id != 0 && shader.locs != NULL) {
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

    s_locsFixed = true;
  }
  return shader;
}

Material PropLit_MakeMaterial(Texture2D diffuse, Texture2D normal,
                               Texture2D roughness) {
  // LoadMaterialDefault() gives a Material with a properly allocated
  // maps[MAX_MATERIAL_MAPS] array (raylib-internal allocation, not ours —
  // core's "no malloc" rule is about our own code, not raylib's own Load*
  // APIs, same as ResourceManager_LoadTexture calling raylib's LoadTexture).
  // A hand-built Material{0} would leave maps == NULL and crash the first
  // time DrawMesh() indexes into it.
  Material mat = LoadMaterialDefault();
  mat.shader = PropLit_GetShader();

  mat.maps[MATERIAL_MAP_DIFFUSE].texture = diffuse;
  mat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE; // colDiffuse base; DrawModelEx's tint multiplies into this per-draw
  mat.maps[MATERIAL_MAP_NORMAL].texture = normal;
  mat.maps[MATERIAL_MAP_ROUGHNESS].texture = roughness;

  return mat;
}

void PropLit_UpdateLighting(void) {
  Shader shader = PropLit_GetShader();
  if (shader.id == 0)
    return;

  int lightDirLoc = GetShaderLocation(shader, "u_lightDir");
  if (lightDirLoc >= 0) {
    // Same negation convention as SkillManager_BeginShader (CORE_ISSUES.md
    // Item 10): Environment_GetSunDirection() is the direction light
    // TRAVELS (sky->ground, used for shadow-skew math); shaders' convention
    // for dot(normal, lightDir) needs the opposite, surface->light
    // direction.
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

  int viewPosLoc = GetShaderLocation(shader, "u_viewPos");
  if (viewPosLoc >= 0) {
    Vector3 viewPos = camera.position;
    SetShaderValue(shader, viewPosLoc, &viewPos, SHADER_UNIFORM_VEC3);
  }
}
