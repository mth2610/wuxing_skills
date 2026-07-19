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
    FloatCurve_AddStop(&s_meteorWidthCurve, 0.95f, 2.5f); // Bulbous head
    FloatCurve_AddStop(&s_meteorWidthCurve, 1.0f, 0.0f);  // Pinch to a point at the absolute tip to avoid flat cut-off

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
    cometCfg.disableInnerCore = true;    // Tắt lớp lõi phát sáng
    cometCfg.blendMode = BLEND_ALPHA;    // Dùng alpha thường thay vì additive
    cometCfg.useCustomBlendMode = true;  // Bắt buộc, vì BLEND_ALPHA=0 không detect được qua ">0"

    cometCfg.type = TRAIL_TYPE_WISP;
    cometCfg.pos = (Vector3){pos.x, pos.y + 10.0f, pos.z}; // Spawn high up
    cometCfg.vel = (Vector3){4.0f, -8.0f, 0.0f};           // Fall diagonally down-right

    // Very long tail
    cometCfg.len = 6.0f; // Wisp uses physical length
    cometCfg.thick = 0.8f;
    cometCfg.life = 1.2f;

    // Orange tint but dark enough to stay below bloomThreshold=0.5 luma
    // so PostFX bloom won't kick in — lets us see pure BLEND_ALPHA effect.
    // Luma = 0.299*r + 0.587*g + 0.114*b
    // (140, 60, 15) luma = 0.299*0.55 + 0.587*0.24 + 0.114*0.06 ≈ 0.31 < 0.5
    cometCfg.tint = (Color){140, 60, 15, 200};

    cometCfg.smoothSpline = true;

    // ADD TEXTURE so we can see UV scrolling clearly! Use noise to avoid directional mapping issues
    cometCfg.tex = ResourceManager_LoadTexture("assets/textures/noise.png");

    // Scroll the texture backward along the tail
    cometCfg.uvTiling = 1.0f;
    cometCfg.uvScrollSpeed = -1.5f;

    // Meteor rumbles and wiggles a bit as it falls through atmosphere
    cometCfg.distortionStrength = 0.2f;
    cometCfg.distortionSpeed = 6.0f;

    cometCfg.widthCurve = &s_meteorWidthCurve;
    cometCfg.alphaCurve = &s_meteorAlphaCurve;

    SpawnTrailEntity(cometCfg);
}
