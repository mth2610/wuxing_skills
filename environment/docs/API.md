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
typedef void (*EnvShadowMapCasterCallback)(Shader depthShader, void *userData);
void EnvShadow_SetMapCasterCallback(EnvShadowMapCasterCallback callback, void *userData);
void EnvShadow_BeginStaticCapture(Vector3 center, float halfExtent);
void EnvShadow_EndStaticCapture(void);
void EnvShadow_InvalidateStaticCache(void);
bool EnvShadow_HasStaticCache(void);
Matrix EnvShadow_GetStaticLightVP(void);
Texture2D EnvShadow_GetStaticShadowMap(void);
```

Call `EnvShadow_SetFocus` before the frame's dynamic capture. Its `halfExtent`
is clamped to 8–96 m and should stay tight around the camera/player. For large
maps, call `EnvShadow_BeginStaticCapture` after static models are created, draw
only their geometry with `EnvShadow_GetDepthShader`, then end the capture. The
static projection is world-fixed (1024² desktop, 512² Android), while the
dynamic projection remains camera-following (2048²/1024²). Receivers combine
the two visibility layers. Changing the sun direction invalidates the cached
layer automatically; rebuild it at the map's chosen day/night cadence. Set
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
```

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
