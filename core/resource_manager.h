#ifndef RESOURCE_MANAGER_H
#define RESOURCE_MANAGER_H

#include "raylib.h"

// Initialize resource manager cache
void ResourceManager_Init(void);

// Unload all cached textures and shaders to free VRAM
void ResourceManager_Unload(void);

// Load texture (returns cached instance if already loaded)
Texture2D ResourceManager_LoadTexture(const char *filePath);

// Load shader (returns cached instance if already loaded)
Shader ResourceManager_LoadShader(const char *vsFilePath, const char *fsFilePath);

// Load one PERMUTATION of a shader: `defines` (e.g. "#define INSTANCED 1\n")
// is injected after #version, and is part of the cache key — the same .vs/.fs
// pair with different defines are different programs, and therefore have
// different uniform locations. NULL/"" is identical to ResourceManager_LoadShader.
//
// Use this instead of copying a .vs to make a variant: a copy drifts from its
// original silently (the two instanced copies deleted on 20/08/2026 had both
// already diverged in comments and in one wobble term).
Shader ResourceManager_LoadShaderVariant(const char *vsFilePath, const char *fsFilePath,
                                         const char *defines);

// Load sound (returns cached instance if already loaded)
Sound ResourceManager_LoadSound(const char *filePath);

// Load a TTF/OTF font at baseSize (returns cached instance if already loaded
// at that exact path+size; a different baseSize for the same path loads a
// separate atlas, same as raylib's own LoadFontEx). Falls back to
// GetFontDefault() if filePath doesn't exist — never fails outright, safe to
// call even before an asset has been provided. Uses bilinear filtering so it
// scales smoothly (raylib's built-in font only looks correct unscaled).
Font ResourceManager_LoadFont(const char *filePath, int baseSize);

// Load a 3D model (returns cached instance if already loaded at that exact
// path). Falls back to an empty Model{} (meshCount == 0 — check before
// drawing) if filePath doesn't exist — never fails outright, safe to call
// before an asset has been provided.
Model ResourceManager_LoadModel(const char *filePath);

// Load the animation clips embedded in a model file (returns cached
// array+count if already loaded for that path). *outCount is set to 0 and
// NULL is returned if filePath doesn't exist or has no animations.
ModelAnimation *ResourceManager_LoadModelAnimations(const char *filePath, int *outCount);

#endif // RESOURCE_MANAGER_H
