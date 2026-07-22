// wood.inl — Master include for wood element VFX sub-modules
// Included once by visual_composer.c

static Vector3 GetWoodPointAt(Vector3 startPos, Vector3 p1, Vector3 p2, Vector3 contactPos, Vector3 targetPos, float t, float sizeScale, int branchIndex, int branchCount)
{
    float castSeedPhase = (startPos.x * 137.3f + startPos.z * 313.1f + targetPos.x * 914.4f + targetPos.z * 519.9f);
    float branchPhase = (branchCount > 1) ? ((float)branchIndex / (float)branchCount) * 2.0f * PI : 0.0f;
    branchPhase += castSeedPhase;

    if (t <= 1.0f)
    {
        Vector3 pos = GetBezierPoint(startPos, p1, p2, contactPos, t);
        Vector3 dir = Vector3Normalize(Vector3Subtract(contactPos, startPos));
        Vector3 perp = (Vector3){-dir.z, 0, dir.x};
        if (Vector3Length(perp) == 0.0f)
            perp = (Vector3){0, 0, 1};
        perp = Vector3Normalize(perp);

        float envelope = sinf(t * PI);

        float wave1 = sinf(t * 5.0f + branchPhase);
        float wave2 = sinf(t * 9.0f + branchPhase * 1.618f) * 0.4f;
        float organicSway = (wave1 + wave2) * 0.4f * sizeScale * envelope;

        pos = Vector3Add(pos, Vector3Scale(perp, organicSway));

        float archHeight = 0.05f + 0.02f * (branchIndex % 3);
        pos.y += envelope * archHeight * sizeScale + 0.02f;

        return pos;
    }
    else
    {
        float t_wrap = t - 1.0f;
        float ratio = t_wrap / 0.8f;
        if (ratio > 1.0f)
            ratio = 1.0f;

        float easeRatio = ratio * ratio * (3.0f - 2.0f * ratio);

        float sphereRadius = 1.25f * sizeScale;
        Vector3 sphereCenter = {targetPos.x, targetPos.y + sphereRadius, targetPos.z};

        float startPhi = PI * 0.95f;
        float endPhi = PI * 0.05f;
        float phi = startPhi - easeRatio * (startPhi - endPhi);

        float contactAngle = atan2f(contactPos.z - targetPos.z, contactPos.x - targetPos.x);

        float coilDir = (branchIndex % 2 == 0) ? 1.0f : -1.0f;

        float baseTurns = (branchCount == 1) ? 3.5f : 1.8f;
        float turns = baseTurns + 0.4f * (branchIndex % 3);

        float theta = easeRatio * turns * (2.0f * PI) * coilDir + contactAngle + branchPhase;

        float wobble = (sinf(theta * 2.0f) * 0.05f + cosf(phi * 3.0f) * 0.04f) * sizeScale;
        float currentRadius = sphereRadius + wobble;

        return (Vector3){
            sphereCenter.x + currentRadius * sinf(phi) * cosf(theta),
            sphereCenter.y + currentRadius * cosf(phi),
            sphereCenter.z + currentRadius * sinf(phi) * sinf(theta)};
    }
}

static ColorGradient s_leafGrad = {0};      
static ColorGradient s_leafHeroGrad = {0};  
static ColorGradient s_pollenGrad = {0};    
static SkillCurve s_leafFlutter = {0};      
static SkillCurve s_leafFadeInOut = {0};    
static bool s_woodAmbInit = false;

static void WoodAmbience_InitShared(void)
{
    if (s_woodAmbInit)
        return;

    ColorGradient_AddStop(&s_leafGrad, 0.0f, (Color){70, 220, 120, 230});
    ColorGradient_AddStop(&s_leafGrad, 0.6f, (Color){46, 204, 113, 180});
    ColorGradient_AddStop(&s_leafGrad, 1.0f, (Color){20, 90, 55, 0});

    ColorGradient_AddStop(&s_leafHeroGrad, 0.0f, (Color){200, 255, 210, 255});
    ColorGradient_AddStop(&s_leafHeroGrad, 0.5f, (Color){120, 240, 150, 210});
    ColorGradient_AddStop(&s_leafHeroGrad, 1.0f, (Color){40, 140, 80, 0});

    ColorGradient_AddStop(&s_pollenGrad, 0.0f, (Color){230, 255, 150, 0});
    ColorGradient_AddStop(&s_pollenGrad, 0.25f, (Color){210, 250, 130, 200});
    ColorGradient_AddStop(&s_pollenGrad, 1.0f, (Color){120, 180, 60, 0});

    FloatCurve_AddStop(&s_leafFlutter, 0.0f, 0.7f);
    FloatCurve_AddStop(&s_leafFlutter, 0.2f, 1.25f);
    FloatCurve_AddStop(&s_leafFlutter, 0.45f, 0.75f);
    FloatCurve_AddStop(&s_leafFlutter, 0.7f, 1.15f);
    FloatCurve_AddStop(&s_leafFlutter, 1.0f, 0.6f);

    FloatCurve_AddStop(&s_leafFadeInOut, 0.0f, 0.0f);
    FloatCurve_AddStop(&s_leafFadeInOut, 0.15f, 1.0f);
    FloatCurve_AddStop(&s_leafFadeInOut, 0.8f, 0.9f);
    FloatCurve_AddStop(&s_leafFadeInOut, 1.0f, 0.0f);

    s_woodAmbInit = true;
}

#include "glowing_vine.inl"
// @gen:wood_includes begin
// 1 include(s) — auto-managed by sync_vfx_test.py
#include "leaf_swirl.inl"
// @gen:wood_includes end
