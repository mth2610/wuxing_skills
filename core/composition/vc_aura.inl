// ============================================================================
// AURA_QI — "chân khí": khí bốc lên từ 1 vành quanh chân, xoáy nhẹ, tan ở đỉnh.
// Khác hẳn model "neo quanh thân + orbit" của các AURA_* khác bên dưới, nên
// tách hẳn thành nhánh riêng, return sớm, KHÔNG đi qua switch/ring-decal cũ.
//
// LƯU Ý CẦN BẠN XÁC NHẬN TRƯỚC KHI BUILD:
//   1. Enum AuraStyle nằm ở header khác (không có trong file .inl này) — cần
//      tự thêm giá trị AURA_QI vào đó, mình không thấy header nên không sửa được.
//   2. GetTrail(trailId) giả định trả về con trỏ có field .pos/.tint/.scale
//      (đúng như cách "GetTrail(id)->ownerTag" được nhắc trong CORE_API.md,
//      nhưng field list đầy đủ của TrailEntity thì mình không có — kiểm tra
//      lại tên field khớp với trail_system.h thực tế của bạn).
//   3. Xác suất spawn dùng GetRandomValue() thuần theo frame, giả định 60fps
//      (giống hệt cách file gốc đang làm ở dòng "GetRandomValue(0,100) < 15"
//      bên dưới) — vì signature hàm này không có dt.
//   4. cfg.tex trỏ tới "assets/textures/vfx/qi_wisp_soft.png" — ĐỔI đường dẫn
//      này thành texture khói mềm bạn tự tạo (xem prompt phần đầu trả lời),
//      KHÔNG dùng chung decal_burn.png của style lửa/băng.
// ============================================================================

#define QI_SPAWN_CHANCE_PERMILLE 42 // ~2.5 cột khí/giây @ 60fps
#define QI_COLUMN_LIFE_MIN 2.0f
#define QI_COLUMN_LIFE_MAX 3.2f
#define QI_SPARKLE_CHANCE_PER_SEC 6.0f // trung bình 6 lần lấp lánh/giây/cột

static ForceField s_qiRiseFld;
static ColorGradient s_qiGrad;
static Texture2D s_qiWispTex;
static bool s_qiInit = false;

static void QiAura_LazyInit(void)
{
    if (s_qiInit)
        return;
    s_qiInit = true;

    // FIX #1 (nguyên nhân chính gây hình "lưỡi liềm sketchy"): texture mềm,
    // không dùng texture mặc định/fallback nữa.
    s_qiWispTex = ResourceManager_LoadTexture("assets/textures/qi_wisp_soft.png");

    // FIX #3: giảm mạnh cường độ curl + viscosity so với bản trước — bản cũ
    // bẻ cong + hãm quá nhanh khiến wisp "đông cứng" thành hình tĩnh thay vì
    // tiếp tục bốc lên xoáy nhẹ liên tục.
    ForceField_Clear(&s_qiRiseFld);
    ForceField_AddLayer(&s_qiRiseFld, (ForceLayer){
                                          .type = FORCE_GRAVITY_DIR,
                                          .direction = (Vector3){0.0f, 1.0f, 0.0f},
                                          .strength = 0.95f,
                                      });
    ForceField_AddLayer(&s_qiRiseFld, (ForceLayer){
                                          .type = FORCE_NOISE_CURL,
                                          .strength = 0.28f, // gentle wavy shimmer, no chaotic scattering
                                          .noiseScale = 0.5f,
                                          .noiseSpeed = 0.6f,
                                      });
    ForceField_AddLayer(&s_qiRiseFld, (ForceLayer){
                                          .type = FORCE_VISCOSITY,
                                          .strength = 0.4f,
                                      });

    // Gradient thân sương: alpha đỉnh chỉ ~0.27, KHÔNG bao giờ chạm 1.0,
    // fade in lúc mới sinh + fade out hẳn về 0 lúc tan ở đỉnh (t=1).
    ColorGradient_AddStop(&s_qiGrad, 0.00f, (Color){210, 235, 255, 0});
    ColorGradient_AddStop(&s_qiGrad, 0.15f, (Color){210, 235, 255, 70});
    ColorGradient_AddStop(&s_qiGrad, 0.85f, (Color){210, 235, 255, 55});
    ColorGradient_AddStop(&s_qiGrad, 1.00f, (Color){210, 235, 255, 0});
}

// Lớp lấp lánh: cưỡi trên wisp, tương phản hẳn với thân sương (alpha cao,
// additive, ngắn, thưa) — cùng triết lý LightningTrailFlicker nhưng cho wisp.
static void QiWispSparkle(int trailId, float dt)
{
    TrailEntity *t = GetTrail(trailId);
    if (!t)
        return;

    float chancePerFrame = QI_SPARKLE_CHANCE_PER_SEC * dt;
    if ((float)GetRandomValue(0, 10000) / 10000.0f >= chancePerFrame)
        return;

    Color hot = (Color){255, 255, 255, 235}; // sáng gắt, gần trắng tinh — ngược hẳn thân mờ
    VFXLight_Spawn(t->position, hot, 0.35f * t->scale, 0.12f, VFX_PRIORITY_LOW);

    SpawnParticle((ParticleConfig){
        .position = t->position,
        .velocity = (Vector3){0.0f, 0.15f, 0.0f},
        .colorStart = hot,
        .colorEnd = (Color){255, 255, 255, 0},
        .radius = 0.015f * t->scale,
        .lifetime = 0.15f,
    });
}

void VFX_ComposeAura(AuraStyle style, Vector3 pos, float radius, float time)
{
    QiAura_LazyInit();

    // Map style to corresponding element color
    Color color = {210, 235, 255, 255}; // default AURA_QI (silver/blue)
    switch (style)
    {
    case AURA_FIRE:
        color = (Color){255, 90, 15, 255};
        break; // lint: allow-color
    case AURA_ICE:
        color = (Color){160, 225, 255, 255};
        break; // lint: allow-color
    case AURA_WIND:
        color = (Color){100, 230, 140, 255};
        break; // lint: allow-color
    case AURA_LIGHTNING:
        color = (Color){180, 110, 255, 255};
        break; // lint: allow-color
    case AURA_TAIJI:
        color = (Color){245, 230, 190, 255};
        break; // lint: allow-color
    case AURA_QI:
        color = (Color){210, 235, 255, 255};
        break; // lint: allow-color
    }

    // Spawn more, smaller, and thinner wisps (Kiểu B)
    if (GetRandomValue(0, 999) < 85) // Spawn ~5.5 columns/sec at 60FPS
    {
        float angle = (float)GetRandomValue(0, 359) * DEG2RAD;
        // Spawn exactly on the boundary ring of the radius (thin ring)
        float r = radius * (0.95f + ((float)rand() / (float)RAND_MAX) * 0.05f);
        Vector3 spawnPos = {
            pos.x + cosf(angle) * r,
            pos.y + 0.05f,
            pos.z + sinf(angle) * r};

        float riseSpeed = 0.55f + ((float)rand() / (float)RAND_MAX) * 0.35f;
        float life = QI_COLUMN_LIFE_MIN +
                     ((float)rand() / (float)RAND_MAX) * (QI_COLUMN_LIFE_MAX - QI_COLUMN_LIFE_MIN);

        TrailConfig cfg = {0};
        cfg.type = TRAIL_TYPE_WISP;
        cfg.pos = spawnPos;
        cfg.vel = (Vector3){0.0f, riseSpeed, 0.0f}; // strictly vertical rise
        cfg.len = 1.0f * radius;                    // medium-height strand
        // Thinner filaments (reduced from 0.025f to 0.006f)
        cfg.thick = (0.006f + ((float)rand() / (float)RAND_MAX) * 0.01f) * radius;
        cfg.trailLength = 26.0f; // smooth wavy trail
        cfg.life = life;
        cfg.scale = 1.0f;
        cfg.tex = s_qiWispTex;
        cfg.tint = (Color){color.r, color.g, color.b, 80}; // element color with soft alpha
        cfg.blendMode = BLEND_ALPHA;                       // Alpha blending! Not additive!
        cfg.forceField = &s_qiRiseFld;
        cfg.gradient = NULL; // use default wisp style taper based on cfg.tint
        cfg.onUpdate = QiWispSparkle;
        cfg.priority = VFX_PRIORITY_LOW;

        SpawnTrailEntity(cfg);
    }
}
