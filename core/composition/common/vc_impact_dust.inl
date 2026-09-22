// Generic close-contact particulate event. Variants are surface vocabularies,
// not element/gameplay effects; callers own position, material and intensity.
#include "core/vfx_surface_registry.h"

#define IMPACT_DUST_MAX_PARTICLES 14
#define IMPACT_DUST_ANIM_RATES 4

static Texture2D s_impactDustTex[VFX_IMPACT_DUST_VARIANT_COUNT] = {0};
static Texture2D s_impactDustNormalTex[VFX_IMPACT_DUST_VARIANT_COUNT] = {0};
static SpriteAnim s_impactDustAnim[VFX_IMPACT_DUST_VARIANT_COUNT][IMPACT_DUST_ANIM_RATES];
static bool s_impactDustStyleReady[VFX_IMPACT_DUST_VARIANT_COUNT] = {0};
static SkillCurve s_impactDustGrow = {0};
static SkillCurve s_impactDustFade = {0};
static bool s_impactDustCurvesReady = false;

static const VFX_SurfaceId s_impactDustSurfaces[VFX_IMPACT_DUST_VARIANT_COUNT] = {
    VFX_SURFACE_SMOKE_PUFF_LIGHT_NIAGARA,
    VFX_SURFACE_SMOKE_PUFF_DARK_NIAGARA,
    VFX_SURFACE_SMOKE_WISPY_NIAGARA,
    VFX_SURFACE_PLASMA_WISPS_NIAGARA,
};

static int ImpactDust_StyleIndex(VFX_ImpactDustVariant variant)
{
    return (variant >= VFX_IMPACT_DUST_VARIANT_DUST_PUFF &&
            variant < VFX_IMPACT_DUST_VARIANT_COUNT) ? (int)variant : 0;
}

static void ImpactDust_Init(VFX_ImpactDustVariant variant)
{
    const int style = ImpactDust_StyleIndex(variant);
    if (!s_impactDustCurvesReady) {
        FloatCurve_AddStop(&s_impactDustGrow, 0.0f, 0.72f);
        FloatCurve_AddStop(&s_impactDustGrow, 0.34f, 1.15f);
        FloatCurve_AddStop(&s_impactDustGrow, 1.0f, 1.38f);
        FloatCurve_AddStop(&s_impactDustFade, 0.0f, 0.0f);
        FloatCurve_AddStop(&s_impactDustFade, 0.10f, 0.78f);
        FloatCurve_AddStop(&s_impactDustFade, 1.0f, 0.0f);
        s_impactDustCurvesReady = true;
    }
    if (s_impactDustStyleReady[style]) return;
    const VFX_SurfaceProfile *profile = VFX_SurfaceRegistry_Get(s_impactDustSurfaces[style]);
    if (profile != NULL && profile->body.id != 0) {
        static const float rateMul[IMPACT_DUST_ANIM_RATES] = {1.0f, 0.91f, 0.82f, 0.74f};
        s_impactDustTex[style] = profile->body;
        s_impactDustNormalTex[style] = profile->normalMap;
        for (int i = 0; i < IMPACT_DUST_ANIM_RATES; ++i)
            SpriteAnim_Init(&s_impactDustAnim[style][i], profile->flipbookColumns,
                            profile->flipbookRows, profile->flipbookFrames,
                            ((float)profile->flipbookFrames / 0.98f) * rateMul[i], ANIM_ONCE);
    }
    s_impactDustStyleReady[style] = true;
}

const char *VFX_ImpactDustVariant_Name(VFX_ImpactDustVariant variant)
{
    static const char *const names[VFX_IMPACT_DUST_VARIANT_COUNT] = {
        "DUST PUFF", "DARK SMOKE PUFF", "SMOKE WISP", "ENERGY WISP"
    };
    return names[ImpactDust_StyleIndex(variant)];
}

// One bounded contact burst. severity controls population, never a persistent rate.
void VFX_ComposeImpactDustVariant(Vector3 pos, VC_MaterialId matId, float scale,
                                  float severity01, VFX_ImpactDustVariant variant)
{
    const int style = ImpactDust_StyleIndex(variant);
    const bool energy = variant == VFX_IMPACT_DUST_VARIANT_ENERGY_WISP;
    ImpactDust_Init(variant);
    if (s_impactDustTex[style].id == 0 || scale <= 0.0f) return;
    severity01 = Clamp(severity01, 0.0f, 1.0f);
    const VFX_ElementMaterial *mat = VFX_Material(matId);
    const int count = 8 + (int)(severity01 * (IMPACT_DUST_MAX_PARTICLES - 8));
    for (int i = 0; i < count; ++i) {
        const float angle = Random01() * 2.0f * PI;
        const float support = (i == 0) ? 1.0f : Math_Mix(0.48f, 0.86f, Random01());
        const float speed = Math_Mix(0.10f, energy ? 1.10f : 0.62f, Random01()) * scale * support;
        const Color base = energy ? (Color){150, 220, 255, 124} : (Color){218, 210, 192, 78};
        Color tint = {(unsigned char)((base.r * 9 + mat->body.r) / 10),
                      (unsigned char)((base.g * 9 + mat->body.g) / 10),
                      (unsigned char)((base.b * 9 + mat->body.b) / 10),
                      energy ? 92 : 78};
        ParticleConfig p = {
            .position = {pos.x + cosf(angle) * Random01() * 0.24f * scale,
                         pos.y + 0.035f * scale,
                         pos.z + sinf(angle) * Random01() * 0.24f * scale},
            .velocity = {cosf(angle) * speed,
                         Math_Mix(0.04f, energy ? 0.52f : 0.28f, Random01()) * scale,
                         sinf(angle) * speed},
            .radius = (i == 0 ? Math_Mix(0.44f, 0.66f, Random01())
                              : Math_Mix(0.20f, 0.50f, Random01())) * scale,
            .lifetime = energy ? Math_Mix(0.52f, 0.82f, Random01()) : Math_Mix(0.82f, 1.10f, Random01()),
            .colorStart = tint, .colorEnd = VC_WithAlpha(tint, 0),
            .radiusCurve = &s_impactDustGrow, .alphaCurve = &s_impactDustFade,
            .render.texture = s_impactDustTex[style],
            .render.blendMode = energy ? VFX_BLEND_ADDITIVE : VFX_BLEND_ALPHA,
            .spriteAnim = &s_impactDustAnim[style][i % IMPACT_DUST_ANIM_RATES],
            .spriteAnimPhase = (i == 0) ? 0.0f : Random01() * 0.22f,
            .rotation = Random01() * 2.0f * PI,
            .angularVelocity = (Random01() - 0.5f) * 0.16f,
        };
        // Smoke sheets are packed directional data, never display RGB.
        if (!energy) {
            p.render.smokeSheet = 1;
            p.render.normalTex = s_impactDustNormalTex[style];
        } else {
            p.render.unlit = 1;
        }
        SpawnParticle(p);
    }
}

// Compatibility: existing contact callers retain the neutral dust vocabulary.
void VFX_ComposeImpactDust(Vector3 pos, VC_MaterialId matId, float scale, float severity01)
{
    VFX_ComposeImpactDustVariant(pos, matId, scale, severity01,
                                 VFX_IMPACT_DUST_VARIANT_DUST_PUFF);
}

void VFX_SurfaceImpact_Emit(const VFX_SurfaceImpactEvent *event)
{
    if (event == NULL || event->scale <= 0.0f) return;
    VFX_ImpactSurface surface = event->surface;
    if (surface < VFX_IMPACT_SURFACE_EARTH || surface >= VFX_IMPACT_SURFACE_COUNT)
        surface = VFX_IMPACT_SURFACE_EARTH;
    float severity = Clamp(event->severity01, 0.0f, 1.0f);
    Vector3 normal = event->normal;
    if (Vector3LengthSqr(normal) < 0.0001f) normal = (Vector3){0.0f, 1.0f, 0.0f};
    else normal = Vector3Normalize(normal);

    switch (surface) {
    case VFX_IMPACT_SURFACE_WATER:
        /* Water stays fluid-module-free: a frost body plus cold vapor are
         * persistent particle/decal primitives, valid from the update phase. */
        VFX_ComposeDecalVariant(event->position, VC_MAT_ICE, event->scale,
                                severity, 1.0f, VFX_DECAL_VARIANT_FROST);
        VFX_ComposeSmokePuff(event->position, VC_MAT_ICE, event->scale,
                             0.35f + severity * 0.35f);
        return;

    case VFX_IMPACT_SURFACE_METAL:
        VFX_ComposeContactSpark(event->position, event->material, event->scale, severity);
        VFX_ComposeDecalVariant(event->position, event->material, event->scale,
                                severity, 0.65f, VFX_DECAL_VARIANT_IMPACT);
        return;

    case VFX_IMPACT_SURFACE_EARTH:
        /* Earth must read as a dry soil expansion, never as the generic
         * impact carrier that can retain a prior Fire decal in the tester. */
        VFX_ComposeGroundDustRing(event->position, VC_MAT_EARTH,
                                  event->scale, severity);
        return;

    case VFX_IMPACT_SURFACE_FIRE:
    case VFX_IMPACT_SURFACE_WOOD:
    default:
        VFX_ComposeImpactDustVariant(event->position, event->material, event->scale,
                                     severity, VFX_IMPACT_DUST_VARIANT_DUST_PUFF);
        break;
    }
    VFX_ComposeDecalVariant(event->position, event->material, event->scale,
                            severity, 1.0f, VFX_DECAL_VARIANT_IMPACT);
    if (event->material == VC_MAT_FIRE && severity >= 0.45f)
        VFX_ComposeEmberBurst(event->position, normal, event->material,
                              event->scale, severity);
}
