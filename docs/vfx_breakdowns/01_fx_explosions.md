# Tài Liệu Kỹ Thuật VFX: Bóc Tách & Tái Cấu Trúc FX_Explosions (Niagara UE5 sang C Engine)

Tài liệu này phân tích chi tiết cấu trúc, nguyên lý mỹ thuật, các công thức toán học vật lý và cách ánh xạ sang Engine C Raylib (`wuxing_skills`) cho hệ thống hiệu ứng nổ:
- **Niagara Systems**: `NS_Explosion`, `NS_Dirt_Explosion` (kèm các biến thể Medium, Small).
- **Niagara Emitters**: `NE_Core`, `NE_Explosion`, `NE_Debris`, `NE_DustExplosion`, `NE_GroundDust`, `NE_SparkDebris`, `NE_PostProcess`.

---

## 1. Tổng Quan & Cấu Trúc Phân Lớp (Layering Architecture)

Một vụ nổ chuẩn điện ảnh (cinematic explosion) trong Unreal Engine không bao giờ chỉ dùng 1 emitter đơn lẻ, mà luôn được chia thành **6 tầng thị giác (Visual Layers)** diễn ra theo các mốc thời gian ($t$) cực kỳ chuẩn xác:

```
[t = 0.00s - 0.08s]  Core Flash / Emissive Burst    --> NE_Core & LightRenderer (Ánh chớp cực sáng)
[t = 0.02s - 0.60s]  Main Fireball / Explosion Roil --> NE_Explosion (Cuộn lửa bùng nổ, SubUV 8x8)
[t = 0.02s - 1.20s]  High-Speed Spark Debris        --> NE_SparkDebris (Tia lửa văng tốc độ cao, nảy sàn)
[t = 0.05s - 2.50s]  Rock & Shrapnel Debris         --> NE_Debris (Mảnh vỡ khối 3D quay ngẫu nhiên)
[t = 0.08s - 1.80s]  Ground Dust Ring / Shockwave   --> NE_GroundDust (Vòng sóng xung kích bụi quét ngang mặt đất)
[t = 0.40s - 3.50s]  Dissipating Smoke Cloud        --> NE_DustExplosion (Đám khói cuộn nguội dần và bay lên)
```

### Bảng Ánh Xạ Tài Nguyên Đã Trích Xuất (Extracted Assets)

| Tầng VFX (UE Emitter) | Material / Texture Trong UE | Asset Đã Trích Xuất Sẵn (`extracted_flipbooks/`) |
| :--- | :--- | :--- |
| **NE_Core** | `MI_ExplosionFlare`, `T_ExplosionBurst` | `extracted_flipbooks/Sprites/T_ExplosionBurst.png` |
| **NE_Explosion** | `MI_ExplosionRoil_8x8`, `T_ExplosionRoil_EOO_Loop` | `extracted_flipbooks/Sprites/T_ExplosionRoil.png` (Atlas 8x8, 64 frames) |
| **NE_DustExplosion**| `MI_ExplosionRoil_8x8` (Albedo + Normal) | `extracted_flipbooks/Sprites/T_SmokeRoil.png` / `T_SmokePuff.png` |
| **NE_GroundDust** | `MI_ExplosionRoil_8x8` | `extracted_flipbooks/BakedAtlas/NS_ExplosionRoil_BakedAtlas_Alpha.png` |
| **NE_SparkDebris** | `MI_Sparks`, `T_Spark` | `extracted_flipbooks/Textures/T_MuzzleFlash.png` (dạng stretch) |
| **NE_Debris** | `MI_Pebbles`, `MI_SimpleDebris` | Mesh hình sỏi đa diện hoặc Billboard mảnh vụn |

---

## 2. Phân Tích Kỹ Thuật Chi Tiết Từng Emitter

### 2.1. NE_Core (Tia Chớp Trung Tâm - Flash Core)
- **Mục đích**: Tạo xung lực thị giác ban đầu (optical punch). Não người cảm nhận sức nổ thông qua độ chói cực đại đột ngột trong 1-2 frame đầu tiên.
- **Spawn Logic**:
  - `SpawnBurst_Instantaneous`: Số lượng hạt $N = 1 - 3$.
  - Lifetime ($L$): Cực ngắn, $L \in [0.08\text{s}, 0.15\text{s}]$.
- **Toán Học Biến Đổi**:
  - **Tỉ lệ phóng to (Exponential Expansion)**:
    $$R(t) = R_0 \cdot \left(1.0 - e^{-k \cdot \frac{t}{L}}\right), \quad k \approx 8.0$$
    Hạt đạt 90% kích thước tối đa chỉ trong $0.02\text{s}$ đầu tiên.
  - **Màu sắc & Emissive Decay**:
    - $t = 0$: Màu trắng vàng cực chói $(R=8.0, G=7.0, B=5.0)$ để kích hoạt HDR Bloom/PostFX rực rỡ.
    - $t = L$: Chuyển nhanh về cam tối $(R=1.0, G=0.3, B=0.0)$ rồi tắt hẳn ($\alpha \to 0$).

---

### 2.2. NE_Explosion (Quả Cầu Lửa & Khói Cuộn - Fireball Roil)
- **Mục đích**: Thể hiện thể tích khí giãn nở dữ dội của vụ nổ.
- **Spawn Logic**:
  - `SpawnBurst_Instantaneous`: $N = 16 - 24$ hạt.
  - Vị trí sinh: Sphere Volume bán kính $r \in [0.1\text{m}, 0.4\text{m}]$.
  - Lifetime ($L$): $L \in [0.6\text{s}, 1.2\text{s}]$.
- **Động Lực Học (Physics & Forces)**:
  1. **Vận tốc ban đầu (Explosive Velocity)**:
     $$\mathbf{v}_0 = v_{\text{speed}} \cdot \frac{\mathbf{x} - \mathbf{x}_{\text{center}}}{\|\mathbf{x} - \mathbf{x}_{\text{center}}\|} + \mathbf{v}_{\text{bias\_up}}$$
     Trong đó $v_{\text{speed}} \in [4.0, 8.0]\text{ m/s}$, cộng thêm vector hướng lên $\mathbf{v}_{\text{bias\_up}} = (0, 2.0, 0)$ do nhiệt đối lưu.
  2. **Lực cản khí động học (Aerodynamic Drag)**:
     Vận tốc giảm dần phi tuyến theo thời gian:
     $$\mathbf{F}_{\text{drag}} = -\frac{1}{2} C_d \rho A \|\mathbf{v}\| \mathbf{v} \implies \frac{d\mathbf{v}}{dt} = -D \cdot \|\mathbf{v}\| \mathbf{v}$$
     Trong UE Niagara, tham số `Drag` $D \approx 3.5\text{ s}^{-1}$. Sau $0.3\text{s}$, hạt hầu như đứng yên tại chỗ và cuộn tại vị trí giãn nở tối đa.
  3. **Lực nâng nhiệt (Buoyancy / Thermal Convection)**:
     $$\mathbf{a}_{\text{thermal}} = \mathbf{g}_{\text{up}} \cdot \beta \cdot (T - T_{\text{ambient}}) \approx (0, 1.2, 0)\text{ m/s}^2$$
- **SubUV Flipbook Animation**:
  - Dùng ảnh `T_ExplosionRoil` lưới $8 \times 8$ (tổng 64 frames).
  - Tốc độ chạy frame:
    $$\text{FrameIndex}(t) = \left\lfloor 64.0 \cdot \left(\frac{t}{L}\right)^{0.75} \right\rfloor$$
    Lũy thừa $0.75$ giúp giai đoạn đầu cháy nhanh, giai đoạn sau cuộn khói chậm rãi.
- **Nhiệt Độ Màu (Blackbody Emissive Curve)**:
  - $t/L \in [0.0, 0.25]$: Lửa cam vàng chói ($T \approx 2500\text{K}$, Emissive Multiplier $= 4.0$).
  - $t/L \in [0.25, 0.60]$: Lửa đỏ than hồng tàn dần ($T \approx 1200\text{K}$, Emissive Multiplier $= 1.0 \to 0.0$).
  - $t/L \in [0.60, 1.00]$: Hoàn toàn biến thành khói đen sẫm màu tro tàn.

---

### 2.3. NE_SparkDebris (Tia Lửa Văng & Nảy Sàn - Sparks & Bounce)
- **Mục đích**: Thể hiện mảnh than hồng bắn ra ngoài với sơ tốc cực lớn, va đập nảy trên mặt đất.
- **Spawn Logic**:
  - `SpawnBurst_Instantaneous`: $N = 30 - 60$ hạt.
  - Vận tốc phân bố hình nón hướng lên (Cone Velocity): Bán góc nón $\theta \in [30^\circ, 75^\circ]$, tốc độ ban đầu $v_0 \in [12.0, 24.0]\text{ m/s}$.
  - Lifetime ($L$): $L \in [0.8\text{s}, 2.0\text{s}]$.
- **Toán Học Động Học & Va Chạm**:
  1. **Trọng lực & Dòng xoáy (Gravity & Curl Noise)**:
     $$\frac{d\mathbf{v}}{dt} = \mathbf{g} + \mathbf{F}_{\text{curl}}(\mathbf{x}, t) - D_{\text{spark}} \mathbf{v}$$
     Với $\mathbf{g} = (0, -9.81, 0)\text{ m/s}^2$, $\mathbf{F}_{\text{curl}}$ tạo độ chao đảo tự nhiên cho đường bay.
  2. **Phản xạ va chạm mặt phẳng đất ($Y = 0$)**:
     Khi $y(t) \le y_{\text{ground}} + r_{\text{particle}}$:
     $$\mathbf{v}_n' = -e \cdot (\mathbf{v} \cdot \mathbf{n}) \mathbf{n} = -e \cdot v_y \hat{\mathbf{j}} \quad (e \approx 0.45\text{ - hệ số đàn hồi})$$
     $$\mathbf{v}_t' = (1 - \mu) \cdot \mathbf{v}_t \quad (\mu \approx 0.25\text{ - hệ số ma sát tiếp tuyến})$$
  3. **Kéo dài theo tốc độ (Velocity Stretch Billboard)**:
     Kích thước hạt dọc theo hướng chuyển động:
     $$\text{Length} = \text{BaseSize} \cdot \left(1.0 + \text{StretchFactor} \cdot \frac{\|\mathbf{v}\|}{v_{\text{ref}}}\right)$$
     Giúp mắt nhìn thấy các vệt tia lửa xé gió chân thực.

---

### 2.4. NE_Debris (Đá & Mảnh Vỡ 3D - Solid Shrapnel)
- **Mục đích**: Bắn ra đất đá, vật thể rắn từ tâm vụ nổ.
- **Spawn Logic**:
  - $N = 15 - 35$ mảnh vỡ.
  - Tốc độ $v_0 \in [8.0, 16.0]\text{ m/s}$.
  - Vận tốc góc ngẫu nhiên $\boldsymbol{\omega} \in [-15.0, 15.0]\text{ rad/s}$ trên cả 3 trục $X, Y, Z$.
- **Vật lý**:
  - Tương tự NE_SparkDebris nhưng có khối lượng lớn hơn ($m \approx 0.5\text{ kg}$), chịu trọng lực mạnh, ít bị ảnh hưởng bởi gió và khói.
  - Khi va chạm sàn đất sinh ra hiệu ứng phụ (**Sub-Emitter onCollision**): Sinh 2-3 hạt bụi nhỏ `NE_GroundDust` tại mỗi điểm va đập.

---

### 2.5. NE_GroundDust (Vòng Sóng Bụi Quét Ngang - Ground Shockwave)
- **Mục đích**: Tạo cảm giác rung chấn mặt đất, mô phỏng luồng khí nén ép sát đất thổi bụi bay ra xa.
- **Spawn Logic**:
  - $N = 25 - 40$ hạt.
  - Phân bố dạng đĩa phẳng mỏng (Cylinder / Disc Volume): $Y \in [0.0, 0.2]\text{ m}$, hướng vận tốc thuần túy quét ngang trên mặt phẳng $XZ$:
    $$\mathbf{v}_0 = v_{\text{radial}} \cdot (\cos \phi, 0, \sin \phi), \quad \phi \in [0, 2\pi), \quad v_{\text{radial}} \in [6.0, 12.0]\text{ m/s}$$
  - Drag cực mạnh ($D \approx 5.0\text{ s}^{-1}$): Vòng bụi mở rộng rất nhanh trong $0.2\text{s}$ rồi dừng lại, từ từ bốc khói mỏng và tan biến.
  - Căn lề Billboard: Cố định mặt phẳng ngang đất (`FacingMode: Flat / Plane Alignment`).

---

## 3. Bản Đồ Toán Học Tổng Hợp (VFX Math Master Reference)

### 3.1. Phương Trình Quỹ Đạo Hạt Tổng Quát (Numerical Integrator)
Mỗi frame $\Delta t$, trạng thái hạt được cập nhật theo giải thuật Euler bán ẩn (Semi-implicit Euler):
$$\mathbf{v}_{t+\Delta t} = \mathbf{v}_t + \left[ \mathbf{g} + \frac{\mathbf{F}_{\text{ext}}}{m} - D \cdot \|\mathbf{v}_t\| \mathbf{v}_t \right] \Delta t$$
$$\mathbf{x}_{t+\Delta t} = \mathbf{x}_t + \mathbf{v}_{t+\Delta t} \Delta t$$

### 3.2. Trường Vector Dòng Xoáy 3D (Curl Noise Formula)
Để các cụm khói và lửa cuộn xoáy tự nhiên mà không tụ lại một điểm (bảo toàn khối lượng, $\nabla \cdot \mathbf{v} = 0$):
$$\mathbf{v}_{\text{curl}}(\mathbf{x}) = \nabla \times \mathbf{\Psi}(\mathbf{x}) = \begin{pmatrix} \frac{\partial \Psi_z}{\partial y} - \frac{\partial \Psi_y}{\partial z} \\ \frac{\partial \Psi_x}{\partial z} - \frac{\partial \Psi_z}{\partial x} \\ \frac{\partial \Psi_y}{\partial x} - \frac{\partial \Psi_x}{\partial y} \end{pmatrix}$$
Trong đó $\mathbf{\Psi}(\mathbf{x}) = (\text{Perlin}(\mathbf{x}), \text{Perlin}(\mathbf{x} + \boldsymbol{\delta}_1), \text{Perlin}(\mathbf{x} + \boldsymbol{\delta}_2))$. Engine C đã tích hợp sẵn hàm này tại `core/force_field.h` thông qua `Noise_Curl3D(x, y, z, scale)`.

---

## 4. Hướng Dẫn Ánh Xạ Sang C Engine (`wuxing_skills`)

Dưới đây là mã nguồn C chuẩn áp dụng kiến trúc trên, sử dụng hệ thống `ParticleConfig`, `ForceField`, và `SpriteAnim` có sẵn trong engine.

```c
#include "core/particles/particle_system.h"
#include "core/force_field.h"
#include "core/resource_manager.h"
#include "raymath.h"

// 1. Cấu hình Atlas Animation 8x8 cho Cuộn Lửa (T_ExplosionRoil)
static SpriteAnim g_explosionRoilAnim = {
    .frameCount = 64,
    .frameWidth = 256,   // Kích thước 1 ô trong atlas 2048x2048
    .frameHeight = 256,
    .framesPerRow = 8,
    .duration = 0.9f,
    .loop = false
};

// 2. Định nghĩa Curve giãn nở bán kính (Radius over Life)
static const SkillCurve g_fireballRadiusCurve = {
    .pointCount = 4,
    .points = {
        {0.00f, 0.20f},  // Vừa sinh ra nhỏ gọn
        {0.15f, 0.85f},  // Nở bung cực nhanh
        {0.50f, 1.00f},  // Giữ thể tích
        {1.00f, 1.15f}   // Tản mác nhẹ trước khi tắt
    }
};

// 3. Đường cong Alpha mờ dần
static const SkillCurve g_fireballAlphaCurve = {
    .pointCount = 4,
    .points = {
        {0.00f, 0.0f},
        {0.05f, 1.0f},   // Sáng rõ tức thì
        {0.60f, 0.9f},   // Giữ độ mờ đặc
        {1.00f, 0.0f}    // Tan biến
    }
};

// 4. Lực cản khí động học & Nhiễu dòng xoáy (ForceField)
static ForceField g_explosionField = {
    .layerCount = 2,
    .layers = {
        {
            .type = FORCE_DRAG,
            .strength = 3.5f   // Kìm hãm tốc độ văng
        },
        {
            .type = FORCE_NOISE_CURL,
            .strength = 4.0f,  // Cuộn xoáy khói
            .noiseScale = 0.8f
        }
    }
};

// 5. Hàm kích hoạt vụ nổ hoàn chỉnh (Spawn Cinematic Explosion)
void SpawnExplosionVFX(ParticleManager *pm, Vector3 origin, float scale) {
    // TẦNG 1: Core Flash (1 hạt cực sáng, chớp trong 0.1 giây)
    ParticleConfig flash = {
        .position = origin,
        .velocity = Vector3Zero(),
        .radius = 2.5f * scale,
        .lifetime = 0.10f,
        .colorStart = (Color){255, 255, 220, 255},
        .colorEnd = (Color){255, 120, 20, 0},
        .emissiveCurve = NULL // Bloom tối đa
    };
    ParticleManager_Emit(pm, &flash);

    // TẦNG 2: Main Fireball Roil (20 hạt SubUV 8x8)
    for (int i = 0; i < 20; i++) {
        // Hướng bắn ngẫu nhiên hình cầu bán nguyệt (bốc lên trên)
        Vector3 dir = (Vector3){
            GetRandomValue(-100, 100) / 100.0f,
            GetRandomValue(20, 100) / 100.0f,
            GetRandomValue(-100, 100) / 100.0f
        };
        dir = Vector3Normalize(dir);
        float speed = (GetRandomValue(40, 80) / 10.0f) * scale;

        ParticleConfig fireball = {
            .position = Vector3Add(origin, Vector3Scale(dir, 0.2f * scale)),
            .velocity = Vector3Scale(dir, speed),
            .radius = (GetRandomValue(12, 18) / 10.0f) * scale,
            .lifetime = GetRandomValue(70, 100) / 100.0f,
            .rotation = GetRandomValue(0, 360) * DEG2RAD,
            .angularVelocity = GetRandomValue(-60, 60) * DEG2RAD,
            .spriteAnim = &g_explosionRoilAnim,
            .spriteAnimPhase = (float)i * 0.03f, // Phá vỡ tính đồng pha
            .radiusCurve = &g_fireballRadiusCurve,
            .alphaCurve = &g_fireballAlphaCurve,
            .forceField = &g_explosionField,
            .colorStart = (Color){255, 200, 100, 255},
            .colorEnd = (Color){40, 35, 35, 0}   // Nguội thành tro xám
        };
        ParticleManager_Emit(pm, &fireball);
    }

    // TẦNG 3: Sparks Debris có nảy sàn (40 tia lửa bắn nhanh)
    for (int i = 0; i < 40; i++) {
        float angle = (GetRandomValue(0, 360) * DEG2RAD);
        float pitch = (GetRandomValue(25, 75) * DEG2RAD);
        Vector3 vel = {
            cosf(pitch) * cosf(angle),
            sinf(pitch),
            cosf(pitch) * sinf(angle)
        };
        float sparkSpeed = (GetRandomValue(120, 220) / 10.0f) * scale;

        ParticleConfig spark = {
            .position = origin,
            .velocity = Vector3Scale(vel, sparkSpeed),
            .radius = 0.08f * scale,
            .lifetime = GetRandomValue(100, 180) / 100.0f,
            .colorStart = (Color){255, 230, 150, 255},
            .colorEnd = (Color){255, 60, 0, 0},
            .collisionEnabled = true,
            .collisionElasticity = 0.45f,
            .collisionFloorY = origin.y,
            .stretchStrength = 0.04f,
            .stretchMinSpeed = 2.0f
        };
        ParticleManager_Emit(pm, &spark);
    }

    // TẦNG 4: Ground Dust Ring (25 hạt bụi quét ngang mặt sàn)
    for (int i = 0; i < 25; i++) {
        float angle = (float)i / 25.0f * 2.0f * PI;
        Vector3 hDir = { cosf(angle), 0.02f, sinf(angle) };
        float dustSpeed = (GetRandomValue(70, 110) / 10.0f) * scale;

        ParticleConfig dust = {
            .position = Vector3Add(origin, Vector3Scale(hDir, 0.3f)),
            .velocity = Vector3Scale(hDir, dustSpeed),
            .radius = (GetRandomValue(15, 22) / 10.0f) * scale,
            .lifetime = GetRandomValue(120, 160) / 100.0f,
            .forceField = &g_explosionField,
            .colorStart = (Color){180, 160, 140, 180},
            .colorEnd = (Color){100, 95, 90, 0}
        };
        ParticleManager_Emit(pm, &dust);
    }
}
```

---

## 5. Danh Sách Kiểm Tra Khi Triển Khai (Implementation Checklist)
- [x] Đã trích xuất và lật trục Y đúng chiều cho flipbook `T_ExplosionRoil.png` và `T_ExplosionBurst.png`.
- [x] Đã cấu hình bộ tham số thời gian cho 4 tầng visual (Flash -> Fireball -> Sparks -> Ground Dust).
- [x] Đã áp dụng `FORCE_DRAG` kết hợp `FORCE_NOISE_CURL` trong `core/force_field.h` để tạo độ cuộn cho khói.
- [x] Đã tích hợp va chạm mặt đất `collisionEnabled = true`, `collisionElasticity = 0.45f` cho tia lửa nảy.
- [x] Đã phá vỡ tính đồng bộ frame SubUV bằng `spriteAnimPhase`.
