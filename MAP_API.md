# Wuxing Skills - Map Creator API Documentation

Tài liệu này cung cấp hướng dẫn chi tiết, đặc tả kỹ thuật và khung mã nguồn mẫu để tạo mới một bản đồ (Map Plugin) trong engine **Wuxing Skills**. 

Nếu bạn là một AI Agent hoặc Developer, bạn chỉ cần đọc kỹ tài liệu này, tạo thư mục map tương ứng, điền code và chạy biên dịch `make`. Hệ thống tự động hóa sẽ tự nhận diện và đăng ký map mới vào trò chơi.

---

## 1. Cấu Trúc Thư Mục & Đặt Tên (Bắt Buộc)

`maps/` chia làm 2 phần:

```
maps/
    toolkit/            # Code dùng lại được (KHÔNG phải là 1 map) — xem mục 1b
    worlds/
        <map_name>/     # Mỗi map thành phẩm là 1 thư mục con ở đây
            <map_name>.h
            <map_name>.c
```

Engine sử dụng script quét tự động `generate_map_registry.py` — nó quét đệ quy toàn bộ `maps/` (không quan tâm độ sâu thư mục) để tìm `.h` khai báo `Init{Prefix}Map`/`Draw{Prefix}Map`, nên **map mới luôn phải nằm dưới `maps/worlds/`**, không đặt trực tiếp dưới `maps/`. Không cần sửa CMakeLists.txt hay script khi thêm map mới.

1. Phải nằm trong một thư mục con riêng dưới `maps/worlds/`.
2. Tên thư mục con và tên file `.c`/`.h` phải trùng khớp với nhau.
   - Định dạng: `maps/worlds/<map_name>/<map_name>.c` và `maps/worlds/<map_name>/<map_name>.h`
   - Ví dụ: `maps/worlds/desert_lava/desert_lava.c` và `maps/worlds/desert_lava/desert_lava.h`

---

## 1b. Map Toolkit (`maps/toolkit/`) — Đọc Trước Khi Tự Viết Code Vẽ Nền/Đá

**Đây là phần quan trọng nhất để AI có thể tạo map thành phẩm chỉ bằng cách đọc tài liệu này.** Trước khi tự tay viết `rlgl`/`DrawModel` cho nền đất, đường đi, hay đá rải rác — kiểm tra xem `maps/toolkit/map_props.h` đã có sẵn hàm cho việc đó chưa. Chỉ rơi xuống code thủ công (mục 9-12) cho những gì toolkit chưa có (địa hình đồi núi/heightmap, rừng cây từ model 3D, hồ dung nham...).

Toolkit gồm nhiều file, đều **Map Agent sở hữu** (không phải Core Agent, dù `prop_lit`/`grass_material` từng nằm ở `core/` — đã chuyển hẳn sang đây vì chỉ `maps/` dùng). Implementation của `map_props.h` được chia 1 file `.inl` cho mỗi loại prop (`map_props_ground.inl`, `map_props_strip.inl`, `map_props_rocks.inl`), `#include` lại từ `map_props.c` — thêm loại prop mới thì thêm 1 `.inl` mới + `#include` nó vào `map_props.c`, không viết thẳng vào `map_props.c`.

### `maps/toolkit/map_props.h` — bộ hàm chính, ưu tiên dùng đầu tiên

```c
// --- Ground plane (nền đất) ---
typedef struct { Model model; Vector3 drawOffset; bool ready; } MapGroundSurface;

MapGroundSurface MapProp_CreateGround(float width, float depth, float tileSize,
                                       const char *splatMapPath,
                                       const char *grassTexPath,
                                       const char *pathTexPath);
// Sloped-island variant: heightmapPath grayscale (WHITE=plateau, BLACK=cliff
// edge), cliffDepth = meters the edge sinks below the plateau. Same
// splat/grass/path textures + shader as MapProp_CreateGround.
MapGroundSurface MapProp_CreateGroundHeightmap(const char *heightmapPath, float width, float depth,
                                                float cliffDepth, float tileSize,
                                                const char *splatMapPath,
                                                const char *grassTexPath,
                                                const char *pathTexPath);
void MapProp_DrawGround(const MapGroundSurface *ground, Vector3 worldCenter);
void MapProp_UnloadGround(MapGroundSurface *ground);

// --- Flat strip (đường đá, lối đi, cầu) ---
typedef struct { Model model; bool ready; } MapStripSurface;

MapStripSurface MapProp_CreateStrip(float length, float width, float tileSize,
                                     const char *diffusePath,
                                     const char *normalPath,   // NULL = texture phẳng, không shader
                                     const char *roughnessPath); // truyền cả 3 path = dùng prop_lit
void MapProp_DrawStrip(const MapStripSurface *strip, Vector3 worldCenter, float yOffset);
void MapProp_UnloadStrip(MapStripSurface *strip);

// --- Rock props (1 model đá, rải nhiều vị trí) ---
typedef struct { Model model; bool ready; } MapRockSet;

typedef struct {
    Vector3 position;   // X/Z world; Y bị bỏ qua — đá tự chìm nửa xuống đất
    float   radiusScale; // tỉ lệ XZ
    float   heightScale; // tỉ lệ Y — dẹt xuống cho dáng tảng đá
    float   rotationDeg;
} MapRockPlacement;

MapRockSet MapProp_CreateRocks(const char *diffusePath,
                                const char *normalPath, const char *roughnessPath); // NULL/NULL = phẳng
void MapProp_DrawRocks(const MapRockSet *rocks, const MapRockPlacement *placements, int count);
void MapProp_UnloadRocks(MapRockSet *rocks);

// Sinh 1 vòng đá khổng lồ quanh viền map (mô-tuýp "đảo nổi giữa vách núi" mà
// mọi map dùng chung) — vẽ lại bằng CHÍNH MapProp_DrawRocks/MapRockSet ở
// trên, chỉ là radiusScale/heightScale lớn hơn hẳn. seed cố định layout.
int MapProp_GenerateMountainRing(MapRockPlacement *outPlacements, int maxCount,
                                  float mapWidth, float mapDepth,
                                  float minRadiusScale, float maxRadiusScale,
                                  float minHeightScale, float maxHeightScale,
                                  unsigned int seed);

// --- Sea of clouds (đáy vực dưới đảo nổi) ---
typedef struct { Model model; bool ready; } MapCloudSea;

MapCloudSea MapProp_CreateCloudSea(float width, float depth, float tileSize);
void MapProp_DrawCloudSea(const MapCloudSea *cloud, Vector3 worldCenter, float yOffset);
void MapProp_UnloadCloudSea(MapCloudSea *cloud);
```

* `MapProp_CreateGround` (`map_props_ground.inl`): `tileSize` = số mét thế giới cho mỗi lần `grassTexPath`/`pathTexPath` lặp lại. Vẽ bằng shader riêng `maps/toolkit/shaders/ground_splat.fs`: đọc kênh đỏ của `splatMapPath` làm mask (trắng = cỏ, đen = đất), trộn `grassTexPath`/`pathTexPath` theo mask đó, cộng thêm `grassDepth` để mép cỏ/đất hòa tự nhiên hơn. **Kiểm tra log khi thêm map mới** — đây là file đang được chỉnh tay thường xuyên, một lỗi cú pháp GLSL nhỏ (ví dụ ký tự lạc trước `#version`) khiến shader compile fail và raylib âm thầm rơi về shader mặc định (`WARNING: SHADER: ... Failed to compile fragment shader code` trong log) — ground vẫn vẽ được (không crash) nhưng nhìn sai hẳn so với ý đồ, không có thông báo nào khác ngoài log.
* `MapProp_CreateGroundHeightmap` (`map_props_ground.inl`): cùng shader/texture với `MapProp_CreateGround`, chỉ khác nguồn mesh — dùng `GenMeshHeightmap` thay vì `GenMeshPlane`, cho mặt đất lõm xuống thành vách ở viền thay vì phẳng lì. `heightmapPath` là ảnh grayscale: **trắng = cao nguyên phẳng đi được** (giữ ở local Y=0, đúng quy ước "mặt đất Y=0" của cả project), **đen = mép vách** (chìm xuống `cliffDepth` mét). Sinh ảnh heightmap bằng `python3 scripts/generate_island_heightmap.py <out.png> <size> <seed>` — mặc định script tạo **hình chữ nhật đơn giản, phẳng ở 90% diện tích giữa**, chỉ dải mỏng 10% ngoài viền mới lõm xuống thành vách (mildly jagged, không phải khối u lởm chởm to). Đừng chỉnh `plateau_edge`/`falloff_width` trong script quá thấp — mục tiêu là nội thất map phẳng, chỉ viền mới là vách. **`cliffDepth` phải nhỏ hơn (ít âm hơn) giá trị `yOffset` truyền cho `MapProp_DrawCloudSea`** — nếu không vách đá sẽ đâm xuyên qua mặt phẳng mây bên dưới. **Lưu ý implementation:** `GenMeshHeightmap` trả về mesh trải từ local `[0,width]x[0,depth]`, KHÔNG tự căn giữa như `GenMeshPlane` — việc căn giữa được làm bằng `MapGroundSurface.drawOffset` (cộng vào `worldCenter` lúc `DrawModel` trong `MapProp_DrawGround`), **không phải bằng cách sửa trực tiếp `mesh.vertices` sau khi tạo** — cách đó đã thử và chỉ render đúng 1/4 mesh (nguyên nhân gốc chưa rõ, không đáng để truy tiếp — offset-at-draw-time là cách né an toàn).
* `MapProp_CreateStrip` (`map_props_strip.inl`): vẽ bằng `maps/toolkit/shaders/path_blend.fs` — texture lặp theo `tiling` + noise phá viền hình học (`discard` cho alpha thấp ở 25% mép hai bên) để mép đường không bị thẳng cứng. `normalPath`/`roughnessPath` hiện **chưa dùng** (giữ trong chữ ký cho một biến thể prop_lit tương lai) — truyền `NULL` cho cả hai là đủ.
* `MapProp_CreateRocks` (`map_props_rocks.inl`): truyền `NULL` cho `normalPath`+`roughnessPath` để dùng texture phẳng đơn giản (không shader); truyền đủ cả 3 đường dẫn texture để dùng vật liệu `prop_lit` (có normal map + roughness, phản chiếu ánh sáng thật).
* `MapProp_GenerateMountainRing` (`map_props_rocks.inl`): chỉ SINH vị trí (ghi vào mảng `MapRockPlacement` do bạn cấp) — không tự vẽ, không tự tạo model riêng. Vẽ bằng `MapProp_DrawRocks(&s_rocks, s_mountainRocks, count)` giống hệt đá rải rác, dùng lại đúng 1 model/texture đá đã load — không cần thêm bộ texture "đá núi" riêng. `min/maxRadiusScale`/`min/maxHeightScale` nên lớn hơn hẳn đá thường (ví dụ 6-14 và 18-30, so với đá rải rác thường ~0.5-1.1) để đọc ra dáng vách núi chứ không phải đá lẻ.
* `MapProp_CreateCloudSea` (`map_props_cloud.inl`): thuần thủ tục (FBM noise cuộn theo thời gian qua `GetTime()`), **không cần texture**. Vẽ bằng `maps/toolkit/shaders/cloud_sea.fs` — dùng `discard` cho vùng mật độ thấp thay vì alpha-blend, vì `maps/CLAUDE.md` cấm alpha < 255 trong scene chính (vỡ particle). `width`/`depth` nên lớn hơn hẳn map để trông như biển mây vô tận, không phải 1 tấm bìa cắt cạnh lộ liễu. Chỉ thấy được khi đứng gần mép vách (`MapProp_CreateGroundHeightmap`) nhìn xuyên qua khe hở giữa các tảng đá viền — đứng giữa cao nguyên sẽ bị chính mặt đất che khuất, đúng như địa hình thật.
* Quy ước gọi: `Create*` gọi đúng 1 lần trong `Init{Prefix}Map`, `Draw*` gọi mỗi frame trong `Draw{Prefix}Map`, `Unload*` gọi trong `Unload{Prefix}Map` nếu map có khai báo hàm Unload. `MapProp_GenerateMountainRing` là ngoại lệ — nó không load tài nguyên gì, chỉ điền số vào mảng, nên gọi ở đâu trong `Init` cũng được, kể cả không có `Unload` tương ứng.
* Rock dùng `prop_lit` (đủ 3 path) cần gọi `PropLit_UpdateLighting()` **một lần mỗi frame trước khi vẽ** (xem ví dụ đầy đủ ở mục 6). Ground/strip/cloud sea tự đẩy uniform ánh sáng riêng trong `Draw*` của chính nó, không cần gọi gì thêm.

### `maps/toolkit/prop_lit.h` — vật liệu có ánh sáng thật cho đá/đường đi

```c
Shader   PropLit_GetShader(void);
Material PropLit_MakeMaterial(Texture2D diffuse, Texture2D normal, Texture2D roughness);
void     PropLit_UpdateLighting(void);
```
`map_props.c` tự gọi các hàm này khi bạn truyền đủ 3 path cho `MapProp_CreateStrip`/`CreateRocks` — bạn thường không cần gọi trực tiếp, **trừ** `PropLit_UpdateLighting()` vẫn phải gọi 1 lần/frame trong `Draw{Prefix}Map` (không tự động chạy). Đọc ánh sáng qua `Environment_Get{SunDirection,SunColor,AmbientColor}()`, tự tắt/mở theo thời gian trong ngày nếu map dùng day/night cycle. Không bao giờ gọi `UnloadShader()`/`UnloadMaterial()` lên kết quả — shader dùng chung, cache qua `ResourceManager_LoadShader`.

### `maps/toolkit/grass_material.h` — vật liệu nền thay thế (đang gác lại)

Texture-blend hybrid ground material (grassBase + grassDetail + dirt, trộn qua `fbm2` noise) — từng được thử làm nền chính nhưng bị gác lại do vấn đề hình ảnh chưa giải quyết xong (xem `CORE_ISSUES.md` Item 38). Vẫn còn trong codebase để dùng lại sau; **không phải lựa chọn mặc định** — `MapProp_CreateGround` hiện dùng `ground_splat.fs` riêng, không dùng `grass_material`.

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

Real-world-scaled: 1 unit = 1 meter (rescaled from the old 1cm-scale — see root `CLAUDE.md` "Standard coordinates & scale"). Code samples further below in this doc (§ examples using `600.0f`/`440.0f`/`1800.0f`) predate the rescale — use the constants below, not the old sample literals, when writing a new map.

* **Tâm Đấu Trường:** `(6.0f, 0.0f, 4.4f)` (Hằng số `arenaCenter`).
* **Bán Kính Hoạt Động:** `18.0f` (Hằng số `arenaRadius`). Toàn bộ mặt đất chơi được nên nằm trong bán kính này.
* **Cao Độ Mặc Định (Y):** Mặt đất chính nằm ở cao độ `Y = 0.0f` (hoặc chênh lệch rất nhỏ như `-0.0005f`).

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

## 6. Khung File Source Mẫu — Dùng Toolkit (Khuyến Nghị, Bắt Đầu Từ Đây)

Đây là cách nhanh nhất để tạo 1 map thành phẩm: đảo nổi (nền lõm xuống vách ở viền) + đường đi + đá rải rác + vách núi bao quanh + biển mây bên dưới, toàn bộ dùng `maps/toolkit/` (mục 1b), không cần tự viết `rlgl`. Copy khung này vào `maps/worlds/<map_name>/<map_name>.c`, đổi số liệu. **Mọi map trong project này đều theo mô-tuýp "đảo nổi giữa vách núi + biển mây" — đây là khung mặc định, không phải một lựa chọn.**

```c
#include "verdant_path.h"           // đổi tên theo map của bạn
#include "raylib.h"
#include "environment/environment_system.h"
#include "maps/toolkit/prop_lit.h"
#include "maps/toolkit/map_props.h"

#define MAP_WIDTH 100.0f
#define MAP_DEPTH 75.0f
static const Vector3 kMapCenter = {MAP_WIDTH * 0.5f, 0.0f, MAP_DEPTH * 0.5f};

#define PATH_LENGTH (MAP_WIDTH - 10.0f)
#define PATH_WIDTH 4.0f

// Vách đảo lõm xuống CLIFF_DEPTH mét; biển mây phải nằm SÂU HƠN (âm hơn)
// mức này để vách không đâm xuyên mây — xem MapProp_CreateGroundHeightmap.
#define CLIFF_DEPTH 8.0f
#define CLOUD_SEA_Y -12.0f

#define ROCK_COUNT 6
static const MapRockPlacement kRocks[ROCK_COUNT] = {
    {{15.0f, 0.0f, 10.0f}, 0.6f, 0.5f, 20.0f},
    {{30.0f, 0.0f, 60.0f}, 0.9f, 0.7f, 100.0f},
    {{70.0f, 0.0f, 15.0f}, 0.5f, 0.45f, 200.0f},
    {{85.0f, 0.0f, 55.0f}, 1.1f, 0.8f, 60.0f},
    {{45.0f, 0.0f, 65.0f}, 0.7f, 0.55f, 320.0f},
    {{20.0f, 0.0f, 45.0f}, 0.8f, 0.6f, 150.0f},
};

#define MOUNTAIN_ROCK_COUNT 36
static MapRockPlacement s_mountainRocks[MOUNTAIN_ROCK_COUNT];

static MapGroundSurface s_ground;
static MapStripSurface s_path;
static MapRockSet s_rocks;
static MapCloudSea s_cloudSea;
static bool s_ready = false;

void InitVerdantPathMap(void)
{
    // 1. Ánh sáng/sương mù — mục 4
    Environment_SetAmbientColor((Color){60, 65, 85, 255});
    Environment_SetSunColor((Color){200, 205, 220, 255});
    Environment_SetSunDirection((Vector3){0.5f, -0.8f, -0.3f});
    Environment_SetShadowColor((Color){10, 10, 15, 150});

    EnvFogConfig fog = {0};
    fog.enabled = true;
    fog.color = (Color){40, 45, 60, 255};
    fog.start = 60.0f;
    fog.end = 140.0f;
    fog.density = 1.0f;
    Environment_SetFogConfig(fog);

    // 2. Nền đảo nổi — heightmap sinh bằng:
    //    python3 scripts/generate_island_heightmap.py assets/heightmaps/<map_name>_island.png 128 <seed>
    s_ground = MapProp_CreateGroundHeightmap("assets/heightmaps/verdant_path_island.png",
                                    MAP_WIDTH, MAP_DEPTH, CLIFF_DEPTH, 12.0f,
                                    "assets/textures/grass_ground_diffuse.png",
                                    "assets/textures/grass_ground_diffuse.png",
                                    "assets/textures/dirt_diffuse.png");

    // 3. Đường đá — path_blend.fs tự lo ánh sáng/viền, normal/roughness chưa dùng (NULL)
    s_path = MapProp_CreateStrip(PATH_LENGTH, PATH_WIDTH, 2.0f,
                                 "assets/textures/stone_path_diffuse.png",
                                 NULL, NULL);

    // 4. Đá rải rác — 1 model, nhiều vị trí (mục 11's nguyên tắc "1 model nhiều draw call")
    s_rocks = MapProp_CreateRocks("assets/textures/rock_diffuse.png",
                                  "assets/textures/rock_normal.png",
                                  "assets/textures/rock_roughness.png");

    // 5. Vách núi quanh viền — dùng lại đúng model đá ở bước 4, chỉ scale to hơn
    MapProp_GenerateMountainRing(s_mountainRocks, MOUNTAIN_ROCK_COUNT,
                                 MAP_WIDTH, MAP_DEPTH,
                                 6.0f, 14.0f, 18.0f, 30.0f, 1337);

    // 6. Biển mây — rộng hơn map để trông vô tận, sâu hơn CLIFF_DEPTH
    s_cloudSea = MapProp_CreateCloudSea(MAP_WIDTH + 300.0f, MAP_DEPTH + 300.0f, 50.0f);

    s_ready = true;
}

void DrawVerdantPathMap(void)
{
    if (!s_ready) return;

    PropLit_UpdateLighting(); // bắt buộc mỗi frame — s_rocks dùng prop_lit (đủ 3 path)

    MapProp_DrawCloudSea(&s_cloudSea, kMapCenter, CLOUD_SEA_Y); // vẽ trước, ở xa/dưới nhất
    MapProp_DrawGround(&s_ground, kMapCenter);
    MapProp_DrawStrip(&s_path, kMapCenter, 0.01f); // yOffset nhỏ tránh z-fighting với nền
    MapProp_DrawRocks(&s_rocks, s_mountainRocks, MOUNTAIN_ROCK_COUNT); // vách núi
    MapProp_DrawRocks(&s_rocks, kRocks, ROCK_COUNT); // đá rải rác trên cao nguyên
}
```

Map này không cần `Update{Prefix}Map`/`Unload{Prefix}Map` — không khai báo 2 hàm đó trong `.h` thì script đăng ký sẽ tự bỏ qua (mục 2), không sao cả.

Nếu map cần thứ toolkit chưa có (bụi cây, thảm hoa, hồ nước, địa hình đồi núi...) — thêm hàm mới vào `maps/toolkit/map_props.h`/`.c` theo đúng khuôn `Create*`/`Draw*`/`Unload*` ở trên, hoặc rơi xuống code thủ công ở các mục 8-12 bên dưới cho phần map riêng đó.

---

## 6b. Khung File Source Mẫu — Tự Vẽ Bằng `rlgl` (Nâng Cao / Toolkit Chưa Hỗ Trợ)

Dưới đây là một ví dụ hoàn chỉnh về file `.c` của một map chủ đề Sa Mạc Dung Nham, vẽ hoàn toàn thủ công bằng `rlgl` — dùng khi cần hiệu ứng toolkit chưa có (hồ dung nham cuộn sóng, mặt trăng nền...), không phải cách mặc định để tạo nền/đường/đá (đã có mục 6):

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

1. **Bước 1:** Tạo một thư mục mới trùng tên map dưới `maps/worlds/` (Ví dụ: `maps/worlds/desert_lava/`).
2. **Bước 2:** Tạo file `.h` và `.c` trong thư mục đó — ưu tiên theo mẫu Toolkit ở mục 6; chỉ dùng mẫu `rlgl` thủ công ở mục 6b cho phần toolkit chưa hỗ trợ.
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

> Nền/đường đi/đá đã có `MapProp_CreateGround`/`CreateStrip`/`CreateRocks` (mục 1b) tự lo việc nạp+gán texture — chỉ đọc mục này khi cần texture cho một loại prop khác (mục 8's model 3D tự do) mà toolkit chưa có hàm riêng.

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

> Cho đá rải rác, `MapProp_CreateRocks`/`MapProp_DrawRocks` (mục 1b) đã làm đúng nguyên tắc này sẵn — chỉ cần đọc phần dưới đây khi rải một loại prop khác (bụi tre, thảm hoa...) chưa có hàm riêng trong toolkit.

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



