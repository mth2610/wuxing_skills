// P4 — ImpactDust. A cold particulate event, distinct from smoke: authored
// dust parcels burst outward close to the ground, grow briefly, then tear/fade.
#include "core/vfx_surface_registry.h"

#define IMPACT_DUST_MAX_PARTICLES 14 // A/B: restore contact-cloud density
static Texture2D s_impactDustTex = {0};
static Texture2D s_impactDustNormalTex = {0};
#define IMPACT_DUST_ANIM_RATES 4
static SpriteAnim s_impactDustAnim[IMPACT_DUST_ANIM_RATES];
static SkillCurve s_impactDustGrow = {0};
static SkillCurve s_impactDustFade = {0};
// NE_GroundDust spends its radial impulse in roughly the first 0.2 s. This is
// a velocity multiplier, so the authored 6-12 m/s launch remains measurable.
static bool s_impactDustReady = false;

static void ImpactDust_Init(void)
{
    if (s_impactDustReady)
        return;
    const VFX_SurfaceProfile *profile = VFX_SurfaceRegistry_Get(
        VFX_SURFACE_SMOKE_PUFF_LIGHT_NIAGARA);
    if (profile != NULL && profile->body.id != 0) {
        s_impactDustTex = profile->body;
        s_impactDustNormalTex = profile->normalMap;
    }
    const int frames = (profile != NULL && profile->flipbookFrames > 0)
        ? profile->flipbookFrames : 64;
    const int columns = (profile != NULL && profile->flipbookColumns > 0)
        ? profile->flipbookColumns : 8;
    const int rows = (profile != NULL && profile->flipbookRows > 0)
        ? profile->flipbookRows : 8;
    // Use the complete extracted arc; per-particle rate variation breaks the
    // obvious lockstep that turns a contact cloud into tiled cards.
    static const float rateMul[IMPACT_DUST_ANIM_RATES] = {1.0f, 0.91f, 0.82f, 0.74f};
    for (int i = 0; i < IMPACT_DUST_ANIM_RATES; ++i)
        SpriteAnim_Init(&s_impactDustAnim[i], columns, rows, frames,
                        ((float)frames / 0.98f) * rateMul[i], ANIM_ONCE);
    FloatCurve_AddStop(&s_impactDustGrow, 0.0f, 0.72f);
    FloatCurve_AddStop(&s_impactDustGrow, 0.34f, 1.15f);
    FloatCurve_AddStop(&s_impactDustGrow, 1.0f, 1.38f);
    FloatCurve_AddStop(&s_impactDustFade, 0.0f, 0.0f);
    FloatCurve_AddStop(&s_impactDustFade, 0.10f, 0.78f);
    FloatCurve_AddStop(&s_impactDustFade, 1.0f, 0.0f);
    s_impactDustReady = true;
}

// Event — call once per contact. `severity01` controls parcel count, never a
// persistent emission rate. Cost budget: at most 11 live alpha particles.
void VFX_ComposeImpactDust(Vector3 pos, VC_MaterialId matId, float scale, float severity01)
{
    ImpactDust_Init();
    if (s_impactDustTex.id == 0)
        return;
    severity01 = severity01 < 0.0f ? 0.0f : (severity01 > 1.0f ? 1.0f : severity01);
    if (scale <= 0.0f)
        return;
    const VFX_ElementMaterial *mat = VFX_Material(matId);
    int count = 8 + (int)(severity01 * (IMPACT_DUST_MAX_PARTICLES - 8));
    for (int i = 0; i < count; ++i)
    {
        float angle = Random01() * 2.0f * PI;
        float support = (i == 0) ? 1.0f : Math_Mix(0.48f, 0.86f, Random01());
        float speed = Math_Mix(0.10f, 0.62f, Random01()) * scale * support;
        // The sheet carries the internal shadow. Like SmokePuff's flipbook
        // path, lift the vertex colour so multiplying a dark pocket does not
        // turn dust black. Ground material is only a faint warm hint; dust is
        // read as off-white/light grey, never fire-orange or soot-black.
        const Color baseDust = {218, 210, 192, 78};
        Color dust = {(unsigned char)((baseDust.r * 9 + mat->body.r) / 10),
                      (unsigned char)((baseDust.g * 9 + mat->body.g) / 10),
                      (unsigned char)((baseDust.b * 9 + mat->body.b) / 10), 78};
        SpawnParticle((ParticleConfig){
            .position = {pos.x + cosf(angle) * Random01() * 0.24f * scale,
                         pos.y + 0.035f * scale,
                         pos.z + sinf(angle) * Random01() * 0.24f * scale},
            .velocity = {cosf(angle) * speed, Math_Mix(0.04f, 0.28f, Random01()) * scale,
                         sinf(angle) * speed},
            .radius = (i == 0 ? Math_Mix(0.44f, 0.66f, Random01())
                              : Math_Mix(0.20f, 0.50f, Random01())) *
                      scale,
            .lifetime = Math_Mix(0.82f, 1.10f, Random01()),
            .colorStart = dust,
            .colorEnd = VC_WithAlpha(dust, 0),
            .radiusCurve = &s_impactDustGrow,
            .alphaCurve = &s_impactDustFade,
            // Dust OCCLUDES. Make the blend law explicit: alpha cards, never
            // additive. Low individual alpha lets their overlaps build one
            // cloud instead of visibly separate coloured plates.
            .render.texture = s_impactDustTex,
            .render.blendMode = VFX_BLEND_ALPHA,
            .render.smokeSheet = 1,
            .render.normalTex = s_impactDustNormalTex,
            .spriteAnim = &s_impactDustAnim[i % IMPACT_DUST_ANIM_RATES],
            .spriteAnimPhase = (i == 0) ? 0.0f : Random01() * 0.22f,
            .rotation = Random01() * 2.0f * PI,
            .angularVelocity = (Random01() - 0.5f) * 0.16f,
        });
    }
}
