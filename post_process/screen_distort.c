/* SCREEN DISTORTION — shockwave refraction, and nothing else. */
#include "post_process/screen_distort.h"
#include "post_process/scene_targets.h"
#include "core/resource_manager.h"
#include "rlgl.h"
#include <math.h>
#include <string.h>

static Shader distortShader;
static Shader meshDistortShader;
static bool s_meshDistortReady = false;
static DistortionSource sources[MAX_DISTORTION_SOURCES];
static int activeSourcesCount = 0;

// Uniform locations - shockwave
static int centersLoc;
static int radiiLoc;
static int strengthsLoc;
static int progressLoc;
static int countLoc;
static int aspectLoc;

// Uniform locations - mesh distortion
static int meshLocSceneTex;
static int meshLocHasScene;
static int meshLocStrength;
static int meshLocFlowSpeed;
static int meshLocTintColor;
static int meshLocEdgeFade;
static int meshLocTime;
static int meshLocResolution;
static int meshLocFlowMap;

void ScreenDistort_Init(void)
{
  distortShader = LoadShader(0, "core/shaders/distortion.fs");
  centersLoc = GetShaderLocation(distortShader, "u_centers");
  radiiLoc = GetShaderLocation(distortShader, "u_radii");
  strengthsLoc = GetShaderLocation(distortShader, "u_strengths");
  progressLoc = GetShaderLocation(distortShader, "u_progress");
  countLoc = GetShaderLocation(distortShader, "u_count");
  aspectLoc = GetShaderLocation(distortShader, "u_aspectRatio");

  meshDistortShader = ResourceManager_LoadShader("core/shaders/distortion_mesh.vs", "core/shaders/distortion_mesh.fs");
  if (meshDistortShader.id > 0)
  {
    meshDistortShader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(meshDistortShader, "mvp");
    meshDistortShader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(meshDistortShader, "matModel");
    meshLocSceneTex = GetShaderLocation(meshDistortShader, "u_sceneTex");
    meshLocHasScene = GetShaderLocation(meshDistortShader, "u_hasScene");
    meshLocStrength = GetShaderLocation(meshDistortShader, "u_distortionStrength");
    meshLocFlowSpeed = GetShaderLocation(meshDistortShader, "u_flowSpeed");
    meshLocTintColor = GetShaderLocation(meshDistortShader, "u_tintColor");
    meshLocEdgeFade = GetShaderLocation(meshDistortShader, "u_edgeFade");
    meshLocTime = GetShaderLocation(meshDistortShader, "u_time");
    meshLocResolution = GetShaderLocation(meshDistortShader, "u_resolution");
    meshLocFlowMap = GetShaderLocation(meshDistortShader, "texture0");
    s_meshDistortReady = true;
  }

  activeSourcesCount = 0;
  memset(sources, 0, sizeof(sources));
}

void ScreenDistort_Unload(void)
{
  UnloadShader(distortShader);
  if (s_meshDistortReady)
  {
    UnloadShader(meshDistortShader);
    s_meshDistortReady = false;
  }
}

void ScreenDistort_Add(Vector3 worldPos, float radius, float strength, float lifetime, float speed)
{
  if (activeSourcesCount >= MAX_DISTORTION_SOURCES)
  {
    int minIdx = 0;
    float minLife = sources[0].lifetime;

    const DistortionSource *srcPtr = sources;
    for (int i = 1; i < MAX_DISTORTION_SOURCES; i++)
    {
      if (srcPtr[i].lifetime < minLife)
      {
        minLife = srcPtr[i].lifetime;
        minIdx = i;
      }
    }
    sources[minIdx] = (DistortionSource){worldPos, radius, strength, lifetime, lifetime, speed};
    return;
  }

  sources[activeSourcesCount] = (DistortionSource){worldPos, radius, strength, lifetime, lifetime, speed};
  activeSourcesCount++;
}

bool ScreenDistort_HasLiveSources(void) { return activeSourcesCount > 0; }

void ScreenDistort_Update(float dt)
{
  for (int i = activeSourcesCount - 1; i >= 0; i--)
  {
    sources[i].lifetime -= dt;
    if (sources[i].lifetime <= 0.0f)
    {
      sources[i] = sources[activeSourcesCount - 1];
      activeSourcesCount--;
    }
  }
}

void ScreenDistort_Draw(Camera3D camera)
{
  float screenWidth = (float)GetScreenWidth();
  float screenHeight = (float)GetScreenHeight();
  if (screenWidth <= 0.0f || screenHeight <= 0.0f)
    return;

  float invScreenWidth = 1.0f / screenWidth;
  float invScreenHeight = 1.0f / screenHeight;
  float aspect = screenWidth * invScreenHeight;

  static Vector2 centers[MAX_DISTORTION_SOURCES];
  static float radii[MAX_DISTORTION_SOURCES];
  static float strengths[MAX_DISTORTION_SOURCES];
  static float progress[MAX_DISTORTION_SOURCES];

  int validCount = 0;
  for (int i = 0; i < activeSourcesCount; i++)
  {
    const DistortionSource *src = &sources[i];
    Vector2 screenPos = GetWorldToScreen(src->worldPos, camera);

    if (screenPos.x < -200.0f || screenPos.x > screenWidth + 200.0f ||
        screenPos.y < -200.0f || screenPos.y > screenHeight + 200.0f)
    {
      continue;
    }

    centers[validCount].x = screenPos.x * invScreenWidth;
    centers[validCount].y = 1.0f - (screenPos.y * invScreenHeight);

    radii[validCount] = src->radius * invScreenWidth;
    strengths[validCount] = src->strength;
    progress[validCount] = 1.0f - (src->lifetime / src->maxLifetime);

    validCount++;
  }

  Texture2D sceneTex = SceneTargets_GetSceneTexture();

  BeginShaderMode(distortShader);
  SetShaderValue(distortShader, aspectLoc, &aspect, SHADER_UNIFORM_FLOAT);
  SetShaderValue(distortShader, countLoc, &validCount, SHADER_UNIFORM_INT);

  if (validCount > 0)
  {
    SetShaderValueV(distortShader, centersLoc, centers, SHADER_UNIFORM_VEC2, validCount);
    SetShaderValueV(distortShader, radiiLoc, radii, SHADER_UNIFORM_FLOAT, validCount);
    SetShaderValueV(distortShader, strengthsLoc, strengths, SHADER_UNIFORM_FLOAT, validCount);
    SetShaderValueV(distortShader, progressLoc, progress, SHADER_UNIFORM_FLOAT, validCount);
  }

  // [TỐI ƯU HÓA & FIX ALPHA HDR]: Vô hiệu hóa blend khi copy sceneTex sang mainRenderTex
  rlDisableColorBlend();
  DrawTexturePro(sceneTex,
                 (Rectangle){0, 0, (float)sceneTex.width, -(float)sceneTex.height},
                 (Rectangle){0, 0, (float)sceneTex.width, (float)sceneTex.height},
                 (Vector2){0, 0}, 0.0f, WHITE);
  rlDrawRenderBatchActive();
  rlEnableColorBlend();
  EndShaderMode();
}

void ScreenDistort_RequestMeshPass(void)
{
  SceneTargets_RequestSceneSnapshot();
}

Shader ScreenDistort_GetMeshShader(void)
{
  return meshDistortShader;
}

void ScreenDistort_BeginMeshPass(Texture2D flowMap, float strength, Vector2 flowSpeed, Color tintColor)
{
  if (!s_meshDistortReady)
    return;

  Texture2D snapshot = SceneTargets_GetSceneSnapshotTexture();
  int hasScene = (snapshot.id > 0) ? 1 : 0;

  SceneTargets_BeginVFXBody();
  rlDisableDepthMask();
  rlDisableColorBlend();

  BeginShaderMode(meshDistortShader);

  SetShaderValue(meshDistortShader, meshLocHasScene, &hasScene, SHADER_UNIFORM_INT);
  if (hasScene)
  {
    SetShaderValueTexture(meshDistortShader, meshLocSceneTex, snapshot);
  }

  SetShaderValue(meshDistortShader, meshLocStrength, &strength, SHADER_UNIFORM_FLOAT);
  SetShaderValue(meshDistortShader, meshLocFlowSpeed, &flowSpeed, SHADER_UNIFORM_VEC2);

  Vector4 tint = {(float)tintColor.r * (1.0f / 255.0f), (float)tintColor.g * (1.0f / 255.0f),
                  (float)tintColor.b * (1.0f / 255.0f), (float)tintColor.a * (1.0f / 255.0f)};
  SetShaderValue(meshDistortShader, meshLocTintColor, &tint, SHADER_UNIFORM_VEC4);

  float edgeFade = 1.0f;
  SetShaderValue(meshDistortShader, meshLocEdgeFade, &edgeFade, SHADER_UNIFORM_FLOAT);

  float time = (float)GetTime();
  if (meshLocTime >= 0)
    SetShaderValue(meshDistortShader, meshLocTime, &time, SHADER_UNIFORM_FLOAT);

  Vector2 res = (snapshot.id > 0)
                    ? (Vector2){(float)snapshot.width, (float)snapshot.height}
                    : (Vector2){(float)SceneTargets_GetWidth(), (float)SceneTargets_GetHeight()};
  if (meshLocResolution >= 0)
    SetShaderValue(meshDistortShader, meshLocResolution, &res, SHADER_UNIFORM_VEC2);

  if (flowMap.id > 0 && meshLocFlowMap >= 0)
  {
    SetShaderValueTexture(meshDistortShader, meshLocFlowMap, flowMap);
  }
}

void ScreenDistort_EndMeshPass(void)
{
  if (!s_meshDistortReady)
    return;
  EndShaderMode();
  rlEnableColorBlend();
  rlEnableDepthMask();
  rlDrawRenderBatchActive();
  SceneTargets_EndVFXLayer();
}