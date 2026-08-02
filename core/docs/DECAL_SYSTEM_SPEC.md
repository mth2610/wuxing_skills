# Decal System Rebuild Specification

Status: proposed  
Owner: Decal Agent under Core Engine  
Reviewers: Core Agent; Vulkan Backend Agent for rlvk-sensitive changes  
Targets: PC OpenGL/Vulkan and Android Vulkan/GLES, mobile-first  
Language/runtime: strict C99, Raylib 6.0, static allocation only

## 1. Decision

Rebuild the existing subsystem incrementally around:

- one `DecalInstance` representation;
- data-driven `DecalMaterialId` descriptors;
- fixed pools with generation-safe handles;
- mesh-stamp and oriented/projected rendering paths;
- CPU visibility, LOD, priority, and screen-coverage budgets;
- material buckets and bounded draw calls;
- optional PC-only screen-space decals only after a deferred-compatible scene
  path exists.

Do not replace the working system with a greenfield DBuffer renderer. Preserve a
shippable path after every phase.

## 2. Assessment of the proposed architecture

### Adopt

- One decal instance type; no Fire/Blood/Ice instance subclasses.
- Material IDs instead of gameplay-specific renderer branches.
- Static free/active lists and O(1) allocation/deactivation.
- Mesh decals as the mobile default.
- Bounded projected decals for large or dynamic effects.
- CPU frustum/distance/priority culling.
- Material buckets before rendering.
- LOD and screen-coverage limits.
- Data-driven fade, flow, flipbook, emission, and receiver policies.

### Modify

- `Material Library` must extend the existing semantic surface registry, not
  duplicate it. A decal material references one or more registered surfaces and
  adds render policy.
- `receiverMask` is primarily a spawn/projection contract. The current forward
  renderer has no GBuffer object-category channel, so arbitrary per-pixel shader
  rejection is not available.
- Screen coverage complements a hard instance cap; it does not replace it.
- Texture arrays are a phase-5 optimization only after rlvk and GLES validation.
  The current backend has known sampler-binding sensitivity.
- PBR feature flags must compile into a small set of shader families. A single
  shader with every advertised feature would be expensive on Mali and difficult
  to validate.

### Defer or reject

- GPU Hi-Z culling: defer until the engine exposes a stable sampled-depth API on
  every target backend. Sixty-four to a few hundred CPU-culled instances do not
  justify it.
- DBuffer/screen-space decals: defer until a deferred scene path exists. Do not
  build a decal-only deferred renderer.
- Runtime chunk-mesh merging: reject for timed/animated decals. It complicates
  deletion, receiver deformation, lifetime, and material animation. Reconsider
  only for permanent editor-baked marks.
- “Thousands of active decals” is not a mobile acceptance target. Screen
  coverage, fragments, passes, and uniform/batch pressure matter more than the
  instance count.

## 3. Current-system findings

The current implementation is a useful prototype, not a durable architecture:

- A fixed pool and dense active-ID list already provide static allocation and
  O(1) removal.
- Legacy horizontal quads, oriented quads, flow decals, and conformal terrain
  stamps share one oversized entity and one manager.
- Textures and Raylib blend modes are stored directly on each instance.
- Material assignment is non-atomic:
  `DecalSystem_AddConformalEx()` followed by
  `DecalSystem_SetLastConformalMaterial()`. A later spawn between those calls
  can modify the wrong instance.
- There are no generation-safe handles, explicit destroy/update APIs,
  priorities, receiver masks, visibility lists, or distance/coverage budgets.
- Pool overflow and conformal overflow use unrelated implicit eviction rules.
- Conformal rendering performs per-instance uniform updates in two passes. This
  is the exact rlvk UBO-pressure pattern documented in `ENGINE_LANDMINES.md`.
- Render state is manually controlled inside the manager; depth/cull mistakes
  have already caused decals to draw through characters.
- Erosion previously exposed a fixed UV noise grid, demonstrating that visual
  lifecycle policy is too tightly embedded in a feature-specific shader.
- `VFX_SurfaceRegistry` already owns semantic asset paths, sampler rules, and
  lifecycle metadata. The rebuild must reuse it.

## 4. Non-negotiable constraints

1. No `malloc`, `calloc`, `realloc`, or `free`.
2. Backend-agnostic draw code: Raylib/rlgl only.
3. Every raster-state change is bracketed by
   `rlDrawRenderBatchActive()`.
4. Every `SetShaderValue()` occurs inside the matching active shader scope.
5. Avoid per-instance uniform changes. Encode instance variation in vertex data
   or instance buffers once the backend path is validated.
6. Do not add a second sampler to a production shader without Vulkan and Android
   validation.
7. Depth test remains enabled for world decals. Depth writes are disabled while
   drawing decals.
8. Skills pass IDs/descriptors, never load decal textures or shaders.
9. Public API signatures live in headers; regenerate `core/docs/API.md` after
   changes.
10. Every phase must preserve OpenGL, Vulkan, and mobile fallback behavior.

## 5. Target architecture

```text
VFX composition / gameplay
          |
          v
DecalSystem_Spawn(DecalSpawnDesc)
          |
          v
Generation-safe fixed pool
          |
          +--> lifetime / priority / eviction
          +--> receiver projection cache
          +--> bounds and screen-coverage estimate
          |
          v
CPU visibility + LOD selection
          |
          v
Material/projection buckets
          |
          +--> mesh-stamp renderer
          +--> oriented/projected renderer
          |
          v
base pass + optional emissive pass
```

### Required source layout

```text
core/decals/
  decal_system.h/.c          public lifecycle API and orchestration
  decal_types.h              handles, enums, descriptors, bounds
  decal_pool.h/.c            static pool and generation tracking
  decal_material.h/.c        material lookup and validation
  decal_cull.h/.c            visibility, LOD, coverage budget
  decal_mesh_renderer.h/.c   conformal/oriented mesh submissions
  decal_projector.h/.c       optional bounded forward projector
  shaders/
  docs/
```

Do not split files before their contracts are stable. The layout is the end
state, not a requirement for phase 1.

## 6. Data contracts

### Handle

```c
typedef uint32_t DecalHandle;
#define DECAL_HANDLE_INVALID ((DecalHandle)0)
```

Pack slot index and generation. A stale handle must never address a recycled
slot.

### Material ID

```c
typedef uint16_t DecalMaterialId;
#define DECAL_MATERIAL_INVALID ((DecalMaterialId)0)
```

The engine does not expose `DECAL_FIRE`, `DECAL_ICE`, or gameplay names.
Generated material entries may be named semantically in data, but renderer code
switches only on feature flags and projection families.

### Spawn descriptor

```c
typedef struct {
    Vector3 position;
    Vector3 normal;
    Vector2 size;
    float rotationRadians;
    DecalMaterialId material;
    uint32_t receiverMask;
    float lifetimeSeconds;       // <= 0 means persistent
    float fadeInSeconds;
    float fadeOutSeconds;
    unsigned char priority;
    unsigned char projectionMode;
    unsigned short flags;
    Color tint;
} DecalSpawnDesc;
```

No pointer in the descriptor may need to outlive `DecalSystem_Spawn()`.
Receiver sampling callbacks belong to a registered receiver/provider, not each
instance.

### Runtime instance

The private instance stores:

- generation and active state;
- transform and conservative bounds;
- material ID and receiver mask;
- remaining/total lifetime;
- priority, flags, projection mode, selected LOD;
- cached receiver vertices/normals for mesh stamps;
- render sort key;
- last-visible frame for eviction scoring.

Do not store `Shader`, texture paths, or gameplay element IDs per instance.

### Material descriptor

```c
typedef struct {
    VFX_SurfaceId surface;
    DecalShaderFamily shaderFamily;
    BlendMode baseBlend;
    Color baseTint;
    Color emissiveTint;
    float emissiveThreshold;
    float emissiveIntensity;
    float roughness;
    float normalStrength;
    float flowSpeed;
    unsigned int featureFlags;
    unsigned char minLod;
} DecalMaterialDesc;
```

Material data must be generated from a manifest. `VFX_SurfaceRegistry` remains
the asset/sampler owner; `DecalMaterialDesc` owns rendering policy.

## 7. Public API target

```c
void DecalSystem_Init(void);
void DecalSystem_Unload(void);
void DecalSystem_BeginFrame(const DecalView *view);
DecalHandle DecalSystem_Spawn(const DecalSpawnDesc *desc);
bool DecalSystem_Destroy(DecalHandle handle);
bool DecalSystem_IsAlive(DecalHandle handle);
bool DecalSystem_SetTransform(DecalHandle handle,
                              Vector3 position,
                              Vector3 normal,
                              float rotationRadians);
void DecalSystem_Update(float dt);
void DecalSystem_Draw(DecalRenderStage stage);
void DecalSystem_GetStats(DecalStats *outStats);
```

Forbidden public patterns:

- “set last spawned decal”;
- raw mutable `DecalEntity *`;
- texture/shader loading at call sites;
- public `Cull()`, `Merge()`, or `RenderQueue` mutation;
- one spawn function per material or gameplay effect.

## 8. Projection modes

### Mesh stamp — required, default

- Current conformal rings are the migration source.
- Samples a registered receiver provider at spawn time.
- LOD controls rings/sectors.
- Best for terrain, cracks, scorch, frost, stains, footprints.
- Depth test on, depth write off, small normal offset.

### Oriented mesh — required

- True surface-aligned quad or small clipped mesh.
- Used for walls and static props when a receiver normal is known.
- No camera-stretch compensation.

### Bounded forward projector — phase 6

- Unit box/frustum clipped to a bounded receiver set.
- Normal-angle rejection and depth range are mandatory.
- Intended for larger skill zones or marks crossing a small number of static
  surfaces.
- Not a full-screen pass.

### Screen-space/DBuffer — optional future

Only proceed if the main renderer adopts a compatible deferred path and mobile
can disable it without changing material semantics.

## 9. Receiver policy

Initial masks:

```c
DECAL_RECEIVER_TERRAIN
DECAL_RECEIVER_STATIC_MESH
DECAL_RECEIVER_CHARACTER
DECAL_RECEIVER_WEAPON
DECAL_RECEIVER_WATER
DECAL_RECEIVER_FX
```

Phase 1 supports terrain and explicit oriented static surfaces. Character,
weapon, and water receiving are rejected by default.

The manager validates a material’s allowed receiver mask at spawn. Projection
providers return surface position, normal, and receiver category. Do not claim
per-pixel receiver masking until the scene buffers actually contain that data.

## 10. Pool, lifetime, priority, and eviction

Start with measured budgets:

```text
Mobile:  96 total, 24 conformal/projected visible
PC:     256 total, 64 conformal/projected visible
```

These are compile-time/config caps, not promises. Record exact structure size
and total static memory before increasing them.

Eviction score, lowest removed first:

```text
priority
+ visible bonus
+ persistent bonus
- normalized age
- offscreen duration
- projected screen coverage penalty
```

Rules:

- Never evict priority 255 unless every active decal is 255.
- Prefer expired, offscreen, low-priority, old instances.
- A single oversized low-priority decal may be rejected at spawn.
- Persistent decals still consume budgets and can be evicted unless pinned.
- Pending removal is unnecessary while update/draw are single-threaded; use an
  explicit kill flag only if mutation can occur during iteration.

## 11. Visibility, LOD, and coverage

Phase-4 CPU pipeline:

1. lifetime/active check;
2. conservative sphere versus frustum;
3. distance cutoff from material;
4. projected-size estimate;
5. LOD selection;
6. screen-coverage budget;
7. material/projection bucket append.

Suggested LOD:

```text
LOD0 >= 96 px diameter: full mesh, base + emissive
LOD1 >= 40 px:          reduced mesh, base + emissive
LOD2 >= 12 px:          quad/reduced mesh, base only
LOD3 < 12 px:           discard
```

Coverage is an approximate sum of projected bounding areas, clamped per decal.
Track base-pass and emissive-pass coverage separately. Default mobile target:
20% base and 8% emissive coverage, then tune from GPU captures.

GPU Hi-Z is explicitly outside this plan.

## 12. Render queue and batching

Sort key:

```text
render stage
projection mode
shader family
blend mode
material ID
surface/texture ID
LOD
```

Distance is not a primary key for opaque/multiply mesh stamps. Alpha materials
that require order use coarse back-to-front distance buckets after material
grouping.

Phase 3 may retain rlgl immediate geometry, but it must build a visible queue
first. Phase 5 replaces per-instance uniform mutation with:

- vertex color for base tint/opacity;
- spare vertex channels or an instance buffer for material parameters;
- material-shared uniforms set once per bucket;
- one shader scope per bucket;
- one texture binding per surface bucket.

Texture arrays are accepted only after:

- rlvk binding validation;
- GLES 3 validation;
- identical dimensions/format/mips enforced by the asset pipeline;
- measured win over material buckets.

Atlas is the fallback, with padding and mip-safe borders.

## 13. Shader families

Limit production families:

1. `DECAL_SHADER_UNLIT`: base color/opacity, optional dissolve.
2. `DECAL_SHADER_EMISSIVE`: base plus threshold/mask emission.
3. `DECAL_SHADER_FLOW`: flow/UV animation plus optional emission.
4. `DECAL_SHADER_PBR`: optional PC/high tier after normal/roughness receiver
   integration exists.

Feature flags select behavior inside a family, but no family should declare
unused extra samplers casually. Prefer packed channels:

```text
Body RGBA: RGB neutral albedo, A opacity
Mask RGBA: R emission, G roughness, B flow/height, A reserved
Normal RG: optional encoded XY
```

For mobile, a one-sampler emissive material may derive emission from authored
luminance threshold. High tier may use the packed mask after backend validation.

Fade rules:

- authored alpha owns the silhouette;
- temporal fade uses smooth alpha/dissolve;
- no fixed UV-cell noise;
- derivative smoothing (`fwidth`) is mandatory at dissolve boundaries;
- edge glow is a material option, not a hard-coded Fire/Ice branch.

## 14. Render-stage contract

World decals render:

1. after opaque terrain/static geometry has populated depth;
2. with depth test enabled;
3. with depth writes disabled;
4. before transparent particles and screen-space VFX;
5. with batch flushes surrounding every depth/cull/blend state change.

Character occlusion must be correct regardless of draw order: if the character
has written nearer depth, a ground decal fragment fails depth testing.

Emissive pass uses the same depth test and geometry as the base pass. Skip it
entirely when no visible material has emission enabled.

## 15. Migration phases and AI work packages

Each package is sequential. An agent must not start until the previous package’s
acceptance tests pass.

### D0 — Contract freeze and regression harness

Owner: Decal Agent  
Writes: `core/decals/`, `core/tests/`, decal docs

Tasks:

- Document existing spawn APIs and call sites.
- Add structural tests for depth test/write, shader scope, generated materials,
  pool removal, and no allocation.
- Add visual bench cases: Fire, Ice, non-emissive impact, wall mark, sloped
  terrain, character occlusion, lifetime shrink.
- Capture PC Vulkan/OpenGL and Android baselines.

Acceptance:

- Existing visuals remain selectable.
- A test fails if depth test is disabled during world decal rendering.
- A test fails if `SetShaderValue()` occurs outside the matching shader scope.

### D1 — Atomic API and handles

Owner: Decal Agent  
Core review required for public header

Tasks:

- Add handle generation and `DecalSpawnDesc`.
- Make spawn plus material assignment atomic.
- Remove `DecalSystem_SetLastConformalMaterial`.
- Keep legacy wrappers temporarily inside Core only.
- Add explicit destroy/is-alive/update-transform.

Acceptance:

- Stale handles cannot modify recycled slots.
- Spawn failure returns `DECAL_HANDLE_INVALID`.
- No “last spawned” mutable global contract remains.
- Pool memory size is reported in a compile-time/static test.

### D2 — Data-driven material library

Owner: Decal Agent  
Asset-schema review: Skills Agent  
Backend review if sampler count changes

Tasks:

- Define `DecalMaterialDesc` and generated manifest.
- Reference `VFX_SurfaceId`; do not duplicate texture ownership.
- Migrate Fire, Ice, and generic impact as pilot materials.
- Move tint/emission/dissolve/flow policy out of composition code.
- Validate all referenced assets and channel contracts.

Acceptance:

- Renderer contains no `VC_MAT_FIRE`/`VC_MAT_ICE` branches.
- Composition maps a gameplay material to one decal material ID.
- Missing/invalid material fails visibly in debug and safely in release.
- One decal material can be retuned without recompiling composition code.

### D3 — Render queue and state correctness

Owner: Decal Agent  
Vulkan Backend review required

Tasks:

- Build bounded visible/bucket arrays.
- Move all draw traversal through queue buckets.
- Enforce render-stage and depth contracts.
- Skip empty emissive/flow passes.
- Centralize state begin/end helpers.

Acceptance:

- Characters and props occlude ground decals.
- No depth/cull/blend state leaks after `DecalSystem_Draw`.
- Draw-pass counts match visible feature requirements.
- Vulkan validation and rlvk guard scenarios show no stale uniform behavior.

### D4 — CPU culling, LOD, priority, and coverage

Owner: Decal Agent

Tasks:

- Add bounds, frustum/distance culling, projected-size LOD.
- Add priority-aware allocation and eviction.
- Add base/emissive screen-coverage budgets.
- Expose detailed stats without exposing mutable internals.

Acceptance:

- Offscreen decals generate no geometry.
- Low-priority offscreen decals are evicted before visible high-priority decals.
- Oversized decals cannot exceed the configured mobile coverage budget.
- LOD transitions do not expose square or circular proxy boundaries.

### D5 — Batch and mobile optimization

Owner: Decal Agent  
Vulkan Backend Agent required

Tasks:

- Remove per-instance uniform changes from the hot path.
- Pack parameters into vertex/instance data.
- Measure material bucket, atlas, and texture-array alternatives.
- Select arrays only if all backends pass and captures show a win.
- Tune mobile pool/coverage/LOD defaults.

Acceptance:

- No per-decal UBO snapshot in steady-state grouped rendering.
- Samsung A33/Mali stress scene has no scrambled geometry or stale colors.
- Target frame budget is documented with GPU/CPU timings.
- Texture sampling is correct on Vulkan, OpenGL, and GLES fallback.

### D6 — Forward projected decals

Owner: Decal Agent  
Core and Map interface review required

Tasks:

- Add bounded projector volumes.
- Add normal-angle and receiver-category rejection.
- Limit active projected decals independently.
- Reuse the material library and queue.

Acceptance:

- No projection through unrelated walls/characters.
- Projector cost is bounded by visible volumes and coverage.
- Mesh-stamp path remains the mobile default.

### D7 — Optional high-tier PBR/screen-space research

Owner: Decal Agent + Core Renderer owner

Start only with measured visual need and compatible scene buffers. This is a
research gate, not committed production scope.

## 16. Required tests

### Unit/structural

- pool allocate/free/recycle/generation wrap;
- stale handle rejection;
- deterministic priority eviction;
- lifetime/fade boundary behavior;
- material manifest/registry parity;
- invalid material/surface fallback;
- zero allocation scan;
- state-change flush contract;
- shader uniform scope contract;
- API signature/index generation.

### Runtime visual

- flat terrain, steep terrain, seam crossing;
- character standing above decal;
- camera at grazing angle;
- Fire/Ice/non-emissive materials through one API;
- overlapping alpha/multiply/emissive decals;
- spawn at pool and coverage limits;
- shrink/fade at native and reduced mobile resolution;
- device rotation/resolution changes.

### Performance

- 1, 16, 64, and cap decals;
- tiny versus screen-filling decals;
- base only versus base+emissive versus flow;
- OpenGL, PC Vulkan, Android Vulkan/Mali;
- CPU update/cull/sort time, draw calls, fragments/coverage, UBO pushes.

## 17. Documentation and ownership

Create a dedicated Decal Agent, but keep it subordinate to Core ownership.

Recommended skill scope:

- read/write: `core/decals/**`, `core/tests/decal_*`,
  `core/docs/DECAL_*`;
- shared-write with review: `core/vfx_surface_registry.*`,
  decal material manifest/generator;
- read-only: composition headers and map/environment receiver interfaces;
- forbidden: skill implementation `.c`, unrelated shaders, Vulkan backend
  internals unless assigned jointly.

Required docs:

- `core/docs/DECAL_SYSTEM_SPEC.md`: architecture and phased plan (this file);
- `core/decals/docs/API_GUIDE.md`: spawn/material/projection examples;
- `core/decals/docs/LANDMINES.md`: module-specific failures;
- `core/decals/docs/PROGRESS.md`: phase checklist, captures, measurements;
- `core/decals/README.md`: short ownership and entry-point map.

Do not create a hand-maintained decal signature index. Public signatures remain
in generated `core/docs/API.md`.

Core Agent retains final approval for:

- public API changes;
- shared render-stage ordering;
- shared shader helpers;
- pool-cap memory increases;
- new backend dependencies.

## 18. Definition of done

The rebuild is complete when:

- all gameplay effects spawn through one descriptor API;
- materials are data-driven and renderer code is element-agnostic;
- handles are generation-safe and material assignment is atomic;
- depth/receiver behavior is correct around characters and terrain;
- CPU culling, LOD, priority, and coverage budgets are active;
- the hot render path has no per-instance uniform churn;
- PC OpenGL/Vulkan and Android Vulkan are visually and performance validated;
- public API, guide, landmines, progress, and asset contracts are current;
- legacy spawn functions and compatibility paths are removed.
