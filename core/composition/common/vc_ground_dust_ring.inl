// NE_GroundDust — physical ground shockwave, separate from compact impact dust.
#include "core/vfx_surface_registry.h"

#define GROUND_DUST_RING_MIN_PARTICLES 25
#define GROUND_DUST_RING_MAX_PARTICLES 40
#define GROUND_DUST_RING_ANIM_RATES 4

static Texture2D s_groundDustRingTex = {0};
static Texture2D s_groundDustRingNormalTex = {0};
static SpriteAnim s_groundDustRingAnim[GROUND_DUST_RING_ANIM_RATES];
static SkillCurve s_groundDustRingGrow = {0};
static SkillCurve s_groundDustRingFade = {0};
static SkillCurve s_groundDustRingBrake = {0};
static bool s_groundDustRingReady = false;

static void GroundDustRing_Init(void)
{
    if (s_groundDustRingReady) return;
    const VFX_SurfaceProfile *profile = VFX_SurfaceRegistry_Get(
        VFX_SURFACE_SMOKE_PUFF_LIGHT_NIAGARA);
    s_groundDustRingTex = profile != NULL ? profile->body : (Texture2D){0};
    s_groundDustRingNormalTex = profile != NULL ? profile->normalMap : (Texture2D){0};
    const int frames = profile != NULL && profile->flipbookFrames > 0 ? profile->flipbookFrames : 64;
    const int columns = profile != NULL && profile->flipbookColumns > 0 ? profile->flipbookColumns : 8;
    const int rows = profile != NULL && profile->flipbookRows > 0 ? profile->flipbookRows : 8;
    static const float rateMul[GROUND_DUST_RING_ANIM_RATES] = {1.0f, 0.91f, 0.82f, 0.74f};
    for (int i = 0; i < GROUND_DUST_RING_ANIM_RATES; ++i)
        SpriteAnim_Init(&s_groundDustRingAnim[i], columns, rows, frames,
                        ((float)frames / 0.98f) * rateMul[i], ANIM_ONCE);
    FloatCurve_AddStop(&s_groundDustRingGrow, 0.0f, 0.72f);
    FloatCurve_AddStop(&s_groundDustRingGrow, 0.34f, 1.15f);
    FloatCurve_AddStop(&s_groundDustRingGrow, 1.0f, 1.38f);
    FloatCurve_AddStop(&s_groundDustRingFade, 0.0f, 0.0f);
    FloatCurve_AddStop(&s_groundDustRingFade, 0.10f, 0.78f);
    FloatCurve_AddStop(&s_groundDustRingFade, 1.0f, 0.0f);
    FloatCurve_AddStop(&s_groundDustRingBrake, 0.0f, 1.0f);
    FloatCurve_AddStop(&s_groundDustRingBrake, 0.24f, 0.10f);
    FloatCurve_AddStop(&s_groundDustRingBrake, 1.0f, 0.03f);
    s_groundDustRingReady = true;
}

void VFX_ComposeGroundDustRing(Vector3 pos, VC_MaterialId matId, float scale,
                               float severity01)
{
    GroundDustRing_Init();
    if (s_groundDustRingTex.id == 0 || scale <= 0.0f) return;
    severity01 = Clamp(severity01, 0.0f, 1.0f);
    const VFX_ElementMaterial *mat = VFX_Material(matId);
    const int count = GROUND_DUST_RING_MIN_PARTICLES +
        (int)(severity01 * (GROUND_DUST_RING_MAX_PARTICLES - GROUND_DUST_RING_MIN_PARTICLES));
    float ringRadius = Math_Mix(0.30f, 1.15f, severity01) * scale;
    for (int i = 0; i < count; ++i)
    {
        float angle = ((float)i + Random01() * 0.55f) / (float)count * 2.0f * PI;
        float radialJitter = Math_Mix(0.78f, 1.08f, Random01());
        float speed = Math_Mix(6.0f, 12.0f, Random01()) * scale;
        // A ground-plane card is foreshortened by the isometric camera. It
        // needs more carrier opacity than a camera-facing impact puff, while
        // remaining an alpha body rather than becoming emissive fog.
        const Color baseDust = {210, 202, 186, 112};
        Color dust = {(unsigned char)((baseDust.r * 9 + mat->body.r) / 10),
                      (unsigned char)((baseDust.g * 9 + mat->body.g) / 10),
                      (unsigned char)((baseDust.b * 9 + mat->body.b) / 10), 56};
        SpawnParticle((ParticleConfig){
            .position = {pos.x + cosf(angle) * ringRadius * radialJitter,
                         pos.y + 0.08f * scale,
                         pos.z + sinf(angle) * ringRadius * radialJitter},
            .velocity = {cosf(angle) * speed, Math_Mix(0.015f, 0.055f, Random01()) * scale,
                         sinf(angle) * speed},
            .radius = Math_Mix(0.32f, 0.55f, Random01()) * scale,
            .lifetime = Math_Mix(0.72f, 1.08f, Random01()),
            .colorStart = dust, .colorEnd = VC_WithAlpha(dust, 0),
            .radiusCurve = &s_groundDustRingGrow, .alphaCurve = &s_groundDustRingFade,
            .speedCurve = &s_groundDustRingBrake,
            .render.texture = s_groundDustRingTex, .render.blendMode = VFX_BLEND_ALPHA,
            .render.smokeSheet = 1, .render.normalTex = s_groundDustRingNormalTex,
            .spriteAnim = &s_groundDustRingAnim[i % GROUND_DUST_RING_ANIM_RATES],
            .spriteAnimPhase = Random01() * 0.16f, .rotation = Random01() * 2.0f * PI,
            .angularVelocity = (Random01() - 0.5f) * 0.10f,
            // The ring's placement and velocity remain in XZ. Camera-facing
            // cards preserve that soft particulate read on the current rlvk
            // path; flat cards can vanish edge-on against the terrain.
            // camera-facing cards preserve the ring under the current renderer
        });
    }
}
