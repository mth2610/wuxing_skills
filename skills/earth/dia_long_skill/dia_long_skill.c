// Địa Long Cốt (Earth Dragon Spine) — benchmark Earth skill.
// Drag a path: the ground tears open into a glowing fissure, jagged shard
// vertebrae erupt in a traveling wave along the drawn line, climaxing in a
// rock-and-fang "dragon head" at the end, then the whole spine sinks back
// and dissolves chunk by chunk.
#include "skills/earth/dia_long_skill/dia_long_skill.h"
#include "core/resource_manager.h"
#include "core/decal_system.h"
#include "core/particle_system.h"
#include "core/color_gradient.h"
#include "core/procedural_mesh_utils.h"
#include "core/path_spline.h"
#include "core/skill_helper.h"
#include "core/skill_manager.h"
#include "core/vfx_light.h"
#include "core/camera_fx.h"
#include "core/screen_distort.h"
#include "core/emitter_system.h"
#include "core/utils_math.h"
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

#define MAX_SPINES        2
#define MAX_VERTS         10    // vertebra clusters along the path (head extra)

// Pass 1: VERT_SPACING rescaled 55.0 cm → 0.55 m
static float s_vertSpacing = 0.55f;
static float s_shakeEnable = 1.0f;   // 1=on, 0=off

#define CAST_TIME         0.5f
#define RISE_TIME         0.22f
#define HEAD_RISE_TIME    0.30f
#define VERT_STAGGER      0.07f // eruption wave speed along the spine
#define ACTIVE_TIME       2.2f
#define COLLAPSE_TIME     0.9f
#define DOT_TICK          0.5f

// Pass 5: sandbox-tunable geometry knobs
// Vertebra heights (meters): base + arc delta, randomised ±15% per vertebra
static float s_vertHeightBase  = 0.44f;  // Pass 1: was 44.0f cm
static float s_vertHeightArc   = 0.30f;  // Pass 1: was 30.0f cm (arc along path)
// Head height (meters)
static float s_headHeight      = 0.72f;  // Pass 1: was 72.0f cm
// Fissure mesh dimensions (meters)
static float s_fissureWidth    = 0.22f;  // Pass 1: was 22.0f cm
static float s_fissureDepth    = 0.18f;  // Pass 1: was 18.0f cm
// Decal radii (meters)
static float s_crackDecalRadius    = 0.46f;  // Pass 1: was 46.0f cm (base, +0..14 cm jitter → 0..0.14)
static float s_shatterDecalRadiusV = 0.34f;  // Pass 1: was 34.0f cm, vertebra aftermath
static float s_shatterDecalRadiusH = 0.55f;  // Pass 1: was 55.0f cm, head aftermath
static float s_runeDecalRadius     = 0.60f;  // Pass 1: was 60.0f cm, windup rune
static float s_lavaDecalRadius     = 0.65f;  // Pass 1: was 65.0f cm, head lava
// VFX light radii (meters)
static float s_castLightRadius    = 0.70f;   // Pass 1: was 70.0f cm
static float s_eruptLightRadius   = 0.60f;   // Pass 1: was 60.0f cm, per vertebra
static float s_headLightRadius    = 1.50f;   // Pass 1: was 150.0f cm
// AoE damage radii (meters)
static float s_vertAoERadius      = 0.32f;   // Pass 1: was 32.0f cm
static float s_headAoERadius      = 0.60f;   // Pass 1: was 60.0f cm
static float s_dotAoERadius       = 0.55f;   // Pass 1: was 55.0f cm
// Projectile query radius (meters)
static float s_projectileRadius   = 0.45f;   // Pass 1: was 45.0f cm
// Screen distort radius (meters)
static float s_distortRadius      = 1.30f;   // Pass 1: was 130.0f cm
// Emitter rate (not spatial — particles/sec, unchanged)
static float s_headEmitterRate    = 24.0f;
// Particle speeds (m/s): eruption burst outward / upward
static float s_burstSpeedOut      = 0.70f;   // Pass 1: was 70.0f cm/s
static float s_burstSpeedUpMin    = 0.60f;   // Pass 1: was 60 cm/s
static float s_burstSpeedUpMax    = 1.30f;   // Pass 1: was 130 cm/s
// Ember drift speeds (m/s): active-phase vertebra motes
static float s_emberSpeedXZ       = 0.12f;   // Pass 1: was 12 cm/s
static float s_emberSpeedUpMin    = 0.18f;   // Pass 1: was 18 cm/s
static float s_emberSpeedUpMax    = 0.40f;   // Pass 1: was 40 cm/s
// Particle radii (meters)
static float s_burstRadiusMin     = 0.018f;  // Pass 1: was 1.8 cm (18/10 cm)
static float s_burstRadiusMax     = 0.042f;  // Pass 1: was 4.2 cm (42/10 cm)
static float s_emberRadiusMin     = 0.008f;  // Pass 1: was 0.8 cm  (8/10 cm)
static float s_emberRadiusMax     = 0.016f;  // Pass 1: was 1.6 cm (16/10 cm)

// Per-phase over-lifetime curves (flat → no-op until shaped in sandbox)
static SkillCurve s_castRadiusCurve, s_castSpeedCurve, s_castAlphaCurve, s_castEmissiveCurve;
static SkillCurve s_eruptRadiusCurve, s_eruptSpeedCurve, s_eruptAlphaCurve, s_eruptEmissiveCurve;
static SkillCurve s_activeRadiusCurve, s_activeSpeedCurve, s_activeAlphaCurve, s_activeEmissiveCurve;

// One tunable force mix per phase
static SkillForceMix s_castForce;
static SkillForceMix s_eruptForce;
static SkillForceMix s_activeForce;
static ForceField    s_castField;
static ForceField    s_eruptField;
static ForceField    s_activeField;

// 3 phases × (3 curves + 1 ForceMix×29) + ~40 scalar tunables
// = 3×32 + 40 = 136
#define DIA_LONG_TUNABLE_COUNT 144

typedef enum {
    SPINE_INACTIVE = 0,
    SPINE_CASTING,   // fissure telegraph, dust converging
    SPINE_ERUPTING,  // vertebra wave travels tail -> head
    SPINE_ACTIVE,    // spine holds, magma pulses, head zone ticks damage
    SPINE_COLLAPSE   // staggered sink + chunky dissolve
} SpineState;

typedef struct {
    Vector3 pos;
    float   delay;    // eruption stagger along the path
    float   riseT;    // 0..1 rise progress
    float   height;
    bool    erupted;  // one-shot eruption FX fired
    ShardClusterMeshData shards;
} Vertebra;

typedef struct {
    SpineState state;
    float   timer;
    float   scale;
    float   dissolve;
    float   dotTimer;
    int     vertCount;
    Vertebra verts[MAX_VERTS];

    // Dragon head (path end): same basalt-column style as the vertebrae,
    // just taller and denser — the finale of the same wave, not a set piece.
    Vector3 headPos;
    Vector3 headDir;
    float   headDelay;
    float   headRiseT;
    float   headHeight;
    bool    headErupted;
    ShardClusterMeshData headShards;

    FissureMeshData fissure;

    float damage;
    float knockback;
    int   headEmitterId;
    int   ownerAgentId; // CORE_ISSUES.md Item 15 — set from Cast's agentId param
} DragonSpine;

static DragonSpine   s_spines[MAX_SPINES];
static Shader        s_shader;
static Texture2D     s_crackTex;
static int           s_uDissolveLoc = -1;
static int           s_uGlowLoc = -1;
static ColorGradient s_dustGrad;
static int           s_skillIndex = -1;

// Overshoot ease — shards punch slightly past their rest height, then settle.
static float EaseOutBack(float t)
{
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

static float SpineGlow(const DragonSpine *sp)
{
    switch (sp->state) {
        case SPINE_ERUPTING: return 1.0f;
        case SPINE_ACTIVE:   return 0.5f + 0.18f * sinf(sp->timer * 4.0f);
        case SPINE_COLLAPSE: return (1.0f - sp->dissolve) * 0.3f;
        default:             return 0.0f;
    }
}

static void SpineStartCollapse(DragonSpine *sp)
{
    if (sp->state == SPINE_COLLAPSE || sp->state == SPINE_INACTIVE) return;
    sp->state = SPINE_COLLAPSE;
    sp->timer = 0.0f;
    if (sp->headEmitterId >= 0) {
        Emitter_Stop(sp->headEmitterId);
        sp->headEmitterId = -1;
    }
    // Aftermath: lasting shatter marks where the spine stood.
    for (int v = 0; v < sp->vertCount; v += 2) {
        SpawnGroundDecal(DECAL_PRESET_EARTH_SHATTER, sp->verts[v].pos,
                         s_shatterDecalRadiusV * sp->scale, 6.0f);
    }
    SpawnGroundDecal(DECAL_PRESET_EARTH_SHATTER, sp->headPos,
                     s_shatterDecalRadiusH * sp->scale, 6.0f);
}

// CORE_ISSUES.md Item 13 — zone skill: let gameplay code query "is my spine gone".
static bool DiaLongSkill_HasActiveInstance(int agentId)
{
    for (int i = 0; i < MAX_SPINES; i++) {
        if (s_spines[i].state != SPINE_INACTIVE && s_spines[i].ownerAgentId == agentId)
            return true;
    }
    return false;
}

// CORE_ISSUES.md Item 14 — crowd-control interrupt collapses only the caster's spine.
static void DiaLongSkill_Abort(int agentId)
{
    for (int i = 0; i < MAX_SPINES; i++) {
        if (s_spines[i].state != SPINE_INACTIVE && s_spines[i].ownerAgentId == agentId)
            SpineStartCollapse(&s_spines[i]);
    }
}

void InitDiaLongSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth; (void)screenHeight;

    s_skillIndex = Skill_GetIndexByName("DIA_LONG");
    RegisterSkillLifecycleQuery(s_skillIndex, DiaLongSkill_HasActiveInstance);
    RegisterSkillAbort(s_skillIndex, DiaLongSkill_Abort);

    for (int i = 0; i < MAX_SPINES; i++) {
        s_spines[i].state = SPINE_INACTIVE;
        s_spines[i].headEmitterId = -1;
    }

    s_shader = ResourceManager_LoadShader(
        "skills/earth/dia_long_skill/dia_long.vs",
        "skills/earth/dia_long_skill/dia_long.fs");
    s_uDissolveLoc = GetShaderLocation(s_shader, "u_dissolve");
    s_uGlowLoc     = GetShaderLocation(s_shader, "u_glow");
    s_crackTex     = ResourceManager_LoadTexture("assets/textures/crack.png");

    ColorGradient_AddStop(&s_dustGrad, 0.00f, ColorAlpha(ORANGE, 0.9f));
    ColorGradient_AddStop(&s_dustGrad, 0.35f, ColorAlpha(ELEMENT_COLOR_EARTH, 0.8f));
    ColorGradient_AddStop(&s_dustGrad, 1.00f, (Color){ 40, 28, 18, 0 });

    // Pass 5 — per-phase curves initialised flat (no-op until shaped in sandbox)
    SkillCurve_SetConstant(&s_castRadiusCurve,    1.0f);
    SkillCurve_SetConstant(&s_castSpeedCurve,     1.0f);
    SkillCurve_SetConstant(&s_castAlphaCurve,     1.0f);
    SkillCurve_SetConstant(&s_castEmissiveCurve,  1.0f);
    SkillCurve_SetConstant(&s_eruptRadiusCurve,   1.0f);
    SkillCurve_SetConstant(&s_eruptSpeedCurve,    1.0f);
    SkillCurve_SetConstant(&s_eruptAlphaCurve,    1.0f);
    SkillCurve_SetConstant(&s_eruptEmissiveCurve, 1.0f);
    SkillCurve_SetConstant(&s_activeRadiusCurve,  1.0f);
    SkillCurve_SetConstant(&s_activeSpeedCurve,   1.0f);
    SkillCurve_SetConstant(&s_activeAlphaCurve,   1.0f);
    SkillCurve_SetConstant(&s_activeEmissiveCurve,1.0f);

    // Pass 5 — register tunables
    static SkillTunableEntry s_tunables[DIA_LONG_TUNABLE_COUNT];
    int tn = 0;
    #include "dia_long_skill_tunables.inl"
    SkillTunables_LoadPersisted("skills/earth/dia_long_skill/dia_long.tuning",
                                s_tunables, tn);
    RegisterSkillTunables(s_skillIndex, s_tunables, tn);
}

void CastDiaLongSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    if (!SkillManager_CanCast(s_skillIndex, agentId)) return;

    DragonSpine *sp = NULL;
    for (int i = 0; i < MAX_SPINES; i++) {
        if (s_spines[i].state == SPINE_INACTIVE) { sp = &s_spines[i]; break; }
    }
    if (!sp) return;

    float scale = (params.sizeScale > 0.0f) ? params.sizeScale : 1.0f;

    // Resample the drawn path at even spacing (straight line fallback).
    Vector3 rawPath[33];
    int rawCount = 0;
    if (params.pathPointCount > 1) {
        for (int i = 0; i < params.pathPointCount && i < 32; i++)
            rawPath[rawCount++] = params.pathPoints[i];
    } else {
        rawPath[rawCount++] = startPos;
        rawPath[rawCount++] = target;
    }
    for (int i = 0; i < rawCount; i++) rawPath[i].y = 0.0f;

    Vector3 sampled[MAX_VERTS + 1];
    int sampledCount = SamplePath(rawPath, rawCount, s_vertSpacing * scale,
                                  sampled, MAX_VERTS + 1);
    if (sampledCount < 2) {
        sampled[0] = rawPath[0];
        sampled[1] = rawPath[rawCount - 1];
        sampledCount = 2;
    }

    *sp = (DragonSpine){ 0 };
    sp->state         = SPINE_CASTING;
    sp->scale         = scale;
    sp->ownerAgentId  = agentId;
    sp->headEmitterId = -1;
    sp->damage        = Skill_CalculateDamage(SKILL_CAT_AOE_CONTROL, params);
    sp->knockback     = Skill_CalculateKnockback(SKILL_CAT_AOE_CONTROL, params);
    sp->vertCount     = sampledCount - 1; // last sample point is the head

    // Vertebrae: shard clusters leaning alternately off the path direction,
    // heights arcing up toward the head — a dragon's spine silhouette.
    for (int v = 0; v < sp->vertCount; v++) {
        Vertebra *vb = &sp->verts[v];
        float t = (sp->vertCount > 1) ? (float)v / (float)(sp->vertCount - 1) : 0.0f;

        Vector3 dir = Vector3Subtract(sampled[v + 1], sampled[v]);
        dir.y = 0.0f;
        if (Vector3Length(dir) < 0.001f) dir = (Vector3){ 1, 0, 0 };
        dir = Vector3Normalize(dir);
        Vector3 perp = (Vector3){ -dir.z, 0.0f, dir.x };

        vb->pos    = sampled[v];
        vb->delay  = (float)v * VERT_STAGGER;
        vb->riseT  = 0.0f;
        // Pass 1: height now in meters (was cm)
        vb->height = (s_vertHeightBase + s_vertHeightArc * t) * scale
                   * ((float)GetRandomValue(85, 115) / 100.0f); // §12.3
        vb->erupted = false;

        // §12.2 perpendicular jitter: alternate lean sign so the spine zigzags,
        // but keep the columns near-vertical (basalt-pillar look, not spikes).
        float lean = ((v & 1) ? 1.0f : -1.0f)
                   * ((float)GetRandomValue(6, 14) / 100.0f);
        Vector3 up = { dir.x * 0.15f + perp.x * lean, 1.0f, dir.z * 0.15f + perp.z * lean };

        ShardClusterConfig cfg = ProceduralMesh_DefaultShardClusterConfig();
        cfg.spreadAngle  = 0.12f;  // tight cluster, near-parallel columns
        cfg.thicknessMin = 0.18f;  // chunky cross-section
        cfg.thicknessMax = 0.26f;
        cfg.tipSharpness = 0.35f;  // mostly flat-cut tops, like broken basalt
        cfg.sides        = 6;
        ProceduralMesh_BuildShardCluster(&vb->shards, vb->pos, Vector3Normalize(up),
                                         2 + GetRandomValue(0, 1),
                                         vb->height * 0.6f, vb->height,
                                         GetRandomValue(0, 99999), &cfg);
    }

    // Head: boulder + forward fangs + vertical crown at the path's end.
    sp->headPos    = sampled[sampledCount - 1];
    sp->headDir    = Vector3Subtract(sp->headPos, sampled[sampledCount - 2]);
    sp->headDir.y  = 0.0f;
    if (Vector3Length(sp->headDir) < 0.001f) sp->headDir = (Vector3){ 1, 0, 0 };
    sp->headDir    = Vector3Normalize(sp->headDir);
    sp->headDelay  = (float)sp->vertCount * VERT_STAGGER + 0.15f;
    sp->headHeight = s_headHeight * scale; // Pass 1: now in meters
    sp->headRiseT  = 0.0f;
    sp->headErupted = false;

    // Same column recipe as the vertebrae — just one more column and taller.
    ShardClusterConfig headCfg = ProceduralMesh_DefaultShardClusterConfig();
    headCfg.spreadAngle  = 0.12f;
    headCfg.thicknessMin = 0.18f;
    headCfg.thicknessMax = 0.26f;
    headCfg.tipSharpness = 0.35f;
    headCfg.sides        = 6;
    Vector3 headUp = { sp->headDir.x * 0.15f, 1.0f, sp->headDir.z * 0.15f };
    ProceduralMesh_BuildShardCluster(&sp->headShards, sp->headPos,
                                     Vector3Normalize(headUp), 4,
                                     sp->headHeight * 0.6f, sp->headHeight,
                                     GetRandomValue(0, 99999), &headCfg);

    // The dragon vein itself: one jagged 3D crack following the whole drawn
    // path — wide and deep enough that the magma glow inside is clearly seen.
    ProceduralMesh_BuildFissure(&sp->fissure, sampled, sampledCount,
                                s_fissureWidth * scale, s_fissureDepth * scale, 0.85f,
                                GetRandomValue(0, 99999));

    // Spider-crack telegraph: overlapping crack decals along the whole drawn
    // line, yaw-aligned to the path — the ground visibly fractures during
    // the windup, before anything erupts.
    for (int i = 0; i < sampledCount; i++) {
        Vector3 segDir = (i < sampledCount - 1)
            ? Vector3Subtract(sampled[i + 1], sampled[i])
            : sp->headDir;
        float yawDeg = atan2f(segDir.z, segDir.x) * RAD2DEG
                     + (float)GetRandomValue(-25, 25); // §12.2 jitter
        // Pass 4: cap crack decal radius proportional to path spacing
        float crackR = fminf(fmaxf((s_crackDecalRadius + (float)GetRandomValue(0, 14) * 0.01f) * scale,
                                   s_vertSpacing * 0.18f),
                             s_vertSpacing * scale * 0.6f);
        DecalSystem_Add(sampled[i], yawDeg, crackR, s_crackTex, 6.5f,
                        ColorAlpha(ELEMENT_COLOR_EARTH, 0.85f));
    }

    // Telegraph: windup at the caster, rune warning where the head will erupt.
    // CORE_ISSUES.md Item 34 — EARTH_CRACK internals not yet rescaled.
    SpawnCastEffect(startPos, EFFECT_PRESET_EARTH_CRACK, scale * 0.7f);
    PlayCastSound(EFFECT_PRESET_EARTH_CRACK);
    SpawnGroundDecal(DECAL_PRESET_EARTH_RUNE, sp->headPos, s_runeDecalRadius * scale,
                     CAST_TIME + sp->headDelay + 1.0f);
    VFXLight_Spawn(startPos, ORANGE, s_castLightRadius * scale, CAST_TIME, VFX_PRIORITY_LOW);

    SkillManager_TriggerCooldown(s_skillIndex, agentId,
                                 Skill_CalculateCooldown(SKILL_CAT_AOE_CONTROL, params));
}

static void VertebraErupt(DragonSpine *sp, Vertebra *vb)
{
    vb->erupted = true;
    SpawnGroundDecal(DECAL_PRESET_EARTH_SHATTER, vb->pos,
                     s_shatterDecalRadiusV * sp->scale, 3.0f);
    VFXLight_Spawn(vb->pos, ORANGE, s_eruptLightRadius * sp->scale, 0.35f, VFX_PRIORITY_LOW);
    if (s_shakeEnable > 0.5f) CameraFX_Shake(0.06f);
    // Pass 4: clamp vertebra AoE radius to sensible proportion of height
    float aoeR = fminf(fmaxf(s_vertAoERadius * sp->scale,
                              vb->height * 0.3f),
                       vb->height * 1.5f);
    Entity_ApplyAoEDamage(vb->pos, aoeR, sp->damage * 0.20f, 140.0f);

    for (int i = 0; i < 10; i++) {
        float a = (float)i / 10.0f * 2.0f * PI;
        // Pass 1: burst velocities now in m/s
        float upSpeed = s_burstSpeedUpMin
                      + (s_burstSpeedUpMax - s_burstSpeedUpMin)
                        * ((float)GetRandomValue(0, 100) / 100.0f);
        Vector3 vel = { cosf(a) * s_burstSpeedOut * sp->scale,
                        upSpeed,
                        sinf(a) * s_burstSpeedOut * sp->scale };
        // Pass 1: particle radius now in meters
        float r = s_burstRadiusMin
                + (s_burstRadiusMax - s_burstRadiusMin)
                  * ((float)GetRandomValue(0, 100) / 100.0f);
        SpawnParticle((ParticleConfig){
            .position      = vb->pos,
            .velocity      = vel,
            .radius        = r * sp->scale,
            .lifetime      = 0.9f,
            .gradient      = &s_dustGrad,
            .emissiveCurve = &s_eruptEmissiveCurve
        });
    }
}

static void HeadErupt(DragonSpine *sp)
{
    sp->headErupted = true;
    // CORE_ISSUES.md Item 34 — EARTH_CRACK internals not yet rescaled.
    SpawnImpactEffect(sp->headPos, EFFECT_PRESET_EARTH_CRACK, 1.5f * sp->scale);
    PlayImpactSound(EFFECT_PRESET_EARTH_CRACK);
    // Pass 1: distort radius in meters; speed/strength are unitless screen params
    ScreenDistort_Add(sp->headPos, s_distortRadius * sp->scale, 0.8f, 0.45f, 250.0f);
    if (s_shakeEnable > 0.5f) CameraFX_Shake(0.5f);
    // Held for the whole active phase per VFX standards, ultimate priority.
    VFXLight_Spawn(sp->headPos, ORANGE, s_headLightRadius * sp->scale,
                   ACTIVE_TIME + HEAD_RISE_TIME, VFX_PRIORITY_HIGH_ULTIMATE);
    // Pass 4: head AoE radius floored/capped proportional to head height
    float headAoER = fminf(fmaxf(s_headAoERadius * sp->scale,
                                  sp->headHeight * 0.5f),
                           sp->headHeight * 2.0f);
    Entity_ApplyAoEDamage(sp->headPos, headAoER, sp->damage, sp->knockback * 1.4f);
    sp->headEmitterId = Emitter_AttachToPoint(EMITTER_EARTH_DUST, sp->headPos,
                                              s_headEmitterRate, ACTIVE_TIME + HEAD_RISE_TIME);
    // Crawling lava-crack decal (radial flow shader) under the head zone.
    SpawnGroundDecal(DECAL_PRESET_FIRE_LAVA, sp->headPos,
                     s_lavaDecalRadius * sp->scale, ACTIVE_TIME + COLLAPSE_TIME);
}

void UpdateDiaLongSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    (void)enemyPos; (void)enemyRadius;

    // Rebuild force fields from tunables each frame (cheap, matches thunder_orb pattern)
    SkillForceMix_AddLayers(&s_castForce,   &s_castField);
    SkillForceMix_AddLayers(&s_eruptForce,  &s_eruptField);
    SkillForceMix_AddLayers(&s_activeForce, &s_activeField);

    for (int i = 0; i < MAX_SPINES; i++) {
        DragonSpine *sp = &s_spines[i];
        if (sp->state == SPINE_INACTIVE) continue;
        sp->timer += dt;

        switch (sp->state) {
        case SPINE_CASTING: {
            if (sp->timer >= CAST_TIME) {
                sp->state = SPINE_ERUPTING;
                sp->timer = 0.0f;
                // CORE_ISSUES.md Item 34 — EARTH_CRACK internals not yet rescaled.
                SpawnImpactEffect(sp->verts[0].pos, EFFECT_PRESET_EARTH_CRACK,
                                  0.7f * sp->scale);
                if (s_shakeEnable > 0.5f) CameraFX_Shake(0.25f);
            }
            break;
        }

        case SPINE_ERUPTING: {
            for (int v = 0; v < sp->vertCount; v++) {
                Vertebra *vb = &sp->verts[v];
                float t = (sp->timer - vb->delay) / RISE_TIME;
                vb->riseT = (t < 0.0f) ? 0.0f : (t > 1.0f) ? 1.0f : t;
                if (!vb->erupted && vb->riseT > 0.0f) VertebraErupt(sp, vb);
            }
            float ht = (sp->timer - sp->headDelay) / HEAD_RISE_TIME;
            sp->headRiseT = (ht < 0.0f) ? 0.0f : (ht > 1.0f) ? 1.0f : ht;
            if (!sp->headErupted && sp->headRiseT > 0.0f) HeadErupt(sp);

            if (sp->headRiseT >= 1.0f) {
                sp->state = SPINE_ACTIVE;
                sp->timer = 0.0f;
                sp->dotTimer = 0.0f;
            }
            break;
        }

        case SPINE_ACTIVE: {
            // Lingering quake zone around the head.
            sp->dotTimer += dt;
            if (sp->dotTimer >= DOT_TICK) {
                sp->dotTimer -= DOT_TICK;
                // Pass 4: dot AoE radius floored/capped around head height
                float dotR = fminf(fmaxf(s_dotAoERadius * sp->scale,
                                         sp->headHeight * 0.4f),
                                   sp->headHeight * 2.0f);
                Entity_ApplyAoEDamage(sp->headPos, dotR, sp->damage * 0.15f, 0.0f);
            }
            // Ember motes drifting off a random vertebra — continuous life
            // while active, per VFX standards.
            if (GetRandomValue(0, 100) < 30 && sp->vertCount > 0) {
                const Vertebra *vb = &sp->verts[GetRandomValue(0, sp->vertCount - 1)];
                // Pass 1: velocities in m/s, radius in meters
                float xzSign = (GetRandomValue(0, 1) ? 1.0f : -1.0f);
                float zSign  = (GetRandomValue(0, 1) ? 1.0f : -1.0f);
                float upSpeed = s_emberSpeedUpMin
                              + (s_emberSpeedUpMax - s_emberSpeedUpMin)
                                * ((float)GetRandomValue(0, 100) / 100.0f);
                float er = s_emberRadiusMin
                         + (s_emberRadiusMax - s_emberRadiusMin)
                           * ((float)GetRandomValue(0, 100) / 100.0f);
                SpawnParticle((ParticleConfig){
                    .position      = { vb->pos.x, vb->pos.y + vb->height * 0.5f, vb->pos.z },
                    .velocity      = { xzSign * s_emberSpeedXZ,
                                       upSpeed,
                                       zSign  * s_emberSpeedXZ },
                    .radius        = er * sp->scale,
                    .lifetime      = 1.1f,
                    .gradient      = &s_dustGrad,
                    .emissiveCurve = &s_activeEmissiveCurve
                });
            }
            if (sp->timer >= ACTIVE_TIME) SpineStartCollapse(sp);
            break;
        }

        case SPINE_COLLAPSE: {
            sp->dissolve = sp->timer / COLLAPSE_TIME;
            if (sp->dissolve > 1.0f) sp->dissolve = 1.0f;
            // Sink stagger mirrors the eruption wave: tail dives first.
            for (int v = 0; v < sp->vertCount; v++) {
                Vertebra *vb = &sp->verts[v];
                float t = (sp->timer - vb->delay * 0.6f) / (COLLAPSE_TIME * 0.7f);
                float sink = (t < 0.0f) ? 0.0f : (t > 1.0f) ? 1.0f : t;
                vb->riseT = 1.0f - SmoothStep01(sink);
            }
            float ht = sp->timer / COLLAPSE_TIME;
            sp->headRiseT = 1.0f - SmoothStep01(ht);
            if (sp->timer >= COLLAPSE_TIME) sp->state = SPINE_INACTIVE;
            break;
        }

        default: break;
        }
    }
}

void DrawDiaLongSkill(void)
{
    // Solid rock meshes: full depth write + test (VFX_ARCHITECTURE rule 2).
    rlEnableDepthMask();
    rlEnableDepthTest();

    for (int i = 0; i < MAX_SPINES; i++) {
        DragonSpine *sp = &s_spines[i];
        if (sp->state == SPINE_INACTIVE || sp->state == SPINE_CASTING) continue;

        BeginShaderMode(s_shader);
        // Uploads identity matModel + auto-binds u_time/viewPos/u_resolution/
        // u_lightDir — required for the raw rlBegin() submissions below.
        SkillManager_BeginShader(s_shader);

        float glow = SpineGlow(sp);
        SetShaderValue(s_shader, s_uGlowLoc, &glow, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_shader, s_uDissolveLoc, &sp->dissolve, SHADER_UNIFORM_FLOAT);

        ProceduralMesh_DrawFissure(&sp->fissure, ELEMENT_COLOR_EARTH);

        for (int v = 0; v < sp->vertCount; v++) {
            const Vertebra *vb = &sp->verts[v];
            if (vb->riseT <= 0.001f) continue;
            float rise = (sp->state == SPINE_COLLAPSE)
                       ? vb->riseT                    // plain sink, no overshoot
                       : EaseOutBack(vb->riseT);      // eruption punch
            rlPushMatrix();
            // Pass 1: underground offset in meters (6 cm = 0.06 m)
            rlTranslatef(0.0f, -(1.0f - rise) * (vb->height + 0.06f), 0.0f);
            ProceduralMesh_DrawShardCluster(&vb->shards, ELEMENT_COLOR_EARTH);
            rlPopMatrix();
        }

        if (sp->headRiseT > 0.001f) {
            float rise = (sp->state == SPINE_COLLAPSE)
                       ? sp->headRiseT
                       : EaseOutBack(sp->headRiseT);
            rlPushMatrix();
            // Pass 1: underground offset in meters (8 cm = 0.08 m)
            rlTranslatef(0.0f, -(1.0f - rise) * (sp->headHeight + 0.08f), 0.0f);
            ProceduralMesh_DrawShardCluster(&sp->headShards, ELEMENT_COLOR_EARTH);
            rlPopMatrix();
        }

        SkillManager_EndShader();
        EndShaderMode();
    }
}

void UnloadDiaLongSkill(void)
{
    // No-op: shader is owned by ResourceManager, never unload here.
}

bool IsDiaLongSkillCoiling(void)
{
    for (int i = 0; i < MAX_SPINES; i++) {
        if (s_spines[i].state == SPINE_CASTING || s_spines[i].state == SPINE_ERUPTING)
            return true;
    }
    return false;
}

int GetDiaLongSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles)
{
    int count = 0;
    for (int i = 0; i < MAX_SPINES && count < maxProjectiles; i++) {
        const DragonSpine *sp = &s_spines[i];
        if (sp->state != SPINE_ERUPTING && sp->state != SPINE_ACTIVE) continue;
        outProjectiles[count].position = sp->headPos;
        outProjectiles[count].radius   = s_projectileRadius * sp->scale; // Pass 1: in meters
        outProjectiles[count].active   = true;
        count++;
    }
    return count;
}

void DeactivateDiaLongProjectile(int index)
{
    int count = 0;
    for (int i = 0; i < MAX_SPINES; i++) {
        DragonSpine *sp = &s_spines[i];
        if (sp->state != SPINE_ERUPTING && sp->state != SPINE_ACTIVE) continue;
        if (count == index) {
            SpineStartCollapse(sp);
            return;
        }
        count++;
    }
}
