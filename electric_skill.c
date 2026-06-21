#include "electric_skill.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_PARTICLES 2000
#define MAX_EMITTERS 10

// ---- Electric Skill Travel Settings ----
#define ELECTRIC_TRAVEL_SPEED 1.6f // Takes ~0.6 seconds to reach target
#define ELECTRIC_SHOCK_DURATION 0.9f // Electrocution/vibration duration

// ---- Particle Types ----
typedef enum {
    PARTICLE_SPARK = 0,       // Floating spark that curves in magnetic fields
    PARTICLE_ARC = 1,         // Tiny crackling discharge line
    PARTICLE_SHOCKWAVE = 2,   // Expanding plasma ring
    PARTICLE_BURST_SPARK = 3  // Fast-flying spark on impact
} ElectricParticleType;

// ---- Emitter Structure ----
typedef struct {
    bool active;
    Vector2 startPos;
    Vector2 targetPos;
    Vector2 currentPos;
    float progress;
    float durationTimer;
    float spawnAccumulator;
    float erraticTimer;
    bool impacted;
    float lightningFlashTimer;
    Vector2 lightningPath[24];
    int lightningPathCount;
    Vector2 branchPath1[12];
    int branchPath1Count;
    Vector2 branchPath2[12];
    int branchPath2Count;
    float sizeScale; // Hệ số thu phóng sét và cầu điện plasma
} ElectricEmitter;

// ---- Particle Structure ----
typedef struct {
    bool active;
    int type;
    Vector2 position;
    Vector2 velocity;
    float radius;
    float lifetime;
    float maxLifetime;
    float angle;
    float frequency;
    float amplitude;
    float time;
    Vector2 points[6];
    int pointCount;
} ElectricParticle;

static ElectricParticle particlePool[MAX_PARTICLES];
static ElectricEmitter emitters[MAX_EMITTERS];
static int lastUsedParticle = 0;

static RenderTexture2D canvasTexture;
static Shader electricShader;
static int timeLoc;

// ---- Math Helpers ----
static float Random01(void) {
    return (float)GetRandomValue(0, 10000) / 10000.0f;
}

// Generates a jagged path between two points using midpoint displacement
static void GenerateJaggedPath(Vector2 start, Vector2 end, Vector2 *outPoints, int *outCount, int maxPoints, float maxDisplacement) {
    if (maxPoints < 2) return;
    outPoints[0] = start;
    outPoints[maxPoints - 1] = end;
    *outCount = maxPoints;
    
    Vector2 dir = Vector2Subtract(end, start);
    float length = Vector2Length(dir);
    if (length < 1.0f) {
        *outCount = 2;
        outPoints[0] = start;
        outPoints[1] = end;
        return;
    }
    Vector2 perp = { -dir.y / length, dir.x / length };
    
    for (int i = 1; i < maxPoints - 1; i++) {
        float t = (float)i / (maxPoints - 1);
        Vector2 basePos = Vector2Add(start, Vector2Scale(dir, t));
        float envelope = sinf(t * 3.14159265f); // 0 at ends, 1 in middle
        float dispVal = ((float)GetRandomValue(-1000, 1000) / 1000.0f) * maxDisplacement * envelope;
        outPoints[i] = Vector2Add(basePos, Vector2Scale(perp, dispVal));
    }
}

static void DrawGlowLine(Vector2 start, Vector2 end, float baseWidth, float alphaScale) {
    // Layer 1: Wide outer glow (low density)
    DrawLineEx(start, end, baseWidth * 2.5f, ColorAlpha(WHITE, alphaScale * 0.12f));
    DrawCircleV(end, baseWidth * 2.5f * 0.5f, ColorAlpha(WHITE, alphaScale * 0.12f));
    DrawCircleV(start, baseWidth * 2.5f * 0.5f, ColorAlpha(WHITE, alphaScale * 0.12f));

    // Layer 2: Medium glow
    DrawLineEx(start, end, baseWidth * 1.6f, ColorAlpha(WHITE, alphaScale * 0.35f));
    DrawCircleV(end, baseWidth * 1.6f * 0.5f, ColorAlpha(WHITE, alphaScale * 0.35f));
    DrawCircleV(start, baseWidth * 1.6f * 0.5f, ColorAlpha(WHITE, alphaScale * 0.35f));

    // Layer 3: Inner glow
    DrawLineEx(start, end, baseWidth * 0.9f, ColorAlpha(WHITE, alphaScale * 0.65f));
    DrawCircleV(end, baseWidth * 0.9f * 0.5f, ColorAlpha(WHITE, alphaScale * 0.65f));
    DrawCircleV(start, baseWidth * 0.9f * 0.5f, ColorAlpha(WHITE, alphaScale * 0.65f));

    // Layer 4: Hot core
    DrawLineEx(start, end, baseWidth * 0.35f, ColorAlpha(WHITE, alphaScale * 0.95f));
    DrawCircleV(end, baseWidth * 0.35f * 0.5f, ColorAlpha(WHITE, alphaScale * 0.95f));
    DrawCircleV(start, baseWidth * 0.35f * 0.5f, ColorAlpha(WHITE, alphaScale * 0.95f));
}

static void DrawGlowLineThin(Vector2 start, Vector2 end, float baseWidth, float alphaScale) {
    // Layer 1: Outer glow
    DrawLineEx(start, end, baseWidth * 2.0f, ColorAlpha(WHITE, alphaScale * 0.25f));
    DrawCircleV(end, baseWidth * 2.0f * 0.5f, ColorAlpha(WHITE, alphaScale * 0.25f));
    DrawCircleV(start, baseWidth * 2.0f * 0.5f, ColorAlpha(WHITE, alphaScale * 0.25f));

    // Layer 2: Core
    DrawLineEx(start, end, baseWidth * 0.7f, ColorAlpha(WHITE, alphaScale * 0.90f));
    DrawCircleV(end, baseWidth * 0.7f * 0.5f, ColorAlpha(WHITE, alphaScale * 0.90f));
    DrawCircleV(start, baseWidth * 0.7f * 0.5f, ColorAlpha(WHITE, alphaScale * 0.90f));
}


static void SpawnParticle(int type, Vector2 pos, Vector2 vel, float baseRadius, float maxLife) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        int index = (lastUsedParticle + i) % MAX_PARTICLES;
        if (!particlePool[index].active) {
            particlePool[index].type = type;
            particlePool[index].position = pos;
            particlePool[index].velocity = vel;
            particlePool[index].radius = baseRadius;
            particlePool[index].maxLifetime = maxLife;
            particlePool[index].lifetime = maxLife;
            particlePool[index].active = true;
            particlePool[index].time = 0.0f;
            particlePool[index].frequency = 5.0f + Random01() * 15.0f;
            particlePool[index].amplitude = 80.0f + Random01() * 180.0f;
            particlePool[index].angle = Random01() * 2.0f * 3.14159265f;
            
            // For arcs, pre-generate a local small jagged path
            if (type == PARTICLE_ARC) {
                Vector2 arcEnd = {
                    pos.x + (float)GetRandomValue(-60, 60),
                    pos.y + (float)GetRandomValue(-60, 60)
                };
                GenerateJaggedPath(pos, arcEnd, particlePool[index].points, &particlePool[index].pointCount, 5, 12.0f);
            }
            
            lastUsedParticle = (index + 1) % MAX_PARTICLES;
            break;
        }
    }
}

void InitElectricSkill(int screenWidth, int screenHeight) {
    canvasTexture = LoadRenderTexture(screenWidth, screenHeight);
    electricShader = LoadShader(0, "electric.fs");
    timeLoc = GetShaderLocation(electricShader, "u_time");

    for (int i = 0; i < MAX_PARTICLES; i++)
        particlePool[i].active = false;
    for (int i = 0; i < MAX_EMITTERS; i++)
        emitters[i].active = false;
}

void CastElectricSkill(Vector2 startPos, Vector2 target, float sizeScale) {
    for (int i = 0; i < MAX_EMITTERS; i++) {
        if (!emitters[i].active) {
            emitters[i].active = true;
            emitters[i].startPos = startPos;
            emitters[i].targetPos = target;
            emitters[i].currentPos = startPos;
            emitters[i].progress = 0.0f;
            emitters[i].durationTimer = ELECTRIC_SHOCK_DURATION;
            emitters[i].spawnAccumulator = 0.0f;
            emitters[i].erraticTimer = 0.0f;
            emitters[i].impacted = false;
            emitters[i].lightningFlashTimer = 0.0f;
            emitters[i].lightningPathCount = 0;
            emitters[i].branchPath1Count = 0;
            emitters[i].branchPath2Count = 0;
            emitters[i].sizeScale = sizeScale;
            break;
        }
    }

    // Spark blast at casting origin - scale counts and radii by sizeScale
    int burstCount = 15 * sizeScale;
    for (int s = 0; s < burstCount; s++) {
        float angle = Random01() * 2.0f * 3.14159265f;
        float speed = 100.0f + Random01() * 250.0f;
        Vector2 vel = { cosf(angle) * speed, sinf(angle) * speed };
        SpawnParticle(PARTICLE_SPARK, startPos, vel, (3.0f + Random01() * 4.0f) * sizeScale, 0.4f + Random01() * 0.4f);
    }
    
    // Initial plasma muzzle burst
    SpawnParticle(PARTICLE_SHOCKWAVE, startPos, (Vector2){0, 0}, 30.0f * sizeScale, 0.35f);
}

void UpdateElectricSkill(float dt) {
    for (int e = 0; e < MAX_EMITTERS; e++) {
        if (!emitters[e].active)
            continue;

        emitters[e].erraticTimer += dt;

        if (!emitters[e].impacted) {
            // 1. PROJECTILE TRAVEL PHASE
            emitters[e].progress += dt * ELECTRIC_TRAVEL_SPEED;

            if (emitters[e].progress >= 1.0f) {
                // DETONATION/IMPACT TRIGGER
                emitters[e].progress = 1.0f;
                emitters[e].impacted = true;
                emitters[e].currentPos = emitters[e].targetPos;

                float scale = emitters[e].sizeScale;

                // A. Ground Crack shockwaves
                SpawnParticle(PARTICLE_SHOCKWAVE, emitters[e].targetPos, (Vector2){0, 0}, 50.0f * scale, 0.5f);
                SpawnParticle(PARTICLE_SHOCKWAVE, emitters[e].targetPos, (Vector2){0, 0}, 90.0f * scale, 0.7f);

                // B. Explosion particles
                int sparkCount = 35 * scale;
                for (int s = 0; s < sparkCount; s++) {
                    float angle = Random01() * 2.0f * 3.14159265f;
                    float speed = (150.0f + Random01() * 450.0f) * scale;
                    Vector2 vel = { cosf(angle) * speed, sinf(angle) * speed };
                    SpawnParticle(PARTICLE_BURST_SPARK, emitters[e].targetPos, vel, (2.5f + Random01() * 3.5f) * scale, 0.5f + Random01() * 0.5f);
                }

                // C. Discharges around the enemy
                int arcCount = 8 * scale;
                for (int s = 0; s < arcCount; s++) {
                    Vector2 offset = { (float)GetRandomValue(-40, 40) * scale, (float)GetRandomValue(-40, 40) * scale };
                    SpawnParticle(PARTICLE_ARC, Vector2Add(emitters[e].targetPos, offset), (Vector2){0, 0}, 1.2f * scale, 0.25f);
                }
                
                // Pre-generate lightning paths
                Vector2 skyPos = { emitters[e].targetPos.x + (float)GetRandomValue(-80, 80) * scale, -50.0f };
                GenerateJaggedPath(skyPos, emitters[e].targetPos, emitters[e].lightningPath, &emitters[e].lightningPathCount, 16, 35.0f * scale);
                
                // Branches
                if (emitters[e].lightningPathCount > 8) {
                    Vector2 bStart = emitters[e].lightningPath[8];
                    Vector2 bEnd = { bStart.x + (float)GetRandomValue(-150, 150) * scale, bStart.y + (float)GetRandomValue(50, 200) * scale };
                    GenerateJaggedPath(bStart, bEnd, emitters[e].branchPath1, &emitters[e].branchPath1Count, 8, 15.0f * scale);
                }
                if (emitters[e].lightningPathCount > 12) {
                    Vector2 bStart = emitters[e].lightningPath[12];
                    Vector2 bEnd = { bStart.x + (float)GetRandomValue(-150, 150) * scale, bStart.y + (float)GetRandomValue(50, 200) * scale };
                    GenerateJaggedPath(bStart, bEnd, emitters[e].branchPath2, &emitters[e].branchPath2Count, 8, 15.0f * scale);
                }
            } else {
                // Continue travel
                Vector2 basePos = Vector2Lerp(emitters[e].startPos, emitters[e].targetPos, emitters[e].progress);
                Vector2 dir = Vector2Normalize(Vector2Subtract(emitters[e].targetPos, emitters[e].startPos));
                Vector2 perp = { -dir.y, dir.x };
                
                // Erratic side-to-side wobble
                float wobble = sinf(emitters[e].progress * 25.0f) * 20.0f * (1.0f - emitters[e].progress);
                emitters[e].currentPos = Vector2Add(basePos, Vector2Scale(perp, wobble));

                // Spawn trails
                emitters[e].spawnAccumulator += dt;
                if (emitters[e].spawnAccumulator >= 0.015f) {
                    // Small trailing sparks
                    Vector2 backVel = Vector2Scale(dir, -50.0f);
                    Vector2 spreadVel = { (float)GetRandomValue(-30, 30), (float)GetRandomValue(-30, 30) };
                    SpawnParticle(PARTICLE_SPARK, emitters[e].currentPos, Vector2Add(backVel, spreadVel), 2.5f + Random01() * 2.5f, 0.3f + Random01() * 0.3f);
                    
                    // Erratic discharges
                    if (GetRandomValue(1, 10) <= 3) {
                        SpawnParticle(PARTICLE_ARC, emitters[e].currentPos, (Vector2){0,0}, 1.5f, 0.15f);
                    }
                    emitters[e].spawnAccumulator = 0.0f;
                }
            }
        } else {
            // 2. SHOCK/ELECTROCUTION PHASE
            emitters[e].durationTimer -= dt;
            if (emitters[e].durationTimer <= 0.0f) {
                emitters[e].active = false;
                continue;
            }

            // Regenerate lightning paths frequently to make it flash/writhe
            emitters[e].lightningFlashTimer += dt;
            if (emitters[e].lightningFlashTimer >= 0.05f) {
                emitters[e].lightningFlashTimer = 0.0f;
                Vector2 skyPos = { emitters[e].targetPos.x + (float)GetRandomValue(-80, 80), -50.0f };
                GenerateJaggedPath(skyPos, emitters[e].targetPos, emitters[e].lightningPath, &emitters[e].lightningPathCount, 16, 35.0f);
                
                // Branches
                if (emitters[e].lightningPathCount > 8) {
                    Vector2 bStart = emitters[e].lightningPath[8];
                    Vector2 bEnd = { bStart.x + (float)GetRandomValue(-150, 150), bStart.y + (float)GetRandomValue(50, 200) };
                    GenerateJaggedPath(bStart, bEnd, emitters[e].branchPath1, &emitters[e].branchPath1Count, 8, 15.0f);
                }
                if (emitters[e].lightningPathCount > 12) {
                    Vector2 bStart = emitters[e].lightningPath[12];
                    Vector2 bEnd = { bStart.x + (float)GetRandomValue(-150, 150), bStart.y + (float)GetRandomValue(50, 200) };
                    GenerateJaggedPath(bStart, bEnd, emitters[e].branchPath2, &emitters[e].branchPath2Count, 8, 15.0f);
                }
            }

            // Spawn sparks during electrocution
            if (GetRandomValue(1, 10) <= 6) {
                float angle = Random01() * 2.0f * 3.14159265f;
                float speed = 80.0f + Random01() * 250.0f;
                Vector2 vel = { cosf(angle) * speed, sinf(angle) * speed };
                SpawnParticle(PARTICLE_SPARK, emitters[e].targetPos, vel, 2.0f + Random01() * 3.0f, 0.2f + Random01() * 0.4f);
            }
            if (GetRandomValue(1, 100) <= 15) {
                Vector2 offset = { (float)GetRandomValue(-30, 30), (float)GetRandomValue(-30, 30) };
                SpawnParticle(PARTICLE_ARC, Vector2Add(emitters[e].targetPos, offset), (Vector2){0,0}, 1.5f, 0.15f);
            }
        }
    }

    // 3. UPDATE PARTICLES
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!particlePool[i].active)
            continue;

        particlePool[i].lifetime -= dt;
        if (particlePool[i].lifetime <= 0.0f) {
            particlePool[i].active = false;
            continue;
        }

        particlePool[i].time += dt;

        if (particlePool[i].type == PARTICLE_SPARK || particlePool[i].type == PARTICLE_BURST_SPARK) {
            // Apply curved trajectories (simulating magnetic force field)
            Vector2 forward = particlePool[i].velocity;
            Vector2 perp = { -forward.y, forward.x };
            float len = Vector2Length(perp);
            if (len > 0.0f) perp = Vector2Scale(perp, 1.0f / len);
            
            float lifeRatio = particlePool[i].lifetime / particlePool[i].maxLifetime;
            float wave = sinf(particlePool[i].time * particlePool[i].frequency) * particlePool[i].amplitude * lifeRatio;
            
            // Decelerate the forward velocity
            particlePool[i].velocity.x *= (1.0f - 1.5f * dt);
            particlePool[i].velocity.y *= (1.0f - 1.5f * dt);

            // Add gravity slightly
            particlePool[i].velocity.y += 50.0f * dt;

            // Move
            particlePool[i].position.x += particlePool[i].velocity.x * dt + perp.x * wave * dt;
            particlePool[i].position.y += particlePool[i].velocity.y * dt + perp.y * wave * dt;
        } 
        else if (particlePool[i].type == PARTICLE_SHOCKWAVE) {
            // Shockwaves expand in size and don't translate
            particlePool[i].velocity.y = 0;
            particlePool[i].velocity.x = 0;
        }
        else if (particlePool[i].type == PARTICLE_ARC) {
            // Arcs are stationary, they just crackle and fade
        }
    }
}

void DrawElectricSkill(void) {
    bool active = false;
    for (int i = 0; i < MAX_EMITTERS; i++) {
        if (emitters[i].active) { active = true; break; }
    }
    if (!active) {
        for (int i = 0; i < MAX_PARTICLES; i++) {
            if (particlePool[i].active) { active = true; break; }
        }
    }
    if (!active) return;

    float time = GetTime();

    // PASS 1: Draw density fields onto render texture canvas
    BeginTextureMode(canvasTexture);
        ClearBackground(BLANK);
        BeginBlendMode(BLEND_ALPHA);

            // A. Draw traveling plasma orb
            for (int e = 0; e < MAX_EMITTERS; e++) {
                if (!emitters[e].active || emitters[e].impacted)
                    continue;

                float sizePulse = 1.0f + sinf(emitters[e].erraticTimer * 30.0f) * 0.15f;
                float r = 18.0f * sizePulse * emitters[e].sizeScale;

                // Multiple overlapping gradient spheres to build core density
                DrawCircleGradient((int)emitters[e].currentPos.x, (int)emitters[e].currentPos.y, r * 1.4f, ColorAlpha(WHITE, 0.35f), BLANK);
                DrawCircleGradient((int)emitters[e].currentPos.x, (int)emitters[e].currentPos.y, r * 0.9f, ColorAlpha(WHITE, 0.70f), BLANK);
                DrawCircleGradient((int)emitters[e].currentPos.x, (int)emitters[e].currentPos.y, r * 0.4f, ColorAlpha(WHITE, 1.0f), BLANK);
            }

            // B. Draw vertical lightning strike & branches
            for (int e = 0; e < MAX_EMITTERS; e++) {
                if (!emitters[e].active || !emitters[e].impacted)
                    continue;

                float lifeRatio = emitters[e].durationTimer / ELECTRIC_SHOCK_DURATION;
                
                // Lightning bolt is active mostly in the first 70% of the shock time
                if (lifeRatio > 0.3f) {
                    float boltFade = Clamp((lifeRatio - 0.3f) / 0.7f, 0.0f, 1.0f);
                    
                    // Main bolt
                    if (emitters[e].lightningPathCount > 1) {
                        float width = 6.0f * boltFade * emitters[e].sizeScale;
                        for (int i = 0; i < emitters[e].lightningPathCount - 1; i++) {
                            DrawGlowLine(emitters[e].lightningPath[i], emitters[e].lightningPath[i + 1], width, 0.95f * boltFade);
                        }
                    }

                    // Branch 1
                    if (emitters[e].branchPath1Count > 1) {
                        float width = 3.0f * boltFade * emitters[e].sizeScale;
                        for (int i = 0; i < emitters[e].branchPath1Count - 1; i++) {
                            DrawGlowLineThin(emitters[e].branchPath1[i], emitters[e].branchPath1[i + 1], width, 0.85f * boltFade);
                        }
                    }

                    // Branch 2
                    if (emitters[e].branchPath2Count > 1) {
                        float width = 3.0f * boltFade * emitters[e].sizeScale;
                        for (int i = 0; i < emitters[e].branchPath2Count - 1; i++) {
                            DrawGlowLineThin(emitters[e].branchPath2[i], emitters[e].branchPath2[i + 1], width, 0.85f * boltFade);
                        }
                    }
                }
            }

            // C. Draw Particles (Sparks, Arcs, Shockwaves)
            for (int i = 0; i < MAX_PARTICLES; i++) {
                if (!particlePool[i].active)
                    continue;

                float lifeRatio = particlePool[i].lifetime / particlePool[i].maxLifetime;

                if (particlePool[i].type == PARTICLE_SPARK || particlePool[i].type == PARTICLE_BURST_SPARK) {
                    float r = particlePool[i].radius * (0.4f + 0.6f * lifeRatio);
                    // Draw spark core density
                    DrawCircleGradient((int)particlePool[i].position.x, (int)particlePool[i].position.y, r * 1.5f, ColorAlpha(WHITE, lifeRatio * 0.5f), BLANK);
                    DrawCircleGradient((int)particlePool[i].position.x, (int)particlePool[i].position.y, r * 0.7f, ColorAlpha(WHITE, lifeRatio * 0.9f), BLANK);
                } 
                else if (particlePool[i].type == PARTICLE_ARC) {
                    float width = particlePool[i].radius * lifeRatio;
                    if (particlePool[i].pointCount > 1) {
                        for (int k = 0; k < particlePool[i].pointCount - 1; k++) {
                            DrawGlowLineThin(particlePool[i].points[k], particlePool[i].points[k + 1], width, lifeRatio);
                        }
                    }
                } 
                else if (particlePool[i].type == PARTICLE_SHOCKWAVE) {
                    float shockProgress = 1.0f - lifeRatio;
                    float currentRadius = particlePool[i].radius * (0.2f + 0.8f * shockProgress);
                    float ringAlpha = lifeRatio * 0.8f;
                    
                    // Draw nested rings using lines to create plasma expansion profile
                    for (float offset = 0.0f; offset < 6.0f; offset += 1.5f) {
                        DrawCircleLines((int)particlePool[i].position.x, (int)particlePool[i].position.y, currentRadius - offset, ColorAlpha(WHITE, ringAlpha * (1.0f - offset / 6.0f)));
                    }
                }
            }

        EndBlendMode();
    EndTextureMode();

    // PASS 2: Apply multi-channel electric shader and render to screen
    SetShaderValue(electricShader, timeLoc, &time, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(electricShader);
        DrawTextureRec(
            canvasTexture.texture,
            (Rectangle){ 0, 0, (float)canvasTexture.texture.width, (float)-canvasTexture.texture.height },
            (Vector2){ 0, 0 },
            WHITE
        );
    EndShaderMode();
}

void UnloadElectricSkill(void) {
    UnloadShader(electricShader);
    UnloadRenderTexture(canvasTexture);
}

bool IsElectricSkillShocking(void) {
    for (int e = 0; e < MAX_EMITTERS; e++) {
        if (emitters[e].active && emitters[e].impacted) {
            return true;
        }
    }
    return false;
}

int GetElectricSkillProjectiles(SkillProjectile* outProjectiles, int maxProjectiles) {
    int count = 0;
    for (int i = 0; i < MAX_EMITTERS; i++) {
        if (emitters[i].active && !emitters[i].impacted && count < maxProjectiles) {
            outProjectiles[count].position = emitters[i].currentPos;
            outProjectiles[count].radius = 18.0f * emitters[i].sizeScale; // Bán kính quả cầu sét tỷ lệ theo cỡ
            outProjectiles[count].active = true;
            count++;
        }
    }
    return count;
}

void DeactivateElectricProjectile(int index) {
    int count = 0;
    for (int i = 0; i < MAX_EMITTERS; i++) {
        if (emitters[i].active && !emitters[i].impacted) {
            if (count == index) {
                emitters[i].progress = 1.0f;
                emitters[i].impacted = true;
                
                float scale = emitters[i].sizeScale;

                // Kích nổ ngay tại vị trí va chạm với bán kính tỷ lệ theo cỡ
                SpawnParticle(PARTICLE_SHOCKWAVE, emitters[i].currentPos, (Vector2){0, 0}, 50.0f * scale, 0.5f);
                SpawnParticle(PARTICLE_SHOCKWAVE, emitters[i].currentPos, (Vector2){0, 0}, 90.0f * scale, 0.7f);

                int sparkCount = 35 * scale;
                for (int s = 0; s < sparkCount; s++) {
                    float angle = Random01() * 2.0f * 3.14159265f;
                    float speed = (150.0f + Random01() * 450.0f) * scale;
                    Vector2 vel = { cosf(angle) * speed, sinf(angle) * speed };
                    SpawnParticle(PARTICLE_BURST_SPARK, emitters[i].currentPos, vel, (2.5f + Random01() * 3.5f) * scale, 0.5f + Random01() * 0.5f);
                }

                int arcCount = 8 * scale;
                for (int s = 0; s < arcCount; s++) {
                    Vector2 offset = { (float)GetRandomValue(-40, 40) * scale, (float)GetRandomValue(-40, 40) * scale };
                    SpawnParticle(PARTICLE_ARC, Vector2Add(emitters[i].currentPos, offset), (Vector2){0, 0}, 1.2f * scale, 0.25f);
                }
                
                // Tạo đường dẫn sét đánh dọc từ trên trời xuống điểm va chạm (giãn độ lệch sét)
                Vector2 skyPos = { emitters[i].currentPos.x + (float)GetRandomValue(-80, 80) * scale, -50.0f };
                GenerateJaggedPath(skyPos, emitters[i].currentPos, emitters[i].lightningPath, &emitters[i].lightningPathCount, 16, 35.0f * scale);
                
                if (emitters[i].lightningPathCount > 8) {
                    Vector2 bStart = emitters[i].lightningPath[8];
                    Vector2 bEnd = { bStart.x + (float)GetRandomValue(-150, 150) * scale, bStart.y + (float)GetRandomValue(50, 200) * scale };
                    GenerateJaggedPath(bStart, bEnd, emitters[i].branchPath1, &emitters[i].branchPath1Count, 8, 15.0f * scale);
                }
                if (emitters[i].lightningPathCount > 12) {
                    Vector2 bStart = emitters[i].lightningPath[12];
                    Vector2 bEnd = { bStart.x + (float)GetRandomValue(-150, 150) * scale, bStart.y + (float)GetRandomValue(50, 200) * scale };
                    GenerateJaggedPath(bStart, bEnd, emitters[i].branchPath2, &emitters[i].branchPath2Count, 8, 15.0f * scale);
                }
                
                // Đồng thời dịch targetPos về vị trí va chạm hiện tại để sét bám theo
                emitters[i].targetPos = emitters[i].currentPos;
                break;
            }
            count++;
        }
    }
}
 