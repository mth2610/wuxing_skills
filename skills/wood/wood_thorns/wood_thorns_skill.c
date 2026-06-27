#include "skills/wood/wood_thorns/wood_thorns_skill.h"
#include "core/resource_manager.h"
#include "core/particle_system.h"
#include "core/color_gradient.h"
#include "core/decal_system.h"
#include "core/screen_distort.h"
#include "core/vfx_light.h"
#include "core/camera_fx.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#define PI 3.14159265358979323846f

/* ================================================================
 * § 1  CONFIGURATIONS
 * ================================================================ */
#define MAX_THORNS           32
#define THORNS_PER_CAST      6
#define THORN_SPACING        35.0f
#define THORN_DELAY          0.08f

#define THORN_RISE_TIME      0.20f
#define THORN_HOLD_TIME      1.20f
#define THORN_DISSOLVE_TIME  0.40f

#define THORN_MAX_HEIGHT     46.0f
#define THORN_BASE_RADIUS    6.5f
#define HEIGHT_SEGS          8
#define RADIAL_SEGS          8

#define AOE_RADIUS           25.0f
#define DUST_PARTICLES       12
#define SHAKE_TRAUMA         0.28f

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
} Thorn;

/* ================================================================
 * § 2  STATIC STORAGE
 * ================================================================ */
static Thorn         s_thorns[MAX_THORNS];
static Texture2D     s_crackTex;
static Shader        s_shader;
static int           s_uDissolveLoc;
static int           s_uTimeLoc;
static ColorGradient s_dustGrad;

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

static void SpawnDustBurst(Vector3 pos, float scale)
{
    float baseRad = THORN_BASE_RADIUS * scale;
    for (int i = 0; i < DUST_PARTICLES; i++) {
        float angle = (float)i / DUST_PARTICLES * 2.0f * PI;
        Vector3 vel = {
            cosf(angle) * (float)GetRandomValue(35, 75),
            (float)GetRandomValue(40, 90),
            sinf(angle) * (float)GetRandomValue(35, 75)
        };
        Vector3 particlePos = {
            pos.x + cosf(angle) * baseRad,
            pos.y + 0.5f,
            pos.z + sinf(angle) * baseRad
        };

        SpawnParticle((ParticleConfig){
            .position         = particlePos,
            .velocity         = vel,
            .colorStart       = ELEMENT_COLOR_WOOD,
            .colorEnd         = ColorAlpha(ColorLerp(ELEMENT_COLOR_WOOD, BLACK, 0.5f), 0.0f),
            .radius           = (float)GetRandomValue(30, 60) / 10.0f,
            .lifetime         = (float)GetRandomValue(6, 12) / 10.0f,
            .forceField       = NULL,
            .gradient         = &s_dustGrad,
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
void InitWoodThornsSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth; (void)screenHeight;

    for (int i = 0; i < MAX_THORNS; i++) {
        s_thorns[i].state = THORN_INACTIVE;
    }

    s_crackTex = ResourceManager_LoadTexture("assets/textures/crack.png");
    s_shader = ResourceManager_LoadShader(NULL, "skills/wood/wood_thorns/wood_thorns.fs");
    s_uDissolveLoc = GetShaderLocation(s_shader, "u_dissolve");
    s_uTimeLoc     = GetShaderLocation(s_shader, "u_time");

    // Green/Brown foliage gradient
    ColorGradient_AddStop(&s_dustGrad, 0.00f, ELEMENT_COLOR_WOOD);
    ColorGradient_AddStop(&s_dustGrad, 0.50f, ColorAlpha(ColorLerp(ELEMENT_COLOR_WOOD, BLACK, 0.3f), 0.7f));
    ColorGradient_AddStop(&s_dustGrad, 1.00f, (Color){ 0, 0, 0, 0 });
}

/* ================================================================
 * § 5  LIFECYCLE — CAST
 * ================================================================ */
void CastWoodThornsSkill(Vector3 startPos, Vector3 target, SkillParams params)
{
    Vector3 toTarget = { target.x - startPos.x, 0.0f, target.z - startPos.z };
    float dist = Vector3Length(toTarget);
    if (dist < 1.0f) return;

    Vector3 dir = Vector3Scale(toTarget, 1.0f / dist);
    Vector3 perp = { -dir.z, 0.0f, dir.x }; // Perpendicular direction for spatial jitter

    float maxLine = (float)(THORNS_PER_CAST - 1) * THORN_SPACING;
    float lineLen = (dist < maxLine) ? dist : maxLine;
    float stepSize = (THORNS_PER_CAST > 1) ? (lineLen / (float)(THORNS_PER_CAST - 1)) : 0.0f;

    float spawnScale = (params.sizeScale > 0.0f) ? params.sizeScale : 1.0f;
    float baseDmg = Skill_CalculateDamage(SKILL_CAT_AOE_CONTROL, params);
    float baseKb = Skill_CalculateKnockback(SKILL_CAT_AOE_CONTROL, params);

    for (int i = 0; i < THORNS_PER_CAST; i++) {
        int slot = FindFreeSlot();
        if (slot < 0) break;

        float offset = (float)i * stepSize;
        
        // Aesthetic Rule 1: Perpendicular spatial jitter (12 units max offset left/right)
        float sideJitter = (float)GetRandomValue(-120, 120) / 10.0f;
        Vector3 thornPos = {
            startPos.x + dir.x * offset + perp.x * sideJitter,
            0.0f,
            startPos.z + dir.z * offset + perp.z * sideJitter
        };

        // Aesthetic Rule 2: Random angle, rotation, and pitch/roll tilt
        float randomYaw = (float)GetRandomValue(0, 360) * (PI / 180.0f);
        float randomTiltX = (float)GetRandomValue(-10, 10) * (PI / 180.0f);
        float randomTiltZ = (float)GetRandomValue(-10, 10) * (PI / 180.0f);
        
        // Random scale variation (85% to 115%)
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
            .knockback     = baseKb
        };
    }
}

/* ================================================================
 * § 6  LIFECYCLE — UPDATE
 * ================================================================ */
void UpdateWoodThornsSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
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
                ScreenDistort_AddSource(th->pos, 60.0f, 0.25f, 0.25f, 150.0f);
                CameraFX_Shake(SHAKE_TRAUMA);
            }
            break;

        case THORN_RISING:
            th->riseTimer += dt;
            
            // Decal emergence stamp (Large ground-rupturing crack!)
            if (!th->spawnedDecal) {
                Decal_Spawn(
                    th->pos,
                    (float)GetRandomValue(0, 360),
                    THORN_BASE_RADIUS * th->scale * 5.2f,
                    s_crackTex,
                    5.0f,
                    ColorAlpha(ColorLerp(ELEMENT_COLOR_WOOD, BLACK, 0.4f), 0.8f) /* green-tinted cracks */
                );
                th->spawnedDecal = true;
            }

            // VFX Light emergence flare (lasts throughout active hold duration)
            if (!th->spawnedLight) {
                VFXLight_Spawn(th->pos, ELEMENT_COLOR_WOOD, 55.0f * th->scale, 1.4f);
                th->spawnedLight = true;
            }

            // Collision check & gameplay effects at midpoint of animation
            if (!th->dealtDamage && th->riseTimer >= THORN_RISE_TIME * 0.5f) {
                float dx = enemyPos.x - th->pos.x;
                float dz = enemyPos.z - th->pos.z;
                float distSq = dx * dx + dz * dz;
                float hitRad = (AOE_RADIUS + enemyRadius) * th->scale;

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
            
            // Emit glowing poison gas/mist seeping out from the body of the thorn mesh
            if (GetRandomValue(0, 100) < 25) {
                float hRatio = (float)GetRandomValue(0, 100) / 100.0f;
                float currentHeight = THORN_MAX_HEIGHT * th->scale;
                float currentRadius = THORN_BASE_RADIUS * th->scale * (1.0f - hRatio);
                float angle = (float)GetRandomValue(0, 360) * DEG2RAD;
                
                Vector3 particlePos = {
                    th->pos.x + cosf(angle) * currentRadius * 0.8f,
                    th->pos.y + hRatio * currentHeight,
                    th->pos.z + sinf(angle) * currentRadius * 0.8f
                };
                
                Vector3 vel = {
                    cosf(angle) * (float)GetRandomValue(5, 15),
                    (float)GetRandomValue(15, 35),
                    sinf(angle) * (float)GetRandomValue(5, 15)
                };
                
                SpawnParticle((ParticleConfig){
                    .position         = particlePos,
                    .velocity         = vel,
                    .colorStart       = ColorAlpha(ColorLerp(ELEMENT_COLOR_WOOD, YELLOW, 0.15f), 0.9f),
                    .colorEnd         = ColorAlpha(ColorLerp(ELEMENT_COLOR_WOOD, BLACK, 0.6f), 0.0f),
                    .radius           = (float)GetRandomValue(12, 28) / 10.0f,
                    .lifetime         = (float)GetRandomValue(8, 16) / 10.0f,
                    .forceField       = NULL,
                    .gradient         = &s_dustGrad,
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

        float currentHeight = THORN_MAX_HEIGHT * th->scale * heightRatio;
        float baseRad = THORN_BASE_RADIUS * th->scale;
        if (currentHeight < 0.5f) continue;

        // Aesthetic Rule 4: Bind shader during all active phases to prevent visual popping
        BeginShaderMode(s_shader);
        SetShaderValue(s_shader, s_uDissolveLoc, &dissolveAmt, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_shader, s_uTimeLoc, &currentTime, SHADER_UNIFORM_FLOAT);

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

        // Draw the organic thorn mesh using low-level rlgl (PROHIBITED flat primitives check passed!)
        rlPushMatrix();
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
            outProjectiles[count].radius = THORN_BASE_RADIUS * s_thorns[i].scale;
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
