// VFX_ComposeSpiritOrb — Quả cầu linh hồn/tinh thần lơ lửng
//
// Continuous composition: gọi một lần mỗi frame khi orb còn active.
// Signature: VFX_ComposeSpiritOrb(VC_MaterialId matId, Vector3 pos,
//                                  float radius, float time)
//
// Cấu trúc 4 layer (từ trong ra ngoài):
//   Layer 1 — Core pulse:    Quả cầu lõi nhỏ phát sáng, thở nhịp nhàng theo VC_Breathe.
//   Layer 2 — Orbit motes:   6–9 hạt sáng nhỏ xoay quanh lõi theo helical orbit,
//                             tốc độ/bán kính khác nhau để không đọc như ring đồng nhất.
//   Layer 3 — Rising wisps:  Hạt nhỏ tản ra từ mặt cầu, bốc lên chậm rãi nhờ
//                             curl-noise + buoyancy, tự nhiên như khói thuốc lam.
//   Layer 4 — Ambient glow:  VFXLight điểm tại tâm, cường độ pulse theo time,
//                             + glint xác suất thấp mỗi frame.
//
// Anti-robotic rules tuân theo:
//   §12.2 Perpendicular Jitter  — orbit radius/phase khác nhau mỗi mote.
//   §12.3 Instance Randomization — size/lifetime/speed đều scatter.
//   §12.4 No Visual Popping     — hạt fade qua radiusCurve, không snap.
//   ForceField + motion library thay vì công thức inline tại chỗ.

void VFX_ComposeSpiritOrb(VC_MaterialId matId, Vector3 pos, float radius, float time)
{
    const VFX_ElementMaterial *mat = VFX_Material(matId);

    // ── Shared state: khởi tạo một lần, dùng mãi ────────────────────────────

    // ForceField cho wisps: curl-noise nhẹ + buoyancy nhẹ + viscosity
    // (giữ wisps trôi lơ lửng chứ không bay vọt lên)
    static ForceField s_orbWispFld = {0};
    if (s_orbWispFld.layerCount == 0)
    {
        ForceField_AddLayer(&s_orbWispFld, (ForceLayer){
            .type      = FORCE_NOISE_CURL,
            .strength  = 0.4f,
            .noiseScale = 0.45f,
            .noiseSpeed = 0.55f});
        ForceField_AddLayer(&s_orbWispFld, (ForceLayer){
            .type      = FORCE_GRAVITY_DIR,
            .direction = (Vector3){0.0f, 1.0f, 0.0f},
            .strength  = 0.18f});
        ForceField_AddLayer(&s_orbWispFld, (ForceLayer){
            .type     = FORCE_VISCOSITY,
            .strength = 2.5f});
    }

    // ForceField cho orbit motes: weak gravity-point hút vào tâm giữ quỹ đạo
    static ForceField s_orbMoteFld = {0};
    if (s_orbMoteFld.layerCount == 0)
    {
        ForceField_AddLayer(&s_orbMoteFld, (ForceLayer){
            .type     = FORCE_VISCOSITY,
            .strength = 1.2f});
    }

    // radiusCurve cho mọi hạt: fade-in → hold → fade-out (§12.4, no popping)
    static SkillCurve s_orbMoteSize = {0};
    static bool s_orbSizeCurveInit = false;
    if (!s_orbSizeCurveInit)
    {
        FloatCurve_AddStop(&s_orbMoteSize, 0.0f,  0.0f);
        FloatCurve_AddStop(&s_orbMoteSize, 0.2f,  1.0f);
        FloatCurve_AddStop(&s_orbMoteSize, 0.75f, 0.85f);
        FloatCurve_AddStop(&s_orbMoteSize, 1.0f,  0.0f);
        s_orbSizeCurveInit = true;
    }

    // alphaCurve chậm hơn cho wisps: stay transparent longer để không overshoot
    static SkillCurve s_orbWispAlpha = {0};
    static bool s_orbAlphaCurveInit = false;
    if (!s_orbAlphaCurveInit)
    {
        FloatCurve_AddStop(&s_orbWispAlpha, 0.0f,  0.0f);
        FloatCurve_AddStop(&s_orbWispAlpha, 0.30f, 1.0f);
        FloatCurve_AddStop(&s_orbWispAlpha, 0.65f, 0.7f);
        FloatCurve_AddStop(&s_orbWispAlpha, 1.0f,  0.0f);
        s_orbAlphaCurveInit = true;
    }

    // ── Layer 1: Core pulse — quả cầu lõi thở nhịp nhàng ────────────────────
    // VC_Breathe: radius *= (1 ± amp*sin(time*freq)) → lõi "thở" chứ không đứng im.
    // Spawn 1–2 hạt lớn tại tâm với lifetime ngắn để core liên tục "chớp".
    {
        float breathe = VC_Breathe(time, 2.3f, 0.22f);
        float coreR   = radius * 0.28f * breathe;
        Color coreColor = mat->glow;

        // Hạt lõi chính — hot white core, ngắn, rất sáng
        SpawnParticle((ParticleConfig){
            .position   = pos,
            .velocity   = (Vector3){0, 0, 0},
            .colorStart = (Color){
                (unsigned char)fminf(coreColor.r + 60, 255),
                (unsigned char)fminf(coreColor.g + 60, 255),
                (unsigned char)fminf(coreColor.b + 60, 255), 220},
            .colorEnd   = ColorAlpha(coreColor, 0.0f),
            .radius     = coreR,
            .lifetime   = 0.08f + Random01() * 0.04f,
            .radiusCurve = &s_orbMoteSize});

        // Hạt shell vỏ ngoài — tint màu nguyên tố, rộng hơn, dài hơn, mờ hơn
        if (GetRandomValue(0, 100) < 70)
        {
            SpawnParticle((ParticleConfig){
                .position   = pos,
                .velocity   = (Vector3){0, 0, 0},
                .colorStart = ColorAlpha(coreColor, 0.5f),
                .colorEnd   = ColorAlpha(coreColor, 0.0f),
                .radius     = radius * 0.45f * breathe,
                .lifetime   = 0.12f + Random01() * 0.06f,
                .radiusCurve = &s_orbMoteSize});
        }
    }

    // ── Layer 2: Orbit motes — hạt xoay quanh orb ────────────────────────────
    // 9 điểm orbit trên 3 vòng ở 3 góc nghiêng khác nhau (§12.3: không phải
    // 1 ring phẳng đồng nhất). Mỗi frame spawn xác suất thấp để pool không bị
    // ngập khi gọi liên tục.
    {
        // 3 băng orbit: xích đạo, nghiêng 35°, nghiêng -50°
        static const float kTilts[3] = {0.0f, 35.0f * 0.01745f, -50.0f * 0.01745f};
        static const float kSpeeds[3] = {0.9f, -0.65f, 0.45f}; // CCW / CW / slow CW

        Color moteColor = mat->soft;
        moteColor.a = 210;

        for (int band = 0; band < 3; band++)
        {
            if (GetRandomValue(0, 100) < 30) continue; // 70% chance spawn per band per frame

            // Angle theo band: offset bởi time để orbit liên tục
            float angle = time * kSpeeds[band] + (float)band * (2.0f * PI / 3.0f);
            float tilt  = kTilts[band];

            // VC_MotionOrbit: quỹ đạo tròn đều tại center
            // Nhưng orb cần orbit trên mặt phẳng nghiêng — tính tay với tilt
            float orbR = radius * (0.85f + 0.1f * sinf(time * 1.3f + (float)band));
            Vector3 motePos = {
                pos.x + cosf(angle) * orbR,
                pos.y + sinf(tilt) * sinf(angle) * orbR,
                pos.z + sinf(angle) * orbR * cosf(tilt)
            };

            // Jitter §12.2: lệch nhẹ khỏi orbit hoàn hảo
            motePos.x += (Random01() - 0.5f) * radius * 0.12f;
            motePos.y += (Random01() - 0.5f) * radius * 0.08f;
            motePos.z += (Random01() - 0.5f) * radius * 0.12f;

            // Velocity tiếp tuyến: VC_TangentXZ cho vòng phẳng, scale nhỏ
            float tangentAngle = angle + PI * 0.5f;
            float vScale = orbR * fabsf(kSpeeds[band]) * 0.5f;
            Vector3 vel = {
                -sinf(angle) * vScale,
                0.0f,
                cosf(angle) * vScale * cosf(tilt)
            };

            // §12.3: size/lifetime scatter mỗi mote
            float sizeVar    = 0.8f + Random01() * 0.4f;
            float lifeVar    = 0.9f + Random01() * 0.5f;

            SpawnParticle((ParticleConfig){
                .position    = motePos,
                .velocity    = vel,
                .colorStart  = moteColor,
                .colorEnd    = ColorAlpha(moteColor, 0.0f),
                .radius      = radius * 0.07f * sizeVar,
                .lifetime    = lifeVar,
                .radiusCurve = &s_orbMoteSize,
                .forceField  = &s_orbMoteFld});
        }
    }

    // ── Layer 3: Rising wisps — khói/linh khí bốc ra từ mặt cầu ─────────────
    // Spawn trên bề mặt cầu, tản ra ngoài, curl-noise uốn lên. Xác suất thấp
    // để liên tục nhỏ giọt, không nổ thành burst.
    {
        Color wispColor = mat->body;

        if (GetRandomValue(0, 100) < 40)
        {
            // Điểm spawn ngẫu nhiên trên mặt cầu (uniform sphere sampling)
            float phi   = acosf(1.0f - 2.0f * Random01());
            float theta = Random01() * 2.0f * PI;
            float sp = sinf(phi);
            Vector3 dir = {sp * cosf(theta), cosf(phi), sp * sinf(theta)};

            Vector3 spawnPos = Vector3Add(pos, Vector3Scale(dir, radius * (0.9f + Random01() * 0.15f)));

            // Velocity: hướng ra ngoài khỏi tâm + upward bias nhỏ
            Vector3 vel = Vector3Add(
                Vector3Scale(dir, 0.08f + Random01() * 0.07f),
                (Vector3){0.0f, 0.05f + Random01() * 0.05f, 0.0f});

            SpawnParticle((ParticleConfig){
                .position    = spawnPos,
                .velocity    = vel,
                .colorStart  = ColorAlpha(wispColor, 0.7f),
                .colorEnd    = ColorAlpha(wispColor, 0.0f),
                .radius      = radius * (0.12f + Random01() * 0.10f),
                .lifetime    = 1.4f + Random01() * 0.8f,
                .radiusCurve = &s_orbMoteSize,
                .alphaCurve  = &s_orbWispAlpha,
                .forceField  = &s_orbWispFld});
        }

        // Wisp tối/khói phụ — desaturated, alpha thấp, cung cấp cảm giác volume
        if (GetRandomValue(0, 100) < 15)
        {
            float phi   = acosf(1.0f - 2.0f * Random01());
            float theta = Random01() * 2.0f * PI;
            float sp = sinf(phi);
            Vector3 dir = {sp * cosf(theta), fabsf(cosf(phi)), sp * sinf(theta)};

            Vector3 spawnPos = Vector3Add(pos, Vector3Scale(dir, radius * (1.0f + Random01() * 0.2f)));

            Color smoke = {
                (unsigned char)(wispColor.r / 3 + 55),
                (unsigned char)(wispColor.g / 3 + 50),
                (unsigned char)(wispColor.b / 3 + 60),
                55};

            SpawnParticle((ParticleConfig){
                .position   = spawnPos,
                .velocity   = (Vector3){
                    (Random01() - 0.5f) * 0.06f,
                    0.04f + Random01() * 0.04f,
                    (Random01() - 0.5f) * 0.06f},
                .colorStart = smoke,
                .colorEnd   = ColorAlpha(smoke, 0.0f),
                .radius     = radius * (0.20f + Random01() * 0.12f),
                .lifetime   = 2.0f + Random01() * 1.0f,
                .radiusCurve = &s_orbMoteSize,
                .forceField  = &s_orbWispFld});
        }
    }

    // ── Layer 4: Ambient glow + occasional sparkle ────────────────────────────
    // VFXLight: radius thở theo time để ánh sáng pulsate.
    // Không spawn mỗi frame — xác suất 15%/frame để tránh ngập pool (MAX 16 lights).
    {
        float breathe    = VC_Breathe(time, 1.8f, 0.30f);
        float lightRadius = radius * 2.0f * breathe;

        if (GetRandomValue(0, 100) < 15)
            VFXLight_Spawn(pos, mat->soft, lightRadius, 0.18f, VFX_PRIORITY_LOW);

        // Glint bùng sáng thỉnh thoảng — "đốm sáng linh hồn" (5% per frame)
        if (GetRandomValue(0, 1000) < 50)
        {
            float glintAngle  = Random01() * 2.0f * PI;
            float glintHeight = (Random01() - 0.3f) * radius;
            Vector3 glintPos  = {
                pos.x + cosf(glintAngle) * radius * (0.6f + Random01() * 0.5f),
                pos.y + glintHeight,
                pos.z + sinf(glintAngle) * radius * (0.6f + Random01() * 0.5f)};
            VFX_ComposeGlintBurst(glintPos, 3 + GetRandomValue(0, 3), radius * 0.15f, mat->glow);
        }
    }
}
