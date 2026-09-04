#ifndef MAP_SHADOW_H
#define MAP_SHADOW_H

#include "raylib.h"

// Connects a map-owned material shader to Environment's directional shadow.
// Configure once after loading the shader, attach once per material, then push
// the shared matrix/enable state before drawing.
void MapShadow_ConfigureShader(Shader shader);
void MapShadow_AttachMaterial(Material *material);
void MapShadow_UpdateShader(Shader shader);

#endif // MAP_SHADOW_H
