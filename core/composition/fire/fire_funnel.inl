void VFX_ComposeFireFunnel(Vector3 pos, float bottomRadius, float topRadius, float height, float time)
{
    static EffectMaterial s_fireFunnelMat;
    static bool s_loaded = false;
    if (!s_loaded)
    {
        const VFX_ElementMaterial *mat = VFX_Material(VC_MAT_FIRE);
        EffectMaterialParams p = {
            .baseColor = mat->body,
            .emissiveIntensity = 0.6f,
            .distortionStrength = 0.3f,
            .customParam1 = 0.0f,
        };
        Material_LoadCustomShader(&s_fireFunnelMat, &p,
            "core/shaders/fire_funnel.vs",
            "core/shaders/fire_funnel.fs");
        s_loaded = true;
    }

    pos = Vector3Add(pos, (Vector3){0, height * 0.3f, 0});

    VortexFunnelConfig cfg = {
        .topRadius = topRadius,
        .bottomRadius = bottomRadius,
        .height = height,
        .twistAmount = 0.0f,
        .ridgeCount = 0,
        .ridgeAmount = 0.0f,
    };

    VortexFunnelMeshData funnelData;
    ProceduralMesh_BuildVortexFunnel(&funnelData, pos, &cfg, 12, 8, 0.0f);

    float phase = sinf(time * 1.3f + pos.x * 0.1f + pos.z * 0.1f) * 5.0f;
    s_fireFunnelMat.params.customParam1 = phase;
    s_fireFunnelMat.params.emissiveIntensity = 0.6f + 0.15f * sinf(time * 0.7f);

    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();
    rlDisableBackfaceCulling();

    Material_Begin(s_fireFunnelMat);
    ProceduralMesh_DrawVortexFunnel(&funnelData, WHITE);
    Material_End();

    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();
}
