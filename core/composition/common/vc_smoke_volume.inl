// vc_smoke_volume.inl — Primary VFX Smoke Volume (UE5 Niagara Architecture)
//
// Replaces monolithic single-sprite smoke with 4 specialized Niagara 8x8 flipbook styles:
//   1. ROIL:       Heavy thermal convection column (smoke_roil_8x8.png).
//   2. PUFF_DARK:  Dense black detonation smoke (smoke_puff_8x8.png).
//   3. PUFF_LIGHT: Soft light-grey hit / impact dust smoke (smoke_puff_light_8x8.png).
//   4. WISPY:      Dispersed drifting smoke wisps (smoke_wispy_8x8.png).
//
// Implements Morton-Taylor-Turner plume dynamics:
//   - Fast thermal buoyancy at emitter base, transitioning to ambient wind drift.
//   - Radial expansion (entrainment) as smoke billows rise.
//   - Authentic particle lighting with self-shadowing and volumetric scattering.

#include "core/tuning.h"
#include "core/particles/particle_system.h"
#include "core/force_field.h"
#include "core/utils_math.h"
#include "core/vfx_surface_registry.h"
#include "raymath.h"

#define SVOL_MAX_EMITTERS 16
#define SVOL_BODY_LIFE_MAX 2.20f
#define SVOL_BODY_PHASE_MAX 0.60f
#define SVOL_BODY_LIFE_AVG 1.65f
#define SVOL_MAX_LIVE_PER_EMITTER 8

static bool s_svolInit = false;

// Resolve each style lazily: a scene using one smoke vocabulary should not pay
// texture memory for the other three whole-puff sheets.
static Texture2D s_svolTextures[VFX_SMOKE_STYLE_COUNT] = {0};
static Texture2D s_svolNormalTextures[VFX_SMOKE_STYLE_COUNT] = {0};
static SpriteAnim s_svolAnims[VFX_SMOKE_STYLE_COUNT] = {0};
static bool s_svolStyleInit[VFX_SMOKE_STYLE_COUNT] = {0};
static const VFX_SurfaceId s_svolSurfaceIds[VFX_SMOKE_STYLE_COUNT] = {
    VFX_SURFACE_SMOKE_ROIL_NIAGARA,
    VFX_SURFACE_SMOKE_PUFF_DARK_NIAGARA,
    VFX_SURFACE_SMOKE_PUFF_LIGHT_NIAGARA,
    VFX_SURFACE_SMOKE_WISPY_NIAGARA,
};

// Shared physics curves
static SkillCurve s_svolGrow = {0};
static SkillCurve s_svolFade = {0};
static SkillCurve s_svolRise = {0};
static ForceField s_svolFld = {0};

// Tunables
static float s_svolCountMul = 1.0f;
static float s_svolSizeMul  = 1.15f;
static float s_svolAlpha    = 0.55f;
static float s_svolBuoyancy = 1.35f;
static float s_svolDrag     = 1.20f;

typedef struct {
    bool active;
    bool stopping;
    Vector3 pos;
    Vector3 wind;
    float scale;
    float density;
    float accum;
    float elapsed;
    float seed;
    float legacyFeedAge;
    unsigned int generation;
    VFX_SmokeStyle style;
    ForceField fld;
} VC_SmokeVolumeEmitter;

static VC_SmokeVolumeEmitter s_svolEmitters[SVOL_MAX_EMITTERS];
static int s_svolNextEmitter = 0;
static unsigned int s_svolNextGeneration = 1;

static int SVol_StyleIndex(VFX_SmokeStyle style)
{
    return (style >= VFX_SMOKE_STYLE_ROIL && style < VFX_SMOKE_STYLE_COUNT)
        ? (int)style : (int)VFX_SMOKE_STYLE_ROIL;
}

static bool SVol_ResolveStyle(int styleIndex, Texture2D *texture,
                              Texture2D *normalTexture, SpriteAnim **anim)
{
    if (!s_svolStyleInit[styleIndex])
    {
        const VFX_SurfaceProfile *profile =
            VFX_SurfaceRegistry_Get(s_svolSurfaceIds[styleIndex]);
        if (profile != NULL && profile->body.id != 0)
        {
            s_svolTextures[styleIndex] = profile->body;
            s_svolNormalTextures[styleIndex] = profile->normalMap;
            SpriteAnim_Init(&s_svolAnims[styleIndex],
                            profile->flipbookColumns,
                            profile->flipbookRows,
                            profile->flipbookFrames,
                            (float)profile->flipbookFrames /
                                (SVOL_BODY_LIFE_MAX + SVOL_BODY_PHASE_MAX),
                            ANIM_ONCE);
        }
        s_svolStyleInit[styleIndex] = true;
    }
    *texture = s_svolTextures[styleIndex];
    *normalTexture = s_svolNormalTextures[styleIndex];
    *anim = &s_svolAnims[styleIndex];
    return texture->id != 0;
}

static void SVol_UpdateEmitterForce(VC_SmokeVolumeEmitter *e)
{
    ForceField *f = &e->fld;
    f->layerCount = 0;
    // Layer 0: Buoyancy (upward thermal lift)
    ForceField_AddLayer(f, (ForceLayer){
        .type = FORCE_GRAVITY_DIR,
        .direction = {0.0f, 1.0f, 0.0f},
        .strength = s_svolBuoyancy,
    });
    // Layer 1: Atmospheric drag (slows upward velocity as smoke cools)
    ForceField_AddLayer(f, (ForceLayer){
        .type = FORCE_DRAG,
        .strength = s_svolDrag,
    });
    // Layer 2: Convective curl turbulence
    ForceField_AddLayer(f, (ForceLayer){
        .type = FORCE_NOISE_CURL,
        .strength = 0.65f,
        .noiseScale = 1.4f / (e->scale > 0.1f ? e->scale : 1.0f),
        .noiseSpeed = 0.85f,
    });
    // Layer 3: Ambient wind drift
    float windLen = Vector3Length(e->wind);
    if (windLen > 0.05f)
    {
        Vector3 windNorm = Vector3Scale(e->wind, 1.0f / windLen);
        ForceField_AddLayer(f, (ForceLayer){
            .type = FORCE_WIND,
            .direction = windNorm,
            .strength = windLen * 1.5f,
        });
    }
}

static void SVol_InitShared(void)
{
    if (s_svolInit)
        return;

    Tuning_RegisterFloat("smoke_vol_count_mul", &s_svolCountMul, 1.0f);
    Tuning_RegisterFloat("smoke_vol_size_mul", &s_svolSizeMul, 1.15f);
    Tuning_RegisterFloat("smoke_vol_alpha", &s_svolAlpha, 0.55f);
    Tuning_RegisterFloat("smoke_vol_buoyancy", &s_svolBuoyancy, 1.35f);
    Tuning_RegisterFloat("smoke_vol_drag", &s_svolDrag, 1.20f);

    // 1. Growth Curve (Morton-Taylor-Turner radial expansion with height)
    FloatCurve_AddStop(&s_svolGrow, 0.00f, 0.40f);
    FloatCurve_AddStop(&s_svolGrow, 0.25f, 0.85f);
    FloatCurve_AddStop(&s_svolGrow, 0.65f, 1.30f);
    FloatCurve_AddStop(&s_svolGrow, 1.00f, 1.80f);

    // 2. Alpha Optical Thickness Curve
    FloatCurve_AddStop(&s_svolFade, 0.00f, 0.00f);
    FloatCurve_AddStop(&s_svolFade, 0.10f, 1.00f);
    FloatCurve_AddStop(&s_svolFade, 0.60f, 0.70f);
    FloatCurve_AddStop(&s_svolFade, 1.00f, 0.00f);

    // 3. Rise Velocity Curve (fast at hot base, decelerating higher up)
    FloatCurve_AddStop(&s_svolRise, 0.00f, 1.40f);
    FloatCurve_AddStop(&s_svolRise, 0.35f, 0.95f);
    FloatCurve_AddStop(&s_svolRise, 1.00f, 0.30f);

    // 4. Default Shared ForceField
    ForceField_AddLayer(&s_svolFld, (ForceLayer){
        .type = FORCE_GRAVITY_DIR,
        .direction = {0.0f, 1.0f, 0.0f},
        .strength = s_svolBuoyancy,
    });
    ForceField_AddLayer(&s_svolFld, (ForceLayer){
        .type = FORCE_DRAG,
        .strength = s_svolDrag,
    });
    ForceField_AddLayer(&s_svolFld, (ForceLayer){
        .type = FORCE_NOISE_CURL,
        .strength = 0.65f,
        .noiseScale = 1.4f,
        .noiseSpeed = 0.85f,
    });

    s_svolInit = true;
}

static void SVol_Emit(VC_SmokeVolumeEmitter *e, float dt)
{
    SVol_InitShared();

    if (e->density <= 0.0f)
        return;

    // Pick texture and anim based on requested style
    const int styleIndex = SVol_StyleIndex(e->style);
    Texture2D tex = {0};
    Texture2D normalTex = {0};
    SpriteAnim *anim = NULL;
    if (!SVol_ResolveStyle(styleIndex, &tex, &normalTex, &anim))
        return;
    Color tint = (Color){220, 224, 230, 255}; // neutral white smoke
    float baseSize = 0.55f;

    switch (e->style)
    {
    case VFX_SMOKE_STYLE_PUFF_DARK:
        tint = (Color){52, 48, 46, 255};
        baseSize = 0.48f;
        break;
    case VFX_SMOKE_STYLE_PUFF_LIGHT:
        tint = (Color){185, 180, 175, 255}; // light stone dust / steam
        baseSize = 0.42f;
        break;
    case VFX_SMOKE_STYLE_WISPY:
        tint = (Color){150, 145, 142, 255};
        baseSize = 0.65f;
        break;
    case VFX_SMOKE_STYLE_ROIL:
    default:
        // The primary/default vocabulary is white smoke. Keep soot as the
        // explicit PUFF_DARK style rather than making every volume black.
        tint = (Color){220, 224, 230, 255};
        baseSize = 0.55f;
        break;
    }

    const ForceField *activeField = (e->fld.layerCount > 0) ? &e->fld : &s_svolFld;
    const float scale = e->scale > 0.01f ? e->scale : 1.0f;
    float targetLive = (float)SVOL_MAX_LIVE_PER_EMITTER * e->density * s_svolCountMul;
    targetLive = fminf(targetLive, (float)SVOL_MAX_LIVE_PER_EMITTER);
    e->accum += dt * (targetLive / SVOL_BODY_LIFE_AVG);
    int n = (int)e->accum;
    e->accum -= (float)n;
    if (n > 12) { n = 12; e->accum = 0.0f; }
    if (n < 0) n = 0;

    for (int i = 0; i < n; i++)
    {
        float ang = Random01() * 2.0f * PI;
        float rad = sqrtf(Random01()) * 0.15f * scale;
        Vector3 p = {
            e->pos.x + cosf(ang) * rad,
            e->pos.y + Random01() * 0.08f * scale,
            e->pos.z + sinf(ang) * rad
        };

        float life = Math_Mix(1.10f, SVOL_BODY_LIFE_MAX, Random01());
        Color colStart = VC_WithAlpha(tint, (unsigned char)(255.0f * s_svolAlpha * (e->style == VFX_SMOKE_STYLE_WISPY ? 0.45f : 0.85f)));
        Color colEnd = VC_WithAlpha(tint, 0);

        SpawnParticle((ParticleConfig){
            .position = p,
            .velocity = {
                cosf(ang) * 0.08f * scale,
                Math_Mix(0.40f, 0.85f, Random01()) * scale,
                sinf(ang) * 0.08f * scale
            },
            .radius = Math_Mix(0.30f, 0.65f, powf(Random01(), 1.3f)) * baseSize * s_svolSizeMul * scale,
            .lifetime = life,
            .colorStart = colStart,
            .colorEnd = colEnd,
            .alphaCurve = &s_svolFade,
            .speedCurve = &s_svolRise,
            .radiusCurve = &s_svolGrow,
            .forceField = activeField,
            .windInfluence = 0.80f,
            .render.texture = tex,
            .render.blendMode = VFX_BLEND_ALPHA,
            .render.smokeSheet = 1,
            .render.normalTex = normalTex,
            .spriteAnim = anim,
            .spriteAnimPhase = Random01() * SVOL_BODY_PHASE_MAX,
            .spriteAnimRate = Math_Mix(0.85f, 1.0f, Random01()),
            .spriteFlipX = Random01() < 0.5f,
            .spriteFlipY = false,
            .rotation = (Random01() - 0.5f) * 0.40f,
            .angularVelocity = (Random01() - 0.5f) * 0.25f,
            .followTarget = &e->pos,
            .followTargetGeneration = &e->generation,
            .followGeneration = e->generation,
            .followStrength = 0.60f,
        });
    }
}

// ── Public APIs ─────────────────────────────────────────────────────────────

int VFX_SmokeVolumeEmitter_Spawn(Vector3 pos, float scale, float density, VFX_SmokeStyle style)
{
    SVol_InitShared();
    int slot = -1;
    for (int i = 0; i < SVOL_MAX_EMITTERS; ++i)
        if (!s_svolEmitters[i].active) { slot = i; break; }
    if (slot < 0) slot = s_svolNextEmitter++ % SVOL_MAX_EMITTERS;

    unsigned int generation = s_svolNextGeneration++;
    if (generation == 0) generation = s_svolNextGeneration++;

    s_svolEmitters[slot] = (VC_SmokeVolumeEmitter){
        .active = true,
        .pos = pos,
        .scale = scale > 0.0f ? scale : 1.0f,
        .density = density < 0.0f ? 0.0f : (density > 1.0f ? 1.0f : density),
        .legacyFeedAge = -1.0f,
        .seed = (float)slot * 1.6180339f + pos.x * 0.37f + pos.z * 0.71f,
        .generation = generation,
        .style = style,
    };
    SVol_UpdateEmitterForce(&s_svolEmitters[slot]);
    return slot;
}

void VFX_ComposeSmokeVolume(Vector3 pos, float scale, float density, VFX_SmokeStyle style)
{
    static int legacyHandle = -1;
    if (legacyHandle < 0 || !s_svolEmitters[legacyHandle].active)
        legacyHandle = VFX_SmokeVolumeEmitter_Spawn(pos, scale, density, style);

    s_svolEmitters[legacyHandle].legacyFeedAge = 0.0f;
    s_svolEmitters[legacyHandle].pos = pos;
    s_svolEmitters[legacyHandle].scale = scale;
    s_svolEmitters[legacyHandle].density = density;
    s_svolEmitters[legacyHandle].style = style;
}

void VFX_SmokeVolumeEmitter_SetTransform(int handle, Vector3 pos, Vector3 wind)
{
    if (handle < 0 || handle >= SVOL_MAX_EMITTERS || !s_svolEmitters[handle].active) return;
    s_svolEmitters[handle].pos = pos;
    s_svolEmitters[handle].wind = wind;
    SVol_UpdateEmitterForce(&s_svolEmitters[handle]);
}

void VFX_SmokeVolumeEmitter_SetDensity(int handle, float density01)
{
    if (handle < 0 || handle >= SVOL_MAX_EMITTERS || !s_svolEmitters[handle].active) return;
    s_svolEmitters[handle].density = density01 < 0.0f ? 0.0f : (density01 > 1.0f ? 1.0f : density01);
}

void VFX_SmokeVolumeEmitter_Stop(int handle)
{
    if (handle >= 0 && handle < SVOL_MAX_EMITTERS)
        s_svolEmitters[handle].stopping = true;
}

void VFX_KillSmokeVolumeEmitter(int handle)
{
    if (handle >= 0 && handle < SVOL_MAX_EMITTERS)
        s_svolEmitters[handle].active = false;
}

static void VC_SmokeVolumeEmitter_Update(float dt)
{
    for (int i = 0; i < SVOL_MAX_EMITTERS; ++i)
    {
        VC_SmokeVolumeEmitter *e = &s_svolEmitters[i];
        if (!e->active) continue;

        if (e->legacyFeedAge >= 0.0f)
        {
            e->legacyFeedAge += dt;
            if (e->legacyFeedAge > 0.25f)
            {
                e->active = false;
                continue;
            }
        }

        if (e->stopping)
        {
            e->density -= dt * 1.5f;
            if (e->density <= 0.01f)
            {
                e->active = false;
                continue;
            }
        }

        e->elapsed += dt;
        SVol_Emit(e, dt);
    }
}
