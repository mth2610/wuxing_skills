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

// --- Ambient bán cầu (Real Shading P1c) — derived from the flat ambient
// above; feeds surface_lit's hemispheric term (sky above / ground bounce below).
Color Environment_GetSkyAmbient(void);    // = ambient * 1.25 (cooler-tinted blue channel)
Color Environment_GetGroundAmbient(void); // = ambient * ~0.5 (dimmer, slight warm shift)

// --- Màu Bóng Râm (Shadow) ---
Color Environment_GetShadowColor(void);
void Environment_SetShadowColor(Color col);

// --- Sương Mù (Fog) ---
EnvFogConfig Environment_GetFogConfig(void);
void Environment_SetFogConfig(EnvFogConfig config);
```

---

## 5. Chu kỳ ánh sáng ngày/đêm (Time-of-Day)

Hệ thống blend keyframe theo thời gian, dùng để tạo map có ánh sáng chuyển động qua các mốc (bình minh → trưa → hoàng hôn → đêm) mà **không cần texture/geometry riêng cho ngày và đêm** — chỉ ánh sáng (ambient, sun color/direction, shadow color, fog) thay đổi.

**Hoàn toàn opt-in / backward-compatible:** nếu không gọi `Environment_SetTimeOfDayPresets()` (hoặc gọi nhưng để speed = 0, mặc định), `Environment_Update()` không làm gì khác so với trước — các lời gọi `Environment_Set*()` tĩnh một-lần trong `Init()` của map vẫn có hiệu lực đầy đủ, không bị hệ thống này ghi đè.

```c
#define MAX_TIME_OF_DAY_PRESETS 8

typedef struct {
    Color        ambientColor;
    Color        sunColor;
    Vector3      sunDirection;
    Color        shadowColor;
    EnvFogConfig fog;
} EnvLightingPreset;

// Khai báo các keyframe (mốc thời gian) cho một chu kỳ ánh sáng đầy đủ.
void  Environment_SetTimeOfDayPresets(const EnvLightingPreset *presets, const float *timePoints, int count);

// Tốc độ chu kỳ, đơn vị: chu kỳ/giây. Mặc định 0 = tạm dừng/tắt.
void  Environment_SetTimeOfDaySpeed(float cyclesPerSecond);

// Nhảy thủ công tới một mốc thời gian trong chu kỳ.
void  Environment_SetTimeOfDay(float t);
float Environment_GetTimeOfDay(void);
```

**Tham số & ràng buộc:**
*   `timePoints`: giá trị chuẩn hóa `[0,1)`, **phải sắp xếp tăng dần**, số lượng `<= MAX_TIME_OF_DAY_PRESETS` (8).
*   **Chu kỳ khép vòng (wrap-around):** đoạn từ `timePoints[count-1]` qua mốc `1.0`/`0.0` rồi tới `timePoints[0]` cũng được nội suy mượt, **không phải là một cú cắt cứng** về preset đầu.
*   Gọi `Environment_SetTimeOfDayPresets()` sẽ **ghi đè** toàn bộ preset đã set trước đó.
*   `Environment_SetTimeOfDaySpeed(0)` (mặc định) hoặc chưa từng gọi `SetTimeOfDayPresets` → hệ thống hoàn toàn "trơ" (inert), `Environment_Update()` không đổi hành vi so với trước khi có tính năng này.
*   **Ràng buộc `fog.enabled`:** đây là `bool`, không thể nội suy tuyến tính được. **Tất cả preset truyền vào cùng một lần `SetTimeOfDayPresets()` phải đồng nhất giá trị `fog.enabled`** (toàn bộ `true` hoặc toàn bộ `false`). Khi blend, hệ thống chỉ lấy `fog.enabled` từ một trong hai preset đang được nội suy — nếu bạn trộn `true`/`false` giữa các preset, hành vi bật/tắt sương mù sẽ nhảy tùy tiện giữa hai preset kề nhau thay vì mượt.
*   Khi blend, `sunDirection` được nội suy tuyến tính theo từng thành phần rồi `Normalize()` lại (giống quy ước của `Environment_SetSunDirection`); các `Color` (bao gồm `fog.color`) được lerp theo từng kênh byte; `fog.start`/`fog.end`/`fog.density` lerp dạng float.
*   Kết quả blend được ghi thẳng vào cùng state tĩnh mà `Environment_DrawSmartShadow()` và các Getter ở mục 4 đang đọc — không cần đổi gì ở nơi khác.

**Ví dụ sử dụng (map muốn chu kỳ ngày/đêm 20 phút thực):**
```c
EnvLightingPreset presets[3] = {
    { .ambientColor = {50,50,70,255}, .sunColor = {255,245,230,255}, .sunDirection = {0.5f,-0.8f,-0.3f}, .shadowColor = {8,8,12,180}, .fog = {.enabled = false} }, // noon
    { .ambientColor = {30,20,40,255}, .sunColor = {255,140,80,255},  .sunDirection = {0.9f,-0.2f,-0.1f}, .shadowColor = {8,8,12,180}, .fog = {.enabled = false} }, // dusk
    { .ambientColor = {10,10,25,255}, .sunColor = {60,70,120,255},   .sunDirection = {-0.3f,-0.6f,0.4f}, .shadowColor = {4,4,8,180},  .fog = {.enabled = false} }, // night
};
float times[3] = { 0.0f, 0.4f, 0.7f };
Environment_SetTimeOfDayPresets(presets, times, 3);
Environment_SetTimeOfDaySpeed(1.0f / 1200.0f); // 1 chu kỳ / 20 phút thực
```
