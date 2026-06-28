#include "skills/earth/earth_shatter_skill/earth_shatter_skill.h"
#include "core/resource_manager.h"
#include "core/particle_system.h"
#include "core/color_gradient.h"
#include "core/decal_system.h"
#include "core/screen_distort.h"
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
#define MAX_CRYSTALS         64
#define CRYSTALS_PER_CAST    18

#define CRYSTAL_RISE_TIME    0.15f
#define CRYSTAL_HOLD_TIME    1.40f
#define CRYSTAL_DISSOLVE_TIME 0.50f

#define CRYSTAL_MAX_HEIGHT   24.0f
#define CRYSTAL_BASE_RADIUS  11.0f
#define RADIAL_SEGS          5  // Pentagonal sharp crystals
#define HEIGHT_SEGS          4  // Height segments for organic rocky look

#define AOE_RADIUS           40.0f
#define DUST_PARTICLES       30
#define SHAKE_TRAUMA         0.20f

typedef enum {
    CRYSTAL_INACTIVE = 0,
    CRYSTAL_WAITING,
    CRYSTAL_RISING,
    CRYSTAL_HOLDING,
    CRYSTAL_DISSOLVING
} CrystalState;

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
    CrystalState state;
    bool       dealtDamage;
    bool       spawnedDecal;
    bool       spawnedLight;
    float      damage;
    float      knockback;
} GeoCrystal;

/* ================================================================
 * § 2  STATIC STORAGE
 * ================================================================ */
static GeoCrystal    s_crystals[MAX_CRYSTALS];
static Texture2D     s_crackTex;
static Shader        s_shader;
static int           s_uDissolveLoc;
static int           s_uTimeLoc;
static int           s_uCamPosLoc;
static ColorGradient s_dustGrad;
static Vector3       s_lastCastCenter;

/* ================================================================
 * § 3  INTERNAL HELPERS
 * ================================================================ */
static int FindFreeSlot(void)
{
    for (int i = 0; i < MAX_CRYSTALS; i++) {
        if (s_crystals[i].state == CRYSTAL_INACTIVE) return i;
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

static void SpawnGoldDustBurst(Vector3 pos, float scale)
{
    float baseRad = CRYSTAL_BASE_RADIUS * scale * 2.0f;
    for (int i = 0; i < DUST_PARTICLES; i++) {
        float angle = (float)i / DUST_PARTICLES * 2.0f * PI;
        Vector3 vel = {
            cosf(angle) * (float)GetRandomValue(40, 90),
            (float)GetRandomValue(50, 110),
            sinf(angle) * (float)GetRandomValue(40, 90)
        };
        Vector3 particlePos = {
            pos.x + cosf(angle) * baseRad,
            pos.y + 1.0f,
            pos.z + sinf(angle) * baseRad
        };

        SpawnParticle((ParticleConfig){
            .position         = particlePos,
            .velocity         = vel,
            .colorStart       = ColorAlpha(GOLD, 1.0f),
            .colorEnd         = ColorAlpha(ORANGE, 0.0f),
            .radius           = (float)GetRandomValue(20, 50) / 10.0f,
            .lifetime         = (float)GetRandomValue(8, 15) / 10.0f,
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
void InitEarthShatterSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth; (void)screenHeight;

    for (int i = 0; i < MAX_CRYSTALS; i++) {
        s_crystals[i].state = CRYSTAL_INACTIVE;
    }

    s_crackTex = ResourceManager_LoadTexture("assets/textures/crack.png");
    s_shader = ResourceManager_LoadShader("skills/earth/earth_shatter_skill/earth_shatter.vs", "skills/earth/earth_shatter_skill/earth_shatter.fs");
    s_uDissolveLoc = GetShaderLocation(s_shader, "u_dissolve");
    s_uTimeLoc     = GetShaderLocation(s_shader, "u_time");
    s_uCamPosLoc   = GetShaderLocation(s_shader, "u_camPos");

    // Earth/Gold gradient for dust
    ColorGradient_AddStop(&s_dustGrad, 0.00f, WHITE);
    ColorGradient_AddStop(&s_dustGrad, 0.30f, GOLD);
    ColorGradient_AddStop(&s_dustGrad, 0.70f, DARKBROWN);
    ColorGradient_AddStop(&s_dustGrad, 1.00f, (Color){ 0, 0, 0, 0 });
}

/* ================================================================
 * § 5  LIFECYCLE — CAST
 * ================================================================ */
void CastEarthShatterSkill(Vector3 startPos, Vector3 target, SkillParams params)
{
    s_lastCastCenter = target;
    
    float spawnScale = (params.sizeScale > 0.0f) ? params.sizeScale : 1.0f;
    float baseDmg = Skill_CalculateDamage(SKILL_CAT_AOE_CONTROL, params);
    float baseKb = Skill_CalculateKnockback(SKILL_CAT_AOE_CONTROL, params);

    // Erupt in concentric rings
    int rings = 3;
    int crystalsPerRing[] = {1, 6, 11};
    float ringRadii[] = {0.0f, 15.0f, 28.0f};
    
    int spawned = 0;
    for (int r = 0; r < rings; r++) {
        int count = crystalsPerRing[r];
        for (int i = 0; i < count; i++) {
            if (spawned >= CRYSTALS_PER_CAST) break;
            int slot = FindFreeSlot();
            if (slot < 0) break;

            float angle = (float)i / count * 2.0f * PI + (r * 0.5f);
            float radius = ringRadii[r] * spawnScale;
            float jitterX = (float)GetRandomValue(-40, 40) / 10.0f;
            float jitterZ = (float)GetRandomValue(-40, 40) / 10.0f;

            Vector3 crystalPos = {
                target.x + cosf(angle) * radius + jitterX,
                0.0f,
                target.z + sinf(angle) * radius + jitterZ
            };

            // Outer rings rise slightly later for a ripple effect
            float delay = r * 0.06f + (float)GetRandomValue(0, 3) * 0.01f;

            float randomYaw = (float)GetRandomValue(0, 360) * (PI / 180.0f);
            // Outer crystals tilt outwards
            float tiltOut = r * 15.0f * (PI / 180.0f); 
            float tiltX = sinf(angle) * tiltOut + (float)GetRandomValue(-5, 5) * (PI / 180.0f);
            float tiltZ = -cosf(angle) * tiltOut + (float)GetRandomValue(-5, 5) * (PI / 180.0f);
            
            float sizeJitter = (float)GetRandomValue(70, 130) / 100.0f;
            // Center is biggest
            if (r == 0) sizeJitter *= 1.6f;

            s_crystals[slot] = (GeoCrystal){
                .pos           = crystalPos,
                .delay         = delay,
                .riseTimer     = 0.0f,
                .holdTimer     = CRYSTAL_HOLD_TIME,
                .dissolveTimer = 0.0f,
                .scale         = spawnScale * sizeJitter,
                .yaw           = randomYaw,
                .tiltX         = tiltX,
                .tiltZ         = tiltZ,
                .state         = CRYSTAL_WAITING,
                .dealtDamage   = false,
                .spawnedDecal  = false,
                .spawnedLight  = false,
                .damage        = baseDmg,
                .knockback     = baseKb
            };
            spawned++;
        }
    }
    
    // Add central big decal
    SpawnGroundDecal(DECAL_PRESET_CRACK, target, 70.0f * spawnScale, 3.0f);
    
    SpawnGoldDustBurst(target, spawnScale * 1.5f);
    SpawnImpactEffect(target, EFFECT_PRESET_EARTH_CRACK, spawnScale);
}

/* ================================================================
 * § 6  LIFECYCLE — UPDATE
 * ================================================================ */
void UpdateEarthShatterSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    for (int i = 0; i < MAX_CRYSTALS; i++) {
        GeoCrystal *c = &s_crystals[i];
        if (c->state == CRYSTAL_INACTIVE) continue;

        switch (c->state) {
        case CRYSTAL_WAITING:
            c->delay -= dt;
            if (c->delay <= 0.0f) {
                c->state     = CRYSTAL_RISING;
                c->riseTimer = 0.0f;
            }
            break;

        case CRYSTAL_RISING:
            c->riseTimer += dt;
            
            if (!c->spawnedDecal) {
                SpawnGroundDecal(DECAL_PRESET_CRACK, c->pos, 25.0f * c->scale, 1.5f);
                c->spawnedDecal = true;
            }

            if (!c->spawnedLight) {
                VFXLight_Spawn(c->pos, GOLD, 80.0f * c->scale, 1.8f);
                c->spawnedLight = true;
            }

            if (!c->dealtDamage && c->riseTimer >= CRYSTAL_RISE_TIME * 0.4f) {
                float dx = enemyPos.x - c->pos.x;
                float dz = enemyPos.z - c->pos.z;
                float distSq = dx * dx + dz * dz;
                float hitRad = (15.0f + enemyRadius) * c->scale;

                if (distSq <= hitRad * hitRad) {
                    char dmgStr[16];
                    snprintf(dmgStr, sizeof(dmgStr), "%d", (int)c->damage);
                    AddFloatingText(c->pos, dmgStr, GOLD, 26.0f, 0.7f);
                    AddFloatingText(c->pos, "SHATTER!", ORANGE, 18.0f, 0.8f);

                    // Launch enemy up (Knockup)
                    Vector3 pushDir = { dx, 4.0f, dz }; // Add strong upward force
                    pushDir = Vector3Normalize(pushDir);
                    AddKnockbackToEnemy(Vector3Scale(pushDir, c->knockback * 1.5f));
                }
                c->dealtDamage = true;
            }

            if (c->riseTimer >= CRYSTAL_RISE_TIME) {
                c->state     = CRYSTAL_HOLDING;
                c->holdTimer = CRYSTAL_HOLD_TIME;
            }
            break;

        case CRYSTAL_HOLDING:
            c->holdTimer -= dt;
            
            // Sparkle particles
            if (GetRandomValue(0, 100) < 15) {
                Vector3 particlePos = {
                    c->pos.x + (float)GetRandomValue(-10, 10) * 0.1f * c->scale,
                    c->pos.y + (float)GetRandomValue(5, (int)(CRYSTAL_MAX_HEIGHT * c->scale)),
                    c->pos.z + (float)GetRandomValue(-10, 10) * 0.1f * c->scale
                };
                
                SpawnParticle((ParticleConfig){
                    .position         = particlePos,
                    .velocity         = (Vector3){0, (float)GetRandomValue(10, 25), 0},
                    .colorStart       = ColorAlpha(WHITE, 1.0f),
                    .colorEnd         = ColorAlpha(GOLD, 0.0f),
                    .radius           = (float)GetRandomValue(5, 12) / 10.0f,
                    .lifetime         = (float)GetRandomValue(5, 10) / 10.0f,
                    .gradient         = &s_dustGrad,
                });
            }

            if (c->holdTimer <= 0.0f) {
                c->state         = CRYSTAL_DISSOLVING;
                c->dissolveTimer = 0.0f;
            }
            break;

        case CRYSTAL_DISSOLVING:
            c->dissolveTimer += dt;
            if (c->dissolveTimer >= CRYSTAL_DISSOLVE_TIME) {
                c->state = CRYSTAL_INACTIVE;
            }
            break;

        default:
            break;
        }
    }
}

/* ================================================================
 * § 7  LIFECYCLE — DRAW 
 * ================================================================ */
void DrawEarthShatterSkill(void)
{
    float currentTime = (float)GetTime();

    // CRITICAL: Bật lại Depth Test và Depth Mask tường minh để ngăn hiện tượng vẽ đè xuyên thấu do kỹ năng trước tắt
    rlEnableDepthTest();
    rlEnableDepthMask();

    // Bắt đầu Shader Mode và Bind Texture đá một lần ngoài vòng lặp để tối ưu hiệu năng cực đại
    BeginShaderMode(s_shader);
    
    // Truyền toạ độ camera cho Shading 3D ánh sáng
    extern Camera3D camera;
    float camPos[3] = { camera.position.x, camera.position.y, camera.position.z };
    SetShaderValue(s_shader, s_uCamPosLoc, camPos, SHADER_UNIFORM_VEC3);
    SetShaderValue(s_shader, s_uTimeLoc, &currentTime, SHADER_UNIFORM_FLOAT);

    rlActiveTextureSlot(0);
    rlEnableTexture(s_crackTex.id);

    for (int idx = 0; idx < MAX_CRYSTALS; idx++) {
        const GeoCrystal *c = &s_crystals[idx];
        if (c->state == CRYSTAL_INACTIVE || c->state == CRYSTAL_WAITING) continue;

        float heightRatio = 1.0f;
        float dissolveAmt = 0.0f;

        if (c->state == CRYSTAL_RISING) {
            float t = c->riseTimer / CRYSTAL_RISE_TIME;
            float inv = 1.0f - t;
            // Elastic pop-up effect
            heightRatio = 1.0f - (inv * inv * inv); 
        } else if (c->state == CRYSTAL_DISSOLVING) {
            heightRatio = 1.0f;
            dissolveAmt = c->dissolveTimer / CRYSTAL_DISSOLVE_TIME;
        }

        float currentHeight = CRYSTAL_MAX_HEIGHT * c->scale * heightRatio;
        float baseRad = CRYSTAL_BASE_RADIUS * c->scale;
        if (currentHeight < 0.5f) continue;

        SetShaderValue(s_shader, s_uDissolveLoc, &dissolveAmt, SHADER_UNIFORM_FLOAT);

        // Khai báo mảng chứa các vòng tầng đỉnh giống wood_thorns
        Vector3 rings[HEIGHT_SEGS + 1][RADIAL_SEGS];

        for (int h = 0; h <= HEIGHT_SEGS; h++) {
            float hRatio = (float)h / HEIGHT_SEGS;
            
            // Taper: thuôn dần lên trên, giữ lại 26% bán kính ở đỉnh để đá có khối cụt dẹt
            float rad = baseRad * (1.0f - hRatio * 0.74f);

            for (int r = 0; r < RADIAL_SEGS; r++) {
                float angle = (float)r / RADIAL_SEGS * 2.0f * PI;

                // Tạo nếp đá gập ghềnh nhẹ (nhám tinh thể) tránh làm méo vặn tự cắt nhau của mesh
                float noiseAmt = 0.08f * sinf(hRatio * 6.0f + angle * 2.0f + idx * 1.3f);
                float rRad = rad * (1.0f + noiseAmt);

                // Gai đá mọc thẳng đứng sừng sững, không dịch tâm xiên vẹo kỳ dị
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

        // Vẽ các mặt xung quanh bằng Quads đa tầng nối tiếp (Áp dụng FLAT SHADING để lộ rõ góc cạnh)
        rlPushMatrix();
        rlBegin(RL_QUADS);
        for (int h = 0; h < HEIGHT_SEGS; h++) {
            float v1 = (float)h / HEIGHT_SEGS;
            float v2 = (float)(h + 1) / HEIGHT_SEGS;

            for (int r = 0; r < RADIAL_SEGS; r++) {
                int nextR = (r + 1) % RADIAL_SEGS;
                float u1 = (float)r / RADIAL_SEGS;
                float u2 = (float)(r + 1) / RADIAL_SEGS;

                // Tính Face Normal cho mặt Quad phẳng
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
        
        // Vẽ nắp chóp nhọn trên cùng (bịt đầu đá tảng - Áp dụng FLAT SHADING đồng tâm tuyệt đối)
        rlBegin(RL_TRIANGLES);
        Vector3 peak = Vector3Add(c->pos, RotateAndTilt((Vector3){0, currentHeight, 0}, c->yaw, c->tiltX, c->tiltZ));
        for (int r = 0; r < RADIAL_SEGS; r++) {
            int nextR = (r + 1) % RADIAL_SEGS;

            // Tính Face Normal cho mặt tam giác
            Vector3 vt0 = rings[HEIGHT_SEGS][r];
            Vector3 vt1 = rings[HEIGHT_SEGS][nextR];
            Vector3 vt2 = peak;
            Vector3 et1 = Vector3Subtract(vt1, vt0);
            Vector3 et2 = Vector3Subtract(vt2, vt0);
            Vector3 topFaceNormal = Vector3Normalize(Vector3CrossProduct(et1, et2));

            rlNormal3f(topFaceNormal.x, topFaceNormal.y, topFaceNormal.z);
            
            rlTexCoord2f((float)r/RADIAL_SEGS, 1.0f);
            rlVertex3f(rings[HEIGHT_SEGS][r].x, rings[HEIGHT_SEGS][r].y, rings[HEIGHT_SEGS][r].z);
            
            rlTexCoord2f((float)nextR/RADIAL_SEGS, 1.0f);
            rlVertex3f(rings[HEIGHT_SEGS][nextR].x, rings[HEIGHT_SEGS][nextR].y, rings[HEIGHT_SEGS][nextR].z);
            
            rlTexCoord2f(0.5f, 1.0f);
            rlVertex3f(peak.x, peak.y, peak.z);
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
void UnloadEarthShatterSkill(void)
{
}

/* ================================================================
 * § 9  ENGINE CALLOUT RETRIEVERS
 * ================================================================ */
bool IsEarthShatterSkillActive(void)
{
    for (int i = 0; i < MAX_CRYSTALS; i++) {
        if (s_crystals[i].state == CRYSTAL_RISING || s_crystals[i].state == CRYSTAL_HOLDING) {
            return true;
        }
    }
    return false;
}

int GetEarthShatterSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles)
{
    int count = 0;
    for (int i = 0; i < MAX_CRYSTALS; i++) {
        if (s_crystals[i].state != CRYSTAL_INACTIVE && s_crystals[i].state != CRYSTAL_WAITING) {
            outProjectiles[count].position = s_crystals[i].pos;
            outProjectiles[count].radius = CRYSTAL_BASE_RADIUS * s_crystals[i].scale;
            outProjectiles[count].active = true;
            count++;
            if (count >= maxProjectiles) break;
        }
    }
    return count;
}

void DeactivateEarthShatterProjectile(int index)
{
    int count = 0;
    for (int i = 0; i < MAX_CRYSTALS; i++) {
        if (s_crystals[i].state != CRYSTAL_INACTIVE && s_crystals[i].state != CRYSTAL_WAITING) {
            if (count == index) {
                s_crystals[i].state = CRYSTAL_DISSOLVING;
                s_crystals[i].dissolveTimer = 0.0f;
                break;
            }
            count++;
        }
    }
}
