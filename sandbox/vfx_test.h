#ifndef VFX_TEST_H
#define VFX_TEST_H

#include "raylib.h"

bool VFXTest_UpdateAndHandleInput(Vector3 playerPos, Vector3 mouseTarget3D, Texture2D testAtlasTex,
                                  Texture2D globalParticleTex);
void VFXTest_Draw3D(void);
void VFXTest_DrawHUD(void);

// Headless render mode: jump to NEWFX tab at `newfxIndex`, set spawn position.
// Oneshot effects are fired immediately; continuous effects start drawing via
// s_isPlayingMesh. Call once before the main loop, then run warmup frames.
void VFXTest_SetRenderTarget(int newfxIndex, Vector3 spawnPos);

#endif // VFX_TEST_H