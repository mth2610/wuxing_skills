#ifndef CORE_FLUID_SURFACE_H
#define CORE_FLUID_SURFACE_H

#include "raylib.h"
#include "core/particles/particle_manager.h"

#define FLUID_SURFACE_MAX_PARTICLES 384
/* Distinct liquids that may share one screen. The capture rasterizes a slot
 * index per pixel, so this is the width of the composite's material table, not
 * a count of bodies: any number of bodies may share a slot. */
#define FLUID_SURFACE_MATERIAL_SLOTS 4

/* Which BRANCH of the optics a liquid takes. Not a style flag — the three
 * differ in what physically happens at and under the surface, and the composite
 * assembles a different set of terms for each. */
typedef enum {
    /* Water, poison, oil: refracts the background, absorbs along the measured
     * path (Beer-Lambert), reflects a few percent at normal incidence. */
    FLUID_LIQUID_DIELECTRIC = 0,
    /* Lava, molten glass: opaque within a reference depth, radiates a
     * thickness-driven blackbody colour, and its skin reads as cooled crust. */
    FLUID_LIQUID_EMISSIVE = 1,
    /* Liquid metal, mercury: a conductor. High COLOURED F0 and NO transmission
     * at all, so the body is read almost entirely through reflection. */
    FLUID_LIQUID_CONDUCTOR = 2
} FluidLiquidClass;

typedef struct {
    Color body;             /* identity/albedo; for a conductor this IS its F0 */
    Color glow;             /* hot core (emissive) / rim tint (dielectric) */
    Color soft;             /* pastel: foam, horizon reflection fill */
    FluidLiquidClass liquidClass;
    float emission;         /* radiance of a fully-thick body; 0 for cold liquids */
    float ior;              /* refractive index; <= 1 falls back to water's 1.333 */
    float roughnessScale;   /* scales the authored perceptual roughness; <= 0 -> 1 */
    float opacityPerMetre;  /* grey extinction ON TOP of the body colour's own */
    float foam;             /* 0..1 gain on foam (dielectric) / crust (emissive) */
} FluidLiquidDesc;

/* Water, unchanged from what FluidSurface_SetMaterialColors has always built. */
FluidLiquidDesc FluidSurface_DielectricDesc(Color body, Color glow, Color soft);

/* Binds `desc` to a slot and makes it the material for every particle and every
 * stream registered AFTER this call, until the next bind. Returns the slot.
 *
 * Slots are content-addressed: binding the same liquid twice reuses one slot, so
 * a caller that re-binds every frame does not consume the table. When all slots
 * hold liquids that were used more recently than this one, the least recently
 * used is evicted — with four slots and a handful of liquids on screen this
 * cannot bite, and the failure mode if it ever does is a body changing colour,
 * never a crash. */
int FluidSurface_BindMaterial(const FluidLiquidDesc *desc);

/* The slot the next registration would land in. GPU-PBD records it at spawn so
 * a body that outlives the frame it was spawned in keeps its own liquid. */
int FluidSurface_CurrentMaterial(void);

/* --- Cost gates ----------------------------------------------------------
 *
 * SSF's cost is almost entirely PER FRAME, not per body: the capture, the depth
 * filter, the thickness chain and the composite all run once no matter how many
 * liquids are in them. Measured on the VFX tester, the water ring has 6.3x the
 * splat area and 3.2x the screen coverage of the PBD crown and costs 1.5x. So
 * the expensive decision is not "how many bodies" — it is "does the surface run
 * at all this frame", and that is what these gates arbitrate.
 *
 * Nothing enforced this before: any number of skills could submit streams, and
 * nothing skipped SSF when the frame was already over budget. */
typedef enum {
    /* Never gets SSF. There can be a dozen of these on screen and none of them
     * is what the player is looking at. */
    FLUID_PRIORITY_MINION = 0,
    /* A basic attack. Only ever JOINS a surface that is already running — its
     * marginal cost is then splat area alone. It may not switch SSF on. */
    FLUID_PRIORITY_BASIC = 1,
    /* A hero/player cast. May switch the surface on. */
    FLUID_PRIORITY_CAST = 2,
    /* A boss ultimate. Outranks a cast for the resources that are still
     * single-owner (the reconstruction radius), and is the last thing dropped
     * when the frame is over budget. */
    FLUID_PRIORITY_ULTIMATE = 3
} FluidSurfacePriority;

/* Radius in PIXELS below which a body is not worth a screen-space surface. The
 * per-frame cost is absurd for a small splash, and at this size a reconstructed
 * surface is indistinguishable from the particles it was built from. */
#define FLUID_SURFACE_MIN_PROJECTED_RADIUS_PX 16.0f

/* Frame time (ms) above which the surface admits ULTIMATE only. Deliberately
 * well past 16.6: dropping a hero's water the instant a frame runs long would
 * make the effect flicker in and out during exactly the busy moments it exists
 * for. */
#define FLUID_SURFACE_BUDGET_MS 26.0f

/* Ask BEFORE building a fluid body, every frame the body wants to exist.
 * `worldRadius` is the body's approximate bounding radius in metres.
 *
 * Returns false when the caller must render with ordinary particles instead —
 * the caller owns that fallback; this function only decides. It uses the
 * PREVIOUS frame's camera and frame time, so it is order-independent within a
 * frame: a basic attack does not have to be submitted after the hero's cast to
 * see that the surface is running. */
bool FluidSurface_RequestBody(FluidSurfacePriority priority, Vector3 center,
                              float worldRadius);

/* The reconstruction radius is still ONE value for the whole capture. A gated
 * caller sets it through this, so a boss ultimate's kernel is not resized by a
 * player cast that happened to submit after it. Within a frame the highest
 * priority wins; equal priorities are last-writer-wins.
 * FluidSurface_SetReconstructionRadius stays unconditional for ungated callers. */
void FluidSurface_SetReconstructionRadiusFor(FluidSurfacePriority priority, float radius);

/* Screen-space liquid surface. Register from a 3D draw path (no GL work),
 * capture after ScreenDistort_End(), then composite into ScreenDistort's VFX
 * body layer before its HDR scene composite. */
void FluidSurface_Init(int width, int height);
void FluidSurface_Unload(void);
/* Sets the optical identity used by absorption, scattering and highlights.
 * Callers normally forward body/glow/soft from VFX_Material(...).
 * Shorthand for binding FluidSurface_DielectricDesc(body, glow, soft). */
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
