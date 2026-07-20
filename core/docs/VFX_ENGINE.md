# VFX Engine Roadmap — Wuxing Skills

---

## Phase 1 — Shader & Texture Pipeline (highest priority)

### 1.1 Flow Map Shader `flow_map.fs`
The most important technique still missing. A flow map is a 2-channel RG texture encoding a UV velocity-field vector.

| Component | Description |
|---|---|
| `flow_map.fs` | Fragment shader: sample the flowmap → distort UV → blend 2 phases (offset 0.0 & 0.5) to hide the seam |
| `FlowMapConfig` (struct) | `flowTex`, `speed`, `strength`, `tiling`, `phaseOffset` |
| Applications | Trail ribbon texture (a sword streaming energy), Water skill (water surface), Fire skill (burning flame), Electric (a wave running along a lightning bolt) |

Core formula to implement in the shader:
```glsl
// Blend two phases to avoid a discontinuity each cycle
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
Currently particles/trails only use a static texture. Need to add:

| Field | Description |
|---|---|
| `frameCount`, `fps` | Frame count & playback speed |
| `rows`, `cols` | Atlas layout |
| `playMode` | `ANIM_ONCE`, `ANIM_LOOP`, `ANIM_RANDOM_START` |

Integration: add `SpriteAnim *anim` (nullable) to `ParticleConfig` and `TrailConfig`. Each update computes `currentFrame = (int)(age * fps) % frameCount` → computes the UV offset.

---

### 1.3 Multi-Stop Color Gradient — `color_gradient.h`
The current `colorStart` / `colorEnd` is too crude. Need:

```c
typedef struct { float t; Color color; } GradientStop;
typedef struct { GradientStop stops[8]; int count; } ColorGradient;
Color ColorGradient_Sample(const ColorGradient *g, float t);
```

Integration: `ParticleConfig` and `TrailConfig` take a `const ColorGradient *gradient` (nullable, falls back to the old start/end).

---

### 1.4 Standardized Shader Library — `shaders/`
Reorganize shaders into a shared library:

| File | Used for |
|---|---|
| `base_billboard.fs` | Basic particles (current) |
| `additive_soft.fs` | Fire, electricity, glow — BLEND_ADDITIVE + soft edge |
| `dissolve.fs` | Entity death — noise mask cutoff |
| `distortion.fs` | Heat/water — distorts the screen behind it via a normal map |
| `flow_map.fs` | (section 1.1) |
| `rim_glow.fs` | Glowing edge for trail ribbons (sword, dragon) |

---

## Phase 2 — Camera & Screen-Space Effects

### 2.1 Camera Shake — `camera_fx.h`
```c
void CameraFX_Shake(float trauma);      // trauma [0..1], power curve
void CameraFX_Update(Camera3D *cam, float dt);
```
Uses Perlin noise seeded by time, trauma decays automatically. Called from `SwordDeathCallback`, hit callbacks.

---

### 2.2 Screen Distortion Pass — `screen_distort.h`
Render the scene into a `RenderTexture2D`, then a fullscreen quad with `distortion.fs`:
- Takes a list of `DistortionSource` (world position, radius, strength)
- Projects to screen space, samples a normal map → distorts UV
- Used for: metal-vapor explosions, steam, shockwaves

---

### 2.3 Post-Processing Stack — `post_fx.h`
Fixed pass order, each pass togglable:

| Pass | Technique |
|---|---|
| Bloom | 2-pass Gaussian blur on the bright region (threshold) |
| Chromatic Aberration | Offsets the R/G/B channels radially from screen center |
| Color Grade | A 3D 16×16×16 LUT texture or simple curves |
| Vignette | Darkens the screen corners |

---

## Phase 3 — Extended Systems

### 3.1 Sub-Emitter System — extends `particle_system.h`
Add to `ParticleConfig`:
```c
const ParticleConfig *onDeathEmit;   // NULL = unused
int onDeathEmitCount;
const ParticleConfig *onLiveEmit;    // fires continuously while alive
float onLiveEmitRate;                // particles/second
```

---

### 3.2 Dynamic Point Light — `vfx_light.h`
Don't use raylib lights (too heavy). Implement a simple deferred-style approach:
```c
void VFXLight_Spawn(Vector3 pos, Color color, float radius, float lifetime);
void VFXLight_GetActive(VFXLightData *out, int *count, int maxCount);
```
Pass the light list into the shader via a uniform array. Cap at 8 simultaneous lights (enough for skill effects).

---

### 3.3 Decal System — `decal_system.h`
Ground marks when a skill hits the ground:
- A static pool of 32 decals
- Each decal: position, angle, scale, texture, lifetime, fade
- Rendered as a quad flush to the ground (small Y offset), `BLEND_ALPHA`

---

### 3.4 Spline / Path Sampler — `spline_path.h`
A shared Catmull-Rom spline, no malloc:
```c
typedef struct { Vector3 points[16]; int count; } SplinePath;
Vector3 SplinePath_Sample(const SplinePath *s, float t); // t = [0..1]
Vector3 SplinePath_Tangent(const SplinePath *s, float t);
```
Used for: a winding wood dragon, a lightning bolt following a path, water flowing along terrain.

---
