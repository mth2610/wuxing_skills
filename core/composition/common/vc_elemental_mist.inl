#include "rlgl.h"

void VFX_ComposeElementalMist(VC_MaterialId matId, Vector3 pos, float radius, float time)
{
    const VFX_ElementMaterial *mat = VFX_Material(matId);

    // -------------------------------------------------------------------------
    // LỚP 1: SƯƠNG BÒ SÁT ĐẤT (Ground Creep Decal)
    // TỐI ƯU: Giảm xác suất rải decal từ 15% xuống 5%. Tăng thời gian sống (3.0f -> 4.0f)
    // để bù đắp, giúp tiết kiệm bộ đệm Decal mà vẫn giữ được vệt sương dưới đất.
    // -------------------------------------------------------------------------
    if (GetRandomValue(0, 100) < 5)
    {
        Texture2D smokeTex = ResourceManager_LoadTexture("assets/textures/generic/shadow_blob.png");
        DecalSystem_AddEx(pos,
                          Random01() * 360.0f,
                          0.8f,
                          0.2f, radius * 1.5f,
                          smokeTex,
                          4.0f, // Tăng lifetime
                          VC_WithAlpha(mat->soft, 180),
                          BLEND_ALPHA,
                          0.02f);
    }

    // -------------------------------------------------------------------------
    // LỚP 2: CÁC XÚC TU SƯƠNG BAY LÊN (Soft Mist Particles)
    // -------------------------------------------------------------------------
    static SkillCurve s_mistSize = {0};
    static ForceField s_fld;
    static bool s_initDone = false;

    if (!s_initDone)
    {
        FloatCurve_AddStop(&s_mistSize, 0.0f, 0.2f);
        FloatCurve_AddStop(&s_mistSize, 0.3f, 1.0f);
        FloatCurve_AddStop(&s_mistSize, 0.7f, 1.0f);
        FloatCurve_AddStop(&s_mistSize, 1.0f, 0.5f);

        ForceField_Clear(&s_fld);
        // TỐI ƯU: Giảm strength của noise curl (0.12f -> 0.08f) để hạt di chuyển ổn định hơn,
        // bớt đòi hỏi CPU phải cập nhật vector liên tục biên độ lớn.
        ForceField_AddLayer(&s_fld, (ForceLayer){.type = FORCE_NOISE_CURL, .strength = 0.08f, .noiseScale = 1.0f, .noiseSpeed = 0.3f});
        ForceField_AddLayer(&s_fld, (ForceLayer){.type = FORCE_GRAVITY_DIR, .direction = (Vector3){0.0f, -1.0f, 0.0f}, .strength = 0.4f});
        ForceField_AddLayer(&s_fld, (ForceLayer){.type = FORCE_VISCOSITY, .strength = 4.0f});

        s_initDone = true;
    }

    // TỐI ƯU OVERDRAW CỰC MẠNH: Thay vì đẻ 1-2 hạt mỗi frame (90 hạt/s),
    // ta chỉ cho phép đẻ 1 hạt với xác suất 30% (~18 hạt/s).
    int spawnCount = (GetRandomValue(0, 100) < 30) ? 1 : 0;

    for (int i = 0; i < spawnCount; i++)
    {
        float a = Random01() * 2.0f * PI;
        float rr = radius * 0.9f * sqrtf(Random01());

        // Bù đắp cho việc ít hạt hơn: Tăng kích thước gốc lên một chút xíu (0.15 -> 0.18)
        float pRadius = 0.18f + Random01() * 0.12f;

        SpawnParticle((ParticleConfig){
            .position = {pos.x + cosf(a) * rr,
                         pos.y + pRadius * 0.4f + Random01() * 0.05f,
                         pos.z + sinf(a) * rr},

            .velocity = {(Random01() - 0.5f) * 0.03f,
                         0.01f + Random01() * 0.015f,
                         (Random01() - 0.5f) * 0.03f},

            // Bù đắp hiển thị: Tăng Alpha khởi điểm (35 -> 60) vì khối lượng hạt đã giảm 75%
            .colorStart = VC_WithAlpha(mat->soft, 60),
            .colorEnd = VC_WithAlpha(mat->soft, 0),
            .radius = pRadius,
            .lifetime = 1.5f + Random01() * 1.0f,
            .radiusCurve = &s_mistSize,
            .forceField = &s_fld});
    }

    // TỐI ƯU ĐÈN: Giảm xác suất nháy đèn chớp từ 5% xuống 2% để giảm pass chiếu sáng
    if (GetRandomValue(0, 100) < 2)
    {
        VFXLight_Spawn(Vector3Add(pos, (Vector3){0, 0.05f, 0}),
                       mat->soft, radius * 0.6f, 0.2f, VFX_PRIORITY_LOW);
    }
}

void VFX_ComposePathMistWave(VC_MaterialId matId, const Vector3 *pathPoints, int pathCount, float progress, float radius)
{
    if (progress <= 0.0f || progress >= 1.0f || pathCount < 2)
        return;

    const VFX_ElementMaterial *mat = VFX_Material(matId);

    float fIdx = progress * (pathCount - 1);
    int idx0 = (int)fIdx;
    int idx1 = idx0 + 1;

    if (idx1 >= pathCount)
        idx1 = pathCount - 1;

    Vector3 pos = Vector3Lerp(pathPoints[idx0], pathPoints[idx1], fIdx - (float)idx0);

    static SkillCurve s_trailSize = {0};
    static ForceField s_trailFld;
    static bool s_trailInit = false;

    if (!s_trailInit)
    {
        FloatCurve_AddStop(&s_trailSize, 0.0f, 0.2f);
        FloatCurve_AddStop(&s_trailSize, 0.2f, 1.0f);
        FloatCurve_AddStop(&s_trailSize, 0.7f, 1.0f);
        FloatCurve_AddStop(&s_trailSize, 1.0f, 0.0f);

        ForceField_Clear(&s_trailFld);
        ForceField_AddLayer(&s_trailFld, (ForceLayer){.type = FORCE_NOISE_CURL, .strength = 0.08f, .noiseScale = 1.0f, .noiseSpeed = 0.3f});
        ForceField_AddLayer(&s_trailFld, (ForceLayer){.type = FORCE_GRAVITY_DIR, .direction = (Vector3){0.0f, -1.0f, 0.0f}, .strength = 0.4f});
        ForceField_AddLayer(&s_trailFld, (ForceLayer){.type = FORCE_VISCOSITY, .strength = 5.0f});

        s_trailInit = true;
    }

    // TỐI ƯU DECAL: Đưa xác suất từ 10% xuống 4%.
    if (GetRandomValue(0, 100) < 4)
    {
        Texture2D frostTex = ResourceManager_LoadTexture("assets/textures/decals/decal_frost_ring.png");
        DecalSystem_AddEx(pos, Random01() * 360.0f, 0.5f,
                          0.2f, radius * 1.5f, frostTex, 4.0f,
                          VC_WithAlpha(mat->soft, 160), BLEND_ALPHA, 0.02f);
    }

    // TỐI ƯU LƯỢNG HẠT: Chuyển từ sinh 1-2 hạt mỗi frame sang xác suất 40% đẻ 1 hạt.
    int spawnCount = (GetRandomValue(0, 100) < 40) ? 1 : 0;

    for (int i = 0; i < spawnCount; i++)
    {
        float a = Random01() * 2.0f * PI;
        float pRadius = 0.25f + Random01() * 0.15f; // Tăng radius gốc để bù đắp
        float rr = radius * 0.8f * sqrtf(Random01());

        SpawnParticle((ParticleConfig){
            .position = {pos.x + cosf(a) * rr, pos.y + pRadius - 0.02f, pos.z + sinf(a) * rr},
            .velocity = {(Random01() - 0.5f) * 0.02f, 0.01f, (Random01() - 0.5f) * 0.02f},

            // Tăng Alpha (25 -> 45) vì đã giảm số lượng hạt
            .colorStart = VC_WithAlpha(mat->soft, 45),
            .colorEnd = VC_WithAlpha(mat->soft, 0),
            .radius = pRadius,
            .lifetime = 1.5f + Random01() * 1.0f,
            .radiusCurve = &s_trailSize,
            .forceField = &s_trailFld});
    }

    // TỐI ƯU ĐÈN: 5% -> 2%
    if (GetRandomValue(0, 100) < 2)
    {
        VFXLight_Spawn(Vector3Add(pos, (Vector3){0, 0.1f, 0}), mat->soft, radius * 1.2f, 0.2f, VFX_PRIORITY_LOW);
    }
}