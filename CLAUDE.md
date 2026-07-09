# Wuxing Skills — Root Agent Guide

C/Raylib 6.0 / OpenGL 3.3 game project. Isometric Night-time Arena. 6 elements: Water, Wood, Fire, Earth, Metal, Taiji.

## Reference docs
- `CORE_API.md` — Full engine API (particle, trail, force field, shader, mesh...)
- `SKILL_RECIPE.md` — **One-prompt skill creation guide** (archetype picker, command sequence, element presets, scale rules, aesthetic checklist)
- `COMPUTE_API.md` — GPU compute particle system (shared by skills + environment)
- `ENVIRONMENT_API.md` — Lighting, shadow, fog system
- `MAP_API.md` — Map creation & management
- `ENTITIES_API.md` — Agent pool, vertical physics, damage entry point (minimal gameplay layer)
- `VFX_ARCHITECTURE.md` — Overall VFX architecture
- `WUXING_ART_DIRECTION.md` — Art style and aesthetic laws
- `nguhanhtyvo_kehoach.md` — Game design doc (source of truth for gameplay intent)

## Module Agents — Ownership split

| Agent | Owns | Extra read access |
|---|---|---|
| **Core Agent** | `core/` | `.h` headers of skills, maps, environment |
| **Compute Agent** | `compute/` | `COMPUTE_API.md`, `core/resource_manager.h` |
| **Skills Agent** | `skills/`, `CORE_API.md` (shared write w/ Core Agent) | `core/*.h`, `compute/gpu_particle_system.h`, `environment/environment_system.h`, `assets/` |
| **Map Agent** | `maps/` | `environment/environment_system.h`, `core/skill_manager.h`, `assets/` |
| **Environment Agent** | `environment/` | `core/decal_system.h`, `core/skill_manager.h`, `compute/gpu_particle_system.h` |
| **Entities Agent** | `entities/` | `core/skill_manager.h`, `ENTITIES_API.md` — minimal scope, see `entities/CLAUDE.md` |
| **Sandbox Agent** | `sandbox/` | `.h` headers of ALL modules (dev/test integration harness, not shipped gameplay) — see `sandbox/CLAUDE.md` |

## Directories FORBIDDEN to every agent
```
build/
_deps/
android.wuxing_skills/
```
Never read, list, or touch any file under these.

## Cross-module rules
- A module agent only reads `.h` (interface) of other modules — **never `.c`**
- Need to change a file owned by another module → ask that module's agent to do it
- Breaking API changes must be documented before being applied

## Build
Out-of-source, everything (CMake cache, `_deps` incl. raylib checkout, object
files, binary) lives under `build/` — keeps the repo root clean. `build/`
(like `_deps/`, `android.wuxing_skills/`) is off-limits to agents to read/
list/touch; only the human runs these commands directly.
```bash
cmake -S . -B build         # Configure once (or after CMakeLists.txt changes)
cmake --build build -j4     # Build the whole project
./build/wuxing               # Run the game
# Key K: cycle through maps
```

## Standard coordinates & scale
Real-world-scaled: **1 unit = 1 meter** (rescaled from the old de facto 1 unit = 1cm — see `entities/entities.c`, `sandbox/sandbox_core.c`, `maps/*/`, `main.c` camera clip planes). `PHYSICS_GRAVITY_MPS2 = 9.81f` is the real-world reference every force/gravity value should be judged against when tuning a skill (see `core/tuning.h` §3b and `RegisterSkillTunables` in `core/skill_manager.h` for the sandbox live-tuning UI that makes this practical).
- Arena center: `(6.0f, 0.0f, 4.4f)`, radius: `18.0f`
- Y = 0.0f: ground level
- Mesh radii: 0.10–0.20f | Force/gravity: 3.0–7.0f (compare against real gravity 9.81f) | Particle speed: 1.0–3.0f

Conversion complete engine-wide: `entities/entities.c`, `sandbox/sandbox_core.c`, `main.c`, `maps/*/`, and every skill under `skills/` are now on this 1-unit-1-meter scale. Any **new** skill must be authored meter-scaled from the start — never introduce 1cm-scale (old ×100) numbers.

## Token-efficiency rules for every agent (MANDATORY)

Each sub-module `CLAUDE.md` (`core/`, `compute/`, `skills/`, `maps/`, `environment/`) has its own "Token-efficiency rules" block — always apply it. General principles for cross-agent coordination:

1. **Read with intent, not exploratory reads.** Grep/find the symbol first, read the full file only if truly needed.
2. **Don't re-read a file already read this session** unless it was edited or may have changed externally.
3. **Narrow lookup → grep/find directly. Broad search (>3 lookups) → spawn the `Explore` agent.** Don't spawn an agent for what one shell command can do.
4. **Cross-module communication: answer the question, don't hand over the file.** When an agent needs info from another module, the owning agent answers directly (signature, value, location) instead of pasting the whole file.
5. **Don't dump full code/files into responses.** Cite `path:line`, paste only the directly relevant snippet.
6. **Batch independent tool calls in one message** instead of calling them sequentially.
7. **Skip generated/build-output files** (`*_generated.h`, `*_config.h`, anything under `build/`, `_deps/`, `android.wuxing_skills/`) unless debugging something specific to them — and never read the 3 forbidden directories.
8. **Summarize, don't re-list.** Report findings from multi-file surveys as key takeaways, not a replay of everything read.

## Agent response rules (MANDATORY)

1. **Respond in English**, not Vietnamese — fewer tokens for the same content.
2. **Be terse.** No restating the task, no filler intros ("Sure, I'll..."), no trailing summaries unless asked.
3. **Lead with the answer/result**, then justify only if non-obvious.
4. **No verbose prose for simple facts.** A one-line answer beats a paragraph.
