void VFX_ComposeTaijiArcStrike(Vector3 pos, float scale)
{
    TaijiFx_InitShared();

    const VFX_ElementMaterial *mat = VFX_Material(VC_MAT_TAIJI);

    // ── Palette riêng — yin tím sâu / yang trắng-vàng / ion xanh-trắng ──────

    static ColorGradient s_yinArcGrad = {0};  // tia âm: tím → violet → transparent
    static ColorGradient s_yangArcGrad = {0}; // tia dương: vàng-trắng → fade
    static ColorGradient s_ionGrad = {0};     // burst trung tâm: white-hot → tím
    static ColorGradient s_staticGrad = {0};  // residual static: tím nhạt drift
    static bool s_arcGradInit = false;
    if (!s_arcGradInit)
    {
        // Yin arc: tím đậm → violet sáng → transparent
        ColorGradient_AddStop(&s_yinArcGrad, 0.0f, (Color){200, 140, 255, 255}); // head: violet hot
        ColorGradient_AddStop(&s_yinArcGrad, 0.3f, (Color){130, 50, 200, 200});  // thân: tím
        ColorGradient_AddStop(&s_yinArcGrad, 0.75f, (Color){60, 15, 100, 100});  // dim
        ColorGradient_AddStop(&s_yinArcGrad, 1.0f, (Color){20, 5, 40, 0});       // tail

        // Yang arc: vàng-trắng → kem → transparent
        ColorGradient_AddStop(&s_yangArcGrad, 0.0f, (Color){255, 255, 200, 255}); // head: white-gold
        ColorGradient_AddStop(&s_yangArcGrad, 0.3f, (Color){255, 210, 80, 210});  // vàng
        ColorGradient_AddStop(&s_yangArcGrad, 0.75f, (Color){180, 130, 30, 100});
        ColorGradient_AddStop(&s_yangArcGrad, 1.0f, (Color){80, 55, 5, 0});

        // Ion burst: white-hot → tím → fade
        ColorGradient_AddStop(&s_ionGrad, 0.0f, (Color){255, 255, 255, 255}); // white-hot
        ColorGradient_AddStop(&s_ionGrad, 0.2f, (Color){220, 180, 255, 230}); // lavender
        ColorGradient_AddStop(&s_ionGrad, 0.6f, (Color){120, 50, 200, 130});  // tím
        ColorGradient_AddStop(&s_ionGrad, 1.0f, (Color){40, 10, 80, 0});

        // Static residual: tím mờ lơ lửng
        ColorGradient_AddStop(&s_staticGrad, 0.0f, (Color){140, 80, 220, 0});
        ColorGradient_AddStop(&s_staticGrad, 0.25f, (Color){160, 100, 240, 160});
        ColorGradient_AddStop(&s_staticGrad, 0.75f, (Color){90, 40, 160, 80});
        ColorGradient_AddStop(&s_staticGrad, 1.0f, (Color){30, 10, 60, 0});

        s_arcGradInit = true;
    }

    // size/fade curves — khởi tạo một lần
    static SkillCurve s_ionSize = {0};
    static SkillCurve s_ionAlpha = {0};
    static SkillCurve s_staticDrift = {0};
    static bool s_curvesInit = false;
    if (!s_curvesInit)
    {
        // Ion burst: snap lớn ngay lập tức → shrink dần
        FloatCurve_AddStop(&s_ionSize, 0.0f, 0.2f);
        FloatCurve_AddStop(&s_ionSize, 0.12f, 1.0f);
        FloatCurve_AddStop(&s_ionSize, 0.55f, 0.6f);
        FloatCurve_AddStop(&s_ionSize, 1.0f, 0.0f);

        // Ion alpha: flash nhanh → fade chậm
        FloatCurve_AddStop(&s_ionAlpha, 0.0f, 1.0f);
        FloatCurve_AddStop(&s_ionAlpha, 0.15f, 1.0f);
        FloatCurve_AddStop(&s_ionAlpha, 0.6f, 0.5f);
        FloatCurve_AddStop(&s_ionAlpha, 1.0f, 0.0f);

        // Static drift: fade in → hold → fade out (ambient residual)
        FloatCurve_AddStop(&s_staticDrift, 0.0f, 0.0f);
        FloatCurve_AddStop(&s_staticDrift, 0.2f, 1.0f);
        FloatCurve_AddStop(&s_staticDrift, 0.7f, 0.85f);
        FloatCurve_AddStop(&s_staticDrift, 1.0f, 0.0f);

        s_curvesInit = true;
    }

    // ForceField hút vào tâm cho tia sét (gravity-point mạnh)
    static ForceField s_arcInwardFld = {0};
    if (s_arcInwardFld.layerCount == 0)
    {
        ForceField_AddLayer(&s_arcInwardFld, (ForceLayer){
                                                 .type = FORCE_GRAVITY_POINT,
                                                 .origin = (Vector3){0, 0, 0}, // override per-call bên dưới
                                                 .strength = 8.0f,
                                                 .radius = 3.0f,
                                                 .falloff = 1.0f});
        ForceField_AddLayer(&s_arcInwardFld, (ForceLayer){
                                                 .type = FORCE_VISCOSITY,
                                                 .strength = 0.8f});
    }
    // Update origin về pos hiện tại
    s_arcInwardFld.layers[0].origin = pos;

    // ForceField cho residual static drift: curl nhẹ + buoyancy
    static ForceField s_residualFld = {0};
    if (s_residualFld.layerCount == 0)
    {
        ForceField_AddLayer(&s_residualFld, (ForceLayer){
                                                .type = FORCE_NOISE_CURL,
                                                .strength = 0.5f,
                                                .noiseScale = 3.5f,
                                                .noiseSpeed = 1.8f});
        ForceField_AddLayer(&s_residualFld, (ForceLayer){
                                                .type = FORCE_GRAVITY_DIR,
                                                .direction = (Vector3){0.0f, 1.0f, 0.0f},
                                                .strength = 0.12f});
        ForceField_AddLayer(&s_residualFld, (ForceLayer){
                                                .type = FORCE_VISCOSITY,
                                                .strength = 2.2f});
    }

    float R = scale * 1.2f; // bán kính vành taiji

    // ── Phase 1: Arc trails — N tia sét từ vành hội tụ vào tâm ──────────────
    // Yin (tím) và Yang (vàng) xen kẽ nhau. TRAIL_TYPE_PROJECTILE bay từ
    // vành vào tâm, wobble thấp để đọc như sét (không phải projectile cong).
    {
        int arcCount = 8; // 4 yin + 4 yang, đối xứng
        for (int i = 0; i < arcCount; i++)
        {
            bool isYin = (i % 2 == 0);
            // Góc đều + jitter nhỏ §12.2
            float baseAngle = ((float)i / (float)arcCount) * 2.0f * PI;
            float jitter = (Random01() - 0.5f) * 0.2f;
            float angle = baseAngle + jitter;

            // Spawn trên vành, cao hơn mặt đất một chút
            float heightVar = scale * (0.05f + Random01() * 0.2f);
            Vector3 spawnPos = {
                pos.x + cosf(angle) * R * (0.9f + Random01() * 0.2f),
                pos.y + heightVar,
                pos.z + sinf(angle) * R * (0.9f + Random01() * 0.2f)};

            // Bay thẳng vào tâm, speed §12.3 scatter
            Vector3 dir = Vector3Normalize(Vector3Subtract(pos, spawnPos));
            float speed = scale * (4.0f + Random01() * 2.5f);

            // §12.3: thickness scatter
            float thickVar = 0.75f + Random01() * 0.5f;

            TrailConfig tcfg = {0};
            tcfg.type = TRAIL_TYPE_PROJECTILE;
            tcfg.pos = spawnPos;
            tcfg.vel = Vector3Scale(dir, speed);
            tcfg.target = pos;
            tcfg.thick = 0.010f * scale * thickVar;
            tcfg.len = 0.014f * scale * thickVar;
            tcfg.trailLength = scale * (0.18f + Random01() * 0.15f);
            tcfg.life = 0.22f + Random01() * 0.12f;
            tcfg.gradient = isYin ? &s_yinArcGrad : &s_yangArcGrad;
            tcfg.widthEnvelope = TRAIL_WIDTH_ENVELOPE_TAPER_TAIL;
            tcfg.curveRangeOverride = 1.2f;               // snap nhanh về target
            tcfg.wobbleAmplitudeOverride = 0.25f * scale; // rung nhẹ — taiji điện có nhịp, không chaos
            tcfg.priority = VFX_PRIORITY_LOW;

            SpawnTrailEntity(tcfg);
        }
    }

    // ── Phase 2: Central ion burst ────────────────────────────────────────────
    // Hạt white-hot bùng ra mọi hướng từ điểm va chạm của các arc.
    {
        int ionCount = 20 + GetRandomValue(0, 8);
        for (int i = 0; i < ionCount; i++)
        {
            // Uniform sphere sampling — bùng đều mọi hướng
            float phi = acosf(1.0f - 2.0f * Random01());
            float theta = Random01() * 2.0f * PI;
            float sp = sinf(phi);
            Vector3 dir = {sp * cosf(theta), cosf(phi), sp * sinf(theta)};

            // Phần lớn bắn ra theo mặt phẳng ngang (taiji = cân bằng nằm ngang)
            float horizBias = 0.7f + Random01() * 0.3f;
            dir.y *= (1.0f - horizBias);
            dir = Vector3Normalize(dir);

            float speed = scale * (1.5f + Random01() * 2.0f);
            bool heroIon = GetRandomValue(0, 100) < 15;

            SpawnParticle((ParticleConfig){
                .position = Vector3Add(pos, Vector3Scale(dir, scale * 0.05f)),
                .velocity = Vector3Scale(dir, speed),
                .radius = scale * (heroIon ? 0.022f : 0.012f) * (0.8f + Random01() * 0.45f),
                .lifetime = heroIon ? (0.5f + Random01() * 0.3f) : (0.25f + Random01() * 0.2f),
                .gradient = &s_ionGrad,
                .radiusCurve = &s_ionSize,
                .alphaCurve = &s_ionAlpha,
                .forceField = &s_arcInwardFld}); // pull nhẹ về tâm → hạt không bay thẳng dại
        }

        // Flash core trắng: siêu ngắn, rất sáng, tâm điểm
        SpawnParticle((ParticleConfig){
            .position = pos,
            .colorStart = (Color){255, 255, 255, 250},
            .colorEnd = VC_WithAlpha(mat->body, 0),
            .radius = 0.22f * scale,
            .lifetime = 0.09f,
            .radiusCurve = &s_ionSize});

        // Halo tím ngoài: lớn hơn, sống lâu hơn một chút
        SpawnParticle((ParticleConfig){
            .position = pos,
            .colorStart = VC_WithAlpha(mat->body, 200),
            .colorEnd = VC_WithAlpha(mat->body, 0),
            .radius = 0.42f * scale,
            .lifetime = 0.18f,
            .radiusCurve = &s_ionSize});
    }

    // ── Phase 3: Residual static drift — ion tím lơ lửng tan dần ─────────────
    {
        int staticCount = 14 + GetRandomValue(0, 6);
        for (int i = 0; i < staticCount; i++)
        {
            float angle = Random01() * 2.0f * PI;
            float rr = R * (0.3f + Random01() * 0.8f);
            Vector3 spawnPos = {
                pos.x + cosf(angle) * rr,
                pos.y + scale * (0.02f + Random01() * 0.35f),
                pos.z + sinf(angle) * rr};

            SpawnParticle((ParticleConfig){
                .position = spawnPos,
                .velocity = (Vector3){
                    (Random01() - 0.5f) * 0.1f * scale,
                    0.04f + Random01() * 0.06f,
                    (Random01() - 0.5f) * 0.1f * scale},
                .radius = scale * (0.008f + Random01() * 0.008f),
                .lifetime = 1.0f + Random01() * 0.8f,
                .gradient = &s_staticGrad,
                .radiusCurve = &s_staticDrift,
                .alphaCurve = &s_staticDrift,
                .forceField = &s_residualFld});
        }
    }

    // ── Phase 4: Decals + lights ───────────────────────────────────────────────
    {
        // Vòng rune taiji xoay dưới đất — dùng runeDecal của material TAIJI
        Texture2D runeTex = ResourceManager_LoadTexture(mat->runeDecal);

        // Outer ring: xoay thuận chiều
        DecalSystem_AddEx(pos, 0.0f, 45.0f,
                          R * 0.6f, R * 1.5f,
                          runeTex, 0.6f, ColorAlpha(mat->body, 180),
                          BLEND_ADDITIVE, 0.02f);

        // Inner ring: xoay ngược (counter-spin = taiji signature)
        DecalSystem_AddEx(pos, 180.0f, -70.0f,
                          R * 0.3f, R * 0.9f,
                          runeTex, 0.5f, ColorAlpha(mat->glow, 140),
                          BLEND_ADDITIVE, 0.025f);

        // Shockwave ring mờ — impact vật lý của burst
        VFX_ComposeShockwaveRing(pos, R * 0.8f, 0.4f, mat->soft);

        // Screen distort: "bẻ cong không gian" — taiji là lực cân bằng
        ScreenDistort_Add(pos, R * 1.2f, 0.22f, 0.35f, 2.8f);

        // Light flash chính: tím nóng, ngắn
        VFXLight_Spawn(pos, mat->body, R * 2.2f, 0.25f, VFX_PRIORITY_LOW);

        // Light yang: vàng, nhỏ hơn, sống lâu hơn một chút (afterglow)
        VFXLight_Spawn(Vector3Add(pos, (Vector3){0, scale * 0.1f, 0}),
                       mat->glow, R * 1.2f, 0.45f, VFX_PRIORITY_LOW);

        // Glint burst tại tâm — "điểm nóng" taiji
        VFX_ComposeGlintBurst(pos, 10 + GetRandomValue(0, 5), R * 0.4f, mat->glow);

        // Vài glint trên vành — sparks nơi arc chạm vào
        int rimSparks = 4;
        for (int i = 0; i < rimSparks; i++)
        {
            float a = ((float)i / (float)rimSparks) * 2.0f * PI + Random01() * 0.5f;
            Vector3 rimPos = {
                pos.x + cosf(a) * R * (0.85f + Random01() * 0.2f),
                pos.y + scale * 0.05f,
                pos.z + sinf(a) * R * (0.85f + Random01() * 0.2f)};
            VFX_ComposeGlintBurst(rimPos, 3 + GetRandomValue(0, 2),
                                  R * 0.12f,
                                  (i % 2 == 0) ? mat->body : mat->glow);
        }
    }
}
