#ifndef VFX_TEST_H
#define VFX_TEST_H

#include "raylib.h"

bool VFXTest_UpdateAndHandleInput(Vector3 playerPos, Vector3 mouseTarget3D, Texture2D testAtlasTex,
                                  Texture2D globalParticleTex);
void VFXTest_Draw3D(void);
void VFXTest_DrawShadowPass(void);
/* Dedicated refraction post-pass (runs after SceneTargets_SnapshotScene) */
void VFXTest_DrawRefraction(Camera3D cam);
void VFXTest_SetPlayerYaw(float yaw);
/* Nhớ camera của khung hình này, để ảnh chụp biết chiếu vùng hiệu ứng ra đâu.
 * Gọi trong pass 3D, trước VFXTest_Draw3D. */
void VFXTest_SetCamera(Camera3D cam);
void VFXTest_DrawHUD(void);
/* Read-only hit test using the HUD's shared layout. Call before camera/input
 * updates to suppress scene gestures over visible UI or a captured UI drag.
 * Hidden UI returns false; open fixture/help panels own their backdrop. */
bool VFXTest_IsPointerOverUI(void);
/* Frame telemetry shown in the compact status strip, independent of F3. */
void VFXTest_SetPerformanceTelemetry(float fps, float frameTimeMs, float peakFrameMs);
/* Actual camera values; zoomRatio is baseline target distance / current distance. */
void VFXTest_SetCameraTelemetry(float tiltDegrees, float zoomRatio, float distanceMeters);
/* Returns a queued tilt-button step (-1/0/+1), then clears it. The caller owns
 * angular step size, pitch bounds, and the camera transform. */
int VFXTest_ConsumeCameraTiltStep(void);

/* Hidden by default: the mannequin (main.c's DrawCharacter3D) sits at the same
 * point the camera pivots on and fixtures spawn at, so it constantly occludes
 * or intersects whatever VFX is under test. Toggle with B. */
bool VFXTest_ShouldHideCharacterRef(void);

/* Hidden by default: GPU particle pool / skill-manager / core-test debug HUD
 * text, all pure clutter while judging how a VFX looks. Toggle with F3.
 * Tab/CapsLock is reserved for parameter selection. */
bool VFXTest_ShouldHideDebugOverlays(void);

// Headless render mode: jump to NEWFX tab at `newfxIndex`, set spawn position.
// Oneshot effects are fired immediately; continuous effects start drawing via
// s_isPlayingMesh. Call once before the main loop, then run warmup frames.
void VFXTest_SetRenderTarget(int newfxIndex, Vector3 spawnPos);

// Capture-only neutral smoke; independent of the generated NEWFX catalog.
// Neutral means no elemental tint, not a white unshaded calibration material.
void VFXTest_SetNeutralSmokeRenderTarget(Vector3 spawnPos);

#endif // VFX_TEST_H
