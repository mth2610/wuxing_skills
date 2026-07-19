#include "core/gfx_quality.h"

static GfxQuality s_q = GFX_MED;

void GfxQuality_Set(GfxQuality q) { s_q = q; }
GfxQuality GfxQuality_Get(void)   { return s_q; }

GfxQuality GfxQuality_Default(void) {
#if defined(__ANDROID__)
    return GFX_MED;      // A33/Mali class; drop to GFX_LOW if perf demands
#else
    return GFX_HIGH;     // desktop / Vulkan-Mac
#endif
}
