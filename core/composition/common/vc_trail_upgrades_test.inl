#include "core/trail_system.h"
#include "core/resource_manager.h"
#include "core/force_field.h"
#include "raylib.h"
#include "raymath.h"
#include <stdbool.h>

static SkillCurve s_trailWidthCurve;
static SkillCurve s_trailAlphaCurve;
static bool s_trailCurvesInit = false;

static void InitTrailCurves(void)
{
    if (s_trailCurvesInit)
        return;

    // Width curve: starts thin at head, bulges gracefully in middle, tapers to point at tail
    FloatCurve_AddStop(&s_trailWidthCurve, 0.0f, 0.2f);
    FloatCurve_AddStop(&s_trailWidthCurve, 0.4f, 1.2f);
    FloatCurve_AddStop(&s_trailWidthCurve, 1.0f, 0.05f);

    // Alpha curve: full opacity at head, smooth fade out at tail
    FloatCurve_AddStop(&s_trailAlphaCurve, 0.0f, 1.0f);
    FloatCurve_AddStop(&s_trailAlphaCurve, 0.6f, 0.85f);
    FloatCurve_AddStop(&s_trailAlphaCurve, 1.0f, 0.0f);

    s_trailCurvesInit = true;
}

void VFX_ComposeTrailUpgradesTest(Vector3 pos)
{
    InitTrailCurves();

    float time = (float)GetTime();
    Vector3 targetPos = (Vector3){
        pos.x + sinf(time * 2.2f) * 2.5f,
        pos.y + 1.8f + sinf(time * 3.5f) * 0.8f,
        pos.z + cosf(time * 2.2f) * 2.5f};

    // Oneshot Trail Spawn: 1.4s lifetime, flies, fades out, and disappears cleanly
    TrailConfig wispCfg = {0};
    wispCfg.type = TRAIL_TYPE_WISP;
    wispCfg.pos = pos;
    wispCfg.target = targetPos;
    wispCfg.vel = (Vector3){0, 0, 6.0f};       // Shoot forward!
    wispCfg.len = 2.2f;
    wispCfg.thick = 0.16f;
    wispCfg.life = 1.4f;
    wispCfg.tint = (Color){80, 170, 255, 200}; // Deep crisp blue — no over-exposure flare
    wispCfg.smoothSpline = true;               // Upgrade 1: Catmull-Rom smooth spline
    wispCfg.uvTiling = 2.0f;                   // Upgrade 2: Texture UV tiling
    wispCfg.uvScrollSpeed = 2.0f;              // Upgrade 2: Texture UV scrolling
    wispCfg.distortionStrength = 0.00f;        // Upgrade 2: Silky smooth low-frequency wave (no zipper micro-ripples)
    wispCfg.distortionSpeed = 1.5f;
    wispCfg.widthCurve = &s_trailWidthCurve; // Upgrade 3: SkillCurve Width Envelope
    wispCfg.alphaCurve = &s_trailAlphaCurve; // Upgrade 3: SkillCurve Alpha Envelope

    SpawnTrailEntity(wispCfg);
}
