// scrollSpeed > 0 = energy radiates outward, < 0 = energy pulls inward.
void VFX_ComposeGroundAura(VC_MaterialId matId, Vector3 pos, float radius, float scrollSpeed, float time)
{
    const VFX_ElementMaterial *mat = VFX_Material(matId);
    float dt = GetFrameTime();

    // ─────────────────────────────────────────────────────────────────────────
    // LAYER 1: GROUND ENERGY DISC (ground_aura shader)
    // Flat UV-mapped quad; the FS computes a radial mask + FBM wisps in polar
    // coords so the corners clip away and the result reads as a glowing disc.
    // ─────────────────────────────────────────────────────────────────────────
    static Shader s_sh        = {0};
    static int    s_uBody     = -1;
    static int    s_uGlow     = -1;
    static int    s_uOpacity  = -1;
    static int    s_uScroll   = -1;
    static int    s_uNoise    = -1;
    static int    s_uTime     = -1;
    static bool   s_init      = false;
    if (!s_init)
    {
        s_sh       = ResourceManager_LoadShader("core/shaders/ground_aura.vs",
                                                "core/shaders/ground_aura.fs");
        s_uBody    = GetShaderLocation(s_sh, "u_bodyColor");
        s_uGlow    = GetShaderLocation(s_sh, "u_glowColor");
        s_uOpacity = GetShaderLocation(s_sh, "u_opacity");
        s_uScroll  = GetShaderLocation(s_sh, "u_scrollSpeed");
        s_uNoise   = GetShaderLocation(s_sh, "u_noiseScale");
        s_uTime    = GetShaderLocation(s_sh, "u_time");
        s_init     = true;
    }

    // Element colors updated every frame so matId changes reflect immediately.
    rlDrawRenderBatchActive();
    SkillManager_BeginShader(s_sh);

    float t = (float)GetTime();
    if (s_uTime    >= 0) SetShaderValue(s_sh, s_uTime,    &t,       SHADER_UNIFORM_FLOAT);

    Vector4 body = ColorNormalize(VC_WithAlpha(mat->body, 220));
    Vector4 glow = ColorNormalize(mat->glow);
    if (s_uBody    >= 0) SetShaderValue(s_sh, s_uBody,    &body,    SHADER_UNIFORM_VEC4);
    if (s_uGlow    >= 0) SetShaderValue(s_sh, s_uGlow,    &glow,    SHADER_UNIFORM_VEC4);

    float opacity = 0.85f;
    float scroll  = scrollSpeed;
    float noise   = 3.2f;
    if (s_uOpacity >= 0) SetShaderValue(s_sh, s_uOpacity, &opacity, SHADER_UNIFORM_FLOAT);
    if (s_uScroll  >= 0) SetShaderValue(s_sh, s_uScroll,  &scroll,  SHADER_UNIFORM_FLOAT);
    if (s_uNoise   >= 0) SetShaderValue(s_sh, s_uNoise,   &noise,   SHADER_UNIFORM_FLOAT);

    // UV-mapped square quad on the ground — FS masks it to a circle via smoothstep.
    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();

    float hw = radius;
    float y  = pos.y + 0.018f; // slight offset above ground to avoid z-fighting
    rlSetTexture(0);
    rlBegin(RL_QUADS);
        rlTexCoord2f(0.0f, 0.0f); rlVertex3f(pos.x - hw, y, pos.z - hw);
        rlTexCoord2f(0.0f, 1.0f); rlVertex3f(pos.x - hw, y, pos.z + hw);
        rlTexCoord2f(1.0f, 1.0f); rlVertex3f(pos.x + hw, y, pos.z + hw);
        rlTexCoord2f(1.0f, 0.0f); rlVertex3f(pos.x + hw, y, pos.z - hw);
    rlEnd();
    rlDrawRenderBatchActive();

    rlEnableDepthMask();
    EndBlendMode();
    SkillManager_EndShader();

    // ─────────────────────────────────────────────────────────────────────────
    // LAYER 2: EDGE SPARKS — particles flickering at the ring perimeter
    // ─────────────────────────────────────────────────────────────────────────
    if (Random01() < (14.0f * dt))
    {
        float theta = Random01() * 2.0f * PI;
        float edgeR = radius * (0.82f + Random01() * 0.16f);
        Vector3 spawnP = {
            pos.x + cosf(theta) * edgeR,
            pos.y + 0.02f,
            pos.z + sinf(theta) * edgeR
        };
        SpawnParticle((ParticleConfig){
            .position   = spawnP,
            .velocity   = {(Random01() - 0.5f) * 0.4f,
                           0.3f + Random01() * 0.6f,
                           (Random01() - 0.5f) * 0.4f},
            .colorStart = VC_WithAlpha(mat->glow, 200),
            .colorEnd   = VC_WithAlpha(mat->glow, 0),
            .radius     = 0.012f + Random01() * 0.014f,
            .lifetime   = 0.25f + Random01() * 0.35f,
        });
    }

    // ─────────────────────────────────────────────────────────────────────────
    // LAYER 3: AMBIENT LIGHT pulse at disc center
    // ─────────────────────────────────────────────────────────────────────────
    if (Random01() < (5.0f * dt))
    {
        VFXLight_Spawn(Vector3Add(pos, (Vector3){0, 0.1f, 0}),
                       mat->glow, radius * 2.2f, 0.22f, VFX_PRIORITY_LOW);
    }
}
