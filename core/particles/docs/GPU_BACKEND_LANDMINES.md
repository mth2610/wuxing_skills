# compute — Landmines

> Distilled, reusable lessons for the **compute** (GPU particle) module. Format: Symptom → Cause → Rule.
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

## Patch Log

| Date | Editor (human/AI) | Section edited | Based on which source | Tier |
|---|---|---|---|---|
| 2026-10-01 | AI | GPU pool completion gate | particle_gpu_backend.c, particle_gpu_work_gate.h, particle_gpu_idle_test.c | Ground-truth |
