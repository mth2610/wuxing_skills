# Wuxing Skills - Map Creator API Documentation

Tài liệu này cung cấp hướng dẫn chi tiết, đặc tả kỹ thuật và khung mã nguồn mẫu để tạo mới một bản đồ (Map Plugin) trong engine **Wuxing Skills**. 

Nếu bạn là một AI Agent hoặc Developer, bạn chỉ cần đọc kỹ tài liệu này, tạo thư mục map tương ứng, điền code và chạy biên dịch `make`. Hệ thống tự động hóa sẽ tự nhận diện và đăng ký map mới vào trò chơi.

---

## 1. Cấu Trúc Thư Mục & Đặt Tên (Bắt Buộc)

Engine sử dụng script quét tự động `generate_map_registry.py`. Để được đăng ký thành công, mỗi map bắt buộc phải tuân theo quy tắc:

1. Phải nằm trong một thư mục con riêng dưới thư mục `maps/`.
2. Tên thư mục con và tên file `.c`/`.h` phải trùng khớp với nhau.
   - Định dạng: `maps/<map_name>/<map_name>.c` và `maps/<map_name>/<map_name>.h`
   - Ví dụ: `maps/desert_lava/desert_lava.c` và `maps/desert_lava/desert_lava.h`

---

## 2. Quy Tắc Khai Báo Trong File Header `.h`

Script quét tự động sẽ đọc nội dung file `.h` để tìm tiếp đầu ngữ (prefix).
* Tất cả các hàm phải sử dụng chung một dạng viết Hoa/Thường (CamelCase) thống nhất cho Prefix.
* **Bắt buộc** phải khai báo tối thiểu 2 hàm `Init{Prefix}Map` và `Draw{Prefix}Map` trong file `.h`.

### Khung file header chuẩn:
```c
#ifndef DESERT_LAVA_MAP_H
#define DESERT_LAVA_MAP_H

// Bắt buộc phải có
void InitDesertLavaMap(void);
void DrawDesertLavaMap(void);

// Tùy chọn (nếu có, script sẽ tự động đăng ký)
void UpdateDesertLavaMap(float dt);
void UnloadDesertLavaMap(void);

#endif // DESERT_LAVA_MAP_H
```
*Lưu ý: Nếu trong file `.h` có chuỗi `UpdateDesertLavaMap` và `UnloadDesertLavaMap`, hệ thống sẽ tự động gọi chúng trong vòng lặp chính của engine.*

---

## 3. Quy Hoạch Không Gian & Tọa Độ Đấu Trường

Các map được vẽ dưới dạng một "hòn đảo lơ lửng" để hỗ trợ cơ chế rơi vực (Ring Out) theo trục Z. Khi vẽ map, hãy sử dụng các hằng số tọa độ sau để đồng bộ với logic di chuyển và va chạm của người chơi/quái vật:

* **Tâm Đấu Trường:** `(600.0f, 0.0f, 440.0f)` (Hằng số `arenaCenter`).
* **Bán Kính Hoạt Động:** `1800.0f` (Hằng số `arenaRadius`). Toàn bộ mặt đất chơi được nên nằm trong bán kính này.
* **Cao Độ Mặc Định (Y):** Mặt đất chính nằm ở cao độ `Y = 0.0f` (hoặc chênh lệch rất nhỏ như `-0.05f`).

---

## 4. API Hệ Thống Môi Trường & Ánh Sáng (`environment_system.h`)

Để tạo bầu không khí ban đêm ma mị hoặc thay đổi màu sắc ánh trăng, sương mù, hãy sử dụng các hàm sau trong hàm `Init`:

### Thiết lập ánh sáng & Sương mù:
* `void Environment_SetAmbientColor(Color col)`: Màu môi trường khuất sáng (ví dụ: Xanh đen thẳm cho ban đêm).
* `void Environment_SetSunColor(Color col)`: Màu ánh trăng/nắng chính chiếu xuống vật thể.
* `void Environment_SetSunDirection(Vector3 dir)`: Hướng chiếu của ánh trăng (tự động chuẩn hóa).
* `void Environment_SetShadowColor(Color col)`: Màu sắc và độ đậm nhạt của bóng đổ.
* `void Environment_SetFogConfig(EnvFogConfig config)`: Cấu hình sương mù.
  ```c
  typedef struct {
      Color color;    // Màu sương mù (thường trùng màu Ambient)
      float start;    // Khoảng cách bắt đầu mờ sương (tính từ Camera)
      float end;      // Khoảng cách sương mù đặc hoàn toàn
      float density;  // Mật độ (thường để 1.0f)
      bool enabled;   // Bật/tắt sương mù
  } EnvFogConfig;
  ```

### Thiết lập Đổ Bóng Giả (Smart Fake Shadow) - Cực Kỳ Quan Trọng:
Để tối ưu hóa trên di động, engine **cấm sử dụng đổ bóng thời gian thực (Real-time Shadows)**. Mọi chướng ngại vật tĩnh trên map (cột đá, thân cây, tảng đá) phải được vẽ bóng giả nằm sát đất bằng cách gọi hàm sau **trước khi** vẽ mô hình 3D của vật thể đó:
```c
void Environment_DrawSmartShadow(Vector3 pos, EnvShadowShapeType shape, float width, float height);
```
*   `pos`: Tọa độ chân của vật thể (nơi chạm đất).
*   `shape`: Kiểu hình dáng (`ENV_SHAPE_SPHERE`, `ENV_SHAPE_CYLINDER`, `ENV_SHAPE_BOX`).
*   `width`: Đường kính/Bề ngang.
*   `height`: Chiều cao (dùng để tính toán độ kéo dài của bóng theo hướng nắng).

---

## 5. Nguyên Tắc Thiết Kế Đồ Họa & Vẽ Mesh

1. **Thẩm mỹ (Aesthetics):** Phù hợp với phong cách ban đêm huyền bí. Tông màu trầm ấm, đá nâu xám, kết hợp các điểm phát sáng nhẹ từ mạch ngầm nguyên tố.
2. **Low-Poly & Flat Shading:** 
   - Ưu tiên sử dụng các hình khối cơ bản của Raylib (`DrawCylinder`, `DrawSphere`, `DrawCube`).
   - Cylinder nên được vẽ với số lượng mặt nhỏ (ví dụ `segments = 8` hoặc `16`) để lộ thớ cạnh sắc lẹm đặc trưng của Low-Poly.
3. **Kỷ Luật Alpha (Bắt buộc):**
   - **Tuyệt đối KHÔNG vẽ vật thể trên bản đồ có giá trị Alpha nhỏ hơn 255** (ví dụ vẽ hồ nước trong suốt có alpha = 200).
   - *Lý do:* Engine render toàn bộ cảnh 3D vào một Canvas tổng duy nhất. Nếu ghi đè Alpha < 255 lên Canvas này, các hiệu ứng hạt (Particles) vẽ chồng lên sau đó sẽ bị lỗi tạo các ô vuông đen xám bao quanh rất xấu.
   - *Giải pháp:* Vẽ mặt nước mờ đục hoàn toàn (Alpha = 255), sử dụng các tông màu sẫm pha xanh lục/xanh dương để mô phỏng độ sâu của nước.

---

## 6. Khung File Source Mẫu Hoàn Chỉnh (`desert_lava.c`)

Dưới đây là một ví dụ hoàn chỉnh về file `.c` của một map chủ đề Sa Mạc Dung Nham để bạn copy và tinh chỉnh:

```c
#include "desert_lava.h"
#include "raylib.h"
#include "rlgl.h"
#include "environment/environment_system.h"
#include <math.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

// Biến lưu thời gian hoạt ảnh (sóng nước/dung nham cuộn)
static float s_lavaTime = 0.0f;

void InitDesertLavaMap(void) {
    // 1. Cấu hình không khí đêm dung nham (Ambient xanh đen, ánh trăng vàng nhạt)
    Environment_SetAmbientColor((Color){ 15, 10, 20, 255 }); 
    Environment_SetSunColor((Color){ 90, 70, 50, 255 });    
    Environment_SetSunDirection((Vector3){ 0.5f, -0.8f, -0.3f }); 
    Environment_SetShadowColor((Color){ 5, 2, 8, 200 });

    // 2. Thiết lập sương mù màu đỏ tối bốc lên từ magma
    EnvFogConfig fog = {0};
    fog.enabled = true;
    fog.color = (Color){ 25, 10, 10, 255 }; 
    fog.start = 700.0f;
    fog.end = 2000.0f;
    fog.density = 1.0f;
    Environment_SetFogConfig(fog);
}

void DrawDesertLavaMap(void) {
    Vector3 center = { 600.0f, 0.0f, 440.0f };
    float radius = 1805.0f;
    float poolRadius = 300.0f;

    // --- BƯỚC 1: VẼ NỀN ĐẤT CÁT LƠ LỬNG ---
    rlDisableBackfaceCulling();
    rlBegin(RL_TRIANGLES);
    Color cSandCenter = GetColor(0x3A2518FF); // Màu đất cát đêm
    Color cSandEdge = GetColor(0x1A0F0AFF);   // Tối dần ra ngoài rìa
    int segments = 64;
    for (int i = 0; i < segments; i++) {
        float a1 = ((float)i / segments) * 2.0f * PI;
        float a2 = ((float)(i + 1) / segments) * 2.0f * PI;
        Vector3 p1 = { center.x + cosf(a1) * radius, center.y - 0.1f, center.z + sinf(a1) * radius };
        Vector3 p2 = { center.x + cosf(a2) * radius, center.y - 0.1f, center.z + sinf(a2) * radius };
        
        rlColor4ub(cSandCenter.r, cSandCenter.g, cSandCenter.b, 255);
        rlVertex3f(center.x, center.y - 0.1f, center.z);
        rlColor4ub(cSandEdge.r, cSandEdge.g, cSandEdge.b, 255);
        rlVertex3f(p2.x, p2.y, p2.z);
        rlColor4ub(cSandEdge.r, cSandEdge.g, cSandEdge.b, 255);
        rlVertex3f(p1.x, p1.y, p1.z);
    }
    rlEnd();

    // --- BƯỚC 2: VẼ HỒ DUNG NHAM Ở GIỮA (ĐỤC HOÀN TOÀN, ALPHA = 255) ---
    rlBegin(RL_TRIANGLES);
    Color cLavaCenter = GetColor(0xFF5500FF); // Cam rực sáng ở giữa
    Color cLavaEdge = GetColor(0x8B0000FF);   // Đỏ thẫm viền hồ
    for (int i = 0; i < segments; i++) {
        float a1 = ((float)i / segments) * 2.0f * PI;
        float a2 = ((float)(i + 1) / segments) * 2.0f * PI;
        // Hoạt ảnh gợn sóng dung nham nhấp nhô nhẹ
        float w1 = sinf(a1 * 6.0f + s_lavaTime) * 8.0f;
        float w2 = sinf(a2 * 6.0f + s_lavaTime) * 8.0f;
        Vector3 p1 = { center.x + cosf(a1) * (poolRadius + w1), center.y, center.z + sinf(a1) * (poolRadius + w1) };
        Vector3 p2 = { center.x + cosf(a2) * (poolRadius + w2), center.y, center.z + sinf(a2) * (poolRadius + w2) };
        
        rlColor4ub(cLavaCenter.r, cLavaCenter.g, cLavaCenter.b, 255);
        rlVertex3f(center.x, center.y, center.z);
        rlColor4ub(cLavaEdge.r, cLavaEdge.g, cLavaEdge.b, 255);
        rlVertex3f(p2.x, p2.y, p2.z);
        rlColor4ub(cLavaEdge.r, cLavaEdge.g, cLavaEdge.b, 255);
        rlVertex3f(p1.x, p1.y, p1.z);
    }
    rlEnd();
    rlEnableBackfaceCulling();

    // --- BƯỚC 3: VẼ VẬT THỂ TĨNH & ĐỔ BÓNG GIẢ ---
    Vector3 pillarPos = { center.x + 350.0f, center.y, center.z - 250.0f };
    float pHeight = 80.0f;
    float pRadius = 20.0f;
    // 1. Vẽ bóng đổ trước
    Environment_DrawSmartShadow(pillarPos, ENV_SHAPE_CYLINDER, pRadius, pHeight);
    // 2. Vẽ cột đá Low-Poly dạng lăng trụ bát giác (segments = 8)
    DrawCylinder((Vector3){pillarPos.x, pillarPos.y + pHeight * 0.5f, pillarPos.z}, pRadius, pRadius, pHeight, 8, GetColor(0x2D1E18FF));
    DrawCylinderWires((Vector3){pillarPos.x, pillarPos.y + pHeight * 0.5f, pillarPos.z}, pRadius, pRadius, pHeight, 8, GetColor(0x4A352BFF));

    // --- BƯỚC 4: VẼ MẶT TRĂNG PHƯƠNG XA (Mặt trăng máu) ---
    Vector3 moonPos = { center.x - 600.0f, 300.0f, center.z - 1200.0f };
    rlDisableLighting();
    DrawSphere(moonPos, 150.0f, GetColor(0xFF4400FF)); // Trăng máu rực rỡ
    rlEnableLighting();
}

void UpdateDesertLavaMap(float dt) {
    // Cập nhật thời gian trôi của magma
    s_lavaTime += dt * 1.5f;
}

void UnloadDesertLavaMap(void) {
    // Dọn dẹp tài nguyên (nếu có load texture ngoài)
}
```

---

## 7. Các Bước Tạo & Đưa Map Vào Game (Cho AI/Dev)

Khi muốn thêm map mới, bạn chỉ cần thực hiện các bước sau:

1. **Bước 1:** Tạo một thư mục mới trùng tên map dưới `maps/` (Ví dụ: `maps/desert_lava/`).
2. **Bước 2:** Tạo file `.h` và `.c` trong thư mục đó theo mẫu ở mục 2 và mục 6.
3. **Bước 3:** Chạy lệnh `make` ở terminal gốc của project.
   - Trình biên dịch CMake sẽ tự động chạy script `generate_map_registry.py` để phát hiện map mới, sinh mã đăng ký vào `core/maps_generated.h` và biên dịch liên kết vào game.
4. **Kiểm tra:** Mở game lên (`./wuxing`), bạn có thể nhấn phím **`K`** để chuyển đổi qua lại giữa các map để test xem map mới của bạn hiển thị như thế nào!

---

## 8. Hướng Dẫn Nạp & Đưa Mô Hình 3D (3D Models) Vào Map

Nếu bạn đã tải về các tệp mô hình 3D (như cây tre `.obj`, ngôi nhà `.gltf` kèm texture), hãy đưa chúng vào map theo quy trình sau:

### Bước 1: Lưu trữ tệp mô hình
Tạo thư mục lưu trữ trong dự án, ví dụ:
- Mô hình: `assets/models/bamboo.obj` (hoặc `.gltf`)
- Texture đi kèm: `assets/textures/bamboo_diffuse.png`

### Bước 2: Khai báo và Nạp trong File C
Bạn khai báo biến tĩnh `static Model` ở đầu tệp map `.c` của mình để lưu trữ tài nguyên nạp vào VRAM, nạp nó trong `Init` và giải phóng trong `Unload`.

```c
#include "desert_lava.h"
#include "raylib.h"
#include "environment/environment_system.h"

static Model s_bambooModel;
static bool s_bambooLoaded = false;

void InitDesertLavaMap(void) {
    // Nạp mô hình 3D từ assets
    s_bambooModel = LoadModel("assets/models/bamboo.obj");
    
    // Nếu mô hình dùng texture ngoài, nạp và gán vào vật liệu của mô hình
    if (s_bambooModel.meshCount > 0) {
        Texture2D tex = LoadTexture("assets/textures/bamboo_diffuse.png");
        s_bambooModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = tex;
    }
    s_bambooLoaded = true;
    
    // Cấu hình môi trường khác...
}

void DrawDesertLavaMap(void) {
    if (s_bambooLoaded) {
        Vector3 treePos = { 500.0f, 0.0f, 300.0f };
        
        // 1. Vẽ bóng đổ giả sát mặt đất
        Environment_DrawSmartShadow(treePos, ENV_SHAPE_CYLINDER, 15.0f, 60.0f);
        
        // 2. Vẽ mô hình 3D cây tre (Dùng DrawModelEx để hỗ trợ xoay/tỉ lệ)
        Vector3 rotationAxis = { 0.0f, 1.0f, 0.0f }; // Xoay quanh trục đứng Y
        float rotationAngle = 45.0f; // Xoay 45 độ
        Vector3 scale = { 1.5f, 1.5f, 1.5f }; // Tỉ lệ phóng to
        
        DrawModelEx(s_bambooModel, treePos, rotationAxis, rotationAngle, scale, WHITE);
    }
}

void UnloadDesertLavaMap(void) {
    if (s_bambooLoaded) {
        // Giải phóng texture đã gán vào vật liệu trước
        UnloadTexture(s_bambooModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture);
        // Giải phóng mô hình 3D khỏi VRAM
        UnloadModel(s_bambooModel);
        s_bambooLoaded = false;
    }
}
```

---

## 9. Thiết Kế Địa Hình Lồi Lõm (Terrain & Heightmap) & Giới Hạn Vật Lý

Để tạo địa hình lồi lõm, sườn đồi hoặc các hố sụt lún, bạn cần lưu ý phân biệt giữa **Hình ảnh hiển thị (Graphics)** và **Vật lý di chuyển (Physics/Collision)** của Engine hiện tại:

### A. Cơ Chế Vật Lý Hiện Tại (Giới hạn của Core)
Hiện tại, động cơ vật lý cốt lõi của nhân vật và quái vật trong `sandbox_core.c` hoạt động dựa trên giả định:
*   Mặt sàn đấu trường mặc định là phẳng tuyệt đối ở độ cao **`Y = 0.0f`**.
*   Các nền tảng cao (Platform) được khai báo tĩnh thông qua mảng các Cột Đá (`pillars`): Khi nhân vật nhảy lên đỉnh cột đá thì độ cao sàn đứng của họ (`currentGroundY`) mới được nâng lên bằng chiều cao cột đó.
*   *Lưu ý:* Map plugin chỉ quyết định **phần vẽ đồ họa**. Bản thân map không thể tự thay đổi thuật toán va chạm của Core trừ khi Core được cập nhật thêm API Heightmap.

### B. Giải pháp 1: Địa hình lồi lõm dạng Đồ họa thuần túy (Visual-only Hills)
Nếu bạn chỉ muốn người chơi di chuyển bằng phẳng ở `Y = 0` nhưng phần nhìn có các mô đất nhấp nhô lồi lõm:
1. Bạn vẽ các mô đất bằng cách rải các hình cầu (`DrawSphere`), hình hộp (`DrawCube`) nửa chìm nửa nổi dưới lòng đất.
2. Hoặc nạp một tệp lưới 3D địa hình gồ ghề (Heightmap Mesh) vẽ đè lên cao độ `Y = 0`.
*Ưu điểm:* Cực kỳ nhẹ, dễ vẽ, không sợ lỗi kẹt nhân vật. Nhân vật sẽ lướt mượt mà xuyên qua/trên các thớ đất nhấp nhô nhẹ.

### C. Giải pháp 2: Tạo Hố/Vực Thẳm (Holes & Cliffs)
Để tạo các hố sụt lún mà người chơi **rơi vào sẽ bị trọng lực kéo tuột xuống vực và chết**:
1. Trong hàm `Draw`, bạn vẽ địa hình có chừa ra các lỗ rỗng (không vẽ lưới đa giác ở khu vực đó, để lộ khoảng không tối).
2. Vì Core hiện tại chỉ check rơi vực khi người chơi vượt quá bán kính `arenaRadius` (`1800.0f` tính từ tâm `600, 440`), nếu bạn muốn tạo các hố chết người *ở giữa* map, bạn sẽ cần phối hợp cập nhật thêm logic kiểm tra va chạm hình tròn của hố đó trong tệp vật lý của Core (hoặc báo với Core Agent thiết lập thêm vùng cấm đi `NAT_CLIFF` gây chết).

---

## 10. Nạp Texture Cho Bản Đồ (Đá, Cát, Nước)

Khi bạn muốn phủ texture hình ảnh (ví dụ: texture đá `stone.png`, đất cỏ `grass.png`), bạn có hai cách áp dụng tùy theo cách bạn vẽ địa hình:

### A. Gán Texture vào Mô hình 3D (Model) tải từ ngoài
```c
static Model s_rockModel;
static bool s_rockLoaded = false;

void InitMap(void) {
    s_rockModel = LoadModel("assets/models/rock.obj");
    
    // Nạp texture đá từ tệp ảnh
    Texture2D rockTex = LoadTexture("assets/textures/stone_diffuse.png");
    
    // Gán texture vào kênh màu chính (Diffuse Map) của vật liệu số 0
    s_rockModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = rockTex;
    s_rockLoaded = true;
}

void DrawMap(void) {
    if (s_rockLoaded) {
        DrawModel(s_rockModel, (Vector3){ 600.0f, 0.0f, 440.0f }, 1.0f, WHITE);
    }
}

void UnloadMap(void) {
    if (s_rockLoaded) {
        // Bắt buộc phải unload texture trước khi unload model để tránh rò rỉ VRAM
        UnloadTexture(s_bambooModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture);
        UnloadModel(s_rockModel);
    }
}
```

### B. Áp Texture trực tiếp lên hình khối tự vẽ bằng `rlgl` (Ví dụ: Mặt đất)
Nếu vẽ sàn đấu bằng `rlgl`, bạn cần gán ID texture và gán tọa độ UV (`rlTexCoord2f`) cho từng đỉnh:
```c
static Texture2D s_groundTex;

void InitMap(void) {
    s_groundTex = LoadTexture("assets/textures/grass_texture.png");
    // Thiết lập lặp texture (Wrap Mode) nếu muốn texture tự lặp lại trên diện tích rộng
    SetTextureWrap(s_groundTex, TEXTURE_WRAP_REPEAT);
}

void DrawMap(void) {
    rlSetTexture(s_groundTex.id); // Kích hoạt texture
    rlBegin(RL_TRIANGLES);
        // Đỉnh 1
        rlTexCoord2f(0.0f, 0.0f); // Tọa độ UV (0,0)
        rlVertex3f(500.0f, 0.0f, 300.0f);
        
        // Đỉnh 2
        rlTexCoord2f(1.0f, 0.0f); // Tọa độ UV (1,0)
        rlVertex3f(600.0f, 0.0f, 300.0f);
        
        // Đỉnh 3
        rlTexCoord2f(0.5f, 1.0f); // Tọa độ UV (0.5,1)
        rlVertex3f(550.0f, 0.0f, 400.0f);
    rlEnd();
    rlSetTexture(0); // Tắt texture sau khi vẽ xong
}
```

---

## 11. Tối Ưu Hóa Bộ Nhớ: Vẽ Cả Rừng/Thảm Hoa Từ 1 Model Duy Nhất

**Tuyệt đối KHÔNG** gọi hàm `LoadModel` nhiều lần cho từng cây tre hay từng bông hoa. Điều này sẽ làm tràn bộ nhớ VRAM và gây crash game.
*   **Giải pháp:** Chỉ nạp model **đúng 1 lần** trong `Init` để lưu vào bộ nhớ đệm, sau đó trong hàm `Draw`, hãy dùng vòng lặp `for` để vẽ model đó ra nhiều vị trí khác nhau với tỉ lệ và góc xoay ngẫu nhiên để tạo thành rừng hoặc thảm hoa.

### Code mẫu tạo Rừng Tre từ 1 Model:
```c
#define MAX_BAMBOO_TREES 30

static Model s_bambooModel;
static Vector3 s_bambooPositions[MAX_BAMBOO_TREES];
static float s_bambooRotations[MAX_BAMBOO_TREES];
static float s_bambooScales[MAX_BAMBOO_TREES];
static bool s_modelReady = false;

void InitMap(void) {
    s_bambooModel = LoadModel("assets/models/bamboo.obj");
    s_modelReady = true;

    // Sinh ngẫu nhiên vị trí rừng tre một lần duy nhất tại đây (tránh sinh trong hàm Draw gây giật lag)
    for (int i = 0; i < MAX_BAMBOO_TREES; i++) {
        // Sinh tọa độ xung quanh rìa bản đồ
        float angle = ((float)i / MAX_BAMBOO_TREES) * 2.0f * PI;
        float radius = 1000.0f + (float)GetRandomValue(-200, 200); 
        s_bambooPositions[i] = (Vector3){
            600.0f + cosf(angle) * radius,
            0.0f,
            440.0f + sinf(angle) * radius
        };
        
        s_bambooRotations[i] = (float)GetRandomValue(0, 360);
        s_bambooScales[i] = 1.0f + (float)GetRandomValue(-20, 20) / 100.0f; // Tỉ lệ 0.8f đến 1.2f
    }
}

void DrawMap(void) {
    if (!s_modelReady) return;

    Vector3 rotationAxis = { 0.0f, 1.0f, 0.0f }; // Xoay quanh trục Y

    // Vòng lặp vẽ cả khu rừng
    for (int i = 0; i < MAX_BAMBOO_TREES; i++) {
        // 1. Vẽ bóng đổ tương ứng với vị trí cây đó
        Environment_DrawSmartShadow(s_bambooPositions[i], ENV_SHAPE_CYLINDER, 15.0f, 80.0f);
        
        // 2. Vẽ mô hình cây tre tại vị trí đó
        Vector3 scaleVec = { s_bambooScales[i], s_bambooScales[i], s_bambooScales[i] };
        DrawModelEx(s_bambooModel, s_bambooPositions[i], rotationAxis, s_bambooRotations[i], scaleVec, WHITE);
    }
}

void UnloadMap(void) {
    if (s_modelReady) {
        UnloadModel(s_bambooModel);
        s_modelReady = false;
    }
}
```
*Tương tự, với thảm cỏ hoặc thảm hoa, bạn chỉ cần nạp 1 model bông hoa/ngọn cỏ nhỏ, sau đó chạy vòng lặp sinh ngẫu nhiên hàng trăm điểm cạnh nhau để phủ xanh bề mặt đấu trường.*

---

## 12. Bám Dính Vật Thể Lên Địa Hình Gồ Ghề (Snapping to Terrain)

Khi bản đồ không còn phẳng mà có đồi núi lồi lõm (Heightmap), bạn **không thể** để tọa độ của cây hay nhà cố định ở `Y = 0.0f` vì chúng sẽ bị lơ lửng trên không trung hoặc bị chôn vùi dưới đất.

### Giải pháp: Truy vấn độ cao từ tệp ảnh Heightmap (CPU Height Query)
Ta sử dụng một hàm helper bằng C để đọc giá trị pixel của tệp ảnh Heightmap (ảnh xám thể hiện độ cao: màu trắng là đỉnh núi, màu đen là thung lũng) tại tọa độ `(X, Z)` và quy đổi nó ra độ cao `Y` thực tế.

#### Hàm Helper tính độ cao:
```c
// Lấy độ cao Y tại tọa độ XZ của thế giới dựa trên ảnh Heightmap
float GetHeightmapHeight(Image heightmap, Vector3 terrainSize, Vector3 terrainCenter, float x, float z) {
    float halfWidth = terrainSize.x / 2.0f;
    float halfLength = terrainSize.z / 2.0f;
    
    // 1. Chuyển đổi tọa độ World XZ sang tọa độ Pixel (U, V) của ảnh
    float normX = (x - (terrainCenter.x - halfWidth)) / terrainSize.x;
    float normZ = (z - (terrainCenter.z - halfLength)) / terrainSize.z;
    
    int pixelX = (int)(normX * heightmap.width);
    int pixelZ = (int)(normZ * heightmap.height);
    
    // 2. Giới hạn tọa độ pixel nằm trong biên của bức ảnh
    if (pixelX < 0) pixelX = 0;
    if (pixelX >= heightmap.width) pixelX = heightmap.width - 1;
    if (pixelZ < 0) pixelZ = 0;
    if (pixelZ >= heightmap.height) pixelZ = heightmap.height - 1;
    
    // 3. Đọc màu của pixel tại đó (giả sử ảnh Grayscale, lấy giá trị Kênh Đỏ R từ 0 - 255)
    Color pixel = GetImageColor(heightmap, pixelX, pixelZ);
    
    // 4. Quy đổi giá trị 0-255 ra cao độ Y tương ứng với chiều cao tối đa của địa hình (terrainSize.y)
    float height = ((float)pixel.r / 255.0f) * terrainSize.y;
    return height;
}
```

#### Cách ứng dụng trong code của Map:
```c
static Model s_terrainModel;
static Model s_treeModel;
static Image s_heightmapImage;
static Vector3 s_terrainSize = { 1800.0f, 150.0f, 1800.0f }; // Rộng 1800, Cao tối đa 150, Dài 1800
static Vector3 s_center = { 600.0f, 0.0f, 440.0f };

// Tọa độ ngẫu nhiên của 10 cây tre
static Vector3 s_treePositions[10];

void InitMap(void) {
    // 1. Nạp lưới địa hình từ ảnh Heightmap để hiển thị 3D
    s_heightmapImage = LoadImage("assets/heightmaps/terrain_height.png");
    Mesh mesh = GenMeshHeightmap(s_heightmapImage, s_terrainSize);
    s_terrainModel = LoadModelFromMesh(mesh);
    s_terrainModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = LoadTexture("assets/textures/grass_diffuse.png");
    
    s_treeModel = LoadModel("assets/models/tree.obj");

    // 2. Rải cây tự động bám dính (snap) theo độ nhấp nhô của đồi núi
    for (int i = 0; i < 10; i++) {
        float x = s_center.x + (float)GetRandomValue(-600, 600);
        float z = s_center.z + (float)GetRandomValue(-600, 600);
        
        // Gọi hàm truy vấn độ cao từ ảnh heightmap để gán vào Y
        float y = GetHeightmapHeight(s_heightmapImage, s_terrainSize, s_center, x, z);
        
        s_treePositions[i] = (Vector3){ x, y, z };
    }
}

void DrawMap(void) {
    // Vẽ địa hình đồi núi gồ ghề
    DrawModel(s_terrainModel, (Vector3){ s_center.x - s_terrainSize.x/2.0f, 0.0f, s_center.z - s_terrainSize.z/2.0f }, 1.0f, WHITE);
    
    // Vẽ các cây tre đã được bám dính đúng độ cao
    for (int i = 0; i < 10; i++) {
        Environment_DrawSmartShadow(s_treePositions[i], ENV_SHAPE_CYLINDER, 15.0f, 50.0f);
        DrawModel(s_treeModel, s_treePositions[i], 1.0f, WHITE);
    }
}

void UnloadMap(void) {
    UnloadImage(s_heightmapImage); // Giải phóng dữ liệu ảnh CPU
    UnloadModel(s_terrainModel);
    UnloadModel(s_treeModel);
}
```



