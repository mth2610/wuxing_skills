/**
 * @file vfx_plasma_orb_optimized.c
 * @brief Phiên bản tối ưu hóa bậc cao (Lượt 2) cho hiệu ứng Plasma Energy Orb.
 * * Các điểm cải tiến trong lượt tối ưu này:
 * 1. KHỬ BỎ ACOSF VÀ TRIGONOMETRY DƯ THỪA: Trong hàm PlasmaFilamentDir, toàn bộ các phép toán
 * acosf, cosf(pitch), và sinf(pitch) được thay thế bằng đại số thuần túy (Identity Mapping).
 * Giảm từ 5 hàm lượng giác nặng xuống còn 2 hàm lượng giác phẳng (sin/cos của yaw) và 1 phép sqrtf.
 * 2. ĐỒNG BỘ DELTA-TIME TOÀN DIỆN: Loại bỏ hoàn toàn sự phụ thuộc vào Framerate của các hàm sinh hạt.
 * Hiệu ứng nhất quán 100% dù chạy ở 60 FPS, 144 FPS hay biến thiên.
 * 3. THAY THẾ GETRANDOMVALUE BẰNG FAST LCG: Loại bỏ overhead gọi hàm ngẫu nhiên hệ thống (vốn có lock tốn kém)
 * bằng một bộ sinh số ngẫu nhiên tuyến tính cục bộ cực nhanh tận dụng thanh ghi CPU.
 * 4. GIẢM THIỂU OVERDRAW VÀ ĐA GIÁC: Hạ số lượng segments của các sphere ẩn xuống mức tối ưu cho hiển thị,
 * giảm tải cho cả Vertex Shader và Pixel Shader (tránh nghẽn băng thông trong suốt - Alpha Blending).
 */

#include <math.h>
#include <stdbool.h>

// --- GIẢ ĐỊNH MÔI TRƯỜNG RAYLIB / ENGINE CỦA BẠN ---
// (Giữ nguyên các định nghĩa cấu trúc để đảm bảo tính tương thích và độc lập khi biên dịch)
#ifndef PI
#define PI 3.14159265358979323846f
#endif

// Bộ sinh số ngẫu nhiên cục bộ siêu tốc (Fast Local LCG) thay thế cho GetRandomValue/rand() của hệ thống
static inline unsigned int VFX_FastRNG(unsigned int *seed)
{
    *seed = (*seed * 1664525u) + 1013904223u;
    return *seed;
}

static inline float VFX_FastRandom01(unsigned int *seed)
{
    return (float)(VFX_FastRNG(seed) & 0xFFFFFF) / 16777215.0f;
}

/**
 * @brief Tạo vector hướng ngẫu nhiên phân bố đều trên mặt cầu (Uniform Sphere Distribution).
 * @note Đã tối ưu hóa lượt 2: Khử bỏ toán lượng giác không gian gồ ghề (acosf, sin/cos của pitch).
 */
static Vector3 PlasmaFilamentDir(int index, int epoch)
{
    // LCG Hash nguyên bản - Giữ nguyên tính chất deterministic xuất sắc của bạn
    unsigned int rng = (unsigned int)(index * 374761393 + epoch * 668265263) + 1442695040u;
    rng ^= rng >> 13;
    rng *= 1274126177u;
    rng ^= rng >> 16;
    float u = (float)(rng & 0xFFFF) / 65535.0f; // yaw 0..1

    rng = rng * 1664525u + 1013904223u;
    float v = (float)(rng >> 8 & 0xFFFF) / 65535.0f; // pitch 0..1

    // ── TỐI ƯU TOÁN HỌC BẬC CAO ──
    // Đồng nhất thức hình học: pitch = acos(2v - 1) - PI/2
    // => sin(pitch) = sin(acos(2v - 1) - PI/2) = -(2v - 1) = 1 - 2v
    // => cos(pitch) = cos(acos(2v - 1) - PI/2) = sin(acos(2v - 1)) = sqrt(1 - (2v - 1)^2)
    float yaw = u * 2.0f * PI;
    float t = 2.0f * v - 1.0f;
    float r_xy = sqrtf(1.0f - t * t); // Bán kính hình chiếu lên mặt phẳng XZ

    // Chỉ còn lại 2 phép lượng giác phẳng cho vòng tròn Yaw
    float cos_yaw = cosf(yaw);
    float sin_yaw = sinf(yaw);

    // Trả về tọa độ tối ưu (Y tương đương sin(pitch), XZ tương đương cos(pitch)*cos/sin(yaw))
    return (Vector3){r_xy * cos_yaw, -t, r_xy * sin_yaw};
}

void VFX_ComposePlasmaOrb(Vector3 pos, float radius, float time)
{
    if (radius <= 0.0f)
        return;

    // Lấy thời gian của frame hiện tại để đồng bộ hóa hoàn toàn (FPS-independent)
    float dt = GetFrameTime();

    // Khởi tạo hạt giống ngẫu nhiên cục bộ dựa trên nhịp thời gian để tránh lặp mẫu trùng lặp
    unsigned int localSeed = (unsigned int)(time * 2357.0f) + 7;

    // Breathing nhẹ nhàng cho quả cầu sinh động
    float breathe = 1.0f + 0.03f * sinf(time * 2.1f) + 0.015f * sinf(time * 5.3f);
    float r = radius * breathe;

    // ── Layer 1: Core (Bloom Sphere) ──
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();

    EffectMaterialParams bloomParams = {0};
    bloomParams.baseColor = (Color){130, 215, 255, 70};
    bloomParams.emissiveIntensity = 0.8f + 0.15f * sinf(time * 5.0f);
    bloomParams.rimStrength = 0.4f;
    bloomParams.fresnelPower = 1.4f;
    bloomParams.translucency = 0.9f;
    EffectMaterial bloomMat = Material_LoadCustom(bloomParams);

    Material_Begin(bloomMat);
    // TỐI ƯU 2: Giảm số lượng lưới từ 16x16 xuống 12x12 vì bloom nhòe không cần mật độ đa giác cao
    DrawCoreSphere(pos, r * 0.30f, 12, 12, WHITE);
    Material_End();
    rlDrawRenderBatchActive();

    // ── Layer 2: Wisp Filaments (Cuộn hạt tạo sợi quấn) ──
    static ColorGradient s_wispHeadGrad = {0};
    static ColorGradient s_wispTailGrad = {0};
    static ForceField s_wispCurlFld = {0};
    static bool s_wispInit = false;
    if (!s_wispInit)
    {
        ColorGradient_AddStop(&s_wispHeadGrad, 0.0f, (Color){255, 235, 240, 255});
        ColorGradient_AddStop(&s_wispHeadGrad, 0.45f, (Color){255, 120, 165, 220});
        ColorGradient_AddStop(&s_wispHeadGrad, 1.0f, (Color){170, 60, 220, 0});

        ColorGradient_AddStop(&s_wispTailGrad, 0.0f, (Color){255, 150, 185, 190});
        ColorGradient_AddStop(&s_wispTailGrad, 1.0f, (Color){110, 45, 200, 0});

        ForceField_AddLayer(&s_wispCurlFld, (ForceLayer){
                                                .type = FORCE_NOISE_CURL,
                                                .strength = 2.5f,
                                                .noiseScale = 3.5f,
                                                .noiseSpeed = 3.5f});
        ForceField_AddLayer(&s_wispCurlFld, (ForceLayer){
                                                .type = FORCE_VISCOSITY,
                                                .strength = 2.5f});
        s_wispInit = true;
    }

    // TỐI ƯU 2: Tăng nhẹ bán kính hạt đuôi (0.007f -> 0.011f) để tạo độ phủ đè mượt mà hơn
    static ParticleConfig s_wispTail;
    s_wispTail = (ParticleConfig){
        .radius = 0.011f * (radius / 0.5f),
        .lifetime = 0.25f,
        .gradient = &s_wispTailGrad};

    // TỐI ƯU 2: Chuyển đổi xác suất sinh hạt dựa hoàn toàn trên DeltaTime thay vì kiểm tra frame mù quáng.
    // Tần suất sinh mục tiêu: ~27 hạt head/giây (Tương đương 45% ở 60 FPS trước đây).
    float headSpawnRate = 27.0f;
    if (VFX_FastRandom01(&localSeed) < (headSpawnRate * dt))
    {
        // Thay thế GetRandomValue bằng bộ băm LCG tuyến tính cục bộ nhanh hơn
        int randIdx = VFX_FastRNG(&localSeed) & 1023;
        int randEpoch = VFX_FastRNG(&localSeed) & 1023;
        Vector3 dir = PlasmaFilamentDir(randIdx, randEpoch);

        float life = 0.45f + VFX_FastRandom01(&localSeed) * 0.25f;
        float speed = r * 0.2f / life;

        SpawnParticle((ParticleConfig){
            .position = Vector3Add(pos, Vector3Scale(dir, r * 0.12f)),
            .velocity = Vector3Scale(dir, speed),
            .radius = 0.011f * (radius / 0.5f),
            .lifetime = life,
            .gradient = &s_wispHeadGrad,
            .forceField = &s_wispCurlFld,
            .onLiveEmit = &s_wispTail,
            // TỐI ƯU 2: Giảm tỉ lệ sinh đuôi (150 -> 80) kết hợp với tăng bán kính hạt đuôi ở trên.
            // Giải pháp này giải phóng >40% lượng particle pool bị chiếm dụng mà sợi wisp vẫn đặc, liền mạch!
            .onLiveEmitRate = 80.0f});
    }

    // ── Layer 3: Wispy Membrane (Vỏ bọc Plasma kép) ──
    rlDisableBackfaceCulling();

    static PlasmaMaterial s_shellOuter, s_shellInner;
    static bool s_shellLoaded = false;
    if (!s_shellLoaded)
    {
        PlasmaMaterialParams p = {0};
        p.baseColor = (Color){20, 60, 160, 255};
        p.wispColor = (Color){110, 220, 255, 255};
        p.noiseScale = 3.2f;
        p.noiseSpeed = 0.45f;
        p.fresnelPower = 2.6f;
        p.rimStrength = 0.8f;
        p.emissive = 0.25f;
        p.opacity = 0.55f;
        p.displaceAmp = 0.05f;
        s_shellOuter = PlasmaMaterial_Load(p);

        p.noiseScale = 4.6f;
        p.noiseSpeed = -0.6f;
        p.fresnelPower = 2.0f;
        p.rimStrength = 0.5f;
        p.emissive = 0.15f;
        p.opacity = 0.3f;
        s_shellInner = PlasmaMaterial_Load(p);
        s_shellLoaded = true;
    }

    s_shellOuter.params.displaceAmp = r * 0.08f;
    s_shellInner.params.displaceAmp = r * 0.05f;

    PlasmaMaterial_Begin(s_shellOuter);
    // TỐI ƯU 2: Giảm phân đoạn lưới từ 28x28 xuống 20x20.
    // Vì màng bọc bị biến dạng liên tục bởi vertex shader, sự suy giảm số lượng đa giác là vô ảnh đối với mắt người,
    // nhưng cắt giảm một lượng lớn gánh nặng vertex xử lý nhiễu 3D ồn ào.
    DrawCoreSphere(pos, r, 20, 20, WHITE);
    PlasmaMaterial_End();

    PlasmaMaterial_Begin(s_shellInner);
    // TỐI ƯU 2: Giảm phân đoạn lưới trong từ 24x24 xuống 16x16.
    DrawCoreSphere(pos, r * 0.82f, 16, 16, WHITE);
    PlasmaMaterial_End();

    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();
    rlDrawRenderBatchActive();

    // ── Layer 4: Presence (Motes, Spark & Screen Distortion) ──

    // Ánh sáng lập lòe (Target: ~20 lần/giây)
    float lightSpawnRate = 20.0f;
    if (VFX_FastRandom01(&localSeed) < (lightSpawnRate * dt))
    {
        VFXLight_Spawn(pos, (Color){90, 210, 255, 255},
                       r * (3.5f + 0.6f * sinf(time * 3.0f)), 0.15f, VFX_PRIORITY_LOW);
    }

    // Cyan motes bốc hơi khỏi vỏ quả cầu (Target: ~18 hạt/giây thay vì 30% mỗi frame mù quáng)
    float moteSpawnRate = 18.0f;
    if (VFX_FastRandom01(&localSeed) < (moteSpawnRate * dt))
    {
        int randIdx = VFX_FastRNG(&localSeed) & 63;
        int randEpoch = VFX_FastRNG(&localSeed) & 1023;
        Vector3 dir = PlasmaFilamentDir(randIdx, randEpoch);

        SpawnParticle((ParticleConfig){
            .position = Vector3Add(pos, Vector3Scale(dir, r * (0.95f + 0.1f * VFX_FastRandom01(&localSeed)))),
            .velocity = Vector3Scale(dir, 0.15f + VFX_FastRandom01(&localSeed) * 0.2f),
            .colorStart = (Color){140, 235, 255, 200},
            .colorEnd = (Color){40, 90, 255, 0},
            .radius = 0.008f + VFX_FastRandom01(&localSeed) * 0.008f,
            .lifetime = 0.5f + VFX_FastRandom01(&localSeed) * 0.5f});
    }

    // Tia lửa nóng xuất hiện ngẫu nhiên khi filament va đập vỏ (Target: ~4 lần/giây)
    float sparkSpawnRate = 4.0f;
    if (VFX_FastRandom01(&localSeed) < (sparkSpawnRate * dt))
    {
        int randIdx = VFX_FastRNG(&localSeed) & 63;
        int randEpoch = VFX_FastRNG(&localSeed) & 1023;
        Vector3 dir = PlasmaFilamentDir(randIdx, randEpoch);

        VFX_ComposeGlintBurst(Vector3Add(pos, Vector3Scale(dir, r * 0.85f)), 2,
                              r * 0.06f, (Color){255, 150, 190, 255});
    }

    // Biến dạng không khí xung quanh quả cầu (Target: ~3.5 lần/giây)
    float distortSpawnRate = 3.5f;
    if (VFX_FastRandom01(&localSeed) < (distortSpawnRate * dt))
    {
        ScreenDistort_Add(pos, r * 1.5f, 0.08f, 0.4f, 1.5f);
    }
}