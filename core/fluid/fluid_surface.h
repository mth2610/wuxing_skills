#ifndef CORE_FLUID_SURFACE_H
#define CORE_FLUID_SURFACE_H

#include "raylib.h"
#include "core/particles/particle_manager.h"

#define FLUID_SURFACE_MAX_PARTICLES 384

/* Screen-space liquid surface. Register from a 3D draw path (no GL work),
 * capture after ScreenDistort_End(), then composite into ScreenDistort's VFX
 * body layer before its HDR scene composite. */
void FluidSurface_Init(int width, int height);
void FluidSurface_Unload(void);
/* Sets the optical identity used by absorption, scattering and highlights.
 * Callers normally forward body/glow/soft from VFX_Material(...). */
void FluidSurface_SetMaterialColors(Color body, Color glow, Color soft);
/* Approximate world-space radius of one optical kernel. It controls the
 * depth range used by screen-space surface reconstruction. */
void FluidSurface_SetReconstructionRadius(float radius);
void FluidSurface_RegisterParticle(Vector3 position, float radius);
void FluidSurface_RegisterEllipsoid(Vector3 position, Vector3 radii);
/* Accepts the same opaque stream from either particle backend. The GPU path
 * is rasterized by the owning renderer and is never read back to CPU. */
bool FluidSurface_SubmitParticleStream(const ParticleRenderStream *stream);
/* Whether the current frame has any surface input to capture/composite. */
bool FluidSurface_HasPending(void);
void FluidSurface_Capture(Camera3D camera);
void FluidSurface_Composite(void);

#endif
