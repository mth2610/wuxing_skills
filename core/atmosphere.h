#ifndef ATMOSPHERE_H
#define ATMOSPHERE_H

#include "raylib.h"

// Đợt G3 — ambient atmosphere: slow-drifting dust/ash motes that catch the
// moonlight and fill the dead night air with depth and life. Self-contained
// (own soft-dot texture, fixed static pool, no malloc), reusable by any scene.
// Motes live in a box volume and wrap inside it, so density stays constant and
// the field never empties. Drawn as additive billboards → in the HDR pipeline
// overlapping motes bloom softly on their own.
//
// Usage:
//   Atmosphere_Init();                                  // once, after GL is up
//   Atmosphere_Configure(center, extent, count, tint);  // optional; sane default
//   ... each frame:
//   Atmosphere_Update(dt, camera);
//   Atmosphere_Draw(camera);   // inside the 3D pass, manages its own blend
//   Atmosphere_Unload();                                // at shutdown

void Atmosphere_Init(void);

// center/extent define the box the motes fill (extent = half-size per axis).
// count is clamped to the internal pool. tint is the mote color (moonlight).
void Atmosphere_Configure(Vector3 center, Vector3 extent, int count, Color tint);

void Atmosphere_Update(float dt, Camera3D camera);
void Atmosphere_Draw(Camera3D camera);
void Atmosphere_Unload(void);

#endif // ATMOSPHERE_H
