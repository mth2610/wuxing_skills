# Motion, Emitter, Particle and Trail migration

Date: 2026-10-08. Scope: staged architecture migration, with Vulkan GPU execution as the priority.

> **Planning exception:** explicitly requested by the user on 2026-10-08. This file is a migration proposal, not documentation of an implemented API. The user request overrides the general prohibition on plan documents. Proposed names, layouts and thresholds below require validation during implementation. No implementation is authorized merely by this plan.

## 1. Intended result

Four independently configurable systems:

| System | Responsibility | Does not own |
|---|---|---|
| Emitter | When, where and what to spawn; initial state; source sampling; child-emission policies | Ongoing movement, component rendering |
| Motion | Shared fields, body properties, force/flow sampling, translational integration and movement controllers | Particle appearance, ribbon connectivity, component allocation |
| Particle | Particle storage, lifetime, internal appearance evolution and rendering, including mesh instances | Emission scheduling or a private movement implementation |
| Trail | Connected node storage, topology, attachment constraints, length/bend response, UVs and rendering | Effect-specific projectile/orbit/target movement algorithms |

**Design decision:** use exactly two public attachment modes:

1. **Free:** neither endpoint is attached; all nodes can respond to Motion.
2. **Head-anchored:** the head follows an external attachment; other nodes respond to Motion.

Attachment is independent of field selection, material response, and node creation. Do not introduce orbit, projectile, vortex or follower as new physical trail types. Keep old types only in compatibility adapters until their consumers are migrated.

Trail and ribbon share one connected-chain simulation foundation. Ribbon is a rendering representation; historical trail generation is an emission policy.

### Primary acceptance example

Emit a ribbon of defined length, attach its head to a thrown stone, and optionally bind a field transform to the stone. The head follows the stone. The tail retains velocity and responds to gravity, airflow and fields while remaining connected. Releasing the attachment turns the same ribbon into a free ribbon without a position or velocity discontinuity.

Changing the field should produce curl, spreading, gathering or stream-following without changing TrailSystem code. Basic material response still needs reusable mass/drag/stretch/bend settings; the architecture does not eliminate physical parameters.

## 2. Verified starting points

These observations come from source inspected during planning, not from a complete call-site audit.

| Observation | Source |
|---|---|
| Existing generic-looking EmitterConfig contains ParticleConfig | `core/emitter_system.h` |
| A second emitter API exposes EmitterSystem_Init/Update and Emitter_AttachToPoint | `core/skill_helper.h` |
| ParticleManager owns point/mesh-vertex/mesh-edge emission-source descriptors | `core/particles/particle_manager.h` |
| Motion body helpers depend on ParticleDynamicsProfile | `core/motion/motion_body.h` |
| Motion receiver masks currently identify particles and foliage | `core/motion/motion_fields.h` |
| Versioned GPU Motion sidecars already exist alongside the particle ABI | `core/motion/motion_gpu.h` |
| Modern spatial Motion has a particle compute integration path | `core/particles/shaders/gpu/particle_gpu.comp` |
| Captured routes, child-emission features and other configurations can require CPU simulation | `core/particles/particle_motion_capabilities.h` |
| Trail projectile and follower updates integrate ForceField/Wind directly | `core/trails/trail_system.c` |
| Mesh adjacency is already a standalone utility | `core/mesh_adjacency.h` |
| Mesh-surface particle emission also exists in composition | `core/composition/common/vc_mesh_particle_emitter.inl` |

The CPU-only summary in `particle_manager.h` is broader than the capability routing now implemented. Reconcile it when changing that API; do not use the summary alone to select a migration path.

## 3. Contracts to settle before production changes

The following are proposed contracts. Record accepted public contracts in their owning headers before applying breaking changes.

### 3.1 State and movement ownership

- Component pools own particle and ribbon state. Motion operates on explicit state views; it does not become a second allocation registry for every component.
- Motion owns body properties and shared integration functions without depending on ParticleSystem headers. ParticleDynamicsProfile becomes a compatibility adapter.
- Exactly one path integrates each body/node per substep. Do not run legacy trail movement after Motion integration.
- Preserve units: position in meters, velocity in m/s, acceleration in m/s², force in Newtons, inverse mass in 1/kg, time in seconds.
- Preserve legacy acceleration semantics and existing GPU enum values/layouts. Introduce versioned or appended fields and explicit adapters.
- Contact geometry belongs to the relevant collision provider. Define a shared contact-response contract where needed; component event production must not duplicate integration.
- Internal visual rotation/deformation stays with the component initially. General angular dynamics are a separate extension, not a hidden requirement for this migration.

### 3.2 Attachments and moving fields

- Use generation-checked attachment handles plus a local offset, not long-lived pointers to caller matrices.
- Attachment providers publish previous/current transforms, timing and velocity. CPU and GPU providers use the same logical contract.
- Field transform binding is independent of ribbon attachment. Multiple fields can follow one source, and ribbons can respond to ambient fields.
- A moving field changes spatial evaluation; it does not implicitly teleport receiver positions or overwrite receiver velocity with the source velocity.
- Explicitly distinguish a field's spatial frame from medium velocity/entrainment. Include angular frame velocity only when the selected field law requires it.
- Release preserves node positions and velocities. Define attachment-loss behavior explicitly: release, finish/fade, or destroy.
- Teleports and discontinuous transforms carry an explicit discontinuity signal. Choose reset/break/reposition behavior instead of interpolating a long artificial trail.
- Support same-step attachment changes at a defined simulation boundary. Defer arbitrary mid-dispatch mutation.

### 3.3 Ribbon structure and node creation

- Trail owns ordered nodes, rest lengths, bend response, attachment enforcement and geometry frames.
- Motion supplies external acceleration/forces/flow and predicted motion; Trail projects structural constraints and reconciles velocity from corrected positions.
- Pin the head throughout constraint solving, not only before the solver.
- Use a fixed simulation step with bounded substeps and a declared overload policy.
- Initial node-creation policies: complete chain and continuous history. These are emitter policies, not additional attachment modes.
- Emit a complete chain with explicit rest length, spacing and initial shape. Do not accumulate extra length every frame for the stone-and-silk case.
- Continuous history uses distance-based sampling with fractional carry and actual birth positions/times.
- A history point may be stationary or field-responsive through an explicit receiver/body setting; avoid accidental global changes to existing trail visuals.
- Preserve local ordering under the selected solver and strong fields. Scalar distance constraints alone do not prevent local node inversion. Do not prohibit legitimate large-scale curls by imposing global monotonicity.
- Self-collision and full cloth width/twist simulation are outside the initial solver scope. Describe the initial result as a connected ribbon chain, not a full fabric simulator.

### 3.4 Emission and events

- Emitter owns scheduling, random seeds, source sampling, initial-state distribution and child-spawn rules.
- A typed spawn sink sends commands to Particle or Trail; mesh instances remain a particle render mode unless a separate component owner is justified.
- Mesh adjacency remains a reusable geometry utility. Move its emission use, not its general ownership, into Emitter.
- Components and Motion report events; Emitter maps those events to new spawn commands. Motion arrival behavior must not implicitly create visual effects.
- Define event identity, parent generation, sequence, time, position, velocity, normal and deterministic seed where relevant.
- Bound event queues, child depth, spawn counts and per-step work. Expose overflow counters and a deterministic overflow policy.
- Visual GPU child emission consumes GPU events without synchronous readback. CPU gameplay events use an explicitly separate delivery contract.
- Emitter stop prevents future births; existing components drain naturally. Force-destroy is a separate operation.
- Define whether pending child events survive emitter stop. Keep referenced templates/resources alive until components and pending events retire.

## 4. Execution sequence

Each phase produces a reviewable change and passes its exit gate before dependent work begins. Do not remove old paths while they still have consumers.

### Phase 0 — Complete the ownership and behavior audit

1. Inventory spawn, update, render and teardown paths for both emitter implementations, ParticleManager, TrailSystem and composition emitters.
2. Map projectile steering, follow-target, travel paths, orbit, force sampling, wind, contact and arrival ownership.
3. Classify child emission: live-rate, death, collision and arrival; identify callbacks requiring CPU state.
4. Identify supported render modes separately from enum declarations. A declared mode is not proof of implementation.
5. Have module owners inspect their own consumers, including generated sandbox wiring; respect module read/write boundaries.
6. Record API adapters, source-data lifetimes, pool limits and field-mask compatibility requirements in the review.
7. Capture existing focused-test results, matched-camera fixtures and representative performance baselines before edits.

**Exit gate:** every audited movement path has one owner and migration destination; unknown consumers and unsupported modes are explicit. Present contract decisions for review before foundational implementation.

### Phase 1 — Extract component-neutral Motion contracts

1. Introduce neutral body/receiver/state contracts within `core/motion/`; reuse existing physical-field descriptors.
2. Adapt ParticleDynamicsProfile into those contracts while preserving current defaults and units.
3. Remove Motion's dependency on particle types incrementally, including GPU packing and arrival-profile dependencies.
4. Define receiver categories for ribbon nodes/heads and mesh bodies. Audit existing ALL-mask behavior before extending bits; preserve old callers through adapters.
5. Consolidate CPU field sampling and integration; define fallback versus rejection for unsupported features.
6. Establish shared GLSL Motion functions under Motion ownership with explicit buffer bindings/state access. Keep a compatibility include at the old particle shader path as needed.

**Exit gate:** particle behavior is unchanged under focused unit/route tests and production GPU parity probes. Neutral Motion headers no longer require particle types when this phase is complete.

### Phase 2 — Establish CPU ribbon reference behavior

1. Add Free and HeadAnchored attachment contracts behind an opt-in modern trail descriptor.
2. Introduce complete-chain initialization and generation-checked attachment binding.
3. Integrate external node motion through shared Motion helpers, then apply segment/bend constraints.
4. Enforce head pinning each iteration; derive final velocity consistently after constraint correction.
5. Implement release, attachment destruction, field expiration, teleport and pool-reset behavior.
6. Preserve the existing renderer initially to isolate simulation changes.
7. Keep legacy projectile/follower APIs routed through their old behavior until adapters are proven.

**Exit gate:** deterministic CPU tests cover free drift, pinned-head movement, release continuity, bounded stretch and strong-field stability across display frame rates.

### Phase 3 — Prove a minimal Vulkan GPU ribbon

1. Select bounded ribbon/node pools and publish their memory budget before changing capacities.
2. Define versioned GPU node/ribbon records, generation ownership and one writer for each persistent field.
3. Reuse the shared field snapshot and Motion GLSL implementation for node integration.
4. Prototype one workgroup per bounded ribbon, with uniform barrier participation and ping-pong constraint updates. Measure before adopting it as the final dispatch layout.
5. Split integration and constraint passes if dependencies or group-size limits require it. Never assume workgroup barriers synchronize different workgroups.
6. Render directly from GPU node state through the renderer abstraction. Start with one connected strip and correct stable framing, winding and UVs.
7. Validate spawn/upload → compute → compute → vertex/indirect dependencies with the Renderer owner.
8. Keep a CPU reference/fallback. Do not upload stale CPU node state over GPU simulation state each frame.

**Exit gate:** one free ribbon and one moving-head ribbon run through production Vulkan compute and rendering, without per-frame readback. Targeted diagnostic readback is permitted for parity tests.

### Phase 4 — Introduce generic Emitter scheduling

1. Create one canonical emitter API and an explicit mapping for both existing emitter systems.
2. Separate schedule, source, spawn template and sink. Use tagged C99 descriptors and fixed pools; avoid a global variant containing every component's live state.
3. Implement burst, timed count, continuous rate and distance-based emission with deterministic seeds and fractional carry.
4. Derive rate from total count/duration for timed casts; preserve no-startup-burst behavior where currently authored.
5. Route particle and complete-chain ribbon spawn commands through their owning allocators.
6. Add continuous history-node production using a ribbon handle; distinguish it from repeatedly spawning new ribbons.
7. Implement stop/drain/destroy/reset semantics and generation checks.
8. Initially keep spawn-command production on CPU where adequate; GPU simulation remains resident. Move scheduling to GPU where GPU parents or measured scale require it.

**Exit gate:** the same schedule/source can spawn particles or ribbons through different sinks; components outlive a stopped emitter safely; existing emission counts and spacing remain stable.

### Phase 5 — Migrate mesh-source emission

1. Move point/vertex/edge source sampling out of ParticleManager into Emitter source adapters.
2. Extract the reusable mesh-surface sampler from composition while retaining material/effect recipes in composition.
3. Preserve legacy sampling distributions first. Add area-weighted triangle sampling as an explicit new policy if required.
4. Define static/animated mesh updates, transform and normal handling, source invalidation and resource ownership.
5. Keep MeshAdjacency reusable outside Emitter.
6. Add GPU mesh sampling only when source geometry is already GPU-accessible or upload costs justify it; do not promise GPU animated-mesh sampling before auditing the character interface.

**Exit gate:** particle and ribbon sinks can consume the same mesh-source sample contract; source destruction/reuse cannot access stale data; legacy mesh emission stays visually consistent.

### Phase 6 — Migrate sub-emission to events

1. Introduce bounded CPU event records and equivalent versioned GPU records.
2. Move child-spawn policy out of particle update loops into Emitter event consumers.
3. Add GPU event append/consume passes and indirect spawn work where supported by the renderer abstraction.
4. Defer descendants to a defined later phase or substep; prevent unbounded recursive spawning in one dispatch.
5. Preserve event position, velocity inheritance, exactly-once behavior and resource lifetimes.
6. Define delayed CPU delivery where necessary. Keep callbacks/gameplay-sensitive routes on CPU until their semantics can be preserved.
7. Expand the capability matrix only after production-path tests pass.

**Exit gate:** particle death/collision/arrival can spawn particles or ribbons through shared policies, with bounded work and no synchronous visual-event readback.

### Phase 7 — Migrate consumers and retire duplication

1. Migrate one representative consumer at a time: free ribbon, thrown-stone silk, continuous weapon history, mesh-source particles, child emission.
2. Convert legacy trail-specific movement into reusable Motion controllers/fields where equivalent. Preserve externally driven attachments where appropriate.
3. Migrate composition recipes through owning agents; regenerate sandbox fixtures through the existing generator workflow.
4. Require parity evidence before switching defaults. Keep an explicit legacy route while unmigrated callers remain.
5. Remove private integration and duplicate emitter scheduling only after the call-site audit shows no remaining users.
6. Regenerate `core/docs/API.md`; update actual header contracts and usage guidance. Keep future-work material out of API/LANDMINES.

**Exit gate:** migrated paths have one movement authority and one emission authority, with no fallback that silently drops behavior. Legacy removal is a separate reviewed change.

## 5. Validation matrix

| Boundary | Required checks |
|---|---|
| Units | Same acceleration across mass changes; Newton-force response scales with inverse mass; drag/airflow retain established semantics |
| Free ribbon | Uniform translation without artificial stretch; gravity/drag; overlapping fields; field exit and expiration |
| Head attachment | Translating/rotating source and offset; pin accuracy; release velocity; destroyed/reused handle; teleport |
| Structure | Rest-length error; bend response; local inversion stress; degenerate segments; extreme curvature; explicit self-collision limitation |
| Time | Fixed-step simulation under 20/60/240 FPS presentation; frame spikes; bounded overload; subframe spawn timing |
| Emission | Burst/timed counts; no extra initial burst; distance carry; stop/drain; deterministic replay; pool exhaustion |
| Events | Exactly-once death/collision/arrival; parent reuse; queue overflow; child-depth limit; cancellation/resource lifetime |
| GPU parity | Actual dispatch versus CPU reference at field edges, overlap, contact and moving frames; declared numerical tolerances |
| GPU ownership | No stale CPU overwrites; reset/reuse; multi-dispatch hazards; compute-to-render visibility; attachment upload ordering |
| Rendering | Matched gameplay-camera captures; connected geometry; UV continuity; stable orientation; body/emission behavior on bright backgrounds |
| Performance | Measured simulation/render cost versus node/ribbon count; dispatches; uploaded bytes; pool memory; idle cost; event saturation |

Define numerical thresholds before accepting each phase: maximum segment-length error relative to rest length, attachment error in meters, CPU/GPU position and velocity tolerances, and target workload/frame budget. Derive values from the chosen timestep, solver and target hardware; do not invent universal tolerances in this plan.

Existing focused starting points include `motion_fields_test`, `motion_gpu_test`, `particle_motion_capabilities_test`, `motion_coupling_test`, `motion_ribbon_trail_test`, `trail_cloth_test`, `guided_emission_test`, `swept_trail_test` and `trail_only_emission_test` under `core/tests/`. Inspect what each test actually observes before treating it as coverage.

Validation ladder:

1. Run focused Core tests for changed contracts, then the full Core suite. Separate baseline failures from new failures.
2. For renderer changes, follow its compile/headless/visual ladder before full-game diagnosis.
3. Check generated contracts using `python3 scripts/sync_vfx_test.py --check` when composition wiring changes.
4. Configure/build using the root-approved CMake commands; do not inspect forbidden build directories.
5. Run matched bright-background matrices using fixture names: `scripts/render_vfx_matrix.sh "<FIXTURE NAME>" 40 90 140`.
6. Capture the stone/ribbon example at the gameplay camera, including throw, deceleration and release. Numeric tests and close-ups alone do not establish visual acceptance.
7. Measure GPU work with valid device timing. Host timings or unavailable/zero GPU timestamps do not establish GPU performance.

Known traps to carry into tests: duplicate Motion/Wind influence (`ENGINE_LANDMINES.md`, physical response ownership); node inversion despite distance constraints (`core/docs/LANDMINES.md`, chain ordering); GPU parity mismatches at support edges/contact and stale sidecar uploads (`core/particles/docs/GPU_BACKEND_LANDMINES.md`).

## 6. Ownership, review and rollback

- **Core owner:** Motion contracts, CPU reference, particle/trail/emitter adapters, shared shader logic, geometry utilities and Core tests.
- **Renderer owner:** Vulkan abstraction changes, synchronization, indirect execution and renderer-level GPU tests. Audit its module instructions before touching renderer files.
- **Sandbox owner:** integration fixtures and generated wiring; no manual edits to generated fixture code.
- **Consumer owners:** their own skill/environment/map/character integration and call-site migration. Request specific interface answers instead of reading other modules' implementation files.
- Review boundaries: neutral Motion API; ribbon attachment/solver; Vulkan prototype; generic Emitter API; event ABI; each default-route switch; final legacy deletion.
- Every migration change retains a known legacy route or a small revert boundary. Do not reinterpret stored configs in place.
- Roll back the affected route when behavior, GPU validation or lifecycle checks fail. Do not silently strip unsupported fields/events to make GPU routing succeed.

## 7. First implementation increment

After contract review, implement only Phases 1–3 as the first coherent increment:

1. Neutral Motion body/receiver contracts with particle compatibility.
2. Free/head-anchored CPU reference ribbon.
3. Minimal Vulkan implementation sharing field evaluation and rendering resident nodes.
4. One compact fixture demonstrating particles plus both ribbon attachment modes in the same field, including the thrown-stone release sequence.

This increment proves the central architecture before generic emission and event migration widen the scope. It is an intermediate milestone; the full requested system separation is complete only after Phases 4–7.

## Patch Log

| Date | Editor | Section | Basis | Tier |
|---|---|---|---|---|
| 2026-10-08 | Codex | Verified starting points | Source files identified individually in §2, inspected directly | Ground-truth |
| 2026-10-08 | Codex | Target boundaries and attachment modes | User discussion and explicit plan-file request | Design decision |
| 2026-10-08 | Codex | Contracts, phases, gates and validation | Proposed migration design; not implemented API | Proposal |
