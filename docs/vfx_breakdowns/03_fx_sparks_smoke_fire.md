# Tài Liệu Kỹ Thuật VFX: Bóc Tách & Tái Cấu Trúc FX_Sparks, Smoke & Ambient Fire (Niagara UE5 sang C Engine)

Tài liệu này phân tích chi tiết cơ chế tia lửa phát xạ liên tục/bùng nổ (`FX_Sparks`), khói ống khói bốc cao (`FX_Smoke`), và ngọn lửa môi trường đối lưu xoáy (`NS_Fire`):
- **Hệ Thống Tia Lửa**: `NS_Spark_Burst`, `NS_Spark_Continuous`, `NS_Spark_Impact_Looping`, `NE_Sparks`, `NE_SecondarySparks`.
- **Hệ Thống Cột Khói**: `NS_Smoke_Plume`, `NS_Chimney_Smoke`, `NE_Smoke`.
- **Hệ Thống Ngọn Lửa Môi Trường**: `NS_Fire`.

---

## 1. Hệ Thống Tia Lửa Bắn & Va Chạm Nảy (Sparks Physics & Secondary Bounce)

Tia lửa trong Niagara UE5 được xây dựng dựa trên nguyên lý **Phát xạ đa cấp (Two-Tier Cascade)**:
1. **Primary Sparks (`NE_Sparks`)**: Các tia lửa chính phóng ra từ nguồn với sơ tốc cao ($10 - 25\text{ m/s}$), chịu trọng lực kéo xuống và sức cản không khí.
2. **Secondary Sparks (`NE_SecondarySparks`)**: Khi tia lửa chính va chạm sàn hoặc nổ vỡ trên không, nó vỡ vụn thành các tia nhỏ li ti nảy tiếp trên bề mặt.

### Bảng Ánh Xạ Tài Nguyên Đã Trích Xuất

| Thành Phần | Material / Texture Trong UE | Asset Đã Trích Xuất (`extracted_flipbooks/`) |
| :--- | :--- | :--- |
| **Sparks Billboard** | `MI_Sparks`, `T_Spark` | `extracted_flipbooks/Textures/T_MuzzleFlash.png` (dạng kéo dãn; raw `M_Sparks` còn dùng procedural `Sprite_Capsule` + `MF_MotionStretchSpark`, không phải hai layer sprite body/core) |
| **Core Flash** | `M_BrightCore`, `MI_Flare` | `extracted_flipbooks/Flares/T_Flare_Round.png` |
| **Smoke Puff Light**| `MI_SmokePuffLight_8x8`, `T_SmokePuffLight_EOO_Loop` | `extracted_flipbooks/Sprites/T_SmokePuff.png` |

### Mô Hình Vật Lý Tia Lửa Bay (Ballistics & Drag)
Mỗi tia lửa tuân theo phương trình vi phân chuyển động:
$$\frac{d\mathbf{v}}{dt} = \mathbf{g} - D \cdot \|\mathbf{v}\| \mathbf{v} + \mathbf{F}_{\text{curl}}(\mathbf{x}, t)$$
- Trọng lực thực tế: $\mathbf{g} = (0, -9.81, 0)\text{ m/s}^2$.
- Lực cản khí động: $D \approx 0.8\text{ s}^{-1}$ (tia lửa có diện tích tiếp xúc nhỏ nên cản ít hơn khói).
- Xoáy loạn lưu nhẹ: $\mathbf{F}_{\text{curl}}$ tần số cao ($f = 2.0$), biên độ nhỏ ($A = 1.5$) giúp hạt tia lửa rung rinh như than hồng bị gió tạt.

---

## 2. Hệ Thống Cột Khói Đối Lưu & Ống Khói (Smoke Plume & Chimney)

### 2.1. Bản Chất Vật Lý Cột Khói (Thermal Plume Dynamics)
Khói bốc lên từ ống khói hoặc đám cháy lớn không chuyển động thẳng tắp mà tuân theo **quy luật cột đối lưu nhiệt của Morton-Taylor-Turner**:
1. **Lực đẩy nổi (Thermal Buoyancy)**:
   $$a_y(t) = g \cdot \left(\frac{T(t) - T_0}{T_0}\right) = a_0 \cdot e^{-\lambda t}$$
   Ban đầu khói rất nóng nên bốc lên cực nhanh ($v_y \approx 4 - 6\text{ m/s}$). Khi khói nguội dần ($T \to T_0$), gia tốc hướng lên giảm về $0$, khói bắt đầu trôi ngang theo gió.
2. **Hòa trộn khí quyển & Giãn nở thể tích (Entrainment & Expansion)**:
   Khi bốc cao, khói cuốn theo không khí lạnh xung quanh khiến thể tích nở to ra:
   $$R(y) = R_0 + \alpha_{\text{entrain}} \cdot y, \quad \alpha_{\text{entrain}} \approx 0.15 - 0.25$$
   Càng lên cao, đường kính cột khói càng nở rộng hình nón ngược.
3. **Mờ đục quang học (Optical Thickness Decay)**:
   $$\text{Opacity}(t) = \alpha_{\text{base}} \cdot \left(1.0 - \left(\frac{t}{L}\right)^2\right)$$
   Lũy thừa 2 giúp khói duy trì độ dày đặc ở chân cột và chỉ tản nhanh ở ngọn.

---

## 3. Hệ Thống Ngọn Lửa Môi Trường (NS_Fire & Swirling Vortex)

Trong `NS_Fire`, Niagara kết hợp **Lực xoáy trục (Vortex Force)** và **Nhiễu Curl Noise** để tạo ngọn lửa nhảy múa:

### 3.1. Trường Lực Xoáy Cột Lửa (Vortex Force Field)
Lực xoáy tác động lên hạt theo công thức:
$$\mathbf{F}_{\text{vortex}}(\mathbf{x}) = S_{\text{vortex}} \cdot \frac{\mathbf{u}_{\text{axis}} \times (\mathbf{x} - \mathbf{x}_0)}{\|\mathbf{x} - \mathbf{x}_0\| + \epsilon}$$
- Trục xoay: $\mathbf{u}_{\text{axis}} = (0, 1, 0)$ (thẳng đứng).
- Tâm xoay: $\mathbf{x}_0$ là gốc đốm lửa.
- Hạt lửa vừa bay lên vừa xoay quanh trục tim lửa, tạo hình chóp xoắn ốc (tornado flame) rất sống động.

---

## 4. Triển Khai Trong Engine C (`wuxing_skills`)

Mã nguồn C chuẩn hóa hoàn chỉnh cho 2 hiệu ứng: Vòi tia lửa liên tục (`SpawnContinuousSparks`) và Cột khói đối lưu (`SpawnChimneySmoke`):

```c
#include "core/particles/particle_system.h"
#include "core/force_field.h"
#include "raymath.h"

// 1. Cấu hình Atlas Animation 8x8 cho Khói (T_SmokePuff)
static SpriteAnim g_smokePuffAnim = {
    .frameCount = 64,
    .frameWidth = 256,
    .frameHeight = 256,
    .framesPerRow = 8,
    .duration = 2.5f,
    .loop = false
};

// 2. Đường cong nở rộng của cột khói (Radius expands with height)
static const SkillCurve g_chimneyRadiusCurve = {
    .pointCount = 4,
    .points = {
        {0.00f, 0.30f},  // Khởi đầu ở miệng ống khói nhỏ gọn
        {0.30f, 0.70f},
        {0.70f, 1.20f},
        {1.00f, 1.80f}   // Trên đỉnh nở to gấp 6 lần
    }
};

// 3. Đường cong mờ dần quang học (Alpha over life)
static const SkillCurve g_chimneyAlphaCurve = {
    .pointCount = 4,
    .points = {
        {0.00f, 0.0f},
        {0.10f, 0.8f},   // Bốc lên rõ nét
        {0.60f, 0.5f},
        {1.00f, 0.0f}    // Tan vào không khí
    }
};

// 4. Lực gió và dòng xoáy cho cột khói (ForceField)
static ForceField g_chimneySmokeField = {
    .layerCount = 2,
    .layers = {
        {
            .type = FORCE_WIND,
            .direction = { 1.5f, 0.2f, 0.5f }, // Gió thổi dạt ngang
            .strength = 1.8f
        },
        {
            .type = FORCE_NOISE_CURL,
            .strength = 1.2f,
            .noiseScale = 0.4f                  // Xoáy chậm biên độ lớn
        }
    }
};

// 5. Hàm cập nhật Cột Khói Ống Khói (Chimney Smoke Emitter Update)
void UpdateChimneySmokeEmitter(ParticleManager *pm, Vector3 chimneyTop, float dt, float *spawnAccumulator) {
    *spawnAccumulator += dt;
    float spawnInterval = 1.0f / 12.0f; // 12 hạt mỗi giây

    while (*spawnAccumulator >= spawnInterval) {
        *spawnAccumulator -= spawnInterval;

        // Vận tốc bốc lên đối lưu ban đầu
        Vector3 initialVel = {
            GetRandomValue(-10, 10) / 100.0f,
            (float)GetRandomValue(180, 260) / 100.0f, // 1.8 - 2.6 m/s
            GetRandomValue(-10, 10) / 100.0f
        };

        ParticleConfig smoke = {
            .position = chimneyTop,
            .velocity = initialVel,
            .radius = 0.45f,
            .lifetime = GetRandomValue(220, 320) / 100.0f, // Sống 2.2s - 3.2s
            .rotation = GetRandomValue(0, 360) * DEG2RAD,
            .angularVelocity = GetRandomValue(-30, 30) * DEG2RAD,
            .spriteAnim = &g_smokePuffAnim,
            .spriteAnimPhase = (float)GetRandomValue(0, 100) / 100.0f,
            .radiusCurve = &g_chimneyRadiusCurve,
            .alphaCurve = &g_chimneyAlphaCurve,
            .forceField = &g_chimneySmokeField,
            .colorStart = (Color){200, 200, 205, 180},
            .colorEnd = (Color){120, 120, 125, 0}
        };
        ParticleManager_Emit(pm, &smoke);
    }
}

// 6. Hàm kích hoạt Vòi Tia Lửa Hàn / Chập Điện (Continuous Spark Generator)
void EmitWeldingSparks(ParticleManager *pm, Vector3 sparkOrigin, Vector3 normal, int count) {
    for (int i = 0; i < count; i++) {
        // Hướng bắn góc nón theo pháp tuyến bề mặt tiếp xúc
        Vector3 randDir = {
            GetRandomValue(-80, 80) / 100.0f,
            GetRandomValue(-80, 80) / 100.0f,
            GetRandomValue(-80, 80) / 100.0f
        };
        Vector3 dir = Vector3Normalize(Vector3Add(normal, randDir));
        float speed = (float)GetRandomValue(80, 180) / 10.0f; // 8.0 - 18.0 m/s

        ParticleConfig spark = {
            .position = sparkOrigin,
            .velocity = Vector3Scale(dir, speed),
            .radius = 0.035f,
            .lifetime = (float)GetRandomValue(40, 110) / 100.0f,
            .colorStart = (Color){255, 240, 160, 255}, // Vàng rực chói
            .colorEnd = (Color){255, 40, 0, 0},         // Cam tàn
            .collisionEnabled = true,
            .collisionElasticity = 0.55f,               // Nảy tốt trên nền
            .collisionFloorY = sparkOrigin.y,
            .stretchStrength = 0.06f,
            .stretchMinSpeed = 1.5f
        };
        ParticleManager_Emit(pm, &spark);
    }
}
```

---

## 5. Tổng Kết Điểm Mấu Chốt
- **Độ Kéo Dãn (Velocity Stretch)**: Bắt buộc bật `stretchStrength` cho tia lửa để biến billboard tròn đơn điệu thành vệt sáng nhọn sinh động theo tốc độ.
- **Nở Thể Tích Khói (Radius Curve)**: Điểm khác biệt lớn nhất giữa khói đẹp và khói giả tạo là độ nở bán kính `radiusCurve` mô phỏng sự hòa trộn không khí lạnh theo độ cao.
