// vc_muzzle_flash.inl — direction-bearing one-shot muzzle-flash primary.
//
// The extracted sheets are VARIANT atlases, not animations. Each shot selects
// one cell and holds it for its sub-frame lifetime. The side layer is a real
// authored-axis cross: atlas +V follows the barrel without borrowing velocity,
// so a stationary flash cannot accidentally move just to acquire orientation.

#include "core/particles/particle_system.h"
#include "core/vfx_light.h"
#include "core/vfx_surface_registry.h"
#include "raymath.h"

static bool s_muzzleInit = false;
static Texture2D s_muzzleFrontTexture = {0};
static Texture2D s_muzzleSideTexture = {0};
static Texture2D s_muzzleSphereTexture = {0};
static Texture2D s_muzzleSmokeTexture = {0};
static Texture2D s_muzzleSmokeNormalTexture = {0};
static SpriteAnim s_muzzleFrontAnim = {0};
static SpriteAnim s_muzzleSideAnim = {0};
static SpriteAnim s_muzzleSphereAnim = {0};
static SpriteAnim s_muzzleSmokeAnim = {0};
static SkillCurve s_muzzleSmokeGrow = {0};
static SkillCurve s_muzzleSmokeFade = {0};

static Texture2D MuzzleFlash_ResolveBody(VFX_SurfaceId id)
{
    const VFX_SurfaceProfile *profile = VFX_SurfaceRegistry_Get(id);
    return (profile != NULL) ? profile->body : (Texture2D){0};
}

static void MuzzleFlash_InitShared(void)
{
    if (s_muzzleInit) return;
    s_muzzleInit = true;

    s_muzzleFrontTexture = MuzzleFlash_ResolveBody(
        VFX_SURFACE_MUZZLE_FLASH_FRONT_NIAGARA);
    s_muzzleSideTexture = MuzzleFlash_ResolveBody(
        VFX_SURFACE_MUZZLE_FLASH_SIDE_NIAGARA);
    s_muzzleSphereTexture = MuzzleFlash_ResolveBody(
        VFX_SURFACE_MUZZLE_FLASH_SPHERE_NIAGARA);
    const VFX_SurfaceProfile *smoke = VFX_SurfaceRegistry_Get(
        VFX_SURFACE_SMOKE_PUFF_LIGHT_NIAGARA);
    if (smoke != NULL && smoke->body.id != 0) {
        s_muzzleSmokeTexture = smoke->body;
        s_muzzleSmokeNormalTexture = smoke->normalMap;
        SpriteAnim_Init(&s_muzzleSmokeAnim, smoke->flipbookColumns,
                        smoke->flipbookRows, smoke->flipbookFrames,
                        (float)smoke->flipbookFrames / 0.78f, ANIM_ONCE);
    }

    // SpriteAnim_Init takes (rows, cols). The extracted horizontal variants
    // are therefore 1x4 and 1x2; transposing those arguments samples strips
    // across multiple cells rather than one complete flame.
    SpriteAnim_Init(&s_muzzleFrontAnim, 1, 4, 4, 1.0f, ANIM_ONCE);
    SpriteAnim_Init(&s_muzzleSideAnim, 1, 2, 2, 1.0f, ANIM_ONCE);
    SpriteAnim_Init(&s_muzzleSphereAnim, 2, 2, 4, 1.0f, ANIM_ONCE);
    FloatCurve_AddStop(&s_muzzleSmokeGrow, 0.0f, 0.72f);
    FloatCurve_AddStop(&s_muzzleSmokeGrow, 0.28f, 1.05f);
    FloatCurve_AddStop(&s_muzzleSmokeGrow, 1.0f, 1.35f);
    FloatCurve_AddStop(&s_muzzleSmokeFade, 0.0f, 0.0f);
    FloatCurve_AddStop(&s_muzzleSmokeFade, 0.12f, 0.68f);
    FloatCurve_AddStop(&s_muzzleSmokeFade, 1.0f, 0.0f);
}

void VFX_ComposeMuzzleSmoke(Vector3 muzzlePos, Vector3 forward,
                            float scale, float density01)
{
    if (scale <= 0.0f || density01 <= 0.0f) return;
    if (density01 > 1.0f) density01 = 1.0f;
    MuzzleFlash_InitShared();
    if (s_muzzleSmokeTexture.id == 0) return;
    Vector3 axis = Vector3Normalize(forward);
    if (Vector3LengthSqr(axis) < 1e-8f) axis = (Vector3){0.0f, 0.0f, 1.0f};
    for (int i = 0; i < 2; ++i) {
        float spread = (i == 0) ? -0.07f : 0.07f;
        Color smoke = {218, 220, 224, (unsigned char)(64.0f * density01)};
        SpawnParticle((ParticleConfig){
            .position = Vector3Add(muzzlePos, Vector3Scale(axis,
                         (0.11f + 0.08f * (float)i) * scale)),
            .velocity = {axis.x * (0.55f + 0.18f * (float)i) * scale + spread,
                         (0.12f + 0.05f * (float)i) * scale,
                         axis.z * (0.55f + 0.18f * (float)i) * scale - spread},
            .radius = (0.17f + 0.05f * (float)i) * scale,
            .lifetime = 0.68f + 0.10f * (float)i,
            .colorStart = smoke, .colorEnd = VC_WithAlpha(smoke, 0),
            .radiusCurve = &s_muzzleSmokeGrow,
            .alphaCurve = &s_muzzleSmokeFade,
            .spriteAnim = &s_muzzleSmokeAnim,
            .spriteAnimPhase = 0.04f + 0.10f * (float)i,
            .spriteAnimRate = 0.84f - 0.08f * (float)i,
            .rotation = (float)GetRandomValue(0, 359) * DEG2RAD,
            .render.texture = s_muzzleSmokeTexture,
            .render.blendMode = VFX_BLEND_ALPHA,
            .render.smokeSheet = 1,
            .render.normalTex = s_muzzleSmokeNormalTexture,
        });
    }
}

void VFX_ComposeMuzzleFlash(Vector3 muzzlePos, Vector3 forward,
                            VC_MaterialId matId, float scale,
                            float intensity01)
{
    if (scale <= 0.0f) scale = 1.0f;
    if (intensity01 < 0.0f) intensity01 = 0.0f;
    if (intensity01 > 1.0f) intensity01 = 1.0f;
    if (intensity01 <= 0.0f) return;

    MuzzleFlash_InitShared();
    const VFX_ElementMaterial *material = VFX_Material(matId);
    Vector3 axis = Vector3Normalize(forward);
    if (Vector3LengthSqr(axis) < 1e-8f) axis = (Vector3){0.0f, 0.0f, 1.0f};

    const unsigned char alpha = (unsigned char)(255.0f * intensity01);
    const Color hot = VC_WithAlpha(VC_Whiten(material->glow, 0.78f), alpha);
    const Color warm = VC_WithAlpha(material->glow, alpha);
    const Vector3 bodyCenter = Vector3Add(
        muzzlePos, Vector3Scale(axis, 0.46f * scale));

    // Directional body: two orthogonal sheets containing the barrel axis.
    SpawnParticle((ParticleConfig){
        .position = bodyCenter,
        .radius = 0.22f * scale,
        .lifetime = 0.055f,
        .colorStart = warm,
        .colorEnd = VC_WithAlpha(material->body, 0),
        .spriteAnim = &s_muzzleSideAnim,
        .spriteAnimPhase = (float)GetRandomValue(0, 1),
        .spriteAnimRate = 0.001f,
        .render.texture = s_muzzleSideTexture,
        .render.appearance = VFX_APPEARANCE_GLOW,
        .render.facingMode = VFX_FACING_CROSS_BILLBOARD,
        .render.facingDirection = axis,
        .render.facingAspect = 2.0f,
    });

    // Camera fill prevents the cross from collapsing when the barrel points at
    // the viewer. Per-cell aspect is 512:1024, so +V remains twice +U.
    SpawnParticle((ParticleConfig){
        .position = bodyCenter,
        .radius = 0.20f * scale,
        .lifetime = 0.050f,
        .colorStart = hot,
        .colorEnd = VC_WithAlpha(material->glow, 0),
        .spriteAnim = &s_muzzleFrontAnim,
        .spriteAnimPhase = (float)GetRandomValue(0, 3),
        .spriteAnimRate = 0.001f,
        .rotation = (float)GetRandomValue(-12, 12) * DEG2RAD,
        .render.texture = s_muzzleFrontTexture,
        .render.appearance = VFX_APPEARANCE_GLOW,
        .render.facingAspect = 2.0f,
    });

    // Compact pressure sphere anchors the flame at the muzzle and hides the
    // crossed planes' shared seam.
    SpawnParticle((ParticleConfig){
        .position = Vector3Add(muzzlePos, Vector3Scale(axis, 0.10f * scale)),
        .radius = 0.26f * scale,
        .lifetime = 0.040f,
        .colorStart = hot,
        .colorEnd = VC_WithAlpha(material->glow, 0),
        .spriteAnim = &s_muzzleSphereAnim,
        .spriteAnimPhase = (float)GetRandomValue(0, 3),
        .spriteAnimRate = 0.001f,
        .rotation = (float)GetRandomValue(0, 359) * DEG2RAD,
        .render.texture = s_muzzleSphereTexture,
        .render.appearance = VFX_APPEARANCE_GLOW,
    });

    VFXLight_Spawn(Vector3Add(muzzlePos, Vector3Scale(axis, 0.22f * scale)),
                   material->soft, 1.35f * scale * intensity01, 0.045f,
                   VFX_PRIORITY_LOW);
}
