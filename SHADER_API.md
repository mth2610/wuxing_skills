# Shader API — GLSL Guidelines & 3D Rendering Best Practices

> Source: `core/shaders/common/` (vs_header, fs_header, lighting, noise, fx, triplanar)
> Cross-reference: [`CORE_API.md`](CORE_API.md) §10 stub

---

## 10. GLSL Shader Guidelines

### `#include` Is an Engine Preprocessing Step, Not Native GLSL

> [!IMPORTANT]
> GLSL has no native `#include` directive. The `#include "core/shaders/common/..."` lines below are resolved by **`shader_preprocessor.h/.c`** (`ShaderPreprocessor_Load()`), which is wired into `ResourceManager_LoadShader()`. It recursively reads the file, textually substitutes every `#include "..."` line with the target file's contents (up to `MAX_INCLUDE_DEPTH`), and only then hands the fully-expanded source to `LoadShaderFromMemory()`. The resulting buffer is heap-allocated with `RL_MALLOC`/freed with `RL_FREE` internally — skill code never touches this buffer and never calls `ShaderPreprocessor_Load()` directly; it is invoked automatically by `ResourceManager_LoadShader()`.
>
> Practical implication: a raw `glCompileShader` call (or any tool that lints `.vs`/`.fs` files standalone, e.g. an online GLSL validator) will fail on the `#include` line because it isn't valid core GLSL — this is expected and not a project bug. Only `ResourceManager_LoadShader()` produces compilable output.

### Common Shader Files — Tổng quan

| File | Dùng trong | Cung cấp |
|---|---|---|
| `vs_header.glsl` | Mọi `.vs` | Attributes, uniforms, varyings, `VS_FinalOutput()` |
| `fs_header.glsl` | Mọi `.fs` | Varyings nhận, uniforms môi trường, `finalColor` |
| `lighting.glsl` | `.fs` cần chiếu sáng | `perturbNormal`, `calcFresnel`, `calcSpecular`, `calcDiffuse` |
| `noise.glsl` | `.vs` / `.fs` cần nhiễu | `hash2`, `hash3`, `vnoise`, `fbm2`, `fbm2N` |
| `fx.glsl` | `.fs` cần hiệu ứng | `dissolveCalc`, `flowBlend`, `emissiveMask` |
| `triplanar.glsl` | `.fs` cho mesh không có UV ổn định | `triplanarWeights`, `triplanarNoise`, `triplanarSample` |

**Quy tắc include:**
- Luôn include theo thứ tự: `fs_header.glsl` → `noise.glsl` (nếu cần) → `lighting.glsl` → `fx.glsl` → `triplanar.glsl` (nếu cần, phụ thuộc `noise.glsl` cho `triplanarNoise`)
- `fx.glsl` không phụ thuộc `noise.glsl` — có thể include riêng lẻ hoặc cùng nhau
- Không tái implement hash/noise/fbm/dissolve/flow blend/triplanar trong skill code

### Triplanar Mapping (`core/shaders/common/triplanar.glsl`)

Giải quyết Item 4a (`CORE_ISSUES.md`): các `ProceduralMesh_Draw*` (Rock, ShardCluster, Fissure, VortexFunnel) vẽ qua `rlBegin`/`rlEnd` immediate-mode — chỉ có position + normal, **không có texcoord** — nên UV-based texturing sẽ stretch/streak trên facet jagged. Triplanar chiếu texture/pattern từ 3 mặt phẳng trục world-space (X/Y/Z) và blend theo world normal thay vì dùng UV.

```glsl
vec3 triplanarWeights(vec3 worldNormal, float sharpness);              // sharpness 2.0-6.0
float triplanarNoise(vec3 worldPos, vec3 weights, float scale);        // procedural, không cần texture asset
vec4 triplanarSample(sampler2D tex, vec3 worldPos, vec3 weights, float scale); // texture asset thật
```

Pattern dùng trong `main()`:
```glsl
vec3 w = triplanarWeights(fragNormal, 4.0);
float pattern = triplanarNoise(fragPosition, w, 0.05); // hoặc triplanarSample(myTex, fragPosition, w, 0.02)
```

> [!NOTE]
> `scale` là tần số chiếu world-space (không phải UV [0,1]) — giá trị nhỏ (0.01-0.05) cho mesh lớn, lớn hơn (0.05-0.1) cho mesh nhỏ/chi tiết. Tune bằng mắt theo kích thước thực tế của mesh.

### Required Includes

Vertex Shader

```glsl
#version 330
#include "core/shaders/common/vs_header.glsl"
```

Fragment Shader — 3D mesh cần chiếu sáng đầy đủ:

```glsl
#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"    // nếu cần hash/fbm
#include "core/shaders/common/lighting.glsl"  // perturbNormal, calcFresnel, calcSpecular, calcDiffuse
#include "core/shaders/common/fx.glsl"        // dissolveCalc, flowBlend, emissiveMask
```

Fragment Shader — tối giản (chỉ dissolve, không cần lighting 3D):

```glsl
#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/fx.glsl"
```

### Shader Loading

Skills using custom 3D lighting must always load both vertex and fragment shaders.

```c
Shader shader = ResourceManager_LoadShader(
    "skill.vs",
    "skill.fs"
);
```

Only unlit shaders may pass `NULL` as the vertex shader.

Do not use `NULL` as the vertex shader when using lighting.

### Built-in Variables

Provided automatically by the common headers — **do not redeclare** any of these in a skill `.vs`/`.fs`.

**From `vs_header.glsl` (vertex shader only):**

| Variable | Direction | Type | Space / Notes |
|---|---|---|---|
| `vertexPosition` | `in` (attribute) | `vec3` | Object/local space — raw mesh vertex |
| `vertexTexCoord` | `in` (attribute) | `vec2` | Raw mesh UV |
| `vertexNormal` | `in` (attribute) | `vec3` | Object/local space — raw mesh normal, **not yet normalized or transformed** |
| `mvp` | `uniform` | `mat4` | Model-View-Projection — used internally by `VS_FinalOutput()` |
| `matModel` | `uniform` | `mat4` | Model matrix — used internally by `VS_FinalOutput()` |
| `u_time` | `uniform` | `float` | Auto-bound by `SkillManager_BeginShader()` — do not set manually |
| `viewPos` | `uniform` | `vec3` | Camera world-space position — auto-bound |
| `u_resolution` | `uniform` | `vec2` | Screen resolution — auto-bound |
| `fragPosition` | `out` (varying) | `vec3` | **World-space.** Written only by `VS_FinalOutput()` |
| `fragTexCoord` | `out` (varying) | `vec2` | Passthrough of `vertexTexCoord`, written by `VS_FinalOutput()` |
| `fragNormal` | `out` (varying) | `vec3` | **World-space, normalized.** Written by `VS_FinalOutput()` |

**From `fs_header.glsl` (fragment shader only):**

| Variable | Direction | Type | Space / Notes |
|---|---|---|---|
| `fragPosition` | `in` (varying) | `vec3` | **World-space** — matches VS output exactly |
| `fragTexCoord` | `in` (varying) | `vec2` | UV, passed through unchanged from VS |
| `fragNormal` | `in` (varying) | `vec3` | **World-space, normalized** |
| `u_time` | `uniform` | `float` | Auto-bound — do not set manually |
| `viewPos` | `uniform` | `vec3` | Camera world-space position — auto-bound |
| `u_resolution` | `uniform` | `vec2` | Auto-bound |
| `u_lightDir` | `uniform` | `vec3` | Real environment sun direction, pre-negated to point *toward* the light — auto-bound **only if the skill uses `SkillManager_BeginShader()`**; skills calling raw `BeginShaderMode()` must set it manually (see note below) |
| `finalColor` | `out` | `vec4` | Final pixel output — write exactly once per `main()` |

> [!NOTE]
> **`fragNormal` caveat:** `VS_FinalOutput()` computes `fragNormal` from the **original** `vertexNormal` (`normalize(matModel * vec4(vertexNormal, 0.0))`) — it does **not** recompute the normal from a displaced surface. If your vertex shader displaces position (e.g. `tube.vs`'s `getDisplacement()`), the outgoing `fragNormal` will *not* reflect that displacement. This is why skills like the Water Stream tube re-derive a perturbed normal in the **fragment** shader via `perturbNormal()` using a matching height-field gradient, rather than relying on a geometrically displaced normal from the VS. If a skill needs a true displaced-geometry normal, it must compute it manually in the VS (e.g. via finite-difference neighboring vertices) — `VS_FinalOutput()` will not do this automatically.

### Built-in Functions

#### `lighting.glsl` — Chiếu sáng 3D

```glsl
vec3  perturbNormal(vec3 baseNormal, vec2 heightDelta, float strength);
float calcFresnel(vec3 normal, vec3 viewDir, float power);
float calcSpecular(vec3 normal, vec3 lightDir, vec3 viewDir, float shininess);
float calcDiffuse(vec3 normal, vec3 lightDir, float ambient);
```

* **`perturbNormal(baseNormal, heightDelta, strength)`** — Perturbs a base normal using the gradient of a skill-supplied height field, to fake surface roughness (water ripples, lava bubbling, bark texture...) without extra geometry.
  - `baseNormal`: the mesh normal to perturb — typically `fragNormal` (already world-space, normalized).
  - `heightDelta`: `vec2(h(u-eps) - h(u+eps), h(v-eps) - h(v+eps))` — the **gradient** of your own height function, sampled at `±eps` around the current `fragTexCoord` in U and V respectively. The skill must implement its own height function (e.g. `tube.fs`'s `getIrregularity()`) and **must reuse the exact same formula as the vertex shader's displacement function**, or lighting and physical displacement will visually mismatch.
  - `strength`: deformation intensity, **typical range 0.3 – 0.8**.
* **`calcFresnel(normal, viewDir, power)`** — Schlick-approximated rim term, returns `[0..1]` (`0` = surface viewed face-on, `1` = viewed edge-on). **Typical power: 2.0 – 5.0.**
* **`calcSpecular(normal, lightDir, viewDir, shininess)`** — Blinn-Phong specular highlight, returns `[0..1]` — caller scales bằng intensity (e.g. `* 5.0`). **Typical shininess: 32 – 512.**
* **`calcDiffuse(normal, lightDir, ambient)`** — Lambertian diffuse với ambient floor, trả về `[ambient..1.0]`.
  - `ambient`: ánh sáng nền tối thiểu, thường `0.10 – 0.25`.
  - Nhân trực tiếp vào baseColor: `baseColor *= calcDiffuse(normal, lightDir, 0.15);`

**`lightDir` chuẩn của project** (hard-code trong mọi skill):
```glsl
vec3 lightDir = normalize(vec3(0.5, 0.8, 0.5));
```

---

#### `noise.glsl` — Nhiễu ngẫu nhiên

```glsl
float hash2(vec2 p);                    // 2D hash → [0, 1]
float hash3(vec3 p);                    // 3D hash → [0, 1]
float vnoise(vec2 p);                   // 2D value noise → [0, 1]  (tên "vnoise" tránh conflict GLSL built-in noise2)
float fbm2(vec2 p);                     // 3-octave FBM → [0, ~1]
float fbm2N(vec2 p, int octaves);       // N-octave FBM, 1–6 → [0, 1] normalized
```

* **`hash2 / hash3`** — Pseudo-random hash. `hash3` dùng cho dissolve theo world-space: `hash3(floor(fragPosition * scale))`.
* **`vnoise`** — Value noise, nhanh hơn Perlin. Dùng làm base cho FBM hoặc UV warp trực tiếp. (Không dùng tên `noise2` — GLSL built-in conflict.)
* **`fbm2`** — 3-octave FBM, dùng trong đa số VFX (lửa, plasma, vân sóng). Có built-in rotation để tránh axis-aligned artifacts.
* **`fbm2N`** — Khi cần kiểm soát chi tiết: 1–2 octave cho gió mềm/hào quang, 5–6 cho vỏ cây/đá.

```glsl
// Ví dụ: UV warp theo FBM để làm gió uốn xoắn
vec2 flow = vec2(u_time * 0.4, -u_time * 0.6);
float distort = fbm2(vec2(localU, localV) * 8.0 + flow);
vec2 warpedUV = uv + (distort - 0.5) * 0.008;
```

---

#### `fx.glsl` — Hiệu ứng VFX

```glsl
float dissolveCalc(float noiseVal, float dissolve, float edgeWidth, out float edgeFactor);
float flowBlend(sampler2D tex, vec2 uv, vec2 flowDir, float speed, float strength, float time);
float emissiveMask(vec3 worldPos, float freq, float threshold);
```

* **`dissolveCalc`** — Noise-based dissolve + viền cháy sáng. Trả về `1.0` nếu pixel bị xóa, `0.0` nếu giữ lại. `edgeFactor` (out) là mức độ viền để mix màu element.
  ```glsl
  // Pattern chuẩn — include noise.glsl trước:
  float n = hash3(floor(fragPosition * 10.0));
  float edgeFactor;
  if (dissolveCalc(n, u_dissolve, 0.08, edgeFactor) >= 1.0) discard;
  baseColor = mix(baseColor, vec3(1.0, 0.5, 0.1), edgeFactor); // viền lửa ví dụ
  ```

* **`flowBlend`** — Flow map 2-phase blend chống giật (không seam khi phase reset). Trả về `float` luminance của texture sau blend.
  ```glsl
  vec2 flowDir = texture(flowTex, fragTexCoord).rg * 2.0 - 1.0;
  float intensity = flowBlend(causticsTex, fragTexCoord * 2.0, flowDir, 1.2, 0.05, u_time);
  baseColor += waterColor * intensity * 1.5;
  ```

* **`emissiveMask`** — Sine-based emissive từ world position — không bị kéo méo theo UV. Dùng cho nhựa cây, mạch năng lượng, rạn nứt phát sáng.
  ```glsl
  float mask = emissiveMask(fragPosition, 1.5, 0.88);
  baseColor += elementGlowColor * mask * 2.5;
  ```

Do not reimplement these functions.

> [!NOTE] **Resolved (CORE_ISSUES.md Item 10).** `u_lightDir` is now a real
> auto-bound uniform (added to `fs_header.glsl`, table above) —
> `SkillManager_BeginShader()` sets it to `-Environment_GetSunDirection()`
> (negated: the environment API returns the direction light *travels*, Y
> negative; shaders' `dot(normal, lightDir)` convention needs the direction
> *toward* the light, Y positive). **Do not hard-code
> `normalize(vec3(0.5, 0.8, 0.5))` in new skills** — use `normalize(u_lightDir)`
> instead so rim/diffuse lighting actually matches the environment's real
> sun direction (confirmed previously mismatched by comparing against a
> character's cast shadow). `tube.fs`, `stone_prison.fs`, `water_sphere.fs`,
> and `effect_material.fs` were migrated as part of this fix.
>
> **If your skill calls raw `BeginShaderMode()` instead of
> `SkillManager_BeginShader()`** (check your skill's `Draw` function —
> several existing skills do, e.g. `tube_skill.c`, `stone_prison_skill.c`),
> the auto-bind does **not** reach your shader. You must fetch
> `GetShaderLocation(shader, "u_lightDir")` yourself in `Init[Name]Skill`
> and call `SetShaderValue(shader, loc, &lightDir, SHADER_UNIFORM_VEC3)`
> with `lightDir = Vector3Negate(Environment_GetSunDirection())` each frame
> you draw — same pattern as `viewPos`/`u_camPos` in those two files.

### Custom Per-Skill Uniforms (e.g. `u_uvLength`, `u_dissolve`)

Skill-specific uniforms (anything not in the built-in tables above) are **not** handled by `SkillManager_BeginShader()` — the skill's own C code is responsible for sending them.

* **Lookup:** Cache the uniform location once, typically as a `static int` next to the shader, fetched in `Init[Name]Skill()` via `GetShaderLocation(shader, "u_uvLength")`. Do not call `GetShaderLocation` every frame — it's a string-hash lookup the engine does not cache for you.
* **Set timing:** Call `SetShaderValue()` for skill-specific uniforms **after** `SkillManager_BeginShader(shader)` (so the shader is bound) and **before** the draw call that uses them, every frame the value changes (e.g. `u_dissolve` ramping toward `1.0`) or once if constant for the skill's lifetime (e.g. `u_uvLength`, fixed at cast-time from the Bezier path length).
* **VS/FS synchronization:** If the same uniform name (e.g. `u_uvLength`) is declared in **both** `.vs` and `.fs` (as in the Water Stream sample), `SetShaderValue()` must be called **once** with that uniform's location for the shader program as a whole — raylib's `Shader.id` is one linked GL program covering both stages, so one `SetShaderValue()` call updates the value for both VS and FS reads of the same uniform name. There is no need (and no mechanism) to set it "twice, once per stage."
* **Declaration:** Declare these uniforms only in the `.vs`/`.fs` file(s) that read them — e.g. `u_uvLength` appears in both `tube.vs` and `tube.fs` because both need it; `u_dissolve` appears only in `tube.fs` because only the fragment shader uses it for fade-out.

### Rules

- Always use both `.vs` and `.fs` for 3D shaders.
- Include `fs_header.glsl` before `lighting.glsl`.
- Call `VS_FinalOutput()` as the final step of every vertex shader.
- Declare only skill-specific uniforms.
- Keep shader logic focused on the visual behavior of the element.
- Strict Parameter Requirement: The core engine's final vertex output function MUST receive exactly one vec3 argument representing the final processed or displaced vertex position.

### Android / GLES Compatibility Rules

Build Android chạy trên OpenGL ES. Pipeline dùng **hai path** tùy shader có dùng `#include` hay không.

#### Path 1 — Standalone shaders (không có `#include`)

`scripts/convert_shaders_to_gles.py` chạy lúc build APK, convert sang **GLES 1.00 (`#version 100`)**:

| Desktop GLSL 3.3 | GLES 1.00 (sau build script) |
|---|---|
| `in vec3 pos` (VS) | `attribute vec3 pos` |
| `out vec3 vary` (VS) | `varying vec3 vary` |
| `in vec3 vary` (FS) | `varying vec3 vary` |
| `out vec4 finalColor` + mọi dùng `finalColor` | xóa khai báo + đổi thành `gl_FragColor` |
| `texture(sampler, uv)` | `texture2D(sampler, uv)` |
| precision (FS) | tự inject `precision highp float;` nếu chưa có |

Build script **KHÔNG** tự sửa: `f` suffix trên float literal, precision cho `.vs`, nội dung `#include`.

> Yêu cầu GLES 2.0+ (Android 2.2+, tất cả thiết bị target).

#### Path 2 — Shaders dùng `#include` common headers

Build script **BỎ QUA** — các file này giữ nguyên `#version 330` trong APK.

Ở runtime, `ResourceManager_LoadShader` → `ShaderPreprocessor_Load`:
1. Mở rộng đệ quy mọi `#include "..."` (ví dụ `vs_header.glsl`, `lighting.glsl`)
2. `RewriteVersionForGLES()` đổi `#version 330` → `#version 300 es`
3. Kết quả: source GLES 3.0 với `in`/`out`/`texture()` — hợp lệ

Common headers (`vs_header.glsl`, `fs_header.glsl`, `lighting.glsl`, `noise.glsl`, `fx.glsl`) **đã có** `#ifdef GL_ES precision highp float; #endif` — không cần khai báo thêm trong skill shader. Cả VS lẫn FS đều dùng `highp float` (quan trọng — xem Rule E).

> Yêu cầu GLES 3.0+ (Android 4.3+, toàn bộ thiết bị hiện đại).

---

**Rule A — Không dùng `f` suffix trên float literal (áp dụng cho CẢ HAI path):**

```glsl
// SAI — Android GLES compiler từ chối, build script KHÔNG tự sửa:
float breathe = 1.25f + 0.12f * sin(u_time * 5.5);

// ĐÚNG:
float breathe = 1.25 + 0.12 * sin(u_time * 5.5);
```

`f` suffix là cú pháp C. Desktop driver bỏ qua; Android GLES strict compiler từ chối → `shader.id = 0`.

**Rule B — Standalone VS phải tự khai báo precision:**

Build script tự inject precision cho standalone `.fs`, nhưng **không** làm với `.vs`. Mọi standalone vertex shader (không có `#include "core/shaders/common/vs_header.glsl"`) phải thêm:

```glsl
#version 330

#ifdef GL_ES
precision highp float;
#endif
```

Shader dùng common headers → precision đã có trong `vs_header.glsl`/`fs_header.glsl`, không cần khai báo lại.

**Rule C — Behavior khi shader compile thất bại trên Android:**

`ResourceManager_LoadShader` **không crash** khi shader compile fail — trả về `shader.id = 0` và log:
```
SHADER: compile failed, not caching (vs=... fs=...)
```
`SkillManager_BeginShader` guard `id == 0` → no-op (bỏ qua `BeginShaderMode`). Skill vẫn chạy nhưng render với default flat shader → mesh trông **trắng toát / không có hiệu ứng**.

Khi thấy chiêu render trắng toát trên Android: kiểm tra logcat dòng trên, sửa theo Rule A/B, rebuild APK.

**Rule D — `matModel` phải được set thủ công khi dùng rlgl immediate mode:**

`VS_FinalOutput()` trong `vs_header.glsl` tính `fragNormal = normalize(matModel * vertexNormal)`. Raylib chỉ upload `matModel` khi dùng `DrawMesh`/`DrawModel` — **không** upload khi dùng rlgl immediate mode (`rlBegin`/`rlEnd`/`ProceduralMesh_DrawTube`...).

Trên Android GLES 3.0, `matModel` giữ giá trị **all-zeros** → `normalize(vec3(0,0,0))` = undefined (NaN trên Adreno/Mali) → `fragNormal = NaN` → `clamp(NaN, 0, 1) = 1.0` → màu trắng. Trên Mac desktop, OpenGL driver xử lý normalize(zero) khác (trả về identity-ish) nên không thấy lỗi.

**`SkillManager_BeginShader` tự động set `matModel = identity` trước `BeginShaderMode`.** Skill code không cần làm gì thêm nếu dùng `SkillManager_BeginShader`.

> [!IMPORTANT] **Bug đã sửa (2026-06-30):** bản fix trước đây dùng `shader.locs[SHADER_LOC_MATRIX_MODEL] >= 0` để kiểm tra location hợp lệ — đây là cách kiểm tra SAI. Raylib's `LoadShaderFromMemory` chỉ auto-bind một danh sách uniform mặc định cố định (`mvp`, `colDiffuse`, `texture0`, vertex attribs...); `matModel` không nằm trong danh sách đó, nên slot `shader.locs[SHADER_LOC_MATRIX_MODEL]` không bao giờ được ghi và giữ giá trị `0` từ `RL_CALLOC` ban đầu. `0` vẫn pass `>= 0` dù **không phải vị trí thật của `matModel`** → `SetShaderValueMatrix` ghi đè nhầm vào bất kỳ uniform nào khác thực sự nằm ở location 0 trong chương trình GLSL đã link (ví dụ một `sampler2D texture0` khai báo riêng) → vỡ texture binding / giá trị uniform đó → mesh có thể hiện toàn màu trắng dù hình dạng vẫn đúng. Đã xác nhận qua `tsunami_skill` (FlowMap's `texture0` bị ghi đè bởi identity matrix). `core/skill_manager.c`'s `SkillManager_BeginShader` đã được sửa để dùng `GetShaderLocation(shader, "matModel")` (tra theo tên, trả về `-1` thật nếu không tồn tại) thay vì đọc `shader.locs[SHADER_LOC_MATRIX_MODEL]`.

Nếu skill gọi `BeginShaderMode` trực tiếp (bypass `SkillManager_BeginShader`), PHẢI set matModel thủ công — và PHẢI tra location bằng tên, không dùng `shader.locs[SHADER_LOC_MATRIX_MODEL]`:

```c
// Trước draw call, sau BeginShaderMode():
int matModelLoc = GetShaderLocation(s_shader, "matModel");
if (matModelLoc >= 0) {
    Matrix identity = MatrixIdentity();
    SetShaderValueMatrix(s_shader, matModelLoc, identity);
}
```

**Rule E — VS và FS phải dùng cùng precision cho mọi shared uniform (GLES 3.x strict):**

Trên GLES 3.x strict implementations (Mali-G68, GLES 3.2), nếu một uniform xuất hiện ở cả VS lẫn FS, cả hai phải có **cùng precision qualifier**. Nếu không khớp → link failure → `shader.id = 0` → màu trắng.

```
// Lỗi điển hình trong logcat:
// SHADER: [ID 14] Link error: L0001 The fragment floating-point variable u_time
//         does not match the vertex variable u_time. The precision does not match.
```

Common headers đã xử lý vấn đề này: cả `vs_header.glsl` và `fs_header.glsl` đều dùng `precision highp float` — do đó `u_time`, `viewPos`, `u_resolution` và mọi uniform khai báo theo default đều là `highp` ở cả hai stage.

Nếu skill tự khai báo uniform riêng (ví dụ `uniform float u_uvLength;`) trong cả `.vs` lẫn `.fs`, uniform đó sẽ inherit default precision — `highp` từ `vs_header.glsl` cho VS và `highp` từ `fs_header.glsl` cho FS → khớp, không có vấn đề.

Nếu skill tự khai báo precision mặc định thấp hơn (vd `precision mediump float;`) ở FS standalone, phải đảm bảo VS cũng dùng `mediump` — hoặc tốt hơn là dùng `highp` nhất quán ở cả hai.

> [!NOTE]
> Desktop OpenGL driver thường compile thành công kể cả khi có `f` suffix hay thiếu precision, và xử lý `normalize(zero)` khác mobile — lỗi thường chỉ xuất hiện khi test trên thiết bị Android thật (GLES strict mode).

---

---
## 11 3D Rendering & Shader Best Practices

### 11.1 Vertex Color Reset

Before drawing custom geometry with `rlBegin()`, always reset the vertex color:
```c
#include "rlgl.h"   // required for rlBegin/rlColor4ub/rlVertex3f/rlEnd — not implicitly pulled in by raylib.h
// ...
rlColor4ub(255, 255, 255, 255);
```
Otherwise the mesh may inherit colors from previous draw calls. `rlColor4ub` (and the rest of the `rl*` immediate-mode API) lives in `rlgl.h`, a separate header from `raylib.h` — a skill that only includes `raylib.h` will fail with `implicit declaration of function` at compile time, not an obvious "missing header" error.

### 11.2 Procedural Noise
When using world-space procedural noise:
- Use low world-coordinate scales (e.g. `fragPosition.xz * 0.05`).
- Avoid high frequencies that produce TV-static artifacts.
- Stretch individual axes when directional patterns are desired.

### 11.3 3D Lighting

The default Raylib vertex shader cannot be used for custom 3D lighting.

Rules:

- Always provide both `.vs` and `.fs`.
- Load both with `ResourceManager_LoadShader()`.
- Never pass `NULL` as the vertex shader.
- Use `core/shaders/common/vs_header.glsl` and `VS_FinalOutput()`.

### 11.4 Core Custom VFX Shaders

The core engine provides specialized, high-performance standalone custom shaders under `core/shaders/` to render procedurally-detailed billboards and meshes:

- **`magic_filaments.fs`** (Magical Sparkling Filaments Shader):
  - **Inputs/Uniforms**: `u_color` (vec4), `u_progress` (float), `u_diffusion` (float), `u_noiseScale` (float), `u_driftSpeed` (float), `u_sourcePos` (vec2).
  - **Technique**: Uses a 1-octave value noise domain-warp for coordinate bending, a highly optimized 2-octave ridged FBM (`ridgedFBM2`) to isolate thin glowing fibers, an expanding shell-fresnel rim, and high-frequency time-varying value noise sparkle peaks.
  - **Performance Profile**: Optimized to use exactly 5 texture noise fetches per pixel (representing a 50% GPU fill rate footprint reduction).
- **`energy_smoke.fs`** (Gaseous Energy Smoke Shader):
  - **Inputs/Uniforms**: `u_color` (vec4), `u_progress` (float), `u_diffusion` (float), `u_noiseScale` (float), `u_driftSpeed` (float), `u_sourcePos` (vec2).
  - **Technique**: Simulates point-source gas expansion using a closed-form analytic diffusion solution:
    $$C(r,t) = \frac{C_0 \cdot e^{-\frac{r^2}{4Dt}}}{4\pi Dt}$$
    This models expanding, fading Gaussian profiles with built-in domain warping using 2D value noise.
  - **Performance Profile**: Uses 2D value noise instead of 3D FBM, reducing mathematically heavy hash overhead by 86% per pixel.
- **`smoke_column.fs`** (Continuous Smoke Column Shader):
  - **Technique**: Decodes position seeds packed into the model's normal vector coordinates:
    $$\text{seedVal} = u\_seed + \frac{\text{fragNormal.y}}{\|\text{fragNormal.xz}\|} \times 100.0$$
    This decodes CPU-generated random offsets on the GPU to generate continuous, un-pixelated noise fields across billboard planes with **zero batch flushes**.

---

---
