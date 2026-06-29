#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include "skills/water/water_orb_skill/water_orb_skill.h"
#include "core/skill_manager.h"
#include "core/resource_manager.h"
#include "core/force_field.h"
#include "core/particle_system.h"
#include "core/color_gradient.h"
#include "core/procedural_mesh_utils.h"
#include "core/vfx_light.h"
#include "core/impact_burst.h"
#include "core/utils_math.h"

#ifndef PI
#define PI 3.1415926535f
#endif

// ============================================================
// Hỏa Long... à không, đây là QUẢ CẦU NƯỚC NÚNG NÍNH (Water Orb)
// Skill nước: một quả cầu nước lơ lửng, thân hình "núng nính"
// (squish-stretch lệch pha theo 3 trục) bay tới mục tiêu rồi vỡ tung.
//
// Kiến trúc:
//   - Không dùng custom shader (theo quyết định của user) — "núng nính"
//     giả lập bằng cách scale phi-đều DrawCoreSphere() qua rlgl matrix
//     stack (rlPushMatrix/rlScalef), KHÔNG phải custom mesh/displacement.
//   - 3 lớp sphere lồng nhau: core đặc sáng, lớp giữa bán trong suốt,
//     lớp vỏ ngoài mờ viền Fresnel giả (alpha thấp + scale lớn hơn).
//   - Bọt khí bên trong = particle quỹ đạo ngẫu nhiên dùng FORCE_NOISE_CURL.
//   - Khi chạm enemy/target: vỡ tung dùng VFX_TriggerImpactBurst.
// ============================================================

#define WATERORB_MAX_PROJECTILES   4

typedef enum {
    ORB_STATE_INACTIVE = 0,
    ORB_STATE_FLYING,
    ORB_STATE_IMPACTED
} OrbState;

typedef struct {
    bool      active;
    OrbState  state;
    Vector3   position;
    Vector3   velocity;
    Vector3   target;
    float     radius;       // bán kính gốc (chưa wobble)
    float     age;          // thời gian tồn tại, dùng cho wobble phase
    float     speed;
    float     damage;
    float     sizeScale;
} WaterOrbProjectile;

static WaterOrbProjectile s_orbs[WATERORB_MAX_PROJECTILES];

static ForceField  s_bubbleForceField;   // static — bắt buộc theo §5
static ColorGradient s_orbGradient;      // core sáng -> thân nước -> viền nhạt

static bool s_initialized = false;

// ------------------------------------------------------------------
// Helper nội bộ: tìm slot rảnh trong pool tĩnh (O(1) free-list scan
// đơn giản — pool chỉ có 4 slot nên scan tuyến tính là chấp nhận được,
// không cần free-list phức tạp như trail/particle pool lớn).
// ------------------------------------------------------------------
static int WaterOrb_FindFreeSlot(void) {
    for (int i = 0; i < WATERORB_MAX_PROJECTILES; i++) {
        if (!s_orbs[i].active) return i;
    }
    return -1;
}

void InitWaterOrbSkill(int screenWidth, int screenHeight) {
    (void)screenWidth;
    (void)screenHeight;

    for (int i = 0; i < WATERORB_MAX_PROJECTILES; i++) {
        s_orbs[i] = (WaterOrbProjectile){0};
        s_orbs[i].active = false;
        s_orbs[i].state  = ORB_STATE_INACTIVE;
    }

    // Force field nội tại cho bọt khí xoáy nhẹ bên trong quả cầu —
    // §5: ForceField phải static, Clear trước khi AddLayer.
    ForceField_Clear(&s_bubbleForceField);
    ForceField_AddLayer(&s_bubbleForceField, (ForceLayer){
        .type       = FORCE_NOISE_CURL,
        .origin     = (Vector3){0.0f, 0.0f, 0.0f}, // offset seed, không dùng làm world pos
        .strength   = 1.2f,
        .noiseScale = 1.5f,
        .noiseSpeed = 0.6f
    });
    ForceField_AddLayer(&s_bubbleForceField, (ForceLayer){
        .type     = FORCE_DRAG,
        .strength = 0.35f // kéo bọt khí lại gần tâm, tránh bay lạc ra ngoài vỏ
    });

    // §6: Color Gradient ưu tiên hơn colorStart/colorEnd cho hiệu ứng
    // nhiều giai đoạn màu — core sáng trắng-xanh -> thân ELEMENT_COLOR_WATER
    // -> viền nhạt gần trong suốt.
    s_orbGradient = (ColorGradient){0};
    ColorGradient_AddStop(&s_orbGradient, 0.0f, ColorLerp(ELEMENT_COLOR_WATER, WHITE, 0.6f));
    ColorGradient_AddStop(&s_orbGradient, 0.5f, ELEMENT_COLOR_WATER);
    ColorGradient_AddStop(&s_orbGradient, 1.0f, ColorLerp(ELEMENT_COLOR_WATER, BLACK, 0.35f));

    s_initialized = true;
}

void CastWaterOrbSkill(Vector3 startPos, Vector3 target, SkillParams params) {
    if (!s_initialized) return;

    int slot = WaterOrb_FindFreeSlot();
    if (slot < 0) return; // pool đầy, bỏ qua cast (không malloc thêm)

    float sizeScale = (params.sizeScale > 0.0f) ? params.sizeScale : 1.0f;

    WaterOrbProjectile *orb = &s_orbs[slot];
    orb->active     = true;
    orb->state      = ORB_STATE_FLYING;
    orb->position   = startPos;
    orb->target     = target;
    orb->radius     = 0.9f * sizeScale;
    orb->age        = 0.0f;
    orb->speed      = 9.0f;
    orb->damage     = (params.damage > 0.0f) ? params.damage : 18.0f;
    orb->sizeScale  = sizeScale;

    Vector3 dir = Vector3Subtract(target, startPos);
    float dist = Vector3Length(dir);
    if (dist > 0.0001f) {
        dir = Vector3Scale(dir, 1.0f / dist);
    } else {
        dir = (Vector3){0.0f, 0.0f, 1.0f};
    }
    orb->velocity = Vector3Scale(dir, orb->speed);

    // Đèn point-light theo dõi quả cầu suốt pha bay — §8 VFX Standards:
    // "Keep point lights alive during the active phase", không spawn/kill
    // trong 1 frame.
    VFXLight_Spawn(orb->position, ELEMENT_COLOR_WATER, 3.0f * sizeScale, 999.0f);
}

void UpdateWaterOrbSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    if (!s_initialized) return;

    for (int i = 0; i < WATERORB_MAX_PROJECTILES; i++) {
        WaterOrbProjectile *orb = &s_orbs[i];
        if (!orb->active) continue;

        orb->age += dt;

        if (orb->state == ORB_STATE_FLYING) {
            orb->position = Vector3Add(orb->position, Vector3Scale(orb->velocity, dt));

            // Spawn bọt khí bên trong khối nước liên tục trong khi bay —
            // §8 VFX Standards: "Spawn particles continuously while active".
            // Lưu ý: đây KHÔNG phải sub-emitter (onDeathEmit/onLiveEmit), nên
            // theo §6 không bắt buộc static — dùng biến stack cục bộ là đúng
            // (mỗi orb trong loop cần config riêng, không chia sẻ).
            ParticleConfig bubbleCfg = (ParticleConfig){0};
            bubbleCfg.position    = orb->position;
            bubbleCfg.velocity    = (Vector3){0.0f, 0.3f, 0.0f};
            bubbleCfg.radius      = 0.05f * orb->sizeScale;
            bubbleCfg.lifetime    = 0.6f;
            bubbleCfg.gradient    = &s_orbGradient;
            bubbleCfg.forceField  = &s_bubbleForceField;
            if (Random01() < 0.5f) {
                SpawnParticle(bubbleCfg);
            }

            // Cập nhật vị trí đèn theo quả cầu.
            // (VFXLight không có API "move" theo id trong tài liệu hiện có —
            //  chấp nhận để đèn đứng yên tại điểm cast; xem TODO cuối file.)

            // Kiểm tra va chạm với enemy.
            float hitDist = orb->radius + enemyRadius;
            if (Vector3Distance(orb->position, enemyPos) <= hitDist) {
                orb->state = ORB_STATE_IMPACTED;
            }

            // Kiểm tra đã bay quá xa target (path thẳng, không homing) thì
            // tự vỡ tại target để không bay xuyên qua vô hạn.
            if (Vector3Distance(orb->position, orb->target) <= orb->radius * 0.5f) {
                orb->state = ORB_STATE_IMPACTED;
            }
        }

        if (orb->state == ORB_STATE_IMPACTED) {
            ApplyAoEDamage(orb->position, orb->radius * 1.5f, orb->damage, 4.0f);

            ImpactBurstConfig burst = {0};
            burst.distortEnabled  = true;
            burst.distortRadius   = 2.0f * orb->sizeScale;
            burst.distortStrength = 0.4f;
            burst.distortLife     = 0.3f;
            burst.distortSpeed    = 6.0f;

            burst.decalEnabled        = false; // quả cầu vỡ trên không / trên enemy, không cần decal đất

            burst.lightEnabled = true;
            burst.lightColor   = ELEMENT_COLOR_WATER;
            burst.lightRadius  = 4.0f;
            burst.lightLife    = 0.4f;

            burst.particlesEnabled = true;
            burst.particles.countMin   = 14;
            burst.particles.countMax   = 22;
            burst.particles.speedMin   = 2.0f;
            burst.particles.speedMax   = 5.0f;
            burst.particles.radiusMin  = 0.06f;
            burst.particles.radiusMax  = 0.14f;
            burst.particles.lifetimeMin = 0.4f;
            burst.particles.lifetimeMax = 0.8f;
            burst.particles.pitchRange  = 60.0f;
            burst.particles.upwardBias  = 0.4f;
            burst.particles.gradient    = &s_orbGradient;

            VFX_TriggerImpactBurst(orb->position, orb->sizeScale, &burst);

            orb->active = false;
            orb->state  = ORB_STATE_INACTIVE;
        }
    }
}

// ------------------------------------------------------------------
// Vẽ quả cầu nước "núng nính" — 3 lớp sphere lồng nhau, mỗi lớp scale
// phi-đều theo 3 trục, lệch pha bằng hằng số PHASE_X/Y/Z để tạo cảm
// giác bóp-méo ngẫu nhiên hữu cơ (không lặp đối xứng cả 3 trục cùng lúc).
// §12.1 cho phép DrawCoreSphere (API engine), chỉ cấm DrawSphere() gốc
// của Raylib — rlPushMatrix/rlScalef là rlgl transform, không phải mesh
// primitive, nên không vi phạm rule.
// ------------------------------------------------------------------
static void WaterOrb_DrawWobblySphere(Vector3 center, float baseRadius, float age,
                                       float wobbleAmp, float wobbleSpeed,
                                       int rings, int slices, Color color) {
    const float PHASE_X = 0.0f;
    const float PHASE_Y = 2.094395f; // 2*PI/3
    const float PHASE_Z = 4.18879f;  // 4*PI/3

    float sx = 1.0f + wobbleAmp * sinf(age * wobbleSpeed + PHASE_X);
    float sy = 1.0f + wobbleAmp * sinf(age * wobbleSpeed + PHASE_Y);
    float sz = 1.0f + wobbleAmp * sinf(age * wobbleSpeed + PHASE_Z);

    // Bảo toàn thể tích gần đúng: nếu 1 trục phình thì 2 trục còn lại hơi
    // thắt lại — đây là cảm giác "núng nính" nước thật (incompressible),
    // tránh trông như quả cầu chỉ phồng to lên đều.
    float volumeCorrect = 1.0f / cbrtf(sx * sy * sz);
    sx *= volumeCorrect;
    sy *= volumeCorrect;
    sz *= volumeCorrect;

    rlPushMatrix();
        rlTranslatef(center.x, center.y, center.z);
        rlScalef(sx, sy, sz);
        DrawCoreSphere((Vector3){0.0f, 0.0f, 0.0f}, baseRadius, rings, slices, color);
    rlPopMatrix();
}

void DrawWaterOrbSkill(void) {
    if (!s_initialized) return;

    rlColor4ub(255, 255, 255, 255); // §11.1: reset vertex color trước khi vẽ custom geometry

    for (int i = 0; i < WATERORB_MAX_PROJECTILES; i++) {
        WaterOrbProjectile *orb = &s_orbs[i];
        if (!orb->active || orb->state != ORB_STATE_FLYING) continue;

        Color coreColor = ColorGradient_Sample(&s_orbGradient, 0.0f);
        Color bodyColor = ColorGradient_Sample(&s_orbGradient, 0.5f);
        Color shellColor = ColorAlpha(ColorGradient_Sample(&s_orbGradient, 1.0f), 0.35f);

        // Lớp vỏ ngoài: lớn hơn, mờ, wobble nhanh & mạnh nhất — giả Fresnel
        // rim bằng alpha thấp + kích thước nhô ra ngoài thân chính.
        WaterOrb_DrawWobblySphere(orb->position, orb->radius * 1.15f, orb->age,
                                   0.22f, 5.0f, 10, 14, shellColor);

        // Lớp thân chính: màu ELEMENT_COLOR_WATER, wobble biên độ trung bình.
        WaterOrb_DrawWobblySphere(orb->position, orb->radius, orb->age,
                                   0.16f, 5.0f, 12, 16, bodyColor);

        // Core sáng bên trong: nhỏ hơn, wobble ngược pha nhẹ (dùng age*1.3
        // để lệch nhịp với lớp thân, tránh "đập" đồng bộ trông giả).
        WaterOrb_DrawWobblySphere(orb->position, orb->radius * 0.55f, orb->age * 1.3f,
                                   0.12f, 6.0f, 8, 10, coreColor);
    }
}

void UnloadWaterOrbSkill(void) {
    // §3 Mandatory Teardown Rule: KHÔNG gọi UnloadTexture/UnloadShader —
    // skill này không tự load texture/shader riêng (chỉ dùng DrawCoreSphere
    // + particle/light hệ thống dùng chung), nên không có gì cần dọn ở đây.
}

bool IsWaterOrbSkillCoiling(void) {
    // Skill này không có pha "coiling" (cuộn lực trước khi bắn) —
    // luôn trả false. Giữ hàm để khớp đúng prototype lifecycle bắt buộc.
    return false;
}

int GetWaterOrbSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles) {
    if (outProjectiles == NULL) return 0;

    int count = 0;
    for (int i = 0; i < WATERORB_MAX_PROJECTILES && count < maxProjectiles; i++) {
        if (!s_orbs[i].active || s_orbs[i].state != ORB_STATE_FLYING) continue;
        outProjectiles[count].position = s_orbs[i].position;
        outProjectiles[count].radius   = s_orbs[i].radius;
        outProjectiles[count].active   = true;
        count++;
    }
    return count;
}

void DeactivateWaterOrbProjectile(int index) {
    if (index < 0 || index >= WATERORB_MAX_PROJECTILES) return;
    s_orbs[index].active = false;
    s_orbs[index].state  = ORB_STATE_INACTIVE;
}

/* ------------------------------------------------------------------
 * TODO (cần xác nhận từ source thật trước khi coi là production-ready):
 *
 * 1. VFXLight_Spawn() trong tài liệu không có API "di chuyển" 1 đèn đã
 *    spawn theo id — quả cầu bay nhưng đèn light đứng yên tại điểm cast.
 *    Cần kiểm tra core/vfx_light.h thật: nếu có VFXLight id + hàm
 *    VFXLight_SetPosition(id, pos), nên dùng để đèn bám theo quả cầu.
 *    => Cấp độ: Inferred/thiếu — chưa có ground-truth để sửa đúng cách.
 *
 * 2. ParticleConfig.forceField trong tài liệu ghi rõ "Dynamic steering"
 *    nhưng không nói rõ particle bám theo *world-space* hay *local-space*
 *    của orb->position đang di chuyển. Bong bóng hiện spawn tại world-pos
 *    hiện tại của quả cầu mỗi frame (không gắn theo parent), nên khi quả
 *    cầu bay, bong bóng cũ sẽ "rớt lại" phía sau thay vì bay theo cùng
 *    quả cầu — đây là hành vi hợp lý cho hiệu ứng "sủi bọt để lại vệt", nhưng
 *    nếu ý đồ là bọt khí kẹt CỐ ĐỊNH bên trong khối nước di chuyển cùng
 *    quả cầu, cần cơ chế parent-attach khác (có thể là TRAIL_TYPE_FOLLOWER
 *    thay vì particle rời) — cần xác nhận ý đồ thiết kế.
 * ------------------------------------------------------------------ */
