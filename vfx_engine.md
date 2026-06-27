# VFX Engine Roadmap — Ngũ Hành Tỷ Võ

---

## Phase 1 — Shader & Texture Pipeline (ưu tiên cao nhất)

### 1.1 Flow Map Shader `flow_map.fs`
Kỹ thuật quan trọng nhất còn thiếu. Flow map là texture RG 2 kênh encode vector trường vận tốc UV.

| Thành phần | Mô tả |
|---|---|
| `flow_map.fs` | Fragment shader: sample flowmap → distort UV → blend 2 pha (offset 0.0 & 0.5) để khử seam |
| `FlowMapConfig` (struct) | `flowTex`, `speed`, `strength`, `tiling`, `phaseOffset` |
| Áp dụng | Trail ribbon texture (kiếm kim loại chảy năng lượng), Water skill (mặt nước), Fire skill (lửa cháy), Electric (sóng điện dọc tia sét) |

Công thức cốt lõi cần implement trong shader:
```glsl
// Blend hai pha để tránh discontinuity mỗi chu kỳ
vec2 flowVec   = texture(flowMap, uv).rg * 2.0 - 1.0;
float phase0   = fract(time * speed);
float phase1   = fract(time * speed + 0.5);
float blend    = abs(fract(time * speed) * 2.0 - 1.0);
vec4  col0     = texture(mainTex, uv + flowVec * phase0 * strength);
vec4  col1     = texture(mainTex, uv + flowVec * phase1 * strength);
fragColor      = mix(col0, col1, blend);
```

---

### 1.2 Spritesheet Animation — `sprite_anim.h`
Hiện tại particle/trail chỉ dùng static texture. Cần thêm:

| Field | Mô tả |
|---|---|
| `frameCount`, `fps` | Số frame & tốc độ |
| `rows`, `cols` | Layout atlas |
| `playMode` | `ANIM_ONCE`, `ANIM_LOOP`, `ANIM_RANDOM_START` |

Tích hợp: thêm `SpriteAnim *anim` (nullable) vào `ParticleConfig` và `TrailConfig`. Mỗi update tính `currentFrame = (int)(age * fps) % frameCount` → tính UV offset.

---

### 1.3 Multi-Stop Color Gradient — `color_gradient.h`
`colorStart` / `colorEnd` hiện tại quá thô. Cần:

```c
typedef struct { float t; Color color; } GradientStop;
typedef struct { GradientStop stops[8]; int count; } ColorGradient;
Color ColorGradient_Sample(const ColorGradient *g, float t);
```

Tích hợp: `ParticleConfig` và `TrailConfig` nhận `const ColorGradient *gradient` (nullable, fallback về start/end cũ).

---

### 1.4 Shader Library chuẩn hoá — `shaders/`
Tổ chức lại các shader thành thư viện dùng chung:

| File | Dùng cho |
|---|---|
| `base_billboard.fs` | Particle cơ bản (hiện tại) |
| `additive_soft.fs` | Lửa, điện, glow — BLEND_ADDITIVE + soft edge |
| `dissolve.fs` | Cái chết của entity — noise mask cutoff |
| `distortion.fs` | Nhiệt/nước — distort màn hình phía sau bằng normal map |
| `flow_map.fs` | (mục 1.1) |
| `rim_glow.fs` | Viền phát sáng cho trail ribbon (thanh kiếm, rồng) |

---

## Phase 2 — Camera & Screen-Space Effects

### 2.1 Camera Shake — `camera_fx.h`
```c
void CameraFX_Shake(float trauma);      // trauma [0..1], power curve
void CameraFX_Update(Camera3D *cam, float dt);
```
Dùng Perlin noise seed theo thời gian, trauma decay tự động. Gọi từ `SwordDeathCallback`, hit callback.

---

### 2.2 Screen Distortion Pass — `screen_distort.h`
Render scene vào `RenderTexture2D`, sau đó fullscreen quad với `distortion.fs`:
- Nhận list `DistortionSource` (vị trí world, bán kính, cường độ)
- Project sang screen space, sample normal map → distort UV
- Dùng cho: nổ khí kim, hơi nước, sóng xung kích

---

### 2.3 Post-Processing Stack — `post_fx.h`
Thứ tự pass cố định, bật/tắt từng pass:

| Pass | Kỹ thuật |
|---|---|
| Bloom | Gaussian blur 2-pass trên bright region (threshold) |
| Chromatic Aberration | Offset R/G/B channel theo radial từ tâm màn hình |
| Color Grade | LUT texture 3D 16×16×16 hoặc simple curves |
| Vignette | Tối góc màn hình |

---

## Phase 3 — System Mở Rộng

### 3.1 Sub-Emitter System — mở rộng `particle_system.h`
Thêm vào `ParticleConfig`:
```c
const ParticleConfig *onDeathEmit;   // NULL = không dùng
int onDeathEmitCount;
const ParticleConfig *onLiveEmit;    // phát liên tục khi còn sống
float onLiveEmitRate;                // particles/giây
```

---

### 3.2 Dynamic Point Light — `vfx_light.h`
Không dùng raylib light (quá nặng). Implement simple deferred-style:
```c
void VFXLight_Spawn(Vector3 pos, Color color, float radius, float lifetime);
void VFXLight_GetActive(VFXLightData *out, int *count, int maxCount);
```
Pass danh sách light vào shader qua uniform array. Giới hạn 8 light đồng thời (đủ cho skill effect).

---

### 3.3 Decal System — `decal_system.h`
Ground mark khi chiêu đánh trúng đất:
- Pool tĩnh 32 decal
- Mỗi decal: vị trí, góc, scale, texture, lifetime, fade
- Render bằng quad áp sát mặt đất (y offset nhỏ), blend `BLEND_ALPHA`

---

### 3.4 Spline / Path Sampler — `spline_path.h`
Catmull-Rom spline dùng chung, không malloc:
```c
typedef struct { Vector3 points[16]; int count; } SplinePath;
Vector3 SplinePath_Sample(const SplinePath *s, float t); // t = [0..1]
Vector3 SplinePath_Tangent(const SplinePath *s, float t);
```
Dùng cho: rồng mộc uốn lượn, tia điện dẫn đường, nước chảy theo địa hình.

---


```