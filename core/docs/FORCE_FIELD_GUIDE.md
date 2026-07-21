# ForceField System — Usage Guide

> **In short:** ForceField is the only way to steer particle motion beyond the initial `velocity` and `drag`. Each particle attaches **one** ForceField, and that ForceField holds **multiple force layers** (gravity, noise, vortex, viscosity...) stacked on top of each other.
>
> ⚠️ **Scale note:** the numeric `strength` values in the examples below predate the meter-scale conversion (1 unit = 1 m). They are old ×100 magnitudes — divide by ~100 for meter-scale (e.g. `500` → `~5`, gravity `~9.8` not `845`). See `AGENT_CODE_STANDARD.md` §2. The *shapes* (which layers, relative ratios) still teach correctly.

---

## 1. Core concept

```
ParticleConfig
├── velocity        → initial velocity (which way it's fired)
├── drag            → linear per-frame deceleration (a simple brake)
└── forceField ──→  ForceField
                    ├── ForceLayer 0   (e.g. GRAVITY_DIR ↓)
                    ├── ForceLayer 1   (e.g. NOISE_CURL)
                    └── ForceLayer 2   (e.g. VISCOSITY)
```

- **ForceField** is a static container (a `static` variable in the skill file). Many particles can point at the same ForceField.
- Every frame, `UpdateParticles()` automatically calls `ForceField_Evaluate()` and `ForceField_GetViscosityDamping()` for each live particle.
- The skill file **owns** the ForceField memory. The particle system only holds a pointer, it does not copy.

---

## 2. Force types (ForceType)

### Additive acceleration — added to velocity

| ForceType | Effect | Key fields |
|---|---|---|
| `FORCE_GRAVITY_DIR` | Constant acceleration in any direction. Gravity, updraft. | `direction` (pre-normalized), `strength` (m/s²) |
| `FORCE_GRAVITY_POINT` | Pull or push particles toward a point. `strength < 0` = push away. | `origin`, `strength`, `radius`, `falloff` |
| `FORCE_VORTEX` | Swirl around an axis. Makes whirlwinds, tornadoes. | `origin` (point on axis), `direction` (axis dir), `strength`, `radius` |
| `FORCE_WIND` | Directional wind + Perlin noise for natural gusting. | `direction`, `strength`, `noiseScale`, `noiseSpeed` |
| `FORCE_NOISE_PERLIN` | Push particles along Perlin noise. Jitter, dust, chaotic smoke. | `strength`, `noiseScale`, `noiseSpeed` |
| `FORCE_NOISE_CURL` | Curl noise — divergence-free. Smooth swirling, no clumping. | `strength`, `noiseScale`, `noiseSpeed` |
| `FORCE_DRAG` | Drag proportional to velocity (opposes `vel`). | `strength` |

### Multiplicative damping — multiplied into velocity

| ForceType | Effect | Key fields |
|---|---|---|
| `FORCE_VISCOSITY` | `vel *= exp(-strength * dt)`. Particles thicken. Use for water, fluids. | `strength` (~1–10) |

> ⚠️ `FORCE_VISCOSITY` produces **no acceleration** — it only acts via `ForceField_GetViscosityDamping()`. A particle must have `forceField != NULL` for viscosity to run.

---

## 3. ForceLayer parameters

```c
typedef struct {
    ForceType type;

    Vector3 origin;     // vortex/attract center — NOT used for GRAVITY_DIR, DRAG, VISCOSITY
    Vector3 direction;  // force direction / vortex axis (pre-normalized)

    float strength;     // magnitude: m/s² for accel, damping constant for VISCOSITY
    float radius;       // 0.0 = infinite; > 0 = only within a sphere of this radius
    float falloff;      // 0.0 = constant; 1.0 = linear; 2.0 = quadratic

    float noiseScale;   // spatial frequency: small = big/slow noise; large = small/fast noise
    float noiseSpeed;   // how fast the noise scrolls over time
} ForceLayer;
```

**radius + falloff rules:**
- `radius = 0` → force applies uniformly to every particle, independent of position
- `radius > 0, falloff = 0` → constant inside the sphere, zero outside
- `radius > 0, falloff = 1` → strongest at center, decays linearly to 0 at the edge
- `radius > 0, falloff = 2` → quadratic decay (like an electromagnetic force)

---

## 4. Creating a new ForceField in a skill

### Step 1 — declare a static at the top of the file

```c
static ForceField s_myField;
```

### Step 2 — initialize in InitXxxSkill()

```c
void InitMySkill(...) {
    ForceField_Clear(&s_myField);  // always Clear first

    ForceField_AddLayer(&s_myField, (ForceLayer){
        .type      = FORCE_GRAVITY_DIR,
        .direction = {0, -1, 0},
        .strength  = 500.0f,
    });

    ForceField_AddLayer(&s_myField, (ForceLayer){
        .type       = FORCE_NOISE_CURL,
        .strength   = 40.0f,
        .noiseScale = 0.015f,
        .noiseSpeed = 0.6f,
    });
}
```

### Step 3 — attach to a particle at spawn

```c
ParticleConfig cfg = {0};
cfg.position     = spawnPos;
cfg.velocity     = vel;
cfg.radius       = 5.0f;
cfg.lifetime     = 1.0f;
cfg.colorStart   = RED;
cfg.colorEnd     = (Color){0, 0, 0, 0};
cfg.forceField   = &s_myField;   // ← attach here (holds the FORCE_DRAG layer)
SpawnParticle(cfg);
```

### Step 4 (optional) — move the vortex origin with a moving emitter

```c
// In UpdateXxxSkill(), before spawning:
s_myField.layers[0].origin = emitters[e].currentPos;
```

---

## 5. Recipes for common effects

### 🔥 Rising, curling fire
```c
{ .type = FORCE_GRAVITY_DIR, .direction = {0,1,0}, .strength = 200.0f }
{ .type = FORCE_NOISE_CURL,  .strength = 80.0f, .noiseScale = 0.02f, .noiseSpeed = 1.2f }
```

### 🌪️ Tornado
```c
{ .type = FORCE_VORTEX,      .origin = center, .direction = {0,1,0}, .strength = 300.0f, .radius = 150.0f }
{ .type = FORCE_NOISE_CURL,  .strength = 30.0f, .noiseScale = 0.01f, .noiseSpeed = 0.8f }
```

### 💧 Water splash
```c
{ .type = FORCE_GRAVITY_DIR, .direction = {0,-1,0}, .strength = 845.0f }
{ .type = FORCE_NOISE_PERLIN,.strength = 20.0f, .noiseScale = 0.008f, .noiseSpeed = 0.3f }
{ .type = FORCE_VISCOSITY,   .strength = 4.8f }   // droplets clump, thicken
```

### ⚡ Lightning / jittering spark
```c
{ .type = FORCE_NOISE_PERLIN,.strength = 120.0f, .noiseScale = 0.05f, .noiseSpeed = 3.0f }
```

### 🌿 Drifting leaves
```c
{ .type = FORCE_NOISE_CURL,  .strength = 25.0f, .noiseScale = 0.012f, .noiseSpeed = 0.4f }
{ .type = FORCE_GRAVITY_DIR, .direction = {0,-1,0}, .strength = 65.0f }
```

---

## 6. Limits & notes

| | Value | Note |
|---|---|---|
| Max layers / ForceField | **8** | Enough for most effects |
| Number of ForceFields | Unlimited | Each skill manages its own |
| Many particles sharing one field | ✅ | Point at the same pointer |
| ForceField on GPU (compute shader) | ❌ | GPU integrates velocity only |
| FORCE_VISCOSITY needs forceField != NULL | ✅ | No field = no viscosity |

**Curl vs Perlin:**
- **Perlin** — cheaper, but has divergence (particles can clump)
- **Curl** — costlier (6 Perlin samples), divergence-free, more natural swirl, no clumping

---

## 7. Noise primitives — standalone use

```c
float   n    = Noise_Perlin3D(x, y, z);          // [-1, 1]
float   v    = Noise_Value3D(x, y, z);            // [0, 1], ~2x cheaper
Vector3 curl = Noise_Curl3D(x, y, z, scale);      // divergence-free Vector3
```

---

## 8. Per-frame flow diagram

```
UpdateParticles(dt)
│
└─ for each particle i:
    ├─ lifetime -= dt  →  if dead, remove from the array
    │
    └─ if forceField != NULL:
        ├─ acc  = ForceField_Evaluate(ff, pos, vel, time, axisOrigin, axisDir)
        │         └─ sum of all additive layers (gravity/noise/vortex/wind/drag)
        ├─ vel += acc * dt
        │
        ├─ damp = ForceField_GetViscosityDamping(ff, dt)
        │         └─ product of exp(-s*dt) over all VISCOSITY layers
        └─ vel *= damp
    
    pos += vel * dt
```
