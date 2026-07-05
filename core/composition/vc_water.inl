void VFX_ComposeMagicPuddle(Vector3 pos)
{
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();

    float radius = 1.2f;
    rlPushMatrix();
    rlTranslatef(pos.x, pos.y + 0.01f, pos.z);

    Shader flowShader = ResourceManager_LoadShader(0, "core/shaders/puddle.fs");
    Texture2D tex = ResourceManager_LoadTexture("assets/textures/water_caustics.png");
    Texture2D flowTex = ResourceManager_LoadTexture("assets/textures/water_flow.png");

    SetTextureWrap(tex, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(flowTex, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(flowTex, TEXTURE_FILTER_BILINEAR);

    int timeLoc = GetShaderLocation(flowShader, "u_time");
    int tex0Loc = GetShaderLocation(flowShader, "causticsTex");
    int tex1Loc = GetShaderLocation(flowShader, "flowTex");

    float time = GetTime();
    SetShaderValue(flowShader, timeLoc, &time, SHADER_UNIFORM_FLOAT);

    BeginShaderMode(flowShader);
    SkillManager_BeginShader(flowShader);
    SetShaderValueTexture(flowShader, tex0Loc, tex);
    SetShaderValueTexture(flowShader, tex1Loc, flowTex);

    rlDrawRenderBatchActive();
    rlActiveTextureSlot(1);
    rlEnableTexture(flowTex.id);
    rlActiveTextureSlot(0);
    rlEnableTexture(tex.id);

    ProceduralMesh_DrawOrganicPuddle((Vector3){0, 0, 0}, radius);

    rlDrawRenderBatchActive();
    rlActiveTextureSlot(1);
    rlDisableTexture();
    rlActiveTextureSlot(0);
    rlDisableTexture();

    SkillManager_EndShader();
    EndShaderMode();

    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    EndBlendMode();
    rlPopMatrix();
    rlEnableBackfaceCulling();
}

void VFX_ComposeWaterStream(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float radius, float progress, float time)
{
    static Shader s_tubeShader = {0};
    static int s_timeLoc = -1;
    static int s_viewPosLoc = -1;
    static int s_lightDirLoc = -1;
    static int s_uvLengthLoc = -1;

    if (s_tubeShader.id == 0)
    {
        s_tubeShader = ResourceManager_LoadShader("skills/water/water_stream/tube.vs", "skills/water/water_stream/tube.fs");
        s_timeLoc = GetShaderLocation(s_tubeShader, "u_time");
        s_viewPosLoc = GetShaderLocation(s_tubeShader, "viewPos");
        s_lightDirLoc = GetShaderLocation(s_tubeShader, "u_lightDir");
        s_uvLengthLoc = GetShaderLocation(s_tubeShader, "u_uvLength");
    }

    if (s_tubeShader.locs == NULL)
        return;

    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlEnableBackfaceCulling();
    BeginBlendMode(BLEND_ALPHA);
    BeginShaderMode(s_tubeShader);

    SkillManager_BeginShader(s_tubeShader);

    SetShaderValue(s_tubeShader, s_timeLoc, &time, SHADER_UNIFORM_FLOAT);

    SetShaderValue(s_tubeShader, s_viewPosLoc, &camera.position, SHADER_UNIFORM_VEC3);

    Vector3 lightDir = Vector3Negate(Environment_GetSunDirection());
    SetShaderValue(s_tubeShader, s_lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);
    float uvLength = 2.0f;
    SetShaderValue(s_tubeShader, s_uvLengthLoc, &uvLength, SHADER_UNIFORM_FLOAT);

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

void VFX_ComposeIceCrystal(Vector3 basePos, int seed)
{
    static CrystalMaterial s_iceMat;
    static bool s_iceMatLoaded = false;
    if (!s_iceMatLoaded)
    {
        CrystalMaterialParams p = {0};
        p.baseColor = (Color){0, 110, 230, 200};
        p.edgeColor = (Color){130, 220, 255, 255};
        p.roughness = 0.35f;
        p.fresnel = 1.2f;
        p.refraction = 0.15f;
        p.sparkle = 0.45f;
        p.crack = 0.35f;
        p.emission = 0.15f;
        p.thickness = 1.8f;
        s_iceMat = CrystalMaterial_Load(p);
        s_iceMatLoaded = true;
    }

    CrystalDesc desc = {0};
    desc.height = 1.3f;
    desc.radius = 0.16f;
    desc.taper = 0.85f;
    desc.twist = 0.6f;
    desc.noise = 0.08f;
    desc.sides = 6;
    desc.segments = 6;

    CrystalMaterial_Begin(s_iceMat);
    ProceduralMesh_DrawCrystalCluster(basePos, &desc, 3, seed, 1.0f, WHITE);
    CrystalMaterial_End();
}
