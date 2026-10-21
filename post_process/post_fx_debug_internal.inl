#ifndef WUXING_POST_FX_DEBUG_INTERNAL_INL
#define WUXING_POST_FX_DEBUG_INTERNAL_INL

static bool PostFX_ApplyBloomOverrideValue(bool current, float overrideValue)
{
    if (overrideValue < 0.0f) return current;
    return overrideValue >= 0.5f;
}

#endif
