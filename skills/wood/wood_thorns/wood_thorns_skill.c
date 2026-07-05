#include "skills/wood/wood_thorns/wood_thorns_skill.h"
#include "core/resource_manager.h"
#include "core/particle_system.h"
#include "core/color_gradient.h"
#include "core/decal_system.h"
#include "core/screen_distort.h"
#include "core/vfx_light.h"
#include "core/camera_fx.h"
#include "core/path_spline.h"
#include "core/skill_helper.h"
#include "core/skill_manager.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

/* ================================================================
 * § 1  CONFIGURATIONS  (real-world-scaled: 1 unit = 1 meter)
 * ================================================================ */
#define MAX_THORNS           32
#define THORNS_PER_CAST      6

// Pool-size / timing constants — not spatial, stay #define.
#define THORN_DELAY          0.08f
#define THORN_RISE_TIME      0.20f
#define THORN_HOLD_TIME      1.20f
#define THORN_DISSOLVE_TIME  0.40f
#define DUST_PARTICLES       12
#define HEIGHT_SEGS          8
#define RADIAL_SEGS          8

#include "wood_thorns_skill_params.inl"

typedef enum {
    THORN_INACTIVE = 0,
    THORN_WAITING,
    THORN_RISING,
    THORN_HOLDING,
    THORN_DISSOLVING
} ThornState;

typedef struct {
    Vector3    pos;
    float      delay;
    float      riseTimer;
    float      holdTimer;
    float      dissolveTimer;
    float      scale;
    float      yaw;
    float      tiltX;
    float      tiltZ;
    float      phase;
    ThornState state;
    bool       dealtDamage;
    bool       spawnedDecal;
    bool       spawnedLight;
    float      damage;
    float      knockback;
    int        ownerAgentId; // CORE_ISSUES.md Item 15 — caster's agent pool slot
} Thorn;

/* ================================================================
 * § 2  STATIC STORAGE
 * ================================================================ */
static Thorn         s_thorns[MAX_THORNS];
static Texture2D     s_crackTex;
static Shader        s_shader;
static int           s_uDissolveLoc;
static int           s_uTimeLoc;
static int           s_uCamPosLoc;
static ColorGradient s_dustGrad;
static int           s_skillIndex = -1;
static SkillTunableEntry s_tunables[WOOD_THORNS_TUNABLE_COUNT];

/* ================================================================
 * § 3  INTERNAL HELPERS
 * ================================================================ */
static int FindFreeSlot(void)
{
    for (int i = 0; i < MAX_THORNS; i++) {
        if (s_thorns[i].state == THORN_INACTIVE) return i;
    }
    return -1;
}

static Vector3 RotateAndTilt(Vector3 local, float yaw, float tiltX, float tiltZ)
{
    // Rotate around Z (tiltX)
    float cosTX = cosf(tiltX), sinTX = sinf(tiltX);
    float y1 = local.y * cosTX - local.x * sinTX;
    float x1 = local.y * sinTX + local.x * cosTX;

    // Rotate around X (tiltZ)
    float cosTZ = cosf(tiltZ), sinTZ = sinf(tiltZ);
    float y2 = y1 * cosTZ - local.z * sinTZ;
    float z2 = y1 * sinTZ + local.z * cosTZ;

    // Rotate around Y (yaw)
    float cosY = cosf(yaw), sinY = sinf(yaw);
    float x3 = x1 * cosY - z2 * sinY;
    float z3 = x1 * sinY + z2 * cosY;

    return (Vector3){ x3, y2, z3 };
}

static void RebuildCastField(void) {
    ForceField_Clear(&s_castField);
    SkillForceMix_AddLayers(&s_castForce, &s_castField);
}
static void RebuildHoldField(void) {
    ForceField_Clear(&s_holdField);
    SkillForceMix_AddLayers(&s_holdForce, &s_holdField);
}

static void SpawnDustBurst(Vector3 pos, float scale)
{
    // s_castField is rebuilt at the top of UpdateWoodThornsSkill each frame
    // (this is only ever called from Update, after that rebuild).
    float baseRad = s_thornBaseRadius * scale;
    for (int i = 0; i < DUST_PARTICLES; i++) {
        float angle = (float)i / DUST_PARTICLES * 2.0f * PI;
        float outSpd = s_dustSpeedOutMin + (float)GetRandomValue(0, 10000) / 10000.0f *
                       (s_dustSpeedOutMax - s_dustSpeedOutMin);
        float upSpd  = s_dustSpeedUpMin  + (float)GetRandomValue(0, 10000) / 10000.0f *
                       (s_dustSpeedUpMax  - s_dustSpeedUpMin);
        Vector3 vel = {
            cosf(angle) * outSpd,
            upSpd,
            sinf(angle) * outSpd
        };
        Vector3 particlePos = {
            pos.x + cosf(angle) * baseRad,
            pos.y + 0.005f,
            pos.z + sinf(angle) * baseRad
        };
        float life = s_dustLifeMin + (float)GetRandomValue(0, 10000) / 10000.0f *
                     (s_dustLifeMax - s_dustLifeMin);
        float rad  = s_dustRadiusMin + (float)GetRandomValue(0, 10000) / 10000.0f *
                     (s_dustRadiusMax - s_dustRadiusMin);

        SpawnParticle((ParticleConfig){
            .position         = particlePos,
            .velocity         = vel,
            .colorStart       = ELEMENT_COLOR_WOOD,
            .colorEnd         = ColorAlpha(ColorLerp(ELEMENT_COLOR_WOOD, BLACK, 0.5f), 0.0f),
            .radius           = rad,
            .lifetime         = life,
            .forceField       = &s_castField,
            .gradient         = &s_dustGrad,
            .radiusCurve      = &s_castRadiusCurve,
            .speedCurve       = &s_castSpeedCurve,
            .alphaCurve       = &s_castAlphaCurve,
            .emissiveCurve    = &s_castEmissiveCurve,
            .spriteAnim       = NULL,
            .onDeathEmit      = NULL,
            .onDeathEmitCount = 0,
            .onLiveEmit       = NULL,
            .onLiveEmitRate   = 0.0f
        });
    }
}

/* ================================================================
 * § 4  LIFECYCLE — INIT
 * ================================================================ */
// CORE_ISSUES.md Item 13 — lets gameplay code know when this caster's
// root-zone thorns are truly gone.
static bool WoodThornsSkill_HasActiveInstance(int agentId)
{
    for (int i = 0; i < MAX_THORNS; i++) {
        if (s_thorns[i].state != THORN_INACTIVE && s_thorns[i].ownerAgentId == agentId)
            return true;
    }
    return false;
}

void InitWoodThornsSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth; (void)screenHeight;

    s_skillIndex = Skill_GetIndexByName("WOOD_THORNS");
    RegisterSkillLifecycleQuery(s_skillIndex, WoodThornsSkill_HasActiveInstance);

    for (int i = 0; i < MAX_THORNS; i++) {
        s_thorns[i].state = THORN_INACTIVE;
    }

    s_crackTex = ResourceManager_LoadTexture("assets/textures/crack.png");
    s_shader = ResourceManager_LoadShader("skills/wood/wood_thorns/wood_thorns.vs", "skills/wood/wood_thorns/wood_thorns.fs");
    s_uDissolveLoc = GetShaderLocation(s_shader, "u_dissolve");
    s_uTimeLoc     = GetShaderLocation(s_shader, "u_time");
    s_uCamPosLoc   = GetShaderLocation(s_shader, "u_camPos");

    // Green/Brown foliage gradient
    ColorGradient_AddStop(&s_dustGrad, 0.00f, ELEMENT_COLOR_WOOD);
    ColorGradient_AddStop(&s_dustGrad, 0.50f, ColorAlpha(ColorLerp(ELEMENT_COLOR_WOOD, BLACK, 0.3f), 0.7f));
    ColorGradient_AddStop(&s_dustGrad, 1.00f, (Color){ 0, 0, 0, 0 });

    // Seed curves flat (shaped in sandbox). A zero-initialized SkillCurve has
    // 0 stops and must be seeded before first use (core/skill_curve.h).
    SkillCurve_SetConstant(&s_castRadiusCurve,   1.0f);
    SkillCurve_SetConstant(&s_castSpeedCurve,    1.0f);
    SkillCurve_SetConstant(&s_castAlphaCurve,    1.0f);
    SkillCurve_SetConstant(&s_castEmissiveCurve, 1.0f);
    SkillCurve_SetConstant(&s_holdRadiusCurve,   1.0f);
    SkillCurve_SetConstant(&s_holdSpeedCurve,    1.0f);
    SkillCurve_SetConstant(&s_holdAlphaCurve,    1.0f);
    SkillCurve_SetConstant(&s_holdEmissiveCurve, 1.0f);

    // Register tunables (function-scope include sees s_tunables and tn).
    int tn = 0;
#include "wood_thorns_skill_tunables.inl"
    SkillTunables_LoadPersisted("skills/wood/wood_thorns/wood_thorns_skill.tuning", s_tunables, tn);
    RegisterSkillTunables(s_skillIndex, s_tunables, tn);

    // Build force fields once from (possibly persisted) tunables; Update
    // rebuilds them per frame while any thorn instance is live.
    RebuildCastField();
    RebuildHoldField();
}

/* ================================================================
 * § 5  LIFECYCLE — CAST
 * ================================================================ */
// Pass 3 — dispatch: this skill uses CAST_PATH_ANCHORED (multi-point drag path),
// not CAST_PATH_PROJECTILE. No dispatch changes needed.
void CastWoodThornsSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    if (!SkillManager_CanCast(s_skillIndex, agentId)) return;

    float spawnScale = (params.sizeScale > 0.0f) ? params.sizeScale : 1.0f;
    float baseDmg = Skill_CalculateDamage(SKILL_CAT_AOE_CONTROL, params);
    float baseKb = Skill_CalculateKnockback(SKILL_CAT_AOE_CONTROL, params);

    // Build the raw drag-cast path, falling back to a straight startPos->target
    // line if the caller didn't draw a multi-point path (CORE_API.md's
    // "Minimal Complete .c Skeleton (Anchored-Along-Path Skill)" pattern).
    Vector3 rawPath[33];
    int rawCount = 0;
    if (params.pathPointCount > 1) {
        for (int i = 0; i < params.pathPointCount && i < 32; i++) rawPath[rawCount++] = params.pathPoints[i];
    } else {
        Vector3 toTarget = { target.x - startPos.x, 0.0f, target.z - startPos.z };
        if (Vector3Length(toTarget) < 1.0f) return;
        rawPath[rawCount++] = startPos;
        rawPath[rawCount++] = target;
    }

    // Resample at tunable s_thornSpacing along the drawn path.
    Vector3 sampled[THORNS_PER_CAST];
    int sampledCount = SamplePath(rawPath, rawCount, s_thornSpacing, sampled, THORNS_PER_CAST);
    if (sampledCount <= 0) return;

    for (int i = 0; i < sampledCount; i++) {
        int slot = FindFreeSlot();
        if (slot < 0) break;

        Vector3 basePoint = sampled[i];
        Vector3 dir = (i + 1 < sampledCount)
            ? Vector3Normalize(Vector3Subtract(sampled[i + 1], sampled[i]))
            : ((i > 0) ? Vector3Normalize(Vector3Subtract(sampled[i], sampled[i - 1]))
                       : (Vector3){0, 0, 1});

        Vector3 perp = { -dir.z, 0.0f, dir.x }; // Perpendicular for jitter

        // Aesthetic Rule 1: Perpendicular spatial jitter
        float sideJitter = (float)GetRandomValue(-(int)(s_sideJitterMax * 100.0f),
                                                   (int)(s_sideJitterMax * 100.0f)) / 100.0f;
        Vector3 thornPos = {
            basePoint.x + perp.x * sideJitter,
            0.0f,
            basePoint.z + perp.z * sideJitter
        };

        // Aesthetic Rule 2: Random angle, rotation, and pitch/roll tilt
        float randomYaw = (float)GetRandomValue(0, 360) * (PI / 180.0f);
        float randomTiltX = (float)GetRandomValue(-10, 10) * (PI / 180.0f);
        float randomTiltZ = (float)GetRandomValue(-10, 10) * (PI / 180.0f);

        // Random scale variation
        float sizeJitter = (float)GetRandomValue(85, 115) / 100.0f;

        s_thorns[slot] = (Thorn){
            .pos           = thornPos,
            .delay         = (float)i * THORN_DELAY,
            .riseTimer     = 0.0f,
            .holdTimer     = THORN_HOLD_TIME,
            .dissolveTimer = 0.0f,
            .scale         = spawnScale * sizeJitter,
            .yaw           = randomYaw,
            .tiltX         = randomTiltX,
            .tiltZ         = randomTiltZ,
            .phase         = (float)GetRandomValue(0, 100) / 10.0f,
            .state         = THORN_WAITING,
            .dealtDamage   = false,
            .spawnedDecal  = false,
            .spawnedLight  = false,
            .damage        = baseDmg,
            .knockback     = baseKb,
            .ownerAgentId  = agentId
        };
    }

    SkillManager_TriggerCooldown(
        s_skillIndex, agentId,
        Skill_CalculateCooldown(SKILL_CAT_AOE_CONTROL, params));
}

/* ================================================================
 * § 6  LIFECYCLE — UPDATE
 * ================================================================ */
void UpdateWoodThornsSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    // Zero-instance early-out: skip per-frame field rebuilds when idle.
    bool anyActive = false;
    for (int i = 0; i < MAX_THORNS; i++) {
        if (s_thorns[i].state != THORN_INACTIVE) { anyActive = true; break; }
    }
    if (!anyActive) return;

    // Rebuild per-phase force fields from tunables each frame so sandbox
    // force tuning applies live to already-spawned particles.
    RebuildCastField();
    RebuildHoldField();

    for (int i = 0; i < MAX_THORNS; i++) {
        Thorn *th = &s_thorns[i];
        if (th->state == THORN_INACTIVE) continue;

        switch (th->state) {
        case THORN_WAITING:
            th->delay -= dt;
            if (th->delay <= 0.0f) {
                th->state     = THORN_RISING;
                th->riseTimer = 0.0f;

                SpawnDustBurst(th->pos, th->scale);
                // Pass 2 — ScreenDistort_Add: radius arg now in meters.
                ScreenDistort_Add(th->pos, s_distortRadius, 0.25f, 0.25f, 0.15f);
                if (s_shakeEnable > 0.5f) CameraFX_Shake(s_shakeTrauma);
            }
            break;

        case THORN_RISING:
            th->riseTimer += dt;

            // Decal emergence stamp (large ground-rupturing crack)
            if (!th->spawnedDecal) {
                // Pass 2 — DecalSystem_Add: scale arg in meters.
                DecalSystem_Add(
                    th->pos,
                    (float)GetRandomValue(0, 360),
                    s_thornBaseRadius * th->scale * s_decalScale,
                    s_crackTex,
                    5.0f,
                    ColorAlpha(ColorLerp(ELEMENT_COLOR_WOOD, BLACK, 0.4f), 0.8f)
                );
                th->spawnedDecal = true;
            }

            // VFX Light emergence flare — Pass 2: radius in meters.
            if (!th->spawnedLight) {
                VFXLight_Spawn(th->pos, LIME, s_lightRadius * th->scale, 2.5f, VFX_PRIORITY_LOW);
                th->spawnedLight = true;
            }

            // Collision check & gameplay effects at midpoint of animation
            if (!th->dealtDamage && th->riseTimer >= THORN_RISE_TIME * 0.5f) {
                float dx = enemyPos.x - th->pos.x;
                float dz = enemyPos.z - th->pos.z;
                float distSq = dx * dx + dz * dz;
                float dist   = sqrtf(distSq);
                // Pass 4 — distance-proportional floor/cap on hit radius.
                float hitRad = fminf(fmaxf(dist * 0.18f, s_aoeRadius * th->scale),
                                     dist * 0.6f);

                if (distSq <= hitRad * hitRad) {
                    char dmgStr[16];
                    snprintf(dmgStr, sizeof(dmgStr), "%d", (int)th->damage);
                    AddFloatingText(th->pos, dmgStr, ELEMENT_COLOR_WOOD, 24.0f, 0.7f);
                    AddFloatingText(th->pos, "ROOTED!", LIME, 16.0f, 0.8f);

                    // Root the enemy
                    AddRootToEnemy(1.2f);

                    // Knockback push direction away from center
                    Vector3 pushDir = { dx, 0.0f, dz };
                    if (dx == 0.0f && dz == 0.0f) {
                        pushDir = (Vector3){ 0.0f, 0.0f, 1.0f };
                    } else {
                        pushDir = Vector3Normalize(pushDir);
                    }
                    AddKnockbackToEnemy(Vector3Scale(pushDir, th->knockback));
                }
                th->dealtDamage = true;
            }

            if (th->riseTimer >= THORN_RISE_TIME) {
                th->state     = THORN_HOLDING;
                th->holdTimer = THORN_HOLD_TIME;
            }
            break;

        case THORN_HOLDING:
            th->holdTimer -= dt;

            // Emit glowing poison mist seeping from the body of the thorn mesh
            if (GetRandomValue(0, 100) < 25) {
                float hRatio = (float)GetRandomValue(0, 100) / 100.0f;
                float currentHeight = s_thornMaxHeight * th->scale;
                float currentRadius = s_thornBaseRadius * th->scale * (1.0f - hRatio);
                float angle = (float)GetRandomValue(0, 360) * DEG2RAD;

                Vector3 particlePos = {
                    th->pos.x + cosf(angle) * currentRadius * 0.8f,
                    th->pos.y + hRatio * currentHeight,
                    th->pos.z + sinf(angle) * currentRadius * 0.8f
                };

                float outSpd = s_mistSpeedOutMin + (float)GetRandomValue(0, 10000) / 10000.0f *
                               (s_mistSpeedOutMax - s_mistSpeedOutMin);
                float upSpd  = s_mistSpeedUpMin  + (float)GetRandomValue(0, 10000) / 10000.0f *
                               (s_mistSpeedUpMax  - s_mistSpeedUpMin);
                Vector3 vel = {
                    cosf(angle) * outSpd,
                    upSpd,
                    sinf(angle) * outSpd
                };
                float life = s_mistLifeMin + (float)GetRandomValue(0, 10000) / 10000.0f *
                             (s_mistLifeMax - s_mistLifeMin);
                float rad  = s_mistRadiusMin + (float)GetRandomValue(0, 10000) / 10000.0f *
                             (s_mistRadiusMax - s_mistRadiusMin);

                SpawnParticle((ParticleConfig){
                    .position         = particlePos,
                    .velocity         = vel,
                    .colorStart       = ColorAlpha(LIME, 0.95f),
                    .colorEnd         = ColorAlpha(GREEN, 0.0f),
                    .radius           = rad,
                    .lifetime         = life,
                    .forceField       = &s_holdField,
                    .gradient         = &s_dustGrad,
                    .radiusCurve      = &s_holdRadiusCurve,
                    .speedCurve       = &s_holdSpeedCurve,
                    .alphaCurve       = &s_holdAlphaCurve,
                    .emissiveCurve    = &s_holdEmissiveCurve,
                    .spriteAnim       = NULL,
                    .onDeathEmit      = NULL,
                    .onDeathEmitCount = 0,
                    .onLiveEmit       = NULL,
                    .onLiveEmitRate   = 0.0f
                });
            }

            if (th->holdTimer <= 0.0f) {
                th->state         = THORN_DISSOLVING;
                th->dissolveTimer = 0.0f;
            }
            break;

        case THORN_DISSOLVING:
            th->dissolveTimer += dt;
            if (th->dissolveTimer >= THORN_DISSOLVE_TIME) {
                th->state = THORN_INACTIVE;
            }
            break;

        default:
            break;
        }
    }
}

/* ================================================================
 * § 7  LIFECYCLE — DRAW (Organic procedural rlgl mesh)
 * ================================================================ */
void DrawWoodThornsSkill(void)
{
    float currentTime = (float)GetTime();

    for (int idx = 0; idx < MAX_THORNS; idx++) {
        const Thorn *th = &s_thorns[idx];
        if (th->state == THORN_INACTIVE || th->state == THORN_WAITING) continue;

        float heightRatio = 1.0f;
        float dissolveAmt = 0.0f;

        if (th->state == THORN_RISING) {
            float t = th->riseTimer / THORN_RISE_TIME;
            float inv = 1.0f - t;
            heightRatio = 1.0f - (inv * inv); // Quadratic ease-out
        } else if (th->state == THORN_DISSOLVING) {
            heightRatio = 1.0f;
            dissolveAmt = th->dissolveTimer / THORN_DISSOLVE_TIME;
        }

        float currentHeight = s_thornMaxHeight * th->scale * heightRatio;
        float baseRad = s_thornBaseRadius * th->scale;
        if (currentHeight < 0.005f) continue;

        // Aesthetic Rule 4: Bind shader during all active phases to prevent visual popping
        BeginShaderMode(s_shader);
        // CORE_ISSUES.md Item 11: raw BeginShaderMode() before an immediate-mode
        // rlgl draw never auto-uploads matModel like DrawMesh/DrawModel does.
        // Without this, a vertex shader normalizing (matModel * normal) reads a
        // zero-initialized matModel -> NaN normal. SkillManager_BeginShader()
        // fixes matModel (identity) + also binds u_time/viewPos/u_resolution.
        SkillManager_BeginShader(s_shader);
        SetShaderValue(s_shader, s_uDissolveLoc, &dissolveAmt, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_shader, s_uTimeLoc, &currentTime, SHADER_UNIFORM_FLOAT);

        SetShaderValue(s_shader, s_uCamPosLoc, &camera.position, SHADER_UNIFORM_VEC3);

        // Precompute mesh rings with noise perturbation for organic bark/thorn structure
        Vector3 rings[HEIGHT_SEGS + 1][RADIAL_SEGS];
        Vector3 normals[HEIGHT_SEGS + 1][RADIAL_SEGS];

        for (int h = 0; h <= HEIGHT_SEGS; h++) {
            float hRatio = (float)h / HEIGHT_SEGS;

            // Linear taper to tip
            float rad = baseRad * (1.0f - hRatio);

            for (int r = 0; r < RADIAL_SEGS; r++) {
                float angle = (float)r / RADIAL_SEGS * 2.0f * PI;

                // Aesthetic Rule 3: Procedural perturbation to make shapes rough & woody
                float wave = 1.0f + 0.16f * sinf(hRatio * 9.0f + angle * 3.0f + th->phase);
                float perturbedRad = rad * wave;

                Vector3 localPos = {
                    perturbedRad * cosf(angle),
                    hRatio * currentHeight,
                    perturbedRad * sinf(angle)
                };

                // Normal direction pointing outwards from center axis
                Vector3 localNormal = {
                    cosf(angle),
                    0.1f, // slight upward angle for normals
                    sinf(angle)
                };
                localNormal = Vector3Normalize(localNormal);

                // Rotate and tilt vertices locally
                rings[h][r] = Vector3Add(th->pos, RotateAndTilt(localPos, th->yaw, th->tiltX, th->tiltZ));
                normals[h][r] = RotateAndTilt(localNormal, th->yaw, th->tiltX, th->tiltZ);
            }
        }

        // Draw the organic thorn mesh using low-level rlgl
        rlPushMatrix();
        rlColor4ub(255, 255, 255, 255); // CRITICAL FIX: Reset vertex color to white
        rlCheckRenderBatchLimit(HEIGHT_SEGS * RADIAL_SEGS * 4);
        rlBegin(RL_QUADS);
        for (int h = 0; h < HEIGHT_SEGS; h++) {
            float v1 = (float)h / HEIGHT_SEGS;
            float v2 = (float)(h + 1) / HEIGHT_SEGS;

            for (int r = 0; r < RADIAL_SEGS; r++) {
                int nextR = (r + 1) % RADIAL_SEGS;
                float u1 = (float)r / RADIAL_SEGS;
                float u2 = (float)(r + 1) / RADIAL_SEGS;

                // Quad vertex definitions
                rlNormal3f(normals[h][r].x, normals[h][r].y, normals[h][r].z);
                rlTexCoord2f(u1, v1);
                rlVertex3f(rings[h][r].x, rings[h][r].y, rings[h][r].z);

                rlNormal3f(normals[h][nextR].x, normals[h][nextR].y, normals[h][nextR].z);
                rlTexCoord2f(u2, v1);
                rlVertex3f(rings[h][nextR].x, rings[h][nextR].y, rings[h][nextR].z);

                rlNormal3f(normals[h + 1][nextR].x, normals[h + 1][nextR].y, normals[h + 1][nextR].z);
                rlTexCoord2f(u2, v2);
                rlVertex3f(rings[h + 1][nextR].x, rings[h + 1][nextR].y, rings[h + 1][nextR].z);

                rlNormal3f(normals[h + 1][r].x, normals[h + 1][r].y, normals[h + 1][r].z);
                rlTexCoord2f(u1, v2);
                rlVertex3f(rings[h + 1][r].x, rings[h + 1][r].y, rings[h + 1][r].z);
            }
        }
        rlEnd();
        rlPopMatrix();

        SkillManager_EndShader();
        EndShaderMode();
    }
}

/* ================================================================
 * § 8  LIFECYCLE — UNLOAD
 * ================================================================ */
void UnloadWoodThornsSkill(void)
{
    /* Cached assets are freed by the global Resource Manager */
}

/* ================================================================
 * § 9  ENGINE CALLOUT RETRIEVERS
 * ================================================================ */
bool IsWoodThornsSkillCoiling(void)
{
    for (int i = 0; i < MAX_THORNS; i++) {
        if (s_thorns[i].state == THORN_RISING || s_thorns[i].state == THORN_HOLDING) {
            return true; // Roots/thorns are active, restricting movement
        }
    }
    return false;
}

int GetWoodThornsSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles)
{
    int count = 0;
    for (int i = 0; i < MAX_THORNS; i++) {
        if (s_thorns[i].state != THORN_INACTIVE && s_thorns[i].state != THORN_WAITING) {
            outProjectiles[count].position = s_thorns[i].pos;
            outProjectiles[count].radius = s_thornBaseRadius * s_thorns[i].scale;
            outProjectiles[count].active = true;
            count++;
            if (count >= maxProjectiles) break;
        }
    }
    return count;
}

void DeactivateWoodThornsProjectile(int index)
{
    int count = 0;
    for (int i = 0; i < MAX_THORNS; i++) {
        if (s_thorns[i].state != THORN_INACTIVE && s_thorns[i].state != THORN_WAITING) {
            if (count == index) {
                s_thorns[i].state = THORN_DISSOLVING;
                s_thorns[i].dissolveTimer = 0.0f;
                break;
            }
            count++;
        }
    }
}
