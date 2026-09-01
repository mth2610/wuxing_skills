# COMPUTE MODULE API

> Shared GPU compute module — particle physics, rain, simulation.
> Lives in `core/particles/gpu/` and is private to the particle manager.

---

## 1. Architecture overview

The module auto-detects GPU capability at runtime and picks one of two paths:

| Path | Condition | Description |
|---|---|---|
| **COMPUTE** | GL 4.3+ (desktop) or GLES 3.1+ (Android Mali-G68+) | Physics fully on GPU — compute-shader dispatch, SSBO |
| **CPU/VBO** | GL 3.3 (macOS) or older devices | Physics on CPU, VBO upload per frame |

The caller doesn't need to check the path — the API is identical on both.

---

## 2. Game-loop integration (main.c)

```c
#include "core/particles/gpu/particle_gpu_legacy.h"

// After InitWindow():
GpuParticleSystem_Init();

// In the game loop — Update phase:
GpuParticleSystem_Update(dt);

// In the game loop — 3D draw phase (after BeginMode3D / MyBeginMode3D):
GpuParticleSystem_Draw(camera, particleTexture);

// Cleanup:
GpuParticleSystem_Unload();
```

---

## 3. API

### `GpuParticleSystem_Init(void)`
Detects compute capability, initializes shaders and buffers. Call once after `InitWindow()`.

### `GpuParticleSystem_Spawn(GpuParticleConfig cfg)`
Spawn one particle. Works on both paths. Ring-buffer — old particles are overwritten when the pool is full.

```c
typedef struct {
    Vector3 position;
    Vector3 velocity;
    Color   colorStart;
    Color   colorEnd;
    float   radius;
    float   lifetime;  // seconds
    float   drag;      // 0.0 = no drag | 0.98 = light drag | 1.0 = stop instantly

    // Optional ForceField. NULL evaluates to zero acceleration. The COMPUTE
    // shader and CPU/VBO fallback use the same force-field contract. axisOrigin/axisDir:
    // the dynamic axis for a FORCE_RADIAL_AXIS/FORCE_VORTEX_AXIS layer inside
    // forceField (ignored if you don't use those layer types).
    const ForceField *forceField;
    Vector3 axisOrigin;
    Vector3 axisDir;

    // Optional shared waypoint route. Force/drag run first, then finite path
    // steering; NULL keeps ballistic integration. onTargetEmit is spawned when
    // the swept arrival test reaches the final target.
    const ParticleTravelPath *travelPath;
    const ParticleConfig *onTargetEmit;
    int onTargetEmitCount;
} GpuParticleConfig;
```

> [!NOTE]
> The `forceField` pointer is registered into an internal registry and re-packed EVERY FRAME
> — it must point to long-lived memory (static/pool), never a local stack variable. If
> compute is unavailable, the system falls back to CPU/VBO and evaluates the same field.

**Rain-spawn example:**
```c
GpuParticleConfig rain = {
    .position   = (Vector3){ x, 200.0f, z },
    .velocity   = (Vector3){ wind_x, -300.0f, wind_z },
    .colorStart = (Color){ 180, 200, 255, 200 },
    .colorEnd   = (Color){ 180, 200, 255, 0 },
    .radius     = 2.0f,
    .lifetime   = 1.5f,
    .drag       = 0.02f,
};
GpuParticleSystem_Spawn(rain);
```

### `GpuParticleSystem_Update(float dt)`
Update physics. COMPUTE path: dispatch the compute shader. CPU/VBO path: CPU loop.

### `GpuParticleSystem_Draw(Camera3D camera, Texture2D texture)`
Draw billboard particles. Call in the 3D draw phase. On the compute path, normal
billboards use the previous-frame ScreenDistort depth snapshot and fade over 0.35 m
where they intersect scene geometry; fluid surface capture/thickness passes are excluded.

### `GpuParticleSystem_Unload(void)`
Free GPU buffers and shaders. Call at shutdown.

### `GpuParticleSystem_IsComputeActive(void) → bool`
`true` if the GPU compute path is active.

### `GpuParticleSystem_ActiveCount(void) → int`
Number of live particles.

### `GpuParticleSystem_DrawDebug(int x, int y)`
Show a debug overlay (path, GL version, particle count).

### `GpuParticleSystem_SetVectorFieldTexture(int slot, Texture2D tex)`
Bind a "vector field" texture into a slot (`0` or `1`, see `GPU_VECTOR_FIELD_SLOTS`) so
particles using `ForceLayer.type = FORCE_VECTOR_TEXTURE` (see `../../core/docs/API.md` §5)
sample velocity from it instead of a procedural formula. ONLY effective on the COMPUTE
path. Texture format: the RG channels = flow direction XZ remapped `[-1,1] -> [0,1]`.

```c
Texture2D smokeFlow = LoadTexture("assets/flow/smoke_wall_hug.png");
GpuParticleSystem_SetVectorFieldTexture(0, smokeFlow);

static ForceField s_smokeField; // static — lives as long as the particles
ForceField_Clear(&s_smokeField);
ForceField_AddLayer(&s_smokeField, (ForceLayer){
    .type      = FORCE_VECTOR_TEXTURE,
    .origin    = (Vector3){600.0f, 0.0f, 440.0f}, // box center (y ignored)
    .direction = (Vector3){400.0f, 0.0f, 400.0f}, // box half-extent (xz)
    .strength  = 120.0f,
    .noiseScale = 0.0f, // slot 0
});
```

> [!NOTE]
> The texture is not owned by this module — the caller must `UnloadTexture` it when no
> longer needed (after clearing the slot with `tex.id == 0`, or after every `ForceField`
> using that slot stops). Not yet confirmed on real hardware — macOS caps at GL 4.1 so it
> always falls back to CPU/VBO and never exercises this COMPUTE path on the dev machine.
> Verify on an Android / GL 4.3+ device before treating it as stable.

---

## 4. Limits

```c
#define MAX_GPU_PARTICLES 8192  // Ring-buffer size
#define GPU_VECTOR_FIELD_SLOTS 2  // Concurrent vector-field textures
```

Each particle occupies 128 bytes in the SSBO (eight `vec4`s). Routes are not
copied per particle: up to 32 shared routes live in a separate fixed 9 KiB path
SSBO, while each particle stores only route-slot and waypoint indices. No runtime
allocation is performed.

---

## 5. Shader files

All shaders live in `core/particles/shaders/gpu/`:

| File | Used by | Description |
|---|---|---|
| `gpu_particles.comp` | COMPUTE path | Lifetime, force/drag, guided path steering, swept target arrival |
| `gpu_particles_ssbo.vs` | COMPUTE path | Vertex: billboard from SSBO + gl_VertexID |
| `gpu_particles.fs` | Compute path | Fragment: texture * color interpolate + previous-frame scene-depth soft fade |
| `gpu_particles_vbo.vs` | CPU/VBO path | Vertex: consumes the VBO built on the CPU |

---

## 6. Android / GLES rules

### Compute shader (`.comp`)
- Source keeps `#version 310 es` (GLES 3.1)
- `CompileComputeShader()` runtime-patches → `#version 430 core` on desktop
- The build script detects `layout(local_size_x` → targets ES 3.1 (nothing else needed)

### SSBO vertex shader (`_ssbo.vs`)
- On desktop: `#version 430 core`, loaded via `ResourceManager_LoadShader`
- On Android: the build script converts `#version 430 core` → `#version 310 es` (detects `layout(std430`)
- `ShaderPreprocessor` skips `#version 310 es` (only rewrites `#version 330`) → GLES 3.1 takes it directly

### CPU/VBO shader (`_vbo.vs`, `.fs`)
- Desktop: `#version 330 core`
- Android: the build script converts → `#version 100` (GLES 1.00 / ES 2.0)
- No SSBO features → GLES 3.1 not required

### Precision rule (strict GLES 3.x)
A uniform appearing in both VS and FS must use the same precision. The compute shader needs:
```glsl
precision highp float;
precision highp int;
precision highp sampler2D;  // required if the compute shader has a sampler2D uniform
                            // (e.g. uVectorField0/1) — samplers have no default
                            // precision in GLES, unlike float/int.
```

---

## 7. Extending (Rain, Fog, etc.)

For environment to make rain:
```c
#include "core/particles/gpu/particle_gpu_legacy.h"

// In Environment_Update(dt):
for (int i = 0; i < rain_spawn_count; i++) {
    GpuParticleSystem_Spawn((GpuParticleConfig){
        .position = randomRainPosition(),
        .velocity = (Vector3){ wind.x, -400.0f, wind.z },
        .colorStart = (Color){200, 220, 255, 180},
        .colorEnd   = (Color){200, 220, 255, 0},
        .radius   = 1.5f,
        .lifetime = 1.2f,
        .drag     = 0.01f,
    });
}
```

No Init/Update/Draw needed in environment — main.c manages it centrally.
