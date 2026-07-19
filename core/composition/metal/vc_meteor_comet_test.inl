#include "core/trail_system.h"
#include "core/resource_manager.h"
#include "raylib.h"
#include "raymath.h"
#include <stdbool.h>

static SkillCurve s_meteorWidthCurve;
static SkillCurve s_meteorAlphaCurve;
static bool s_meteorCurvesInit = false;

static void InitMeteorCurves(void)
{
    if (s_meteorCurvesInit)
        return;

    // Width curve: Huge head, slowly tapering to a long tail
    FloatCurve_AddStop(&s_meteorWidthCurve, 0.0f, 0.0f);
    FloatCurve_AddStop(&s_meteorWidthCurve, 0.3f, 0.8f);
    FloatCurve_AddStop(&s_meteorWidthCurve, 0.8f, 1.3f);
    FloatCurve_AddStop(&s_meteorWidthCurve, 1.0f, 2.5f); // Head

    // Alpha curve: Solid at head, fading smoothly at tail
    FloatCurve_AddStop(&s_meteorAlphaCurve, 0.0f, 0.0f);
    FloatCurve_AddStop(&s_meteorAlphaCurve, 0.3f, 0.5f);
    FloatCurve_AddStop(&s_meteorAlphaCurve, 0.8f, 0.9f);
    FloatCurve_AddStop(&s_meteorAlphaCurve, 1.0f, 1.0f);

    s_meteorCurvesInit = true;
}

void VFX_ComposeMeteorCometTest(Vector3 pos)
{
    InitMeteorCurves();

    TrailConfig cometCfg = {0};
    
    // Comet falling from the sky!
    cometCfg.type = TRAIL_TYPE_PROJECTILE; 
    cometCfg.pos = (Vector3){pos.x, pos.y + 10.0f, pos.z}; // Spawn high up
    cometCfg.vel = (Vector3){4.0f, -8.0f, 0.0f}; // Fall diagonally down-right
    
    // Very long tail
    cometCfg.trailLength = 40; 
    cometCfg.thick = 0.8f; 
    cometCfg.life = 1.2f; 
    
    // Orange-yellow fire comet, lower alpha to not blow out bloom
    cometCfg.tint = (Color){255, 120, 30, 200}; 
    
    cometCfg.smoothSpline = true;
    
    // ADD TEXTURE so we can see UV scrolling clearly!
    cometCfg.tex = ResourceManager_LoadTexture("assets/textures/energy_flow.png");
    
    // Scroll the texture backward along the tail
    cometCfg.uvTiling = 2.0f; 
    cometCfg.uvScrollSpeed = -3.0f; 
    
    // Meteor rumbles and wiggles a bit as it falls through atmosphere
    cometCfg.distortionStrength = 0.3f; 
    cometCfg.distortionSpeed = 6.0f;     
    
    cometCfg.widthCurve = &s_meteorWidthCurve;
    cometCfg.alphaCurve = &s_meteorAlphaCurve;

    SpawnTrailEntity(cometCfg);
}
