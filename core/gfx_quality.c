#include "core/gfx_quality.h"

#include <stdbool.h>
#include <ctype.h>
#include <stdlib.h>
#include <math.h>

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
    if (GfxQuality_StringEqualsIgnoreCase(value, "ultra") ||
        GfxQuality_StringEqualsIgnoreCase(value, "4"))
        return GFX_ULTRA;
    return fallback;
}

static GfxQuality s_q = GFX_MED;
static bool s_auto;
static float s_autoWarmup;
static float s_autoWindowSeconds;
static int s_autoWindowFrames;
static int s_autoSlowWindows;

void GfxQuality_Set(GfxQuality q) {
    s_q = q < GFX_UNLIT ? GFX_UNLIT : q > GFX_ULTRA ? GFX_ULTRA : q;
    s_auto = false;
}
GfxQuality GfxQuality_Get(void)   { return s_q; }

void GfxQuality_SetAuto(bool enabled) {
    s_auto = enabled;
    if (enabled && s_q < GFX_LOW) s_q = GFX_LOW;
    s_autoWarmup = 1.5f;
    s_autoWindowSeconds = 0.0f;
    s_autoWindowFrames = 0;
    s_autoSlowWindows = 0;
}

bool GfxQuality_IsAuto(void) { return s_auto; }

void GfxQuality_UpdateAuto(float frameSeconds) {
    if (!s_auto || !isfinite(frameSeconds) || frameSeconds <= 0.0f || frameSeconds >= 0.25f) return;
    if (s_autoWarmup > 0.0f) {
        s_autoWarmup -= frameSeconds;
        return;
    }
    s_autoWindowSeconds += frameSeconds;
    s_autoWindowFrames++;
    if (s_autoWindowSeconds < 1.0f) return;
    float averageSeconds = s_autoWindowSeconds / (float)s_autoWindowFrames;
    s_autoSlowWindows = averageSeconds > 1.0f / 57.0f ? s_autoSlowWindows + 1 : 0;
    s_autoWindowSeconds = 0.0f;
    s_autoWindowFrames = 0;
    if (s_autoSlowWindows >= 2 && s_q > GFX_LOW) {
        s_q = s_q > GFX_HIGH ? GFX_HIGH : GFX_LOW;
        s_autoWarmup = 2.0f;
        s_autoSlowWindows = 0;
    }
}

GfxQuality GfxQuality_Default(void) {
#if defined(__ANDROID__)
    GfxQuality fallback = GFX_MED;  // A33/Mali class; drop to LOW if perf demands
#else
    GfxQuality fallback = GFX_HIGH; // desktop / Vulkan-Mac
#endif
    return GfxQuality_ParseOverride(getenv("WUXING_GFX_QUALITY"), fallback);
}

void GfxQuality_InitDefault(void) {
    const char *override = getenv("WUXING_GFX_QUALITY");
    GfxQuality_Set(GfxQuality_Default());
    GfxQuality_SetAuto(override == NULL || *override == '\0' ||
                      GfxQuality_StringEqualsIgnoreCase(override, "auto"));
}
