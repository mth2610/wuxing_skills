#include "core/trail_system.h"
#include "core/resource_manager.h"
#include "raylib.h"
#include "raymath.h"
#include <stdbool.h>

static SkillCurve s_crescentSlashWidthCurve;
static SkillCurve s_crescentSlashAlphaCurve;
static bool s_crescentSlashCurvesInit = false;

static void InitCrescentSlashCurves(void)
{
    if (s_crescentSlashCurvesInit)
        return;

    // Width curve: Sharp at ends, massive in the middle (crescent shape)
    FloatCurve_AddStop(&s_crescentSlashWidthCurve, 0.0f, 0.0f);
    FloatCurve_AddStop(&s_crescentSlashWidthCurve, 0.5f, 2.5f);
    FloatCurve_AddStop(&s_crescentSlashWidthCurve, 1.0f, 0.0f);

    // Alpha curve: Solid core, fading fast at the tail tip
    FloatCurve_AddStop(&s_crescentSlashAlphaCurve, 0.0f, 0.0f);
    FloatCurve_AddStop(&s_crescentSlashAlphaCurve, 0.2f, 1.0f);
    FloatCurve_AddStop(&s_crescentSlashAlphaCurve, 0.8f, 1.0f);
    FloatCurve_AddStop(&s_crescentSlashAlphaCurve, 1.0f, 0.0f);

    s_crescentSlashCurvesInit = true;
}

void VFX_ComposeCrescentSlashTest(Vector3 pos)
{
    InitCrescentSlashCurves();

    TrailConfig slashCfg = {0};
    
    // Projectile trail perfectly matches a flying slash
    slashCfg.type = TRAIL_TYPE_PROJECTILE; 
    slashCfg.pos = pos;
    slashCfg.vel = (Vector3){8.0f, 0.0f, 0.0f}; // Fast forward slash!
    
    // Make the trail very short so it looks like a concentrated crescent
    slashCfg.trailLength = 10; // Only keep the last 10 nodes to make it very short!
    slashCfg.len = 1.0f; // Doesn't affect projectiles much, but good practice
    slashCfg.thick = 0.8f; // Make it a bit wider to look more like a crescent!
    slashCfg.life = 0.6f; // Quick lethal strike
    slashCfg.tint = (Color){255, 180, 20, 130}; // Warm golden metal, reduced alpha to prevent blinding white blowout
    
    slashCfg.smoothSpline = true;
    
    // High UV scroll speed so the energy inside the crescent looks like it's ripping through the air
    slashCfg.uvTiling = 1.0f; 
    slashCfg.uvScrollSpeed = 4.0f; 
    
    // Minimal distortion, metal attacks are precise and clean
    slashCfg.distortionStrength = 0.01f; 
    slashCfg.distortionSpeed = 5.0f;     
    
    slashCfg.widthCurve = &s_crescentSlashWidthCurve;
    slashCfg.alphaCurve = &s_crescentSlashAlphaCurve;

    SpawnTrailEntity(slashCfg);
}
