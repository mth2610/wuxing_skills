#ifndef GFX_QUALITY_H
#define GFX_QUALITY_H

#include <stdbool.h>

// Resolved rendering tier, read by core/surface_material.c and other binders.
// UNLIT remains a diagnostic passthrough. Every lit tier receives real shadows;
// surface_lit.fs uses fewer PCF groups on LOW/MED. Auto selects lit tiers only.
typedef enum {
    GFX_UNLIT = 0,
    GFX_LOW   = 1,
    GFX_MED   = 2,
    GFX_HIGH  = 3,
    GFX_ULTRA = 4  // Append-only: existing packed shader tiers retain their ids.
} GfxQuality;

void       GfxQuality_Set(GfxQuality q);   // runtime switch (sandbox UI / options menu)
GfxQuality GfxQuality_Get(void);           // read at material bind + anywhere else
GfxQuality GfxQuality_Default(void);       // platform default
// Initialize fixed environment override, or Auto when unset/"auto".
void       GfxQuality_InitDefault(void);
// Auto starts from current lit tier and targets 60 FPS by sustained downgrades
// Ultra -> High -> Low; it never selects Unlit. Fixed Set disables Auto.
void       GfxQuality_SetAuto(bool enabled);
bool       GfxQuality_IsAuto(void);
// Feed actual elapsed frame seconds once/frame, including presentation wait.
// Ignores loading stalls >=250 ms; 1.5 s warmup, two slow 1 s windows and
// 2 s settling after changes prevent transient drops and tier oscillation.
void       GfxQuality_UpdateAuto(float frameSeconds);

#endif // GFX_QUALITY_H
