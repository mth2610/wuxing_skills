#include "skills/earth/stone_prison_skill/stone_prison_skill.h"
#include "core/resource_manager.h"
#include "core/particle_system.h"
#include "core/color_gradient.h"
#include "core/decal_system.h"
#include "core/screen_distort.h"
#include "core/vfx_light.h"
#include "core/camera_fx.h"
#include "core/force_field.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#define PI                  3.14159265358979323846f
#define MAX_PRISONS         2
#define PILLARS_COUNT       6
#define HEIGHT_SEGS         8
#define RADIAL_SEGS         8

#define CAST_TIME           1.2f
#define RISE_TIME           0.2f
#define HOLD_TIME           1.5f
#define DISSOLVE_TIME       0.4f

#define PILLAR_HEIGHT       56.0f
#define PILLAR_RADIUS       8.5f

typedef enum {
    STATE_INACTIVE = 0,
    STATE_CASTING,   // Warning phase, dust gathering
    STATE_RISING,    // Pillars erupting rapidly
    STATE_HOLDING,   // Trapped, enemy rooted
    STATE_EXPLODING, // Explodes, deals damage, collapses
    STATE_FADING     // Dissolving away
} PrisonState;

typedef struct {
    Vector3     pos; // target position
    PrisonState state;
    float       timer;
    float       scale;
    float       yaw;
    bool        dealtDamage;
    bool        spawnedDecal;
    float       damage;
    float       knockback;
    float       particleAccum;
} EarthPrison;

static EarthPrison   s_prisons[MAX_PRISONS];
static Texture2D     s_crackTex;
static Texture2D     s_noiseTex;
static Shader        s_shader;
static Shader        s_crackShader;
static int           s_uTimeLoc, s_uDissolveLoc, s_uCamPosLoc;
static int           s_uProgressLoc, s_uCrackTimeLoc;
static ColorGradient s_dustGrad;

void InitStonePrisonSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth; (void)screenHeight;

    for (int i = 0; i < MAX_PRISONS; i++) {
        s_prisons[i].state = STATE_INACTIVE;
    }

    s_crackTex = ResourceManager_LoadTexture("assets/textures/crack.png");
    s_noiseTex = ResourceManager_LoadTexture("assets/textures/noise.png");
    
    s_shader = ResourceManager_LoadShader("skills/earth/stone_prison_skill/stone_prison.vs", "skills/earth/stone_prison_skill/stone_prison.fs");
    s_uDissolveLoc = GetShaderLocation(s_shader, "u_dissolve");
    s_uTimeLoc     = GetShaderLocation(s_shader, "u_time");
    s_uCamPosLoc   = GetShaderLocation(s_shader, "u_camPos");

    s_crackShader = ResourceManager_LoadShader(NULL, "skills/earth/stone_prison_skill/stone_crack.fs");
    s_uProgressLoc   = GetShaderLocation(s_crackShader, "u_progress");
    s_uCrackTimeLoc  = GetShaderLocation(s_crackShader, "u_time");

    // Warm earth/amber gradient for particles
    ColorGradient_AddStop(&s_dustGrad, 0.00f, ELEMENT_COLOR_EARTH);
    ColorGradient_AddStop(&s_dustGrad, 0.40f, ColorAlpha(ORANGE, 0.8f));
    ColorGradient_AddStop(&s_dustGrad, 1.00f, (Color){0, 0, 0, 0});
}

void CastStonePrisonSkill(Vector3 startPos, Vector3 target, SkillParams params)
{
    (void)startPos;
    for (int i = 0; i < MAX_PRISONS; i++) {
        if (s_prisons[i].state != STATE_INACTIVE) continue;
        
        float sizeScale = (params.sizeScale > 0.0f) ? params.sizeScale : 1.0f;
        s_prisons[i] = (EarthPrison){
            .pos          = target,
            .state        = STATE_CASTING,
            .timer        = 0.0f,
            .scale        = sizeScale,
            .yaw          = (float)GetRandomValue(0, 360) * DEG2RAD,
            .dealtDamage  = false,
            .spawnedDecal = false,
            .damage       = Skill_CalculateDamage(SKILL_CAT_AOE_CONTROL, params),
            .knockback    = Skill_CalculateKnockback(SKILL_CAT_AOE_CONTROL, params)
        };
        // Spawn casting light that glows during warning phase
        VFXLight_Spawn(target, ORANGE, 65.0f * sizeScale, CAST_TIME);
        break;
    }
}

void UpdateStonePrisonSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    for (int idx = 0; idx < MAX_PRISONS; idx++) {
        EarthPrison *p = &s_prisons[idx];
        if (p->state == STATE_INACTIVE) continue;

        p->timer += dt;

        switch (p->state) {
        case STATE_CASTING: {
            p->particleAccum += dt;
            if (p->particleAccum >= 0.06f) {
                p->particleAccum = 0.0f;
                float angle = (float)GetRandomValue(0, 360) * DEG2RAD;
                float dist = (float)GetRandomValue(25, 45) / 10.0f * PILLAR_RADIUS * p->scale * 2.5f;
                Vector3 particlePos = {
                    p->pos.x + cosf(angle) * dist,
                    p->pos.y + 0.1f,
                    p->pos.z + sinf(angle) * dist
                };
                Vector3 vel = { -cosf(angle) * 20.0f, (float)GetRandomValue(30, 60), -sinf(angle) * 20.0f };
                SpawnParticle((ParticleConfig){
                    .position = particlePos,
                    .velocity = vel,
                    .colorStart = ColorAlpha(ELEMENT_COLOR_EARTH, 0.85f),
                    .colorEnd = ColorAlpha(ORANGE, 0.0f),
                    .radius = (float)GetRandomValue(15, 35) / 10.0f * p->scale,
                    .lifetime = 1.0f,
                    .gradient = &s_dustGrad
                });
            }

            if (p->timer >= CAST_TIME) {
                p->state = STATE_RISING;
                p->timer = 0.0f;
                
                CameraFX_Shake(0.32f);
                ScreenDistort_AddSource(p->pos, 90.0f * p->scale, 0.4f, 0.3f, 180.0f);
            }
            break;
        }

        case STATE_RISING: {
            if (!p->spawnedDecal) {
                Decal_Spawn(p->pos, p->yaw, PILLAR_RADIUS * p->scale * 15.0f, s_crackTex, 4.0f, ColorAlpha(ELEMENT_COLOR_EARTH, 0.9f));
                VFXLight_Spawn(p->pos, ORANGE, 130.0f * p->scale, HOLD_TIME);
                
                // Spawn a small crack decal at the base of each of the 6 pillars
                float distToCenter = PILLAR_RADIUS * p->scale * 3.4f;
                for (int i = 0; i < PILLARS_COUNT; i++) {
                    float angle = (float)i / PILLARS_COUNT * 2.0f * PI + p->yaw;
                    Vector3 pillarPos = {
                        p->pos.x + cosf(angle) * distToCenter,
                        p->pos.y,
                        p->pos.z + sinf(angle) * distToCenter
                    };
                    Decal_Spawn(
                        pillarPos,
                        (float)GetRandomValue(0, 360) * DEG2RAD,
                        PILLAR_RADIUS * p->scale * 6.5f,
                        s_crackTex,
                        4.0f,
                        ColorAlpha(ELEMENT_COLOR_EARTH, 0.85f)
                    );
                }

                for (int i = 0; i < 24; i++) {
                    float a = (float)i / 24.0f * 2.0f * PI;
                    Vector3 vel = { cosf(a) * 60.0f * p->scale, (float)GetRandomValue(50, 90), sinf(a) * 60.0f * p->scale };
                    SpawnParticle((ParticleConfig){
                        .position = p->pos,
                        .velocity = vel,
                        .colorStart = ORANGE,
                        .colorEnd = ColorAlpha(ELEMENT_COLOR_EARTH, 0.0f),
                        .radius = (float)GetRandomValue(20, 50) / 10.0f * p->scale,
                        .lifetime = 1.2f,
                        .gradient = &s_dustGrad
                    });
                }
                p->spawnedDecal = true;
            }

            if (p->timer >= RISE_TIME) {
                p->state = STATE_HOLDING;
                p->timer = 0.0f;
            }
            break;
        }

        case STATE_HOLDING: {
            float dx = enemyPos.x - p->pos.x;
            float dz = enemyPos.z - p->pos.z;
            float distSq = dx * dx + dz * dz;
            float checkRad = PILLAR_RADIUS * p->scale * 3.4f + enemyRadius;

            if (distSq <= checkRad * checkRad) {
                AddRootToEnemy(0.15f);
            }

            if (GetRandomValue(0, 100) < 25) {
                float a = (float)GetRandomValue(0, 360) * DEG2RAD;
                float r = (float)GetRandomValue(0, 100) / 100.0f * PILLAR_RADIUS * p->scale * 3.0f;
                Vector3 sparkPos = { p->pos.x + cosf(a) * r, p->pos.y + 1.0f, p->pos.z + sinf(a) * r };
                SpawnParticle((ParticleConfig){
                    .position = sparkPos,
                    .velocity = (Vector3){0, (float)GetRandomValue(20, 45), 0},
                    .colorStart = WHITE,
                    .colorEnd = ColorAlpha(ORANGE, 0.0f),
                    .radius = (float)GetRandomValue(8, 18) / 10.0f * p->scale,
                    .lifetime = 0.8f,
                    .gradient = &s_dustGrad
                });
            }

            if (p->timer >= HOLD_TIME) {
                p->state = STATE_EXPLODING;
                p->timer = 0.0f;
            }
            break;
        }

        case STATE_EXPLODING: {
            if (!p->dealtDamage) {
                float dx = enemyPos.x - p->pos.x;
                float dz = enemyPos.z - p->pos.z;
                float distSq = dx * dx + dz * dz;
                float hitRad = PILLAR_RADIUS * p->scale * 4.2f + enemyRadius;

                if (distSq <= hitRad * hitRad) {
                    char dmgStr[16];
                    snprintf(dmgStr, sizeof(dmgStr), "%d", (int)p->damage);
                    AddFloatingText(p->pos, dmgStr, ORANGE, 30.0f, 0.8f);
                    AddFloatingText(p->pos, "CRUSH!", RED, 20.0f, 0.8f);

                    Vector3 pushDir = { dx, 2.0f, dz };
                    if (dx == 0.0f && dz == 0.0f) pushDir = (Vector3){0, 1, 0};
                    pushDir = Vector3Normalize(pushDir);
                    AddKnockbackToEnemy(Vector3Scale(pushDir, p->knockback * 1.6f));
                }

                ScreenDistort_AddSource(p->pos, 130.0f * p->scale, 0.8f, 0.45f, 250.0f);
                CameraFX_Shake(0.42f);
                VFXLight_Spawn(p->pos, RED, 140.0f * p->scale, 0.5f);

                for (int i = 0; i < 36; i++) {
                    float a = (float)i / 36.0f * 2.0f * PI;
                    Vector3 vel = { cosf(a) * 110.0f * p->scale, (float)GetRandomValue(40, 90), sinf(a) * 110.0f * p->scale };
                    SpawnParticle((ParticleConfig){
                        .position = p->pos,
                        .velocity = vel,
                        .colorStart = ORANGE,
                        .colorEnd = ColorAlpha(ELEMENT_COLOR_EARTH, 0.0f),
                        .radius = (float)GetRandomValue(20, 50) / 10.0f * p->scale,
                        .lifetime = 1.0f,
                        .gradient = &s_dustGrad
                    });
                }
                p->dealtDamage = true;
            }

            if (p->timer >= 0.1f) {
                p->state = STATE_FADING;
                p->timer = 0.0f;
            }
            break;
        }

        case STATE_FADING: {
            if (p->timer >= DISSOLVE_TIME) {
                p->state = STATE_INACTIVE;
            }
            break;
        }
        default:
            break;
        }
    }
}

static void DrawPerturbedPillar(Vector3 pillarPos, float currentHeight, float baseRad, float scale, float dissolve, float yawOffset, float angleFromCenter) {
    (void)scale; (void)dissolve;
    Vector3 rings[HEIGHT_SEGS + 1][RADIAL_SEGS];
    Vector3 normals[HEIGHT_SEGS + 1][RADIAL_SEGS];

    // Subtle elegant inward tilt (12 degrees)
    float tiltAngle = 12.0f * DEG2RAD;
    float dirInX = -cosf(angleFromCenter);
    float dirInZ = -sinf(angleFromCenter);

    for (int h = 0; h <= HEIGHT_SEGS; h++) {
        float hRatio = (float)h / HEIGHT_SEGS;
        float rad = baseRad * (1.0f - hRatio * 0.35f);

        float shiftDist = hRatio * currentHeight * sinf(tiltAngle);
        Vector3 centerOffset = { dirInX * shiftDist, 0.0f, dirInZ * shiftDist };

        for (int r = 0; r < RADIAL_SEGS; r++) {
            float angle = (float)r / RADIAL_SEGS * 2.0f * PI + yawOffset;
            float noiseWave = 1.0f + 0.16f * sinf(hRatio * 8.0f + angle * 3.0f);
            float perturbedRad = rad * noiseWave;

            Vector3 localPos = {
                perturbedRad * cosf(angle),
                hRatio * currentHeight,
                perturbedRad * sinf(angle)
            };

            Vector3 localNormal = { cosf(angle), 0.1f, sinf(angle) };
            localNormal = Vector3Normalize(localNormal);

            rings[h][r] = Vector3Add(Vector3Add(pillarPos, localPos), centerOffset);
            normals[h][r] = localNormal;
        }
    }

    rlPushMatrix();
    rlColor4ub(255, 255, 255, 255); // CRITICAL FIX: Reset vertex color to white!
    rlBegin(RL_QUADS);
    for (int h = 0; h < HEIGHT_SEGS; h++) {
        float v1 = (float)h / HEIGHT_SEGS;
        float v2 = (float)(h + 1) / HEIGHT_SEGS;
        for (int r = 0; r < RADIAL_SEGS; r++) {
            int nextR = (r + 1) % RADIAL_SEGS;
            float u1 = (float)r / RADIAL_SEGS;
            float u2 = (float)(r + 1) / RADIAL_SEGS;

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
    
    rlBegin(RL_TRIANGLES);
    float finalShift = currentHeight * sinf(tiltAngle);
    Vector3 peak = { pillarPos.x + dirInX * finalShift, pillarPos.y + currentHeight, pillarPos.z + dirInZ * finalShift };
    for (int r = 0; r < RADIAL_SEGS; r++) {
        int nextR = (r + 1) % RADIAL_SEGS;
        rlNormal3f(0.0f, 1.0f, 0.0f);
        rlTexCoord2f((float)r/RADIAL_SEGS, 1.0f); rlVertex3f(rings[HEIGHT_SEGS][r].x, rings[HEIGHT_SEGS][r].y, rings[HEIGHT_SEGS][r].z);
        rlTexCoord2f((float)nextR/RADIAL_SEGS, 1.0f); rlVertex3f(rings[HEIGHT_SEGS][nextR].x, rings[HEIGHT_SEGS][nextR].y, rings[HEIGHT_SEGS][nextR].z);
        rlTexCoord2f(0.5f, 1.0f); rlVertex3f(peak.x, peak.y, peak.z);
    }
    rlEnd();
    
    rlPopMatrix();
}

void DrawStonePrisonSkill(void)
{
    float currentTime = (float)GetTime();
    extern Camera3D camera;
    Vector3 camPos = camera.position;

    for (int idx = 0; idx < MAX_PRISONS; idx++) {
        const EarthPrison *p = &s_prisons[idx];
        if (p->state == STATE_INACTIVE) continue;

        // 1. Render the organic crawling crack decal during CASTING
        if (p->state == STATE_CASTING) {
            float t = p->timer / CAST_TIME;
            float progress = t;
            float radius = PILLAR_RADIUS * p->scale * 15.0f * t;
            
            rlDisableDepthMask();
            BeginShaderMode(s_crackShader);
            SetShaderValue(s_crackShader, s_uProgressLoc, &progress, SHADER_UNIFORM_FLOAT);
            SetShaderValue(s_crackShader, s_uCrackTimeLoc, &currentTime, SHADER_UNIFORM_FLOAT);
            
            rlSetTexture(s_crackTex.id);
            rlPushMatrix();
            rlTranslatef(p->pos.x, p->pos.y + 0.04f, p->pos.z);
            rlRotatef(p->yaw * (180.0f / PI), 0.0f, 1.0f, 0.0f);
            rlScalef(radius, 1.0f, radius);
            rlBegin(RL_QUADS);
                rlColor4ub(255, 255, 255, 255);
                rlTexCoord2f(0.0f, 0.0f); rlVertex3f(-0.5f, 0.0f, -0.5f);
                rlTexCoord2f(0.0f, 1.0f); rlVertex3f(-0.5f, 0.0f, 0.5f);
                rlTexCoord2f(1.0f, 1.0f); rlVertex3f(0.5f, 0.0f, 0.5f);
                rlTexCoord2f(1.0f, 0.0f); rlVertex3f(0.5f, 0.0f, -0.5f);
            rlEnd();
            rlPopMatrix();
            rlSetTexture(0);
            
            EndShaderMode();
            rlEnableDepthMask();
            continue; // don't draw pillars yet
        }

        float heightRatio = 1.0f;
        float dissolveAmt = 0.0f;

        if (p->state == STATE_RISING) {
            float t = p->timer / RISE_TIME;
            float inv = 1.0f - t;
            heightRatio = 1.0f - (inv * inv * inv);
        } else if (p->state == STATE_FADING || p->state == STATE_EXPLODING) {
            heightRatio = 1.0f;
            dissolveAmt = p->timer / DISSOLVE_TIME;
            if (dissolveAmt > 1.0f) dissolveAmt = 1.0f;
        }

        BeginShaderMode(s_shader);
        SetShaderValue(s_shader, s_uDissolveLoc, &dissolveAmt, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_shader, s_uTimeLoc, &currentTime, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_shader, s_uCamPosLoc, &camPos, SHADER_UNIFORM_VEC3);

        // Bind noise texture to give rocky surface pattern details
        rlSetTexture(s_noiseTex.id);

        // Draw 6 hexagonal pillars surrounding the center at a balanced distance
        float distToCenter = PILLAR_RADIUS * p->scale * 3.4f;
        for (int i = 0; i < PILLARS_COUNT; i++) {
            float angle = (float)i / PILLARS_COUNT * 2.0f * PI + p->yaw;
            
            float heightJitter = 0.8f + 0.45f * sinf(i * 123.45f);
            float radiusJitter = 0.8f + 0.35f * cosf(i * 56.78f);
            
            float currentHeight = PILLAR_HEIGHT * p->scale * heightRatio * heightJitter;
            float baseRad = PILLAR_RADIUS * p->scale * radiusJitter;
            
            if (currentHeight < 0.5f) continue;

            Vector3 pillarPos = {
                p->pos.x + cosf(angle) * distToCenter,
                p->pos.y - (1.0f - heightRatio) * 15.0f * p->scale,
                p->pos.z + sinf(angle) * distToCenter
            };
            
            float yawOffset = i * 45.0f * DEG2RAD;
            DrawPerturbedPillar(pillarPos, currentHeight, baseRad, p->scale, dissolveAmt, yawOffset, angle);
        }

        rlSetTexture(0);
        EndShaderMode();
    }
}

void UnloadStonePrisonSkill(void)
{
}

bool IsStonePrisonSkillActive(void)
{
    for (int i = 0; i < MAX_PRISONS; i++) {
        if (s_prisons[i].state == STATE_CASTING || s_prisons[i].state == STATE_RISING) {
            return true;
        }
    }
    return false;
}

int GetStonePrisonSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles)
{
    int count = 0;
    for (int i = 0; i < MAX_PRISONS; i++) {
        if (s_prisons[i].state != STATE_INACTIVE && s_prisons[i].state != STATE_CASTING) {
            outProjectiles[count].position = s_prisons[i].pos;
            outProjectiles[count].radius = PILLAR_RADIUS * s_prisons[i].scale * 3.4f;
            outProjectiles[count].active = true;
            count++;
            if (count >= maxProjectiles) break;
        }
    }
    return count;
}

void DeactivateStonePrisonProjectile(int index)
{
    int count = 0;
    for (int i = 0; i < MAX_PRISONS; i++) {
        if (s_prisons[i].state != STATE_INACTIVE && s_prisons[i].state != STATE_CASTING) {
            if (count == index) {
                s_prisons[i].state = STATE_EXPLODING;
                s_prisons[i].timer = 0.0f;
                break;
            }
            count++;
        }
    }
}
