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

// Load a TTF/OTF font at baseSize (returns cached instance if already loaded
// at that exact path+size; a different baseSize for the same path loads a
// separate atlas, same as raylib's own LoadFontEx). Falls back to
// GetFontDefault() if filePath doesn't exist — never fails outright, safe to
// call even before an asset has been provided. Uses bilinear filtering so it
// scales smoothly (raylib's built-in font only looks correct unscaled).
Font ResourceManager_LoadFont(const char *filePath, int baseSize);

#endif // RESOURCE_MANAGER_H
