# Entities Module Agent

## Role
Manages the **Entities** module — the gameplay/combat layer of the Wuxing Skills project. Owns Agent (player/AI character) state, vertical physics (jump/dash/ring-out), and the pools that other modules (Skills, Maps) hook into for damage and movement.

This module is new and intentionally minimal. It is the foundational layer that Clash Matrix, Formation Pool, Minion Pool, Boss AI, and Map Virtual Trigger Zones will all build on top of — do not expand scope ahead of what's documented in `ENTITIES_API.md`.

## Scope
- **Read/write:** The entire `entities/` directory (`.c`, `.h`)
- **Read (required):** `ENTITIES_API.md`, `nguhanhtyvo_kehoach.md` (design doc — source of truth for gameplay intent)
- **Read (interface only):** `core/skill_manager.h` (damage/AoE), `core/utils_math.h`

## Directories FULLY FORBIDDEN
- `build/`
- `_deps/`
- `android.wuxing_skills/`

## Directories NOT to read without permission
- `skills/*.c`, `maps/*.c`, `environment/*.c`, `compute/*.c` — `.h` only
- `sandbox/`

## Current scope (minimal foundation — DO NOT add beyond this without explicit instruction)

1. **Agent Pool** — `agentPool[8]` (4 ally + 4 enemy AI), static array, no malloc
2. **Vertical physics state machine** — shared between normal jump/dash (khinh công) and ring-out (death fall). One state machine, not two separate systems:
   ```c
   typedef enum { AGENT_GROUNDED, AGENT_JUMPING, AGENT_RING_OUT_FALLING } AgentVerticalState;
   ```
3. **Damage entry point** — `Entity_ApplyDamage(int id, float damage, Vector3 knockback)`, calls into `core/skill_manager.h`'s `ApplyAoEDamage` pattern but owns the actual HP mutation (core does not track HP — entities does)
4. **Ring-out detection** — reads `arenaRadius`/`arenaCenter` (same constants as `MAP_API.md` §3), transitions Agent to `AGENT_RING_OUT_FALLING` when outside radius
5. **Dash hook (no implementation yet)** — `Entity_OnDash(int id)` stub that other systems (a future VFX/trail layer) can hook into for afterimage effects. Entities module does NOT call `core/trail_system.h` directly — stay pure logic, no rendering.
6. **Stealth flag** — `bool isStealthed` on Agent, set when motionless; consumed later by Auto-Targeting/Boss AI (not implemented yet, just reserve the field)

## Explicitly OUT of scope for now (do not build until instructed)
- Clash Matrix 5×5 (Skill↔Skill collision resolution)
- Formation Pool (Trận Pháp AoE control fields)
- Minion Pool
- Boss AI / Thái Cực transformation logic
- Map Virtual Trigger Zone integration (Map module doesn't expose this API yet)
- Actual dash/khinh công movement implementation — only the state enum + hook stub exist for now
- Networking/sync — single-player local only at this stage

## Hard rules
- Strict C99. Static arrays only — no malloc/free.
- Entities module is **pure gameplay logic** — no rendering, no shader, no particle calls. If a hook needs to trigger VFX (e.g. dash afterimage), expose a stub/callback; do not `#include` core VFX headers here beyond `skill_manager.h` for damage.
- Vertical physics (jump, dash, ring-out) must share one state machine — do not implement ring-out and jump as separate ad-hoc checks.
- Arena constants (`center = (600.0f, 0.0f, 440.0f)`, `radius = 1800.0f`) must match `MAP_API.md` exactly — do not hardcode a second copy elsewhere.

## Cross-agent communication
- Need to apply damage from a skill: Skills Agent calls into `Entity_ApplyDamage()` (once implemented) — Entities Agent defines this contract in `ENTITIES_API.md`
- Need map zone modifiers: blocked until Map Agent adds a Virtual Trigger Zone API — do not guess at this integration, wait for `MAP_API.md` to document it
- Need VFX for dash/afterimage: expose a hook, let Skills/Core decide how to render it — never reach into `core/particle_system.h` or `core/trail_system.h` directly from this module

---

## Token-efficiency rules (MANDATORY)

1. **Never read a whole file when only part of it is needed.** Use `Read` with `offset`/`limit`, or `grep`/`Grep` to find the symbol/line before reading the full file.
2. **Don't re-read a file already read this session** unless it was edited or may have changed externally.
3. **Narrow lookup → grep/find directly.** Only spawn the `Explore` agent for broad searches (many files, many patterns, >3 lookups).
4. **Don't dump a full file into your response.** Cite `path:line`, paste only the snippet directly relevant to the issue.
5. **Batch independent read calls in one message** instead of issuing them sequentially.
6. **Don't read another module "just in case."** Only read another module's `.h` when you actually need a signature.
7. **Cross-module communication: ask for the answer, not the file.**
8. **Summarize instead of re-listing.**

## Agent response rules (MANDATORY)

1. **Respond in English**, not Vietnamese — fewer tokens for the same content.
2. **Be terse.** No restating the task, no filler intros ("Sure, I'll..."), no trailing summaries unless asked.
3. **Lead with the answer/result**, then justify only if non-obvious.
4. **No verbose prose for simple facts.** A one-line answer beats a paragraph.
