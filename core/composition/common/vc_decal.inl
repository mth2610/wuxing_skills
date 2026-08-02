// Elemental decal primary. The material chooses the visual style while the
// projection, pool, and lifetime mechanics remain shared.
#include "core/tuning.h"

static float s_scorchLife = 1.0f;
static float s_scorchRadius = 1.0f;
static float s_frostLife = 1.0f;
static float s_frostRadius = 1.0f;
static bool s_decalReady = false;

static void Decal_Init(void)
{
    if (s_decalReady) return;
    Tuning_RegisterFloat("scorch_life", &s_scorchLife, 1.0f);
    Tuning_RegisterFloat("scorch_radius", &s_scorchRadius, 1.0f);
    Tuning_RegisterFloat("frost_life", &s_frostLife, 1.0f);
    Tuning_RegisterFloat("frost_radius", &s_frostRadius, 1.0f);
    s_decalReady = true;
}

void VFX_ComposeDecal(Vector3 pos, VC_MaterialId matId, float scale,
                      float severity01, float lifetimeScale)
{
    Decal_Init();
    severity01 = severity01 < 0.0f ? 0.0f : (severity01 > 1.0f ? 1.0f : severity01);
    const VFX_ElementMaterial *material = VFX_Material(matId);
    VFX_SurfaceId surfaceId = VFX_SURFACE_DECAL_IMPACT;
    Color tint = material ? material->body : WHITE;
    float radiusMul = 1.0f;
    float lifeMul = 1.0f;
    float yOffset = 0.035f;

    if (matId == VC_MAT_FIRE)
    {
        surfaceId = VFX_SURFACE_DECAL_SCORCH;
        tint = WHITE;
        radiusMul = s_scorchRadius * (0.72f + 0.58f * severity01);
        lifeMul = s_scorchLife;
        yOffset = 0.045f;
    }
    else if (matId == VC_MAT_ICE)
    {
        surfaceId = VFX_SURFACE_DECAL_FROST;
        tint = material ? material->soft : WHITE;
        radiusMul = s_frostRadius * (0.78f + 0.52f * severity01);
        lifeMul = s_frostLife * (1.25f + 0.45f * severity01);
        yOffset = 0.040f;
    }

    const VFX_SurfaceProfile *surface = VFX_SurfaceRegistry_Get(surfaceId);
    if (surface == NULL || surface->body.id == 0 || scale <= 0.0f) return;
    tint.a = (unsigned char)((matId == VC_MAT_FIRE ? 120.0f : 155.0f) +
                             (matId == VC_MAT_FIRE ? 75.0f : 65.0f) * severity01);
    float radius = scale * radiusMul;
    float life = surface->lifetimeSeconds * lifeMul * lifetimeScale;
    float phase = Random01() * 6.2831853f +
                  (material ? material->body.b * 0.01f : 0.0f);
    DecalMaterialParams params = {
        .baseTint = material ? material->body : WHITE,
        .emissiveTint = material ? material->glow : WHITE,
        .emissiveThreshold = 0.78f,
        .emissiveIntensity = 1.85f
    };
    DecalSystem_AddConformalMaterialEx(pos, Random01() * 360.0f, 0.0f,
                                       radius * 0.90f, radius, surface->body, life,
                                       tint, BLEND_ALPHA, yOffset,
                                       VFX_GroundHeightFromMap, NULL,
                                       VFX_GroundSurfaceFromMap, phase,
                                       surface->fadeInSeconds, surface->fadeOutSeconds,
                                       surface->maxSlopeDegrees, &params);
}
