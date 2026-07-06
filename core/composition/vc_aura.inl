// vc_aura.inl

#define QI_SPAWN_CHANCE_PERMILLE 50    // Tăng tỷ lệ sinh hạt để bù cho thời gian sống ngắn
#define QI_COLUMN_LIFE_MIN 1.2f        // Giảm mạnh thời gian sống (trước là 2.4f)
#define QI_COLUMN_LIFE_MAX 3.4f        // Giảm mạnh thời gian sống (trước là 3.8f)
#define QI_SPARKLE_CHANCE_PER_SEC 1.8f // Giảm nhẹ (trước 2.2f) vì số cột đã ổn định hơn, không cần bù mật độ

// --- Cân bằng SỐ LƯỢNG: ngân sách cứng, không chỉ dựa vào random mỗi frame ---
// QI_SPAWN_CHANCE_PERMILLE chỉ quyết định NHỊP xuất hiện; "vừa đủ, không quá nhiều" được
// đảm bảo bởi trần số cột đang sống cùng lúc bên dưới - deterministic, không phụ thuộc frame rate
// hay rớt frame (khác với chỉ random suông, có thể "bùng" nếu nhiều frame liên tiếp trúng số).
#define QI_MAX_CONCURRENT_COLUMNS 6 // Tối đa 6 cột khí cùng lúc -> mắt luôn dõi theo được, không rối
#define QI_CORE_CHANCE_PERMILLE 850 // Chỉ ~55% số cột được gắn lõi sáng -> có cột sáng nổi bật, có cột chỉ là khói mờ (tự nhiên hơn, đỡ tốn thêm pool)

// --- Cân bằng CHUYỂN ĐỘNG: xoáy có chủ đích + jitter nhỏ, thay vì random hoàn toàn ---
// Trước đây randomAngleOffset random đều 0..2*PI nên baseAngle (vòng xoáy theo thời gian) gần như
// bị xoá sạch - cột xuất hiện ở vị trí hoàn toàn ngẫu nhiên, không thấy xoáy đâu cả. Giờ chỉ jitter
// nhẹ quanh baseAngle -> mắt thấy các cột đang "đuổi" theo nhau theo một vòng xoáy chậm, có chủ đích,
// xen chút ngẫu nhiên hữu cơ cho tự nhiên (không bị máy móc).
#define QI_SWIRL_JITTER_RAD 0.55f

// --- Lõi sáng (hot core) & ánh sáng nền: tham số cải tiến ---
#define QI_CORE_WIDTH_MULT 0.6f  // Thu nhỏ `len` (bề rộng, vuông góc hướng bay) vừa phải -> mảnh hơn thân khói nhưng KHÔNG lọt dưới ngưỡng nhìn thấy
#define QI_CORE_MIN_LEN 0.02f    // Sàn tuyệt đối (mét) cho bề rộng lõi - phòng trường hợp radius nhỏ khiến core*mult ra số cực nhỏ, gần như vô hình
#define QI_CORE_THICK_MULT 1.4f  // Độ dày dọc hướng bay hơi nhỉnh hơn thân khói -> lõi có "khối" rõ ràng thay vì chỉ là 1 lát cắt mỏng dính
#define QI_CORE_LIFE_MULT 0.55f  // Lõi tắt nhanh hơn thân khói (năng lượng "cháy" rồi tan trước khi khói tan)
#define QI_CORE_WHITEN 0.8f      // Trộn màu nguyên tố với WHITE nhiều hơn (trước 0.55f) -> lõi trắng sáng rực rõ ràng, dễ vượt bloomThreshold, viền vẫn phảng phất màu nguyên tố qua gradient
#define QI_BASE_LIGHT_LIFE 0.14f // Ánh sáng nền refresh liên tục mỗi frame, đời ngắn (pattern "re-spawn mỗi frame")
#define QI_BASE_LIGHT_RADIUS_MULT 0.55f
#define QI_BASE_LIGHT_PULSE_SPEED 2.6f
#define QI_BASE_LIGHT_PULSE_AMOUNT 0.18f

// Macro hỗ trợ random nhanh và sạch code
#define RAND_FLOAT() ((float)GetRandomValue(0, 1000) / 1000.0f)
#define RAND_RANGE_F(min, max) ((min) + RAND_FLOAT() * ((max) - (min)))

static ForceField s_qiRiseFld;
static ColorGradient s_qiGrad;     // Gradient thân khói mờ (bao ngoài, BLEND_ALPHA)
static ColorGradient s_qiCoreGrad; // Gradient lõi năng lượng sáng (bên trong, BLEND_ADDITIVE)
static Texture2D s_qiWispTex;
static bool s_qiInit = false;

// Đếm số "cột khí" (1 cột = 1 trail thân khói ngoài, có thể kèm hoặc không kèm lõi sáng) đang
// sống, dùng làm ngân sách cứng QI_MAX_CONCURRENT_COLUMNS ở trên. Đây là bộ đếm dùng chung toàn
// engine cho hiệu ứng chân khí này (giống các pool tĩnh khác trong CORE_API - không có slot
// riêng theo từng nhân vật), nên nếu nhiều nhân vật cùng bật aura một lúc, họ sẽ chia sẻ chung
// ngân sách 6 cột thay vì mỗi người 6 cột riêng.
static int s_qiActiveColumns = 0;

static void QiColumnOnDeath(Vector3 pos, float scale)
{
    (void)pos;
    (void)scale;
    if (s_qiActiveColumns > 0)
        s_qiActiveColumns--;
}

static void QiAura_LazyInit(void)
{
    if (s_qiInit)
        return;

    s_qiInit = true;

    s_qiWispTex = ResourceManager_LoadTexture("assets/textures/qi_wisp_soft.png");

    ForceField_Clear(&s_qiRiseFld);

    // Lực bốc lên (Rise) - Giảm sức mạnh để khí không bay vút lên cao
    ForceField_AddLayer(&s_qiRiseFld, (ForceLayer){
                                          .type = FORCE_GRAVITY_DIR,
                                          .direction = (Vector3){0.0f, 1.0f, 0.0f},
                                          .strength = 0.35f, // Giảm từ 0.92f
                                      });

    // Nhiễu cuộn tạo độ mềm mại (Slow elegant curl) - giảm strength/noiseSpeed so với bản trước
    // (1.0/2.0 -> 0.8/1.6) để chuyển động lượn nhẹ nhàng, "cân bằng" giữa mềm mại và ngẫu nhiên,
    // tránh cảm giác rung giật/hỗn loạn khi kết hợp với vòng xoáy có chủ đích ở vị trí spawn.
    ForceField_AddLayer(&s_qiRiseFld, (ForceLayer){
                                          .type = FORCE_NOISE_CURL,
                                          .strength = 0.8,
                                          .noiseScale = 2.0f,
                                          .noiseSpeed = 1.6f,
                                      });

    // Giữ các luồng khí không bị rời rạc (Keep strands coherent)
    ForceField_AddLayer(&s_qiRiseFld, (ForceLayer){
                                          .type = FORCE_VISCOSITY,
                                          .strength = 0.65f, // Tăng nhẹ độ nhớt để khí tụ lại gần nhau hơn
                                      });

    // Thân khói mờ bao ngoài (glowing smoke) - giữ mềm, alpha vừa phải (sẽ được nhân với màu nguyên tố)
    s_qiGrad.count = 0;
    ColorGradient_AddStop(&s_qiGrad, 0.00f, (Color){255, 255, 255, 0});
    ColorGradient_AddStop(&s_qiGrad, 0.15f, (Color){255, 255, 255, 120});
    ColorGradient_AddStop(&s_qiGrad, 0.50f, (Color){255, 255, 255, 160});
    ColorGradient_AddStop(&s_qiGrad, 0.85f, (Color){255, 255, 255, 60});
    ColorGradient_AddStop(&s_qiGrad, 1.00f, (Color){255, 255, 255, 0});

    // Lõi năng lượng sáng bên trong (hot core) - dùng màu trắng tinh để tạo lõi trắng nóng
    s_qiCoreGrad.count = 0;
    ColorGradient_AddStop(&s_qiCoreGrad, 0.00f, (Color){255, 255, 255, 0});
    ColorGradient_AddStop(&s_qiCoreGrad, 0.06f, (Color){255, 255, 255, 255});
    ColorGradient_AddStop(&s_qiCoreGrad, 0.40f, (Color){255, 255, 255, 220});
    ColorGradient_AddStop(&s_qiCoreGrad, 0.75f, (Color){255, 255, 255, 100});
    ColorGradient_AddStop(&s_qiCoreGrad, 1.00f, (Color){255, 255, 255, 0});
}

static void QiWispSparkle(int trailId, float dt)
{
    TrailEntity *t = GetTrail(trailId);

    if (!t)
        return;

    if (RAND_FLOAT() >= QI_SPARKLE_CHANCE_PER_SEC * dt)
        return;

    Vector3 p = t->position;
    p.x += RAND_RANGE_F(-0.02f, 0.02f) * t->scale;
    p.z += RAND_RANGE_F(-0.02f, 0.02f) * t->scale;

    Color hot = {255, 255, 255, 220}; // Tia lấp lánh trắng nóng, đủ sáng để ăn bloom

    VFXLight_Spawn(
        p,
        hot,
        0.15f * t->scale,
        0.08f,
        VFX_PRIORITY_LOW);

    SpawnParticle((ParticleConfig){
        .position = p,
        .velocity = (Vector3){
            RAND_RANGE_F(-0.03f, 0.03f),
            0.05f + RAND_RANGE_F(0.0f, 0.03f),
            RAND_RANGE_F(-0.03f, 0.03f)},
        .colorStart = hot,
        .colorEnd = (Color){255, 255, 255, 0},
        .radius = 0.006f * t->scale, // Thu nhỏ hạt lấp lánh
        .lifetime = 0.08f,
    });
}

void VFX_ComposeAura(AuraStyle style, Vector3 pos, float radius, float time)
{
    QiAura_LazyInit();

    Color color = {210, 235, 255, 255};

    switch (style)
    {
    case AURA_FIRE:
        color = (Color){255, 100, 40, 255};
        break;
    case AURA_ICE:
        color = (Color){170, 230, 255, 255};
        break;
    case AURA_WIND:
        color = (Color){120, 240, 170, 255};
        break;
    case AURA_LIGHTNING:
        color = (Color){185, 130, 255, 255};
        break;
    case AURA_TAIJI:
        color = (Color){245, 235, 200, 255};
        break;
    case AURA_QI:
        color = (Color){210, 235, 255, 255};
        break;
    }

    // Quầng sáng nền chiếu thật xuống chân nhân vật - refresh MỖI FRAME, độc lập với tỉ lệ
    // sinh hạt bên dưới, để luôn có ánh sáng thật (không chỉ là hạt) kể cả những frame không
    // sinh cột khí mới. Lifetime ngắn + priority LOW nên tự hết và không phá vỡ pool 16 slot.
    float pulse = 1.0f + QI_BASE_LIGHT_PULSE_AMOUNT * sinf(time * QI_BASE_LIGHT_PULSE_SPEED);
    VFXLight_Spawn(
        (Vector3){pos.x, pos.y + 0.05f, pos.z},
        ColorAlpha(ColorLerp(color, WHITE, 0.3f), 1.0f),
        radius * QI_BASE_LIGHT_RADIUS_MULT * pulse,
        QI_BASE_LIGHT_LIFE,
        VFX_PRIORITY_LOW);

    if (GetRandomValue(0, 999) >= QI_SPAWN_CHANCE_PERMILLE)
        return;

    // Ngân sách cứng: nếu sân khấu đã đủ 6 cột đang sống thì bỏ qua lần này, đợi có cột nào đó
    // tắt (QiColumnOnDeath giảm counter) rồi mới nhường chỗ - tránh "bùng phát" số lượng khi
    // random liên tiếp trúng nhiều lần, giữ mật độ luôn ổn định dù framerate thế nào.
    if (s_qiActiveColumns >= QI_MAX_CONCURRENT_COLUMNS)
        return;

    // Vòng xoáy có chủ đích: baseAngle quay đều theo thời gian, chỉ jitter NHẸ quanh nó thay vì
    // random toàn vòng tròn - nhờ vậy mắt thấy rõ các cột đang xoáy theo nhau một cách nhịp nhàng,
    // thay vì bật ra ở vị trí ngẫu nhiên bất kỳ mỗi lần.
    float baseAngle = time * 2.5f;
    float angle = baseAngle + RAND_RANGE_F(-QI_SWIRL_JITTER_RAD, QI_SWIRL_JITTER_RAD);

    // Thu hẹp vòng phát hạt để bám sát nhân vật hơn
    float pulsingRadius = radius * (0.85f + 0.10f * sinf(time * 3.0f));
    float ring = pulsingRadius * RAND_RANGE_F(0.2, 0.5);

    Vector3 spawnPos = {
        pos.x + cosf(angle) * ring,
        pos.y + 0.015f,
        pos.z + sinf(angle) * ring};

    // Vận tốc bốc lên rất thấp
    float rise = RAND_RANGE_F(0.15f, 0.28f);
    float life = RAND_RANGE_F(QI_COLUMN_LIFE_MIN, QI_COLUMN_LIFE_MAX);
    Vector3 vel = (Vector3){
        RAND_RANGE_F(-0.01f, 0.01f),
        rise,
        RAND_RANGE_F(-0.01f, 0.01f)};

    TrailConfig cfg = {0};

    cfg.type = TRAIL_TYPE_WISP;
    cfg.pos = spawnPos;
    cfg.vel = vel;

    // Optimize dimension parameters to survive downsampling for bloom and look soft/blended
    cfg.len = radius * RAND_RANGE_F(0.3f, 0.6f);     // Beautiful vertical height (~ waist to chest height)
    cfg.thick = radius * RAND_RANGE_F(0.02f, 0.03f); // Wide enough to survive bloom and blend softly
    cfg.trailLength = 26.0f;                         // Wavy history node count for smooth flowing motion

    cfg.life = life;
    cfg.scale = 1.0f;

    cfg.tex = s_qiWispTex;
    cfg.tint = (Color){color.r, color.g, color.b, 100}; // Soft smoke body tint
    cfg.gradient = &s_qiGrad;

    // Outer smoke uses BLEND_ALPHA for smooth volumetric blending
    cfg.blendMode = BLEND_ALPHA;

    cfg.forceField = &s_qiRiseFld;
    cfg.onUpdate = QiWispSparkle;
    cfg.onDeath = QiColumnOnDeath;
    cfg.priority = VFX_PRIORITY_LOW;

    SpawnTrailEntity(cfg);
    s_qiActiveColumns++;

    // --- Lõi sáng (hot core), chỉ gắn cho ~85% số cột ---
    if (GetRandomValue(0, 999) < QI_CORE_CHANCE_PERMILLE)
    {
        // 90% white, 10% element color -> extremely bright hot core that triggers bloom
        Color coreTint = ColorLerp(color, WHITE, 0.9f);

        TrailConfig coreCfg = {0};
        coreCfg.type = TRAIL_TYPE_WISP;
        coreCfg.pos = spawnPos;
        coreCfg.vel = vel;

        coreCfg.len = cfg.len * 0.85f;     // Slightly shorter than smoke body
        coreCfg.thick = cfg.thick * 0.35f; // Thin, delicate core filament (35% of body width)
        coreCfg.trailLength = 22.0f;       // Wavy node count to match the outer smoke's curves (hòa quyện!)

        coreCfg.life = life * QI_CORE_LIFE_MULT;
        coreCfg.scale = 1.0f;

        coreCfg.tex = s_qiWispTex;
        coreCfg.tint = coreTint; // Pure brightness
        coreCfg.gradient = &s_qiCoreGrad;

        // Core uses BLEND_ADDITIVE for bright energy glow
        coreCfg.blendMode = BLEND_ADDITIVE;

        coreCfg.forceField = &s_qiRiseFld;
        coreCfg.priority = VFX_PRIORITY_LOW;

        SpawnTrailEntity(coreCfg);
    }
}
