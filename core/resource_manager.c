#include "core/resource_manager.h"
#include "core/shader_preprocessor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CACHED_TEXTURES 32
#define MAX_CACHED_SHADERS 32
#define MAX_CACHED_SOUNDS 32

typedef struct {
  char path[128];
  Texture2D texture;
  bool active;
} CachedTexture;

typedef struct {
  char vsPath[128];
  char fsPath[128];
  Shader shader;
  bool active;
} CachedShader;

typedef struct {
  char path[128];
  Sound sound;
  bool active;
} CachedSound;

static CachedTexture s_textures[MAX_CACHED_TEXTURES];
static CachedShader s_shaders[MAX_CACHED_SHADERS];
static CachedSound s_sounds[MAX_CACHED_SOUNDS];

// Nạp shader có xử lý #include. Dùng thay cho LoadShader() ở mọi nơi trong file
// này.
static Shader LoadShaderProcessed(const char *vsFilePath,
                                  const char *fsFilePath) {
  char *vsCode = (vsFilePath && vsFilePath[0])
                     ? ShaderPreprocessor_Load(vsFilePath)
                     : NULL;
  char *fsCode = (fsFilePath && fsFilePath[0])
                     ? ShaderPreprocessor_Load(fsFilePath)
                     : NULL;
  Shader shader = LoadShaderFromMemory(vsCode, fsCode);
  if (vsCode)
    RL_FREE(vsCode);
  if (fsCode)
    RL_FREE(fsCode);
  return shader;
}

void ResourceManager_Init(void) {
  for (int i = 0; i < MAX_CACHED_TEXTURES; i++) {
    s_textures[i].active = false;
    s_textures[i].path[0] = '\0';
  }
  for (int i = 0; i < MAX_CACHED_SHADERS; i++) {
    s_shaders[i].active = false;
    s_shaders[i].vsPath[0] = '\0';
    s_shaders[i].fsPath[0] = '\0';
  }
  for (int i = 0; i < MAX_CACHED_SOUNDS; i++) {
    s_sounds[i].active = false;
    s_sounds[i].path[0] = '\0';
  }
}

void ResourceManager_Unload(void) {
  // Unload all textures
  for (int i = 0; i < MAX_CACHED_TEXTURES; i++) {
    if (s_textures[i].active) {
      UnloadTexture(s_textures[i].texture);
      s_textures[i].active = false;
      s_textures[i].path[0] = '\0';
    }
  }
  // Unload all shaders
  for (int i = 0; i < MAX_CACHED_SHADERS; i++) {
    if (s_shaders[i].active) {
      UnloadShader(s_shaders[i].shader);
      s_shaders[i].active = false;
      s_shaders[i].vsPath[0] = '\0';
      s_shaders[i].fsPath[0] = '\0';
    }
  }
  // Unload all sounds
  for (int i = 0; i < MAX_CACHED_SOUNDS; i++) {
    if (s_sounds[i].active) {
      UnloadSound(s_sounds[i].sound);
      s_sounds[i].active = false;
      s_sounds[i].path[0] = '\0';
    }
  }
}

Texture2D ResourceManager_LoadTexture(const char *filePath) {
  if (filePath == NULL || filePath[0] == '\0') {
    return (Texture2D){0};
  }

  // 1. Search in cache
  for (int i = 0; i < MAX_CACHED_TEXTURES; i++) {
    if (s_textures[i].active && strcmp(s_textures[i].path, filePath) == 0) {
      return s_textures[i].texture;
    }
  }

  // 2. Load and add to cache
  for (int i = 0; i < MAX_CACHED_TEXTURES; i++) {
    if (!s_textures[i].active) {
      s_textures[i].texture = LoadTexture(filePath);
      snprintf(s_textures[i].path, sizeof(s_textures[i].path), "%s", filePath);
      s_textures[i].active = true;
      return s_textures[i].texture;
    }
  }

  // Fallback if cache is full: load and return un-cached (VRAM warning)
  printf("WARNING: Resource Manager texture cache is full! Loading un-cached: "
         "%s\n",
         filePath);
  return LoadTexture(filePath);
}

Shader ResourceManager_LoadShader(const char *vsFilePath,
                                  const char *fsFilePath) {
  const char *vs = (vsFilePath == NULL) ? "" : vsFilePath;
  const char *fs = (fsFilePath == NULL) ? "" : fsFilePath;

  // 1. Search in cache
  for (int i = 0; i < MAX_CACHED_SHADERS; i++) {
    if (s_shaders[i].active && strcmp(s_shaders[i].vsPath, vs) == 0 &&
        strcmp(s_shaders[i].fsPath, fs) == 0) {
      return s_shaders[i].shader;
    }
  }

  // 2. Load and add to cache
  for (int i = 0; i < MAX_CACHED_SHADERS; i++) {
    if (!s_shaders[i].active) {
      s_shaders[i].shader = LoadShaderProcessed(vsFilePath, fsFilePath);
      // raylib 5.5 returns {id=0, locs=NULL} on compile failure — do NOT cache
      // an invalid shader; callers guard via SkillManager_BeginShader.
      if (s_shaders[i].shader.id == 0 || s_shaders[i].shader.locs == NULL) {
        TraceLog(LOG_WARNING, "SHADER: compile failed, not caching (vs=%s fs=%s)", vs, fs);
        return s_shaders[i].shader;
      }
      snprintf(s_shaders[i].vsPath, sizeof(s_shaders[i].vsPath), "%s", vs);
      snprintf(s_shaders[i].fsPath, sizeof(s_shaders[i].fsPath), "%s", fs);
      s_shaders[i].active = true;
      return s_shaders[i].shader;
    }
  }

  // Fallback if cache is full
  TraceLog(LOG_WARNING, "SHADER: cache full, loading un-cached: vs=%s fs=%s", vs, fs);
  return LoadShaderProcessed(vsFilePath, fsFilePath);
}

Sound ResourceManager_LoadSound(const char *filePath) {
  if (filePath == NULL || filePath[0] == '\0') {
    return (Sound){0};
  }

  // 1. Search in cache
  for (int i = 0; i < MAX_CACHED_SOUNDS; i++) {
    if (s_sounds[i].active && strcmp(s_sounds[i].path, filePath) == 0) {
      return s_sounds[i].sound;
    }
  }

  // 2. Load and add to cache
  for (int i = 0; i < MAX_CACHED_SOUNDS; i++) {
    if (!s_sounds[i].active) {
      s_sounds[i].sound = LoadSound(filePath);
      snprintf(s_sounds[i].path, sizeof(s_sounds[i].path), "%s", filePath);
      s_sounds[i].active = true;
      return s_sounds[i].sound;
    }
  }

  // Fallback if cache is full: load and return un-cached
  TraceLog(LOG_WARNING, "SOUND: cache full, loading un-cached: %s", filePath);
  return LoadSound(filePath);
}