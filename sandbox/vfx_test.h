#ifndef VFX_TEST_H
#define VFX_TEST_H

#include "raylib.h"

void VFXTest_UpdateAndHandleInput(Vector3 playerPos, Texture2D testAtlasTex,
                                  Texture2D globalParticleTex);
void VFXTest_DrawDebugLights3D(void);
void VFXTest_DrawHUD(void);

#endif // VFX_TEST_H