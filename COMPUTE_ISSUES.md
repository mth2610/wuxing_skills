# Compute Issues / Backlog

Tasks for Compute Agent — assign when ready.

## 1. No dynamic global force for GPU particles (wind / vortex)

`GpuParticleConfig` only has `velocity` + `drag` (linear damping) — no way to apply a continuous force after spawn, unlike `core/particle_system.h`'s `ForceField` (vortex, gravity point, noise curl, per-layer falloff).

Impact: effects that need real swirling/curving motion on the GPU path (sandstorm, tornado, swirling embers) can only be faked by baking a tangential velocity at spawn time (works for short-lived, high-respawn-rate effects like rain/snow, but doesn't give true continuous curvature for longer-lived particles).

### Proposed fix — global wind + vortex uniform (not a full ForceField port)

Add a **global** force config (applies to all GPU particles at once, not per-particle layered like core's `ForceField` — porting the full 8-layer system to GPU is out of scope, too heavy for SSBO/compute shader).

```c
// compute/gpu_particle_system.h
typedef struct {
    Vector3 windDir;        // normalized
    float   windStrength;
    Vector3 vortexCenter;
    float   vortexStrength; // positive = ccw around Y axis
    float   vortexRadius;
} GpuForceConfig;

void GpuParticleSystem_SetForce(GpuForceConfig cfg);
void GpuParticleSystem_ClearForce(void);
```

`gpu_particles.comp` integrate step:
```glsl
uniform vec3  u_windDir;
uniform float u_windStrength;
uniform vec3  u_vortexCenter;
uniform float u_vortexStrength;
uniform float u_vortexRadius;

vec3 toCenter = particle.position - u_vortexCenter;
float dist = length(toCenter.xz);
vec3 tangent = vec3(-toCenter.z, 0.0, toCenter.x) / max(dist, 0.001);
float falloff = clamp(1.0 - dist / u_vortexRadius, 0.0, 1.0);

particle.velocity += u_windDir * u_windStrength * dt;
particle.velocity += tangent * u_vortexStrength * falloff * dt;
```

**Constraints:**
- Must implement the identical math on the CPU/VBO fallback path too — COMPUTE_API.md §1 guarantees "API giống nhau ở cả hai path", a mismatch here would break that contract.
- This is purely additive in `compute/` — does **not** touch `core/force_field.h` or `ParticleConfig`. No breaking change to CORE_API.md.
- Document in `COMPUTE_API.md` as a new §8.
- Start with one global wind + one global vortex (covers sandstorm/tornado needs). If multiple simultaneous vortices are needed later, extend to a small fixed array (e.g. `MAX_GPU_FORCES = 4`) rather than full per-particle layering.

---

## 2. Usage guidance — long-duration global effects (snow/rain/sandstorm)

Not a code defect, but worth documenting in `COMPUTE_API.md` so Map/Environment agents don't have to re-derive this each time:

- **Spawn at a steady per-frame budget** (e.g. 5–15 particles/frame for light snow), never in bursts — active particle count stabilizes at `spawnRate × lifetime` and stays flat regardless of match duration (ring-buffer, fixed `MAX_GPU_PARTICLES = 8192`, no leak).
- **`lifetime` must match fall distance**: no ground collision exists in the GPU particle system — particles vanish when `lifetime` expires, not on contact. Tune `lifetime ≈ (spawnHeight − groundY) / |velocity.y|`, and fade `colorEnd.a → 0` to hide any mistiming.
- **No per-frame dynamic force needed for gentle snow** — bake a small randomized horizontal drift into `velocity` at spawn time; only sandstorm/tornado-grade effects need item #1 above.
- **Ownership**: global/environment effects (snow, rain, sandstorm) belong in `Update{Prefix}Map(dt)` or `Environment_Update(dt)` — never in skill code. Skills use `core/particle_system.h` (ForceField support); compute is reserved for global/atmospheric effects.
- Never call `GpuParticleSystem_Init/Unload` outside `main.c` — already centrally managed.

Action: fold this into `COMPUTE_API.md` §7 (Mở rộng) as explicit guidance, possibly with a snow example alongside the existing rain example.

---
*Logged from compute-shader force field discussion, 2026-06-30.*
