#include "core/trail_system.h"
#include "core/resource_manager.h"
#include "raylib.h"
#include "raymath.h"
#include <stdbool.h>

static SkillCurve s_spiritWispWidthCurve;
static SkillCurve s_spiritWispAlphaCurve;
static bool s_spiritWispCurvesInit = false;

static void InitSpiritWispCurves(void)
{
    if (s_spiritWispCurvesInit)
        return;

    // Width curve: Bulging head, slithering snake-like tail
    FloatCurve_AddStop(&s_spiritWispWidthCurve, 0.0f, 0.4f);
    FloatCurve_AddStop(&s_spiritWispWidthCurve, 0.1f, 1.3f); // Head
    FloatCurve_AddStop(&s_spiritWispWidthCurve, 0.4f, 0.6f); // Neck
    FloatCurve_AddStop(&s_spiritWispWidthCurve, 0.7f, 0.9f); // Body
    FloatCurve_AddStop(&s_spiritWispWidthCurve, 1.0f, 0.0f); // Tail

    // Alpha curve: Slow fade at the tail
    FloatCurve_AddStop(&s_spiritWispAlphaCurve, 0.0f, 1.0f);
    FloatCurve_AddStop(&s_spiritWispAlphaCurve, 0.8f, 0.8f);
    FloatCurve_AddStop(&s_spiritWispAlphaCurve, 1.0f, 0.0f);

    s_spiritWispCurvesInit = true;
}

void VFX_ComposeSpiritWispTest(Vector3 pos)
{
    InitSpiritWispCurves();

    float time = (float)GetTime();
    
    // Ghostly drift forward and up
    Vector3 vel = (Vector3){sinf(time) * 1.5f, 2.5f, 4.0f};

    TrailConfig wispCfg = {0};
    wispCfg.type = TRAIL_TYPE_WISP;
    wispCfg.pos = pos;
    wispCfg.vel = vel;
    wispCfg.len = 3.5f; // Long slithering tail
    wispCfg.thick = 0.25f;
    wispCfg.life = 2.0f; // Lives slightly longer
    wispCfg.tint = (Color){150, 60, 255, 220}; // Eerie purple/magenta glow
    
    wispCfg.smoothSpline = true;
    wispCfg.uvTiling = 3.0f; 
    wispCfg.uvScrollSpeed = -1.5f; // Scroll backwards to simulate energy flowing forward
    
    // Smooth, slow, high-amplitude writhing for ghostly feel
    wispCfg.distortionStrength = 0.15f; 
    wispCfg.distortionSpeed = 0.8f;     
    
    wispCfg.widthCurve = &s_spiritWispWidthCurve;
    wispCfg.alphaCurve = &s_spiritWispAlphaCurve;

    SpawnTrailEntity(wispCfg);
}
