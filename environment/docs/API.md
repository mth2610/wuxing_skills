# Environment System API

Documents the **Environment** module (`environment/environment_system.h`). Responsible for
lighting, fake shadows (Smart Fake Shadow), and general atmosphere for the whole Wuxing Skills
engine.

## 1. Data Structures

### `EnvShadowShapeType`
Defines the shape of an object so the system computes an appropriate shadow for it.

```c
typedef enum {
    ENV_SHAPE_SPHERE,   // Characters, monsters, sphere-like objects (drops a capsule shadow from the base).
    ENV_SHAPE_CYLINDER, // Stone pillars, tree trunks, upright cylindrical objects (long capsule shadow).
    ENV_SHAPE_BOX       // Box-shaped objects (e.g. chests, block obstacles).
} EnvShadowShapeType;
```

### `EnvFogConfig`
Fog configuration (if fog is used for atmosphere).

```c
typedef struct {
    Color color;    // Fog color
    float start;    // Distance from camera where fog starts
    float end;      // Distance where fog becomes fully opaque
    float density;  // Fog density
    bool enabled;   // Fog on/off
} EnvFogConfig;
```

---

## 2. Lifecycle Functions

Called automatically by Core in `sandbox_core.c`. Other modules (e.g. Skills) **should not** call
these.

```c
// Initializes default environment parameters (normalizes sun direction, etc).
void Environment_Init(void);

// Updates the environment over time (e.g. day/night cycle, moving clouds).
void Environment_Update(float dt);
```

The optional real directional-shadow layer can follow a large map instead of
remaining fixed over the default arena:

```c
void EnvShadow_SetFocus(Vector3 center, float halfExtent); // light-space texel stabilized
Vector3 EnvShadow_GetFocus(void);
float EnvShadow_GetHalfExtent(void);
typedef void (*EnvShadowMapCasterCallback)(Shader depthShader, void *userData);
void EnvShadow_SetMapCasterCallback(EnvShadowMapCasterCallback callback, void *userData);
void EnvShadow_BeginStaticCapture(Vector3 center, float halfExtent);
void EnvShadow_EndStaticCapture(void);
void EnvShadow_InvalidateStaticCache(void);
bool EnvShadow_HasStaticCache(void);
bool EnvShadow_NeedsStaticCapture(void);
Matrix EnvShadow_GetStaticLightVP(void);
Texture2D EnvShadow_GetStaticShadowMap(void);
```

Call `EnvShadow_SetFocus` before the frame's dynamic capture. Its `halfExtent`
is clamped to 8–96 m and should stay tight around the camera/player. For large
maps, the getters expose the stabilized capture region so map-owned caster
culling can follow the shadow cascade instead of guessing from camera range.
Call `EnvShadow_BeginStaticCapture` after static models are created, draw
only their geometry with `EnvShadow_GetDepthShader`, then end the capture. The
static projection is world-fixed (1024² desktop, 512² Android), while the
dynamic projection remains camera-following (2048²/1024²). Receivers combine
the two visibility layers. `EnvShadow_NeedsStaticCapture` becomes true when
shadows are enabled without a cache or the directional light has changed;
maps should rebuild then, avoiding both stale lighting and per-frame retries
when the platform has no static target. Set
`WUXING_SHADOW_STATIC_VERIFY=1` to read it back once and log occupied texels.
Use `WUXING_SHADOW_DYNAMIC_VERIFY=1` for the equivalent one-shot readback of
the camera-following dynamic target after its first completed capture. To
inspect an off-camera region, set both `WUXING_SHADOW_FOCUS_X` and
`WUXING_SHADOW_FOCUS_Z`; the ordinary camera focus remains the default.

Maps with animated or camera-local geometry can register one
`EnvShadowMapCasterCallback`. It runs inside the existing dynamic shadow pass,
so the map can submit real caster geometry without opening a second target.
Clear the callback before unloading the map-owned models. The callback is an
extension point only; Environment still owns the render target and matrices.

---

## 3. Smart Fake Shadow System — Most Important

The most important API for other modules (e.g. `MeshSystem`, `Seismic Pillar Skill`) to draw
shadows for their objects.

```c
void Environment_DrawSmartShadow(Vector3 pos, EnvShadowShapeType shape, float width, float height);
```

**Parameters:**
*   `pos`: Coordinate (Vector3) at the center of the object's base (ground contact point).
*   `shape`: Shape kind (`ENV_SHAPE_SPHERE`, `ENV_SHAPE_CYLINDER`, `ENV_SHAPE_BOX`).
*   `width`: Object width.
*   `height`: Object height (important for computing capsule shadow elongation).

**Key features:**
*   **Shadow Scaling & Fading:** the system automatically measures the object's height `pos.y`.
    If the object rises higher, the shadow automatically shrinks and fades — very realistic.
*   **Directional Accuracy:** the shadow always leans precisely in the direction of the sun
    (`s_sunDirection`). Current default direction is Southwest.
*   **Soft Edges:** the shadow has no hard edges or double-blend overlap; edges fade smoothly into
    space.

**Example usage in a Skill:**
```c
// Inside a Seismic Pillars Draw function:
Environment_DrawSmartShadow(pillarPos, ENV_SHAPE_CYLINDER, 15.0f, 50.0f);
```

---

## 4. Getter / Setter Functions

Let other systems (e.g. Time-of-Day System, Weather System) modify lighting, sun direction, and
shadow color in real time.

```c
// --- Sun direction ---
Vector3 Environment_GetSunDirection(void);
void Environment_SetSunDirection(Vector3 dir); // Auto-normalizes the vector

// --- Sun color ---
Color Environment_GetSunColor(void);
void Environment_SetSunColor(Color col);
float Environment_GetSunIntensity(void);
void Environment_SetSunIntensity(float intensity);

// --- Ambient color ---
Color Environment_GetAmbientColor(void);
void Environment_SetAmbientColor(Color col);

// --- Hemispheric ambient (Real Shading P1c) — derived from the flat ambient
// above; feeds surface_lit's hemispheric term (sky above / ground bounce below).
Color Environment_GetSkyAmbient(void);    // = ambient * 1.25 (cooler-tinted blue channel)
Color Environment_GetGroundAmbient(void); // = ambient * ~0.5 (dimmer, slight warm shift)

// --- Shadow color ---
Color Environment_GetShadowColor(void);
void Environment_SetShadowColor(Color col);

// --- Fog ---
EnvFogConfig Environment_GetFogConfig(void);
void Environment_SetFogConfig(EnvFogConfig config);

// --- Advanced Atmosphere & Physical Fog (Ghost of Tsushima style) ---
AtmosphereProfile Environment_GetAtmosphereProfile(void);
void              Environment_SetAtmosphereProfile(const AtmosphereProfile *profile);

// --- Local Fog Volumes (Props, Map features, and Skill VFX) ---
int                     FogVolume_Create(const LocalFogVolume *volume);
void                    FogVolume_Update(int id, const LocalFogVolume *volume);
void                    FogVolume_Destroy(int id);
void                    FogVolume_ClearAll(void);
int                     FogVolume_SpawnTransient(Vector3 pos, float radius, Color color, float density, float duration);
int                     FogVolume_GetActiveCount(void);
const LocalFogVolume*   FogVolume_GetByIndex(int index);
const LocalFogVolume*   FogVolume_GetById(int id);
```

`environment/environment_system.c` keeps sunlight tint and direct radiance
separate: normalize `Environment_GetSunColor().rgb` to `[0,1]` and multiply by
`Environment_GetSunIntensity()` when uploading direct sunlight. Intensity
defaults to `1`, clamps finite inputs to `[0,64]`, and resets nonfinite inputs
to `1`. `EnvFrameLighting.sunIntensity` exposes the same scale; changing it
increments the lighting version. Ambient, fog density, and cloud visibility
are unaffected. Maps opting in restore intensity `1` on exit. Existing tint
getters and lighting presets retain their contracts; presets do not overwrite
the separately configured intensity. `Environment_Init()` resets it to `1`.

`AtmosphereProfile.start` is the distance-fog onset in meters. In the volumetric
pass, `start > 3 m` selects distant framing: global haze and canopy beams fade
in behind the camera target over 12%–80% of the projected ground half-span.
The span follows camera zoom and pitch. Local volumes retain world placement
with a 1–3 m horizontal clearance around the target. Near-start profiles retain
their distance-based behavior.

---

## 5. Day/Night Lighting Cycle (Time-of-Day)

Keyframe-based blend system over time, used to give a map lighting that moves through milestones
(dawn → noon → dusk → night) **without needing separate day/night textures or geometry** — only
the lighting (ambient, sun color/direction, shadow color, fog) changes.

**Fully opt-in / backward-compatible:** if `Environment_SetTimeOfDayPresets()` is never called (or
called with speed = 0, the default), `Environment_Update()` does nothing different from before —
static one-time `Environment_Set*()` calls in a map's `Init()` remain fully in effect, undisturbed
by this system.

```c
#define MAX_TIME_OF_DAY_PRESETS 8

typedef struct {
    Color        ambientColor;
    Color        sunColor;
    Vector3      sunDirection;
    Color        shadowColor;
    EnvFogConfig fog;
} EnvLightingPreset;

// Declares the keyframes (time milestones) for a full lighting cycle.
void  Environment_SetTimeOfDayPresets(const EnvLightingPreset *presets, const float *timePoints, int count);

// Cycle speed, in cycles/second. Default 0 = paused/off.
void  Environment_SetTimeOfDaySpeed(float cyclesPerSecond);

// Manually jumps to a point in time within the cycle.
void  Environment_SetTimeOfDay(float t);
float Environment_GetTimeOfDay(void);
```

**Parameters & constraints:**
*   `timePoints`: normalized values `[0,1)`, **must be sorted ascending**, count `<=
    MAX_TIME_OF_DAY_PRESETS` (8).
*   **Wrap-around:** the segment from `timePoints[count-1]` through `1.0`/`0.0` back to
    `timePoints[0]` is smoothly interpolated too, **not** a hard cut back to the first preset.
*   Calling `Environment_SetTimeOfDayPresets()` **overwrites** all previously set presets.
*   `Environment_SetTimeOfDaySpeed(0)` (default), or never calling `SetTimeOfDayPresets` →  the
    system is completely inert; `Environment_Update()` behaves exactly as before this feature
    existed.
*   **`fog.enabled` constraint:** it's a `bool`, so it can't be linearly interpolated. **All
    presets passed in the same `SetTimeOfDayPresets()` call must agree on `fog.enabled`** (all
    `true` or all `false`). During a blend, the system only reads `fog.enabled` from one of the two
    presets being interpolated — mixing `true`/`false` across presets makes fog on/off jump
    arbitrarily between neighboring presets instead of transitioning smoothly.
*   During a blend, `sunDirection` is linearly interpolated per-component then `Normalize()`d
    (same convention as `Environment_SetSunDirection`); `Color` values (including `fog.color`) are
    lerped per byte channel; `fog.start`/`fog.end`/`fog.density` are lerped as floats.
*   The blended result is written directly into the same static state that
    `Environment_DrawSmartShadow()` and the Getters in section 4 read — nothing else needs to
    change.

**Example usage (a map wants a real-time 20-minute day/night cycle):**
```c
EnvLightingPreset presets[3] = {
    { .ambientColor = {50,50,70,255}, .sunColor = {255,245,230,255}, .sunDirection = {0.5f,-0.8f,-0.3f}, .shadowColor = {8,8,12,180}, .fog = {.enabled = false} }, // noon
    { .ambientColor = {30,20,40,255}, .sunColor = {255,140,80,255},  .sunDirection = {0.9f,-0.2f,-0.1f}, .shadowColor = {8,8,12,180}, .fog = {.enabled = false} }, // dusk
    { .ambientColor = {10,10,25,255}, .sunColor = {60,70,120,255},   .sunDirection = {-0.3f,-0.6f,0.4f}, .shadowColor = {4,4,8,180},  .fog = {.enabled = false} }, // night
};
float times[3] = { 0.0f, 0.4f, 0.7f };
Environment_SetTimeOfDayPresets(presets, times, 3);
Environment_SetTimeOfDaySpeed(1.0f / 1200.0f); // 1 cycle / 20 real minutes
```

## 6. Shared Cloud Shadows

`environment/shaders/hemisphere_lighting.glsl` provides
`Environment_HemisphereIrradiance(vec3 unitNormal, vec3 skyRadiance, vec3 groundRadiance)`.
Pass a world-space unit normal and linear sky/ground colors from
`Environment_GetFrameLighting()` in `environment/environment_system.c`.
The helper interpolates ground to sky with clamped `(normal.y + 1) / 2`,
introduces no uniforms, and leaves sunlight/cloud visibility to the caller.
It returns irradiance before surface albedo and canopy ambient occlusion.
The two-argument overload `(vec3 unitNormal, vec3 ambientRadiance)` derives
sky/ground RGB with the same clamped multipliers as the Environment getters,
using the configured ambient intensity without a minimum brightness floor.
It omits the getters' 8-bit channel quantization.

`environment/environment_system.h` exposes map-opt-in cloud configuration and a
resolved receiver snapshot:

```c
EnvCloudShadowConfig Environment_GetCloudShadowConfig(void);
void Environment_SetCloudShadowConfig(const EnvCloudShadowConfig *config);
EnvCloudShadowFrame Environment_GetCloudShadowFrame(void);
void Environment_BindCloudShadowShader(Shader shader);
```

`environment/environment_system.c` defaults to disabled. Set the configuration
on map entry and call `Environment_SetCloudShadowConfig(NULL)` on exit to disable
and reset drift. Configuration fields are finite-clamped; enabling lazily loads
the resource-manager-owned `environment/textures/cloud_noise.png`. If loading
fails, receiver strength resolves to zero. Core macro wind XZ velocity transports
the repeating field; local skill gusts/vortices do not move clouds.

The frame contains `noiseTexture`, `uvTransform` (inverse repeat size, wrapped
UV drift XY, attenuation), `shape` (coverage, softness, plane height, reserved),
and `projection` (sun travel XZ divided by clamped downward Y). Upload these to
`u_cloudUV`, `u_cloudShape`, and `u_cloudProjection` while the receiver shader is
active. Bind the shared noise texture to the receiver's cloud sampler.

`environment/shaders/cloud_shadow.glsl` provides
`Environment_CloudVisibility(sampler2D cloudNoise, vec3 worldPosition)`. Pass an
actual world-space position; the helper projects it to the cloud plane and
returns direct sunlight visibility. Multiply direct diffuse, specular, and
transmission only; preserve ambient/sky light and surface albedo. Every receiver
must use the same snapshot and sampler for coherent grass/ground/prop shading.
Cloud visibility fades near the horizon. Cloud configuration has its own
version; changing configuration or advancing drift does not invalidate the
directional static-shadow cache.

`Environment_BindCloudShadowShader` in `environment/environment_system.c`
uploads these shared uniforms and binds `u_cloudNoise` at raw texture unit 3,
restoring active texture slot 0. Call it while the receiver shader is active;
it does not open/close a shader scope. Raw texture-unit numbers are not
material-map enums. For direct `DrawMesh`/`DrawModel`, reserve an unused material
slot, assign its shader-location entry to `u_cloudNoise`, and bind the frame's
`noiseTexture` in that slot. The material draw then selects that slot's sampler
unit. Alternatively, upload the frame parameters and bind a reserved raw unit
explicitly; do not overwrite a material slot that supplies grass, dirt, or
shadow textures.
Shader locations use a fixed 16-entry cache, cleared by `Environment_Init`.

`environment/tools/bake_cloud_noise.py` reproduces the fixed 64² RGBA8 periodic
field using the Python standard library. It performs no runtime generation.

## Patch Log

| Date | Editor (human/AI) | Section edited | Based on which source | Tier |
|---|---|---|---|---|
| 2026-10-09 | Codex | §4 opt-in direct sun radiance scale | `environment/environment_system.h`, `environment/environment_system.c` | Ground-truth |
| 2026-10-09 | Codex | §6 hemispheric irradiance helper | `environment/shaders/hemisphere_lighting.glsl` | Ground-truth |
| 2026-09-28 | Codex | §4 `AtmosphereProfile.start` volumetric onset | `environment/environment_system.h`, `core/volumetric/volumetric_fog_distance.h`, `core/volumetric/shaders/volumetric_fog.fs` | Ground-truth |
| 2026-10-02 | Codex | §6 shared opt-in cloud visibility | `environment/environment_system.h`, `environment/environment_system.c`, `environment/shaders/cloud_shadow.glsl` | Ground-truth |
