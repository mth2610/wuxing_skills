#include "metal_skill.h"
#include "skill_manager.h"
#include "raymath.h"
#include <stddef.h>
#include <math.h>

#define MAX_METAL_PARTICLES 15000
#define MAX_EMITTERS 10
#define PARTICLE_HISTORY_COUNT 8

// External camera reference
extern Camera3D camera;

// Particle types
typedef enum {
    PARTICLE_SWORD = 0,
    PARTICLE_SPARK = 1,
    PARTICLE_SHARD = 2,
    PARTICLE_PORTAL = 3,
    PARTICLE_SLASH = 4,
    PARTICLE_SHOCKWAVE = 5,
    PARTICLE_STARDUST = 6
} MetalParticleType;

// Emitter structure in 3D
typedef struct {
    bool active;
    Vector3 startPos;
    Vector3 targetPos;
    Vector3 portalPositions[5];
    float spawnDelay[5];
    bool spawned[5];
    bool portalSpawned[5];
    float portalSizes[5];
    int count;
    float timer;
} MetalEmitter;

// Particle structure in 3D
typedef struct {
    Vector3 position;
    Vector3 velocity;
    Vector3 target;     
    float length;       
    float thickness;    
    float lifetime;
    float maxLifetime;
    int type;
    bool active;
    
    Vector3 history[PARTICLE_HISTORY_COUNT];
    int historyCount;
    float angle;
    float wobblePhase;
    float scale;
} MetalParticle;

static MetalParticle metalPool[MAX_METAL_PARTICLES];
static MetalEmitter emitters[MAX_EMITTERS];
static int lastUsedIndex = 0;

static RenderTexture2D metalCanvas;
static Shader metalShader;
static int timeLocMetal;
static Texture2D swordSprite;

static void SpawnMetal(int type, Vector3 pos, Vector3 vel, float len, float thick, float life, Vector3 target, float initialAngle, float wobblePhase, float scale) {
    for (int i = 0; i < MAX_METAL_PARTICLES; i++) {
        int index = (lastUsedIndex + i) % MAX_METAL_PARTICLES;
        if (!metalPool[index].active) {
            metalPool[index].type = type;
            metalPool[index].position = pos;
            metalPool[index].velocity = vel;
            metalPool[index].target = target;
            metalPool[index].length = len;
            metalPool[index].thickness = thick;
            metalPool[index].lifetime = life;
            metalPool[index].maxLifetime = life;
            metalPool[index].active = true;
            metalPool[index].historyCount = 0;
            metalPool[index].angle = initialAngle;
            metalPool[index].wobblePhase = wobblePhase;
            metalPool[index].scale = scale;
            
            for (int h = 0; h < PARTICLE_HISTORY_COUNT; h++) {
                metalPool[index].history[h] = pos;
            }
            
            lastUsedIndex = (index + 1) % MAX_METAL_PARTICLES;
            break;
        }
    }
}

void InitMetalSkill(int screenWidth, int screenHeight) {
    metalCanvas = LoadRenderTexture(screenWidth, screenHeight);
    metalShader = LoadShader(0, "metal.fs");
    timeLocMetal = GetShaderLocation(metalShader, "u_time");
    swordSprite = LoadTexture("sword.png"); 
    
    for (int i = 0; i < MAX_METAL_PARTICLES; i++) {
        metalPool[i].active = false;
    }
    
    for (int i = 0; i < MAX_EMITTERS; i++) {
        emitters[i].active = false;
    }
}

void CastMetalSkill(Vector3 startPos, Vector3 target, int count, float sizeScale) {
    if (count > 5) count = 5;
    if (count < 1) count = 1;
    
    int emitterIndex = -1;
    for (int i = 0; i < MAX_EMITTERS; i++) {
        if (!emitters[i].active) {
            emitterIndex = i;
            break;
        }
    }
    
    if (emitterIndex == -1) return; 
    
    MetalEmitter *em = &emitters[emitterIndex];
    em->active = true;
    em->startPos = startPos;
    em->targetPos = target;
    em->count = count;
    em->timer = 0.0f;
    
    Vector3 dir = Vector3Normalize(Vector3Subtract(target, startPos));
    // Perpendicular direction vector flat on X-Z plane (rotating around Y up axis)
    Vector3 perp = (Vector3){ -dir.z, 0.0f, dir.x };
    
    for (int j = 0; j < count; j++) {
        em->spawned[j] = false;
        em->portalSpawned[j] = false;
        em->spawnDelay[j] = (float)j * 0.15f;
        
        float offsetFactor = (float)j - (float)(count - 1) / 2.0f;
        em->portalPositions[j] = Vector3Add(startPos, Vector3Scale(perp, offsetFactor * 40.0f * sizeScale));
        
        float distFromCenter = fabsf(offsetFactor);
        if (distFromCenter < 0.1f) {
            em->portalSizes[j] = 0.55f * sizeScale;
        } else if (distFromCenter < 1.1f) {
            em->portalSizes[j] = 0.38f * sizeScale;
        } else {
            em->portalSizes[j] = 0.28f * sizeScale;
        }
    }
    
    // Muzzle flash at caster shoulder
    Vector3 zeroTarget = {0, 0, 0};
    for (int i = 0; i < 8; i++) {
        Vector3 flashVel = {
            dir.x * GetRandomValue(200, 400) + GetRandomValue(-80, 80),
            dir.y * GetRandomValue(200, 400) + GetRandomValue(-80, 80),
            dir.z * GetRandomValue(200, 400) + GetRandomValue(-80, 80)
        };
        SpawnMetal(PARTICLE_SPARK, startPos, flashVel, (float)GetRandomValue(5, 10), (float)GetRandomValue(1, 2), 0.2f, zeroTarget, 0.0f, 0.0f, 1.0f);
    }
}

void UpdateMetalSkill(float dt) {
    Vector3 zeroTarget = {0, 0, 0};

    // 1. UPDATE EMITTERS
    for (int e = 0; e < MAX_EMITTERS; e++) {
        if (!emitters[e].active) continue;
        
        emitters[e].timer += dt;
        bool allSpawned = true;
        
        for (int j = 0; j < emitters[e].count; j++) {
            if (!emitters[e].spawned[j]) {
                allSpawned = false;
                
                Vector3 portalPos = emitters[e].portalPositions[j];
                float sizeFactor = emitters[e].portalSizes[j];
                
                float portalAppearTime = emitters[e].spawnDelay[j] - 0.25f;
                if (portalAppearTime < 0.0f) portalAppearTime = 0.0f;
                
                if (emitters[e].timer >= portalAppearTime && !emitters[e].portalSpawned[j]) {
                    emitters[e].portalSpawned[j] = true;
                    float portalLife = emitters[e].spawnDelay[j] - emitters[e].timer + 0.25f;
                    if (portalLife < 0.25f) portalLife = 0.25f;
                    SpawnMetal(PARTICLE_PORTAL, portalPos, (Vector3){0,0,0}, 45.0f * sizeFactor, 6.0f, portalLife, (Vector3){0,0,0}, 0.0f, 0.0f, sizeFactor);
                }
                
                // Intake sparks
                if (emitters[e].portalSpawned[j] && emitters[e].timer < emitters[e].spawnDelay[j]) {
                    if (GetRandomValue(1, 100) <= 25) { 
                        float spawnAngle = (float)GetRandomValue(0, 360) * DEG2RAD;
                        float spawnDist = (float)GetRandomValue(25, 45) * sizeFactor;
                        Vector3 sparkPos = {
                            portalPos.x + cosf(spawnAngle) * spawnDist,
                            portalPos.y + GetRandomValue(-10, 10), // slight vertical spread
                            portalPos.z + sinf(spawnAngle) * spawnDist
                        };
                        
                        Vector3 toPortal = Vector3Subtract(portalPos, sparkPos);
                        float speed = (float)GetRandomValue(140, 240);
                        Vector3 sparkVel = Vector3Scale(Vector3Normalize(toPortal), speed);
                        float sparkLife = spawnDist / speed; 
                        
                        SpawnMetal(PARTICLE_SPARK, sparkPos, sparkVel, (float)GetRandomValue(5, 10), (float)GetRandomValue(1, 2), sparkLife, zeroTarget, 0.0f, 0.0f, sizeFactor);
                    }
                }
                
                // Spawn sword
                if (emitters[e].timer >= emitters[e].spawnDelay[j]) {
                    emitters[e].spawned[j] = true;
                    
                    Vector3 spawnPos = emitters[e].portalPositions[j];
                    Vector3 baseDir = Vector3Normalize(Vector3Subtract(emitters[e].targetPos, spawnPos));
                    
                    float offsetFactor = (float)j - (float)(emitters[e].count - 1) / 2.0f;
                    float spreadAngle = (offsetFactor * 16.0f) * DEG2RAD; 
                    
                    // Rotate baseDir around Y up axis by spreadAngle
                    Vector3 launchDir = {
                        baseDir.x * cosf(spreadAngle) - baseDir.z * sinf(spreadAngle),
                        baseDir.y,
                        baseDir.x * sinf(spreadAngle) + baseDir.z * cosf(spreadAngle)
                    };
                    launchDir = Vector3Normalize(launchDir);
                    
                    float speed = (float)GetRandomValue(650, 950);
                    Vector3 vel = Vector3Scale(launchDir, speed);
                    
                    float swordLen = (float)GetRandomValue(52, 72) * sizeFactor;
                    float swordThick = (float)GetRandomValue(10, 14) * sizeFactor;
                    
                    float randomPhase = (float)GetRandomValue(0, 100) * 0.1f;
                    SpawnMetal(PARTICLE_SWORD, spawnPos, vel, swordLen, swordThick, 2.0f, emitters[e].targetPos, 0.0f, randomPhase, sizeFactor);
                    
                    Vector3 oppositeDir = Vector3Scale(launchDir, -0.6f);
                    for (int s = 0; s < 6; s++) {
                        Vector3 sparkVel = {
                            oppositeDir.x * GetRandomValue(150, 350) + GetRandomValue(-60, 60),
                            oppositeDir.y * GetRandomValue(150, 350) + GetRandomValue(-60, 60),
                            oppositeDir.z * GetRandomValue(150, 350) + GetRandomValue(-60, 60)
                        };
                        SpawnMetal(PARTICLE_SPARK, spawnPos, sparkVel, (float)GetRandomValue(4, 10), (float)GetRandomValue(1, 2), 0.2f, zeroTarget, 0.0f, 0.0f, sizeFactor);
                    }
                }
            }
        }
        
        if (allSpawned) {
            emitters[e].active = false;
        }
    }

    // 2. UPDATE PARTICLES
    for (int i = 0; i < MAX_METAL_PARTICLES; i++) {
        if (!metalPool[i].active) continue;

        metalPool[i].lifetime -= dt;
        if (metalPool[i].lifetime <= 0.0f) {
            metalPool[i].active = false;
            continue;
        }

        for (int h = PARTICLE_HISTORY_COUNT - 1; h > 0; h--) {
            metalPool[i].history[h] = metalPool[i].history[h - 1];
        }
        metalPool[i].history[0] = metalPool[i].position;
        if (metalPool[i].historyCount < PARTICLE_HISTORY_COUNT) {
            metalPool[i].historyCount++;
        }

        if (metalPool[i].type == PARTICLE_SWORD) {
            metalPool[i].wobblePhase += dt * 16.0f;

            metalPool[i].position.x += metalPool[i].velocity.x * dt;
            metalPool[i].position.y += metalPool[i].velocity.y * dt;
            metalPool[i].position.z += metalPool[i].velocity.z * dt;

            Vector3 toTarget = Vector3Subtract(metalPool[i].target, metalPool[i].position);
            float distToTarget = Vector3Length(toTarget);

            if (distToTarget > 20.0f) {
                Vector3 desiredDir = Vector3Normalize(toTarget);
                float currentSpeed = Vector3Length(metalPool[i].velocity);
                
                float maxSpeed = 1350.0f;
                float newSpeed = currentSpeed + 550.0f * dt;
                if (newSpeed > maxSpeed) newSpeed = maxSpeed;
                
                Vector3 perpDir = { -desiredDir.z, 0.0f, desiredDir.x };
                float wobble = sinf(metalPool[i].wobblePhase) * 110.0f * dt;
                
                Vector3 desiredVel = Vector3Add(Vector3Scale(desiredDir, newSpeed), Vector3Scale(perpDir, wobble));
                metalPool[i].velocity = Vector3Lerp(metalPool[i].velocity, desiredVel, dt * 3.2f); 
            }

            if (GetRandomValue(1, 100) <= 55) {
                Vector3 backDir = Vector3Scale(Vector3Normalize(metalPool[i].velocity), -0.2f);
                Vector3 sparkVel = {
                    backDir.x * GetRandomValue(200, 500) + GetRandomValue(-100, 100),
                    backDir.y * GetRandomValue(200, 500) + GetRandomValue(-100, 100),
                    backDir.z * GetRandomValue(200, 500) + GetRandomValue(-100, 100)
                };
                SpawnMetal(PARTICLE_SPARK, metalPool[i].position, sparkVel, (float)GetRandomValue(8, 20), GetRandomValue(1, 2), 0.2f, zeroTarget, 0.0f, 0.0f, metalPool[i].scale);
            }
            
            if (distToTarget < 30.0f || metalPool[i].lifetime < 0.1f) {
                metalPool[i].active = false; 
                
                float scale = metalPool[i].scale;
                if (scale < 0.5f) scale = 0.5f;
                
                // Triangle shards in 3D
                int shardCount = GetRandomValue(8, 14);
                for (int s = 0; s < shardCount; s++) {
                    float shardAngle = (float)GetRandomValue(0, 360) * DEG2RAD;
                    float pitchAngle = (float)GetRandomValue(15, 75) * DEG2RAD;
                    float speed = (float)GetRandomValue(250, 600) * scale;
                    Vector3 shardVel = {
                        cosf(shardAngle) * speed * cosf(pitchAngle),
                        sinf(pitchAngle) * speed + 100.0f, // upward burst
                        sinf(shardAngle) * speed * cosf(pitchAngle)
                    };
                    SpawnMetal(PARTICLE_SHARD, metalPool[i].position, shardVel, (float)GetRandomValue(8, 16) * scale, GetRandomValue(2, 4), 0.55f, zeroTarget, (float)GetRandomValue(0, 360), 0.0f, scale);
                }

                // Radial sparks in 3D
                int sparkCount = GetRandomValue(12, 18);
                for (int s = 0; s < sparkCount; s++) {
                    float sparkAngle = (float)GetRandomValue(0, 360) * DEG2RAD;
                    float pitchAngle = (float)GetRandomValue(-45, 45) * DEG2RAD;
                    float speed = (float)GetRandomValue(350, 750) * scale;
                    Vector3 sparkVel = {
                        cosf(sparkAngle) * speed * cosf(pitchAngle),
                        sinf(pitchAngle) * speed,
                        sinf(sparkAngle) * speed * cosf(pitchAngle)
                    };
                    SpawnMetal(PARTICLE_SPARK, metalPool[i].position, sparkVel, (float)GetRandomValue(6, 15), (float)GetRandomValue(1, 2), 0.3f, zeroTarget, 0.0f, 0.0f, scale);
                }

                // Impact rays
                int slashCount = GetRandomValue(3, 4); 
                for (int s = 0; s < slashCount; s++) {
                    float rayAngle = (float)GetRandomValue(0, 360) * DEG2RAD;
                    float rayLen = (float)GetRandomValue(50, 95) * scale;
                    SpawnMetal(PARTICLE_SLASH, metalPool[i].position, (Vector3){0,0,0}, rayLen, 4.0f * scale, 0.22f, zeroTarget, rayAngle, 0.0f, scale);
                }

                // Shockwave
                SpawnMetal(PARTICLE_SHOCKWAVE, metalPool[i].position, (Vector3){0,0,0}, 45.0f * scale, 2.0f * scale, 0.22f, zeroTarget, 0.0f, 0.0f, scale);
                
                // Stardust rising up in Y axis
                int stardustCount = GetRandomValue(12, 18);
                for (int s = 0; s < stardustCount; s++) {
                    float dustAngle = (float)GetRandomValue(0, 360) * DEG2RAD;
                    float speed = (float)GetRandomValue(100, 260) * scale;
                    Vector3 dustVel = {
                        cosf(dustAngle) * speed * 0.5f,
                        speed + 100.0f,
                        sinf(dustAngle) * speed * 0.5f
                    };
                    float dustLife = (float)GetRandomValue(6, 12) / 10.0f;
                    SpawnMetal(PARTICLE_STARDUST, metalPool[i].position, dustVel, (float)GetRandomValue(3, 6) * scale, 1.0f, dustLife, zeroTarget, 0.0f, 0.0f, scale);
                }
            }
        } 
        else if (metalPool[i].type == PARTICLE_SPARK) {
            metalPool[i].position.x += metalPool[i].velocity.x * dt;
            metalPool[i].position.y += metalPool[i].velocity.y * dt;
            metalPool[i].position.z += metalPool[i].velocity.z * dt;
            metalPool[i].velocity.x *= (1.0f - 2.5f * dt);
            metalPool[i].velocity.y *= (1.0f - 2.5f * dt);
            metalPool[i].velocity.z *= (1.0f - 2.5f * dt);
        }
        else if (metalPool[i].type == PARTICLE_SHARD) {
            metalPool[i].position.x += metalPool[i].velocity.x * dt;
            metalPool[i].position.y += metalPool[i].velocity.y * dt;
            metalPool[i].position.z += metalPool[i].velocity.z * dt;
            metalPool[i].velocity.x *= (1.0f - 1.8f * dt);
            metalPool[i].velocity.z *= (1.0f - 1.8f * dt);
            metalPool[i].velocity.y -= 950.0f * dt; // gravity downward Y
            metalPool[i].angle += 400.0f * dt * (float)(i % 2 == 0 ? 1 : -1);
        }
        else if (metalPool[i].type == PARTICLE_PORTAL) {
            metalPool[i].angle += 140.0f * dt;
        }
        else if (metalPool[i].type == PARTICLE_STARDUST) {
            metalPool[i].position.y += metalPool[i].velocity.y * dt;
            metalPool[i].position.x += (metalPool[i].velocity.x + sinf(metalPool[i].lifetime * 12.0f + (float)i) * 120.0f) * dt;
            metalPool[i].position.z += metalPool[i].velocity.z * dt;
            metalPool[i].velocity.y *= (1.0f - 1.8f * dt);
        }
    }
}

void DrawMetalSkill(void) {
    bool active = false;
    for (int i = 0; i < MAX_EMITTERS; i++) {
        if (emitters[i].active) { active = true; break; }
    }
    if (!active) {
        for (int i = 0; i < MAX_METAL_PARTICLES; i++) {
            if (metalPool[i].active) { active = true; break; }
        }
    }
    if (!active) return;

    float time = (float)GetTime();

    BeginTextureMode(metalCanvas);
        ClearBackground(BLANK);
        BeginBlendMode(BLEND_ADDITIVE);
        
        for (int i = 0; i < MAX_METAL_PARTICLES; i++) {
            if (!metalPool[i].active) continue;

            float lifeRatio = metalPool[i].lifetime / metalPool[i].maxLifetime;
            unsigned char intensity = (unsigned char)(255.0f * lifeRatio);
            
            // Project 3D position to screen-space coordinates using cached helper
            ProjectedPoint pt = ProjectPointCached(metalPool[i].position, camera);
            if (pt.behindCamera) continue;
            Vector2 screenPos = pt.screenPos;
            float depthFactor = pt.depthFactor;

            // 1. PROJECTED RIBBON TRAIL AND SWORD SPRITE
            if (metalPool[i].type == PARTICLE_SWORD) {
                if (metalPool[i].historyCount > 1) {
                    for (int h = 0; h < metalPool[i].historyCount - 1; h++) {
                        ProjectedPoint pt1 = ProjectPointCached(metalPool[i].history[h], camera);
                        ProjectedPoint pt2 = ProjectPointCached(metalPool[i].history[h + 1], camera);
                        if (pt1.behindCamera || pt2.behindCamera) continue;
                        Vector2 p1 = pt1.screenPos;
                        Vector2 p2 = pt2.screenPos;
                        
                        float segRatio = 1.0f - (float)h / (float)PARTICLE_HISTORY_COUNT;
                        
                        for (int step = 0; step < 3; step++) {
                            float t = (float)step / 3.0f;
                            Vector2 p = Vector2Lerp(p1, p2, t);
                            float interpRatio = segRatio - (t * (1.0f / (float)PARTICLE_HISTORY_COUNT));
                            float radius = metalPool[i].thickness * 1.35f * interpRatio * lifeRatio * depthFactor;
                            
                            if (radius > 1.0f) {
                                Color auraCol = ColorAlpha(GOLD, 0.16f * interpRatio * lifeRatio);
                                Color coreCol = ColorAlpha(WHITE, 0.40f * interpRatio * lifeRatio);
                                DrawCircleGradient((int)p.x, (int)p.y, radius, auraCol, BLANK);
                                DrawCircleGradient((int)p.x, (int)p.y, radius * 0.35f, coreCol, BLANK);
                            }
                        }
                    }
                }
                
                // Draw glow aura
                Color auraCol = ColorAlpha(GOLD, 0.22f * lifeRatio);
                Color coreCol = ColorAlpha(WHITE, 0.38f * lifeRatio);
                DrawCircleGradient((int)screenPos.x, (int)screenPos.y, metalPool[i].length * 0.55f * depthFactor, auraCol, BLANK);
                DrawCircleGradient((int)screenPos.x, (int)screenPos.y, metalPool[i].length * 0.18f * depthFactor, coreCol, BLANK);

                // Calculate sprite rotation angle from screen velocity projection
                float rotation = 0.0f;
                if (metalPool[i].historyCount > 1) {
                    ProjectedPoint pt1 = ProjectPointCached(metalPool[i].history[1], camera);
                    rotation = atan2f(screenPos.y - pt1.screenPos.y, screenPos.x - pt1.screenPos.x) * RAD2DEG;
                }
                
                Rectangle sourceRec = { 0.0f, 0.0f, (float)swordSprite.width, (float)swordSprite.height };
                Rectangle destRec = { screenPos.x, screenPos.y, metalPool[i].length * depthFactor, metalPool[i].thickness * 1.4f * depthFactor };
                Vector2 origin = { destRec.width / 2.0f, destRec.height / 2.0f }; 
                
                DrawTexturePro(swordSprite, sourceRec, destRec, origin, rotation, ColorAlpha(WHITE, lifeRatio));
            }
            // 2. SPARKS
            else if (metalPool[i].type == PARTICLE_SPARK) {
                Vector3 dir = Vector3Normalize(metalPool[i].velocity);
                float speed = Vector3Length(metalPool[i].velocity);
                Vector3 tail3D = Vector3Subtract(metalPool[i].position, Vector3Scale(dir, speed * 0.012f));
                
                ProjectedPoint tailPt = ProjectPointCached(tail3D, camera);
                Vector2 tail = tailPt.screenPos;
                float thick = metalPool[i].thickness * lifeRatio * depthFactor;
                Color sparkCol = { intensity, (unsigned char)(intensity * 0.85f), (unsigned char)(intensity * 0.15f), 255 };
                
                DrawLineEx(screenPos, tail, thick, sparkCol);
                DrawCircleV(screenPos, thick * 0.5f, WHITE);
            }
            // 3. SHARDS
            else if (metalPool[i].type == PARTICLE_SHARD) {
                float size = metalPool[i].length * lifeRatio * 0.45f * depthFactor;
                float radAngle = metalPool[i].angle * DEG2RAD;
                
                Vector2 v1 = {
                    screenPos.x + cosf(radAngle) * size,
                    screenPos.y + sinf(radAngle) * size
                };
                Vector2 v2 = {
                    screenPos.x + cosf(radAngle + 120.0f * DEG2RAD) * (size * 0.55f),
                    screenPos.y + sinf(radAngle + 120.0f * DEG2RAD) * (size * 0.55f)
                };
                Vector2 v3 = {
                    screenPos.x + cosf(radAngle + 240.0f * DEG2RAD) * (size * 0.55f),
                    screenPos.y + sinf(radAngle + 240.0f * DEG2RAD) * (size * 0.55f)
                };
                
                Color shardCol = { intensity, (unsigned char)(intensity * 0.72f), (unsigned char)(intensity * 0.15f), 255 };
                DrawTriangle(v1, v2, v3, shardCol);
                DrawTriangleLines(v1, v2, v3, WHITE); 
            }
            // 4. SUMMONING PORTALS
            else if (metalPool[i].type == PARTICLE_PORTAL) {
                float radius = metalPool[i].length * depthFactor;
                float age = metalPool[i].maxLifetime - metalPool[i].lifetime;
                
                if (age < 0.12f) {
                    radius *= (age / 0.12f);
                }
                
                unsigned char pIntensity = (unsigned char)(210.0f * lifeRatio);
                Color portalCol = { pIntensity, (unsigned char)(pIntensity * 0.55f), 0, 255 };
                
                DrawCircleGradient((int)screenPos.x, (int)screenPos.y, radius * 0.4f, ColorAlpha(WHITE, lifeRatio * 0.6f), BLANK);
                DrawCircleGradient((int)screenPos.x, (int)screenPos.y, radius * 1.2f, ColorAlpha(GOLD, 0.06f * lifeRatio), BLANK);
                
                // Draw concentric 2D ellipses to represent 2.5D rotated circles
                DrawEllipseLines((int)screenPos.x, (int)screenPos.y, radius, radius * 0.35f, portalCol);
                DrawEllipseLines((int)screenPos.x, (int)screenPos.y, radius * 0.85f, radius * 0.85f * 0.35f, ColorAlpha(WHITE, lifeRatio * 0.4f));
                DrawEllipseLines((int)screenPos.x, (int)screenPos.y, radius * 0.6f, radius * 0.6f * 0.35f, portalCol);
                
                int spiralArms = 6;
                for (int s = 0; s < spiralArms; s++) {
                    float baseArmAngle = (metalPool[i].angle + (float)s * (360.0f / (float)spiralArms)) * DEG2RAD;
                    Vector2 prevPt = screenPos;
                    
                    for (int step = 1; step <= 10; step++) {
                        float t = (float)step / 10.0f;
                        float armAngle = baseArmAngle + t * 1.8f; 
                        float r = radius * t;
                        Vector2 pt = {
                            screenPos.x + cosf(armAngle) * r,
                            screenPos.y + sinf(armAngle) * r * 0.35f
                        };
                        float thick = ((1.0f - t) * 2.2f + 0.7f) * depthFactor;
                        DrawLineEx(prevPt, pt, thick, ColorAlpha(portalCol, (1.0f - t) * lifeRatio));
                        prevPt = pt;
                    }
                }
            }
            // 5. LIGHT RAYS
            else if (metalPool[i].type == PARTICLE_SLASH) {
                float len = metalPool[i].length * lifeRatio * depthFactor;
                float thick = metalPool[i].thickness * lifeRatio * depthFactor;
                float rayAngle = metalPool[i].angle;
                
                Vector2 tip = {
                    screenPos.x + cosf(rayAngle) * len,
                    screenPos.y + sinf(rayAngle) * len
                };
                Vector2 perpDir = { -sinf(rayAngle), cosf(rayAngle) };
                
                Vector2 b1 = {
                    screenPos.x + perpDir.x * thick,
                    screenPos.y + perpDir.y * thick
                };
                Vector2 b2 = {
                    screenPos.x - perpDir.x * thick,
                    screenPos.y - perpDir.y * thick
                };
                
                Color rayCol = ColorAlpha(GOLD, 0.45f * lifeRatio);
                DrawTriangle(b1, b2, tip, rayCol);
                DrawLineEx(screenPos, tip, thick * 0.35f, ColorAlpha(WHITE, lifeRatio));
            }
            // 6. IMPACT SHOCKWAVES
            else if (metalPool[i].type == PARTICLE_SHOCKWAVE) {
                float progress = 1.0f - lifeRatio;
                float currentRad = metalPool[i].length * progress * depthFactor;
                
                unsigned char waveIntensity = (unsigned char)(240.0f * lifeRatio);
                Color waveCol = { waveIntensity, (unsigned char)(waveIntensity * 0.7f), (unsigned char)(waveIntensity * 0.15f), 255 };
                
                DrawRing(screenPos, currentRad * 0.88f, currentRad, 0.0f, 360.0f, 28, waveCol);
                DrawRing(screenPos, currentRad * 0.96f, currentRad, 0.0f, 360.0f, 28, WHITE);
            }
            // 7. SPARKLING STARDUST
            else if (metalPool[i].type == PARTICLE_STARDUST) {
                float sparkle = sinf(time * 18.0f + (float)i);
                float rad = metalPool[i].length * lifeRatio * (0.6f + 0.4f * sparkle) * depthFactor;
                
                Color dustCol = ColorAlpha(GOLD, 0.4f * lifeRatio);
                DrawCircleGradient((int)screenPos.x, (int)screenPos.y, rad * 2.2f, dustCol, BLANK);
                DrawCircle((int)screenPos.x, (int)screenPos.y, rad * 0.5f, ColorAlpha(WHITE, lifeRatio));
            }
        }
        
        EndBlendMode();
    EndTextureMode();

    SetShaderValue(metalShader, timeLocMetal, &time, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(metalShader);
        DrawTextureRec(metalCanvas.texture, (Rectangle){ 0, 0, (float)metalCanvas.texture.width, (float)-metalCanvas.texture.height }, (Vector2){ 0, 0 }, WHITE);
    EndShaderMode();
}

void UnloadMetalSkill(void) {
    UnloadTexture(swordSprite);
    UnloadShader(metalShader);
    UnloadRenderTexture(metalCanvas);
}

int GetMetalSkillProjectiles(SkillProjectile* outProjectiles, int maxProjectiles) {
    int count = 0;
    for (int i = 0; i < MAX_METAL_PARTICLES; i++) {
        if (metalPool[i].active && metalPool[i].type == PARTICLE_SWORD && count < maxProjectiles) {
            outProjectiles[count].position = metalPool[i].position;
            outProjectiles[count].radius = 9.0f * metalPool[i].scale;
            outProjectiles[count].active = true;
            count++;
        }
    }
    return count;
}

void DeactivateMetalProjectile(int index) {
    int count = 0;
    for (int i = 0; i < MAX_METAL_PARTICLES; i++) {
        if (metalPool[i].active && metalPool[i].type == PARTICLE_SWORD) {
            if (count == index) {
                metalPool[i].active = false;
                
                float scale = metalPool[i].scale;
                if (scale < 0.5f) scale = 0.5f;
                Vector3 zeroTarget = {0, 0, 0};
                
                // Exploding sword shards in 3D hemisphere
                int shardCount = GetRandomValue(8, 14);
                for (int s = 0; s < shardCount; s++) {
                    float shardAngle = (float)GetRandomValue(0, 360) * DEG2RAD;
                    float pitchAngle = (float)GetRandomValue(15, 75) * DEG2RAD;
                    float speed = (float)GetRandomValue(250, 600) * scale;
                    Vector3 shardVel = {
                        cosf(shardAngle) * speed * cosf(pitchAngle),
                        sinf(pitchAngle) * speed + 100.0f,
                        sinf(shardAngle) * speed * cosf(pitchAngle)
                    };
                    SpawnMetal(PARTICLE_SHARD, metalPool[i].position, shardVel, (float)GetRandomValue(5, 10) * scale, GetRandomValue(1, 3), 0.55f, zeroTarget, (float)GetRandomValue(0, 360), 0.0f, scale);
                }

                int sparkCount = GetRandomValue(12, 18);
                for (int s = 0; s < sparkCount; s++) {
                    float sparkAngle = (float)GetRandomValue(0, 360) * DEG2RAD;
                    float pitchAngle = (float)GetRandomValue(-45, 45) * DEG2RAD;
                    float speed = (float)GetRandomValue(350, 750) * scale;
                    Vector3 sparkVel = {
                        cosf(sparkAngle) * speed * cosf(pitchAngle),
                        sinf(pitchAngle) * speed,
                        sinf(sparkAngle) * speed * cosf(pitchAngle)
                    };
                    SpawnMetal(PARTICLE_SPARK, metalPool[i].position, sparkVel, (float)GetRandomValue(4, 10), (float)GetRandomValue(1, 2), 0.3f, zeroTarget, 0.0f, 0.0f, scale);
                }

                int slashCount = GetRandomValue(3, 4); 
                for (int s = 0; s < slashCount; s++) {
                    float rayAngle = (float)GetRandomValue(0, 360) * DEG2RAD;
                    float rayLen = (float)GetRandomValue(33, 62) * scale;
                    SpawnMetal(PARTICLE_SLASH, metalPool[i].position, (Vector3){0,0,0}, rayLen, 2.6f * scale, 0.22f, zeroTarget, rayAngle, 0.0f, scale);
                }

                SpawnMetal(PARTICLE_SHOCKWAVE, metalPool[i].position, (Vector3){0,0,0}, 30.0f * scale, 2.0f * scale, 0.22f, zeroTarget, 0.0f, 0.0f, scale);
                
                int stardustCount = GetRandomValue(12, 18);
                for (int s = 0; s < stardustCount; s++) {
                    float dustAngle = (float)GetRandomValue(0, 360) * DEG2RAD;
                    float speed = (float)GetRandomValue(100, 260) * scale;
                    Vector3 dustVel = {
                        cosf(dustAngle) * speed * 0.5f,
                        speed + 100.0f,
                        sinf(dustAngle) * speed * 0.5f
                    };
                    float dustLife = (float)GetRandomValue(6, 12) / 10.0f;
                    SpawnMetal(PARTICLE_STARDUST, metalPool[i].position, dustVel, (float)GetRandomValue(2, 4) * scale, 1.0f, dustLife, zeroTarget, 0.0f, 0.0f, scale);
                }
                break;
            }
            count++;
        }
    }
}