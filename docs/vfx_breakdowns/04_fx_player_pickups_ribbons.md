# Tài Liệu Kỹ Thuật VFX: Bóc Tách & Tái Cấu Trúc FX_Player, Footsteps, Pickups & Ribbons (Niagara UE5 sang C Engine)

Tài liệu này phân tích chi tiết nhóm hiệu ứng nhân vật (Player Buff/Debuff, Teleport), hiệu ứng bước chân theo bề mặt (`FX_Footstep`), vật phẩm nhặt (`FX_PickUp`), và tia sét hồ quang ribbon (`NS_TeslaCoil`):
- **Vật Phẩm & Tương Tác**: `NS_Pickup_Spawn`, `NS_Pickup_Idle`, `NS_Pickup_Success`, `NS_Pickup_Timeout`.
- **Hiệu Ứng Bước Chân (Footsteps)**: `NS_Footstep_Gravel`, `NS_Footstep_Bubbles`, `NS_Footstep_Fire`, `NS_Footstep_LW`.
- **Hiệu Ứng Người Chơi & Trạng Thái**: `NS_Player_Buff_Looping`, `NS_Player_DeBuff_Looping`, `NS_Player_Teleport_In`, `NS_Player_Teleport_Out`.
- **Ranh Giới & Tia Sét Hồ Quang**: `NS_TeslaCoil`, `NS_Boundary_Sphere`, `NS_Boundary_Box`, `NS_Boundary_Cylinder`.

---

## 1. Hệ Thống Vật Phẩm Nhặt (Pickups State Machine)

Hệ thống Pickup trong Niagara được xây dựng như một **Máy trạng thái thị giác 4 pha (4-Phase Visual State Machine)**:

```
[Phase 1: SPAWN]    --> NS_Pickup_Spawn: Hạt năng lượng từ ngoài tụ vào tâm (Point Inflow Attractor)
[Phase 2: IDLE]     --> NS_Pickup_Idle: Quả cầu năng lượng lơ lửng, phát sáng, xoay quanh trục Y
[Phase 3: SUCCESS]  --> NS_Pickup_Success: Bùng nổ hào quang khi nhặt (Outflow Burst + Star Flares)
[Phase 4: TIMEOUT]  --> NS_Pickup_Timeout: Hạt năng lượng rã dần thành tro tàn khi hết thời gian
```

### Bảng Ánh Xạ Tài Nguyên Đã Trích Xuất

| Thành Phần | Material / Texture Trong UE | Asset Đã Trích Xuất (`extracted_flipbooks/`) |
| :--- | :--- | :--- |
| **Plasma Wisps** | `M_Energy`, `T_Plasma_Wisps` | `extracted_flipbooks/Sprites/T_Plasma_Wisps.png` (Atlas 8x8) |
| **Star Flare** | `MI_Flare`, `MI_Pickup_Flare` | `extracted_flipbooks/Flares/T_Flare_Round.png` |
| **Footprint Decal** | `M_Footprint_Decal` | `extracted_flipbooks/Decals/` |
| **Tesla Ribbon** | `M_Ribbon_Arc`, `M_TracerRibbon` | Renderer Ribbon dạng chuỗi đoạn thẳng đa giác |

---

## 2. Phân Tích Toán Học: Quỹ Đạo Xoắn Hút Tâm & Hồ Quang Sét

### 2.1. Quỹ Đạo Hút Tâm Xoắn Ốc (Inflow Spiral Attractor - `NS_Pickup_Spawn`)
Khi vật phẩm xuất hiện, các hạt sinh ra từ hình cầu bán kính $R$ và bị hút về tâm $\mathbf{x}_{\text{center}}$ theo quỹ đạo xoắn ốc:
$$\mathbf{a}(\mathbf{x}) = \mathbf{a}_{\text{inflow}} + \mathbf{a}_{\text{vortex}}$$
$$\mathbf{a}_{\text{inflow}} = -k_{\text{in}} \cdot \frac{\mathbf{x} - \mathbf{x}_{\text{center}}}{\|\mathbf{x} - \mathbf{x}_{\text{center}}\|^2}$$
$$\mathbf{a}_{\text{vortex}} = k_{\text{vortex}} \cdot \frac{\hat{\mathbf{u}} \times (\mathbf{x} - \mathbf{x}_{\text{center}})}{\|\mathbf{x} - \mathbf{x}_{\text{center}}\|}$$
Khi $\|\mathbf{x} - \mathbf{x}_{\text{center}}\| < r_{\text{kill}}$, hạt tự hủy (`KillParticlesInVolume`) và kích hoạt hạt quả cầu sáng bừng lên ở tâm.

### 2.2. Thuật Toán Tia Sét Hồ Quang Đệ Quy (Procedural Lightning Ribbon - `NS_TeslaCoil`)
Tia sét hồ quang được mô hình hóa bằng giải thuật **Độ dịch điểm giữa đệ quy (Midpoint Displacement Algorithm)**:
1. Cho 2 điểm cực: Cực phát $\mathbf{A}$ và Cực thu $\mathbf{B}$.
2. Chia đôi đoạn thẳng tại trung điểm: $\mathbf{M} = \frac{\mathbf{A} + \mathbf{B}}{2}$.
3. Dịch chuyển $\mathbf{M}$ theo phương trực giao với $\mathbf{AB}$:
   $$\mathbf{M}' = \mathbf{M} + \mathbf{d}_{\text{perp}} \cdot \text{RandomGaussian}(0, \sigma)$$
   với $\sigma = \text{Roughness} \cdot \|\mathbf{B} - \mathbf{A}\|$.
4. Lặp lại đệ quy từ $3 - 5$ lần trên từng phân đoạn nhỏ.
5. Vẽ dải ribbon với độ rộng co hẹp dần từ gốc đến ngọn:
   $$W(s) = W_0 \cdot (1.0 - 0.7 \cdot s), \quad s \in [0, 1]$$

---

## 3. Hệ Thống Dấu Chân & Hiệu Ứng Mặt Đất (Footstep Particle System)

Khi nhân vật chạy, mỗi khi gót chân chạm đất (`AnimNotify_Footstep`), hệ thống kích hoạt hạt bụi và in decal xuống mặt sàn:

| Loại Mặt Sàn | Asset Niagara | Thành Phần Hạt (Particles) | Cơ Chế Vật Lý |
| :--- | :--- | :--- | :--- |
| **Gravel (Sỏi đá)** | `NS_Footstep_Gravel` | Mảnh đá dăm văng nhẹ + chùm bụi mỏng | Bắn góc hẹp lùi về phía sau hướng chạy |
| **Water / Puddle** | `NS_Footstep_Bubbles` | Bọt nước nổi lên + sóng gợn tròn | Hạt nổi ngược ($v_y > 0$), vỡ tan sau $0.4\text{s}$ |
| **Fire Ground** | `NS_Footstep_Fire` | Đốm than đỏ rực + tia lửa li ti | Găm lửa lại vết chân trong $1.5\text{s}$ rồi tàn |

---

## 4. Triển Khai Trong Engine C (`wuxing_skills`)

Mã nguồn C tích hợp hoàn chỉnh hệ thống Pickup Idle và Tia sét Tesla Coil:

```c
#include "core/particles/particle_system.h"
#include "core/force_field.h"
#include "raymath.h"

// 1. Cấu hình Atlas Plasma Wisps 8x8
static SpriteAnim g_plasmaWispsAnim = {
    .frameCount = 64,
    .frameWidth = 256,
    .frameHeight = 256,
    .framesPerRow = 8,
    .duration = 1.6f,
    .loop = true
};

// 2. Lực hút xoáy tâm của Quả Cầu Năng Lượng (Pickup Idle Field)
static ForceField g_pickupOrbitField = {
    .layerCount = 2,
    .layers = {
        {
            .type = FORCE_GRAVITY_POINT,
            .strength = 3.0f // Hút hạt lại gần tâm
        },
        {
            .type = FORCE_VORTEX,
            .direction = { 0.0f, 1.0f, 0.0f },
            .strength = 5.0f // Xoay vòng quanh trục thẳng đứng
        }
    }
};

// 3. Hàm cập nhật hạt lơ lửng cho Vật Phẩm Nhặt (Pickup Idle Update)
void UpdatePickupIdleEffect(ParticleManager *pm, Vector3 centerPos, float dt, float *spawnTimer) {
    *spawnTimer += dt;
    float interval = 1.0f / 16.0f; // 16 hạt/giây

    while (*spawnTimer >= interval) {
        *spawnTimer -= interval;

        // Sinh hạt trên vỏ mặt cầu bán kính 0.4m
        float theta = (float)GetRandomValue(0, 360) * DEG2RAD;
        float phi = (float)GetRandomValue(-60, 60) * DEG2RAD;
        Vector3 offset = {
            0.4f * cosf(phi) * cosf(theta),
            0.4f * sinf(phi),
            0.4f * cosf(phi) * sinf(theta)
        };

        ParticleConfig p = {
            .position = Vector3Add(centerPos, offset),
            .velocity = Vector3Zero(),
            .radius = 0.12f,
            .lifetime = 1.2f,
            .spriteAnim = &g_plasmaWispsAnim,
            .spriteAnimPhase = (float)GetRandomValue(0, 100) / 100.0f,
            .forceField = &g_pickupOrbitField,
            .colorStart = (Color){100, 220, 255, 255}, // Xanh ngọc điện quang
            .colorEnd = (Color){20, 80, 255, 0}
        };
        ParticleManager_Emit(pm, &p);
    }
}

// 4. Hàm tạo đường gấp khúc Tia Sét Hồ Quang (Procedural Lightning Segment)
void GenerateLightningPoints(Vector3 start, Vector3 end, Vector3 *outPoints, int *pointCount, int depth, float roughness) {
    if (depth <= 0 || *pointCount >= 30) {
        outPoints[(*pointCount)++] = end;
        return;
    }

    Vector3 mid = Vector3Scale(Vector3Add(start, end), 0.5f);
    Vector3 seg = Vector3Subtract(end, start);
    float length = Vector3Length(seg);

    // Vector trực giao ngẫu nhiên
    Vector3 perp = Vector3Normalize((Vector3){
        (float)GetRandomValue(-100, 100),
        (float)GetRandomValue(-100, 100),
        (float)GetRandomValue(-100, 100)
    });
    // Loại bỏ thành phần song song với seg
    perp = Vector3Subtract(perp, Vector3Scale(seg, Vector3DotProduct(perp, seg) / (length * length)));
    perp = Vector3Normalize(perp);

    float displacement = roughness * length * ((float)GetRandomValue(-100, 100) / 100.0f);
    mid = Vector3Add(mid, Vector3Scale(perp, displacement));

    GenerateLightningPoints(start, mid, outPoints, pointCount, depth - 1, roughness * 0.6f);
    GenerateLightningPoints(mid, end, outPoints, pointCount, depth - 1, roughness * 0.6f);
}
```

---

## 5. Tổng Kết Kiến Trúc
- **Phối Hợp Dynamic Field**: Với các hiệu ứng ma thuật/năng lượng, việc kết hợp `FORCE_GRAVITY_POINT` và `FORCE_VORTEX` trong `core/force_field.h` giải quyết trọn vẹn sự bồng bềnh mà không cần lập trình quỹ đạo cố định cho từng hạt.
- **Midpoint Displacement**: Giải thuật tia sét đệ quy tạo nên những nhánh hồ quang giật điện ngẫu nhiên cực đẹp và tốn rất ít bộ nhớ GPU.
