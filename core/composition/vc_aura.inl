#define QI_SPAWN_CHANCE_PERMILLE 300
#define QI_COLUMN_LIFE_MIN 0.5f
#define QI_COLUMN_LIFE_MAX 1.0f
#define QI_SPARKLE_CHANCE_PER_SEC 1.8f

#define QI_MAX_CONCURRENT_COLUMNS 6
#define QI_SWIRL_JITTER_RAD 0.55f

#define QI_BASE_LIGHT_LIFE 0.14f
#define QI_BASE_LIGHT_RADIUS_MULT 0.55f
#define QI_BASE_LIGHT_PULSE_SPEED 2.6f
#define QI_BASE_LIGHT_PULSE_AMOUNT 0.18f

#define RAND_FLOAT() ((float)GetRandomValue(0, 1000) / 1000.0f)
#define RAND_RANGE_F(min, max) ((min) + RAND_FLOAT() * ((max) - (min)))

static ForceField s_qiRiseFld;
static ColorGradient s_qiGrad;
static Texture2D s_qiWispTex;
static bool s_qiInit = false;
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

    // Xóa các lực cũ (nếu có) trước khi setup lại
    ForceField_Clear(&s_qiRiseFld);

    // 1. LỰC BỐC LÊN (Directional Gravity)
    // Đẩy luồng khí bay thẳng lên trên theo trục Y.
    // Tăng strength nếu muốn khí bốc lên mãnh liệt hơn.
    ForceField_AddLayer(&s_qiRiseFld, (ForceLayer){
                                          .type = FORCE_GRAVITY_DIR,
                                          .direction = (Vector3){0.0f, 3.0f, 0.0f},
                                          .strength = 0.35f,
                                      });

    // 2. NHIỄU CUỘN (Curl Noise)
    // Tạo chuyển động lượn lờ, uốn éo một cách vô trật tự cho khí.
    // - strength: Độ giật/đẩy của nhiễu.
    // - noiseScale: Kích thước của vòng xoắn (scale to thì xoắn lớn, nhỏ thì giật vụn).
    // - noiseSpeed: Tốc độ biến đổi hình dạng của nhiễu theo thời gian.
    ForceField_AddLayer(&s_qiRiseFld, (ForceLayer){
                                          .type = FORCE_NOISE_CURL,
                                          .strength = 1.0f,
                                          .noiseScale = 0.5f,
                                          .noiseSpeed = 3.0f,
                                      });

    // 3. ĐỘ NHỚT (Viscosity)
    // Hoạt động như lực cản (drag/friction) của không khí.
    // Kéo tụ các phần tử lại với nhau, ngăn không cho lực nhiễu xé toạc luồng khí
    // bay lung tung ra mọi hướng, giúp tổng thể di chuyển mượt mà và liền mạch.
    ForceField_AddLayer(&s_qiRiseFld, (ForceLayer){
                                          .type = FORCE_VISCOSITY,
                                          .strength = 0.65f,
                                      });

    // Gradient thân khói mờ (bao ngoài, BLEND_ALPHA)
    s_qiGrad.count = 0;
    ColorGradient_AddStop(&s_qiGrad, 0.00f, (Color){255, 255, 255, 0});
    ColorGradient_AddStop(&s_qiGrad, 0.15f, (Color){255, 255, 255, 120});
    ColorGradient_AddStop(&s_qiGrad, 0.50f, (Color){255, 255, 255, 160});
    ColorGradient_AddStop(&s_qiGrad, 0.85f, (Color){255, 255, 255, 60});
    ColorGradient_AddStop(&s_qiGrad, 1.00f, (Color){255, 255, 255, 0});
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

    Color hot = {255, 255, 255, 220};

    VFXLight_Spawn(p, hot, 0.15f * t->scale, 0.08f, VFX_PRIORITY_LOW);

    SpawnParticle((ParticleConfig){
        .position = p,
        .velocity = (Vector3){0.0f, 0.05f + RAND_RANGE_F(0.0f, 0.03f), 0.0f},
        .colorStart = hot,
        .colorEnd = (Color){255, 255, 255, 0},
        .radius = 0.006f * t->scale,
        .lifetime = 0.08f,
    });
}

void VFX_ComposeAura(AuraStyle style, Vector3 pos, float radius, float time)
{
    QiAura_LazyInit();

    Color color = {0, 165, 255, 255};
    switch (style)
    {
    case AURA_FIRE:
        color = (Color){255, 60, 0, 255};
        break;
    case AURA_ICE:
        color = (Color){0, 165, 255, 255};
        break;
    case AURA_WIND:
        color = (Color){0, 230, 90, 255};
        break;
    case AURA_LIGHTNING:
        color = (Color){175, 45, 255, 255};
        break;
    case AURA_TAIJI:
        color = (Color){255, 180, 0, 255};
        break;
    case AURA_QI:
        color = (Color){0, 165, 255, 255};
        break;
    }

    if (GetRandomValue(0, 999) >= QI_SPAWN_CHANCE_PERMILLE)
        return;

    if (s_qiActiveColumns >= QI_MAX_CONCURRENT_COLUMNS)
        return;

    float baseAngle = time * 2.5f;
    float angle = baseAngle + RAND_RANGE_F(-QI_SWIRL_JITTER_RAD, QI_SWIRL_JITTER_RAD);

    float pulsingRadius = radius * (0.85f + 0.10f * sinf(time * 3.0f));
    float ring = pulsingRadius * RAND_RANGE_F(0.2f, 0.5f);

    Vector3 spawnPos = {
        pos.x + cosf(angle) * ring,
        pos.y + 0.015f,
        pos.z + sinf(angle) * ring};

    TrailConfig cfg = {0};

    // Thay đổi type này nếu engine hỗ trợ TRAIL_TYPE_NORMAL để loại bỏ sự uốn lượn hardcode
    cfg.type = TRAIL_TYPE_WISP;
    cfg.pos = spawnPos;
    cfg.vel = (Vector3){0.0f, RAND_RANGE_F(0.15f, 0.28f), 0.0f};

    cfg.len = radius * RAND_RANGE_F(0.2f, 0.6f);
    cfg.thick = radius * RAND_RANGE_F(0.035f, 0.048f);
    cfg.trailLength = 26.0f;
    cfg.life = RAND_RANGE_F(QI_COLUMN_LIFE_MIN, QI_COLUMN_LIFE_MAX);
    cfg.scale = 1.0f;

    cfg.tex = (Texture2D){0};
    cfg.tint = (Color){color.r, color.g, color.b, 255};
    cfg.gradient = &s_qiGrad;
    cfg.blendMode = BLEND_ADDITIVE;

    cfg.forceField = &s_qiRiseFld;
    cfg.onUpdate = QiWispSparkle;
    cfg.onDeath = QiColumnOnDeath;
    cfg.priority = VFX_PRIORITY_LOW;

    SpawnTrailEntity(cfg);
    s_qiActiveColumns++;
}