#ifndef POST_PROCESS_TAA_H
#define POST_PROCESS_TAA_H

#include "raylib.h"
#include <stdbool.h>

// Temporal Anti-Aliasing (HDR, Halton 2,3 jitter, YCoCg variance clamp).
void TAA_Init(int width, int height);
void TAA_Resize(int width, int height);
void TAA_Shutdown(void);

void TAA_SetEnabled(bool enabled);
void TAA_InvalidateHistory(void);
bool TAA_IsEnabled(void); // True when the current frame selected temporal AA.

// Call after setting the quality tier and before rendering the scene.
// Jitter advances only when the camera has settled.
void TAA_NextFrame(Camera3D camera, int width, int height);

// Returns current frame's subpixel jitter in pixels [-0.5, +0.5]. Zero when TAA is off.
void TAA_GetJitterPixels(float *outJitterX, float *outJitterY);

// Accumulates HDR color at a stable camera pose. Falls back to currHdr when inactive.
Texture2D TAA_Resolve(Texture2D currHdr);

#endif // POST_PROCESS_TAA_H
