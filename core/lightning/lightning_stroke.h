#ifndef CORE_LIGHTNING_LIGHTNING_STROKE_H
#define CORE_LIGHTNING_LIGHTNING_STROKE_H

// Dedicated short-lived lightning renderer.  This is intentionally separate
// from RibbonStrip: a ribbon preserves a broad continuous sheet through a
// bend, while this module uses an endpoint-pinned, camera-facing canvas whose
// fragment shader carves one FBM-warped distance-field filament.

#include "raylib.h"

typedef enum {
    LIGHTNING_STROKE_RENDER_BODY = 0,
    LIGHTNING_STROKE_RENDER_HALO,
    LIGHTNING_STROKE_RENDER_CORE
} LightningStrokeRenderLayer;

typedef struct {
    Color bodyColor;          // alpha-composited hue carrier
    Color haloColor;          // low-energy additive blue glow
    Color coreColor;          // compact additive near-white filament
    float width;              // body half-width in metres
    float lifetime;           // legacy total-life fallback when postImpactDuration is negative
    float travelDuration;     // seconds for the discharge head to reach `to`
    float postImpactDuration; // seconds to keep arcing after impact; default 0.30, 0 = die on impact
    float coreEmission;       // HDR multiplier for the ion channel
    float haloEmission;       // low-energy multiplier for the surrounding field
    float jaggedness;         // maximum lateral midpoint displacement, metres
    float flickerInterval;    // seconds between path reseeds
    int branchCount;          // 0..2, off by default
    unsigned int seed;        // 0 derives a deterministic endpoint seed
} LightningStrokeConfig;

LightningStrokeConfig LightningStroke_DefaultConfig(void);
int  LightningStroke_Spawn(Vector3 from, Vector3 to, const LightningStrokeConfig *config);
void LightningStroke_SetEndpoints(int handle, Vector3 from, Vector3 to);
void LightningStroke_Kill(int handle);
void LightningStroke_Update(float dt);

// Geometry only appears inside the caller's already-selected VFX render layer
// and blend scope. The primitive begins/ends its own custom shader so it never
// leaks material state into neighbouring effects.
void LightningStroke_DrawLayer(Camera3D camera, LightningStrokeRenderLayer layer);

#endif
