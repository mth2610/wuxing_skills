// Thủy Kính Hộ Thuẫn (Water Mirror Ward) — self-cast Water buff/shield.
// Droplets gather at the caster, then a living water dome blooms up with a
// splash ring that shoves enemies back. While the ward holds: flowing
// caustic shader on the dome, three tilted water ribbons orbiting it,
// mist swirling inside, a wet pool under it, and a speed blessing on
// everyone sheltered within. When it expires the dome dissolves into rain.
#include "skills/water/thuy_kinh_skill/thuy_kinh_skill.h"
#include "core/particle_system.h"
#include "core/trail_system.h"
#include "core/force_field.h"
#include "core/color_gradient.h"
#include "core/skill_helper.h"
#include "core/skill_curve.h"
#include "core/skill_manager.h"
#include "core/procedural_mesh_utils.h"
#include "core/resource_manager.h"
#include "core/decal_system.h"
#include "core/vfx_light.h"
#include "core/screen_distort.h"
#include "core/camera_fx.h"
#include "core/utils_math.h"
#include "environment/environment_system.h"
#include "entities/entities.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef PI
#define PI 3.1415926535f
#endif

#define MAX_WARDS        4
#define RIBBONS_PER_WARD 3

#define GATHER_TIME      0.4f
#define BLOOM_TIME       0.5f
#define ACTIVE_TIME      6.0f
#define DISSOLVE_TIME    0.8f
#define MIST_RATE        12.0f   // mist particles per second per ward (unitless rate)
#define DRIP_INTERVAL    0.22f   // droplet cadence per orbiting ribbon (s)
#define BUFF_SPEED_MULT  1.35f

// Pass 1 — meter-rescaled tunables (÷100 from old 1cm-scale values).
// Loaded from .tuning file on Init, so all defaults below are the canonical
// source of truth — do NOT duplicate them inside the .inl file.
static float s_shakeEnable        = 1.0f;    // 1=on, 0=off
static float s_shieldRadius       = 0.55f;   // dome radius (m)

// Dissolve rain burst
static float s_rainVelOutward     = 0.22f;   // dissolve burst outward XZ speed (m/s)
static float s_rainVelUp          = 0.08f;   // dissolve burst upward speed (m/s)
static float s_rainRadiusMin      = 0.012f;  // dissolve droplet min radius (m)
static float s_rainRadiusMax      = 0.026f;  // dissolve droplet max radius (m)

// Gather phase
static float s_gatherParticleYMin = 0.02f;   // gather particle spawn Y min (m)
static float s_gatherParticleYMax = 0.30f;   // gather particle spawn Y max (m)
static float s_gatherTargetY      = 0.18f;   // gather convergence Y (m)
static float s_gatherSpeed        = 1.30f;   // gather particle inward speed (m/s)
static float s_gatherRadiusMin    = 0.010f;  // gather particle min radius (m)
static float s_gatherRadiusMax    = 0.022f;  // gather particle max radius (m)

// Cast VFX
static float s_castLightY         = 0.20f;   // VFXLight spawn Y (m)
static float s_castLightRadius    = 0.70f;   // VFXLight radius at cast (m)

// Ribbon
static float s_ribbonLen          = 0.026f;  // trail length parameter (m)
static float s_ribbonThick        = 0.012f;  // trail thickness (m)

// Bloom burst
static float s_bloomCrownY        = 0.03f;   // crown splash spawn Y (m)
static float s_bloomVelOutward    = 0.55f;   // crown splash outward XZ speed (m/s)
static float s_bloomVelUpMin      = 0.70f;   // crown splash Y vel min (m/s)
static float s_bloomVelUpMax      = 1.30f;   // crown splash Y vel max (m/s)
static float s_bloomCrownRadiusMin= 0.015f;  // crown splash droplet min radius (m)
static float s_bloomCrownRadiusMax= 0.032f;  // crown splash droplet max radius (m)
static float s_bloomLightRadius   = 1.30f;   // active ward VFXLight radius (m)
static float s_bloomSurgeKnockback= 1.10f;   // protective surge knockback impulse (m/s)

// Ribbon drip
static float s_dripVelXZ          = 0.15f;   // drip lateral velocity half-range (m/s)
static float s_dripVelY           = -0.10f;  // drip downward velocity (m/s)
static float s_dripRadiusMin      = 0.010f;  // drip droplet min radius (m)
static float s_dripRadiusMax      = 0.020f;  // drip droplet max radius (m)
static float s_ribbonFloorY       = 0.04f;   // ribbon tip Y floor, keep above ground (m)

// Mist
static float s_mistYMin           = 0.02f;   // mist spawn Y min (m)
static float s_mistYMax           = 0.12f;   // mist spawn Y max (m)
static float s_mistVelXZ          = 0.18f;   // mist tangential speed (m/s)
static float s_mistVelYMin        = 0.24f;   // mist upward speed min (m/s)
static float s_mistVelYMax        = 0.50f;   // mist upward speed max (m/s)
static float s_mistRadiusMin      = 0.016f;  // mist particle min radius (m)
static float s_mistRadiusMax      = 0.034f;  // mist particle max radius (m)

// Force fields
static float s_rainGravity        = 4.20f;   // dissolve-rain downward gravity (m/s²)
static float s_swirlVortex        = 2.20f;   // mist vortex strength (m/s²)
static float s_swirlPull          = 0.28f;   // mist central pull strength (m/s²)
static float s_swirlDrag          = 0.30f;   // mist drag coefficient (unitless)

// Per-phase over-lifetime curves — flat 1.0 until shaped in sandbox
static SkillCurve s_gatherRadiusCurve, s_gatherSpeedCurve, s_gatherAlphaCurve;
static SkillCurve s_gatherEmissiveCurve;
static SkillCurve s_mistRadiusCurve,   s_mistSpeedCurve,   s_mistAlphaCurve;
static SkillCurve s_mistEmissiveCurve;
static SkillCurve s_dissolveRadiusCurve, s_dissolveSpeedCurve, s_dissolveAlphaCurve;
static SkillCurve s_dissolveEmissiveCurve;

// Per-phase extra force mixes (sandbox-tunable)
static SkillForceMix s_gatherForce;
static ForceField    s_gatherFieldActive;
static SkillForceMix s_mistForce;
static ForceField    s_mistFieldActive;
static SkillForceMix s_dissolveForce;
static ForceField    s_dissolveFieldActive;

// dome(1) + gather(6+3+1+29=39) + cast(2) + ribbon(3) + drip(4) + bloom(8)
// + mist(7+3+1+29=40) + swirl(3) + dissolve(5+3+1+29=38) = 138
#define THUY_KINH_TUNABLE_COUNT 139

typedef enum {
    WARD_INACTIVE = 0,
    WARD_GATHER,
    WARD_BLOOM,
    WARD_ACTIVE,
    WARD_DISSOLVE
} WardState;

typedef struct {
    WardState state;
    float   timer;
    float   scale;
    float   radius;       // full dome radius
    float   growth;       // 0..1 bloom progress (holds 1.0 while active)
    float   dissolve;     // 0..1 shader dissolve
    Vector3 center;       // ground point under the caster
    int     ownerAgentId; // CORE_ISSUES.md Item 15

    // Orbiting water ribbons — tilted circular orbits (§12.3: every ribbon
    // gets its own plane, direction, speed and phase).
    int     ribbonIds[RIBBONS_PER_WARD];
    Vector3 orbitU[RIBBONS_PER_WARD], orbitV[RIBBONS_PER_WARD];
    float   orbitAngle[RIBBONS_PER_WARD];
    float   orbitSpeed[RIBBONS_PER_WARD]; // signed rad/s
    float   bobPhase[RIBBONS_PER_WARD];
    float   dripTimer[RIBBONS_PER_WARD];

    float   mistAccum;
    float   ringTimer;   // cadence of the soft additive ground-ring decals
    ForceField swirl; // vortex + gentle pull that curls mist along the dome
} WardInstance;

static WardInstance  s_wards[MAX_WARDS];
static ColorGradient s_mistGrad;
static ColorGradient s_ribbonGrad;
static ColorGradient s_rainGrad;
static ForceField    s_rainFall;   // shared: dissolve-rain falls under gravity
static Shader        s_shader;
static Texture2D     s_flowTex;     // RG flow directions driving the caustics
static Texture2D     s_causticsTex;
static Texture2D     s_ringTex;     // generic impact ring for the base glow
static int           s_dissolveLoc = -1;
static int           s_lightDirLoc = -1;
static int           s_flowTexLoc  = -1;
static int           s_causticsLoc = -1;
static int           s_skillIndex  = -1;

static float DomeCenterY(const WardInstance *w) { return w->radius * 0.55f; }

static void RebuildRainFall(void)
{
    ForceField_Clear(&s_rainFall);
    ForceField_AddLayer(&s_rainFall, (ForceLayer){
        .type      = FORCE_GRAVITY_DIR,
        .direction = { 0.0f, -1.0f, 0.0f },
        .strength  = s_rainGravity
    });
}

static void WardStartDissolve(WardInstance *w)
{
    if (w->state != WARD_BLOOM && w->state != WARD_ACTIVE) return;
    w->state = WARD_DISSOLVE;
    w->timer = 0.0f;

    for (int r = 0; r < RIBBONS_PER_WARD; r++) {
        if (w->ribbonIds[r] >= 0) {
            KillTrail(w->ribbonIds[r]);
            w->ribbonIds[r] = -1;
        }
    }

    // The dome collapses into rain: one burst of droplets released from
    // random points on the shell, falling under the shared gravity field.
    Vector3 domeC = { w->center.x, DomeCenterY(w), w->center.z };
    for (int i = 0; i < 34; i++) {
        float a = (float)GetRandomValue(0, 3600) / 10.0f * DEG2RAD;
        float p = (float)GetRandomValue(5, 90) / 100.0f * PI; // skip the buried cap
        Vector3 n = { sinf(p) * cosf(a), cosf(p), sinf(p) * sinf(a) };

        // Pass 4 — distance-proportional radius: floor+cap
        float baseRad = fminf(fmaxf(w->radius * 0.18f, s_rainRadiusMin), s_rainRadiusMax);

        // Rebuild dissolve force field from tunables + base gravity
        ForceField_Clear(&s_dissolveFieldActive);
        ForceField_AddLayer(&s_dissolveFieldActive, (ForceLayer){
            .type      = FORCE_GRAVITY_DIR,
            .direction = { 0.0f, -1.0f, 0.0f },
            .strength  = s_rainGravity
        });
        SkillForceMix_AddLayers(&s_dissolveForce, &s_dissolveFieldActive);

        SpawnParticle((ParticleConfig){
            .position      = Vector3Add(domeC, Vector3Scale(n, w->radius)),
            .velocity      = { n.x * s_rainVelOutward, s_rainVelUp, n.z * s_rainVelOutward },
            .radius        = baseRad * w->scale,
            .lifetime      = 1.1f,
            .gradient      = &s_rainGrad,
            .forceField    = &s_dissolveFieldActive,
            .emissiveCurve = &s_dissolveEmissiveCurve
        });
    }
    SpawnGroundDecal(DECAL_PRESET_WATER_RIPPLE, w->center, w->radius * 0.9f, 1.4f);
}

static bool ThuyKinhSkill_HasActiveInstance(int agentId)
{
    for (int i = 0; i < MAX_WARDS; i++) {
        if (s_wards[i].state != WARD_INACTIVE && s_wards[i].ownerAgentId == agentId)
            return true;
    }
    return false;
}

static void ThuyKinhSkill_Abort(int agentId)
{
    for (int i = 0; i < MAX_WARDS; i++) {
        WardInstance *w = &s_wards[i];
        if (w->state == WARD_INACTIVE || w->ownerAgentId != agentId) continue;
        if (w->state == WARD_GATHER) { w->state = WARD_INACTIVE; continue; }
        WardStartDissolve(w);
    }
}

void InitThuyKinhSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth; (void)screenHeight;

    s_skillIndex = Skill_GetIndexByName("THUY_KINH");
    RegisterSkillLifecycleQuery(s_skillIndex, ThuyKinhSkill_HasActiveInstance);
    RegisterSkillAbort(s_skillIndex, ThuyKinhSkill_Abort);

    s_shader = ResourceManager_LoadShader(
        "skills/water/thuy_kinh_skill/thuy_kinh.vs",
        "skills/water/thuy_kinh_skill/thuy_kinh.fs");
    s_dissolveLoc = GetShaderLocation(s_shader, "u_dissolve");
    s_lightDirLoc = GetShaderLocation(s_shader, "u_lightDir");
    s_flowTexLoc  = GetShaderLocation(s_shader, "flowTex");
    s_causticsLoc = GetShaderLocation(s_shader, "causticsTex");

    s_flowTex     = ResourceManager_LoadTexture("assets/textures/water_flow.png");
    s_causticsTex = ResourceManager_LoadTexture("assets/textures/water_caustics.png");
    s_ringTex     = ResourceManager_LoadTexture("assets/textures/generic/impact_ring.png");
    // Caustics scroll across UV seams — must wrap, and bilinear keeps the
    // pattern soft instead of pixel-crunchy up close.
    SetTextureWrap(s_causticsTex, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(s_causticsTex, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(s_flowTex, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(s_flowTex, TEXTURE_FILTER_BILINEAR);

    for (int i = 0; i < MAX_WARDS; i++) {
        s_wards[i].state = WARD_INACTIVE;
        for (int r = 0; r < RIBBONS_PER_WARD; r++) s_wards[i].ribbonIds[r] = -1;
    }

    // Mist inside the dome: pale foam -> element blue -> fades out dark.
    ColorGradient_AddStop(&s_mistGrad, 0.00f, (Color){ 205, 240, 255, 170 });
    ColorGradient_AddStop(&s_mistGrad, 0.45f, ColorAlpha(ELEMENT_COLOR_WATER, 0.55f));
    ColorGradient_AddStop(&s_mistGrad, 1.00f, (Color){ 10, 30, 60, 0 });

    // Orbiting ribbons: thin energy stream — white-hot head (above the
    // bloom threshold so post-FX makes it glow) melting into element cyan.
    ColorGradient_AddStop(&s_ribbonGrad, 0.00f, (Color){ 245, 253, 255, 255 });
    ColorGradient_AddStop(&s_ribbonGrad, 0.25f, ColorAlpha(ColorLerp(ELEMENT_COLOR_WATER, WHITE, 0.6f), 0.9f));
    ColorGradient_AddStop(&s_ribbonGrad, 0.65f, ColorAlpha(ColorLerp(ELEMENT_COLOR_WATER, WHITE, 0.2f), 0.45f));
    ColorGradient_AddStop(&s_ribbonGrad, 1.00f, (Color){ 8, 28, 55, 0 });

    // Dissolve rain: element blue thinning to nothing on the way down.
    ColorGradient_AddStop(&s_rainGrad, 0.00f, ColorLerp(ELEMENT_COLOR_WATER, WHITE, 0.4f));
    ColorGradient_AddStop(&s_rainGrad, 0.55f, ColorAlpha(ELEMENT_COLOR_WATER, 0.65f));
    ColorGradient_AddStop(&s_rainGrad, 1.00f, (Color){ 12, 35, 70, 0 });

    RebuildRainFall();

    // Seed per-phase curves flat (no-op until shaped in sandbox)
    SkillCurve_SetConstant(&s_gatherRadiusCurve,   1.0f);
    SkillCurve_SetConstant(&s_gatherSpeedCurve,    1.0f);
    SkillCurve_SetConstant(&s_gatherAlphaCurve,    1.0f);
    SkillCurve_SetConstant(&s_gatherEmissiveCurve, 1.0f);
    SkillCurve_SetConstant(&s_mistRadiusCurve,     1.0f);
    SkillCurve_SetConstant(&s_mistSpeedCurve,      1.0f);
    SkillCurve_SetConstant(&s_mistAlphaCurve,      1.0f);
    SkillCurve_SetConstant(&s_mistEmissiveCurve,   1.0f);
    SkillCurve_SetConstant(&s_dissolveRadiusCurve, 1.0f);
    SkillCurve_SetConstant(&s_dissolveSpeedCurve,  1.0f);
    SkillCurve_SetConstant(&s_dissolveAlphaCurve,  1.0f);
    SkillCurve_SetConstant(&s_dissolveEmissiveCurve,1.0f);

    static SkillTunableEntry s_tunables[THUY_KINH_TUNABLE_COUNT];
    int tn = 0;
    #include "thuy_kinh_skill_tunables.inl"

    SkillTunables_LoadPersisted("skills/water/thuy_kinh_skill/thuy_kinh_skill.tuning",
                                s_tunables, tn);
    RegisterSkillTunables(s_skillIndex, s_tunables, tn);
}

void CastThuyKinhSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    (void)target; // self-cast: the ward is anchored on the caster

    if (!SkillManager_CanCast(s_skillIndex, agentId)) return;

    WardInstance *w = NULL;
    for (int i = 0; i < MAX_WARDS; i++) {
        if (s_wards[i].state == WARD_INACTIVE) { w = &s_wards[i]; break; }
    }
    if (!w) return;

    float scale = (params.sizeScale > 0.0f) ? params.sizeScale : 1.0f;

    w->state        = WARD_GATHER;
    w->timer        = 0.0f;
    w->scale        = scale;
    w->radius       = s_shieldRadius * scale
                    * ((float)GetRandomValue(95, 108) / 100.0f); // §12.3
    w->growth       = 0.0f;
    w->dissolve     = 0.0f;
    w->center       = (Vector3){ startPos.x, 0.0f, startPos.z };
    w->ownerAgentId = agentId;
    w->mistAccum    = 0.0f;
    w->ringTimer    = 0.0f;

    for (int r = 0; r < RIBBONS_PER_WARD; r++) {
        w->ribbonIds[r] = -1;
        w->dripTimer[r] = (float)GetRandomValue(0, 20) / 100.0f;
        w->bobPhase[r]  = (float)GetRandomValue(0, 628) / 100.0f;
        w->orbitAngle[r] = (float)r / RIBBONS_PER_WARD * 2.0f * PI
                         + (float)GetRandomValue(-40, 40) * DEG2RAD;
        w->orbitSpeed[r] = (2.1f + 0.35f * r) * ((r % 2 == 0) ? 1.0f : -1.0f);

        // Tilted orbit plane: normal leaned 18–38° off vertical, random yaw.
        float yaw  = (float)GetRandomValue(0, 3600) / 10.0f * DEG2RAD;
        float tilt = (float)GetRandomValue(18, 38) * DEG2RAD;
        Vector3 n  = { sinf(tilt) * cosf(yaw), cosf(tilt), sinf(tilt) * sinf(yaw) };
        w->orbitU[r] = Vector3Normalize(Vector3CrossProduct(n, (Vector3){ 0.0f, 0.0f, 1.0f }));
        w->orbitV[r] = Vector3CrossProduct(n, w->orbitU[r]);
    }

    // Mist swirl: slow vortex around the dome axis + a gentle pull toward
    // the dome heart + drag, so mist curls along the inner wall.
    ForceField_Clear(&w->swirl);
    ForceField_AddLayer(&w->swirl, (ForceLayer){
        .type = FORCE_VORTEX,
        .origin = { w->center.x, 0.0f, w->center.z },
        .direction = { 0.0f, 1.0f, 0.0f },
        .strength = s_swirlVortex, .radius = w->radius * 1.5f, .falloff = 0.0f
    });
    ForceField_AddLayer(&w->swirl, (ForceLayer){
        .type = FORCE_GRAVITY_POINT,
        .origin = { w->center.x, DomeCenterY(w), w->center.z },
        .strength = s_swirlPull, .radius = w->radius * 2.0f, .falloff = 1.0f
    });
    ForceField_AddLayer(&w->swirl, (ForceLayer){
        .type = FORCE_DRAG, .strength = s_swirlDrag
    });

    SpawnCastEffect(startPos, EFFECT_PRESET_WATER_SPLASH, scale * 0.7f);
    PlayCastSound(EFFECT_PRESET_WATER_SPLASH);
    VFXLight_Spawn((Vector3){ w->center.x, s_castLightY, w->center.z },
                   ELEMENT_COLOR_WATER, s_castLightRadius * scale, GATHER_TIME + BLOOM_TIME,
                   VFX_PRIORITY_LOW);

    SkillManager_TriggerCooldown(s_skillIndex, agentId,
                                 Skill_CalculateCooldown(SKILL_CAT_BUFF_SUPPORT, params));
}

static void SpawnRibbons(WardInstance *w)
{
    Vector3 domeC = { w->center.x, DomeCenterY(w), w->center.z };
    for (int r = 0; r < RIBBONS_PER_WARD; r++) {
        Vector3 offset = Vector3Add(
            Vector3Scale(w->orbitU[r], cosf(w->orbitAngle[r]) * w->radius),
            Vector3Scale(w->orbitV[r], sinf(w->orbitAngle[r]) * w->radius));

        TrailConfig cfg = { 0 };
        cfg.type        = TRAIL_TYPE_FOLLOWER;
        cfg.pos         = Vector3Add(domeC, offset);
        // No texture: a radial flare stretched along the ribbon reads as a
        // chain of smeared blobs. Thin + near-white so post-FX bloom catches
        // it and it reads as an energy stream, not a ribbon of cloth.
        cfg.len         = s_ribbonLen * w->scale;
        cfg.thick       = s_ribbonThick * w->scale;
        cfg.trailLength = 40.0f; // FOLLOWER: integer history node count
        cfg.life        = ACTIVE_TIME + 2.0f; // killed manually at dissolve
        cfg.scale       = w->scale;
        cfg.gradient    = &s_ribbonGrad;
        cfg.tint        = ELEMENT_COLOR_WATER;
        cfg.priority    = VFX_PRIORITY_LOW;
        w->ribbonIds[r] = SpawnTrailEntity(cfg);
    }
}

static void WardBloomBurst(WardInstance *w)
{
    Vector3 domeC = { w->center.x, DomeCenterY(w), w->center.z };

    PlayImpactSound(EFFECT_PRESET_WATER_SPLASH);
    SpawnGroundDecal(DECAL_PRESET_WATER_RIPPLE, w->center, w->radius * 1.4f, 2.2f);
    SpawnGroundDecal(DECAL_PRESET_WATER, w->center, w->radius * 0.55f,
                     ACTIVE_TIME + DISSOLVE_TIME); // wet pool under the ward

    // Pass 4 — distance-proportional distort radius: floor+cap
    float distortR = fminf(fmaxf(w->radius * 1.5f, 0.05f), w->radius * 2.0f);
    ScreenDistort_Add(domeC, distortR, 0.07f, 0.45f, 190.0f);
    if (s_shakeEnable > 0.5f) CameraFX_Shake(0.12f);

    // Splash crown along the rim as the dome surfaces.
    for (int i = 0; i < 26; i++) {
        float a = (float)i / 26.0f * 2.0f * PI
                + (float)GetRandomValue(-12, 12) * DEG2RAD; // §12.2 jitter
        float velUp = s_bloomVelUpMin + (float)GetRandomValue(0, 100) / 100.0f
                      * (s_bloomVelUpMax - s_bloomVelUpMin);
        SpawnParticle((ParticleConfig){
            .position = { w->center.x + cosf(a) * w->radius * 0.95f, s_bloomCrownY,
                          w->center.z + sinf(a) * w->radius * 0.95f },
            .velocity = { cosf(a) * s_bloomVelOutward, velUp, sinf(a) * s_bloomVelOutward },
            .radius   = (s_bloomCrownRadiusMin + (float)GetRandomValue(0, 100) / 100.0f
                         * (s_bloomCrownRadiusMax - s_bloomCrownRadiusMin)) * w->scale,
            .lifetime = 0.9f,
            .gradient = &s_rainGrad,
            .forceField = &s_rainFall
        });
    }

    // Protective surge: no damage, just shoves enemies out of the ward.
    Entity_ApplyAoEDamage(w->center, w->radius * 1.25f, 0.0f, s_bloomSurgeKnockback);

    // The blessing itself. NOTE: Entity_ApplyAoEBuff has no team filter yet
    // (ENTITIES_API.md §9) — buffs anything inside, ally or enemy.
    Entity_ApplyAoEBuff(w->center, w->radius, BUFF_SPEED_MULT, ACTIVE_TIME);

    // One light held for the ward's whole life (VFX rule: keep lights alive
    // through the active phase, don't strobe per frame).
    VFXLight_Spawn(domeC, ELEMENT_COLOR_WATER, s_bloomLightRadius * w->scale,
                   ACTIVE_TIME + DISSOLVE_TIME, VFX_PRIORITY_LOW);
}

static void UpdateRibbons(WardInstance *w, float dt)
{
    Vector3 domeC = { w->center.x, DomeCenterY(w), w->center.z };
    for (int r = 0; r < RIBBONS_PER_WARD; r++) {
        if (w->ribbonIds[r] < 0) continue;
        TrailEntity *tr = GetTrail(w->ribbonIds[r]);
        if (!tr) { w->ribbonIds[r] = -1; continue; }

        w->orbitAngle[r] += w->orbitSpeed[r] * dt;
        // Orbit radius breathes and bobs so no ribbon traces a perfect circle.
        float breathe = 1.05f + 0.06f * sinf(w->timer * 1.3f + w->bobPhase[r]);
        float bob     = sinf(w->timer * 1.7f + w->bobPhase[r]) * w->radius * 0.10f;

        Vector3 offset = Vector3Add(
            Vector3Scale(w->orbitU[r], cosf(w->orbitAngle[r]) * w->radius * breathe),
            Vector3Scale(w->orbitV[r], sinf(w->orbitAngle[r]) * w->radius * breathe));
        Vector3 tip = Vector3Add(domeC, offset);
        tip.y += bob;
        if (tip.y < s_ribbonFloorY) tip.y = s_ribbonFloorY; // keep ribbons above the ground plane
        UpdateFollowerPosition(w->ribbonIds[r], tip);

        // Droplets shed from the ribbon crest, raining down the dome wall.
        w->dripTimer[r] += dt;
        if (w->dripTimer[r] >= DRIP_INTERVAL) {
            w->dripTimer[r] = (float)GetRandomValue(-8, 6) / 100.0f;
            float dripVelX = (float)GetRandomValue(-100, 100) / 100.0f * s_dripVelXZ;
            float dripVelZ = (float)GetRandomValue(-100, 100) / 100.0f * s_dripVelXZ;
            SpawnParticle((ParticleConfig){
                .position   = tip,
                .velocity   = { dripVelX, s_dripVelY, dripVelZ },
                .radius     = (s_dripRadiusMin + (float)GetRandomValue(0, 100) / 100.0f
                               * (s_dripRadiusMax - s_dripRadiusMin)) * w->scale,
                .lifetime   = 0.9f,
                .gradient   = &s_rainGrad,
                .forceField = &s_rainFall
            });
        }
    }
}

void UpdateThuyKinhSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    (void)enemyPos; (void)enemyRadius;

    RebuildRainFall();

    for (int i = 0; i < MAX_WARDS; i++) {
        WardInstance *w = &s_wards[i];
        if (w->state == WARD_INACTIVE) continue;
        w->timer += dt;

        switch (w->state) {
        case WARD_GATHER: {
            // Rebuild gather field from tunables before emitting
            ForceField_Clear(&s_gatherFieldActive);
            SkillForceMix_AddLayers(&s_gatherForce, &s_gatherFieldActive);

            // Droplets converging on the caster from a collapsing ring.
            for (int p = 0; p < 3; p++) {
                float a = (float)GetRandomValue(0, 3600) / 10.0f * DEG2RAD;
                float r = w->radius * (1.3f - w->timer / GATHER_TIME);
                float posY = s_gatherParticleYMin + (float)GetRandomValue(0, 100) / 100.0f
                             * (s_gatherParticleYMax - s_gatherParticleYMin);
                Vector3 pos = { w->center.x + cosf(a) * r, posY,
                                w->center.z + sinf(a) * r };
                Vector3 toC = Vector3Subtract(
                    (Vector3){ w->center.x, s_gatherTargetY, w->center.z }, pos);
                float speedMult = SkillCurve_Eval(&s_gatherSpeedCurve,
                                                  w->timer / GATHER_TIME);
                SpawnParticle((ParticleConfig){
                    .position     = pos,
                    .velocity     = Vector3Scale(Vector3Normalize(toC), s_gatherSpeed * speedMult),
                    .radius       = (s_gatherRadiusMin + (float)GetRandomValue(0, 100) / 100.0f
                                     * (s_gatherRadiusMax - s_gatherRadiusMin)) * w->scale,
                    .lifetime     = 0.45f,
                    .gradient     = &s_mistGrad,
                    .forceField   = &s_gatherFieldActive,
                    .emissiveCurve = &s_gatherEmissiveCurve
                });
            }
            if (w->timer >= GATHER_TIME) {
                w->state = WARD_BLOOM;
                w->timer = 0.0f;
            }
            break;
        }

        case WARD_BLOOM: {
            float t = w->timer / BLOOM_TIME;
            float e = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); // easeOutCubic
            w->growth = e;
            if (w->timer >= BLOOM_TIME) {
                w->state  = WARD_ACTIVE;
                w->timer  = 0.0f;
                w->growth = 1.0f;
                WardBloomBurst(w);
                SpawnRibbons(w);
            }
            break;
        }

        case WARD_ACTIVE: {
            UpdateRibbons(w, dt);

            // Rebuild mist field from tunables + base swirl layers each frame
            // so sandbox changes take effect immediately.
            ForceField_Clear(&s_mistFieldActive);
            ForceField_AddLayer(&s_mistFieldActive, (ForceLayer){
                .type      = FORCE_VORTEX,
                .origin    = { w->center.x, 0.0f, w->center.z },
                .direction = { 0.0f, 1.0f, 0.0f },
                .strength  = s_swirlVortex, .radius = w->radius * 1.5f, .falloff = 0.0f
            });
            ForceField_AddLayer(&s_mistFieldActive, (ForceLayer){
                .type     = FORCE_GRAVITY_POINT,
                .origin   = { w->center.x, DomeCenterY(w), w->center.z },
                .strength = s_swirlPull, .radius = w->radius * 2.0f, .falloff = 1.0f
            });
            ForceField_AddLayer(&s_mistFieldActive, (ForceLayer){
                .type = FORCE_DRAG, .strength = s_swirlDrag
            });
            SkillForceMix_AddLayers(&s_mistForce, &s_mistFieldActive);

            // Soft additive ring pulses at the waterline — overlapping decals
            // that each fade out give a breathing glow with feathered edges
            // (the old 3D torus read as a hard plastic hoop).
            w->ringTimer += dt;
            if (w->ringTimer >= 0.55f) {
                w->ringTimer = 0.0f;
                DecalSystem_AddEx(w->center,
                                  (float)GetRandomValue(0, 360), 9.0f,
                                  w->radius * 1.00f, w->radius * 1.08f,
                                  s_ringTex, 1.0f,
                                  ColorAlpha(ELEMENT_COLOR_WATER, 0.30f),
                                  BLEND_ADDITIVE, 0.0f);
            }

            // Mist curling up the inside of the dome.
            w->mistAccum += dt * MIST_RATE;
            while (w->mistAccum >= 1.0f) {
                w->mistAccum -= 1.0f;
                float a = (float)GetRandomValue(0, 3600) / 10.0f * DEG2RAD;
                float r = w->radius * (float)GetRandomValue(30, 92) / 100.0f;
                float posY = s_mistYMin + (float)GetRandomValue(0, 100) / 100.0f
                             * (s_mistYMax - s_mistYMin);
                float velY = s_mistVelYMin + (float)GetRandomValue(0, 100) / 100.0f
                             * (s_mistVelYMax - s_mistVelYMin);
                float tPhase = w->timer / ACTIVE_TIME;
                float rMult  = SkillCurve_Eval(&s_mistRadiusCurve,  tPhase);
                SpawnParticle((ParticleConfig){
                    .position      = { w->center.x + cosf(a) * r, posY,
                                       w->center.z + sinf(a) * r },
                    .velocity      = { -sinf(a) * s_mistVelXZ, velY, cosf(a) * s_mistVelXZ },
                    .radius        = (s_mistRadiusMin + (float)GetRandomValue(0, 100) / 100.0f
                                      * (s_mistRadiusMax - s_mistRadiusMin))
                                     * w->scale * rMult,
                    .lifetime      = 1.5f,
                    .gradient      = &s_mistGrad,
                    .forceField    = &s_mistFieldActive,
                    .emissiveCurve = &s_mistEmissiveCurve
                });
            }

            if (w->timer >= ACTIVE_TIME) WardStartDissolve(w);
            break;
        }

        case WARD_DISSOLVE: {
            w->dissolve = SmoothStep01(w->timer / DISSOLVE_TIME);
            if (w->timer >= DISSOLVE_TIME) {
                w->state    = WARD_INACTIVE;
                w->dissolve = 0.0f;
            }
            break;
        }

        default: break;
        }
    }
}

void DrawThuyKinhSkill(void)
{
    bool anyDome = false;
    for (int i = 0; i < MAX_WARDS; i++) {
        WardState st = s_wards[i].state;
        if (st == WARD_BLOOM || st == WARD_ACTIVE || st == WARD_DISSOLVE) {
            anyDome = true;
            break;
        }
    }
    if (!anyDome) return;

    rlDisableDepthMask();

    // The water dome (translucent custom shader). Base ring is decal-based
    // (see WARD_ACTIVE update), not a 3D mesh — nothing else to draw here.
    BeginBlendMode(BLEND_ALPHA);
    BeginShaderMode(s_shader);
    // Immediate-mode draw below: SkillManager_BeginShader fixes matModel to
    // identity and binds u_time/viewPos/u_resolution (CORE_API.md Rule D).
    SkillManager_BeginShader(s_shader);

    Vector3 lightDir = Vector3Negate(Environment_GetSunDirection());
    SetShaderValue(s_shader, s_lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);
    // Flow-map textures: bound via SetShaderValueTexture inside the active
    // shader mode (rlActiveTextureSlot binding is silently broken — CORE_API).
    SetShaderValueTexture(s_shader, s_flowTexLoc, s_flowTex);
    SetShaderValueTexture(s_shader, s_causticsLoc, s_causticsTex);
    rlColor4ub(255, 255, 255, 255); // Rule 11.1: reset vertex color

    for (int i = 0; i < MAX_WARDS; i++) {
        const WardInstance *w = &s_wards[i];
        if (w->state != WARD_BLOOM && w->state != WARD_ACTIVE &&
            w->state != WARD_DISSOLVE) continue;

        float r = w->radius * (0.15f + 0.85f * w->growth);
        SetShaderValue(s_shader, s_dissolveLoc, &w->dissolve, SHADER_UNIFORM_FLOAT);
        DrawCoreSphere((Vector3){ w->center.x, r * 0.55f, w->center.z },
                       r, 36, 48, WHITE);
    }

    SkillManager_EndShader();
    EndShaderMode();
    EndBlendMode();
    rlEnableDepthMask();
}

void UnloadThuyKinhSkill(void)
{
    // No-op: shader is owned by ResourceManager; trails die via state machine.
}

bool IsThuyKinhSkillCoiling(void)
{
    for (int i = 0; i < MAX_WARDS; i++) {
        if (s_wards[i].state == WARD_GATHER || s_wards[i].state == WARD_BLOOM)
            return true;
    }
    return false;
}

int GetThuyKinhSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles)
{
    (void)outProjectiles; (void)maxProjectiles;
    return 0; // pure buff/ward — nothing flies, nothing to intercept
}

void DeactivateThuyKinhProjectile(int index)
{
    (void)index;
}
