#include "skills/fire/hoa_long_phong_ba_skill/hoa_long_phong_ba_skill.h"
#include "core/force_field.h"
#include "core/particle_system.h"
#include "core/color_gradient.h"
#include "core/decal_system.h"
#include "core/screen_distort.h"
#include "core/vfx_light.h"
#include "core/camera_fx.h"
#include "core/skill_manager.h"
#include "core/resource_manager.h"
#include "core/procedural_mesh_utils.h"
#include "sandbox/sandbox_core.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#define MAX_VORTICES 4
#define TUBE_SEGMENTS 36
#define RADIAL_SEGS 16
#define BASE_RADIUS 6.5f

typedef struct {
    Vector3 startPos;
    Vector3 targetPos;
    Vector3 controlP1;
    Vector3 controlP2;
    float progress;       // 0.0 -> 1.0 (bay từ start đến target)
    float gatherTimer;    // Thời gian tụ lực trước khi bắn (Anticipation)
    float scale;
    float life;           // Tuổi thọ duy trì sau va chạm (Aftermath)
    float maxLife;
    Vector3 headPos;
    bool active;
    bool impactTriggered;
    float damage;
    float knockback;
    float twistPhase;
} HoaLongPhongBaVortex;

static HoaLongPhongBaVortex s_vortices[MAX_VORTICES];

#define MAX_SPARKS 256

typedef struct {
    Vector3 position;
    Vector3 velocity;
    float life;
    float maxLife;
    Color color;
    const ForceField *forceField; // Thêm trường lực để tia lửa bay chuẩn động học của lốc xoáy
    bool active;
} FlameSpark;

static FlameSpark s_sparks[MAX_SPARKS];
static int s_sparkIndex = 0;

static void SpawnFlameSpark(Vector3 pos, Vector3 vel, float lifetime, Color color, const ForceField *forceField) {
    int idx = s_sparkIndex;
    s_sparks[idx] = (FlameSpark){
        .position = pos,
        .velocity = vel,
        .life = lifetime,
        .maxLife = lifetime,
        .color = color,
        .forceField = forceField,
        .active = true
    };
    s_sparkIndex = (s_sparkIndex + 1) % MAX_SPARKS;
}

// Tài nguyên đồ họa
static Texture2D s_noiseTex;
static Texture2D s_crackTex;
static Texture2D s_flareTex;
static Shader s_shader;

// Vị trí các Uniform trong Shader
static int s_uTimeLoc;
static int s_uViewPosLoc;
static int s_uUVLengthLoc;
static int s_uDissolveLoc;

// Các trường lực vật lý của hạt (ForceFields)
static ForceField s_travelField;    // Curl Noise cuộn xoáy vệt đuôi khi bay
static ForceField s_geyserField;    // Cột lốc lửa cuộn đứng (Vortex + Curl Noise + Lên cao)
static ForceField s_falloutField;   // Tàn lửa rơi rụng từ từ

// Gradient chuyển màu của hạt lửa
static ColorGradient s_fireGrad;

// Caster toàn cục lấy từ sandbox
extern PlayerEntity player;

// Toán học Bezier
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

static Vector3 GetBezierDerivative(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t) {
    float u = 1.0f - t;
    Vector3 d0 = Vector3Scale(Vector3Subtract(p1, p0), 3.0f * u * u);
    Vector3 d1 = Vector3Scale(Vector3Subtract(p2, p1), 6.0f * u * t);
    Vector3 d2 = Vector3Scale(Vector3Subtract(p3, p2), 3.0f * t * t);
    return Vector3Add(d0, Vector3Add(d1, d2));
}

// Khởi tạo tài nguyên kỹ năng
void InitHoaLongPhongBaSkill(int w, int h) {
    // Tải texture qua ResourceManager để tự động lưu đệm và quản lý vòng đời
    s_noiseTex = ResourceManager_LoadTexture("assets/textures/noise.png");
    s_crackTex = ResourceManager_LoadTexture("assets/textures/crack.png");
    s_flareTex = ResourceManager_LoadTexture("assets/textures/flare.png");

    s_shader = ResourceManager_LoadShader(
        "skills/fire/hoa_long_phong_ba_skill/hoa_long_phong_ba.vs",
        "skills/fire/hoa_long_phong_ba_skill/hoa_long_phong_ba.fs"
    );

    s_uTimeLoc = GetShaderLocation(s_shader, "u_time");
    s_uViewPosLoc = GetShaderLocation(s_shader, "viewPos");
    s_uUVLengthLoc = GetShaderLocation(s_shader, "u_uvLength");
    s_uDissolveLoc = GetShaderLocation(s_shader, "u_dissolve");

    // Dọn dẹp mảng trạng thái
    for (int i = 0; i < MAX_VORTICES; i++) {
        s_vortices[i].active = false;
    }
    for (int i = 0; i < MAX_SPARKS; i++) {
        s_sparks[i].active = false;
    }
    s_sparkIndex = 0;

    // Thiết lập ColorGradient cho hạt lửa
    s_fireGrad.count = 0;
    ColorGradient_AddStop(&s_fireGrad, 0.00f, (Color){ 255, 255, 220, 255 }); // Lõi trắng vàng siêu nóng
    ColorGradient_AddStop(&s_fireGrad, 0.15f, (Color){ 255, 170, 20, 255 });  // Cam sáng rực rỡ
    ColorGradient_AddStop(&s_fireGrad, 0.50f, (Color){ 220, 40, 5, 200 });   // Đỏ cam rực
    ColorGradient_AddStop(&s_fireGrad, 0.85f, (Color){ 100, 10, 0, 100 });   // Đỏ sậm tàn tro
    ColorGradient_AddStop(&s_fireGrad, 1.00f, (Color){ 30, 30, 30, 0 });      // Khói đen tan biến

    // --- CẤU HÌNH TRƯỜNG LỰC HẠT (FORCE FIELDS) ---

    // 1. Trường lực bay (Travel Field): Curl Noise cuộn xoắn khí động + Gió ngược đẩy tạt ra sau
    ForceField_Clear(&s_travelField);
    ForceField_AddLayer(&s_travelField, (ForceLayer){
        .type = FORCE_NOISE_CURL,
        .strength = 145.0f,
        .noiseScale = 0.07f,
        .noiseSpeed = 4.5f
    });
    ForceField_AddLayer(&s_travelField, (ForceLayer){
        .type = FORCE_DRAG,
        .strength = 2.8f
    });

    // 2. Trường lực cột lốc nổ (Geyser Field): Xoáy đứng Y cực mạnh + Cuộn khí động Curl + Phun lên trời
    ForceField_Clear(&s_geyserField);
    // Tăng sức mạnh xoáy lốc để bẻ cong đường đạn vệt lửa vút lên trời
    ForceField_AddLayer(&s_geyserField, (ForceLayer){
        .type = FORCE_VORTEX,
        .direction = { 0.0f, 1.0f, 0.0f },
        .strength = 1100.0f, // Tăng mạnh từ 450 -> 1100
        .radius = 160.0f,    // Bán kính rộng gấp đôi để ôm trọn vệt lửa
        .falloff = 1.0f
    });
    // Phun vút lên cao cực nhanh
    ForceField_AddLayer(&s_geyserField, (ForceLayer){
        .type = FORCE_GRAVITY_DIR,
        .direction = { 0.0f, 1.0f, 0.0f },
        .strength = 390.0f // tăng lực kéo lên trời
    });
    // Thêm curl noise hỗn loạn khí động cho giống ngọn lửa thật
    ForceField_AddLayer(&s_geyserField, (ForceLayer){
        .type = FORCE_NOISE_CURL,
        .strength = 180.0f,
        .noiseScale = 0.05f,
        .noiseSpeed = 6.0f
    });
    ForceField_AddLayer(&s_geyserField, (ForceLayer){
        .type = FORCE_DRAG,
        .strength = 1.8f
    });

    // 3. Trường lực tàn lửa rơi rụng (Fallout Field): Rơi chậm + Cản mạnh để tàn bay lơ lửng
    ForceField_Clear(&s_falloutField);
    ForceField_AddLayer(&s_falloutField, (ForceLayer){
        .type = FORCE_GRAVITY_DIR,
        .direction = { 0.0f, -1.0f, 0.0f },
        .strength = 35.0f
    });
    ForceField_AddLayer(&s_falloutField, (ForceLayer){
        .type = FORCE_DRAG,
        .strength = 4.5f
    });
}

// Bắt đầu thi triển chiêu thức
void CastHoaLongPhongBaSkill(Vector3 startPos, Vector3 target, SkillParams params) {
    for (int i = 0; i < MAX_VORTICES; i++) {
        if (!s_vortices[i].active) {
            float sizeScale = (params.sizeScale > 0.0f) ? params.sizeScale : 1.0f;
            Vector3 to = Vector3Subtract(target, startPos);
            float dist = Vector3Length(to);
            if (dist < 10.0f) dist = 10.0f;
            Vector3 dir = Vector3Scale(to, 1.0f / dist);

            // Tạo các điểm điều khiển Bezier uốn lượn
            Vector3 perpX = Vector3Normalize((Vector3){ -dir.z, 0.0f, dir.x });
            if (Vector3Length(perpX) < 0.1f) perpX = (Vector3){ 1.0f, 0.0f, 0.0f };
            Vector3 perpY = Vector3Normalize(Vector3CrossProduct(dir, perpX));

            float amp = dist * 0.22f; // biên độ uốn lượn ban đầu
            float twistPhase = (float)rand() / (float)RAND_MAX * 2.0f * PI;

            Vector3 cp1 = Vector3Add(
                Vector3Add(startPos, Vector3Scale(dir, dist * 0.33f)),
                Vector3Add(Vector3Scale(perpX, sinf(twistPhase) * amp), Vector3Scale(perpY, cosf(twistPhase) * amp + 15.0f))
            );
            Vector3 cp2 = Vector3Add(
                Vector3Add(startPos, Vector3Scale(dir, dist * 0.66f)),
                Vector3Add(Vector3Scale(perpX, -sinf(twistPhase) * amp * 0.8f), Vector3Scale(perpY, -cosf(twistPhase) * amp * 0.8f + 25.0f))
            );

            s_vortices[i] = (HoaLongPhongBaVortex){
                .startPos = startPos,
                .targetPos = target,
                .controlP1 = cp1,
                .controlP2 = cp2,
                .progress = 0.0f,
                .gatherTimer = 0.5f, // 0.5s tụ lực
                .scale = sizeScale,
                .life = 2.2f,        // Duy trì 2.2s sau va chạm để tạo cột lốc
                .maxLife = 2.2f,
                .headPos = startPos,
                .active = true,
                .impactTriggered = false,
                .damage = Skill_CalculateDamage(SKILL_CAT_AOE_CONTROL, params),
                .knockback = Skill_CalculateKnockback(SKILL_CAT_AOE_CONTROL, params),
                .twistPhase = twistPhase
            };

            // Spawn một chớp sáng nhẹ báo hiệu tụ chiêu
            VFXLight_Spawn(startPos, ELEMENT_COLOR_FIRE, 40.0f * sizeScale, 0.4f);
            break;
        }
    }
}

// Xử lý nổ lốc lửa khổng lồ khi va chạm
static void TriggerVortexImpact(Vector3 pos, float scale, float damage, float knockback) {
    // 1. Ánh sáng rực rỡ (Bỏ hiệu ứng rung camera và sóng sung kích màn hình theo yêu cầu)
    VFXLight_Spawn(pos, ELEMENT_COLOR_FIRE, 90.0f * scale, 1.8f);

    // 2. Tạo Decal nứt đất rực lửa cỡ lớn
    // Y dịch nhẹ lên +0.02f chống Z-fighting nhấp nháy lưới đất
    Vector3 decalPos = { pos.x, pos.y + 0.02f, pos.z };
    DecalSystem_Add(decalPos, (float)(rand() % 360), BASE_RADIUS * scale * 5.2f, s_crackTex, 4.5f, ELEMENT_COLOR_FIRE);

    // 3. Gây sát thương AoE diện rộng và khóa chân kẻ địch
    ApplyAoEDamage(pos, 48.0f * scale, damage, knockback);
    AddRootToEnemy(3.2f); // giữ chân 3.2 giây trong cột lửa

    // 4. Tạo hiệu ứng Hỏa Long Phong Ba (Cột lốc lửa cuộn đứng cao gấp 3-4 lần nhân vật)
    // Chiều cao nhân vật ~15 unit, cột lốc cao gấp 4 lần (~60 unit)
    int geyserHạtCount = (int)(65.0f * scale); // Tối ưu hóa hiệu năng: 150 -> 65 hạt
    for (int i = 0; i < geyserHạtCount; i++) {
        // Tọa độ ngẫu nhiên xung quanh điểm va chạm
        float angle = (float)i / (float)geyserHạtCount * 2.0f * PI;
        float dist = (float)rand() / (float)RAND_MAX * BASE_RADIUS * scale * 2.2f;
        Vector3 particlePos = {
            pos.x + cosf(angle) * dist,
            pos.y + (float)rand() / (float)RAND_MAX * 5.0f,
            pos.z + sinf(angle) * dist
        };

        // Vận tốc ban đầu: xoay tròn nhẹ + phun vút thẳng lên trời cực mạnh
        float spinSpeed = (float)(rand() % 40 + 40) * scale;
        float upSpeed = (float)(rand() % 100 + 75) * scale; // Phun lên cực cao
        Vector3 vel = {
            -sinf(angle) * spinSpeed,
            upSpeed,
            cosf(angle) * spinSpeed
        };

        ParticleConfig cfg = {
            .position = particlePos,
            .velocity = vel,
            .radius = ((float)rand() / (float)RAND_MAX * 3.5f + 1.5f) * scale * 4.0f,
            .lifetime = (float)rand() / (float)RAND_MAX * 1.0f + 0.8f, // Tồn tại lâu hơn để bay lên cao
            .forceField = &s_geyserField,
            .gradient = &s_fireGrad
        };
        SpawnParticle(cfg);
    }

    // Sinh ra các tàn lửa chói sáng kéo vệt dài (Accent Sparks) bắn ra và bị cuốn xoáy lả lướt vào lốc xoáy
    int sparkCount = (int)(10.0f * scale); // Giảm số hạt vệt lửa lúc vỡ xuống còn 10 hạt (tối ưu, tránh rối mắt)
    for (int i = 0; i < sparkCount; i++) {
        float angle = (float)rand() / (float)RAND_MAX * 2.0f * PI;
        float pitch = ((float)rand() / (float)RAND_MAX - 0.2f) * PI * 0.5f;
        float speed = ((float)(rand() % 90 + 55)) * scale; // Tốc độ ban đầu vừa phải để lực xoáy bẻ cong dễ dàng

        Vector3 vel = {
            cosf(angle) * speed * cosf(pitch),
            sinf(pitch) * speed + 35.0f,
            sinf(angle) * speed * cosf(pitch)
        };

        Color sparkColor = (Color){ 255, 255, 240, 255 }; // màu trắng vàng chói sáng
        // Khôi phục truyền &s_geyserField để vệt sáng bay lả lướt xoáy lốc đứng lên trời cực đẹp!
        // Sống lâu hơn (2.0s -> 3.8s) để có đủ thời gian bay xoáy lên trời cao!
        SpawnFlameSpark(pos, vel, (float)rand() / (float)RAND_MAX * 1.8f + 2.0f, sparkColor, &s_geyserField);
    }
}

// Cập nhật trạng thái kỹ năng qua từng khung hình
void UpdateHoaLongPhongBaSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    float time = (float)GetTime();

    // Cập nhật các tia lửa kéo dài (FlameSparks) bay lả lướt xoáy lượn chuẩn xác theo trường lực khí động
    for (int i = 0; i < MAX_SPARKS; i++) {
        if (!s_sparks[i].active) continue;
        s_sparks[i].life -= dt;
        if (s_sparks[i].life <= 0.0f) {
            s_sparks[i].active = false;
            continue;
        }

        Vector3 accel = (Vector3){ 0 };
        if (s_sparks[i].forceField != NULL) {
            // Đánh giá lực xoáy lốc đứng, gió ngược hoặc curl noise hỗn loạn để bay lả lướt khí động!
            accel = ForceField_Evaluate(s_sparks[i].forceField, s_sparks[i].position, s_sparks[i].velocity, time, (Vector3){0}, (Vector3){0, 1, 0});
        } else {
            // Rơi tự do rất nhẹ theo phương thẳng đứng
            accel.y = -15.0f;
        }

        // Cập nhật vận tốc
        s_sparks[i].velocity = Vector3Add(s_sparks[i].velocity, Vector3Scale(accel, dt));
        
        // Damping cản chuyển động
        if (s_sparks[i].forceField == &s_geyserField) {
            s_sparks[i].velocity = Vector3Scale(s_sparks[i].velocity, 1.0f - 1.8f * dt);
        } else {
            s_sparks[i].velocity = Vector3Scale(s_sparks[i].velocity, 1.0f - 2.6f * dt);
        }

        // Cập nhật vị trí
        s_sparks[i].position = Vector3Add(s_sparks[i].position, Vector3Scale(s_sparks[i].velocity, dt));
    }

    for (int idx = 0; idx < MAX_VORTICES; idx++) {
        if (!s_vortices[idx].active) continue;
        HoaLongPhongBaVortex *v = &s_vortices[idx];

        // --- GIAI ĐOẠN 1: GATHER (TỤ LỰC TRƯỚC BẮN) ---
        if (v->gatherTimer > 0.0f) {
            v->gatherTimer -= dt;

            // Spawn decal pháp trận tụ lực đỏ cam quay dưới chân caster (player)
            if (v->gatherTimer > 0.05f) {
                Vector3 pFoot = { v->startPos.x, v->startPos.y + 0.02f, v->startPos.z };
                DecalSystem_Add(pFoot, time * 90.0f, BASE_RADIUS * v->scale * 1.8f, s_crackTex, 0.1f, ColorAlpha(ELEMENT_COLOR_FIRE, 0.4f));
            }

            // Sinh các hạt tụ lửa xung quanh tụ về caster
            for (int i = 0; i < 4; i++) {
                float angle = (float)rand() / (float)RAND_MAX * 2.0f * PI;
                float rDist = (float)rand() / (float)RAND_MAX * 18.0f + 6.0f;
                float height = ((float)rand() / (float)RAND_MAX - 0.3f) * 15.0f;

                Vector3 startPart = {
                    v->startPos.x + cosf(angle) * rDist,
                    v->startPos.y + height + 5.0f,
                    v->startPos.z + sinf(angle) * rDist
                };

                // Vận tốc hướng về tâm startPos
                Vector3 toCenter = Vector3Subtract(v->startPos, startPart);
                Vector3 vel = Vector3Scale(Vector3Normalize(toCenter), 35.0f);

                ParticleConfig cfg = {
                    .position = startPart,
                    .velocity = vel,
                    .radius = ((float)rand() / (float)RAND_MAX * 1.5f + 0.6f) * v->scale * 4.0f,
                    .lifetime = 0.5f,
                    .gradient = &s_fireGrad
                };
                SpawnParticle(cfg);
            }
            continue;
        }

        // --- GIAI ĐOẠN 2: TRAVEL (ĐẠN BAY) ---
        if (!v->impactTriggered) {
            v->progress = Clamp(v->progress + 1.25f * dt, 0.0f, 1.0f); // bay mất 0.8 giây

            // Cập nhật vị trí uốn lượn động cho 2 điểm điều khiển Bezier
            Vector3 to = Vector3Subtract(v->targetPos, v->startPos);
            float dist = Vector3Length(to);
            if (dist < 1.0f) dist = 1.0f;
            Vector3 dir = Vector3Scale(to, 1.0f / dist);
            Vector3 perpX = Vector3Normalize((Vector3){ -dir.z, 0.0f, dir.x });
            if (Vector3Length(perpX) < 0.1f) perpX = (Vector3){ 1.0f, 0.0f, 0.0f };
            Vector3 perpY = Vector3Normalize(Vector3CrossProduct(dir, perpX));

            // Sóng uốn lượn ổn định trong không gian, chỉ dao động nhẹ theo thời gian thực để mượt mà
            float amp = dist * 0.15f * sinf(v->progress * PI * 1.8f + v->twistPhase + time * 2.0f);
            float ampVert = dist * 0.08f * cosf(v->progress * PI * 1.8f + v->twistPhase + time * 1.5f);

            Vector3 dynamicP1 = Vector3Add(
                v->controlP1,
                Vector3Add(Vector3Scale(perpX, amp), Vector3Scale(perpY, ampVert))
            );
            Vector3 dynamicP2 = Vector3Add(
                v->controlP2,
                Vector3Add(Vector3Scale(perpX, -amp * 0.8f), Vector3Scale(perpY, -ampVert * 0.8f))
            );

            v->headPos = GetBezierPoint(v->startPos, dynamicP1, dynamicP2, v->targetPos, v->progress);

            // Sinh hạt vệt lửa dọc đường bay (Travel particles)
            Vector3 tangent = Vector3Normalize(GetBezierDerivative(v->startPos, dynamicP1, dynamicP2, v->targetPos, v->progress));
            // Hạt phun tạt ra đằng sau đạn
            Vector3 backVel = Vector3Scale(Vector3Negate(tangent), 30.0f * v->scale);

            // Đặt lực gió ngược chiều bay vào travelField
            s_travelField.layers[1].type = FORCE_WIND;
            s_travelField.layers[1].direction = Vector3Normalize(backVel);
            s_travelField.layers[1].strength = 80.0f;
            s_travelField.layers[1].radius = 0.0f;
            s_travelField.layers[1].falloff = 0.0f;
            s_travelField.layerCount = 3; // Lớp 0: Curl, Lớp 1: Gió ngược, Lớp 2: Drag

            for (int k = 0; k < 1; k++) { // Tối ưu hóa hiệu năng: 3 -> 1 hạt mỗi frame khi bay
                Vector3 pOffset = {
                    v->headPos.x + ((float)rand() / (float)RAND_MAX - 0.5f) * BASE_RADIUS * v->scale * 0.6f,
                    v->headPos.y + ((float)rand() / (float)RAND_MAX - 0.5f) * BASE_RADIUS * v->scale * 0.6f,
                    v->headPos.z + ((float)rand() / (float)RAND_MAX - 0.5f) * BASE_RADIUS * v->scale * 0.6f
                };

                ParticleConfig cfg = {
                    .position = pOffset,
                    .velocity = Vector3Add(backVel, (Vector3){
                        ((float)rand() / (float)RAND_MAX - 0.5f) * 15.0f,
                        ((float)rand() / (float)RAND_MAX - 0.5f) * 15.0f,
                        ((float)rand() / (float)RAND_MAX - 0.5f) * 15.0f
                    }),
                    .radius = ((float)rand() / (float)RAND_MAX * 2.0f + 0.8f) * v->scale * 4.0f,
                    .lifetime = (float)rand() / (float)RAND_MAX * 0.4f + 0.35f,
                    .forceField = &s_travelField,
                    .gradient = &s_fireGrad
                };
                SpawnParticle(cfg);
            }

            // Sinh thêm hạt tàn lửa kéo vệt dài chói lọi (Accent Sparks) khi đạn bay
            if (rand() % 100 < 18) { // Giảm tần suất sinh hạt khi bay xuống 18% (1 hạt sau 5-6 frame) tránh rối mắt
                Vector3 pOffset = {
                    v->headPos.x + ((float)rand() / (float)RAND_MAX - 0.5f) * BASE_RADIUS * v->scale * 0.35f,
                    v->headPos.y + ((float)rand() / (float)RAND_MAX - 0.5f) * BASE_RADIUS * v->scale * 0.35f,
                    v->headPos.z + ((float)rand() / (float)RAND_MAX - 0.5f) * BASE_RADIUS * v->scale * 0.35f
                };

                // Vận tốc tạt mạnh ra sau đạn để kéo vệt song song
                Vector3 sparkVel = {
                    -tangent.x * 65.0f * v->scale + ((float)rand() / (float)RAND_MAX - 0.5f) * 20.0f,
                    -tangent.y * 65.0f * v->scale + ((float)rand() / (float)RAND_MAX - 0.5f) * 20.0f - 12.0f,
                    -tangent.z * 65.0f * v->scale + ((float)rand() / (float)RAND_MAX - 0.5f) * 20.0f
                };

                Color sparkColor = (Color){ 255, 255, 220, 255 }; // trắng vàng rực rỡ
                // Khôi phục truyền &s_travelField và cho tuổi thọ dài (1.0s -> 1.8s) để vệt sáng bay lả lướt uốn lượn theo đạn!
                SpawnFlameSpark(pOffset, sparkVel, (float)rand() / (float)RAND_MAX * 0.8f + 1.0f, sparkColor, &s_travelField);
            }

            // Va chạm nếu đi hết tiến trình hoặc trúng vùng chạm địch thủ
            bool hitEnemy = false;
            float checkDist = BASE_RADIUS * v->scale + enemyRadius;
            if (Vector3DistanceSqr(v->headPos, enemyPos) <= checkDist * checkDist) {
                hitEnemy = true;
            }

            if (v->progress >= 1.0f || hitEnemy) {
                TriggerVortexImpact(v->headPos, v->scale, v->damage, v->knockback);
                v->impactTriggered = true;
            }
        }

        // --- GIAI ĐOẠN 3: AFTERMATH (DUY TRÌ VÒI RỒNG LỬA SAU VA CHẠM) ---
        if (v->impactTriggered) {
            v->life -= dt;
            if (v->life <= 0.0f) {
                v->active = false;
                continue;
            }

            // Thiết lập vị trí tâm cột lốc cho geyser field
            s_geyserField.layers[0].origin = v->targetPos;

            // Mỗi frame sinh thêm hạt duy trì lốc xoáy rực cháy
            int contCount = (int)(3.0f * v->scale); // Tối ưu hóa hiệu năng: 8 -> 3 hạt mỗi frame để duy trì cột lốc
            float ageRatio = v->life / v->maxLife; // 1.0 -> 0.0
            for (int k = 0; k < contCount; k++) {
                float angle = (float)rand() / (float)RAND_MAX * 2.0f * PI;
                float rDist = (float)rand() / (float)RAND_MAX * BASE_RADIUS * v->scale * 2.0f * ageRatio;
                Vector3 pSpawn = {
                    v->targetPos.x + cosf(angle) * rDist,
                    v->targetPos.y + (float)rand() / (float)RAND_MAX * 2.0f,
                    v->targetPos.z + sinf(angle) * rDist
                };

                float upSpeed = (float)(rand() % 90 + 60) * v->scale * ageRatio;
                float spinSpeed = (float)(rand() % 35 + 25) * v->scale * ageRatio;
                Vector3 vel = {
                    -sinf(angle) * spinSpeed,
                    upSpeed,
                    cosf(angle) * spinSpeed
                };

                ParticleConfig cfg = {
                    .position = pSpawn,
                    .velocity = vel,
                    .radius = ((float)rand() / (float)RAND_MAX * 2.6f + 1.2f) * v->scale * 4.0f * ageRatio,
                    .lifetime = (float)rand() / (float)RAND_MAX * 0.9f + 0.7f,
                    .forceField = &s_geyserField,
                    .gradient = &s_fireGrad
                };
                SpawnParticle(cfg);
            }
        }
    }
}

// Vẽ quả cầu lửa 3D của Hỏa Long Phong Ba
void DrawHoaLongPhongBaSkill(void) {
    float time = (float)GetTime();

    rlDisableDepthMask();
    BeginShaderMode(s_shader);

    // Truyền các Uniforms toàn cục
    SetShaderValue(s_shader, s_uTimeLoc, &time, SHADER_UNIFORM_FLOAT);
    
    // Lấy toạ độ camera để tính toán Fresnel trong shader
    Vector3 camPos = camera.position;
    SetShaderValue(s_shader, s_uViewPosLoc, &camPos, SHADER_UNIFORM_VEC3);

    // Kích hoạt texture0 chứa noise
    rlActiveTextureSlot(0);
    rlEnableTexture(s_noiseTex.id);

    for (int idx = 0; idx < MAX_VORTICES; idx++) {
        if (!s_vortices[idx].active) continue;
        HoaLongPhongBaVortex *v = &s_vortices[idx];

        // Chỉ vẽ quả cầu lửa khi đạn đang bay và chưa nổ hẳn
        if (v->gatherTimer > 0.0f || v->progress <= 0.01f || v->impactTriggered) continue;

        // Tính toán độ tan rã (dissolve) khi đạn sắp va chạm hoặc tan biến
        float dissolve = 0.0f;
        SetShaderValue(s_shader, s_uDissolveLoc, &dissolve, SHADER_UNIFORM_FLOAT);

        // Đối với hình cầu, truyền u_uvLength = 1.0f để tính toán UV đều đặn
        float uvLength = 1.0f;
        SetShaderValue(s_shader, s_uUVLengthLoc, &uvLength, SHADER_UNIFORM_FLOAT);

        // ĐẶT LẠI MÀU ĐỈNH LÀM SẠCH (Rule 7: Reset Vertex Colors)
        rlColor4ub(255, 255, 255, 255);

        // Vẽ quả cầu lửa tại vị trí đầu đạn logic (biến dạng phập phồng & rách rưới xử lý tự động bởi shader)
        DrawCoreSphere(v->headPos, BASE_RADIUS * v->scale * 1.25f, 24, 24, WHITE);
    }

    EndShaderMode();

    // 6. Vẽ các tia lửa kéo dài dạng vệt chói sáng bằng QUADS hướng camera (Motion Stretched Billboard)
    rlDisableDepthMask();
    rlCheckRenderBatchLimit(MAX_SPARKS * 4);
    rlBegin(RL_QUADS);
    for (int i = 0; i < MAX_SPARKS; i++) {
        if (!s_sparks[i].active) continue;
        FlameSpark *s = &s_sparks[i];

        float ageRatio = s->life / s->maxLife; // 1.0 -> 0.0
        Vector3 start = s->position;
        // Điểm đuôi kéo dài ngược hướng vận tốc tạo vệt Motion Blur (kéo vệt dài gấp đôi)
        Vector3 end = Vector3Subtract(start, Vector3Scale(s->velocity, 0.075f));

        Vector3 velVec = s->velocity;
        float velLen = Vector3Length(velVec);
        if (velLen < 0.1f) continue;
        Vector3 vDir = Vector3Scale(velVec, 1.0f / velLen);

        // Hướng từ camera tới tia lửa để xoay Quad hướng về người nhìn
        Vector3 toCam = Vector3Subtract(camera.position, start);
        Vector3 dirToCam = Vector3Normalize(toCam);

        // Trục ngang vuông góc
        Vector3 vRight = Vector3Normalize(Vector3CrossProduct(vDir, dirToCam));
        // Thu nhỏ độ rộng xuống w = 0.16f (mỏng bén sắc nét như sợi chỉ sáng rực, loại bỏ cảm giác dải lụa to dẹt)
        float w = 0.16f; 

        Vector3 p1 = Vector3Add(start, Vector3Scale(vRight, w));
        Vector3 p2 = Vector3Subtract(start, Vector3Scale(vRight, w));
        Vector3 p3 = Vector3Subtract(end, Vector3Scale(vRight, w)); // giữ nguyên độ dày w ở đuôi để tạo dải thẳng tắp
        Vector3 p4 = Vector3Add(end, Vector3Scale(vRight, w));

        // Màu xuất phát trắng vàng sáng rực rỡ mờ dần theo tuổi thọ
        Color colStart = ColorAlpha(s->color, ageRatio);
        // Đuôi vệt chuyển sang cam lửa rực rỡ và giữ Alpha tốt hơn (ageRatio * 0.45f) thay vì tắt ngúm 0.0f, giúp cả vệt sáng bừng!
        Color colEnd = ColorAlpha((Color){ 255, 150, 15, 255 }, ageRatio * 0.45f);

        rlColor4ub(colStart.r, colStart.g, colStart.b, colStart.a);
        rlVertex3f(p1.x, p1.y, p1.z);
        rlVertex3f(p2.x, p2.y, p2.z);

        rlColor4ub(colEnd.r, colEnd.g, colEnd.b, colEnd.a);
        rlVertex3f(p3.x, p3.y, p3.z);
        rlVertex3f(p4.x, p4.y, p4.z);
    }
    rlEnd();

    rlEnableDepthMask();
}

void UnloadHoaLongPhongBaSkill(void) {
    // Không giải phóng thủ công Shader và Texture theo quy tắc ResourceManager quản lý
}

bool IsHoaLongPhongBaSkillCoiling(void) {
    return false;
}

int GetHoaLongPhongBaSkillProjectiles(SkillProjectile *out, int max) {
    int count = 0;
    for (int i = 0; i < MAX_VORTICES; i++) {
        if (s_vortices[i].active && !s_vortices[i].impactTriggered && s_vortices[i].gatherTimer <= 0.0f && count < max) {
            out[count].position = s_vortices[i].headPos;
            out[count].radius = BASE_RADIUS * s_vortices[i].scale;
            out[count].active = true;
            count++;
        }
    }
    return count;
}

void DeactivateHoaLongPhongBaProjectile(int index) {
    if (index >= 0 && index < MAX_VORTICES && s_vortices[index].active && !s_vortices[index].impactTriggered) {
        TriggerVortexImpact(s_vortices[index].headPos, s_vortices[index].scale, s_vortices[index].damage, s_vortices[index].knockback);
        s_vortices[index].impactTriggered = true;
    }
}
