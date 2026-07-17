// ==============================================================================
// Vị trí: core/composition/vc_smoke.inl
// ==============================================================================
static Shader s_raymarchSmokeBoxShader = {0};
static int loc_boxMin, loc_boxMax, loc_center, loc_radius, loc_smokeColor;

void VFX_ComposeRaymarchSmokeBox(VC_MaterialId matId, Vector3 center, float size, float time)
{
    const VFX_ElementMaterial *mat = VFX_Material(matId);

    if (s_raymarchSmokeBoxShader.id == 0)
    {
        s_raymarchSmokeBoxShader = ResourceManager_LoadShader("core/shaders/effect_material.vs", "core/shaders/raymarch_smoke_box.fs");
        loc_boxMin = GetShaderLocation(s_raymarchSmokeBoxShader, "u_boxMin");
        loc_boxMax = GetShaderLocation(s_raymarchSmokeBoxShader, "u_boxMax");
        loc_center = GetShaderLocation(s_raymarchSmokeBoxShader, "u_center");
        loc_radius = GetShaderLocation(s_raymarchSmokeBoxShader, "u_radius");
        loc_smokeColor = GetShaderLocation(s_raymarchSmokeBoxShader, "u_smokeColor");
    }

    if (s_raymarchSmokeBoxShader.id == 0)
        return;

    float halfSize = size * 0.5f;
    Vector3 boxMin = {center.x - halfSize, center.y - halfSize, center.z - halfSize};
    Vector3 boxMax = {center.x + halfSize, center.y + halfSize, center.z + halfSize};

    // Shader xử lý Alpha qua Raymarch
    Vector4 colorVec = {
        (float)mat->body.r / 255.0f,
        (float)mat->body.g / 255.0f,
        (float)mat->body.b / 255.0f,
        1.0f};

    SkillManager_BeginShader(s_raymarchSmokeBoxShader);

    SetShaderValue(s_raymarchSmokeBoxShader, loc_boxMin, &boxMin, SHADER_UNIFORM_VEC3);
    SetShaderValue(s_raymarchSmokeBoxShader, loc_boxMax, &boxMax, SHADER_UNIFORM_VEC3);
    SetShaderValue(s_raymarchSmokeBoxShader, loc_center, &center, SHADER_UNIFORM_VEC3);
    SetShaderValue(s_raymarchSmokeBoxShader, loc_radius, &halfSize, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_raymarchSmokeBoxShader, loc_smokeColor, &colorVec, SHADER_UNIFORM_VEC4);

    rlDisableDepthMask();
    BeginBlendMode(mat->blendMode);

    // Tắt Culling để vẽ cả mặt trước & mặt sau của hộp
    rlDisableBackfaceCulling();

    // Dùng lại Cube gốc. Matrix Identity được Engine bảo vệ an toàn [Rule D].
    DrawCoreCube(center, size, size, size, BLANK);

    rlEnableBackfaceCulling();
    EndBlendMode();
    rlEnableDepthMask();

    SkillManager_EndShader();
}