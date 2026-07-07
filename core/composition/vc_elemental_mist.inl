#include "rlgl.h"

void VFX_ComposeElementalMist(VC_MaterialId matId, Vector3 pos, float radius, float time)
{
    const VFX_ElementMaterial *mat = VFX_Material(matId);

    // -------------------------------------------------------------------------
    // LỚP 1: SƯƠNG BÒ SÁT ĐẤT (Ground Creep Decal)
    // -------------------------------------------------------------------------
    if (GetRandomValue(0, 100) < 15)
    {
        Texture2D smokeTex = ResourceManager_LoadTexture("assets/textures/generic/shadow_blob.png");
        DecalSystem_AddEx(pos,
                          Random01() * 360.0f,
                          0.8f,
                          0.2f, radius * 1.4f,
                          smokeTex,
                          3.0f,
                          VC_WithAlpha(mat->soft, 180),
                          BLEND_ALPHA,
                          0.02f);
    }

    // -------------------------------------------------------------------------
    // LỚP 2: CÁC XÚC TU SƯƠNG BAY LÊN (Soft Mist Particles)
    // Phân bố đều toàn diện tích, hạt nhiều hơn, trôi tự do hơn.
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
        ForceField_AddLayer(&s_fld, (ForceLayer){.type = FORCE_NOISE_CURL, .strength = 0.12f, .noiseScale = 1.0f, .noiseSpeed = 0.3f});
        ForceField_AddLayer(&s_fld, (ForceLayer){.type = FORCE_GRAVITY_DIR, .direction = (Vector3){0.0f, -1.0f, 0.0f}, .strength = 0.4f});
        ForceField_AddLayer(&s_fld, (ForceLayer){.type = FORCE_VISCOSITY, .strength = 4.0f});
        s_initDone = true;
    }

    // Luôn đẻ 1 đến 2 hạt mỗi frame để sương mọc liên tục và dày đặc
    int spawnCount = GetRandomValue(1, 2);
    for (int i = 0; i < spawnCount; i++)
    {
        float a = Random01() * 2.0f * PI;

        // TOÁN HỌC: Dùng sqrtf(Random01()) để diện tích đĩa được phủ đều 100%
        float rr = radius * 0.9f * sqrtf(Random01());

        float pRadius = 0.15f + Random01() * 0.1f;

        SpawnParticle((ParticleConfig){
            .position = {pos.x + cosf(a) * rr,
                         pos.y + pRadius * 0.4f + Random01() * 0.05f,
                         pos.z + sinf(a) * rr},
            // VẬN TỐC: Không văng ra từ tâm nữa. Hạt sẽ trôi lơ lửng, lắc lư tự do.
            .velocity = {(Random01() - 0.5f) * 0.03f,
                         0.01f + Random01() * 0.015f,
                         (Random01() - 0.5f) * 0.03f},
            .colorStart = VC_WithAlpha(mat->soft, 35),
            .colorEnd = VC_WithAlpha(mat->soft, 0),
            .radius = pRadius,
            .lifetime = 1.5f + Random01() * 1.0f,
            .radiusCurve = &s_mistSize,
            .forceField = &s_fld});
    }

    if (GetRandomValue(0, 100) < 5)
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

    // 1. TÍNH TỌA ĐỘ MŨI NHỌN (Wavefront) TRỰC TIẾP TỪ PROGRESS
    float fIdx = progress * (pathCount - 1);
    int idx0 = (int)fIdx;
    int idx1 = idx0 + 1;
    if (idx1 >= pathCount)
        idx1 = pathCount - 1;

    // Nội suy siêu mượt không cần lưu trữ ở cấp độ skill
    Vector3 pos = Vector3Lerp(pathPoints[idx0], pathPoints[idx1], fIdx - (float)idx0);

    // 2. KHỞI TẠO FORCE FIELD TĨNH (Dùng chung cho mọi điểm gọi hàm)
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
        ForceField_AddLayer(&s_trailFld, (ForceLayer){.type = FORCE_NOISE_CURL, .strength = 0.12f, .noiseScale = 1.0f, .noiseSpeed = 0.3f});
        ForceField_AddLayer(&s_trailFld, (ForceLayer){.type = FORCE_GRAVITY_DIR, .direction = (Vector3){0.0f, -1.0f, 0.0f}, .strength = 0.4f});
        ForceField_AddLayer(&s_trailFld, (ForceLayer){.type = FORCE_VISCOSITY, .strength = 5.0f});
        s_trailInit = true;
    }

    // 3. RẢI BÓNG ĐẤT (DECAL) THÔNG MINH
    // Thay vì theo dõi index, ta rải decal theo xác suất. Phễu 10% tạo ra ~6 decal/giây
    // ở 60fps, phủ kín đường đi mà vẫn bảo vệ tuyệt đối giới hạn MAX_DECALS (64) của hệ thống.
    if (GetRandomValue(0, 100) < 10)
    {
        Texture2D frostTex = ResourceManager_LoadTexture("assets/textures/decals/decal_frost_ring.png");
        DecalSystem_AddEx(pos, Random01() * 360.0f, 0.5f,
                          0.2f, radius * 1.5f, frostTex, 3.0f,
                          VC_WithAlpha(mat->soft, 140), BLEND_ALPHA, 0.02f);
    }

    // 4. PHUN SƯƠNG BÁM ĐƯỜNG ĐI
    int spawnCount = GetRandomValue(1, 2);
    for (int i = 0; i < spawnCount; i++)
    {
        float a = Random01() * 2.0f * PI;
        float pRadius = 0.2f + Random01() * 0.15f;
        float rr = radius * 0.8f * sqrtf(Random01());

        SpawnParticle((ParticleConfig){
            .position = {pos.x + cosf(a) * rr, pos.y + pRadius - 0.02f, pos.z + sinf(a) * rr},
            .velocity = {(Random01() - 0.5f) * 0.02f, 0.01f, (Random01() - 0.5f) * 0.02f},
            .colorStart = VC_WithAlpha(mat->soft, 25),
            .colorEnd = VC_WithAlpha(mat->soft, 0),
            .radius = pRadius,
            .lifetime = 1.5f + Random01() * 1.0f,
            .radiusCurve = &s_trailSize,
            .forceField = &s_trailFld});
    }

    // 5. CHỚP SÁNG LẠNH TẠI MŨI NHỌN
    if (GetRandomValue(0, 100) < 5)
    {
        VFXLight_Spawn(Vector3Add(pos, (Vector3){0, 0.1f, 0}), mat->soft, radius * 1.2f, 0.2f, VFX_PRIORITY_LOW);
    }
}