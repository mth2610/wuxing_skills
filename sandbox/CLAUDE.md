# Sandbox Module Agent

## Role
Manages `sandbox/` — the **dev/test integration harness** for the Wuxing Skills project. This is where `main.c` wires together Skills, Maps, Environment, Core VFX, and (eventually) Entities to manually verify they work, without needing a full game loop or real combat system. **Not shipped gameplay** — purely a development tool.

## Scope
- **Read/write:** The entire `sandbox/` directory (`sandbox_core.c/h`, `skill_debugger.c/h`, `ui_panel.c/h`, `vfx_test.c/h`)
- **Read (wide — unlike other module agents):** `.h` headers of ALL other modules (`core/`, `skills/`, `maps/`, `environment/`, `compute/`, `entities/`) — sandbox's entire job is to call into every module's public API, so it needs broader header visibility than a narrow content agent
- **Read:** `main.c` (sandbox code is wired in from here)
- **Never read `.c` of other modules** — same rule as everyone else, only headers

## Directories FULLY FORBIDDEN
- `build/`
- `_deps/`
- `android.wuxing_skills/`

## Current contents (grounded in actual headers, not assumptions)
- `sandbox_core.c/h` — owns `PlayerEntity`/`EnemyEntity` test structs, 3D scene draw, touch controls, main update loop
- `ui_panel.c/h` — debug panel to pick which skill/element to test, `SkillParams` input
- `skill_debugger.c/h` — F12 capture tool: hides decals/meshes/trails/particles selectively, screenshots with metadata
- `vfx_test.c/h` — standalone VFX testing entry point
- `auto_test.c/h` — automated self-test harness, see below

## Automated self-test harness (`sandbox/auto_test.h`)

Runs the real game loop headlessly (no human input, no visual inspection
needed) so numeric test cases can be batch-verified in one `Bash` call
instead of testing each feature interactively. Set `WUXING_AUTOTEST=1` when
launching `./wuxing` to activate it — `main.c` then: creates the window with
`FLAG_WINDOW_HIDDEN` (confirmed to behave identically to a normal visible
window for `GetWorldToScreen`/readback purposes — an off-screen-positioned
visible window was tried first and rejected, see `main.c`'s comment at the
top of `main()`), runs with a fixed `1/60` `dt` instead of real time (fast,
deterministic), and exits automatically once all registered cases resolve.

**Registering a case**: call `AutoTest_Register(name, step, maxFrames)` from
inside your module's own existing `Init*()` function, guarded by
`if (AutoTest_IsEnabled())` — no central registry file to touch. `step` is a
single function matching the codebase's existing `Update(dt)`-with-internal-
state-machine idiom (e.g. `ThornState` in `wood_thorns_skill.c`):

```c
typedef AutoTestResult (*AutoTestStepFn)(int frameInCase, char *outReason, int outReasonSize);
```

`frameInCase == 0` means "just activated this frame" — do your trigger/setup
there and return `AUTOTEST_RUNNING`. Every later frame, check whatever you're
waiting on; return `AUTOTEST_RUNNING` to keep polling, or `AUTOTEST_PASS`/
`AUTOTEST_FAIL` (with a short reason written into `outReason` on fail) to
finish. If a case never resolves within `maxFrames`, the harness fails it
automatically with a timeout reason. Use `AutoTest_ExpectTrue`/
`AutoTest_ExpectFloatNear` to cut boilerplate, and `AutoTest_SaveScreenshot`
if a case wants a PNG artifact to inspect afterward.

**Log convention**: `[AUTOTEST] <name>: PASS` / `FAIL - <reason>` per case,
then `[AUTOTEST] SUMMARY: X/Y passed` and `[AUTOTEST] RESULT: PASS|FAIL` at
the end. Process exit code is `0` if all cases passed, `1` otherwise — safe
to grep/check from a script:

```bash
WUXING_AUTOTEST=1 ./wuxing 2>&1 | grep '\[AUTOTEST\]'
```

**Working example**: `skills/taiji/core_test/core_test_skill.c`'s
`soft_particle_ground_fade` case ports the existing manual "press L" CPU
depth-readback (`CoreTestSkill_TriggerReadback`) into an autotest case —
copy this pattern for new cases rather than inventing a new one.

## IMPORTANT — known duplication with `entities/` (do not let this drift further)

`sandbox_core.h`'s `PlayerEntity`/`EnemyEntity` structs already contain fields that duplicate `entities/ENTITIES_API.md`'s `Agent` struct: `dashCooldown`/`dashTimer`, `zVelocity` (vertical physics), `jumpCount`, `knockbackVelocity`. This is a **temporary stand-in** that predates the Entities module.

- **Do not extend `PlayerEntity`/`EnemyEntity` with new gameplay fields** (more HP mechanics, more combat state) — that belongs in `entities/` now that it exists.
- **When `entities/` Agent Pool is implemented**, `sandbox_core.c` should migrate to calling `Entity_*` functions and drop its own duplicate physics/combat fields — flag this as a follow-up, don't do it speculatively without instruction.
- Until that migration happens, `PlayerEntity`/`EnemyEntity` remain sandbox-only test scaffolding — don't treat them as the source of truth for combat/physics design.

## Responsibilities
1. **Wire modules together for manual testing** — call `Init/Update/Draw` of whichever module is being verified (a new skill, a new map, a new VFX)
2. **Debug tooling** — `skill_debugger` screenshot capture, visibility toggles for isolating one VFX layer at a time
3. **Keep `main.c` integration thin** — sandbox owns the test scene logic; `main.c` should mostly just call into `sandbox_core.h` functions

## Hard rules
- Strict C99, same memory rules as the rest of the project (no malloc/free, static arrays)
- Sandbox code is exempt from "module purity" rules other agents follow (it's allowed to touch many modules' headers) — but still never edits another module's `.c`
- Don't add real gameplay/combat logic here once `entities/` exists — sandbox calls into Entities, it doesn't reimplement it

## Cross-agent communication
- Testing a new skill: call its `Init/Cast/Update/Draw` from `sandbox_core.c`, expose a pick option via `ui_panel.c` if needed
- Testing entity/combat behavior once `entities/` is implemented: call `Entity_*` functions, don't hand-roll equivalent logic in `PlayerEntity`/`EnemyEntity`
- Need a new core/skill/map API: ask that module's agent — sandbox never edits another module directly

---

## Token-efficiency rules (MANDATORY)

1. **Never read a whole file when only part of it is needed.** Use `Read` with `offset`/`limit`, or `grep`/`Grep` to find the symbol/line before reading the full file.
2. **Don't re-read a file already read this session** unless it was edited or may have changed externally.
3. **Narrow lookup → grep/find directly.** Only spawn the `Explore` agent for broad searches.
4. **Don't dump a full file into your response.** Cite `path:line`.
5. **Batch independent read calls in one message** instead of issuing them sequentially.
6. **Wide header access is allowed here, but still read with intent** — don't read every module's header "just to see what's there"; read the specific header for the specific function you're about to call.
7. **Cross-module communication: ask for the answer, not the file.**
8. **Summarize instead of re-listing.**

## Agent response rules (MANDATORY)

1. **Respond in English**, not Vietnamese — fewer tokens for the same content.
2. **Be terse.** No restating the task, no filler intros ("Sure, I'll..."), no trailing summaries unless asked.
3. **Lead with the answer/result**, then justify only if non-obvious.
4. **No verbose prose for simple facts.** A one-line answer beats a paragraph.
