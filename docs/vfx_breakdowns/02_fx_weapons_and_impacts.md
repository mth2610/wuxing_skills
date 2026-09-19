# Tài Liệu Kỹ Thuật VFX: Bóc Tách & Tái Cấu Trúc FX_Weapons, Muzzle Flashes & Impacts (Niagara UE5 sang C Engine)

Tài liệu này phân tích chi tiết cấu trúc, logic nòng súng, đường đạn, vệt khói tên lửa và cơ chế va chạm đa bề mặt (Concrete, Metal, Wood, Glass):
- **Hệ Thống Súng & Tia Lửa**: `NS_MuzzleFlash`, `NE_MuzzleFlash_Base`, `NE_MuzzleFlash_Smoke`, `NE_MuzzleFlash_Sparks_Base`, `NE_BulletShells`.
- **Hệ Thống Đạn Đạo & Đuôi Khói**: `NS_BulletTracer`, `NS_RocketTrail`, `NS_SimpleRibbonTrail`.
- **Hệ Thống Va Chạm (Surface Impacts)**: `NS_Impact_Concrete`, `NS_Impact_Metal`, `NS_Impact_Wood`, `NS_Impact_Glass`, `NE_Impact_Sprite`, `NE_Impact_MeshAndSprite`, `NE_Impact_LightDecal`.

---

## 1. Hệ Thống Lửa Đầu Nòng (Muzzle Flash Architecture)

Lửa đầu nòng là một hiệu ứng cực nhanh ($0.03\text{s} - 0.08\text{s}$) nhưng mang tính quyết định cảm giác uy lực của vũ khí. Trong Niagara UE5, nó được thiết kế gồm 4 thành phần đan xen:

```
[Muzzle Emitter Group]
├── 1. Base Flame Core (NE_MuzzleFlash_Base)    --> Hình nón lửa 3D hoặc chữ thập Cross-Billboard
├── 2. Gun Smoke Puff  (NE_MuzzleFlash_Smoke)   --> Đám khói xám phụt theo nòng súng
├── 3. Forward Sparks  (NE_MuzzleFlash_Sparks)  --> Các hạt than cháy xé gió bay xa
└── 4. Ejected Shell   (NE_BulletShells)        --> Vỏ đạn văng nghiêng sang phải, quay 3D và rơi sàn
```

### Bảng Ánh Xạ Tài Nguyên Đã Trích Xuất

| Thành Phần UE | Asset Đã Trích Xuất Sẵn (`extracted_flipbooks/`) | Ghi Chú Kỹ Thuật |
| :--- | :--- | :--- |
| **Base Flame** | `extracted_flipbooks/Textures/T_MuzzleFlash.png` & `T_MuzzleFlash_Side.png` | Cặp texture trực giao tạo ngọn lửa 3D chữ thập |
| **Gun Smoke** | `extracted_flipbooks/Textures/T_ThinSmoke_FX.png` / `T_SmokePuff.png` | Khói mỏng phụt theo trục nòng súng |
| **Shell Casing** | Procedural Box / Cylinder Mesh trong C | Vỏ đạn đồng quay vật lý Euler |
| **Impact Dust** | `extracted_flipbooks/Sprites/T_SmokePuff.png` (Atlas 8x8) | Bụi bốc lên tại điểm đạn cắm |
| **Impact Decal** | `extracted_flipbooks/Decals/` (`T_BulletHole*`) | Vết nứt/lỗ đạn găm trên tường hoặc đất |

---

## 2. Phân Tích Logic & Toán Học Đầu Nòng (Muzzle Flash Math)

### 2.1. Cấu Trúc Ngọn Lửa Chữ Thập 3D (Cross-Billboard Alignment)
Một sprite 2D billboard thông thường luôn quay mặt về camera (`Facing: Camera`), nhưng đối với nòng súng, khi người chơi nhìn từ bên cạnh hoặc nhìn dọc thân súng, sprite 2D sẽ bị dẹp hoặc méo mó.
- **Giải pháp Niagara**: Sử dụng 2 quad bắt chéo góc $90^\circ$ dọc theo vector nòng súng $\mathbf{d}_{\text{barrel}}$:
  - Quad A: Mặt phẳng $(\mathbf{d}_{\text{barrel}}, \mathbf{u}_{\text{up}})$
  - Quad B: Mặt phẳng $(\mathbf{d}_{\text{barrel}}, \mathbf{r}_{\text{right}})$, với $\mathbf{r}_{\text{right}} = \mathbf{d}_{\text{barrel}} \times \mathbf{u}_{\text{up}}$.
- **Độ co giãn ngẫu nhiên**: Mỗi phát bắn, tỉ lệ chiều dài/rộng được nhân với hệ số ngẫu nhiên:
  $$S_{\text{length}} \in [1.2, 1.8], \quad S_{\text{width}} \in [0.8, 1.2], \quad \text{Roll Angle } \theta \in [0, 2\pi)$$

### 2.2. Vỏ Đạn Văng (Bullet Shell Physics)
- **Vận tốc văng ban đầu**:
  $$\mathbf{v}_{\text{shell}} = v_x \hat{\mathbf{r}} + v_y \hat{\mathbf{u}} + v_z \hat{\mathbf{d}}$$
  Trong đó thành phần sang phải $v_x \in [2.5, 4.0]\text{ m/s}$, hất lên $v_y \in [1.5, 3.0]\text{ m/s}$, lùi nhẹ $v_z \in [-0.5, 0.5]\text{ m/s}$.
- **Mô-men quay tự do (Tumbling)**:
  Vector vận tốc góc $\boldsymbol{\omega}_0 \in [20.0, 40.0]\text{ rad/s}$ làm vỏ đạn lộn vòng liên tục trên không trung.
- **Va chạm mặt đất**: Khi $y \le 0$, vận tốc phản xạ có hệ số nảy $e \approx 0.35$, ma sát tiếp xúc làm vỏ đạn lăn vài vòng rồi dừng lại.

---

## 3. Hệ Thống Đạn Đạo & Đuôi Tên Lửa (Tracers & Rocket Trails)

### 3.1. NS_BulletTracer (Vệt Đạn Xé Gió)
- **Cơ chế Spawn**: `SpawnPerUnit` kết hợp Ribbon Renderer. Khi đạn bay quãng đường $\Delta s$, hệ thống thêm một mấu (ribbon control point).
- **Mã vệt sáng dạng ống kéo dài (Stretched Capsule)**:
  Thay vì sinh hàng trăm hạt liên tục tốn draw calls, đường đạn súng trường được vẽ dưới dạng 1 tia kéo dài theo hướng bay:
  $$\mathbf{p}_{\text{head}} = \mathbf{p}(t), \quad \mathbf{p}_{\text{tail}} = \mathbf{p}(t) - \mathbf{d}_{\text{bullet}} \cdot L_{\text{tracer}}$$
  Với $L_{\text{tracer}} \approx 2.0\text{m} - 5.0\text{m}$. Màu sắc sáng chói (Emissive $> 5.0$) tạo hiệu ứng vệt sáng neon trong ống ngắm.

### 3.2. NS_RocketTrail (Đuôi Khói Tên Lửa)
- **Đặc trưng**: Khói tên lửa sinh ra liên tục theo vị trí tên lửa (`SpawnPerUnit`), ban đầu có đường kính nhỏ bằng ống phụt động cơ ($r \approx 0.08\text{m}$), sau đó nở to dần thành đám khói khổng lồ ($r \to 1.5\text{m}$) và trôi dạt theo gió.
- **Phương trình giãn nở bán kính & mờ đục**:
  $$r(t) = r_0 + (r_{\text{max}} - r_0) \cdot \left(\frac{t}{L}\right)^{0.5}$$
  $$\alpha(t) = \alpha_0 \cdot \left(1.0 - \frac{t}{L}\right)$$
  Lũy thừa căn bậc hai $0.5$ khiến khói nở rất nhanh khi vừa thoát khỏi buồng đốt, sau đó ổn định thể tích.

---

## 4. Hệ Thống Va Chạm Vật Liệu Đa Bề Mặt (Surface Impact Matrix)

Khi viên đạn va chạm vào vật thể, hệ thống kiểm tra loại vật liệu bề mặt (`PhysicalSurface`) và kích hoạt bộ tham số tương ứng trong `NS_Impact_*`:

| Bề Mặt | Đặc Trưng Thị Giác (Visual Signature) | Tia Lửa (Sparks) | Khói/Bụi (Smoke/Dust) | Mảnh Vỡ (Debris) | Decal Vết Đạn |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Concrete (Bê tông)** | Bụi xám trắng phụt mạnh, sỏi văng | Ít, nguội nhanh ($N \le 5$) | Dày đặc, nở to ($N \approx 8$) | Mảnh đá xám văng nảy sàn ($N \approx 12$) | Vết đục loang lổ viền đen |
| **Metal (Kim loại)** | Chùm tia lửa cam vàng xé toạc cực sáng | Rực rỡ, số lượng lớn ($N \approx 40$) | Rất ít, khói mỏng tản nhanh | Vụn kim loại nhỏ li ti | Vết lõm sáng tâm |
| **Wood (Gỗ)** | Dăm gỗ văng theo chiều thớ, khói mùn cưa | Không có ($N = 0$) | Khói mùn màu nâu vàng nhẹ | Dăm gỗ dài nhọn ($N \approx 15$) | Vết toác xơ gỗ |
| **Glass (Kính)** | Mảnh thủy tinh trong suốt văng đa hướng | Không có ($N = 0$) | Hơi bụi thủy tinh trắng li ti | Mảnh kính mỏng phản quang ($N \approx 30$) | Vết nứt mạng nhện |

### Toán Học Phản Xạ Điểm Va Chạm (Surface Normal Alignment)
Mọi vận tốc hạt văng ra từ điểm va đập $\mathbf{p}_{\text{hit}}$ đều được tính toán dựa trên pháp tuyến bề mặt $\hat{\mathbf{n}}$:
$$\mathbf{v}_{\text{spawn}} = v \cdot \left[ \cos(\theta) \hat{\mathbf{n}} + \sin(\theta) (\cos(\phi) \hat{\mathbf{t}}_1 + \sin(\phi) \hat{\mathbf{t}}_2) \right]$$
Trong đó:
- $\hat{\mathbf{t}}_1, \hat{\mathbf{t}}_2$ là hai tiếp tuyến trực giao tạo thành hệ trục tọa độ cục bộ trên bề mặt tiếp xúc.
- $\theta \in [0, \theta_{\text{cone}}]$ là góc lệch so với pháp tuyến. Đối với kim loại, góc nón rộng ($\theta_{\text{cone}} \approx 70^\circ$). Đối với bê tông, góc nón hẹp hơn ($\theta_{\text{cone}} \approx 40^\circ$) để bụi phụt thẳng ra ngoài.

---

## 5. Triển Khai Trong Engine C (`wuxing_skills`)

Dưới đây là mã nguồn C tích hợp Muzzle Flash và Impact System vào engine:

```c
#include "core/particles/particle_system.h"
#include "core/force_field.h"
#include "raymath.h"

// 1. Hàm sinh Lửa Đầu Nòng (Muzzle Flash)
void SpawnMuzzleFlashVFX(ParticleManager *pm, Vector3 muzzlePos, Vector3 forwardDir) {
    // Tầng 1: Flash Core (T_MuzzleFlash)
    ParticleConfig flash = {
        .position = muzzlePos,
        .velocity = Vector3Scale(forwardDir, 2.0f),
        .radius = 0.35f,
        .lifetime = 0.05f,
        .colorStart = (Color){255, 240, 180, 255},
        .colorEnd = (Color){255, 100, 0, 0},
        .rotation = GetRandomValue(0, 360) * DEG2RAD
    };
    ParticleManager_Emit(pm, &flash);

    // Tầng 2: Forward Sparks (12 tia lửa phụt theo hướng bắn)
    for (int i = 0; i < 12; i++) {
        Vector3 spread = {
            GetRandomValue(-20, 20) / 100.0f,
            GetRandomValue(-20, 20) / 100.0f,
            GetRandomValue(-20, 20) / 100.0f
        };
        Vector3 dir = Vector3Normalize(Vector3Add(forwardDir, spread));
        float speed = GetRandomValue(15, 30);

        ParticleConfig spark = {
            .position = muzzlePos,
            .velocity = Vector3Scale(dir, speed),
            .radius = 0.04f,
            .lifetime = GetRandomValue(6, 14) / 100.0f,
            .colorStart = (Color){255, 220, 120, 255},
            .colorEnd = (Color){255, 50, 0, 0},
            .stretchStrength = 0.08f,
            .stretchMinSpeed = 5.0f
        };
        ParticleManager_Emit(pm, &spark);
    }

    // Tầng 3: Gun Smoke Puff (Khói mỏng phụt theo nòng)
    ParticleConfig smoke = {
        .position = muzzlePos,
        .velocity = Vector3Scale(forwardDir, 3.5f),
        .radius = 0.25f,
        .lifetime = 0.40f,
        .colorStart = (Color){160, 160, 160, 140},
        .colorEnd = (Color){80, 80, 80, 0},
        .angularVelocity = GetRandomValue(-45, 45) * DEG2RAD
    };
    ParticleManager_Emit(pm, &smoke);
}

// 2. Định nghĩa các loại bề mặt va chạm
typedef enum {
    SURFACE_CONCRETE,
    SURFACE_METAL,
    SURFACE_WOOD,
    SURFACE_GLASS
} SurfaceType;

// 3. Hàm sinh Hiệu Ứng Va Chạm Tổng Quát (Surface Impact)
void SpawnImpactVFX(ParticleManager *pm, Vector3 hitPos, Vector3 hitNormal, SurfaceType surface) {
    switch (surface) {
        case SURFACE_METAL: {
            // Kim loại: Bùng nổ tia lửa rực rỡ (Sparks Rich)
            for (int i = 0; i < 35; i++) {
                // Phản xạ hình nón rộng theo pháp tuyến bề mặt
                Vector3 randTangent = Vector3Normalize((Vector3){
                    GetRandomValue(-100, 100) / 100.0f,
                    GetRandomValue(-100, 100) / 100.0f,
                    GetRandomValue(-100, 100) / 100.0f
                });
                Vector3 dir = Vector3Normalize(Vector3Add(hitNormal, Vector3Scale(randTangent, 0.8f)));
                float speed = GetRandomValue(8, 22);

                ParticleConfig spark = {
                    .position = hitPos,
                    .velocity = Vector3Scale(dir, speed),
                    .radius = 0.035f,
                    .lifetime = GetRandomValue(20, 60) / 100.0f,
                    .colorStart = (Color){255, 230, 160, 255},
                    .colorEnd = (Color){255, 40, 0, 0},
                    .collisionEnabled = true,
                    .collisionElasticity = 0.4f,
                    .stretchStrength = 0.05f
                };
                ParticleManager_Emit(pm, &spark);
            }
            break;
        }

        case SURFACE_CONCRETE: {
            // Bê tông: Phụt cột bụi dày + đá dăm (Dust + Pebbles)
            for (int i = 0; i < 6; i++) {
                Vector3 dir = Vector3Normalize(Vector3Add(hitNormal, (Vector3){
                    GetRandomValue(-30, 30) / 100.0f,
                    GetRandomValue(-30, 30) / 100.0f,
                    GetRandomValue(-30, 30) / 100.0f
                }));
                ParticleConfig dust = {
                    .position = hitPos,
                    .velocity = Vector3Scale(dir, GetRandomValue(3, 7)),
                    .radius = 0.35f,
                    .lifetime = GetRandomValue(60, 100) / 100.0f,
                    .colorStart = (Color){200, 195, 185, 180},
                    .colorEnd = (Color){100, 95, 90, 0}
                };
                ParticleManager_Emit(pm, &dust);
            }
            break;
        }

        case SURFACE_WOOD: {
            // Gỗ: Mùn cưa và mảnh xơ gỗ văng
            for (int i = 0; i < 15; i++) {
                Vector3 dir = Vector3Normalize(Vector3Add(hitNormal, (Vector3){
                    GetRandomValue(-50, 50) / 100.0f,
                    GetRandomValue(-50, 50) / 100.0f,
                    GetRandomValue(-50, 50) / 100.0f
                }));
                ParticleConfig splinter = {
                    .position = hitPos,
                    .velocity = Vector3Scale(dir, GetRandomValue(4, 10)),
                    .radius = 0.04f,
                    .lifetime = 0.8f,
                    .colorStart = (Color){180, 140, 90, 255},
                    .colorEnd = (Color){120, 90, 50, 255},
                    .collisionEnabled = true,
                    .collisionElasticity = 0.2f
                };
                ParticleManager_Emit(pm, &splinter);
            }
            break;
        }

        case SURFACE_GLASS: {
            // Thủy tinh: Mảnh kính văng lấp lánh không tia lửa
            for (int i = 0; i < 25; i++) {
                Vector3 dir = Vector3Normalize(Vector3Add(hitNormal, (Vector3){
                    GetRandomValue(-80, 80) / 100.0f,
                    GetRandomValue(-80, 80) / 100.0f,
                    GetRandomValue(-80, 80) / 100.0f
                }));
                ParticleConfig glassShard = {
                    .position = hitPos,
                    .velocity = Vector3Scale(dir, GetRandomValue(5, 14)),
                    .radius = 0.05f,
                    .lifetime = 0.7f,
                    .colorStart = (Color){220, 240, 255, 200},
                    .colorEnd = (Color){180, 220, 240, 0},
                    .collisionEnabled = true,
                    .collisionElasticity = 0.3f
                };
                ParticleManager_Emit(pm, &glassShard);
            }
            break;
        }
    }
}
```

---

## 6. Điểm Nhấn Kiến Trúc Dành Cho C Engine
1. **Khử Bớt Chi Phí Draw Calls**: Muzzle Flash chỉ tồn tại tối đa 2-3 frames, có thể tái sử dụng ngay bộ đệm tĩnh của particle manager mà không cần cấp phát lại bộ nhớ (`zero-alloc`).
2. **Dynamic Facing Mode**: Cần hỗ trợ xoay hạt dọc theo hướng chuyển động (`Facing: Velocity Stretched`) cho các tia lửa đầu nòng và mảnh văng va đập.
3. **Phân Biệt Loại Va Chạm (Surface Tagging)**: Tích hợp hàm `SpawnImpactVFX` trực tiếp vào hệ thống Raycast / Đạn bay của game để phản hồi tức thì với từng vật liệu trúng đạn.
