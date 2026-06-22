#include "fluid_skill.h"
#include "skill_manager.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>

#define PARTICLES_PER_SECOND 225.0f
#define SPAWN_INTERVAL (1.0f / PARTICLES_PER_SECOND)
#define MAX_PARTICLES 100000 
#define MAX_EMITTERS 10

// External camera reference to project 3D coordinates to screen space
extern Camera3D camera;

// 3D Stream Emitter Structure
typedef struct {
    bool active;
    Vector3 startPos;
    Vector3 targetPos;
    Vector3 p1;
    Vector3 p2;
    float progress;
    float durationTimer;
    float spawnAccumulator;
    float twistPhase;
    float sizeScale;
} StreamEmitter;

// 3D Water Particle Structure
typedef struct {
    Vector3 position;
    Vector3 velocity;    
    Vector3 startPos, p1, p2, targetPos;
    float progress;      
    float speed;         
    float radius;
    float lifetime;      
    float maxLifetime;
    float spreadOffset;   
    float twistPhase;
    float sizeScale;
    int type;            // 0: Stream, 2: Splash
    bool active;
} WaterParticle;

static WaterParticle waterPool[MAX_PARTICLES];
static StreamEmitter emitters[MAX_EMITTERS];
static int lastUsedParticle = 0;

static RenderTexture2D canvasTexture;
static Shader fluidShader;
static int timeLoc;

// Bezier Curve point calculation in 3D space
static Vector3 GetBezierPoint(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t) {
    float u = 1.0f - t;
    float tt = t * t;
    float uu = u * u;
    float uuu = uu * u;
    float ttt = tt * t;

    Vector3 p = Vector3Scale(p0, uuu);
    p = Vector3Add(p, Vector3Scale(p1, 3.0f * uu * t));
    p = Vector3Add(p, Vector3Scale(p2, 3.0f * u * tt));
    p = Vector3Add(p, Vector3Scale(p3, ttt));
    return p;
}

static void SpawnParticle(int type, Vector3 pos, Vector3 vel, float speed, float progress, float maxLife, float baseRadius, StreamEmitter* em) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        int index = (lastUsedParticle + i) % MAX_PARTICLES;
        if (!waterPool[index].active) {
            waterPool[index].type = type;
            waterPool[index].position = pos;
            waterPool[index].velocity = vel;
            waterPool[index].progress = progress; 
            waterPool[index].speed = speed; 
            waterPool[index].radius = baseRadius;
            waterPool[index].maxLifetime = maxLife; 
            waterPool[index].lifetime = maxLife; 
            waterPool[index].spreadOffset = (float)GetRandomValue(-15, 15);
            waterPool[index].active = true;
            
            if (em != NULL) {
                waterPool[index].startPos = em->startPos;
                waterPool[index].targetPos = em->targetPos;
                waterPool[index].p1 = em->p1;
                waterPool[index].p2 = em->p2;
                waterPool[index].twistPhase = em->twistPhase;
                waterPool[index].sizeScale = em->sizeScale;
            } else {
                waterPool[index].sizeScale = 1.0f;
            }
            
            lastUsedParticle = (index + 1) % MAX_PARTICLES;
            break;
        }
    }
}

void InitFluidSkill(int screenWidth, int screenHeight) {
    canvasTexture = LoadRenderTexture(screenWidth, screenHeight);
    fluidShader = LoadShader(0, "fluid.fs");
    timeLoc = GetShaderLocation(fluidShader, "u_time"); 
    for (int i = 0; i < MAX_PARTICLES; i++) waterPool[i].active = false;
    for (int i = 0; i < MAX_EMITTERS; i++) emitters[i].active = false;
}

void CastFluidSkill(Vector3 startPos, Vector3 target, float twistPhase, float sizeScale) {
    for (int i = 0; i < MAX_EMITTERS; i++) {
        if (!emitters[i].active) {
            emitters[i].active = true;
            emitters[i].startPos = startPos;
            emitters[i].targetPos = target;
            emitters[i].progress = 0.0f;
            emitters[i].spawnAccumulator = 0.0f;
            emitters[i].durationTimer = 0.8f;
            emitters[i].twistPhase = twistPhase;
            emitters[i].sizeScale = sizeScale;

            float dist = Vector3Distance(startPos, target);
            Vector3 dir = Vector3Normalize(Vector3Subtract(target, startPos));
            
            // Perpendicular direction vector flat on X-Z plane (rotating around Y up axis)
            Vector3 perp = (Vector3){ -dir.z, 0.0f, dir.x };
            float spreadScale = sinf(twistPhase);
            
            emitters[i].p1 = Vector3Add(Vector3Add(startPos, Vector3Scale(dir, dist * 0.35f)), Vector3Scale(perp, spreadScale * dist * 0.35f));
            emitters[i].p2 = Vector3Add(Vector3Add(startPos, Vector3Scale(dir, dist * 0.70f)), Vector3Scale(perp, -spreadScale * dist * 0.25f));
            break;
        }
    }

    // Water burst at caster's hands
    int burstCount = GetRandomValue(10, 16) * sizeScale;
    for(int s = 0; s < burstCount; s++) {
        Vector3 burstVel = {
            (float)GetRandomValue(-150, 250) * sizeScale,
            (float)GetRandomValue(-100, 300) * sizeScale, // height velocity
            (float)GetRandomValue(-150, 250) * sizeScale
        };
        float burstRad = (float)GetRandomValue(3, 8);
        float burstLife = (float)GetRandomValue(3, 8) / 10.0f;
        SpawnParticle(2, startPos, burstVel, 0, 0, burstLife, burstRad, NULL);
    }
}

void UpdateFluidSkill(float dt) {
    float time = GetTime();

    // 1. UPDATE EMITTERS
    for (int e = 0; e < MAX_EMITTERS; e++) {
        if (!emitters[e].active) continue;

        float commonSpeed = 1.5f; 
        if (emitters[e].progress < 1.0f) {
            emitters[e].progress += dt * commonSpeed; 
            if (emitters[e].progress > 1.0f) emitters[e].progress = 1.0f;
        }

        emitters[e].durationTimer -= dt;
        if (emitters[e].durationTimer <= 0.0f) {
            emitters[e].active = false;
            continue;
        }
        
        emitters[e].spawnAccumulator += dt;
        int maxSpawnsThisFrame = 30; 
        float actualInterval = SPAWN_INTERVAL / emitters[e].sizeScale;
        while (emitters[e].spawnAccumulator >= actualInterval && maxSpawnsThisFrame-- > 0) {
            Vector3 zeroVel = {0, 0, 0};
            float baseRad = (float)GetRandomValue(9, 16);
            SpawnParticle(0, emitters[e].startPos, zeroVel, commonSpeed, 0.0f, 1.2f, baseRad, &emitters[e]); 
            emitters[e].spawnAccumulator -= actualInterval;
        }
    }

    // 2. UPDATE WATER PARTICLES
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!waterPool[i].active) continue;

        waterPool[i].lifetime -= dt;

        if (waterPool[i].lifetime <= 0.0f) {
            waterPool[i].active = false;
            continue;
        }

        if (waterPool[i].type == 2) { // Splash/mist particles
            waterPool[i].position.x += waterPool[i].velocity.x * dt;
            waterPool[i].position.y += waterPool[i].velocity.y * dt;
            waterPool[i].position.z += waterPool[i].velocity.z * dt;
            waterPool[i].velocity.y -= 800.0f * dt; // Gravity falls on Y axis (height)
            continue;
        }

        // Stream particles along Bezier path
        waterPool[i].progress += waterPool[i].speed * dt;

        if (waterPool[i].progress >= 1.0f) {
            waterPool[i].active = false;
            int splashCount = GetRandomValue(2, 4) * waterPool[i].sizeScale;
            for(int s = 0; s < splashCount; s++) {
                Vector3 splashVel = {
                    (float)GetRandomValue(-160, 160) * waterPool[i].sizeScale,
                    (float)GetRandomValue(-100, 300) * waterPool[i].sizeScale,
                    (float)GetRandomValue(-160, 160) * waterPool[i].sizeScale
                };
                float splashRad = (float)GetRandomValue(2, 6);
                float splashLife = (float)GetRandomValue(4, 10) / 10.0f;
                SpawnParticle(2, waterPool[i].position, splashVel, 0, 0, splashLife, splashRad, NULL);
            }
            continue;
        }

        Vector3 basePos = GetBezierPoint(
            waterPool[i].startPos, 
            waterPool[i].p1, 
            waterPool[i].p2, 
            waterPool[i].targetPos, 
            waterPool[i].progress
        );

        float focusFactor = 1.0f - powf(waterPool[i].progress, 4.0f); 
        float waveFreq = waterPool[i].progress * 15.0f - time * 25.0f; 
        
        float noiseX = sinf(waveFreq + waterPool[i].spreadOffset + waterPool[i].twistPhase) * (15.0f * focusFactor); 
        float noiseY = cosf(waveFreq * 1.2f - waterPool[i].spreadOffset + waterPool[i].twistPhase) * (15.0f * focusFactor);
        float noiseZ = sinf(waveFreq * 0.8f + waterPool[i].spreadOffset * 1.5f + waterPool[i].twistPhase) * (15.0f * focusFactor);

        waterPool[i].position.x = basePos.x + noiseX;
        waterPool[i].position.y = basePos.y + noiseY; // height noise
        waterPool[i].position.z = basePos.z + noiseZ;

        // Leave mist particles behind
        if (GetRandomValue(1, 100) <= 8) { 
            Vector3 forwardDir = Vector3Normalize(Vector3Subtract(waterPool[i].targetPos, waterPool[i].position));
            Vector3 dropVel = {
                ((forwardDir.x * GetRandomValue(150, 300)) + GetRandomValue(-30, 30)) * waterPool[i].sizeScale,
                ((forwardDir.y * GetRandomValue(150, 300)) - GetRandomValue(50, 150)) * waterPool[i].sizeScale, // Y gravity
                ((forwardDir.z * GetRandomValue(150, 300)) + GetRandomValue(-30, 30)) * waterPool[i].sizeScale
            };
            
            float mistRad = (float)GetRandomValue(1, 2);
            float mistLife = (float)GetRandomValue(2, 4) / 10.0f; 
            
            SpawnParticle(2, waterPool[i].position, dropVel, 0, 0, mistLife, mistRad, NULL);
        }
    }
}

void DrawFluidSkill(void) {
    bool active = false;
    for (int i = 0; i < MAX_EMITTERS; i++) {
        if (emitters[i].active) { active = true; break; }
    }
    if (!active) {
        for (int i = 0; i < MAX_PARTICLES; i++) {
            if (waterPool[i].active) { active = true; break; }
        }
    }
    if (!active) return;

    float time = GetTime();

    // Project 3D particles onto 2D canvas to render fluid Metaball shader
    BeginTextureMode(canvasTexture);
        ClearBackground(BLANK); 
        BeginBlendMode(BLEND_ALPHA);
            for (int i = 0; i < MAX_PARTICLES; i++) {
                if (!waterPool[i].active) continue;

                float currentRadius = waterPool[i].radius;
                float currentAlpha = 0.6f;
                float lifeRatio = waterPool[i].lifetime / waterPool[i].maxLifetime;

                if (waterPool[i].type < 2) {
                    float bulge = sinf(waterPool[i].progress * PI); 
                    float sizeScale = 0.3f + (0.7f * bulge); 
                    float fadeScale = (lifeRatio < 0.3f) ? (lifeRatio / 0.3f) : 1.0f;
                    currentRadius *= (sizeScale * fadeScale * waterPool[i].sizeScale);
                    currentAlpha *= fadeScale; 
                } else {
                    currentRadius *= lifeRatio * waterPool[i].sizeScale;
                    currentAlpha = lifeRatio * 1.0f; 
                }

                // Project 3D position to 2D screen coordinate using cached helper
                ProjectedPoint pt = ProjectPointCached(waterPool[i].position, camera);
                if (pt.behindCamera) continue;
                Vector2 screenPos = pt.screenPos;
                float depthFactor = pt.depthFactor;
                
                DrawCircleGradient((int)screenPos.x, (int)screenPos.y, currentRadius * depthFactor, ColorAlpha(WHITE, currentAlpha), BLANK);
            }
        EndBlendMode();
    EndTextureMode();

    SetShaderValue(fluidShader, timeLoc, &time, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(fluidShader);
        DrawTextureRec(canvasTexture.texture, (Rectangle){ 0, 0, (float)canvasTexture.texture.width, (float)-canvasTexture.texture.height }, (Vector2){ 0, 0 }, WHITE);
    EndShaderMode();
}

void UnloadFluidSkill(void) {
    UnloadShader(fluidShader);
    UnloadRenderTexture(canvasTexture);
}

int GetFluidSkillProjectiles(SkillProjectile* outProjectiles, int maxProjectiles) {
    int count = 0;
    for (int i = 0; i < MAX_EMITTERS; i++) {
        if (emitters[i].active && emitters[i].progress < 1.0f && count < maxProjectiles) {
            Vector3 headPos = GetBezierPoint(
                emitters[i].startPos,
                emitters[i].p1,
                emitters[i].p2,
                emitters[i].targetPos,
                emitters[i].progress
            );
            outProjectiles[count].position = headPos;
            outProjectiles[count].radius = 11.0f * emitters[i].sizeScale;
            outProjectiles[count].active = true;
            count++;
        }
    }
    return count;
}

void DeactivateFluidProjectile(int index) {
    int count = 0;
    for (int i = 0; i < MAX_EMITTERS; i++) {
        if (emitters[i].active && emitters[i].progress < 1.0f) {
            if (count == index) {
                emitters[i].active = false;
                Vector3 headPos = GetBezierPoint(
                    emitters[i].startPos,
                    emitters[i].p1,
                    emitters[i].p2,
                    emitters[i].targetPos,
                    emitters[i].progress
                );
                
                int splashCount = GetRandomValue(10, 16) * emitters[i].sizeScale;
                for(int s = 0; s < splashCount; s++) {
                    Vector3 splashVel = {
                        (float)GetRandomValue(-160, 160) * emitters[i].sizeScale,
                        (float)GetRandomValue(-100, 300) * emitters[i].sizeScale,
                        (float)GetRandomValue(-160, 160) * emitters[i].sizeScale
                    };
                    float splashRad = (float)GetRandomValue(3, 8);
                    float splashLife = (float)GetRandomValue(4, 12) / 10.0f;
                    SpawnParticle(2, headPos, splashVel, 0, 0, splashLife, splashRad, NULL);
                }
                break;
            }
            count++;
        }
    }
}