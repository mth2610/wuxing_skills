#ifndef VFX_TEST_H
#define VFX_TEST_H

#include "raylib.h"

bool VFXTest_UpdateAndHandleInput(Vector3 playerPos, Vector3 mouseTarget3D, Texture2D testAtlasTex,
                                  Texture2D globalParticleTex);
void VFXTest_Draw3D(void);
void VFXTest_DrawHUD(void);

#endif // VFX_TEST_H