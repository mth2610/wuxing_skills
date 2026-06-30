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

// Load sound (returns cached instance if already loaded)
Sound ResourceManager_LoadSound(const char *filePath);

#endif // RESOURCE_MANAGER_H
