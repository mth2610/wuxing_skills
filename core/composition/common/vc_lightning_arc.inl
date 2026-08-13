// Composition-facing material adapter for Core's dedicated LightningStroke
// primitive. It owns no lightning geometry or renderer state: that all lives
// under core/lightning/ so other systems can use the same specialised stroke.

static void LightningArc_Update(float dt)
{
    LightningStroke_Update(dt);
}

static void LightningArc_Draw3D(Camera3D camera)
{
    // The coloured body survives a bright scene; glow and core are kept as
    // separate, restrained additive radiance. The stroke renderer itself only
    // submits segmented geometry inside these caller-owned render scopes.
    ScreenDistort_BeginVFXBody();
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();
    rlDrawRenderBatchActive();
    LightningStroke_DrawLayer(camera, LIGHTNING_STROKE_RENDER_BODY);
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    EndBlendMode();
    rlDrawRenderBatchActive();
    ScreenDistort_EndVFXLayer();

    ScreenDistort_BeginVFXEmission();
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();
    rlDrawRenderBatchActive();
    LightningStroke_DrawLayer(camera, LIGHTNING_STROKE_RENDER_HALO);
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    EndBlendMode();
    rlDrawRenderBatchActive();
    ScreenDistort_EndVFXLayer();
}

VFX_LightningArcConfig VFX_LightningArc_DefaultConfig(void)
{
    return (VFX_LightningArcConfig){
        .material = VC_MAT_LIGHTNING,
        .width = 0.075f,
        .lifetime = 0.40f,
        .travelDuration = 0.10f,
        .postImpactDuration = 0.30f,
        .coreEmission = 4.5f,
        .haloEmission = 0.42f,
        .jaggedness = 0.80f,
        .flickerInterval = 0.045f,
        .branchCount = 0,
        .seed = 0u
    };
}

static LightningStrokeConfig LightningArc_ToStrokeConfig(const VFX_LightningArcConfig *config)
{
    const VFX_ElementMaterial *mat = VFX_Material(config->material);
    LightningStrokeConfig stroke = LightningStroke_DefaultConfig();
    Color cobaltBlue = (Color){18, 61, 255, 255}; // deliberate electric identity
    stroke.bodyColor = VFXContrast_ApplyColor(
        VC_WithAlpha(VC_MixColor(cobaltBlue, mat->glow, 0.48f), 72),
        VFX_CONTRAST_ENERGY, VFX_CONTRAST_BODY);
    stroke.haloColor = VFXContrast_ApplyColor(
        VC_WithAlpha(VC_MixColor(cobaltBlue, mat->glow, 0.55f), 68),
        VFX_CONTRAST_ENERGY, VFX_CONTRAST_EMISSION);
    stroke.coreColor = VFXContrast_ApplyColor(
        VC_WithAlpha(VC_Whiten(mat->glow, 0.80f), 244),
        VFX_CONTRAST_ENERGY, VFX_CONTRAST_EMISSION);
    stroke.width = config->width;
    stroke.lifetime = config->lifetime;
    stroke.travelDuration = config->travelDuration;
    stroke.postImpactDuration = config->postImpactDuration;
    stroke.coreEmission = config->coreEmission;
    stroke.haloEmission = config->haloEmission;
    stroke.jaggedness = config->jaggedness;
    stroke.flickerInterval = config->flickerInterval;
    stroke.branchCount = config->branchCount;
    stroke.seed = config->seed;
    return stroke;
}

int VFX_ComposeLightningArc(Vector3 from, Vector3 to, VC_MaterialId material, float width)
{
    VFX_LightningArcConfig config = VFX_LightningArc_DefaultConfig();
    config.material = material;
    config.width = fmaxf(width, 0.075f);
    return VFX_LightningArc_Spawn(from, to, &config);
}

int VFX_LightningArc_Spawn(Vector3 from, Vector3 to, const VFX_LightningArcConfig *config)
{
    VFX_LightningArcConfig resolved = config ? *config : VFX_LightningArc_DefaultConfig();
    if (resolved.width <= 0.0f) resolved.width = 0.075f;
    if (resolved.travelDuration <= 0.0f) resolved.travelDuration = 0.10f;
    if (resolved.postImpactDuration >= 0.0f)
        resolved.lifetime = resolved.travelDuration + resolved.postImpactDuration;
    else {
        if (resolved.lifetime <= 0.0f) resolved.lifetime = 0.40f;
        if (resolved.travelDuration >= resolved.lifetime)
            resolved.travelDuration = resolved.lifetime * 0.75f;
    }
    if (resolved.coreEmission <= 0.0f) resolved.coreEmission = 4.5f;
    if (resolved.haloEmission <= 0.0f) resolved.haloEmission = 0.42f;
    if (resolved.jaggedness < 0.0f) resolved.jaggedness = 0.0f;
    LightningStrokeConfig stroke = LightningArc_ToStrokeConfig(&resolved);
    // A lightning path is visual data, not an area-light source. In particular,
    // a midpoint light makes the entire air-gap look illuminated. The owning
    // skill may instead choose short contact lights at `from` and/or `to`.
    return LightningStroke_Spawn(from, to, &stroke);
}

void VFX_LightningArc_SetEndpoints(int handle, Vector3 from, Vector3 to)
{
    LightningStroke_SetEndpoints(handle, from, to);
}

void VFX_LightningArc_Kill(int handle)
{
    LightningStroke_Kill(handle);
}
