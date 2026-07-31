// P4 — Scorch. Owner-approved visual primary using the semantic review source.
// The source is preview-only; its profile remains the sole owner of the path.
#include "core/tuning.h"

static float s_scorchLife = 1.0f;
static float s_scorchRadius = 1.0f;
static bool s_scorchReady = false;

static void Scorch_Init(void)
{
    if (s_scorchReady) return;
    Tuning_RegisterFloat("scorch_life", &s_scorchLife, 1.0f);
    Tuning_RegisterFloat("scorch_radius", &s_scorchRadius, 1.0f);
    s_scorchReady = true;
}

// Event primary. The currently approved source is deliberately constrained to
// review: profile provenance prevents this from becoming a filename selection.
void VFX_ComposeScorch(Vector3 pos, VC_MaterialId matId, float scale, float severity01)
{
    Scorch_Init();
    const VFX_SurfaceProfile *surface = VFX_SurfaceRegistry_Get(VFX_SURFACE_DECAL_SCORCH);
    if (surface == NULL || surface->body.id == 0 || scale <= 0.0f) return;

    severity01 = severity01 < 0.0f ? 0.0f : (severity01 > 1.0f ? 1.0f : severity01);
    const VFX_ElementMaterial *material = VFX_Material(matId);
    Color tint = WHITE;
    // Char must remain materially dark under multiplied blending; this is an
    // intentional material identity break, not an element-colour hardcode.
    tint.a = (unsigned char)(120.0f + 75.0f * severity01);

    float radius = (0.72f + 0.58f * severity01) * scale * s_scorchRadius;
    float life = surface->lifetimeSeconds * s_scorchLife;
    float phase = Random01() * 6.2831853f + (material ? material->body.r * 0.01f : 0.0f);
    DecalSystem_AddConformalEx(pos, Random01() * 360.0f, 0.0f, radius * 0.88f, radius,
                               surface->body, life, tint, BLEND_ALPHA, 0.045f,
                               VFX_GroundHeightFromMap, NULL, VFX_GroundSurfaceFromMap, phase,
                               surface->fadeInSeconds, surface->fadeOutSeconds,
                               surface->maxSlopeDegrees);
}
