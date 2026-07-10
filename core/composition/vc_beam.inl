void VFX_ComposeBeam(VC_MaterialId matId, Vector3 start, Vector3 end, float width, float progress, float time)
{
    float beamLen = Vector3Distance(start, end);
    if (beamLen < 0.01f)
        return;

    float currentWidth = width * fminf(progress / 0.1f, 1.0f);
    Texture2D energyTex = ResourceManager_LoadTexture("assets/textures/energy_flow.png");

    const VFX_ElementMaterial *mat = VFX_Material(matId);
    Color baseCol = (mat->blendMode == BLEND_ALPHA) ? VC_WithAlpha(mat->body, 200)
                                                    : VC_WithAlpha(mat->glow, 255);
    Color innerCol = (Color){
        (unsigned char)fminf(baseCol.r + 50, 255),
        (unsigned char)fminf(baseCol.g + 50, 255),
        (unsigned char)fminf(baseCol.b + 50, 255),
        baseCol.a };

    float tiling = beamLen / 5.0f; // texel density independent of beam length

    // 3 layers on DrawRibbonEnergyField (core/ribbon_strip.h): outer shell
    // (slow scroll) / inner electric weave (faster scroll, V-flipped so it
    // visually interleaves with the outer layer without any real twisted
    // geometry) / bright untextured hot core.
    RibbonEnergyFieldLayer layers[3] = {
        { .widthRatio = 0.65f, .breatheFreq = 25.0f, .breatheAmp =  0.10f,
          .scrollSpeed = -0.4f, .uvTiling = tiling, .vFlip = false,
          .useTexture = true, .color = baseCol },
        { .widthRatio = 0.50f, .breatheFreq = 25.0f, .breatheAmp = -0.05f,
          .scrollSpeed = -0.7f, .uvTiling = tiling, .vFlip = true,
          .useTexture = true, .color = innerCol },
        { .widthRatio = 0.15f, .breatheFreq = 0.0f, .breatheAmp = 0.0f,
          .scrollSpeed = 0.0f, .uvTiling = 1.0f, .vFlip = false,
          .useTexture = false, .color = WHITE },
    };
    Vector3 points[2] = { start, end };

    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    BeginBlendMode(mat->blendMode);

    // RIBBON_WORLD_UP: axisA prefers world-up, falls back to (1,0,0) when
    // the beam is near-vertical — exactly the perp1 formula this file used
    // to hand-roll (`fabsf(dir.y)<0.99f ? cross(dir,up) : cross(dir,right)`).
    DrawRibbonEnergyField(points, 2, currentWidth, NULL, layers, 3, energyTex,
                          RIBBON_WORLD_UP, (Vector3){0, 1, 0}, (Camera3D){0}, time);

    EndBlendMode();
    rlEnableBackfaceCulling();
    rlDrawRenderBatchActive();
    rlEnableDepthMask();

    // =====================================================================
    // PARTICLES: Hạt năng lượng vỡ ra tại điểm va chạm
    // =====================================================================
    if (progress > 0.05f && GetRandomValue(0, 100) < 30)
    {
        SpawnParticle((ParticleConfig){
            .position = end,
            .velocity = (Vector3){
                ((float)rand() / (float)RAND_MAX - 0.5f) * 3.5f,
                ((float)rand() / (float)RAND_MAX - 0.2f) * 3.0f,
                ((float)rand() / (float)RAND_MAX - 0.5f) * 3.5f},
            .radius = 0.08f * width * (float)GetRandomValue(50, 150) / 100.0f,
            .lifetime = 0.35f,
            .colorStart = (Color){200, 255, 255, 255},
            .colorEnd = (Color){baseCol.r, baseCol.g, baseCol.b, 0}});
    }
}