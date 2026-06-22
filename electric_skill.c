#include "electric_skill.h"
#include "skill_manager.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_PARTICLES 2000
#define MAX_EMITTERS 10

// ---- Electric Skill Travel Settings ----
#define ELECTRIC_TRAVEL_SPEED 1.6f // Mất ~0.6 giây để chạm mục tiêu
#define ELECTRIC_SHOCK_DURATION 0.9f // Thời gian điện giật/rung lắc

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
    Vector3 startPos;
    Vector3 targetPos;
    Vector3 currentPos;
    float progress;
    float durationTimer;
    float spawnAccumulator;
    float erraticTimer;
    bool impacted;
    float lightningFlashTimer;
    Vector3 lightningPath[24];
    int lightningPathCount;
    Vector3 branchPath1[12];
    int branchPath1Count;
    Vector3 branchPath2[12];
    int branchPath2Count;
    float sizeScale; // Hệ số thu phóng sét và cầu điện plasma
} ElectricEmitter;

// ---- Particle Structure ----
typedef struct {
    bool active;
    int type;
    Vector3 position;
    Vector3 velocity;
    float radius;
    float lifetime;
    float maxLifetime;
    float angle;
    float frequency;
    float amplitude;
    float time;
    Vector3 points[6];
    int pointCount;
} ElectricParticle;

static ElectricParticle particlePool[MAX_PARTICLES];
static ElectricEmitter emitters[MAX_EMITTERS];
static int lastUsedParticle = 0;

static RenderTexture2D canvasTexture;
static Shader electricShader;
static int timeLoc;

// Tham chiếu camera từ main
extern Camera3D camera;

// ---- Math Helpers ----
static float Random01(void) {
    return (float)GetRandomValue(0, 10000) / 10000.0f;
}

// Tạo đường đi ngoằn ngoèo giữa hai điểm 3D bằng giải thuật midpoint displacement 3D
static void GenerateJaggedPath(Vector3 start, Vector3 end, Vector3 *outPoints, int *outCount, int maxPoints, float maxDisplacement) {
    if (maxPoints < 2) return;
    outPoints[0] = start;
    outPoints[maxPoints - 1] = end;
    *outCount = maxPoints;
    
    Vector3 dir = Vector3Subtract(end, start);
    float length = Vector3Length(dir);
    if (length < 1.0f) {
        *outCount = 2;
        outPoints[0] = start;
        outPoints[1] = end;
        return;
    }
    
    // Tạo vector vuông góc phẳng trên X-Z
    Vector3 perp = (Vector3){ -dir.z / length, 0.0f, dir.x / length };
    if (Vector3Length(perp) == 0.0f) perp = (Vector3){ 0.0f, 0.0f, 1.0f };
    perp = Vector3Normalize(perp);
    
    // Vector vuông góc thứ hai tạo mặt phẳng 3D vuông góc hoàn chỉnh
    Vector3 up = Vector3Normalize(Vector3CrossProduct(dir, perp));
    
    for (int i = 1; i < maxPoints - 1; i++) {
        float t = (float)i / (maxPoints - 1);
        Vector3 basePos = Vector3Add(start, Vector3Scale(dir, t));
        float envelope = sinf(t * 3.14159265f); // 0 ở hai đầu, 1 ở giữa
        
        float dispVal = ((float)GetRandomValue(-1000, 1000) / 1000.0f) * maxDisplacement * envelope;
        float dispVal2 = ((float)GetRandomValue(-1000, 1000) / 1000.0f) * maxDisplacement * envelope;
        
        // Dịch chuyển ngẫu nhiên trên cả hai trục vuông góc
        Vector3 offset = Vector3Add(Vector3Scale(perp, dispVal), Vector3Scale(up, dispVal2));
        outPoints[i] = Vector3Add(basePos, offset);
    }
}

static void DrawGlowLine(Vector2 start, Vector2 end, float baseWidth, float alphaScale) {
    DrawLineEx(start, end, baseWidth * 2.5f, ColorAlpha(WHITE, alphaScale * 0.12f));
    DrawCircleV(end, baseWidth * 2.5f * 0.5f, ColorAlpha(WHITE, alphaScale * 0.12f));
    DrawCircleV(start, baseWidth * 2.5f * 0.5f, ColorAlpha(WHITE, alphaScale * 0.12f));

    DrawLineEx(start, end, baseWidth * 1.6f, ColorAlpha(WHITE, alphaScale * 0.35f));
    DrawCircleV(end, baseWidth * 1.6f * 0.5f, ColorAlpha(WHITE, alphaScale * 0.35f));
    DrawCircleV(start, baseWidth * 1.6f * 0.5f, ColorAlpha(WHITE, alphaScale * 0.35f));

    DrawLineEx(start, end, baseWidth * 0.9f, ColorAlpha(WHITE, alphaScale * 0.65f));
    DrawCircleV(end, baseWidth * 0.9f * 0.5f, ColorAlpha(WHITE, alphaScale * 0.65f));
    DrawCircleV(start, baseWidth * 0.9f * 0.5f, ColorAlpha(WHITE, alphaScale * 0.65f));

    DrawLineEx(start, end, baseWidth * 0.35f, ColorAlpha(WHITE, alphaScale * 0.95f));
    DrawCircleV(end, baseWidth * 0.35f * 0.5f, ColorAlpha(WHITE, alphaScale * 0.95f));
    DrawCircleV(start, baseWidth * 0.35f * 0.5f, ColorAlpha(WHITE, alphaScale * 0.95f));
}

static void DrawGlowLineThin(Vector2 start, Vector2 end, float baseWidth, float alphaScale) {
    DrawLineEx(start, end, baseWidth * 2.0f, ColorAlpha(WHITE, alphaScale * 0.25f));
    DrawCircleV(end, baseWidth * 2.0f * 0.5f, ColorAlpha(WHITE, alphaScale * 0.25f));
    DrawCircleV(start, baseWidth * 2.0f * 0.5f, ColorAlpha(WHITE, alphaScale * 0.25f));

    DrawLineEx(start, end, baseWidth * 0.7f, ColorAlpha(WHITE, alphaScale * 0.90f));
    DrawCircleV(end, baseWidth * 0.7f * 0.5f, ColorAlpha(WHITE, alphaScale * 0.90f));
    DrawCircleV(start, baseWidth * 0.7f * 0.5f, ColorAlpha(WHITE, alphaScale * 0.90f));
}

static void SpawnParticle(int type, Vector3 pos, Vector3 vel, float baseRadius, float maxLife) {
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
            
            // Đối với tia điện cục bộ, tạo đường đi giật ngoằn ngoèo ngắn
            if (type == PARTICLE_ARC) {
                Vector3 arcEnd = {
                    pos.x + (float)GetRandomValue(-60, 60),
                    pos.y + (float)GetRandomValue(-60, 60),
                    pos.z + (float)GetRandomValue(-60, 60)
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

void CastElectricSkill(Vector3 startPos, Vector3 target, float sizeScale) {
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

    // Nổ tia lửa điện xuất phát từ caster
    int burstCount = 15 * sizeScale;
    for (int s = 0; s < burstCount; s++) {
        float angle = Random01() * 2.0f * 3.14159265f;
        float pitch = (Random01() - 0.5f) * 3.14159265f;
        float speed = 100.0f + Random01() * 250.0f;
        Vector3 vel = {
            cosf(angle) * speed * cosf(pitch),
            sinf(pitch) * speed,
            sinf(angle) * speed * cosf(pitch)
        };
        SpawnParticle(PARTICLE_SPARK, startPos, vel, (3.0f + Random01() * 4.0f) * sizeScale, 0.4f + Random01() * 0.4f);
    }
    
    // Cầu plasma lúc xuất chiêu
    SpawnParticle(PARTICLE_SHOCKWAVE, startPos, (Vector3){0, 0, 0}, 30.0f * sizeScale, 0.35f);
}

void UpdateElectricSkill(float dt) {
    for (int e = 0; e < MAX_EMITTERS; e++) {
        if (!emitters[e].active)
            continue;

        emitters[e].erraticTimer += dt;

        if (!emitters[e].impacted) {
            // 1. GIAI ĐOẠN BAY CỦA CHIÊU THỨC
            emitters[e].progress += dt * ELECTRIC_TRAVEL_SPEED;

            if (emitters[e].progress >= 1.0f) {
                emitters[e].progress = 1.0f;
                emitters[e].impacted = true;
                emitters[e].currentPos = emitters[e].targetPos;

                float scale = emitters[e].sizeScale;

                // A. Sóng kích nổ mặt đất (X-Z)
                SpawnParticle(PARTICLE_SHOCKWAVE, emitters[e].targetPos, (Vector3){0, 0, 0}, 50.0f * scale, 0.5f);
                SpawnParticle(PARTICLE_SHOCKWAVE, emitters[e].targetPos, (Vector3){0, 0, 0}, 90.0f * scale, 0.7f);

                // B. Các hạt tia điện nổ
                int sparkCount = 35 * scale;
                for (int s = 0; s < sparkCount; s++) {
                    float angle = Random01() * 2.0f * 3.14159265f;
                    float pitch = (Random01() - 0.5f) * 3.14159265f;
                    float speed = (150.0f + Random01() * 450.0f) * scale;
                    Vector3 vel = {
                        cosf(angle) * speed * cosf(pitch),
                        sinf(pitch) * speed,
                        sinf(angle) * speed * cosf(pitch)
                    };
                    SpawnParticle(PARTICLE_BURST_SPARK, emitters[e].targetPos, vel, (2.5f + Random01() * 3.5f) * scale, 0.5f + Random01() * 0.5f);
                }

                // C. Sét nhỏ phát tán xung quanh quái
                int arcCount = 8 * scale;
                for (int s = 0; s < arcCount; s++) {
                    Vector3 offset = {
                        (float)GetRandomValue(-40, 40) * scale,
                        (float)GetRandomValue(-40, 40) * scale,
                        (float)GetRandomValue(-40, 40) * scale
                    };
                    SpawnParticle(PARTICLE_ARC, Vector3Add(emitters[e].targetPos, offset), (Vector3){0, 0, 0}, 1.2f * scale, 0.25f);
                }
                
                // Tạo đường dẫn sét chính từ trên trời xuống đầu đối thủ
                Vector3 skyPos = {
                    emitters[e].targetPos.x + (float)GetRandomValue(-80, 80) * scale,
                    emitters[e].targetPos.y + 600.0f, // Sét từ trên trời cao (Y)
                    emitters[e].targetPos.z + (float)GetRandomValue(-80, 80) * scale
                };
                GenerateJaggedPath(skyPos, emitters[e].targetPos, emitters[e].lightningPath, &emitters[e].lightningPathCount, 16, 35.0f * scale);
                
                // Các nhánh sét nhỏ rẽ từ thân sét chính
                if (emitters[e].lightningPathCount > 8) {
                    Vector3 bStart = emitters[e].lightningPath[8];
                    Vector3 bEnd = {
                        bStart.x + (float)GetRandomValue(-150, 150) * scale,
                        bStart.y - (float)GetRandomValue(50, 200) * scale, // Đi xuống phía đất
                        bStart.z + (float)GetRandomValue(-150, 150) * scale
                    };
                    GenerateJaggedPath(bStart, bEnd, emitters[e].branchPath1, &emitters[e].branchPath1Count, 8, 15.0f * scale);
                }
                if (emitters[e].lightningPathCount > 12) {
                    Vector3 bStart = emitters[e].lightningPath[12];
                    Vector3 bEnd = {
                        bStart.x + (float)GetRandomValue(-150, 150) * scale,
                        bStart.y - (float)GetRandomValue(50, 200) * scale,
                        bStart.z + (float)GetRandomValue(-150, 150) * scale
                    };
                    GenerateJaggedPath(bStart, bEnd, emitters[e].branchPath2, &emitters[e].branchPath2Count, 8, 15.0f * scale);
                }
            } else {
                // Di chuyển đạn cầu sét
                Vector3 basePos = Vector3Lerp(emitters[e].startPos, emitters[e].targetPos, emitters[e].progress);
                Vector3 dir = Vector3Normalize(Vector3Subtract(emitters[e].targetPos, emitters[e].startPos));
                Vector3 perp = (Vector3){ -dir.z, 0.0f, dir.x };
                if (Vector3Length(perp) == 0.0f) perp = (Vector3){0, 0, 1};
                perp = Vector3Normalize(perp);
                
                // Dao động quấn quanh trục chuyển động
                float wobble = sinf(emitters[e].progress * 25.0f) * 20.0f * (1.0f - emitters[e].progress);
                emitters[e].currentPos = Vector3Add(basePos, Vector3Scale(perp, wobble));

                emitters[e].spawnAccumulator += dt;
                if (emitters[e].spawnAccumulator >= 0.015f) {
                    // Tia lửa văng phía sau
                    Vector3 backVel = Vector3Scale(dir, -50.0f);
                    Vector3 spreadVel = {
                        (float)GetRandomValue(-30, 30),
                        (float)GetRandomValue(-30, 30),
                        (float)GetRandomValue(-30, 30)
                    };
                    SpawnParticle(PARTICLE_SPARK, emitters[e].currentPos, Vector3Add(backVel, spreadVel), 2.5f + Random01() * 2.5f, 0.3f + Random01() * 0.3f);
                    
                    if (GetRandomValue(1, 10) <= 3) {
                        SpawnParticle(PARTICLE_ARC, emitters[e].currentPos, (Vector3){0,0,0}, 1.5f, 0.15f);
                    }
                    emitters[e].spawnAccumulator = 0.0f;
                }
            }
        } else {
            // 2. GIAI ĐOẠN ĐIỆN GIẬT (SHOCK PHASE)
            emitters[e].durationTimer -= dt;
            if (emitters[e].durationTimer <= 0.0f) {
                emitters[e].active = false;
                continue;
            }

            // Tái tạo đường sét liên tục để tạo cảm giác chớp nháy giật điên cuồng
            emitters[e].lightningFlashTimer += dt;
            if (emitters[e].lightningFlashTimer >= 0.05f) {
                emitters[e].lightningFlashTimer = 0.0f;
                Vector3 skyPos = {
                    emitters[e].targetPos.x + (float)GetRandomValue(-80, 80),
                    emitters[e].targetPos.y + 600.0f,
                    emitters[e].targetPos.z + (float)GetRandomValue(-80, 80)
                };
                GenerateJaggedPath(skyPos, emitters[e].targetPos, emitters[e].lightningPath, &emitters[e].lightningPathCount, 16, 35.0f);
                
                if (emitters[e].lightningPathCount > 8) {
                    Vector3 bStart = emitters[e].lightningPath[8];
                    Vector3 bEnd = {
                        bStart.x + (float)GetRandomValue(-150, 150),
                        bStart.y - (float)GetRandomValue(50, 200),
                        bStart.z + (float)GetRandomValue(-150, 150)
                    };
                    GenerateJaggedPath(bStart, bEnd, emitters[e].branchPath1, &emitters[e].branchPath1Count, 8, 15.0f);
                }
                if (emitters[e].lightningPathCount > 12) {
                    Vector3 bStart = emitters[e].lightningPath[12];
                    Vector3 bEnd = {
                        bStart.x + (float)GetRandomValue(-150, 150),
                        bStart.y - (float)GetRandomValue(50, 200),
                        bStart.z + (float)GetRandomValue(-150, 150)
                    };
                    GenerateJaggedPath(bStart, bEnd, emitters[e].branchPath2, &emitters[e].branchPath2Count, 8, 15.0f);
                }
            }

            // Sinh lửa điện trong khi điện giật
            if (GetRandomValue(1, 10) <= 6) {
                float angle = Random01() * 2.0f * 3.14159265f;
                float pitch = (Random01() - 0.5f) * 3.14159265f;
                float speed = 80.0f + Random01() * 250.0f;
                Vector3 vel = {
                    cosf(angle) * speed * cosf(pitch),
                    sinf(pitch) * speed,
                    sinf(angle) * speed * cosf(pitch)
                };
                SpawnParticle(PARTICLE_SPARK, emitters[e].targetPos, vel, 2.0f + Random01() * 3.0f, 0.2f + Random01() * 0.4f);
            }
            if (GetRandomValue(1, 100) <= 15) {
                Vector3 offset = {
                    (float)GetRandomValue(-30, 30),
                    (float)GetRandomValue(-30, 30),
                    (float)GetRandomValue(-30, 30)
                };
                SpawnParticle(PARTICLE_ARC, Vector3Add(emitters[e].targetPos, offset), (Vector3){0,0,0}, 1.5f, 0.15f);
            }
        }
    }

    // 3. CẬP NHẬT HẠT ĐIỆN LÔI
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
            // Tính toán chuyển động xoắn ốc 3D của hạt điện từ
            Vector3 forward = particlePool[i].velocity;
            Vector3 upDir = {0.0f, 1.0f, 0.0f};
            Vector3 perp = Vector3CrossProduct(forward, upDir);
            if (Vector3Length(perp) == 0.0f) perp = (Vector3){1.0f, 0.0f, 0.0f};
            perp = Vector3Normalize(perp);
            
            float lifeRatio = particlePool[i].lifetime / particlePool[i].maxLifetime;
            float wave = sinf(particlePool[i].time * particlePool[i].frequency) * particlePool[i].amplitude * lifeRatio;
            
            particlePool[i].velocity.x *= (1.0f - 1.5f * dt);
            particlePool[i].velocity.y *= (1.0f - 1.5f * dt);
            particlePool[i].velocity.z *= (1.0f - 1.5f * dt);

            // Trọng lực hút nhẹ
            particlePool[i].velocity.y -= 50.0f * dt;

            // Xoắn thêm trục phụ thứ 3
            Vector3 vertPerp = Vector3Normalize(Vector3CrossProduct(forward, perp));
            float wave2 = cosf(particlePool[i].time * particlePool[i].frequency) * particlePool[i].amplitude * 0.5f * lifeRatio;

            particlePool[i].position.x += particlePool[i].velocity.x * dt + perp.x * wave * dt;
            particlePool[i].position.y += particlePool[i].velocity.y * dt + vertPerp.y * wave2 * dt;
            particlePool[i].position.z += particlePool[i].velocity.z * dt + perp.z * wave * dt + vertPerp.z * wave2 * dt;
        } 
        else if (particlePool[i].type == PARTICLE_SHOCKWAVE) {
            particlePool[i].velocity = (Vector3){0, 0, 0};
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

    // PASS 1: Vẽ các trường plasma mật độ điện lên Canvas texture
    BeginTextureMode(canvasTexture);
        ClearBackground(BLANK);
        BeginBlendMode(BLEND_ALPHA);

            // A. Quả cầu đạn sét đang bay
            for (int e = 0; e < MAX_EMITTERS; e++) {
                if (!emitters[e].active || emitters[e].impacted)
                    continue;

                ProjectedPoint pt = ProjectPointCached(emitters[e].currentPos, camera);
                if (pt.behindCamera) continue;
                Vector2 screenPos = pt.screenPos;
                float depthFactor = pt.depthFactor;

                float sizePulse = 1.0f + sinf(emitters[e].erraticTimer * 30.0f) * 0.15f;
                float r = 18.0f * sizePulse * emitters[e].sizeScale * depthFactor;

                DrawCircleGradient((int)screenPos.x, (int)screenPos.y, r * 1.4f, ColorAlpha(WHITE, 0.35f), BLANK);
                DrawCircleGradient((int)screenPos.x, (int)screenPos.y, r * 0.9f, ColorAlpha(WHITE, 0.70f), BLANK);
                DrawCircleGradient((int)screenPos.x, (int)screenPos.y, r * 0.4f, ColorAlpha(WHITE, 1.0f), BLANK);
            }

            // B. Vẽ luồng lôi điện thiên kiếp giáng từ trời cao
            for (int e = 0; e < MAX_EMITTERS; e++) {
                if (!emitters[e].active || !emitters[e].impacted)
                    continue;

                float lifeRatio = emitters[e].durationTimer / ELECTRIC_SHOCK_DURATION;
                
                if (lifeRatio > 0.3f) {
                    float boltFade = Clamp((lifeRatio - 0.3f) / 0.7f, 0.0f, 1.0f);
                    
                    // Thân sét chính
                    if (emitters[e].lightningPathCount > 1) {
                        float width = 6.0f * boltFade * emitters[e].sizeScale;
                        for (int i = 0; i < emitters[e].lightningPathCount - 1; i++) {
                            Vector3 p1_3d = emitters[e].lightningPath[i];
                            Vector3 p2_3d = emitters[e].lightningPath[i + 1];
                            ProjectedPoint pt1 = ProjectPointCached(p1_3d, camera);
                            ProjectedPoint pt2 = ProjectPointCached(p2_3d, camera);
                            if (pt1.behindCamera || pt2.behindCamera) continue;
                            Vector2 p1 = pt1.screenPos;
                            Vector2 p2 = pt2.screenPos;
                            float depthFactor = pt1.depthFactor;

                            DrawGlowLine(p1, p2, width * depthFactor, 0.95f * boltFade);
                        }
                    }

                    // Nhánh rẽ sét 1
                    if (emitters[e].branchPath1Count > 1) {
                        float width = 3.0f * boltFade * emitters[e].sizeScale;
                        for (int i = 0; i < emitters[e].branchPath1Count - 1; i++) {
                            Vector3 p1_3d = emitters[e].branchPath1[i];
                            Vector3 p2_3d = emitters[e].branchPath1[i + 1];
                            ProjectedPoint pt1 = ProjectPointCached(p1_3d, camera);
                            ProjectedPoint pt2 = ProjectPointCached(p2_3d, camera);
                            if (pt1.behindCamera || pt2.behindCamera) continue;
                            Vector2 p1 = pt1.screenPos;
                            Vector2 p2 = pt2.screenPos;
                            float depthFactor = pt1.depthFactor;

                            DrawGlowLineThin(p1, p2, width * depthFactor, 0.85f * boltFade);
                        }
                    }

                    // Nhánh rẽ sét 2
                    if (emitters[e].branchPath2Count > 1) {
                        float width = 3.0f * boltFade * emitters[e].sizeScale;
                        for (int i = 0; i < emitters[e].branchPath2Count - 1; i++) {
                            Vector3 p1_3d = emitters[e].branchPath2[i];
                            Vector3 p2_3d = emitters[e].branchPath2[i + 1];
                            ProjectedPoint pt1 = ProjectPointCached(p1_3d, camera);
                            ProjectedPoint pt2 = ProjectPointCached(p2_3d, camera);
                            if (pt1.behindCamera || pt2.behindCamera) continue;
                            Vector2 p1 = pt1.screenPos;
                            Vector2 p2 = pt2.screenPos;
                            float depthFactor = pt1.depthFactor;

                            DrawGlowLineThin(p1, p2, width * depthFactor, 0.85f * boltFade);
                        }
                    }
                }
            }

            // C. Vẽ các hạt tia điện ma thuật và vòng plasma
            for (int i = 0; i < MAX_PARTICLES; i++) {
                if (!particlePool[i].active)
                    continue;

                float lifeRatio = particlePool[i].lifetime / particlePool[i].maxLifetime;
                ProjectedPoint pt = ProjectPointCached(particlePool[i].position, camera);
                if (pt.behindCamera) continue;
                Vector2 screenPos = pt.screenPos;
                float depthFactor = pt.depthFactor;

                if (particlePool[i].type == PARTICLE_SPARK || particlePool[i].type == PARTICLE_BURST_SPARK) {
                    float r = particlePool[i].radius * (0.4f + 0.6f * lifeRatio) * depthFactor;
                    DrawCircleGradient((int)screenPos.x, (int)screenPos.y, r * 1.5f, ColorAlpha(WHITE, lifeRatio * 0.5f), BLANK);
                    DrawCircleGradient((int)screenPos.x, (int)screenPos.y, r * 0.7f, ColorAlpha(WHITE, lifeRatio * 0.9f), BLANK);
                } 
                else if (particlePool[i].type == PARTICLE_ARC) {
                    float width = particlePool[i].radius * lifeRatio * depthFactor;
                    if (particlePool[i].pointCount > 1) {
                        for (int k = 0; k < particlePool[i].pointCount - 1; k++) {
                            ProjectedPoint pt1 = ProjectPointCached(particlePool[i].points[k], camera);
                            ProjectedPoint pt2 = ProjectPointCached(particlePool[i].points[k + 1], camera);
                            if (pt1.behindCamera || pt2.behindCamera) continue;
                            Vector2 p1 = pt1.screenPos;
                            Vector2 p2 = pt2.screenPos;
                            DrawGlowLineThin(p1, p2, width, lifeRatio);
                        }
                    }
                } 
                else if (particlePool[i].type == PARTICLE_SHOCKWAVE) {
                    float shockProgress = 1.0f - lifeRatio;
                    float currentRadius = particlePool[i].radius * (0.2f + 0.8f * shockProgress) * depthFactor;
                    float ringAlpha = lifeRatio * 0.8f;
                    
                    for (float offset = 0.0f; offset < 6.0f; offset += 1.5f) {
                        DrawCircleLines((int)screenPos.x, (int)screenPos.y, currentRadius - offset, ColorAlpha(WHITE, ringAlpha * (1.0f - offset / 6.0f)));
                    }
                }
            }

        EndBlendMode();
    EndTextureMode();

    // PASS 2: Áp dụng shader sét vẽ canvas lên màn hình
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
            outProjectiles[count].radius = 18.0f * emitters[i].sizeScale;
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

                SpawnParticle(PARTICLE_SHOCKWAVE, emitters[i].currentPos, (Vector3){0, 0, 0}, 50.0f * scale, 0.5f);
                SpawnParticle(PARTICLE_SHOCKWAVE, emitters[i].currentPos, (Vector3){0, 0, 0}, 90.0f * scale, 0.7f);

                int sparkCount = 35 * scale;
                for (int s = 0; s < sparkCount; s++) {
                    float angle = Random01() * 2.0f * 3.14159265f;
                    float pitch = (Random01() - 0.5f) * 3.14159265f;
                    float speed = (150.0f + Random01() * 450.0f) * scale;
                    Vector3 vel = {
                        cosf(angle) * speed * cosf(pitch),
                        sinf(pitch) * speed,
                        sinf(angle) * speed * cosf(pitch)
                    };
                    SpawnParticle(PARTICLE_BURST_SPARK, emitters[i].currentPos, vel, (2.5f + Random01() * 3.5f) * scale, 0.5f + Random01() * 0.5f);
                }

                int arcCount = 8 * scale;
                for (int s = 0; s < arcCount; s++) {
                    Vector3 offset = {
                        (float)GetRandomValue(-40, 40) * scale,
                        (float)GetRandomValue(-40, 40) * scale,
                        (float)GetRandomValue(-40, 40) * scale
                    };
                    SpawnParticle(PARTICLE_ARC, Vector3Add(emitters[i].currentPos, offset), (Vector3){0, 0, 0}, 1.2f * scale, 0.25f);
                }
                
                // Thiên kiếp sét đánh thẳng từ trên trời xuống
                Vector3 skyPos = {
                    emitters[i].currentPos.x + (float)GetRandomValue(-80, 80) * scale,
                    emitters[i].currentPos.y + 600.0f,
                    emitters[i].currentPos.z + (float)GetRandomValue(-80, 80) * scale
                };
                GenerateJaggedPath(skyPos, emitters[i].currentPos, emitters[i].lightningPath, &emitters[i].lightningPathCount, 16, 35.0f * scale);
                
                if (emitters[i].lightningPathCount > 8) {
                    Vector3 bStart = emitters[i].lightningPath[8];
                    Vector3 bEnd = {
                        bStart.x + (float)GetRandomValue(-150, 150) * scale,
                        bStart.y - (float)GetRandomValue(50, 200) * scale,
                        bStart.z + (float)GetRandomValue(-150, 150) * scale
                    };
                    GenerateJaggedPath(bStart, bEnd, emitters[i].branchPath1, &emitters[i].branchPath1Count, 8, 15.0f * scale);
                }
                if (emitters[i].lightningPathCount > 12) {
                    Vector3 bStart = emitters[i].lightningPath[12];
                    Vector3 bEnd = {
                        bStart.x + (float)GetRandomValue(-150, 150) * scale,
                        bStart.y - (float)GetRandomValue(50, 200) * scale,
                        bStart.z + (float)GetRandomValue(-150, 150) * scale
                    };
                    GenerateJaggedPath(bStart, bEnd, emitters[i].branchPath2, &emitters[i].branchPath2Count, 8, 15.0f * scale);
                }
                
                emitters[i].targetPos = emitters[i].currentPos;
                break;
            }
            count++;
        }
    }
}