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
