void VFX_ComposeWaterStream(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float radius, float progress, float time)
{
    InitWaterTubeShaderIfNeeded();
    if (s_waterTubeShader.locs == NULL)
        return;

    VFXRenderScope renderScope = VFXRender_BeginDraw(
        VFX_RENDER_PASS_BODY, VFX_SURFACE_ALPHA, false);
    rlEnableBackfaceCulling();
    BeginShaderMode(s_waterTubeShader);

    SkillManager_BeginShader(s_waterTubeShader);

    SetShaderValue(s_waterTubeShader, s_waterTimeLoc, &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_waterTubeShader, s_waterViewPosLoc, &camera.position, SHADER_UNIFORM_VEC3);

    Vector3 lightDir = Vector3Negate(Environment_GetSunDirection());
    SetShaderValue(s_waterTubeShader, s_waterLightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);
    float uvLength = 2.0f;
    SetShaderValue(s_waterTubeShader, s_waterUvLengthLoc, &uvLength, SHADER_UNIFORM_FLOAT);

    rlColor4ub(255, 255, 255, 255);

    /* GIỌT NƯỚC — module riêng (core/geometry/pm_droplet.inl): mũi nhọn ở đuôi,
     * chỏm cầu ở đầu, tự khép nên không có nắp nón. Đây là hình hiệu ứng này
     * vẫn luôn ĐỊNH là vẽ; trước 04/08/2026 nó chỉ nhận được một thấu kính đối
     * xứng có đầu bút chì, vì một đường bao duy nhất phục vụ mọi consumer. */
    PMDropletMesh mesh;
    PMDropletConfig config = PMDroplet_DefaultConfig();
    PMDroplet_BuildBezier(&mesh, p0, p1, p2, p3, radius,
                          progress, time, 24, 16, &config);
    PMDroplet_Draw(&mesh, 2.0f);

    SkillManager_EndShader();
    EndShaderMode();
    rlDisableBackfaceCulling();
    VFXRender_EndDraw(&renderScope);
}

void VFX_BeginWaterStreams(float time)
{
    InitWaterTubeShaderIfNeeded();
    if (s_waterTubeShader.locs == NULL)
        return;

    s_waterStreamRenderScope = VFXRender_BeginDraw(
        VFX_RENDER_PASS_BODY, VFX_SURFACE_ALPHA, false);
    rlEnableBackfaceCulling();
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

    PMDropletMesh mesh;
    PMDropletConfig config = PMDroplet_DefaultConfig();

    PMDroplet_BuildAlongPath(&mesh, pathPoints, pathCount, radius,
                             clampedStartT, clampedEndT, individualTime,
                             24, 16, &config);
    PMDroplet_Draw(&mesh, 2.0f);
}

void VFX_EndWaterStreams(void)
{
    if (s_waterTubeShader.locs == NULL)
        return;

    SkillManager_EndShader();
    EndShaderMode();
    rlDisableBackfaceCulling();
    VFXRender_EndDraw(&s_waterStreamRenderScope);
}

void VFX_ComposeWaterStreamOnPath(const Vector3 *pathPoints, int pathCount, float radius, float progress, float segmentLengthRatio, float time)
{
    VFX_BeginWaterStreams(time);
    VFX_DrawWaterStreamOnPath(pathPoints, pathCount, radius, progress, segmentLengthRatio, time, 0.0f);
    VFX_EndWaterStreams();
}
