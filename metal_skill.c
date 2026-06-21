#include "metal_skill.h"
#include "raymath.h"
#include <stddef.h>
#include <math.h>

#define MAX_METAL_PARTICLES 15000
#define MAX_EMITTERS 10
#define PARTICLE_HISTORY_COUNT 8

// Định nghĩa các loại hạt của chiêu hệ Kim
typedef enum {
    PARTICLE_SWORD = 0,     // Phi Kiếm (Flying Sword Projectile)
    PARTICLE_SPARK = 1,     // Tia lửa điện ma sát (Air Sparks)
    PARTICLE_SHARD = 2,     // Mảnh vỡ kim loại rơi (Debris shards)
    PARTICLE_PORTAL = 3,    // Cổng triệu hồi xoáy (Vortex portals)
    PARTICLE_SLASH = 4,     // Tia sáng bạo kích tam giác (Triangle light rays)
    PARTICLE_SHOCKWAVE = 5, // Sóng xung kích tròn bạo kích (Impact shockwave)
    PARTICLE_STARDUST = 6   // Bụi vàng lơ lửng lấp lánh (Sparkling stardust)
} MetalParticleType;

// Cấu trúc Kiếm Trận (Golden Sword Array / Gate of Babylon)
typedef struct {
    bool active;
    Vector2 startPos;
    Vector2 targetPos;
    Vector2 portalPositions[5]; // Tối đa 5 luồng kiếm/cổng triệu hồi
    float spawnDelay[5];        // Độ trễ bắn của từng kiếm
    bool spawned[5];            // Trạng thái đã bắn của từng kiếm
    bool portalSpawned[5];      // Trạng thái đã tạo cổng vẽ của từng kiếm
    float portalSizes[5];       // Kích thước của từng cổng và thanh kiếm tương ứng
    int count;                  // Số lượng kiếm trong lần cast
    float timer;                // Bộ đếm thời gian từ lúc cast
} MetalEmitter;

// Cấu trúc một hạt
typedef struct {
    Vector2 position;
    Vector2 velocity;
    Vector2 target;     
    float length;       
    float thickness;    
    float lifetime;
    float maxLifetime;
    int type;                   // MetalParticleType
    bool active;
    
    // Lưu lịch sử toạ độ phục vụ vẽ đuôi kiếm lỏng (Ribbon Trail)
    Vector2 history[PARTICLE_HISTORY_COUNT];
    int historyCount;
    float angle;                // Góc xoay/hướng phản chiếu
    float wobblePhase;          // Pha dao động sóng
    float scale;                // Tỉ lệ thu phóng
} MetalParticle;

static MetalParticle metalPool[MAX_METAL_PARTICLES];
static MetalEmitter emitters[MAX_EMITTERS];
static int lastUsedIndex = 0;

static RenderTexture2D metalCanvas;
static Shader metalShader;
static int timeLocMetal;
static Texture2D swordSprite;

static void SpawnMetal(int type, Vector2 pos, Vector2 vel, float len, float thick, float life, Vector2 target, float initialAngle, float wobblePhase, float scale) {
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
            
            // Khởi tạo lịch sử vị trí cho Trail
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
    
    // Tải ảnh thanh kiếm
    swordSprite = LoadTexture("sword.png"); 
    
    for (int i = 0; i < MAX_METAL_PARTICLES; i++) {
        metalPool[i].active = false;
    }
    
    for (int i = 0; i < MAX_EMITTERS; i++) {
        emitters[i].active = false;
    }
}

void CastMetalSkill(Vector2 startPos, Vector2 target, int count, float sizeScale) {
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
    
    Vector2 dir = Vector2Normalize(Vector2Subtract(target, startPos));
    Vector2 perp = { -dir.y, dir.x };
    
    // Thiết lập vị trí các cổng triệu hồi và tính toán kích thước
    for (int j = 0; j < count; j++) {
        em->spawned[j] = false;
        em->portalSpawned[j] = false;
        em->spawnDelay[j] = (float)j * 0.15f; // Trễ sequential 0.15 giây bắn từng kiếm
        
        float offsetFactor = (float)j - (float)(count - 1) / 2.0f;
        // Giãn các cổng ra xa hơn một chút khi kích thước kiếm tăng lên
        em->portalPositions[j] = Vector2Add(startPos, Vector2Scale(perp, offsetFactor * 40.0f * sizeScale));
        
        // Thiết lập kích thước khác nhau (Cổng trung tâm to nhất, hai bên nhỏ dần) nhân thêm sizeScale
        float distFromCenter = fabsf(offsetFactor);
        if (distFromCenter < 0.1f) {
            em->portalSizes[j] = 0.55f * sizeScale; // Kiếm chính trung tâm
        } else if (distFromCenter < 1.1f) {
            em->portalSizes[j] = 0.38f * sizeScale; // Kiếm phụ hai bên
        } else {
            em->portalSizes[j] = 0.28f * sizeScale; // Kiếm nhỏ ngoài cùng
        }
    }
    
    // Hiệu ứng muzzle flash nhỏ gọn tại tay caster
    Vector2 zeroTarget = {0, 0};
    for (int i = 0; i < 8; i++) {
        Vector2 flashVel = {
            dir.x * GetRandomValue(200, 400) + GetRandomValue(-80, 80),
            dir.y * GetRandomValue(200, 400) + GetRandomValue(-80, 80)
        };
        SpawnMetal(PARTICLE_SPARK, startPos, flashVel, (float)GetRandomValue(5, 10), (float)GetRandomValue(1, 2), 0.2f, zeroTarget, 0.0f, 0.0f, 1.0f);
    }
}

void UpdateMetalSkill(float dt) {
    Vector2 zeroTarget = {0, 0};

    // 1. CẬP NHẬT CÁC EMITTER (CỔNG XUẤT HIỆN TUẦN TỰ, PHÓNG KIẾM UỐN LƯỢN)
    for (int e = 0; e < MAX_EMITTERS; e++) {
        if (!emitters[e].active) continue;
        
        emitters[e].timer += dt;
        bool allSpawned = true;
        
        for (int j = 0; j < emitters[e].count; j++) {
            if (!emitters[e].spawned[j]) {
                allSpawned = false;
                
                Vector2 portalPos = emitters[e].portalPositions[j];
                float sizeFactor = emitters[e].portalSizes[j];
                
                // Xuất hiện cổng triệu hồi trước khi bắn 0.25 giây
                float portalAppearTime = emitters[e].spawnDelay[j] - 0.25f;
                if (portalAppearTime < 0.0f) portalAppearTime = 0.0f;
                
                if (emitters[e].timer >= portalAppearTime && !emitters[e].portalSpawned[j]) {
                    emitters[e].portalSpawned[j] = true;
                    // Tạo cổng ma thuật tương ứng với kích thước đã tính toán
                    float portalLife = emitters[e].spawnDelay[j] - emitters[e].timer + 0.25f;
                    if (portalLife < 0.25f) portalLife = 0.25f;
                    SpawnMetal(PARTICLE_PORTAL, portalPos, (Vector2){0,0}, 45.0f * sizeFactor, 6.0f, portalLife, (Vector2){0,0}, 0.0f, 0.0f, sizeFactor);
                }
                
                // Hiệu ứng hút tụ hạt năng lượng chạy khi cổng xuất hiện
                if (emitters[e].portalSpawned[j] && emitters[e].timer < emitters[e].spawnDelay[j]) {
                    if (GetRandomValue(1, 100) <= 25) { 
                        float spawnAngle = (float)GetRandomValue(0, 360) * DEG2RAD;
                        float spawnDist = (float)GetRandomValue(25, 45) * sizeFactor;
                        Vector2 sparkPos = {
                            portalPos.x + cosf(spawnAngle) * spawnDist,
                            portalPos.y + sinf(spawnAngle) * spawnDist
                        };
                        
                        Vector2 toPortal = Vector2Subtract(portalPos, sparkPos);
                        float speed = (float)GetRandomValue(140, 240);
                        Vector2 sparkVel = Vector2Scale(Vector2Normalize(toPortal), speed);
                        float sparkLife = spawnDist / speed; 
                        
                        SpawnMetal(PARTICLE_SPARK, sparkPos, sparkVel, (float)GetRandomValue(5, 10), (float)GetRandomValue(1, 2), sparkLife, zeroTarget, 0.0f, 0.0f, sizeFactor);
                    }
                }
                
                // Đến giờ phóng kiếm
                if (emitters[e].timer >= emitters[e].spawnDelay[j]) {
                    emitters[e].spawned[j] = true;
                    
                    Vector2 spawnPos = emitters[e].portalPositions[j];
                    Vector2 baseDir = Vector2Normalize(Vector2Subtract(emitters[e].targetPos, spawnPos));
                    
                    // Tạo góc lệch ban đầu (Spread angle) để kiếm bay tỏa ra rồi gom lại (bay uốn lượn)
                    float offsetFactor = (float)j - (float)(emitters[e].count - 1) / 2.0f;
                    float spreadAngle = (offsetFactor * 16.0f) * DEG2RAD; 
                    
                    Vector2 launchDir = {
                        baseDir.x * cosf(spreadAngle) - baseDir.y * sinf(spreadAngle),
                        baseDir.x * sinf(spreadAngle) + baseDir.y * cosf(spreadAngle)
                    };
                    
                    // Tốc độ ban đầu chậm hơn (650 - 950) để thấy rõ kiếm uốn lượn bay đi
                    float speed = (float)GetRandomValue(650, 950);
                    Vector2 vel = Vector2Scale(launchDir, speed);
                    
                    float swordLen = (float)GetRandomValue(52, 72) * sizeFactor;
                    float swordThick = (float)GetRandomValue(10, 14) * sizeFactor;
                    
                    // Truyền pha dao động ngẫu nhiên cho kiếm bay uyển chuyển
                    float randomPhase = (float)GetRandomValue(0, 100) * 0.1f;
                    SpawnMetal(PARTICLE_SWORD, spawnPos, vel, swordLen, swordThick, 2.0f, emitters[e].targetPos, 0.0f, randomPhase, sizeFactor);
                    
                    // Tia lửa đẩy phản lực nhỏ gọn phía sau cổng triệu hồi
                    Vector2 oppositeDir = Vector2Scale(launchDir, -0.6f);
                    for (int s = 0; s < 6; s++) {
                        Vector2 sparkVel = {
                            oppositeDir.x * GetRandomValue(150, 350) + GetRandomValue(-60, 60),
                            oppositeDir.y * GetRandomValue(150, 350) + GetRandomValue(-60, 60)
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

    // 2. CẬP NHẬT CÁC HẠT VẬT LÝ (PARTICLES)
    for (int i = 0; i < MAX_METAL_PARTICLES; i++) {
        if (!metalPool[i].active) continue;

        metalPool[i].lifetime -= dt;
        if (metalPool[i].lifetime <= 0.0f) {
            metalPool[i].active = false;
            continue;
        }

        // Lưu lịch sử vị trí cho Ribbon Trail
        for (int h = PARTICLE_HISTORY_COUNT - 1; h > 0; h--) {
            metalPool[i].history[h] = metalPool[i].history[h - 1];
        }
        metalPool[i].history[0] = metalPool[i].position;
        if (metalPool[i].historyCount < PARTICLE_HISTORY_COUNT) {
            metalPool[i].historyCount++;
        }

        if (metalPool[i].type == PARTICLE_SWORD) {
            // Tăng pha wobble
            metalPool[i].wobblePhase += dt * 16.0f; // Tần số lắc lư

            // Di chuyển cơ bản
            metalPool[i].position.x += metalPool[i].velocity.x * dt;
            metalPool[i].position.y += metalPool[i].velocity.y * dt;

            // Homing steer bám đuổi mục tiêu
            Vector2 toTarget = Vector2Subtract(metalPool[i].target, metalPool[i].position);
            float distToTarget = Vector2Length(toTarget);

            if (distToTarget > 20.0f) {
                Vector2 desiredDir = Vector2Normalize(toTarget);
                float currentSpeed = Vector2Length(metalPool[i].velocity);
                
                // Gia tốc tăng dần mượt mà, giới hạn tốc độ tối đa khoảng 1350
                float maxSpeed = 1350.0f;
                float newSpeed = currentSpeed + 550.0f * dt;
                if (newSpeed > maxSpeed) newSpeed = maxSpeed;
                
                // Tạo dao động sóng hình sin vuông góc với hướng ngắm
                Vector2 perpDir = { -desiredDir.y, desiredDir.x };
                float wobble = sinf(metalPool[i].wobblePhase) * 110.0f * dt; // Độ lắc lư uốn lượn
                
                Vector2 desiredVel = Vector2Add(Vector2Scale(desiredDir, newSpeed), Vector2Scale(perpDir, wobble));
                
                // Lerp chậm (3.2f) giúp phi kiếm uốn lượn hình vòng cung rộng và uyển chuyển
                metalPool[i].velocity = Vector2Lerp(metalPool[i].velocity, desiredVel, dt * 3.2f); 
            }

            // Sinh tia lửa ma sát dọc đường bay
            if (GetRandomValue(1, 100) <= 55) {
                Vector2 backDir = Vector2Scale(Vector2Normalize(metalPool[i].velocity), -0.2f);
                Vector2 sparkVel = {
                    backDir.x * GetRandomValue(200, 500) + GetRandomValue(-100, 100),
                    backDir.y * GetRandomValue(200, 500) + GetRandomValue(-100, 100)
                };
                SpawnMetal(PARTICLE_SPARK, metalPool[i].position, sparkVel, (float)GetRandomValue(8, 20), GetRandomValue(1, 2), 0.2f, zeroTarget, 0.0f, 0.0f, metalPool[i].scale);
            }
            
            // XỬ LÝ VA CHẠM KHI ĐẾN TARGET (THU NHỎ QUY MÔ VA CHẠM ĐỂ GỌN GÀNG HƠN)
            if (distToTarget < 30.0f || metalPool[i].lifetime < 0.1f) {
                metalPool[i].active = false; 
                
                float scale = metalPool[i].scale; // Scale tỉ lệ hiệu ứng theo độ lớn của kiếm
                if (scale < 0.5f) scale = 0.5f;
                
                // A. Mảnh vỡ rơi tự do (PARTICLE_SHARD) - Thu nhỏ số lượng và tầm văng
                int shardCount = GetRandomValue(8, 14);
                for (int s = 0; s < shardCount; s++) {
                    float shardAngle = (float)GetRandomValue(0, 360) * DEG2RAD;
                    float speed = (float)GetRandomValue(250, 600) * scale;
                    Vector2 shardVel = {
                        cosf(shardAngle) * speed,
                        sinf(shardAngle) * speed - 100.0f 
                    };
                    SpawnMetal(PARTICLE_SHARD, metalPool[i].position, shardVel, (float)GetRandomValue(8, 16) * scale, GetRandomValue(2, 4), 0.55f, zeroTarget, (float)GetRandomValue(0, 360), 0.0f, scale);
                }

                // B. Bùng nổ tia lửa tròn (Radial Sparks) - Thu nhỏ số lượng và lực đẩy
                int sparkCount = GetRandomValue(12, 18);
                for (int s = 0; s < sparkCount; s++) {
                    float sparkAngle = (float)GetRandomValue(0, 360) * DEG2RAD;
                    float speed = (float)GetRandomValue(350, 750) * scale;
                    Vector2 sparkVel = { cosf(sparkAngle) * speed, sinf(sparkAngle) * speed };
                    SpawnMetal(PARTICLE_SPARK, metalPool[i].position, sparkVel, (float)GetRandomValue(6, 15), (float)GetRandomValue(1, 2), 0.3f, zeroTarget, 0.0f, 0.0f, scale);
                }

                // C. Sinh tia sáng bạo kích nhỏ gọn (PARTICLE_SLASH)
                int slashCount = GetRandomValue(3, 4); 
                for (int s = 0; s < slashCount; s++) {
                    float rayAngle = (float)GetRandomValue(0, 360) * DEG2RAD;
                    float rayLen = (float)GetRandomValue(50, 95) * scale;
                    SpawnMetal(PARTICLE_SLASH, metalPool[i].position, (Vector2){0,0}, rayLen, 4.0f * scale, 0.22f, zeroTarget, rayAngle, 0.0f, scale);
                }

                // D. Sóng xung kích tròn thu nhỏ (Shockwave)
                SpawnMetal(PARTICLE_SHOCKWAVE, metalPool[i].position, (Vector2){0,0}, 45.0f * scale, 2.0f * scale, 0.22f, zeroTarget, 0.0f, 0.0f, scale);
                
                // E. Sinh bụi stardust lấp lánh (PARTICLE_STARDUST) - Thu nhỏ số lượng
                int stardustCount = GetRandomValue(12, 18);
                for (int s = 0; s < stardustCount; s++) {
                    float dustAngle = (float)GetRandomValue(0, 360) * DEG2RAD;
                    float speed = (float)GetRandomValue(100, 260) * scale;
                    Vector2 dustVel = {
                        cosf(dustAngle) * speed * 0.5f,
                        sinf(dustAngle) * speed - 100.0f 
                    };
                    float dustLife = (float)GetRandomValue(6, 12) / 10.0f;
                    SpawnMetal(PARTICLE_STARDUST, metalPool[i].position, dustVel, (float)GetRandomValue(3, 6) * scale, 1.0f, dustLife, zeroTarget, 0.0f, 0.0f, scale);
                }
            }
        } 
        else if (metalPool[i].type == PARTICLE_SPARK) {
            metalPool[i].position.x += metalPool[i].velocity.x * dt;
            metalPool[i].position.y += metalPool[i].velocity.y * dt;
            metalPool[i].velocity.x *= (1.0f - 2.5f * dt);
            metalPool[i].velocity.y *= (1.0f - 2.5f * dt);
        }
        else if (metalPool[i].type == PARTICLE_SHARD) {
            metalPool[i].position.x += metalPool[i].velocity.x * dt;
            metalPool[i].position.y += metalPool[i].velocity.y * dt;
            metalPool[i].velocity.x *= (1.0f - 1.8f * dt);
            metalPool[i].velocity.y += 950.0f * dt; 
            metalPool[i].angle += 400.0f * dt * (float)(i % 2 == 0 ? 1 : -1);
        }
        else if (metalPool[i].type == PARTICLE_PORTAL) {
            metalPool[i].angle += 140.0f * dt;
        }
        else if (metalPool[i].type == PARTICLE_STARDUST) {
            // Bụi stardust bay bốc lên chịu cản lực gió và dao động ngang hình sin
            metalPool[i].position.y += metalPool[i].velocity.y * dt;
            metalPool[i].position.x += (metalPool[i].velocity.x + sinf(metalPool[i].lifetime * 12.0f + (float)i) * 120.0f) * dt;
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

            // 1. PHI KIẾM & ĐUÔI KIẾM LỎNG SIÊU MƯỢT (METABALL METALLIC RIBBON)
            if (metalPool[i].type == PARTICLE_SWORD) {
                // Nội suy để vẽ dải đuôi tích hợp gradient tròn mềm mại nối liền nhau
                if (metalPool[i].historyCount > 1) {
                    for (int h = 0; h < metalPool[i].historyCount - 1; h++) {
                        Vector2 p1 = metalPool[i].history[h];
                        Vector2 p2 = metalPool[i].history[h + 1];
                        
                        float segRatio = 1.0f - (float)h / (float)PARTICLE_HISTORY_COUNT;
                        
                        for (int step = 0; step < 3; step++) {
                            float t = (float)step / 3.0f;
                            Vector2 p = Vector2Lerp(p1, p2, t);
                            float interpRatio = segRatio - (t * (1.0f / (float)PARTICLE_HISTORY_COUNT));
                            float radius = metalPool[i].thickness * 1.35f * interpRatio * lifeRatio;
                            
                            if (radius > 1.0f) {
                                Color auraCol = ColorAlpha(GOLD, 0.16f * interpRatio * lifeRatio);
                                Color coreCol = ColorAlpha(WHITE, 0.40f * interpRatio * lifeRatio);
                                DrawCircleGradient((int)p.x, (int)p.y, radius, auraCol, BLANK);
                                DrawCircleGradient((int)p.x, (int)p.y, radius * 0.35f, coreCol, BLANK);
                            }
                        }
                    }
                }
                
                // Vẽ hào quang năng lượng
                Color auraCol = ColorAlpha(GOLD, 0.22f * lifeRatio);
                Color coreCol = ColorAlpha(WHITE, 0.38f * lifeRatio);
                DrawCircleGradient((int)metalPool[i].position.x, (int)metalPool[i].position.y, metalPool[i].length * 0.55f, auraCol, BLANK);
                DrawCircleGradient((int)metalPool[i].position.x, (int)metalPool[i].position.y, metalPool[i].length * 0.18f, coreCol, BLANK);

                // Vẽ Sprite thanh kiếm
                float rotation = atan2f(metalPool[i].velocity.y, metalPool[i].velocity.x) * RAD2DEG;
                Rectangle sourceRec = { 0.0f, 0.0f, (float)swordSprite.width, (float)swordSprite.height };
                Rectangle destRec = { metalPool[i].position.x, metalPool[i].position.y, metalPool[i].length, metalPool[i].thickness * 1.4f };
                Vector2 origin = { destRec.width / 2.0f, destRec.height / 2.0f }; 
                
                DrawTexturePro(swordSprite, sourceRec, destRec, origin, rotation, ColorAlpha(WHITE, lifeRatio));
            }
            // 2. TIA LỬA ĐIỆN PHÓNG (Sparks)
            else if (metalPool[i].type == PARTICLE_SPARK) {
                Vector2 dir = Vector2Normalize(metalPool[i].velocity);
                float speed = Vector2Length(metalPool[i].velocity);
                Vector2 tail = Vector2Subtract(metalPool[i].position, Vector2Scale(dir, speed * 0.012f));
                
                float thick = metalPool[i].thickness * lifeRatio;
                Color sparkCol = { intensity, (unsigned char)(intensity * 0.85f), (unsigned char)(intensity * 0.15f), 255 };
                
                DrawLineEx(metalPool[i].position, tail, thick, sparkCol);
                DrawCircleV(metalPool[i].position, thick * 0.5f, WHITE);
            }
            // 3. MẢNH VỠ KIM LOẠI (Shards)
            else if (metalPool[i].type == PARTICLE_SHARD) {
                float size = metalPool[i].length * lifeRatio * 0.45f;
                float radAngle = metalPool[i].angle * DEG2RAD;
                
                Vector2 v1 = {
                    metalPool[i].position.x + cosf(radAngle) * size,
                    metalPool[i].position.y + sinf(radAngle) * size
                };
                Vector2 v2 = {
                    metalPool[i].position.x + cosf(radAngle + 120.0f * DEG2RAD) * (size * 0.55f),
                    metalPool[i].position.y + sinf(radAngle + 120.0f * DEG2RAD) * (size * 0.55f)
                };
                Vector2 v3 = {
                    metalPool[i].position.x + cosf(radAngle + 240.0f * DEG2RAD) * (size * 0.55f),
                    metalPool[i].position.y + sinf(radAngle + 240.0f * DEG2RAD) * (size * 0.55f)
                };
                
                Color shardCol = { intensity, (unsigned char)(intensity * 0.72f), (unsigned char)(intensity * 0.15f), 255 };
                
                DrawTriangle(v1, v2, v3, shardCol);
                DrawTriangleLines(v1, v2, v3, WHITE); 
            }
            // 4. VÒNG TRÒN VÀ XOÁY NĂNG LƯỢNG TRIỆU HỒI (Vortex Portals)
            else if (metalPool[i].type == PARTICLE_PORTAL) {
                float radius = metalPool[i].length;
                float age = metalPool[i].maxLifetime - metalPool[i].lifetime;
                
                if (age < 0.12f) {
                    radius *= (age / 0.12f);
                }
                
                unsigned char pIntensity = (unsigned char)(210.0f * lifeRatio);
                Color portalCol = { pIntensity, (unsigned char)(pIntensity * 0.55f), 0, 255 };
                
                // Vẽ tâm sáng trắng ấm
                DrawCircleGradient((int)metalPool[i].position.x, (int)metalPool[i].position.y, radius * 0.4f, ColorAlpha(WHITE, lifeRatio * 0.6f), BLANK);
                DrawCircleGradient((int)metalPool[i].position.x, (int)metalPool[i].position.y, radius * 1.2f, ColorAlpha(GOLD, 0.06f * lifeRatio), BLANK);
                DrawCircleLines((int)metalPool[i].position.x, (int)metalPool[i].position.y, radius, portalCol);
                DrawCircleLines((int)metalPool[i].position.x, (int)metalPool[i].position.y, radius * 0.85f, ColorAlpha(WHITE, lifeRatio * 0.4f));
                DrawCircleLines((int)metalPool[i].position.x, (int)metalPool[i].position.y, radius * 0.6f, portalCol);
                
                // Vẽ 6 luồng xoắn ốc (Spiral Arms) tạo hiệu ứng xoáy ma thuật
                int spiralArms = 6;
                for (int s = 0; s < spiralArms; s++) {
                    float baseArmAngle = (metalPool[i].angle + (float)s * (360.0f / (float)spiralArms)) * DEG2RAD;
                    Vector2 prevPt = metalPool[i].position;
                    
                    for (int step = 1; step <= 10; step++) {
                        float t = (float)step / 10.0f;
                        float armAngle = baseArmAngle + t * 1.8f; 
                        float r = radius * t;
                        Vector2 pt = {
                            metalPool[i].position.x + cosf(armAngle) * r,
                            metalPool[i].position.y + sinf(armAngle) * r
                        };
                        float thick = (1.0f - t) * 2.2f + 0.7f;
                        DrawLineEx(prevPt, pt, thick, ColorAlpha(portalCol, (1.0f - t) * lifeRatio));
                        prevPt = pt;
                    }
                }
            }
            // 5. TIA SÁNG BÙNG NỔ HÌNH TAM GIÁC (Explosion Light Rays)
            else if (metalPool[i].type == PARTICLE_SLASH) {
                float len = metalPool[i].length * lifeRatio;
                float thick = metalPool[i].thickness * lifeRatio;
                float rayAngle = metalPool[i].angle;
                
                Vector2 pCenter = metalPool[i].position;
                Vector2 tip = {
                    pCenter.x + cosf(rayAngle) * len,
                    pCenter.y + sinf(rayAngle) * len
                };
                Vector2 perpDir = { -sinf(rayAngle), cosf(rayAngle) };
                
                Vector2 b1 = {
                    pCenter.x + perpDir.x * thick,
                    pCenter.y + perpDir.y * thick
                };
                Vector2 b2 = {
                    pCenter.x - perpDir.x * thick,
                    pCenter.y - perpDir.y * thick
                };
                
                Color rayCol = ColorAlpha(GOLD, 0.45f * lifeRatio);
                DrawTriangle(b1, b2, tip, rayCol);
                DrawLineEx(pCenter, tip, thick * 0.35f, ColorAlpha(WHITE, lifeRatio));
            }
            // 6. SÓNG XUNG KÍCH BẠO KÍCH (Shockwave Ring)
            else if (metalPool[i].type == PARTICLE_SHOCKWAVE) {
                float progress = 1.0f - lifeRatio;
                float currentRad = metalPool[i].length * progress;
                
                unsigned char waveIntensity = (unsigned char)(240.0f * lifeRatio);
                Color waveCol = { waveIntensity, (unsigned char)(waveIntensity * 0.7f), (unsigned char)(waveIntensity * 0.15f), 255 };
                
                DrawRing(metalPool[i].position, currentRad * 0.88f, currentRad, 0.0f, 360.0f, 28, waveCol);
                DrawRing(metalPool[i].position, currentRad * 0.96f, currentRad, 0.0f, 360.0f, 28, WHITE);
            }
            // 7. BỤI VÀNG LƠ LỬNG LẤP LÁNH (Sparkling Stardust)
            else if (metalPool[i].type == PARTICLE_STARDUST) {
                float sparkle = sinf(time * 18.0f + (float)i);
                float rad = metalPool[i].length * lifeRatio * (0.6f + 0.4f * sparkle);
                
                Color dustCol = ColorAlpha(GOLD, 0.4f * lifeRatio);
                DrawCircleGradient((int)metalPool[i].position.x, (int)metalPool[i].position.y, rad * 2.2f, dustCol, BLANK);
                DrawCircle((int)metalPool[i].position.x, (int)metalPool[i].position.y, rad * 0.5f, ColorAlpha(WHITE, lifeRatio));
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
            outProjectiles[count].radius = 9.0f * metalPool[i].scale; // Bán kính va chạm của phi kiếm tỉ lệ theo cỡ
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
                Vector2 zeroTarget = {0, 0};
                
                // Kích hoạt tất cả hiệu ứng nổ tan rã phi kiếm tại vị trí va chạm
                int shardCount = GetRandomValue(8, 14);
                for (int s = 0; s < shardCount; s++) {
                    float shardAngle = (float)GetRandomValue(0, 360) * DEG2RAD;
                    float speed = (float)GetRandomValue(250, 600) * scale;
                    Vector2 shardVel = {
                        cosf(shardAngle) * speed,
                        sinf(shardAngle) * speed - 100.0f 
                    };
                    SpawnMetal(PARTICLE_SHARD, metalPool[i].position, shardVel, (float)GetRandomValue(5, 10) * scale, GetRandomValue(1, 3), 0.55f, zeroTarget, (float)GetRandomValue(0, 360), 0.0f, scale);
                }

                int sparkCount = GetRandomValue(12, 18);
                for (int s = 0; s < sparkCount; s++) {
                    float sparkAngle = (float)GetRandomValue(0, 360) * DEG2RAD;
                    float speed = (float)GetRandomValue(350, 750) * scale;
                    Vector2 sparkVel = { cosf(sparkAngle) * speed, sinf(sparkAngle) * speed };
                    SpawnMetal(PARTICLE_SPARK, metalPool[i].position, sparkVel, (float)GetRandomValue(4, 10), (float)GetRandomValue(1, 2), 0.3f, zeroTarget, 0.0f, 0.0f, scale);
                }

                int slashCount = GetRandomValue(3, 4); 
                for (int s = 0; s < slashCount; s++) {
                    float rayAngle = (float)GetRandomValue(0, 360) * DEG2RAD;
                    float rayLen = (float)GetRandomValue(33, 62) * scale;
                    SpawnMetal(PARTICLE_SLASH, metalPool[i].position, (Vector2){0,0}, rayLen, 2.6f * scale, 0.22f, zeroTarget, rayAngle, 0.0f, scale);
                }

                SpawnMetal(PARTICLE_SHOCKWAVE, metalPool[i].position, (Vector2){0,0}, 30.0f * scale, 2.0f * scale, 0.22f, zeroTarget, 0.0f, 0.0f, scale);
                
                int stardustCount = GetRandomValue(12, 18);
                for (int s = 0; s < stardustCount; s++) {
                    float dustAngle = (float)GetRandomValue(0, 360) * DEG2RAD;
                    float speed = (float)GetRandomValue(100, 260) * scale;
                    Vector2 dustVel = {
                        cosf(dustAngle) * speed * 0.5f,
                        sinf(dustAngle) * speed - 100.0f 
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