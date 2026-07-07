#include "rlgl.h"
#include "core/geometry/procedural_mesh_utils.h"

// Đã thêm tham số "progress" vào chữ ký hàm
void VFX_ComposeCylinderAura(VC_MaterialId matId, Vector3 pos, float radius, float progress, float time)
{
    // Nếu progress == 0 hoặc chiêu đã kết thúc thì không vẽ
    if (progress <= 0.0f || progress >= 1.0f)
        return;

    const VFX_ElementMaterial *mat = VFX_Material(matId);
    float dt = GetFrameTime();

    // Dùng progress để làm hiệu ứng mọc lên từ từ (scale-in ở 20% đầu tiên)
    float scaleIn = (progress < 0.2f) ? (progress / 0.2f) : 1.0f;
    float currentRadius = radius * scaleIn;

    // Nhịp thở của màng aura (co bóp nhẹ)
    float breathe = 1.0f + 0.03f * sinf(time * 3.0f);
    float r = currentRadius * breathe;
    float auraHeight = currentRadius * 2.5f;

    // -------------------------------------------------------------------------
    // LỚP 1: CỘT MÀNG NĂNG LƯỢNG (Cylinder Mesh + PlasmaMaterial)
    // -------------------------------------------------------------------------
    rlDrawRenderBatchActive();
    BeginBlendMode(mat->blendMode);
    rlDisableDepthMask();
    rlDisableBackfaceCulling();

    static PlasmaMaterial s_auraMat;
    static bool s_loaded = false;
    if (!s_loaded)
    {
        PlasmaMaterialParams p = {0};
        s_auraMat = PlasmaMaterial_Load(p);
        s_loaded = true;
    }

    s_auraMat.params.baseColor = VC_WithAlpha(mat->body, 255);
    s_auraMat.params.wispColor = VC_WithAlpha(mat->glow, 255);
    s_auraMat.params.noiseScale = 2.8f;
    s_auraMat.params.noiseSpeed = -0.8f;
    s_auraMat.params.fresnelPower = 3.5f;
    s_auraMat.params.rimStrength = 1.5f;
    s_auraMat.params.emissive = 0.4f;
    s_auraMat.params.opacity = 0.7f;
    s_auraMat.params.displaceAmp = r * 0.05f;

    VortexFunnelConfig fCfg = ProceduralMesh_DefaultVortexFunnelConfig();
    fCfg.bottomRadius = r * 1.15f;
    fCfg.topRadius = r * 1.15f;
    fCfg.height = auraHeight;
    fCfg.twistAmount = 0.0f;
    fCfg.ridgeAmount = 0.0f;

    VortexFunnelMeshData funnelData;
    ProceduralMesh_BuildVortexFunnel(&funnelData, pos, &fCfg, 12, 24, 0.0f);

    PlasmaMaterial_Begin(s_auraMat);
    ProceduralMesh_DrawVortexFunnel(&funnelData, WHITE);
    PlasmaMaterial_End();

    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();
    rlDrawRenderBatchActive();

    // -------------------------------------------------------------------------
    // LỚP 2: TIA NĂNG LƯỢNG CUỘN TRÒN QUANH TRỤ (Wisps)
    // -------------------------------------------------------------------------
    static ForceField s_auraCurlFld = {0};
    static bool s_wispInit = false;
    if (!s_wispInit)
    {
        ForceField_AddLayer(&s_auraCurlFld, (ForceLayer){
                                                .type = FORCE_NOISE_CURL,
                                                .strength = 4.5f,
                                                .noiseScale = 10.0f,
                                                .noiseSpeed = 1.5f});
        ForceField_AddLayer(&s_auraCurlFld, (ForceLayer){
                                                .type = FORCE_VISCOSITY,
                                                .strength = 10.0f});
        ForceField_AddLayer(&s_auraCurlFld, (ForceLayer){
                                                .type = FORCE_GRAVITY_DIR,
                                                .direction = (Vector3){0, 1.0f, 0},
                                                .strength = 3.0f});
        s_wispInit = true;
    }

    if (Random01() < (40.0f * dt))
    {
        float theta = Random01() * 2.0f * PI;
        float h = Random01() * auraHeight;

        Vector3 spawnPos = {
            pos.x + cosf(theta) * (r * 1.15f),
            pos.y + h,
            pos.z + sinf(theta) * (r * 1.15f)};

        Vector3 outDir = {cosf(theta), 0.0f, sinf(theta)};

        TrailConfig tCfg = {0};
        tCfg.type = TRAIL_TYPE_WISP;
        tCfg.pos = spawnPos;
        tCfg.vel = Vector3Scale(outDir, currentRadius * 0.2f);
        tCfg.tint = WHITE;
        tCfg.blendMode = mat->blendMode;
        tCfg.life = 0.4f + Random01() * 0.5f;
        tCfg.thick = 0.015f * currentRadius;
        tCfg.len = currentRadius * 0.4f;
        tCfg.gradient = mat->hotGrad;
        tCfg.forceField = &s_auraCurlFld;
        tCfg.priority = 0;

        SpawnTrailEntity(tCfg);
    }

    // -------------------------------------------------------------------------
    // LỚP 3: PHÁP TRẬN MẶT ĐẤT & ÁNH SÁNG
    // -------------------------------------------------------------------------
    if (GetRandomValue(0, 100) < 20)
    {
        Texture2D baseTex = mat->runeDecal ? ResourceManager_LoadTexture(mat->runeDecal) : ResourceManager_LoadTexture("assets/textures/generic/glow_circle.png");

        DecalSystem_AddEx(pos, time * 30.0f, 0.9f, 1.1f, r * 2.2f,
                          baseTex, 0.2f, VC_WithAlpha(mat->body, 120), mat->blendMode, 0.02f);
    }

    if (GetRandomValue(0, 100) < 10)
    {
        VFXLight_Spawn(Vector3Add(pos, (Vector3){0, currentRadius, 0}),
                       mat->glow, r * 3.0f, 0.2f, VFX_PRIORITY_LOW);
    }
}