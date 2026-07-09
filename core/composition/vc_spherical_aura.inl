void VFX_ComposeSphericalAura(VC_MaterialId matId, Vector3 pos, float radius, float scrollSpeed)
{
    if (radius <= 0.0f)
        return;

    const VFX_ElementMaterial *mat = VFX_Material(matId);

    static Shader s_sh = {0};
    static int s_uBody = -1;
    static int s_uGlow = -1;
    static int s_uOpacity = -1;
    static int s_uScroll = -1;
    static int s_uNoise = -1;
    static int s_uTime = -1;
    static bool s_init = false;

    if (!s_init)
    {
        s_sh = ResourceManager_LoadShader("core/shaders/spherical_aura.vs",
                                          "core/shaders/spherical_aura.fs");
        s_uBody = GetShaderLocation(s_sh, "u_bodyColor");
        s_uGlow = GetShaderLocation(s_sh, "u_glowColor");
        s_uOpacity = GetShaderLocation(s_sh, "u_opacity");
        s_uScroll = GetShaderLocation(s_sh, "u_scrollSpeed");
        s_uNoise = GetShaderLocation(s_sh, "u_noiseScale");
        s_uTime = GetShaderLocation(s_sh, "u_time");
        s_init = true;
    }

    rlDrawRenderBatchActive();
    SkillManager_BeginShader(s_sh);

    float t = (float)GetTime();
    if (s_uTime >= 0)
        SetShaderValue(s_sh, s_uTime, &t, SHADER_UNIFORM_FLOAT);

    // Chuẩn hóa màu sắc lấy từ Material ID hệ thống
    Vector4 body = ColorNormalize(VC_WithAlpha(mat->body, 200));
    Vector4 glow = ColorNormalize(mat->glow);
    if (s_uBody >= 0)
        SetShaderValue(s_sh, s_uBody, &body, SHADER_UNIFORM_VEC4);
    if (s_uGlow >= 0)
        SetShaderValue(s_sh, s_uGlow, &glow, SHADER_UNIFORM_VEC4);

    // Cấu hình các hằng số tinh chỉnh tương đồng với Plasma Shell [cite: 91, 92]
    float opacity = 0.85f;
    float scroll = scrollSpeed; // Giá trị khuyên dùng: 0.45f [cite: 91]
    float noise = 3.50f;        // Tần số wisp bao quanh khối cầu (Scale từ 2.5 đến 4.0 là đẹp nhất) [cite: 104]
    float fresnelPower = 2.50f; // Độ sắc nét của viền hào quang [cite: 92]

    if (s_uOpacity >= 0)
        SetShaderValue(s_sh, s_uOpacity, &opacity, SHADER_UNIFORM_FLOAT);
    if (s_uScroll >= 0)
        SetShaderValue(s_sh, s_uScroll, &scroll, SHADER_UNIFORM_FLOAT);
    if (s_uNoise >= 0)
        SetShaderValue(s_sh, s_uNoise, &noise, SHADER_UNIFORM_FLOAT);

    // Set thêm u_fresnelPower động (Shader cũ sử dụng giá trị cứng)
    int fLoc = GetShaderLocation(s_sh, "u_fresnelPower");
    if (fLoc >= 0)
        SetShaderValue(s_sh, fLoc, &fresnelPower, SHADER_UNIFORM_FLOAT);

    // Kích hoạt cơ chế vẽ đè phát sáng (Additive Blend)
    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    rlColor4ub(255, 255, 255, 255);

    // Tiến hành vẽ khối cầu thủ tục
    DrawCoreSphere(pos, radius, 20, 20, WHITE);

    // ---> [QUAN TRỌNG NHẤT]: Phải đẩy hàng đợi lên GPU vẽ ngay lập tức!
    // Nếu thiếu dòng này, các lệnh rlEnable bên dưới sẽ áp ngược lên khối cầu gây lỗi tàng hình.
    rlDrawRenderBatchActive();

    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();
    SkillManager_EndShader();
}