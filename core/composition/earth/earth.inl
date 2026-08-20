// earth.inl — Master include for earth element VFX sub-modules
// Included once by visual_composer.c

static ColorGradient s_earthDustGrad = {0};  
static ColorGradient s_earthChunkGrad = {0}; 
static ColorGradient s_earthGrainGrad = {0}; 
static SkillCurve s_earthDustBillow = {0};   
static bool s_earthFxInit = false;

static void EarthFx_InitShared(void)
{
    if (s_earthFxInit)
        return;
    ColorGradient_AddStop(&s_earthDustGrad, 0.0f, (Color){150, 125, 95, 0});
    ColorGradient_AddStop(&s_earthDustGrad, 0.25f, (Color){140, 115, 88, 120});
    ColorGradient_AddStop(&s_earthDustGrad, 1.0f, (Color){85, 72, 60, 0});

    ColorGradient_AddStop(&s_earthChunkGrad, 0.0f, (Color){175, 130, 85, 255});
    ColorGradient_AddStop(&s_earthChunkGrad, 0.6f, (Color){120, 90, 60, 220});
    ColorGradient_AddStop(&s_earthChunkGrad, 1.0f, (Color){55, 42, 32, 0});

    ColorGradient_AddStop(&s_earthGrainGrad, 0.0f, (Color){235, 200, 150, 255});
    ColorGradient_AddStop(&s_earthGrainGrad, 0.5f, (Color){190, 150, 100, 200});
    ColorGradient_AddStop(&s_earthGrainGrad, 1.0f, (Color){100, 80, 55, 0});

    FloatCurve_AddStop(&s_earthDustBillow, 0.0f, 0.45f);
    FloatCurve_AddStop(&s_earthDustBillow, 1.0f, 1.8f);
    s_earthFxInit = true;
}

#include "stone_pillar.inl"
#include "fissure_streak.inl"
// @gen:earth_includes begin
// 0 include(s) — auto-managed by sync_vfx_test.py
// @gen:earth_includes end
