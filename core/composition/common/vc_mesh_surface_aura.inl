// Generic shader-only mesh overlay. It intentionally submits no particles.
static Shader s_meshSurfaceAuraShader = {0};
static int s_meshSurfaceAuraColor = -1;
static int s_meshSurfaceAuraWidth = -1;
static int s_meshSurfaceAuraIntensity = -1;
static int s_meshSurfaceAuraOpacity = -1;

static void MeshSurfaceAura_Ensure(void)
{
    if (s_meshSurfaceAuraShader.id) return;
    s_meshSurfaceAuraShader = ResourceManager_LoadShader("core/shaders/mesh_surface_aura.vs", "core/shaders/mesh_surface_aura.fs");
    s_meshSurfaceAuraColor = GetShaderLocation(s_meshSurfaceAuraShader, "u_materialColor");
    s_meshSurfaceAuraWidth = GetShaderLocation(s_meshSurfaceAuraShader, "u_rimWidth");
    s_meshSurfaceAuraIntensity = GetShaderLocation(s_meshSurfaceAuraShader, "u_rimIntensity");
    s_meshSurfaceAuraOpacity = GetShaderLocation(s_meshSurfaceAuraShader, "u_opacity");
}

void VFX_DrawMeshSurfaceAura(Mesh mesh, Matrix transform, const VFX_MeshSurfaceAuraParams *params)
{
    Material material = LoadMaterialDefault();
    Vector4 color;
    if (!params || mesh.vertexCount <= 0) return;
    MeshSurfaceAura_Ensure();
    if (!s_meshSurfaceAuraShader.id) return;
    color = ColorNormalize(params->materialColor);
    material.shader = s_meshSurfaceAuraShader;
    rlDrawRenderBatchActive();
    rlEnableDepthTest();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ALPHA);
    BeginShaderMode(s_meshSurfaceAuraShader);
    SetShaderValue(s_meshSurfaceAuraShader, s_meshSurfaceAuraColor, &color, SHADER_UNIFORM_VEC4);
    SetShaderValue(s_meshSurfaceAuraShader, s_meshSurfaceAuraWidth, &params->rimWidth, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_meshSurfaceAuraShader, s_meshSurfaceAuraIntensity, &params->rimIntensity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_meshSurfaceAuraShader, s_meshSurfaceAuraOpacity, &params->opacity, SHADER_UNIFORM_FLOAT);
    DrawMesh(mesh, material, transform);
    rlDrawRenderBatchActive();
    EndShaderMode();
    EndBlendMode();
    rlEnableDepthMask();
}

void VFX_DrawModelSurfaceAura(Model model, Matrix transform, const VFX_MeshSurfaceAuraParams *params)
{
    if (!params || model.meshCount <= 0) return;
    for (int i = 0; i < model.meshCount; ++i)
        VFX_DrawMeshSurfaceAura(model.meshes[i], MatrixMultiply(model.transform, transform), params);
}
