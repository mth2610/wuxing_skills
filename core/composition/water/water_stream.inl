void VFX_ComposeWaterStream(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float radius, float progress, float time)
{
    InitWaterTubeShaderIfNeeded();
    if (s_waterTubeShader.locs == NULL)
        return;

    ScreenDistort_BeginVFXBody();
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlEnableBackfaceCulling();
    BeginBlendMode(BLEND_ALPHA);
    BeginShaderMode(s_waterTubeShader);

    SkillManager_BeginShader(s_waterTubeShader);

    SetShaderValue(s_waterTubeShader, s_waterTimeLoc, &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_waterTubeShader, s_waterViewPosLoc, &camera.position, SHADER_UNIFORM_VEC3);

    Vector3 lightDir = Vector3Negate(Environment_GetSunDirection());
    SetShaderValue(s_waterTubeShader, s_waterLightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);
    float uvLength = 2.0f;
    SetShaderValue(s_waterTubeShader, s_waterUvLengthLoc, &uvLength, SHADER_UNIFORM_FLOAT);

    rlColor4ub(255, 255, 255, 255);

    TubeMeshData mesh;
    TubeMeshConfig config = ProceduralMesh_DefaultTubeConfig();
    ProceduralMesh_BuildTube(&mesh, p0, p1, p2, p3, radius,
                             progress, time, 24, 16, &config);
    ProceduralMesh_DrawTube(&mesh, 2.0f);

    SkillManager_EndShader();
    EndShaderMode();
    EndBlendMode();
    rlDisableBackfaceCulling();
    rlEnableDepthMask();
}

void VFX_BeginWaterStreams(float time)
{
    InitWaterTubeShaderIfNeeded();
    if (s_waterTubeShader.locs == NULL)
        return;

    ScreenDistort_BeginVFXBody();
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlEnableBackfaceCulling();
    BeginBlendMode(BLEND_ALPHA);
    BeginShaderMode(s_waterTubeShader);

    SkillManager_BeginShader(s_waterTubeShader);

    SetShaderValue(s_waterTubeShader, s_waterTimeLoc, &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_waterTubeShader, s_waterViewPosLoc, &camera.position, SHADER_UNIFORM_VEC3);

    Vector3 lightDir = Vector3Negate(Environment_GetSunDirection());
    SetShaderValue(s_waterTubeShader, s_waterLightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);
    float uvLength = 2.0f;
    SetShaderValue(s_waterTubeShader, s_waterUvLengthLoc, &uvLength, SHADER_UNIFORM_FLOAT);

    rlColor4ub(255, 255, 255, 255);
}

void VFX_DrawWaterStreamOnPath(const Vector3 *pathPoints, int pathCount, float radius, float progress, float segmentLengthRatio, float time, float phaseOffset)
{
    if (s_waterTubeShader.locs == NULL || pathPoints == NULL || pathCount <= 0)
        return;

    float startT = progress - segmentLengthRatio;
    float endT = progress;

    float clampedStartT = startT;
    float clampedEndT = endT;
    if (clampedStartT < 0.0f) clampedStartT = 0.0f;
    if (clampedEndT > 1.0f) clampedEndT = 1.0f;
    if (clampedStartT > 1.0f) clampedStartT = 1.0f;
    if (clampedEndT < 0.0f) clampedEndT = 0.0f;

    if (clampedEndT - clampedStartT <= 0.0f)
        return;

    float individualTime = time + phaseOffset;

    TubeMeshData mesh;
    TubeMeshConfig config = ProceduralMesh_DefaultTubeConfig();

    ProceduralMesh_BuildTubeAlongPath(&mesh, pathPoints, pathCount, radius,
                                      clampedStartT, clampedEndT, individualTime,
                                      24, 16, &config);
    ProceduralMesh_DrawTube(&mesh, 2.0f);
}

void VFX_EndWaterStreams(void)
{
    if (s_waterTubeShader.locs == NULL)
        return;

    SkillManager_EndShader();
    EndShaderMode();
    EndBlendMode();
    rlDisableBackfaceCulling();
    rlEnableDepthMask();
    ScreenDistort_EndVFXLayer();
}

void VFX_ComposeWaterStreamOnPath(const Vector3 *pathPoints, int pathCount, float radius, float progress, float segmentLengthRatio, float time)
{
    VFX_BeginWaterStreams(time);
    VFX_DrawWaterStreamOnPath(pathPoints, pathCount, radius, progress, segmentLengthRatio, time, 0.0f);
    VFX_EndWaterStreams();
}
