#ifndef POST_PROCESS_GTAO_H
#define POST_PROCESS_GTAO_H

#include "raylib.h"
#include <stdbool.h>

void GTAO_Init(int width, int height);
void GTAO_Resize(int width, int height);
void GTAO_Shutdown(void);

void GTAO_SetEnabled(bool enabled);
bool GTAO_IsEnabled(void);

// Renders half-res GTAO + bilateral denoise from linear view-Z depth.
// Returns the denoised AO texture (or {0} if disabled/unavailable).
Texture2D GTAO_Render(Texture2D linearDepth, Camera3D camera);

#endif // POST_PROCESS_GTAO_H
