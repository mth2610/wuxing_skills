#ifndef CORE_LIQUID_SURFACE_H
#define CORE_LIQUID_SURFACE_H

#include "raylib.h"
#include "core/liquid/liquid_motion.h"
#include "core/particles/particle_manager.h"

#define LIQUID_SURFACE_MAX_PARTICLES 384
/* Distinct liquids that may share one screen. The capture rasterizes a slot
 * index per pixel, so this is the width of the composite's material table, not
 * a count of bodies: any number of bodies may share a slot. */
#define LIQUID_SURFACE_MATERIAL_SLOTS 6

/* Which BRANCH of the optics a liquid takes. Not a style flag — the three
 * differ in what physically happens at and under the surface, and the composite
 * assembles a different set of terms for each. */
typedef enum {
    /* Water, poison, oil: refracts the background, absorbs along the measured
     * path (Beer-Lambert), reflects a few percent at normal incidence. */
    LIQUID_CLASS_DIELECTRIC = 0,
    /* Lava, molten glass: opaque within a reference depth, radiates a
     * thickness-driven blackbody colour, and its skin reads as cooled crust. */
    LIQUID_CLASS_EMISSIVE = 1,
    /* Liquid metal, mercury: a conductor. High COLOURED F0 and NO transmission
     * at all, so the body is read almost entirely through reflection. */
    LIQUID_CLASS_CONDUCTOR = 2
} LiquidClass;

typedef struct {
    Color body;             /* identity/albedo; for a conductor this IS its F0 */
    Color glow;             /* hot core (emissive) / rim tint (dielectric) */
    Color soft;             /* pastel: foam, horizon reflection fill */
    LiquidClass liquidClass;
    float emission;         /* radiance of a fully-thick body; 0 for cold liquids */
    float ior;              /* refractive index; <= 1 falls back to water's 1.333 */
    float roughnessScale;   /* scales the authored perceptual roughness; <= 0 -> 1 */
    float opacityPerMetre;  /* grey extinction ON TOP of the body colour's own */
    float foam;             /* 0..1 gain on foam (dielectric) / crust (emissive) */
} LiquidDesc;

/* Water, unchanged from what LiquidSurface_SetMaterialColors has always built. */
LiquidDesc LiquidSurface_DielectricDesc(Color body, Color glow, Color soft);

/* Canonical optical identity paired with LiquidMotion_Get(profile). This is
 * shared by impacts, force-field orbs, and deterministic material benches. */
LiquidDesc LiquidSurface_ProfileDesc(LiquidMotionProfile profile);

/* Binds `desc` to a slot and makes it the material for every particle and every
 * stream registered AFTER this call, until the next bind. Returns the slot.
 *
 * Slots are content-addressed: binding the same liquid twice reuses one slot, so
 * a caller that re-binds every frame does not consume the table. When all slots
 * hold liquids that were used more recently than this one, the least recently
 * used is evicted — with six slots and a handful of liquids on screen this
 * cannot bite (LIQUID BENCH puts five on screen), and the failure mode if it ever
 * does is a body changing colour,
 * never a crash. */
int LiquidSurface_BindMaterial(const LiquidDesc *desc);

/* The slot the next registration would land in. GPU-PBD records it at spawn so
 * a body that outlives the frame it was spawned in keeps its own liquid. */
int LiquidSurface_CurrentMaterial(void);

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
    LIQUID_PRIORITY_MINION = 0,
    /* A basic attack. Only ever JOINS a surface that is already running — its
     * marginal cost is then splat area alone. It may not switch SSF on. */
    LIQUID_PRIORITY_BASIC = 1,
    /* A hero/player cast. May switch the surface on. */
    LIQUID_PRIORITY_CAST = 2,
    /* A boss ultimate. Outranks a cast for the resources that are still
     * single-owner (the reconstruction radius), and is the last thing dropped
     * when the frame is over budget. */
    LIQUID_PRIORITY_ULTIMATE = 3
} LiquidSurfacePriority;

/* Radius in PIXELS below which a body is not worth a screen-space surface. The
 * per-frame cost is absurd for a small splash, and at this size a reconstructed
 * surface is indistinguishable from the particles it was built from. */
#define LIQUID_SURFACE_MIN_PROJECTED_RADIUS_PX 16.0f

/* Frame time (ms) above which the surface admits ULTIMATE only. Deliberately
 * well past 16.6: dropping a hero's water the instant a frame runs long would
 * make the effect flicker in and out during exactly the busy moments it exists
 * for. */
#define LIQUID_SURFACE_BUDGET_MS 26.0f

/* At HIGH, compact dense bodies need one 2D reconstruction round; larger or
 * close-up bodies keep two. This is a BODY footprint threshold, not a quality
 * downgrade: one round already spans a complete optical kernel, while the
 * second exists to remove residual large-scale lumpiness on broad surfaces. */
#define LIQUID_SURFACE_COMPACT_BODY_PX 180.0f
#define LIQUID_SURFACE_COMPACT_KERNEL_PX 8.0f

/* Ask BEFORE building a fluid body, every frame the body wants to exist.
 * `worldRadius` is the body's approximate bounding radius in metres.
 *
 * `alreadyRunning` — pass true when THIS body was admitted on a previous frame
 * and is still going; false when it is starting. It is not an optimisation, it
 * is what stops the gate strobing: the frame-budget test measures a number the
 * gate's own decision controls (admitting the water ring costs 23-27 ms a
 * frame, rejecting it 16-17 ms), so re-testing a running body flips it every
 * frame at any threshold between those. A body's affordability is decided once,
 * when it starts. The size cull is not self-referential and keeps running every
 * frame, with hysteresis.
 *
 * Returns false when the caller must render with ordinary particles instead —
 * the caller owns that fallback; this function only decides. It uses the
 * PREVIOUS frame's camera and frame time, so it is order-independent within a
 * frame: a basic attack does not have to be submitted after the hero's cast to
 * see that the surface is running. */
bool LiquidSurface_RequestBody(LiquidSurfacePriority priority, Vector3 center,
                              float worldRadius, bool alreadyRunning);

/* The fallback reconstruction radius has one priority owner. A gated
 * caller sets it through this, so a boss ultimate's kernel is not resized by a
 * player cast that happened to submit after it. Within a frame the highest
 * priority wins; equal priorities are last-writer-wins.
 * LiquidSurface_SetReconstructionRadius stays unconditional for ungated callers.
 * Each material records its largest submitted radius; reconstruction uses the
 * winning fragment's material kernel instead of another material's last value. */
void LiquidSurface_SetReconstructionRadiusFor(LiquidSurfacePriority priority, float radius);

/* Screen-space liquid surface. Register from a 3D draw path (no GL work),
 * capture after SceneTargets_End(), then composite into ScreenDistort's VFX
 * body layer before its HDR scene composite. */
void LiquidSurface_Init(int width, int height);
void LiquidSurface_Unload(void);
/* Sets the optical identity used by absorption, scattering and highlights.
 * Callers normally forward body/glow/soft from VFX_Material(...).
 * Shorthand for binding LiquidSurface_DielectricDesc(body, glow, soft). */
void LiquidSurface_SetMaterialColors(Color body, Color glow, Color soft);
/* Approximate world-space radius of one optical kernel. It controls the
 * depth range used by screen-space surface reconstruction. */
void LiquidSurface_SetReconstructionRadius(float radius);
/* Optional per-frame screen-footprint hint. Call immediately before submitting
 * a coherent body. Multiple hints accumulate conservatively (largest wins).
 * Callers that omit it keep the full two-round HIGH reconstruction path. */
void LiquidSurface_HintBody(Vector3 center, float worldRadius);
void LiquidSurface_RegisterParticle(Vector3 position, float radius);
void LiquidSurface_RegisterEllipsoid(Vector3 position, Vector3 radii);
/* Accepts the same opaque stream from either particle backend. The GPU path
 * is rasterized by the owning renderer and is never read back to CPU. CPU
 * streams are queued and share the remaining aggregate 384-sample budget at
 * capture; true means accepted into the queue, not fully admitted. */
bool LiquidSurface_SubmitParticleStream(const ParticleRenderStream *stream);
/* Capture/composite host submission costs, never GPU elapsed time. CPU streams
 * share the aggregate sample budget at capture time; accepted submission means
 * queued, not guaranteed full particle admission. Inspect these counters after
 * capture. Enable periodic logging with WUXING_LIQUID_PROFILE=1. */
typedef struct LiquidSurfaceStats {
    int cpuRequested, cpuAdmitted, cpuDropped, streamRejected;
    int gpuStreams, gpuCaptureInstances, captureDraws, reconstructionPasses;
    double sceneCopyHostMs, frontHostMs, backHostMs, thicknessHostMs;
    double reconstructionHostMs, compositeHostMs;
} LiquidSurfaceStats;
LiquidSurfaceStats LiquidSurface_GetStats(void);
/* Whether the current frame has any surface input to capture/composite. */
bool LiquidSurface_HasPending(void);
void LiquidSurface_Capture(Camera3D camera);
void LiquidSurface_Composite(void);

#endif
