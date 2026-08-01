#ifndef CORE_FLUID_SURFACE_H
#define CORE_FLUID_SURFACE_H

#include "raylib.h"
#include "core/particles/particle_manager.h"

#define FLUID_SURFACE_MAX_PARTICLES 96

/* Screen-space liquid surface. Register from a 3D draw path (no GL work),
 * capture after ScreenDistort_End(), then composite inside PostFX_Begin/End(). */
void FluidSurface_Init(int width, int height);
void FluidSurface_Unload(void);
void FluidSurface_RegisterParticle(Vector3 position, float radius);
/* Accepts the same opaque stream from either particle backend. The GPU path
 * is rasterized by the owning renderer and is never read back to CPU. */
bool FluidSurface_SubmitParticleStream(const ParticleRenderStream *stream);
void FluidSurface_Capture(Camera3D camera);
void FluidSurface_Composite(void);

#endif
