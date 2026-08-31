#include "core/gfx_quality.h"

#include <stdbool.h>
#include <ctype.h>
#include <stdlib.h>

static bool GfxQuality_StringEqualsIgnoreCase(const char *left, const char *right) {
    if (left == NULL || right == NULL) return false;
    while (*left != '\0' && *right != '\0') {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right))
            return false;
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

static GfxQuality GfxQuality_ParseOverride(const char *value, GfxQuality fallback) {
    if (value == NULL || *value == '\0') return fallback;
    if (GfxQuality_StringEqualsIgnoreCase(value, "unlit") ||
        GfxQuality_StringEqualsIgnoreCase(value, "off") ||
        GfxQuality_StringEqualsIgnoreCase(value, "0"))
        return GFX_UNLIT;
    if (GfxQuality_StringEqualsIgnoreCase(value, "low") ||
        GfxQuality_StringEqualsIgnoreCase(value, "1"))
        return GFX_LOW;
    if (GfxQuality_StringEqualsIgnoreCase(value, "med") ||
        GfxQuality_StringEqualsIgnoreCase(value, "medium") ||
        GfxQuality_StringEqualsIgnoreCase(value, "2"))
        return GFX_MED;
    if (GfxQuality_StringEqualsIgnoreCase(value, "high") ||
        GfxQuality_StringEqualsIgnoreCase(value, "3"))
        return GFX_HIGH;
    return fallback;
}

static GfxQuality s_q = GFX_MED;

void GfxQuality_Set(GfxQuality q) { s_q = q; }
GfxQuality GfxQuality_Get(void)   { return s_q; }

GfxQuality GfxQuality_Default(void) {
#if defined(__ANDROID__)
    GfxQuality fallback = GFX_MED;  // A33/Mali class; drop to LOW if perf demands
#else
    GfxQuality fallback = GFX_HIGH; // desktop / Vulkan-Mac
#endif
    return GfxQuality_ParseOverride(getenv("WUXING_GFX_QUALITY"), fallback);
}
