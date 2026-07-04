#include "skills/fire/hoa_long_phong_ba_skill/hoa_long_phong_ba_skill.h"
#include "core/force_field.h"
#include "core/particle_system.h"
#include "core/color_gradient.h"
#include "core/decal_system.h"
#include "core/screen_distort.h"
#include "core/vfx_light.h"
#include "core/camera_fx.h"
#include "core/skill_manager.h"
#include "core/skill_helper.h"
#include "core/resource_manager.h"
#include "core/procedural_mesh_utils.h"
#include "core/path_spline.h"
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

#include "hoa_long_phong_ba_skill_params.inl"

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
    int ownerAgentId; // CORE_ISSUES.md Item 15 — set from Cast's agentId param
} HoaLongPhongBaVortex;

static HoaLongPhongBaVortex s_vortices[MAX_VORTICES];


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

// Toán học Bezier: dùng chung core/path_spline.h (GetBezierPoint/GetBezierTangent)
// thay vì hand-roll — xem CORE_API.md "Path Spline". GetBezierTangent trả về
// tangent (chưa normalize) tương đương hệt local GetBezierDerivative cũ, chỉ
// khác tên tham số thứ 4 (target thay vì p3), cùng công thức đạo hàm Bezier
// bậc 3 chuẩn.

// CORE_ISSUES.md Item 16 — cooldown gating state, cached once in Init
static int s_skillIndex = -1;

static void RebuildTravelField(void) {
    ForceField_Clear(&s_travelField);
    ForceField_AddLayer(&s_travelField, (ForceLayer){
        .type = FORCE_NOISE_CURL,
        .strength = s_travelCurlStrength,
        .noiseScale = 7.0f,
        .noiseSpeed = 4.5f
    });
    // Layer 1 placeholder for per-frame wind direction (set in Update)
    ForceField_AddLayer(&s_travelField, (ForceLayer){ .type = FORCE_WIND });
    ForceField_AddLayer(&s_travelField, (ForceLayer){
        .type = FORCE_DRAG,
        .strength = s_travelDrag
    });
    SkillForceMix_AddLayers(&s_travelForceMix, &s_travelField);
}

static void RebuildGeyserField(void) {
    ForceField_Clear(&s_geyserField);
    ForceField_AddLayer(&s_geyserField, (ForceLayer){
        .type = FORCE_VORTEX,
        .direction = { 0.0f, 1.0f, 0.0f },
        .strength = s_geyserVortexStrength,
        .radius = s_geyserVortexRadius,
        .falloff = 1.0f
    });
    ForceField_AddLayer(&s_geyserField, (ForceLayer){
        .type = FORCE_GRAVITY_DIR,
        .direction = { 0.0f, 1.0f, 0.0f },
        .strength = s_geyserUpStrength
    });
    ForceField_AddLayer(&s_geyserField, (ForceLayer){
        .type = FORCE_NOISE_CURL,
        .strength = s_geyserCurlStrength,
        .noiseScale = 5.0f,
        .noiseSpeed = 6.0f
    });
    ForceField_AddLayer(&s_geyserField, (ForceLayer){
        .type = FORCE_DRAG,
        .strength = s_geyserDrag
    });
    SkillForceMix_AddLayers(&s_geyserForceMix, &s_geyserField);
}

// Khởi tạo tài nguyên kỹ năng
void InitHoaLongPhongBaSkill(int w, int h) {
    s_skillIndex = Skill_GetIndexByName("HOA_LONG_PHONG_BA");

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
    // Thiết lập ColorGradient cho hạt lửa
    s_fireGrad.count = 0;
    ColorGradient_AddStop(&s_fireGrad, 0.00f, (Color){ 255, 255, 220, 255 }); // Lõi trắng vàng siêu nóng
    ColorGradient_AddStop(&s_fireGrad, 0.15f, (Color){ 255, 170, 20, 255 });  // Cam sáng rực rỡ
    ColorGradient_AddStop(&s_fireGrad, 0.50f, (Color){ 220, 40, 5, 200 });   // Đỏ cam rực
    ColorGradient_AddStop(&s_fireGrad, 0.85f, (Color){ 100, 10, 0, 100 });   // Đỏ sậm tàn tro
    ColorGradient_AddStop(&s_fireGrad, 1.00f, (Color){ 30, 30, 30, 0 });      // Khói đen tan biến

    // Force fields are seeded via RebuildXxxField() below, which also merges
    // the SkillForceMix overlay — call again at the top of each Update so
    // sandbox tunable changes take effect immediately.
    RebuildTravelField();
    RebuildGeyserField();

    // 3. Trường lực tàn lửa rơi rụng (Fallout Field): Rơi chậm + Cản mạnh để tàn bay lơ lửng
    ForceField_Clear(&s_falloutField);
    ForceField_AddLayer(&s_falloutField, (ForceLayer){
        .type = FORCE_GRAVITY_DIR,
        .direction = { 0.0f, -1.0f, 0.0f },
        .strength = 0.35f
    });
    ForceField_AddLayer(&s_falloutField, (ForceLayer){
        .type = FORCE_DRAG,
        .strength = 4.5f
    });

    // Seed over-lifetime curves flat (no change until shaped in sandbox)
    SkillCurve_SetConstant(&s_travelRadiusCurve,  1.0f);
    SkillCurve_SetConstant(&s_travelSpeedCurve,   1.0f);
    SkillCurve_SetConstant(&s_travelAlphaCurve,   1.0f);
    SkillCurve_SetConstant(&s_travelEmissiveCurve,1.0f);
    SkillCurve_SetConstant(&s_geyserRadiusCurve,  1.0f);
    SkillCurve_SetConstant(&s_geyserSpeedCurve,   1.0f);
    SkillCurve_SetConstant(&s_geyserAlphaCurve,   1.0f);
    SkillCurve_SetConstant(&s_geyserEmissiveCurve,1.0f);

    // Register sandbox tunables
    static SkillTunableEntry s_tunables[HOA_LONG_TUNABLE_COUNT];
    int tn = 0;
#include "hoa_long_phong_ba_skill_tunables.inl"
    SkillTunables_LoadPersisted("skills/fire/hoa_long_phong_ba_skill/hoa_long_phong_ba_skill.tuning", s_tunables, tn);
    RegisterSkillTunables(s_skillIndex, s_tunables, tn);
}

// Bắt đầu thi triển chiêu thức
void CastHoaLongPhongBaSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    if (!SkillManager_CanCast(s_skillIndex, agentId)) return;

    for (int i = 0; i < MAX_VORTICES; i++) {
        if (!s_vortices[i].active) {
            float sizeScale = (params.sizeScale > 0.0f) ? params.sizeScale : 1.0f;
            Vector3 to = Vector3Subtract(target, startPos);
            float dist = Vector3Length(to);
            if (dist < 0.10f) dist = 0.10f; // min 0.1 m (was 10 cm in old scale)
            Vector3 dir = Vector3Scale(to, 1.0f / dist);

            // Tạo các điểm điều khiển Bezier uốn lượn
            Vector3 perpX = Vector3Normalize((Vector3){ -dir.z, 0.0f, dir.x });
            if (Vector3Length(perpX) < 0.1f) perpX = (Vector3){ 1.0f, 0.0f, 0.0f };
            Vector3 perpY = Vector3Normalize(Vector3CrossProduct(dir, perpX));

            float amp = dist * 0.22f; // biên độ uốn lượn ban đầu
            float twistPhase = (float)rand() / (float)RAND_MAX * 2.0f * PI;

            Vector3 cp1 = Vector3Add(
                Vector3Add(startPos, Vector3Scale(dir, dist * 0.33f)),
                Vector3Add(Vector3Scale(perpX, sinf(twistPhase) * amp), Vector3Scale(perpY, cosf(twistPhase) * amp + 0.15f))
            );
            Vector3 cp2 = Vector3Add(
                Vector3Add(startPos, Vector3Scale(dir, dist * 0.66f)),
                Vector3Add(Vector3Scale(perpX, -sinf(twistPhase) * amp * 0.8f), Vector3Scale(perpY, -cosf(twistPhase) * amp * 0.8f + 0.25f))
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
                .twistPhase = twistPhase,
                .ownerAgentId = agentId
            };

            // Spawn một chớp sáng nhẹ báo hiệu tụ chiêu
            VFXLight_Spawn(startPos, ELEMENT_COLOR_FIRE, s_castLightRadius * sizeScale, 0.4f, VFX_PRIORITY_LOW);

            SkillManager_TriggerCooldown(s_skillIndex, agentId, Skill_CalculateCooldown(SKILL_CAT_AOE_CONTROL, params));
            break;
        }
    }
}

// Xử lý nổ lốc lửa khổng lồ khi va chạm
static void TriggerVortexImpact(Vector3 pos, float scale, float damage, float knockback) {
    // 1. Ánh sáng rực rỡ (Bỏ hiệu ứng rung camera và sóng sung kích màn hình theo yêu cầu)
    VFXLight_Spawn(pos, ELEMENT_COLOR_FIRE, s_impactLightRadius * scale, 1.8f, VFX_PRIORITY_LOW);

    // 2. Tạo Decal nứt đất rực lửa cỡ lớn
    // Y dịch nhẹ lên +0.02f chống Z-fighting nhấp nháy lưới đất
    Vector3 decalPos = { pos.x, pos.y + 0.02f, pos.z };
    DecalSystem_Add(decalPos, (float)(rand() % 360), s_baseRadius * scale * 5.2f, s_crackTex, 4.5f, ELEMENT_COLOR_FIRE);

    // 3. Gây sát thương AoE diện rộng và khóa chân kẻ địch
    // Pass 4: distance-proportional floor+cap applied at call site in Update
    ApplyAoEDamage(pos, s_aoeRadius * scale, damage, knockback);
    AddRootToEnemy(3.2f); // giữ chân 3.2 giây trong cột lửa

    // 4. Tạo hiệu ứng Hỏa Long Phong Ba (Cột lốc lửa cuộn đứng)
    int geyserHạtCount = (int)(65.0f * scale);
    for (int i = 0; i < geyserHạtCount; i++) {
        float angle = (float)i / (float)geyserHạtCount * 2.0f * PI;
        float dist = (float)rand() / (float)RAND_MAX * s_baseRadius * scale * 2.2f;
        Vector3 particlePos = {
            pos.x + cosf(angle) * dist,
            pos.y + (float)rand() / (float)RAND_MAX * s_geyserPosYRange,
            pos.z + sinf(angle) * dist
        };

        // Vận tốc ban đầu: xoay tròn nhẹ + phun vút thẳng lên trời
        float spinSpeed = ((float)(rand() % 40) / 100.0f + s_geyserSpinSpeedMin) * scale;
        float upSpeed   = ((float)(rand() % 100) / 100.0f * (s_geyserUpSpeedMax - s_geyserUpSpeedMin) + s_geyserUpSpeedMin) * scale;
        Vector3 vel = {
            -sinf(angle) * spinSpeed,
            upSpeed,
            cosf(angle) * spinSpeed
        };

        ParticleConfig cfg = {
            .position = particlePos,
            .velocity = vel,
            .radius = ((float)rand() / (float)RAND_MAX * s_geyserParticleRadius + s_geyserParticleRadius * 0.3f) * scale * 4.0f,
            .lifetime = (float)rand() / (float)RAND_MAX * 1.0f + 0.8f,
            .forceField = &s_geyserField,
            .gradient = &s_fireGrad,
            .radiusCurve   = &s_geyserRadiusCurve,
            .speedCurve    = &s_geyserSpeedCurve,
            .alphaCurve    = &s_geyserAlphaCurve,
            .emissiveCurve = &s_geyserEmissiveCurve
        };
        SpawnParticle(cfg);
    }

    // Sinh ra các tàn lửa chói sáng kéo vệt dài (Accent Sparks)
}

// Cập nhật trạng thái kỹ năng qua từng khung hình
void UpdateHoaLongPhongBaSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    // Zero-instance early-out: skip per-frame field rebuilds when idle.
    bool anyActive = false;
    for (int idx = 0; idx < MAX_VORTICES; idx++) {
        if (s_vortices[idx].active) { anyActive = true; break; }
    }
    if (!anyActive) return;

    float time = (float)GetTime();
    RebuildTravelField();
    RebuildGeyserField();

    for (int idx = 0; idx < MAX_VORTICES; idx++) {
        if (!s_vortices[idx].active) continue;
        HoaLongPhongBaVortex *v = &s_vortices[idx];

        // --- GIAI ĐOẠN 1: GATHER (TỤ LỰC TRƯỚC BẮN) ---
        if (v->gatherTimer > 0.0f) {
            v->gatherTimer -= dt;

            // Spawn decal pháp trận tụ lực đỏ cam quay dưới chân caster (player)
            if (v->gatherTimer > 0.05f) {
                Vector3 pFoot = { v->startPos.x, v->startPos.y + 0.02f, v->startPos.z };
                DecalSystem_Add(pFoot, time * 90.0f, s_baseRadius * v->scale * 1.8f, s_crackTex, 0.1f, ColorAlpha(ELEMENT_COLOR_FIRE, 0.4f));
            }

            // Sinh các hạt tụ lửa xung quanh tụ về caster
            for (int i = 0; i < 4; i++) {
                float angle = (float)rand() / (float)RAND_MAX * 2.0f * PI;
                float rDist = (float)rand() / (float)RAND_MAX * s_gatherRDistMax + s_gatherRDistMin;
                float height = ((float)rand() / (float)RAND_MAX - 0.3f) * s_gatherHeightRange;

                Vector3 startPart = {
                    v->startPos.x + cosf(angle) * rDist,
                    v->startPos.y + height + s_gatherHeightBase,
                    v->startPos.z + sinf(angle) * rDist
                };

                // Vận tốc hướng về tâm startPos
                Vector3 toCenter = Vector3Subtract(v->startPos, startPart);
                Vector3 vel = Vector3Scale(Vector3Normalize(toCenter), s_gatherSpeed);

                ParticleConfig cfg = {
                    .position = startPart,
                    .velocity = vel,
                    .radius = s_gatherParticleRadius * v->scale * 4.0f,
                    .lifetime = 0.5f,
                    .gradient = &s_fireGrad,
                    .radiusCurve   = &s_travelRadiusCurve,
                    .speedCurve    = &s_travelSpeedCurve,
                    .alphaCurve    = &s_travelAlphaCurve,
                    .emissiveCurve = &s_travelEmissiveCurve
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
            if (dist < 0.1f) dist = 0.1f;
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
            Vector3 tangent = Vector3Normalize(GetBezierTangent(v->startPos, dynamicP1, dynamicP2, v->targetPos, v->progress));
            // Hạt phun tạt ra đằng sau đạn
            Vector3 backVel = Vector3Scale(Vector3Negate(tangent), s_travelBackSpeed * v->scale);

            // Đặt lực gió ngược chiều bay vào travelField
            s_travelField.layers[1].type = FORCE_WIND;
            s_travelField.layers[1].direction = Vector3Normalize(backVel);
            s_travelField.layers[1].strength = s_travelWindStrength;
            s_travelField.layers[1].radius = 0.0f;
            s_travelField.layers[1].falloff = 0.0f;
            // Layer 1 is the placeholder wind slot — direction/strength set here each frame.

            for (int k = 0; k < 1; k++) { // Tối ưu hóa hiệu năng: 3 -> 1 hạt mỗi frame khi bay
                Vector3 pOffset = {
                    v->headPos.x + ((float)rand() / (float)RAND_MAX - 0.5f) * s_baseRadius * v->scale * 0.6f,
                    v->headPos.y + ((float)rand() / (float)RAND_MAX - 0.5f) * s_baseRadius * v->scale * 0.6f,
                    v->headPos.z + ((float)rand() / (float)RAND_MAX - 0.5f) * s_baseRadius * v->scale * 0.6f
                };

                ParticleConfig cfg = {
                    .position = pOffset,
                    .velocity = Vector3Add(backVel, (Vector3){
                        ((float)rand() / (float)RAND_MAX - 0.5f) * s_travelJitter,
                        ((float)rand() / (float)RAND_MAX - 0.5f) * s_travelJitter,
                        ((float)rand() / (float)RAND_MAX - 0.5f) * s_travelJitter
                    }),
                    .radius = s_travelParticleRadius * v->scale,
                    .lifetime = (float)rand() / (float)RAND_MAX * 0.4f + 0.35f,
                    .forceField = &s_travelField,
                    .gradient = &s_fireGrad,
                    .emissiveCurve = &s_travelEmissiveCurve
                };
                SpawnParticle(cfg);
            }

            // Va chạm nếu đi hết tiến trình hoặc trúng vùng chạm địch thủ
            bool hitEnemy = false;
            float checkDist = s_baseRadius * v->scale + enemyRadius;
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
                float rDist = (float)rand() / (float)RAND_MAX * s_aftermathRingRadius * v->scale * ageRatio;
                Vector3 pSpawn = {
                    v->targetPos.x + cosf(angle) * rDist,
                    v->targetPos.y + (float)rand() / (float)RAND_MAX * s_aftermathYJitter,
                    v->targetPos.z + sinf(angle) * rDist
                };

                float upSpeed   = (s_aftermathUpSpeedMin + (float)rand() / (float)RAND_MAX * (s_aftermathUpSpeedMax - s_aftermathUpSpeedMin)) * v->scale * ageRatio;
                float spinSpeed = (s_aftermathSpinMin    + (float)rand() / (float)RAND_MAX * (s_aftermathSpinMax    - s_aftermathSpinMin))    * v->scale * ageRatio;
                Vector3 vel = {
                    -sinf(angle) * spinSpeed,
                    upSpeed,
                    cosf(angle) * spinSpeed
                };

                ParticleConfig cfg = {
                    .position = pSpawn,
                    .velocity = vel,
                    .radius = s_aftermathParticleRadius * v->scale * ageRatio,
                    .lifetime = (float)rand() / (float)RAND_MAX * 0.9f + 0.7f,
                    .forceField = &s_geyserField,
                    .gradient = &s_fireGrad,
                    .emissiveCurve = &s_geyserEmissiveCurve
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
    // CORE_ISSUES.md Item 11: hoa_long_phong_ba.vs computes fragNormal =
    // normalize(matModel * vertexNormal) directly. DrawCoreSphere() below
    // draws via raw rlgl immediate mode, which never auto-uploads matModel
    // (only DrawMesh/DrawModel do) — without this call matModel stays a
    // zero-initialized matrix -> normalize(vec3(0)) -> NaN normal.
    SkillManager_BeginShader(s_shader);

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
        DrawCoreSphere(v->headPos, s_baseRadius * v->scale * 1.25f, 24, 24, WHITE);
    }

    SkillManager_EndShader();
    EndShaderMode();

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
            out[count].radius = s_baseRadius * s_vortices[i].scale;
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
