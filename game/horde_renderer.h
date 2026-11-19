// game/horde_renderer.h
// Zero-lag instanced renderer for Open-World Vampire-Survivors hordes,
// boss-wave minions, minion bolts, and batched ground shadow discs.
#ifndef HORDE_RENDERER_H
#define HORDE_RENDERER_H

#include "raylib.h"

void HordeRenderer_Init(void);
void HordeRenderer_Unload(void);

// Draws all active camp/horde monsters, boss-wave minions, minion bolts,
// and batched shadow discs using DrawMeshInstanced (< 10 draw calls total).
void HordeRenderer_Draw3D(Camera3D cam);

#endif // HORDE_RENDERER_H
