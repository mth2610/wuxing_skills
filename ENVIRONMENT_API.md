# Environment System API Documentation

Tài liệu này mô tả cách sử dụng module **Environment** (`environment/environment_system.h`). Hệ thống này chịu trách nhiệm quản lý ánh sáng, bóng đổ giả (Smart Fake Shadow) và môi trường chung cho toàn bộ engine Wuxing Skills.

## 1. Các kiểu dữ liệu (Data Structures)

### `EnvShadowShapeType`
Định nghĩa hình dáng của vật thể để hệ thống tự động tính toán cách đổ bóng cho phù hợp.

```c
typedef enum {
    ENV_SHAPE_SPHERE,   // Dùng cho: Nhân vật, quái vật, hoặc các vật thể dạng cầu (sẽ đổ bóng Capsule từ dưới chân).
    ENV_SHAPE_CYLINDER, // Dùng cho: Cột đá, thân cây, các vật thể hình trụ đứng (đổ bóng Capsule dài).
    ENV_SHAPE_BOX       // Dùng cho: Các vật thể hình hộp vuông vức (ví dụ: rương, chướng ngại vật khối).
} EnvShadowShapeType;
```

### `EnvFogConfig`
Cấu hình sương mù (nếu có sử dụng cho bầu không khí).

```c
typedef struct {
    Color color;    // Màu sắc của sương mù
    float start;    // Khoảng cách bắt đầu mờ sương (tính từ Camera)
    float end;      // Khoảng cách sương mù đặc hoàn toàn
    float density;  // Mật độ sương mù
    bool enabled;   // Bật/tắt sương mù
} EnvFogConfig;
```

---

## 2. Các hàm vòng đời (Lifecycle)

Các hàm này được Core gọi tự động trong `sandbox_core.c`. Các module khác (như Skill) **không cần/không nên gọi lại**.

```c
// Khởi tạo các thông số mặc định của môi trường (chuẩn hóa hướng nắng, v.v)
void Environment_Init(void);

// Cập nhật môi trường theo thời gian (ví dụ: chu kỳ ngày đêm, mây bay)
void Environment_Update(float dt);
```

---

## 3. Hệ thống Bóng Đổ (Smart Fake Shadow) - Quan Trọng Nhất

Đây là API quan trọng nhất để các module khác (VD: `MeshSystem`, `Seismic Pillar Skill`) tương tác nhằm vẽ bóng cho vật thể của chúng.

```c
void Environment_DrawSmartShadow(Vector3 pos, EnvShadowShapeType shape, float width, float height);
```

**Tham số:**
*   `pos`: Tọa độ (Vector3) tâm đáy của vật thể (điểm chạm đất).
*   `shape`: Kiểu hình dáng (`ENV_SHAPE_SPHERE`, `ENV_SHAPE_CYLINDER`, `ENV_SHAPE_BOX`).
*   `width`: Bề ngang của vật thể.
*   `height`: Chiều cao của vật thể (rất quan trọng để tính toán độ thuôn dài của bóng Capsule).

**Tính năng nổi bật:**
*   **Shadow Scaling & Fading:** Hệ thống sẽ tự động đo lường độ cao `pos.y` của vật thể. Nếu vật thể bay lên cao, bóng sẽ tự động thu nhỏ lại và mờ nhạt dần đi cực kỳ thực tế.
*   **Directional Accuracy:** Bóng đổ luôn ngả chính xác theo hướng chiếu của mặt trời (`s_sunDirection`). Hướng mặc định hiện tại là Tây Nam.
*   **Soft Edges:** Bóng không bị sắc cạnh hay chồng lấp nét đứt (Double-blending), mép bóng mờ dần ra không gian.

**Ví dụ sử dụng trong Skill:**
```c
// Trong hàm Draw của Seismic Pillars (Cột đá):
Environment_DrawSmartShadow(pillarPos, ENV_SHAPE_CYLINDER, 15.0f, 50.0f);
```

---

## 4. Các hàm thiết lập & lấy thông số (Getter / Setter)

Các hàm này cho phép các hệ thống khác (ví dụ: TimeOfDay System, Weather System) can thiệp thay đổi ánh sáng, hướng nắng, màu bóng râm theo thời gian thực (real-time).

```c
// --- Hướng Mặt Trời ---
Vector3 Environment_GetSunDirection(void);
void Environment_SetSunDirection(Vector3 dir); // Tự động chuẩn hóa vector (Normalize)

// --- Màu Ánh Sáng Mặt Trời ---
Color Environment_GetSunColor(void);
void Environment_SetSunColor(Color col);

// --- Màu Môi Trường Khất Sáng (Ambient) ---
Color Environment_GetAmbientColor(void);
void Environment_SetAmbientColor(Color col);

// --- Màu Bóng Râm (Shadow) ---
Color Environment_GetShadowColor(void);
void Environment_SetShadowColor(Color col);

// --- Sương Mù (Fog) ---
EnvFogConfig Environment_GetFogConfig(void);
void Environment_SetFogConfig(EnvFogConfig config);
```
