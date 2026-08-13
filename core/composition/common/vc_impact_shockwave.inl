// ── Impact shockwave: irregular free-space shell, NOT a ground wave ─────────

#define IMPACT_SHOCKWAVE_RADIALS 7
#define IMPACT_SHOCKWAVE_LAYERS  3

typedef struct
{
    Shader shader;
    int bodyColor;
    int glowColor;
    int opacity;
    int emission;
    int detailScale;
    int layerPhase;
    int hasSmokeTexture;
} ImpactShockwaveShader;

typedef struct
{
    float radiusScale;
    float detailScale;
    float phase;
    float opacity;
} ImpactShockwaveLayer;

/* One master shader, three mesh/material instances. This is the High/Mid/Low
 * resolution stack from the reference workflow: no layer is a clean ring on
 * its own, and their misaligned boundaries form the large torn cloud. */
static const ImpactShockwaveLayer s_impactShockLayers[IMPACT_SHOCKWAVE_LAYERS] =
{
    {0.91f, 12.0f, 0.00f, 0.34f}, // HIGH: thin, detailed erosion inside
    {1.00f,  6.2f, 1.71f, 0.52f}, // MID: readable pressure body
    {1.10f,  2.8f, 3.94f, 0.26f}  // LOW: broad, soft broken boundary
};

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
    s_impactShockShader.detailScale = GetShaderLocation(s_impactShockShader.shader,
                                                         "u_detailScale");
    s_impactShockShader.layerPhase = GetShaderLocation(s_impactShockShader.shader,
                                                        "u_layerPhase");
    s_impactShockShader.hasSmokeTexture = GetShaderLocation(s_impactShockShader.shader,
                                                            "u_hasSmokeTexture");
}

static bool ImpactShockwave_HasShader(void)
{
    return s_impactShockShader.shader.id != 0 && s_impactShockShader.bodyColor >= 0 &&
           s_impactShockShader.glowColor >= 0 && s_impactShockShader.opacity >= 0 &&
           s_impactShockShader.emission >= 0 && s_impactShockShader.detailScale >= 0 &&
           s_impactShockShader.layerPhase >= 0 && s_impactShockShader.hasSmokeTexture >= 0;
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
                                        float opacity, float emission,
                                        float detailScale, float layerPhase,
                                        int hasSmokeTexture)
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
    SetShaderValue(s_impactShockShader.shader, s_impactShockShader.detailScale,
                   &detailScale, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_impactShockShader.shader, s_impactShockShader.layerPhase,
                   &layerPhase, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_impactShockShader.shader, s_impactShockShader.hasSmokeTexture,
                   &hasSmokeTexture, SHADER_UNIFORM_INT);
}

static void ImpactShockwave_FillFallbackColors(const VFX_ElementMaterial *mat,
                                               float opacity, Color *colors,
                                               int count)
{
    for (int i = 0; i < count; i++) {
        float u = (float)i / (float)(count - 1);
        float inner = SmoothStep01((u - 0.18f) / 0.24f);
        float outer = 1.0f - SmoothStep01((u - 0.76f) / 0.24f);
        float coverage = inner * outer;
        Color color = VC_MixColor(mat->body, mat->glow, coverage);
        colors[i] = VC_WithAlpha(color, (unsigned char)(opacity * coverage * 255.0f));
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
    const VFX_ElementMaterial *m = VFX_Material(mat);
    const VFX_SurfaceProfile *smokeSurface =
        VFX_SurfaceRegistry_Get(VFX_SURFACE_IMPACT_SMOKE);
    const int hasSmokeTexture = smokeSurface != NULL && smokeSurface->body.id != 0;
    static ImpactShockwaveMeshData meshes[IMPACT_SHOCKWAVE_LAYERS];
    static Color fallbackColors[IMPACT_SHOCKWAVE_LAYERS][IMPACT_SHOCKWAVE_RADIALS];
    for (int i = 0; i < IMPACT_SHOCKWAVE_LAYERS; i++) {
        const ImpactShockwaveLayer *layer = &s_impactShockLayers[i];
        ImpactShockwaveMeshConfig cfg = ProceduralMesh_DefaultImpactShockwaveConfig();
        cfg.radius = rNow * layer->radiusScale;
        // A planar pressure disc, not a raised ground lip or a spherical volume.
        // Its place in space is `center.y`; the three layers carry different
        // edge scales/phases so their combined silhouette has no clean contour.
        cfg.radialJitter = fmaxf(0.025f, cfg.radius * (0.035f + 0.010f * (float)i));
        cfg.angularLobes = 4 + i * 2;
        cfg.angularPhase = t01 * (1.15f + 0.42f * (float)i) + layer->phase;
        ProceduralMesh_BuildImpactShockwave(&meshes[i], center, &cfg,
                                            ImpactShockwave_Slices(),
                                            IMPACT_SHOCKWAVE_RADIALS - 1);
        ImpactShockwave_FillFallbackColors(m, alpha * layer->opacity,
                                           fallbackColors[i], IMPACT_SHOCKWAVE_RADIALS);
    }

    // The impact shell is coloured material first; its bloom is a separate,
    // weaker radiance pass. It does not submit particles, a flash, or a decal.
    // Refraction is a separate one-shot trigger below: submitting it here
    // would allocate a fresh screen source on every continuous draw frame.
    ScreenDistort_BeginVFXBody();
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    rlDrawRenderBatchActive();
    if (ImpactShockwave_HasShader()) {
        SkillManager_BeginShader(s_impactShockShader.shader);
        if (hasSmokeTexture)
            rlSetTexture(smokeSurface->body.id);
        for (int i = 0; i < IMPACT_SHOCKWAVE_LAYERS; i++) {
            const ImpactShockwaveLayer *layer = &s_impactShockLayers[i];
            ImpactShockwave_SetUniforms(m, alpha * layer->opacity, 1.0f,
                                        layer->detailScale, layer->phase, hasSmokeTexture);
            ProceduralMesh_DrawImpactShockwave(&meshes[i], NULL);
            rlDrawRenderBatchActive();
        }
        if (hasSmokeTexture)
            rlSetTexture(0);
        SkillManager_EndShader();
    } else {
        for (int i = 0; i < IMPACT_SHOCKWAVE_LAYERS; i++)
            ProceduralMesh_DrawImpactShockwave(&meshes[i], fallbackColors[i]);
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
        if (hasSmokeTexture)
            rlSetTexture(smokeSurface->body.id);
        for (int i = 0; i < IMPACT_SHOCKWAVE_LAYERS; i++) {
            const ImpactShockwaveLayer *layer = &s_impactShockLayers[i];
            ImpactShockwave_SetUniforms(m, alpha * layer->opacity * 0.48f, 1.18f,
                                        layer->detailScale, layer->phase, hasSmokeTexture);
            ProceduralMesh_DrawImpactShockwave(&meshes[i], NULL);
            rlDrawRenderBatchActive();
        }
        if (hasSmokeTexture)
            rlSetTexture(0);
        SkillManager_EndShader();
    } else {
        for (int i = 0; i < IMPACT_SHOCKWAVE_LAYERS; i++) {
            const ImpactShockwaveLayer *layer = &s_impactShockLayers[i];
            ImpactShockwave_FillFallbackColors(m, alpha * layer->opacity * 0.42f,
                                               fallbackColors[i], IMPACT_SHOCKWAVE_RADIALS);
            ProceduralMesh_DrawImpactShockwave(&meshes[i], fallbackColors[i]);
        }
    }
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();
    rlDrawRenderBatchActive();
    ScreenDistort_EndVFXLayer();
}

void VFX_TriggerImpactShockwaveDistortion(Vector3 center, float radius,
                                          float strength)
{
    // ScreenDistort samples the scene only during its dedicated post pass.
    // Do not sample ScreenDistort_GetSceneTexture() in impact_shockwave.fs:
    // that shader draws INTO the same target, which is a read/write hazard.
    if (GfxQuality_Get() < GFX_MED) return;
    if (radius <= 0.0f) radius = 2.4f;
    strength = fmaxf(0.0f, fminf(strength, 0.35f));
    if (strength <= 0.001f) return;
    ScreenDistort_Add(center, radius, strength, 0.18f, 1.0f);
}
