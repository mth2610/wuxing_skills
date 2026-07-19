#ifndef GFX_QUALITY_H
#define GFX_QUALITY_H

// Real Shading Plan P0 — single global quality switch read by
// core/surface_material.c at bind time. OFF (`GFX_UNLIT`) leaves models on a
// flat passthrough (zero shading cost); LOW/MED/HIGH gate runtime branches
// inside a single `surface_lit` shader (see REAL_SHADING_SPEC.md).
typedef enum {
    GFX_UNLIT = 0,
    GFX_LOW   = 1,
    GFX_MED   = 2,
    GFX_HIGH  = 3
} GfxQuality;

void       GfxQuality_Set(GfxQuality q);   // runtime switch (sandbox UI / options menu)
GfxQuality GfxQuality_Get(void);           // read at material bind + anywhere else
GfxQuality GfxQuality_Default(void);       // platform default

#endif // GFX_QUALITY_H
