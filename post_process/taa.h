#ifndef POST_PROCESS_TAA_H
#define POST_PROCESS_TAA_H

#include "raylib.h"
#include <stdbool.h>

// Temporal Anti-Aliasing (HDR, Halton 2,3 jitter, YCoCg variance clamp).
void TAA_Init(int width, int height);
void TAA_Resize(int width, int height);
void TAA_Shutdown(void);

void TAA_SetEnabled(bool enabled);
bool TAA_IsEnabled(void);

// Call once per frame before the first MyBeginMode3D() to advance the Halton sequence
// and record the camera's unjittered ViewProj for next-frame reprojection.
void TAA_NextFrame(Camera3D camera, int width, int height);

// Returns current frame's subpixel jitter in pixels [-0.5, +0.5]. Zero when TAA is off.
void TAA_GetJitterPixels(float *outJitterX, float *outJitterY);

// Resolves currHdr against history using linearDepth (world meters) and returns the
// resolved HDR texture. Falls back to currHdr when TAA is disabled or unready.
Texture2D TAA_Resolve(Texture2D currHdr, Texture2D linearDepth, Camera3D camera);

#endif // POST_PROCESS_TAA_H
