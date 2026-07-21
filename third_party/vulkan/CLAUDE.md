# Renderer Agent (rlvk) — Vulkan 1.1 backend

## Role
Owns the rlvk Vulkan backend: a drop-in implementation of raylib's rlgl API (same `rl*`
functions, Vulkan 1.1-core rasterizer). Vision: standalone renderer, not wuxing-only.

## Docs (in `docs/`)
- **`docs/HANDOFF.md`** — the deep reference: vision, architecture, conversion table, key files, verification ladder, the full **§7 case-study debugging log**, §8 remaining, §9 architecture invariants. Read it before deep work.
- **`docs/LANDMINES.md`** — scannable trap index + the 7 debugging-methodology rules; **read before any bug hunt** (points into HANDOFF §7 for full chains).
- **`docs/PROGRESS.md`** — status / backlog / known gaps.

## Working protocol
Follows the root `CLAUDE.md` "Agent working protocol"; the rlvk specifics:

**Read first:** root `CLAUDE.md` → this file → `docs/LANDMINES.md` (**before any bug hunt** — your bug likely rhymes with a §7 case study) → `docs/HANDOFF.md` for the full chain/architecture → then grep the implicated `rlvk/*.inl` fragment. Reproduce empirically before reading broadly (methodology rule 1).

**Where to write:**
- **A bug you just root-caused** → add a case study to `docs/HANDOFF.md` §7 (symptom → what it looked like → root cause → fix → guard) **and** a one-line row in `docs/LANDMINES.md` pointing to it. Every draw-path fix also gets a `run_rlvk_visual_test.sh` scenario reproducing it first.
- **A driver quirk** → a `RLVK.Caps.*` flag detected at init + a repro scenario, never an inline hack (methodology rule 6); summarize it in this file's "Known driver quirks" and in `docs/LANDMINES.md`.
- **Status / remaining work / known gaps / perf notes** → `docs/PROGRESS.md`.
- **A lesson another (non-rlvk) module could hit** → promote to root `ENGINE_LANDMINES.md` (leave a pointer behind).
- Remove env-gated experiment switches once the answer is known — permanent switches rot (rule 3).

## Scope
- **Read/write:** `third_party/vulkan/` (umbrella `rlvk.h`, `rlvk/*.inl`, `shaders/`,
  `tests/`, `include/`), `scripts/*rlvk*`
- **Read (reference):** `docs/HANDOFF.md`, raylib headers in the test caches
- **Never:** `build/`, `_deps/`, `android.wuxing_skills/`. Game modules (`core/`, `skills/`,
  `compute/`, ...) only via targeted grep to construct a repro — ask the owning agent instead
  of reading their `.c` files.

## File layout (edit the fragment, never think of it as one file)
`rlvk.h` = public decls + a fixed-order `#include` chain of 14 fragments in `rlvk/`.
Fragments are textual includes of ONE translation unit: no include guards, order is
significant, statics span them. Never include a fragment directly, never reorder the chain.

| Fragment | Contents |
|---|---|
| `rlvk_config.inl` | defines/tunables, PFN table, sync2→sync1 shim |
| `rlvk_state.inl` | structs, caches, global `RLVK`, **Caps + quirks** |
| `rlvk_forward.inl` | static forward decls |
| `rlvk_shaderc.inl` | GLSL→SPIR-V, reflection, clip-z epilogue, location canonicalize |
| `rlvk_matrix.inl` | matrix/vertex ops, GL-style state |
| `rlvk_renderpass.inl` | render-pass+framebuffer caches, scope open/close, blend mode |
| `rlvk_core.inl` | rlglInit/Close, batch flush (draw site), arena |
| `rlvk_texture.inl` | staging, textures, FBOs, `rlvkDrawMesh` (draw site) |
| `rlvk_shader.inl` | shader modules, uniforms, samplers |
| `rlvk_compute.inl` | compute dispatch, SSBOs, quad blit (draw site) |
| `rlvk_frame.inl` | instance/device init, Caps detection, set0 layout |
| `rlvk_pipeline.inl` | pipeline cache, `rlvkBindPipeline`, vertex input |
| `rlvk_platform.inl` | surface, present, swapchain recreate, frame begin |
| `rlvk_format.inl` | math + pixel-format map |

## Verification ladder (MANDATORY — cheapest first, never start with the game)
1. `./scripts/check_rlvk_compile.sh` — compile check, seconds, no GPU. After EVERY edit.
2. `./scripts/run_rlvk_runtime_test.sh` — headless, 20 checks (device init, staging, SSBO,
   compute, shaderc). For init/compute/upload changes.
3. `./scripts/run_rlvk_visual_test.sh [scenario|--list]` — windowed scenario suite, one
   PASS/FAIL line each (clear, batch_alpha, additive3d, shader_uniform, depth, depth_rt,
   soft_depth, winding_rt, instanced, ssbo_vs, readback, stress). For anything touching
   draw/present/blend/depth.
   `VALIDATE=1` prepends Khronos validation. First run builds a raylib cache (~2 min),
   then ~20 s. **Every draw-path bug fix gets a scenario here reproducing it first.**
4. Full game build (`cmake --build build`) — HUMAN-run only, final confirmation.

## Debug tools (env vars, all off by default)
`RLVK_DEBUG_FLUSH` (batch draws), `RLVK_DEBUG_VAO` (mesh buffers), `RLVK_DEBUG_FBO`
(frame/present lifecycle), `RLVK_DEBUG_PIPE` (pipeline key per bind + FBO scope opens),
`RLVK_DEBUG_SSBO` (graphics-SSBO rebase mask + descriptor pushes), `RLVK_MEM_REPORT`,
`RLVK_GPU_TRACE` (GPU ms), `RLVK_DUMP_SPV=dir` (compiled SPIR-V pre-rebase per stage +
post-rebase `rlvk_rebased_vs.spv`, for spirv-dis).

## Known driver quirks (do not re-litigate; each has a Cap or a fixed workaround)
> Quick list below; full case-study index + methodology in `docs/LANDMINES.md`.
- `Caps.noSampledDepth` (MoltenVK/Intel): SAMPLED usage on a depth image silently kills
  depth test/write on that attachment. FBO depth drops SAMPLED under the quirk; sampling of
  `renderTex.depth` is served by an R32F color shadow-copy twin filled at scope close
  (§7.10) — Metal also can't sample a depth-format texture via `sampler2D`, hence R32F color.
- MoltenVK zeroes a UBO if the descriptor layout merely declares storage-image bindings
  (compute layout has none for this reason).
- shaderc `auto_bind_uniforms` rebases even explicit UBO bindings → compute uses loose
  uniforms; explicit std430 SSBO bindings are safe.
- MoltenVK resolves Metal buffer indices through the bound pipeline → bind pipeline BEFORE
  vertex buffers (all 3 draw sites do; keep it that way).
- MoltenVK portability rejects vertex stride < format size (stride-0 broadcasts are
  special-cased with a large dummy buffer on __APPLE__).
- A failed pipeline build must SKIP the draw (stale pipeline renders garbage squares) —
  all 3 draw sites check `rlvkBindPipeline`'s return.
- Unchecked `vkAcquireNextImageKHR` failure = present of un-acquired image 0 + wait on
  never-signaled semaphore = GPU timeout/device lost. The bail-out in rlvkBeginFrame stays.

## Token-efficiency rules (MANDATORY)
1. Grep the symbol first; read ONLY the relevant fragment region (offset/limit).
2. Debug empirically before reading broadly: reproduce with a visual-test scenario or a
   small probe, bisect with env-gated experiments, THEN read the implicated code.
3. Don't paste SPIR-V, validation walls, or full functions into responses — report VUID ids
   and `fragment:line` cites.
4. Batch independent greps/reads in one message.
5. Full-suite output is ~12 lines — always run the suite over describing what might break.
6. Report per the root agent rules: English, terse, lead with the result.
