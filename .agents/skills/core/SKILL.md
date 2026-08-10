---
name: core
description: Own, optimize, debug, test, and evolve the Wuxing Skills Core API and foundational VFX systems under core/. Use for particles, trails, force fields, shaders, decals, VFX lights, ribbons, flow maps, procedural meshes, sprite animation, composition primitives, Core API changes, and Core regressions.
---

# Core Engine Agent

## Role
Manages the entire **Core Engine** module of the Wuxing Skills project. Owns the foundational systems: particle, trail, force field, shader, decal, vfx light, ribbon, flow map, procedural mesh, sprite anim, etc.

## Working protocol
Follow the root `CLAUDE.md` "Agent working protocol". For Core work, read in this order and stop once sufficient:

1. Root `CLAUDE.md` → this skill → `core/CLAUDE.md`.
2. Grep the implicated symbol, then read the relevant `.h` contract and only the needed source region.
3. Read `ENGINE_LANDMINES.md` before rendering, shader, Vulkan, or Android work; read `core/docs/LANDMINES.md` before any bug hunt. For GPU particles, also read `core/particles/docs/GPU_BACKEND_LANDMINES.md`.
4. Read `AGENT_CODE_STANDARD.md` before editing code.

Reproduce empirically before reading broadly. Do not start a Core bug hunt from the full game or from screenshots when a standalone numeric, contract, or wiring probe can answer the question.

**Where to write:**
- A root-caused regression → add the failing-then-passing guard under `core/tests/` and record **Symptom → Cause → Rule** in `core/docs/LANDMINES.md` when the lesson is reusable.
- A cross-module lesson → promote it to `ENGINE_LANDMINES.md` and leave a pointer in the Core landmines.
- Status, remaining work, performance notes, or known gaps → `core/docs/PROGRESS.md`.
- API contracts → the owning `.h`; usage guidance → `core/docs/API_GUIDE.md`; generated API index → follow the regeneration workflow below.
- Remove temporary experiment switches and noisy probes once the answer is known. Keep only diagnostics that remain useful, log on state change, and default them off.

## Scope
- **Read/write:** All files under `core/` (`.c`, `.h`, `.glsl` shaders in `core/shaders/`)
- **Read (reference):** `docs/API.md`, `docs/VFX_ARCHITECTURE.md`, `docs/VFX_ENGINE.md`, `docs/SHADER_API.md`, `docs/COMPOSITION_API.md`, `docs/EXTERNAL_API.md`, `CMakeLists.txt`, `main.c`
- **Read (interface only):** `environment/environment_system.h`, `skills/` headers (`.h` only, never `.c`), `maps/` headers (`.h` only)

## Directories FULLY FORBIDDEN (never read, list, or touch)
- `build/`
- `_deps/`
- `android.wuxing_skills/`

## Directories NOT to read without explicit permission
- `skills/` (header `.h` only, never a skill's `.c`)
- `maps/` (header `.h` only)
- `environment/` (header `.h` only)
- `sandbox/`
- `assets/`

## Responsibilities
1. **API maintenance:** Update and maintain all public API in `core/`. When changing a function signature, notify the Skills/Map/Environment agents so they can update call sites.
2. **Shared shaders:** Own `core/shaders/common/` (`vs_header.glsl`, `fs_header.glsl`, `lighting.glsl`). Don't edit shared shaders without clear cause — changes here affect the entire engine.
3. **Memory safety:** Ensure no `malloc`/`free` in core. Static pools only.
4. **Performance:** Track MAX pool sizes. Expanding them requires weighing memory footprint.
5. **Docs:** Update `docs/API.md` whenever public API is added/changed. `docs/API.md` is **shared-write** with the Skills Agent (it documents usage notes/conventions Skills discovers too) — see "Updating docs/API.md" below.

## Code rules (from docs/API.md)
- Strict C99, Raylib 6.0. Backend: Vulkan 1.1 via `rlvk` (priority); OpenGL 3.3 Core / GLES 3.x fallback. Keep draw code backend-agnostic (`rlgl`/raylib, never raw GL/Vulkan).
- Guard the PI macro: `#ifndef PI #define PI 3.1415926535f #endif`
- No `malloc`/`calloc`/`realloc`/`free`
- Use `ResourceManager_LoadShader()` — never call `UnloadShader`/`UnloadTexture` in skill code
- **Composition layer rule:** element colors/gradients/force fields come from `VFX_Material(VC_MAT_*)` (`core/presets/vfx_presets.h`), motion math (orbit/ring/jitter/breathe/flicker) from `core/composition/vc_motion.h`. New `VFX_Compose*` components must be assembled from material + motion + primitives (`vc_common.inl`); hard-coded colors only for deliberate identity breaks, with a comment. New motion formulas worth reusing go into `vc_motion.h`, not inline.
- Scale (real-world-scaled, 1 unit = 1 meter — see root `CLAUDE.md` "Standard coordinates & scale"): radii ~0.10–0.20f, force 3.0–7.0f (compare against real gravity 9.81f), speed 1.0–3.0f. Only `entities/`, `sandbox/`, `main.c`, and the pilot skills (`fire_ball`, `thunder_orb_skill`) have been converted — most skills still use the old 1cm-scale numbers 100x larger.

## Cross-agent communication
- If the Skills Agent asks about an API: answer from the `.h` headers in `core/`
- Need to know how a skill uses an API: read only its `.h`, never its `.c`
- Any breaking change must be clearly documented

## Debugging loop (MANDATORY)
1. Classify the question: arithmetic/logic, C→shader wiring, runtime path/state, or visual quality.
2. Before editing production code, create the smallest deterministic reproduction: usually a filtered `core/tests/*_test.c`; use a state-change `TraceLog` or discriminative debug view only when a headless test cannot observe the path.
3. Form a hypothesis with a falsifiable prediction and run the cheapest experiment that separates it from alternatives.
4. Patch the smallest source-of-truth location. Avoid compensating fixes at callers when the shared primitive or contract is wrong.
5. Run the targeted test after every edit, then the full Core suite before handoff.
6. Document the reusable lesson and remove temporary instrumentation.

Every regression guard must fail on the pre-fix behavior and pass after the fix. Prefer behavioral assertions over source-string assertions. When a C test mirrors GLSL, also assert that the shader's load-bearing expressions still exist so the mirror cannot silently drift; explicitly state what the mirror cannot validate.

## Verification ladder (MANDATORY — cheapest first)
1. **Focused headless repro:** `./scripts/run_core_tests.sh <filename-filter>` — run after every edit to the implicated logic, shader contract, or wiring.
2. **Full headless suite:** `./scripts/run_core_tests.sh` — run after Core changes before handoff. Tests are standalone, deterministic, and require no game, raylib, GL, GPU, or window.
3. **Generated-contract checks, when relevant:**
   - Public API changed → regenerate `core/docs/API.md` as described below and review the diff.
   - `VFX_Compose*` added/removed → run `python3 scripts/sync_vfx_test.py --check`; coordinate with the Sandbox owner before applying changes under `sandbox/`.
   - VFX surface registry changed → run `python3 scripts/validate_vfx_surface_registry.py`.
4. **Visual confirmation:** Core has no automated visual tier yet. For appearance, integration, draw-order, or backend behavior, provide the exact sandbox fixture and expected observation for the human to run. Do not claim visual verification from green headless tests.
5. **Full game build/run:** human-run only, final confirmation; never read or write `build/`.

For a new behavior or bug fix, add the focused repro first. If a test is impossible, explain the missing observation boundary and use the narrowest available probe rather than silently skipping verification.

## Updating `docs/API.md` — it is GENERATED, do not hand-edit
`docs/API.md` is an **index generated from the `core/*.h` headers** by `scripts/gen_core_api_index.sh`. The Signature Index never drifts because it is re-extracted from source.
- **Changed a signature/struct/enum?** Edit the header (the source of truth), then run `bash scripts/gen_core_api_index.sh > core/docs/API.md`. Do **not** hand-edit the Signature Index.
- **A usage contract / gotcha worth documenting?** Put it in the header's own comment (it's ground-truth there), or — if it's the "what a bare signature can't tell you" kind — in the hand-authored **Critical usage rules** preamble, which lives in the heredoc at the top of `scripts/gen_core_api_index.sh` (edit there, regenerate). Reusable debugging lessons go in `docs/LANDMINES.md`, not the index.
- Struct fields are intentionally NOT in the index (only struct names) — the header is the place to read them.

## Docs layout (per `DOC_ARCHITECTURE.md`)
- `docs/API.md` — **generated index** of signatures/enums/struct-names + a critical-rules preamble (the `_SHORT` companion is abolished). Regenerate via `scripts/gen_core_api_index.sh`; never hand-edit.
- `docs/API_GUIDE.md` — **hand-maintained usage guide** (prose companion to the index): patterns, worked examples, contracts, the "why". Keep it current when API usage changes.
- `docs/LANDMINES.md` — distilled reusable lessons. Cross-cutting ones live in root `ENGINE_LANDMINES.md` — **read that before touching GL/shaders.**
- `docs/PROGRESS.md` — backlog / session log.

---

## Token-efficiency rules (MANDATORY)

1. **Never read a whole file when only part of it is needed.** Use `Read` with `offset`/`limit`, or `grep`/`Grep` to find the symbol/line before reading the full file.
2. **Don't re-read a file already read this session** unless it was edited or may have changed externally.
3. **Narrow lookup → grep/find directly.** Only spawn the `Explore` agent for broad searches (many files, many patterns, >3 lookups).
4. **Don't dump a full file into your response.** Cite `path:line`, paste only the snippet directly relevant to the issue at hand.
5. **Batch independent read calls in one message** (parallel tool calls) instead of issuing them sequentially.
6. **Don't read another module/directory "just in case."** Only read another module's `.h` when you actually need a signature to call its API — not preemptively "for context."
7. **Generated/build-output files** (e.g. `skills_generated.h`, `maps_generated.h`, `skills_config.h`) — only read them when debugging something specific to them, not during a general survey.
8. **Cross-module communication: ask for the answer, not the file.** When you need info from another module, ask the owning agent for a specific answer instead of asking them to paste the whole file.
9. **Summarize instead of re-listing.** When reporting findings from a multi-file survey, summarize the key takeaways — don't re-list everything you read.

## Agent response rules (MANDATORY)

1. **Respond in English**, not Vietnamese — fewer tokens for the same content.
2. **Be terse.** No restating the task, no filler intros ("Sure, I'll..."), no trailing summaries unless asked.
3. **Lead with the answer/result**, then justify only if non-obvious.
4. **No verbose prose for simple facts.** A one-line answer beats a paragraph.
