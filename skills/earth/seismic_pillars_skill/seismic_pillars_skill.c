#include "skills/earth/seismic_pillars_skill/seismic_pillars_skill.h"
#include "core/resource_manager.h"
#include "core/particle_system.h"
#include "core/color_gradient.h"
#include "core/decal_system.h"
#include "core/vfx_light.h"
#include "core/camera_fx.h"
#include "core/skill_helper.h"
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
#define MAX_PILLARS          64
#define PILLARS_PER_CAST     8

#define PILLAR_RISE_TIME     0.18f
#define PILLAR_HOLD_TIME     1.20f
#define PILLAR_DISSOLVE_TIME 0.40f

#define PILLAR_MAX_HEIGHT    38.0f
#define PILLAR_BASE_RADIUS   8.5f
#define RADIAL_SEGS          8  // Octagonal stone columns for clear geometric block lines
#define HEIGHT_SEGS          3  // 3 height segments for natural stone joints

typedef enum {
    PILLAR_INACTIVE = 0,
    PILLAR_WAITING,
    PILLAR_RISING,
    PILLAR_HOLDING,
    PILLAR_DISSOLVING
} PillarState;

typedef struct {
    Vector3     pos;
    Vector3     dir; // Direction of cast for knockback
    float       delay;
    float       riseTimer;
    float       holdTimer;
    float       dissolveTimer;
    float       scale;
    float       yaw;
    float       tiltX;
    float       tiltZ;
    float       maxHeight;
    float       baseRadius;
    PillarState state;
    bool        dealtDamage;
    bool        spawnedDecal;
    bool        spawnedLight;
    float       damage;
    float       knockback;
} GeoPillar;

/* ================================================================
 * § 2  STATIC STORAGE
 * ================================================================ */
static GeoPillar     s_pillars[MAX_PILLARS];
static Texture2D     s_crackTex;
static Shader        s_shader;
static int           s_uDissolveLoc;
static int           s_uTimeLoc;
static int           s_uCamPosLoc;
static ColorGradient s_dustGrad;

/* ================================================================
 * § 3  INTERNAL HELPERS
 * ================================================================ */
static int FindFreeSlot(void)
{
    for (int i = 0; i < MAX_PILLARS; i++) {
        if (s_pillars[i].state == PILLAR_INACTIVE) return i;
    }
    return -1;
}

static Vector3 RotateAndTilt(Vector3 local, float yaw, float tiltX, float tiltZ)
{
    float cosTX = cosf(tiltX), sinTX = sinf(tiltX);
    float y1 = local.y * cosTX - local.x * sinTX;
    float x1 = local.y * sinTX + local.x * cosTX;

    float cosTZ = cosf(tiltZ), sinTZ = sinf(tiltZ);
    float y2 = y1 * cosTZ - local.z * sinTZ;
    float z2 = y1 * sinTZ + local.z * cosTZ;

    float cosY = cosf(yaw), sinY = sinf(yaw);
    float x3 = x1 * cosY - z2 * sinY;
    float z3 = x1 * sinY + z2 * cosY;

    return (Vector3){ x3, y2, z3 };
}

static void SpawnDustBurst(Vector3 pos, float scale)
{
    float baseRad = PILLAR_BASE_RADIUS * scale;
    for (int i = 0; i < 15; i++) {
        float angle = (float)i / 15.0f * 2.0f * PI;
        Vector3 vel = {
            cosf(angle) * (float)GetRandomValue(30, 60),
            (float)GetRandomValue(40, 80),
            sinf(angle) * (float)GetRandomValue(30, 60)
        };
        Vector3 particlePos = {
            pos.x + cosf(angle) * baseRad,
            pos.y + 0.5f,
            pos.z + sinf(angle) * baseRad
        };

        SpawnParticle((ParticleConfig){
            .position         = particlePos,
            .velocity         = vel,
            .colorStart       = ColorAlpha(GOLD, 0.8f),
            .colorEnd         = ColorAlpha(DARKBROWN, 0.0f),
            .radius           = (float)GetRandomValue(15, 35) / 10.0f,
            .lifetime         = (float)GetRandomValue(6, 12) / 10.0f,
            .gradient         = &s_dustGrad
        });
    }
}

/* ================================================================
 * § 4  LIFECYCLE — INIT
 * ================================================================ */
void InitSeismicPillarsSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth; (void)screenHeight;

    for (int i = 0; i < MAX_PILLARS; i++) {
        s_pillars[i].state = PILLAR_INACTIVE;
    }

    s_crackTex = ResourceManager_LoadTexture("assets/textures/crack.png");
    s_shader = ResourceManager_LoadShader("skills/earth/seismic_pillars_skill/seismic_pillars.vs", "skills/earth/seismic_pillars_skill/seismic_pillars.fs");
    s_uDissolveLoc = GetShaderLocation(s_shader, "u_dissolve");
    s_uTimeLoc     = GetShaderLocation(s_shader, "u_time");
    s_uCamPosLoc   = GetShaderLocation(s_shader, "u_camPos");

    ColorGradient_AddStop(&s_dustGrad, 0.00f, WHITE);
    ColorGradient_AddStop(&s_dustGrad, 0.40f, GOLD);
    ColorGradient_AddStop(&s_dustGrad, 0.80f, DARKBROWN);
    ColorGradient_AddStop(&s_dustGrad, 1.00f, (Color){ 0, 0, 0, 0 });
}

/* ================================================================
 * § 5  LIFECYCLE — CAST
 * ================================================================ */
void CastSeismicPillarsSkill(Vector3 startPos, Vector3 target, SkillParams params)
{
    float spawnScale = (params.sizeScale > 0.0f) ? params.sizeScale : 1.0f;
    float baseDmg = Skill_CalculateDamage(SKILL_CAT_AOE_CONTROL, params);
    float baseKb = Skill_CalculateKnockback(SKILL_CAT_AOE_CONTROL, params);

    Vector3 dir = Vector3Subtract(target, startPos);
    dir.y = 0.0f;
    float totalDist = Vector3Length(dir);
    if (totalDist < 1.0f) {
        dir = (Vector3){ 0.0f, 0.0f, 1.0f };
        totalDist = 1.0f;
    } else {
        dir = Vector3Normalize(dir);
    }

    // Spawn pillars sequentially along the line from startPos to target
    float step = 15.0f * spawnScale;
    int count = (int)(totalDist / step) + 1;
    if (count < 4) count = 4;
    if (count > PILLARS_PER_CAST) count = PILLARS_PER_CAST;

    for (int i = 0; i < count; i++) {
        int slot = FindFreeSlot();
        if (slot < 0) break;

        // Position along the path
        float offset = (float)i * step;
        Vector3 pillarPos = {
            startPos.x + dir.x * offset,
            0.0f,
            startPos.z + dir.z * offset
        };

        // Delay increases based on distance index to create sequential wave eruption
        float delay = i * 0.08f;

        float randomYaw = (float)GetRandomValue(0, 360) * (PI / 180.0f);
        // Tilt slightly forward in the direction of the wave
        float tiltX = dir.z * 5.0f * (PI / 180.0f);
        float tiltZ = -dir.x * 5.0f * (PI / 180.0f);
        
        float sizeJitter = (float)GetRandomValue(85, 115) / 100.0f;
        float radJitter = (float)GetRandomValue(70, 130) / 100.0f;     // Bán kính dao động mạnh
        float heightJitter = (float)GetRandomValue(75, 145) / 100.0f;  // Chiều cao dao động mạnh

        s_pillars[slot] = (GeoPillar){
            .pos           = pillarPos,
            .dir           = dir,
            .delay         = delay,
            .riseTimer     = 0.0f,
            .holdTimer     = PILLAR_HOLD_TIME,
            .dissolveTimer = 0.0f,
            .scale         = spawnScale * sizeJitter,
            .yaw           = randomYaw,
            .tiltX         = tiltX + (float)GetRandomValue(-2, 2) * (PI / 180.0f),
            .tiltZ         = tiltZ + (float)GetRandomValue(-2, 2) * (PI / 180.0f),
            .maxHeight     = PILLAR_MAX_HEIGHT * spawnScale * sizeJitter * heightJitter,
            .baseRadius    = PILLAR_BASE_RADIUS * spawnScale * sizeJitter * radJitter,
            .state         = PILLAR_WAITING,
            .dealtDamage   = false,
            .spawnedDecal  = false,
            .spawnedLight  = false,
            .damage        = baseDmg,
            .knockback     = baseKb
        };
    }

    // Foot stamp impact at the beginning
    SpawnImpactEffect(startPos, EFFECT_PRESET_EARTH_CRACK, spawnScale * 0.8f);
}

/* ================================================================
 * § 6  LIFECYCLE — UPDATE
 * ================================================================ */
void UpdateSeismicPillarsSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    for (int i = 0; i < MAX_PILLARS; i++) {
        GeoPillar *c = &s_pillars[i];
        if (c->state == PILLAR_INACTIVE) continue;

        switch (c->state) {
        case PILLAR_WAITING:
            c->delay -= dt;
            if (c->delay <= 0.0f) {
                c->state     = PILLAR_RISING;
                c->riseTimer = 0.0f;
            }
            break;

        case PILLAR_RISING:
            c->riseTimer += dt;
            
            if (!c->spawnedDecal) {
                SpawnGroundDecal(DECAL_PRESET_CRACK, c->pos, c->baseRadius * 2.2f, 2.5f);
                SpawnDustBurst(c->pos, c->scale);
                
                // Add physical camera impulse when pillar hits the ground surface
                CameraFX_AddImpulse(c->pos, (CameraImpulse){
                    .magnitude = 0.16f * c->scale,
                    .duration = 0.22f,
                    .frequency = 14.0f,
                    .falloff = 0.6f
                });
                
                c->spawnedDecal = true;
            }

            if (!c->spawnedLight) {
                VFXLight_Spawn(c->pos, ORANGE, c->baseRadius * 5.0f, 1.0f);
                c->spawnedLight = true;
            }

            // Apply instant damage and Knockup when pillar erupts
            if (!c->dealtDamage && c->riseTimer >= PILLAR_RISE_TIME * 0.3f) {
                float dx = enemyPos.x - c->pos.x;
                float dz = enemyPos.z - c->pos.z;
                float distSq = dx * dx + dz * dz;
                float hitRad = (c->baseRadius + enemyRadius + 5.0f);

                if (distSq <= hitRad * hitRad) {
                    char dmgStr[16];
                    snprintf(dmgStr, sizeof(dmgStr), "%d", (int)c->damage);
                    AddFloatingText(c->pos, dmgStr, GOLD, 26.0f, 0.7f);
                    AddFloatingText(c->pos, "ELEVATE!", ORANGE, 18.0f, 0.8f);

                    // Launch enemy up and chéo chéo forward
                    Vector3 pushDir = { c->dir.x * 0.5f, 6.0f, c->dir.z * 0.5f };
                    pushDir = Vector3Normalize(pushDir);
                    AddKnockbackToEnemy(Vector3Scale(pushDir, c->knockback * 1.4f));
                }
                c->dealtDamage = true;
            }

            if (c->riseTimer >= PILLAR_RISE_TIME) {
                c->state     = PILLAR_HOLDING;
                c->holdTimer = PILLAR_HOLD_TIME;
            }
            break;

        case PILLAR_HOLDING:
            c->holdTimer -= dt;
            
            // Subtle dust sparkles around the column
            if (GetRandomValue(0, 100) < 8) {
                Vector3 particlePos = {
                    c->pos.x + (float)GetRandomValue(-8, 8) * 0.1f * c->scale,
                    c->pos.y + (float)GetRandomValue(1, (int)(c->maxHeight)),
                    c->pos.z + (float)GetRandomValue(-8, 8) * 0.1f * c->scale
                };
                
                SpawnParticle((ParticleConfig){
                    .position         = particlePos,
                    .velocity         = (Vector3){0, (float)GetRandomValue(5, 15), 0},
                    .colorStart       = ColorAlpha(GOLD, 0.6f),
                    .colorEnd         = ColorAlpha(DARKBROWN, 0.0f),
                    .radius           = (float)GetRandomValue(4, 10) / 10.0f,
                    .lifetime         = (float)GetRandomValue(5, 8) / 10.0f,
                    .gradient         = &s_dustGrad
                });
            }

            if (c->holdTimer <= 0.0f) {
                c->state         = PILLAR_DISSOLVING;
                c->dissolveTimer = 0.0f;
            }
            break;

        case PILLAR_DISSOLVING:
            c->dissolveTimer += dt;
            if (c->dissolveTimer >= PILLAR_DISSOLVE_TIME) {
                c->state = PILLAR_INACTIVE;
            }
            break;

        default:
            break;
        }
    }
}

/* ================================================================
 * § 7  LIFECYCLE — DRAW (Octagonal Flat Shading Pillar)
 * ================================================================ */
void DrawSeismicPillarsSkill(void)
{
    float currentTime = (float)GetTime();

    // CRITICAL: Bật Depth Test và Depth Mask tường minh để ngăn các cột đá bị vẽ chồng chéo xuyên thấu
    rlEnableDepthTest();
    rlEnableDepthMask();

    // Khởi động shader và bind texture đá
    BeginShaderMode(s_shader);
    
    extern Camera3D camera;
    float camPos[3] = { camera.position.x, camera.position.y, camera.position.z };
    SetShaderValue(s_shader, s_uCamPosLoc, camPos, SHADER_UNIFORM_VEC3);
    SetShaderValue(s_shader, s_uTimeLoc, &currentTime, SHADER_UNIFORM_FLOAT);

    rlActiveTextureSlot(0);
    rlEnableTexture(s_crackTex.id);

    for (int idx = 0; idx < MAX_PILLARS; idx++) {
        const GeoPillar *c = &s_pillars[idx];
        if (c->state == PILLAR_INACTIVE || c->state == PILLAR_WAITING) continue;

        float heightRatio = 1.0f;
        float dissolveAmt = 0.0f;

        if (c->state == PILLAR_RISING) {
            float t = c->riseTimer / PILLAR_RISE_TIME;
            float inv = 1.0f - t;
            heightRatio = 1.0f - (inv * inv * inv); // Cubic ease-out
        } else if (c->state == PILLAR_DISSOLVING) {
            heightRatio = 1.0f;
            dissolveAmt = c->dissolveTimer / PILLAR_DISSOLVE_TIME;
        }

        float currentHeight = c->maxHeight * heightRatio;
        float baseRad = c->baseRadius;
        if (currentHeight < 0.5f) continue;

        SetShaderValue(s_shader, s_uDissolveLoc, &dissolveAmt, SHADER_UNIFORM_FLOAT);

        // Khai báo mảng chứa các vòng tầng đỉnh của hình trụ tròn
        Vector3 rings[HEIGHT_SEGS + 1][RADIAL_SEGS];

        for (int h = 0; h <= HEIGHT_SEGS; h++) {
            float hRatio = (float)h / HEIGHT_SEGS;
            
            // Hình trụ tròn hoàn hảo: bán kính KHÔNG đổi từ đáy lên đỉnh!
            float rad = baseRad;

            for (int r = 0; r < RADIAL_SEGS; r++) {
                float angle = (float)r / RADIAL_SEGS * 2.0f * PI;

                // Thớ gập ghềnh nhấp nhô nhẹ trên đá để hiển thị rõ các mặt khối
                float noiseAmt = 0.05f * sinf(hRatio * 6.0f + angle * 2.0f + idx * 1.5f);
                float rRad = rad * (1.0f + noiseAmt);

                Vector3 locPos = {
                    rRad * cosf(angle),
                    hRatio * currentHeight,
                    rRad * sinf(angle)
                };

                rings[h][r] = Vector3Add(c->pos, RotateAndTilt(locPos, c->yaw, c->tiltX, c->tiltZ));
            }
        }

        // ĐẶT LẠI MÀU ĐỈNH LÀM SẠCH (Reset Vertex Colors)
        rlColor4ub(255, 255, 255, 255);

        rlPushMatrix();

        // 1. Vẽ các mặt xung quanh bằng Quads đa phân đoạn (FLAT SHADING)
        rlBegin(RL_QUADS);
        for (int h = 0; h < HEIGHT_SEGS; h++) {
            float v1 = (float)h / HEIGHT_SEGS;
            float v2 = (float)(h + 1) / HEIGHT_SEGS;

            for (int r = 0; r < RADIAL_SEGS; r++) {
                int nextR = (r + 1) % RADIAL_SEGS;
                float u1 = (float)r / RADIAL_SEGS;
                float u2 = (float)(r + 1) / RADIAL_SEGS;

                // Tính toán Face Normal
                Vector3 v0 = rings[h][r];
                Vector3 v1_pos = rings[h][nextR];
                Vector3 v2_pos = rings[h + 1][nextR];
                Vector3 edge1 = Vector3Subtract(v1_pos, v0);
                Vector3 edge2 = Vector3Subtract(v2_pos, v0);
                Vector3 faceNormal = Vector3Normalize(Vector3CrossProduct(edge1, edge2));

                rlNormal3f(faceNormal.x, faceNormal.y, faceNormal.z);

                rlTexCoord2f(u1, v1);
                rlVertex3f(rings[h][r].x, rings[h][r].y, rings[h][r].z);

                rlTexCoord2f(u2, v1);
                rlVertex3f(rings[h][nextR].x, rings[h][nextR].y, rings[h][nextR].z);

                rlTexCoord2f(u2, v2);
                rlVertex3f(rings[h + 1][nextR].x, rings[h + 1][nextR].y, rings[h + 1][nextR].z);

                rlTexCoord2f(u1, v2);
                rlVertex3f(rings[h + 1][r].x, rings[h + 1][r].y, rings[h + 1][r].z);
            }
        }
        rlEnd();
        
        // 2. Vẽ nắp phẳng trên cùng (FLAT SHADING bịt kín đầu tảng đá hình trụ)
        rlBegin(RL_TRIANGLES);
        Vector3 topCenter = Vector3Add(c->pos, RotateAndTilt((Vector3){0, currentHeight, 0}, c->yaw, c->tiltX, c->tiltZ));
        for (int r = 0; r < RADIAL_SEGS; r++) {
            int nextR = (r + 1) % RADIAL_SEGS;

            Vector3 vt0 = rings[HEIGHT_SEGS][r];
            Vector3 vt1 = rings[HEIGHT_SEGS][nextR];
            Vector3 vt2 = topCenter;
            Vector3 et1 = Vector3Subtract(vt1, vt0);
            Vector3 et2 = Vector3Subtract(vt2, vt0);
            Vector3 topFaceNormal = Vector3Normalize(Vector3CrossProduct(et1, et2));

            rlNormal3f(topFaceNormal.x, topFaceNormal.y, topFaceNormal.z);
            
            rlTexCoord2f((float)r/RADIAL_SEGS, 1.0f);
            rlVertex3f(rings[HEIGHT_SEGS][r].x, rings[HEIGHT_SEGS][r].y, rings[HEIGHT_SEGS][r].z);
            
            rlTexCoord2f((float)nextR/RADIAL_SEGS, 1.0f);
            rlVertex3f(rings[HEIGHT_SEGS][nextR].x, rings[HEIGHT_SEGS][nextR].y, rings[HEIGHT_SEGS][nextR].z);
            
            rlTexCoord2f(0.5f, 1.0f);
            rlVertex3f(topCenter.x, topCenter.y, topCenter.z);
        }
        rlEnd();

        // 3. Vẽ nắp phẳng đáy dưới cùng (FLAT SHADING bịt kín chân tảng đá hình trụ)
        rlBegin(RL_TRIANGLES);
        Vector3 bottomCenter = Vector3Add(c->pos, RotateAndTilt((Vector3){0, 0, 0}, c->yaw, c->tiltX, c->tiltZ));
        for (int r = 0; r < RADIAL_SEGS; r++) {
            int nextR = (r + 1) % RADIAL_SEGS;

            Vector3 vb0 = rings[0][r];
            Vector3 vb1 = rings[0][nextR];
            Vector3 vb2 = bottomCenter;
            
            // Winding order ngược lại cho đáy hướng xuống dưới
            Vector3 eb1 = Vector3Subtract(vb0, vb2);
            Vector3 eb2 = Vector3Subtract(vb1, vb2);
            Vector3 bottomFaceNormal = Vector3Normalize(Vector3CrossProduct(eb1, eb2));

            rlNormal3f(bottomFaceNormal.x, bottomFaceNormal.y, bottomFaceNormal.z);
            
            rlTexCoord2f((float)r/RADIAL_SEGS, 0.0f);
            rlVertex3f(rings[0][nextR].x, rings[0][nextR].y, rings[0][nextR].z);
            
            rlTexCoord2f((float)nextR/RADIAL_SEGS, 0.0f);
            rlVertex3f(rings[0][r].x, rings[0][r].y, rings[0][r].z);
            
            rlTexCoord2f(0.5f, 0.0f);
            rlVertex3f(bottomCenter.x, bottomCenter.y, bottomCenter.z);
        }
        rlEnd();
        
        rlPopMatrix();
    }

    EndShaderMode();
    rlDisableTexture();
}

/* ================================================================
 * § 8  LIFECYCLE — UNLOAD
 * ================================================================ */
void UnloadSeismicPillarsSkill(void)
{
}

/* ================================================================
 * § 9  ENGINE CALLOUT RETRIEVERS
 * ================================================================ */
bool IsSeismicPillarsSkillActive(void)
{
    for (int i = 0; i < MAX_PILLARS; i++) {
        if (s_pillars[i].state == PILLAR_RISING || s_pillars[i].state == PILLAR_HOLDING) {
            return true;
        }
    }
    return false;
}

int GetSeismicPillarsSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles)
{
    int count = 0;
    for (int i = 0; i < MAX_PILLARS; i++) {
        if (s_pillars[i].state != PILLAR_INACTIVE && s_pillars[i].state != PILLAR_WAITING) {
            outProjectiles[count].position = s_pillars[i].pos;
            outProjectiles[count].radius = PILLAR_BASE_RADIUS * s_pillars[i].scale;
            outProjectiles[count].active = true;
            count++;
            if (count >= maxProjectiles) break;
        }
    }
    return count;
}

void DeactivateSeismicPillarsProjectile(int index)
{
    int count = 0;
    for (int i = 0; i < MAX_PILLARS; i++) {
        if (s_pillars[i].state != PILLAR_INACTIVE && s_pillars[i].state != PILLAR_WAITING) {
            if (count == index) {
                s_pillars[i].state = PILLAR_DISSOLVING;
                s_pillars[i].dissolveTimer = 0.0f;
                break;
            }
            count++;
        }
    }
}
