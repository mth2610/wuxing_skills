# compute — Landmines

> Distilled, reusable lessons for the **compute** (GPU particle) module. Format: Symptom → Cause → Rule.

### More private state can defeat a GPU arithmetic optimization
- **Symptom:** typed guidance becomes slower with overlapping casts; caching path
  projection or unrolling controller records makes the shader slower still.
- **Cause:** full private response matrices and separate saturation arrays keep
  substantial per-invocation state live. Extra caches increase that pressure.
- **Rule:** exploit the actual isotropic/axial/transverse resistance structure,
  reuse saturated records, and measure each candidate on the device against a
  bracketed control. Preserve all eight responses and overflow behavior. GPU
  parity and dispatch timings establish different facts; neither proves live FPS.

### Motion parity passes in the core but fails at boundaries
- **Symptom:** core-centered GPU clouds match CPU references while boundary curl,
  soft field forces or floor contacts differ.
- **Cause:** a separately written shader used quintic support instead of the CPU's
  cubic support, and inherited legacy GPU floor retention `.8` instead of `.75`.
- **Rule:** compare production GPU dispatch against CPU samples at support edges,
  during contact, and with ordinary Wind plus published Motion airflow. Retain
  legacy contact semantics only for legacy particles. CPU-only ABI tests cannot
  establish shader parity (`motion_gpu_test.c` + renderer parity harness).

### Inactive geometry can still corrupt packing
- **Symptom:** a valid static sphere descriptor overruns a GPU staging record.
- **Cause:** inactive path counts were copied even though CPU validation checks
  paths only for active tube/trajectory modes.
- **Rule:** pack only active geometry and bound every path copy. Preserve integer
  handles and GPU-owned lane state; never upload a stale CPU sidecar each frame.
> Cross-cutting device traps (Mali SSBO vertex-stage, depth-test-vs-mask, numeric-over-visual) are in root `ENGINE_LANDMINES.md`. Session logs / open backlog are in `PROGRESS.md`.

### `RewriteVersionForGLES` silently downgrades SSBO shaders
- **Symptom:** `gpu_particles_ssbo.vs` fails to compile on device (`Expected layout qualifier identifier, got 'std430'`) even though the packaged asset is byte-identical `#version 310 es`.
- **Cause:** `core/shader_preprocessor.c`'s `RewriteVersionForGLES()` (runs on every `ResourceManager_LoadShader`) unconditionally rewrote `#version 310 es` → `300 es` *after* the file was read — invisible to an APK byte-compare. `std430`/`binding`/`readonly` are ES-3.1-only.
- **Rule:** only downgrade a shader to `300 es` when it does **not** contain `std430`; real SSBO shaders must keep `310 es`.

### Vertex-stage SSBO reads are unreliable on Mali GLES → GLES compute path off
- **Symptom:** compute path compiles and dispatches, `Pool` count increments, but particles are invisible on Mali/Exynos.
- **Cause:** reading an SSBO in the vertex shader (or using one buffer as both SSBO and VBO) silently fails on many Mali drivers.
- **Rule:** the OpenGL ES backend disables compute on Android and uses the CPU/ring-buffer path. The Vulkan backend has its own validated SSBO path and is not covered by that GLES workaround. Do not re-enable the GLES path or add TBO hacks.

### Immediate-mode quad math must exactly match `particle_system.c`
- **Symptom:** CPU/VBO particles invisible on *all* platforms despite valid data.
- **Cause:** wrong `rx/ry/rz` signs and quad vertex order produced zero-area or culled-winding quads.
- **Rule:** copy `core/particle_system.c`'s `DrawParticles()` billboard construction exactly (vertex order + winding), don't re-derive it.

### Velocity-stretched billboards must project velocity onto the camera view plane
- **Symptom:** Velocity-stretched particles disappear or flicker invisibly from side/horizontal angles (e.g. in guided particle VFX).
- **Cause:** Quad basis was derived by crossing 3D velocity with `camera.up` or camera view direction in 3D world space. This tilted the billboard normal away from the camera (e.g. facing straight up to the sky), turning the billboard edge-on to horizontal camera views where its projected screen area collapses to zero.
- **Rule:** Always project 3D particle velocity onto the camera view plane (`dot(vel, right)` and `dot(vel, up)`). Construct the stretched quad using in-plane basis vectors `tangentDir` and `rightDir` so the quad normal remains identically parallel to `viewDir` from every angle.

### A GPU pool's CPU count does not prove it has finished
- **Symptom:** an empty environment still submits GPU particle work, while an aggressive lifetime gate risks dropping delayed particle events.
- **Cause:** `GpuParticleSystem_ActiveCount()` reports the spawn high-water mark on compute; CPU lifetime tracking also owns collision and arrival events independently of GPU simulation.
- **Rule:** skip work only for a pool known never to have spawned since Init, preserve its compute clock, and keep all post-spawn processing until a stronger completion contract exists (`particle_gpu_idle_test.c` guards initial-empty admission).

### CPU event completion must not decide GPU surface capture admission
- **Symptom:** compact surface capture loses GPU particles near arrival, despite
  valid GPU lifetime, or incorrectly labels a slot after ring overwrite.
- **Cause:** the CPU event shadow can reach a target at a different instant from
  compute. Its active flag is not the authority for GPU visibility.
- **Rule:** gather conservative matching owner/mode slots from spawn metadata;
  let the actual GPU life gate reject them. Mirror uploaded owner/mode routing,
  not pending CPU event-spawn replacements. Never retain indices across a spawn
  revision or changed material routes. Guard: `particle_surface_index_test.c`
  executes the production builder, including a CPU-dead slot and ring reuse.

### Force-field slots cannot be compacted while GPU particles refer to them
- **Symptom:** sequential effects exhaust the 16-entry field registry, or a later
  effect's field unexpectedly accelerates older particles.
- **Cause:** permanent pointer registration never reclaimed finished users;
  compacting slots would invalidate force indices held in GPU buffers.
- **Rule:** lease stable slots for the maximum remaining spawn lifetime, include
  future arrival fields from spawn, advance with exactly the particle-life dt,
  and reclaim only expired leases. Path packing performs lookup without renewal.
  Zero-pack sparse holes before compute. Guard: `particle_field_lease_test.c`.

## Patch Log

| Date | Editor (human/AI) | Section edited | Based on which source | Tier |
|---|---|---|---|---|
| 2026-10-04 | Codex | Conservative surface indices and stable field leases | particle_surface_index.h; particle_field_lease.h; particle_gpu_backend.c | Ground-truth |
| 2026-10-01 | AI | GPU pool completion gate | particle_gpu_backend.c, particle_gpu_work_gate.h, particle_gpu_idle_test.c | Ground-truth |
