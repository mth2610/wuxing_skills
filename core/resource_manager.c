#include "core/resource_manager.h"
#include "core/shader_preprocessor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CACHED_TEXTURES 32
#define MAX_CACHED_SHADERS 32
#define MAX_CACHED_SOUNDS 32
#define MAX_CACHED_FONTS 8
#define MAX_CACHED_MODELS 8

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

typedef struct {
  char path[128];
  int baseSize;
  Font font;
  bool active;
} CachedFont;

typedef struct {
  char path[128];
  Model model;
  ModelAnimation *animations;
  int animCount;
  bool active;
} CachedModel;

static CachedTexture s_textures[MAX_CACHED_TEXTURES];
static CachedShader s_shaders[MAX_CACHED_SHADERS];
static CachedSound s_sounds[MAX_CACHED_SOUNDS];
static CachedFont s_fonts[MAX_CACHED_FONTS];
static CachedModel s_models[MAX_CACHED_MODELS];

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

#ifdef __ANDROID__
  // If fragment shader is loaded and has been rewritten to #version 300 es,
  // but vertex shader is NULL, we must supply a default #version 300 es vertex shader
  // to avoid "Link error: L0001 Shader languages do not match".
  if (fsCode && strstr(fsCode, "#version 300 es") && vsCode == NULL) {
    const char *defaultVs = 
        "#version 300 es\n"
        "precision highp float;\n"
        "in vec3 vertexPosition;\n"
        "in vec2 vertexTexCoord;\n"
        "in vec4 vertexColor;\n"
        "out vec2 fragTexCoord;\n"
        "out vec4 fragColor;\n"
        "uniform mat4 mvp;\n"
        "void main() {\n"
        "    fragTexCoord = vertexTexCoord;\n"
        "    fragColor = vertexColor;\n"
        "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
        "}\n";
    vsCode = RL_MALLOC(strlen(defaultVs) + 1);
    strcpy(vsCode, defaultVs);
  }
#endif

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
  for (int i = 0; i < MAX_CACHED_FONTS; i++) {
    s_fonts[i].active = false;
    s_fonts[i].path[0] = '\0';
  }
  for (int i = 0; i < MAX_CACHED_MODELS; i++) {
    s_models[i].active = false;
    s_models[i].path[0] = '\0';
    s_models[i].animations = NULL;
    s_models[i].animCount = 0;
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
  // Unload all fonts (default font is never cached here, so this never
  // touches GetFontDefault()'s own texture)
  for (int i = 0; i < MAX_CACHED_FONTS; i++) {
    if (s_fonts[i].active) {
      UnloadFont(s_fonts[i].font);
      s_fonts[i].active = false;
      s_fonts[i].path[0] = '\0';
    }
  }
  // Unload all models + their animations
  for (int i = 0; i < MAX_CACHED_MODELS; i++) {
    if (s_models[i].active) {
      if (s_models[i].animations != NULL) {
        UnloadModelAnimations(s_models[i].animations, s_models[i].animCount);
      }
      UnloadModel(s_models[i].model);
      s_models[i].active = false;
      s_models[i].path[0] = '\0';
      s_models[i].animations = NULL;
      s_models[i].animCount = 0;
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

Font ResourceManager_LoadFont(const char *filePath, int baseSize) {
  if (filePath == NULL || filePath[0] == '\0')
    return GetFontDefault();

  // 1. Search in cache (path + baseSize both must match — a different
  // baseSize needs its own atlas, same as raw LoadFontEx)
  for (int i = 0; i < MAX_CACHED_FONTS; i++) {
    if (s_fonts[i].active && s_fonts[i].baseSize == baseSize &&
        strcmp(s_fonts[i].path, filePath) == 0) {
      return s_fonts[i].font;
    }
  }

  // NOTE: On Android, FileExists() returns false for assets inside the APK because it uses access().
  // We rely on LoadFontEx returning a default font if it fails to load.
  // if (!FileExists(filePath)) {
  //   TraceLog(LOG_WARNING, "FONT: %s not found, falling back to default font", filePath);
  //   return GetFontDefault();
  // }

  Font font = LoadFontEx(filePath, baseSize, NULL, 0);
  SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);

  // 2. Add to cache
  for (int i = 0; i < MAX_CACHED_FONTS; i++) {
    if (!s_fonts[i].active) {
      s_fonts[i].font = font;
      s_fonts[i].baseSize = baseSize;
      snprintf(s_fonts[i].path, sizeof(s_fonts[i].path), "%s", filePath);
      s_fonts[i].active = true;
      return font;
    }
  }

  // Fallback if cache is full: return un-cached (caller must not Unload it)
  TraceLog(LOG_WARNING, "FONT: cache full, returning un-cached: %s", filePath);
  return font;
}

Model ResourceManager_LoadModel(const char *filePath) {
  if (filePath == NULL || filePath[0] == '\0')
    return (Model){0};

  for (int i = 0; i < MAX_CACHED_MODELS; i++) {
    if (s_models[i].active && strcmp(s_models[i].path, filePath) == 0) {
      return s_models[i].model;
    }
  }

  // NOTE: On Android, FileExists() returns false for APK assets.
  // if (!FileExists(filePath)) {
  //   TraceLog(LOG_WARNING, "MODEL: %s not found, returning empty model", filePath);
  //   return (Model){0};
  // }

  Model model = LoadModel(filePath);

  for (int i = 0; i < MAX_CACHED_MODELS; i++) {
    if (!s_models[i].active) {
      s_models[i].model = model;
      s_models[i].animations = NULL;
      s_models[i].animCount = 0;
      snprintf(s_models[i].path, sizeof(s_models[i].path), "%s", filePath);
      s_models[i].active = true;
      return model;
    }
  }

  TraceLog(LOG_WARNING, "MODEL: cache full, returning un-cached: %s", filePath);
  return model;
}

ModelAnimation *ResourceManager_LoadModelAnimations(const char *filePath, int *outCount) {
  if (outCount) *outCount = 0;
  if (filePath == NULL || filePath[0] == '\0')
    return NULL;

  for (int i = 0; i < MAX_CACHED_MODELS; i++) {
    if (s_models[i].active && strcmp(s_models[i].path, filePath) == 0 && s_models[i].animations != NULL) {
      if (outCount) *outCount = s_models[i].animCount;
      return s_models[i].animations;
    }
  }

  // NOTE: On Android, FileExists() returns false for APK assets.
  // if (!FileExists(filePath)) {
  //   TraceLog(LOG_WARNING, "MODEL ANIM: %s not found, no animations loaded", filePath);
  //   return NULL;
  // }

  int animCount = 0;
  ModelAnimation *anims = LoadModelAnimations(filePath, &animCount);
  if (anims == NULL || animCount == 0) {
    TraceLog(LOG_WARNING, "MODEL ANIM: %s has no animations", filePath);
    return NULL;
  }

  // Attach to the cache slot for this path if ResourceManager_LoadModel
  // already created one; otherwise take a fresh slot (model-less, animation
  // data only — unusual but harmless).
  for (int i = 0; i < MAX_CACHED_MODELS; i++) {
    if (s_models[i].active && strcmp(s_models[i].path, filePath) == 0) {
      s_models[i].animations = anims;
      s_models[i].animCount = animCount;
      if (outCount) *outCount = animCount;
      return anims;
    }
  }
  for (int i = 0; i < MAX_CACHED_MODELS; i++) {
    if (!s_models[i].active) {
      s_models[i].model = (Model){0};
      s_models[i].animations = anims;
      s_models[i].animCount = animCount;
      snprintf(s_models[i].path, sizeof(s_models[i].path), "%s", filePath);
      s_models[i].active = true;
      if (outCount) *outCount = animCount;
      return anims;
    }
  }

  TraceLog(LOG_WARNING, "MODEL ANIM: cache full, returning un-cached: %s", filePath);
  if (outCount) *outCount = animCount;
  return anims;
}