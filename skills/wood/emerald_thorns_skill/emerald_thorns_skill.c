#include "skills/wood/emerald_thorns_skill/emerald_thorns_skill.h"
#include "core/resource_manager.h"
#include "core/particle_system.h"
#include "core/decal_system.h"
#include "core/camera_fx.h"
#include "core/screen_distort.h"
#include "core/vfx_light.h"
#include "rlgl.h"
#include "raymath.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef PI
#define PI 3.1415926535f
#endif

#define MAX_THORNS 5
#define TUBE_SEGMENTS 20
#define RADIAL_SEGS 12
#define BASE_RADIUS 4.0f
#define GROWTH_SPEED 3.5f
#define THORN_LIFETIME 2.5f

// Quản lý bộ nhớ tĩnh (Cấm malloc)
typedef struct {
    Vector3 p0, p1, p2, p3; // Đường cong uốn lượn của rễ cây
    float progress;         // Tiến trình mọc (0 -> 1)
    float scale;
    float life;
    bool active;
    bool damageDealt;
    float damage;
} EmeraldThorn;

static EmeraldThorn s_thorns[MAX_THORNS];
static Texture2D s_barkTex;
static Shader s_shader;
static int s_uTimeLoc;
static int s_uDissolveLoc;

// --- Hỗ trợ toán học đường cong Bezier ---
static Vector3 GetBezierPoint(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t) {
    float u = 1.0f - t;
    return Vector3Add(
        Vector3Scale(p0, u * u * u),
        Vector3Add(Vector3Scale(p1, 3.0f * u * u * t),
        Vector3Add(Vector3Scale(p2, 3.0f * u * t * t), Vector3Scale(p3, t * t * t)))
    );
}

static Vector3 GetBezierDerivative(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t) {
    float u = 1.0f - t;
    return Vector3Add(
        Vector3Scale(Vector3Subtract(p1, p0), 3.0f * u * u),
        Vector3Add(Vector3Scale(Vector3Subtract(p2, p1), 6.0f * u * t), Vector3Scale(Vector3Subtract(p3, p2), 3.0f * t * t))
    );
}

// --- Vòng đời Skill ---
void InitEmeraldThornsSkill(int w, int h) {
    (void)w; (void)h;
    for (int i = 0; i < MAX_THORNS; i++) s_thorns[i].active = false;

    // Quản lý Resource: Shader và Texture vỏ cây
    s_barkTex = ResourceManager_LoadTexture("assets/textures/bark_crack.png");
    s_shader = ResourceManager_LoadShader("skills/wood/emerald_thorns_skill/thorn.vs", "skills/wood/emerald_thorns_skill/thorn.fs");
    s_uTimeLoc = GetShaderLocation(s_shader, "u_time");
    s_uDissolveLoc = GetShaderLocation(s_shader, "u_dissolve");
}

void CastEmeraldThornsSkill(Vector3 startPos, Vector3 target, SkillParams params) {
    int count = 0;
    
    // Tạo hiệu ứng decal nứt nẻ đất nền (Quy tắc Scale 4.0x - 5.5x)
    DecalSystem_Add(target, (float)GetRandomValue(0, 360), BASE_RADIUS * params.sizeScale * 4.5f, s_barkTex, 3.0f, ColorAlpha(ELEMENT_COLOR_WOOD, 0.6f));
    
    for (int i = 0; i < MAX_THORNS; i++) {
        if (s_thorns[i].active) continue;
        
        // Anti-Robotic: Phân tán vị trí ngẫu nhiên, uốn lượn không thẳng hàng
        float angle = (float)count / 3.0f * 2.0f * PI + ((float)GetRandomValue(-20, 20) * DEG2RAD);
        float radiusOff = (float)GetRandomValue(5, 15) * params.sizeScale;
        Vector3 basePos = Vector3Add(target, (Vector3){ cosf(angle)*radiusOff, 0.0f, sinf(angle)*radiusOff });
        
        // Mọc nghiêng ngẫu nhiên (Jitter/Tilt)
        float tiltX = (float)GetRandomValue(-15, 15);
        float tiltZ = (float)GetRandomValue(-15, 15);
        Vector3 tipPos = Vector3Add(basePos, (Vector3){ tiltX, 25.0f * params.sizeScale, tiltZ });

        s_thorns[i] = (EmeraldThorn){
            .p0 = Vector3Add(basePos, (Vector3){0, -5.0f, 0}), // Bắt đầu từ dưới đất
            .p1 = Vector3Add(basePos, (Vector3){tiltX*0.5f, 5.0f, tiltZ*0.5f}),
            .p2 = Vector3Add(tipPos, (Vector3){-tiltX*0.2f, -5.0f, -tiltZ*0.2f}),
            .p3 = tipPos,
            .progress = 0.0f,
            .scale = params.sizeScale * ((float)GetRandomValue(85, 115) / 100.0f), // Randomize scale 85-115%
            .life = THORN_LIFETIME,
            .active = true,
            .damageDealt = false,
            .damage = 50.0f // Demo logic
        };
        
        count++;
        if (count >= 3) break; // Chỉ xuất ra 3 rễ cây mỗi lần cast
    }
}

void UpdateEmeraldThornsSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    bool hasImpact = false;
    
    for (int i = 0; i < MAX_THORNS; i++) {
        if (!s_thorns[i].active) continue;
        
        s_thorns[i].life -= dt;
        if (s_thorns[i].life <= 0.0f) {
            s_thorns[i].active = false;
            continue;
        }

        if (s_thorns[i].progress < 1.0f) {
            s_thorns[i].progress = Clamp(s_thorns[i].progress + GROWTH_SPEED * dt, 0.0f, 1.0f);
            
            // Xử lý nổ sát thương khi rễ cây vừa mọc lên đạt 50%
            if (s_thorns[i].progress > 0.5f && !s_thorns[i].damageDealt) {
                float hitRad = (BASE_RADIUS * s_thorns[i].scale) + enemyRadius;
                if (Vector3DistanceSqr(s_thorns[i].p0, enemyPos) <= hitRad * hitRad) {
                    ApplyAoEDamage(s_thorns[i].p0, hitRad, s_thorns[i].damage, 15.0f);
                    s_thorns[i].damageDealt = true;
                    hasImpact = true;
                }
            }
        }
    }
    
    if (hasImpact) {
        // Truyền chuẩn xác 1 tham số trauma (0.0..1.0)
        CameraFX_Shake(0.5f);
        ScreenDistort_Add(enemyPos, 60.0f, 0.5f, 0.4f, 100.0f);
    }
}

void DrawEmeraldThornsSkill(void) {
    float time = (float)GetTime();
    
    rlDisableDepthMask();
    SkillManager_BeginShader(s_shader); // Dùng wrapper tự động bind uniform
    SetShaderValue(s_shader, s_uTimeLoc, &time, SHADER_UNIFORM_FLOAT);

    for (int i = 0; i < MAX_THORNS; i++) {
        if (!s_thorns[i].active) continue;

        // Xử lý tan biến (Dissolve) thay vì tắt bật giật cục
        float dissolve = (s_thorns[i].life < 0.5f) ? (1.0f - (s_thorns[i].life / 0.5f)) : 0.0f;
        SetShaderValue(s_shader, s_uDissolveLoc, &dissolve, SHADER_UNIFORM_FLOAT);

        rlPushMatrix();
        // QUY TẮC SỐNG CÒN 9.1: Đặt lại màu trắng trước khi vẽ mesh tuỳ chỉnh
        rlColor4ub(255, 255, 255, 255); 
        
        rlBegin(RL_QUADS);
        // --- Bắt đầu thuật toán Tube / Frenet Frame ---
        for (int seg = 0; seg < TUBE_SEGMENTS; seg++) {
            float t1 = (float)seg / TUBE_SEGMENTS * s_thorns[i].progress;
            float t2 = (float)(seg + 1) / TUBE_SEGMENTS * s_thorns[i].progress;
            
            Vector3 pos1 = GetBezierPoint(s_thorns[i].p0, s_thorns[i].p1, s_thorns[i].p2, s_thorns[i].p3, t1);
            Vector3 tan1 = Vector3Normalize(GetBezierDerivative(s_thorns[i].p0, s_thorns[i].p1, s_thorns[i].p2, s_thorns[i].p3, t1));
            
            // Xây dựng trục tọa độ Frenet
            Vector3 upTmp = (fabsf(tan1.y) > 0.99f) ? (Vector3){1,0,0} : (Vector3){0,1,0};
            Vector3 right = Vector3Normalize(Vector3CrossProduct(upTmp, tan1));
            Vector3 up = Vector3CrossProduct(tan1, right);
            
            // Taper: Thuôn nhọn ở đầu gai
            float rad1 = BASE_RADIUS * s_thorns[i].scale * (1.0f - t1);
            float rad2 = BASE_RADIUS * s_thorns[i].scale * (1.0f - t2);

            for (int r = 0; r < RADIAL_SEGS; r++) {
                int nextR = (r + 1) % RADIAL_SEGS;
                float angle1 = (float)r * (2.0f * PI) / RADIAL_SEGS;
                float angle2 = (float)nextR * (2.0f * PI) / RADIAL_SEGS;
                
                Vector3 n1 = Vector3Add(Vector3Scale(right, cosf(angle1)), Vector3Scale(up, sinf(angle1)));
                Vector3 n2 = Vector3Add(Vector3Scale(right, cosf(angle2)), Vector3Scale(up, sinf(angle2)));
                
                // Perturbation (gợn sóng sần sùi cho vỏ cây)
                float deform = 1.0f + 0.15f * sinf(t1 * 15.0f + angle1 * 4.0f);
                
                Vector3 v1 = Vector3Add(pos1, Vector3Scale(n1, rad1 * deform));
                Vector3 v2 = Vector3Add(pos1, Vector3Scale(n2, rad1 * deform));
                Vector3 v3 = Vector3Add(pos1, Vector3Scale(n2, rad2 * deform)); // Simplified next segment height for demo
                Vector3 v4 = Vector3Add(pos1, Vector3Scale(n1, rad2 * deform));
                
                rlNormal3f(n1.x, n1.y, n1.z); rlTexCoord2f(0.0f, t1); rlVertex3f(v1.x, v1.y, v1.z);
                rlNormal3f(n2.x, n2.y, n2.z); rlTexCoord2f(1.0f, t1); rlVertex3f(v2.x, v2.y, v2.z);
                rlNormal3f(n2.x, n2.y, n2.z); rlTexCoord2f(1.0f, t2); rlVertex3f(v3.x, v3.y, v3.z);
                rlNormal3f(n1.x, n1.y, n1.z); rlTexCoord2f(0.0f, t2); rlVertex3f(v4.x, v4.y, v4.z);
            }
        }
        rlEnd();
        rlPopMatrix();
    }
    
    SkillManager_EndShader();
    rlEnableDepthMask();
}

void UnloadEmeraldThornsSkill(void) {
    // Để trống tuyệt đối. Resource Manager lo việc dọn rác bộ nhớ.
}

bool IsEmeraldThornsSkillCoiling(void) { return false; }
int GetEmeraldThornsSkillProjectiles(SkillProjectile *out, int max) { return 0; }
void DeactivateEmeraldThornsProjectile(int index) {}