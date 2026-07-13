void VFX_ComposeMagicPuddle(Vector3 pos)
{
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();

    float radius = 1.2f;
    rlPushMatrix();
    rlTranslatef(pos.x, pos.y + 0.03f, pos.z);

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
