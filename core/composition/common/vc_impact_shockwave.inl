// ── Impact shockwave: irregular free-space shell, NOT a ground wave ─────────

#define IMPACT_SHOCKWAVE_RADIALS 7

typedef struct
{
    Shader shader;
    int bodyColor;
    int glowColor;
    int opacity;
    int emission;
} ImpactShockwaveShader;

static ImpactShockwaveShader s_impactShockShader = {0};

static int ImpactShockwave_Slices(void)
{
    switch (GfxQuality_Get()) {
    case GFX_HIGH: return 64;
    case GFX_MED:  return 40;
    default:       return 24;
    }
}

static void ImpactShockwave_InitShader(void)
{
    if (s_impactShockShader.shader.id != 0) return;
    s_impactShockShader.shader = ResourceManager_LoadShader("core/shaders/impact_shockwave.vs",
                                                             "core/shaders/impact_shockwave.fs");
    if (s_impactShockShader.shader.id == 0) return;
    s_impactShockShader.bodyColor = GetShaderLocation(s_impactShockShader.shader, "u_bodyColor");
    s_impactShockShader.glowColor = GetShaderLocation(s_impactShockShader.shader, "u_glowColor");
    s_impactShockShader.opacity = GetShaderLocation(s_impactShockShader.shader, "u_opacity");
    s_impactShockShader.emission = GetShaderLocation(s_impactShockShader.shader, "u_emission");
}

static bool ImpactShockwave_HasShader(void)
{
    return s_impactShockShader.shader.id != 0 && s_impactShockShader.bodyColor >= 0 &&
           s_impactShockShader.glowColor >= 0 && s_impactShockShader.opacity >= 0 &&
           s_impactShockShader.emission >= 0;
}

static float ImpactShockwave_Radius01(float t01)
{
    if (t01 <= 0.0f) return 0.0f;
    if (t01 >= 1.0f) return 1.0f;
    return 1.0f - powf(1.0f - t01, 2.8f);
}

static float ImpactShockwave_Alpha01(float t01)
{
    if (t01 <= 0.0f || t01 >= 1.0f) return 0.0f;
    return SmoothStep01(t01 / 0.055f) * powf(1.0f - t01, 1.35f);
}

static void ImpactShockwave_SetUniforms(const VFX_ElementMaterial *mat,
                                        float opacity, float emission)
{
    Vector4 body = ColorNormalize(mat->body);
    Vector4 glow = ColorNormalize(VC_Whiten(mat->glow, 0.38f));
    SetShaderValue(s_impactShockShader.shader, s_impactShockShader.bodyColor,
                   &body, SHADER_UNIFORM_VEC4);
    SetShaderValue(s_impactShockShader.shader, s_impactShockShader.glowColor,
                   &glow, SHADER_UNIFORM_VEC4);
    SetShaderValue(s_impactShockShader.shader, s_impactShockShader.opacity,
                   &opacity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_impactShockShader.shader, s_impactShockShader.emission,
                   &emission, SHADER_UNIFORM_FLOAT);
}

static void ImpactShockwave_FillFallbackColors(const VFX_ElementMaterial *mat,
                                               float opacity, Color *colors,
                                               int count)
{
    for (int i = 0; i < count; i++) {
        float u = (float)i / (float)(count - 1);
        float shell = sinf(PI * u);
        Color color = VC_MixColor(mat->body, mat->glow, shell);
        colors[i] = VC_WithAlpha(color, (unsigned char)(opacity * shell * 255.0f));
    }
}

void VFX_ComposeImpactShockwave(Vector3 center, VC_MaterialId mat,
                                float radius, float t01)
{
    ImpactShockwave_InitShader();
    if (radius <= 0.0f) radius = 2.4f;
    if (t01 <= 0.0f || t01 >= 1.0f) return;

    float alpha = ImpactShockwave_Alpha01(t01);
    if (alpha <= 0.004f) return;

    float rNow = radius * ImpactShockwave_Radius01(t01);
    float rise = SmoothStep01(t01 / 0.07f);
    ImpactShockwaveMeshConfig cfg = ProceduralMesh_DefaultImpactShockwaveConfig();
    cfg.radius = rNow;
    cfg.bandWidth = fmaxf(0.12f, rNow * 0.30f);
    // Starts as a dense pressure volume, then stretches thin and disintegrates.
    cfg.halfHeight = radius * 0.24f * rise * powf(1.0f - t01, 0.55f);
    cfg.radialJitter = cfg.bandWidth * 0.17f;
    cfg.heightJitter = cfg.halfHeight * 0.24f;
    cfg.angularLobes = 5;
    cfg.angularPhase = t01 * 1.55f;

    static ImpactShockwaveMeshData mesh;
    ProceduralMesh_BuildImpactShockwave(&mesh, center, &cfg,
                                        ImpactShockwave_Slices(),
                                        IMPACT_SHOCKWAVE_RADIALS - 1);
    const VFX_ElementMaterial *m = VFX_Material(mat);
    static Color fallbackColors[IMPACT_SHOCKWAVE_RADIALS];
    ImpactShockwave_FillFallbackColors(m, alpha * 0.78f, fallbackColors,
                                       IMPACT_SHOCKWAVE_RADIALS);

    // The impact shell is coloured material first; its bloom is a separate,
    // weaker radiance pass. It does not submit particles, a flash, a decal, or
    // screen distortion — those are independent impact primaries.
    ScreenDistort_BeginVFXBody();
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    rlDrawRenderBatchActive();
    if (ImpactShockwave_HasShader()) {
        SkillManager_BeginShader(s_impactShockShader.shader);
        ImpactShockwave_SetUniforms(m, alpha * 0.78f, 1.0f);
        ProceduralMesh_DrawImpactShockwave(&mesh, NULL);
        rlDrawRenderBatchActive();
        SkillManager_EndShader();
    } else {
        ProceduralMesh_DrawImpactShockwave(&mesh, fallbackColors);
    }
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();
    rlDrawRenderBatchActive();
    ScreenDistort_EndVFXLayer();

    ScreenDistort_BeginVFXEmission();
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    rlDrawRenderBatchActive();
    if (ImpactShockwave_HasShader()) {
        SkillManager_BeginShader(s_impactShockShader.shader);
        ImpactShockwave_SetUniforms(m, alpha * 0.42f, 1.25f);
        ProceduralMesh_DrawImpactShockwave(&mesh, NULL);
        rlDrawRenderBatchActive();
        SkillManager_EndShader();
    } else {
        ImpactShockwave_FillFallbackColors(m, alpha * 0.36f, fallbackColors,
                                           IMPACT_SHOCKWAVE_RADIALS);
        ProceduralMesh_DrawImpactShockwave(&mesh, fallbackColors);
    }
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();
    rlDrawRenderBatchActive();
    ScreenDistort_EndVFXLayer();
}
