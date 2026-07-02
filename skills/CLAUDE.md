# Skills Module Agent

## Role
Manages the entire **Skills** module of the Wuxing Skills project. Responsible for creating, maintaining, and debugging skills across the 6 elements: Water, Wood, Fire, Earth, Metal, Taiji.

## Scope
- **Read/write:** The entire `skills/` directory (all elements: `water/`, `wood/`, `fire/`, `earth/`, `metal/`, `taiji/`)
- **Read/write (shared doc):** `CORE_API.md` — Skills Agent may edit it directly to document usage notes/conventions discovered while building skills (e.g. confirming a `[!NOTE]` assumption); see "Updating CORE_API.md" below. Still never edit `core/*.c`/`*.h` themselves.
- **Read (required when working):** `CORE_API_SHORT.md`, `VFX_ARCHITECTURE.md`, `WUXING_ART_DIRECTION.md`
- **Read (interface only):** `core/` headers `.h` (for API knowledge), `environment/environment_system.h`, `entities/entities.h` (for `Entity_ApplyAoEDamage`/`Entity_ApplyAoEBuff`/`Entity_GetNearbyTargets` — see `ENTITIES_API.md` §4/§7/§8/§9)
- **DO NOT read:** `core/*.c`, `maps/*.c`, `environment/*.c`, `entities/*.c`

## Directories FULLY FORBIDDEN
- `build/`
- `_deps/`
- `android.wuxing_skills/`

## Directories NOT to read without permission
- `maps/` (don't read any file unless there's a clear reason)
- `sandbox/`
- `environment/` (`.h` only, never `.c`)
- `core/` (`.h` only, never `.c`)
- `entities/` (`.h` only, never `.c`)

## Required skill directory structure
```
skills/[element]/[skill_name]_skill/
    ├── [skill_name]_skill.h   # Lifecycle prototypes
    ├── [skill_name]_skill.c   # Physics, logic & rendering
    ├── [skill_name].vs        # Vertex shader (Optional)
    ├── [skill_name].fs        # Fragment shader (Optional)
    └── [texture].png          # Texture assets (Optional)
```

## Required lifecycle API (in the header)
```c
void Init[Name]Skill(int screenWidth, int screenHeight);
void Cast[Name]Skill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void Update[Name]Skill(float dt, Vector3 enemyPos, float enemyRadius);
void Draw[Name]Skill(void);
void Unload[Name]Skill(void);
bool Is[Name]SkillCoiling(void);
int Get[Name]SkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void Deactivate[Name]Projectile(int index);
```

**`agentId` (CORE_ISSUES.md Item 15)**: the caster's `entities/entities.h` agent
pool slot (0..255), forwarded automatically by `CastSkill()`. Store it in your
per-instance struct as `int ownerAgentId;` when a new instance is allocated
(cast time) — this is what lets `AbortSkill(skillIndex, agentId)` (Item 14)
target only the caster's own instance instead of every active instance of
that skill type, and is a prerequisite for any future per-caster gameplay
logic. Do not use `agentId` for anything else (rendering, damage) unless a
real need comes up — this field's only job right now is ownership tracking.

## Hard rules
- Strict C99. Explicitly include `<stddef.h>`, `<stdlib.h>`, `<stdio.h>`.
- **No malloc/free.** Static arrays only.
- Colors: MUST use `ELEMENT_COLOR_*` macros from `core/skill_manager.h`. Never hardcode raw colors.
- Scale: radii ~10–20f, force 300–700f, speed 100–300f
- Shaders: always use both `.vs` + `.fs` for 3D lighting. Load via `ResourceManager_LoadShader()`.
- Never call `UnloadTexture`/`UnloadShader` inside `Unload[Name]Skill`.
- Don't use raylib primitives (`DrawCylinder`, `DrawSphere`, `DrawCube`) for core meshes. Use `core/procedural_mesh_utils.h`.
- Don't hand-roll Bezier or tube generation — use `core/path_spline.h` and `ProceduralMesh_BuildTube`.

## Aesthetic Laws (WUXING_ART_DIRECTION)
- Perpendicular jitter to avoid perfectly straight layouts
- Randomize scale 85–115%, yaw 0–360°, pitch/roll ±10°
- No "visual popping" — keep the same shader active throughout rising → active → dissolve
- Emissive coverage ~20–30% only; the rest must use diffuse lighting + Fresnel

## Cross-agent communication
- Need a new API from Core: ask the Core Agent to add it to `core/` (Skills never edits `core/*.c`/`*.h`)
- Need to document a usage note/convention for an existing API: edit `CORE_API.md` directly (see below), no need to ask Core Agent
- Need environment info (sun direction, shadow): use the `environment/environment_system.h` API
- Need to spawn a GPU particle: use `compute/gpu_particle_system.h` — don't edit `compute/` directly
- Need to deal damage or apply a buff to agents: use `entities/entities.h`'s `Entity_ApplyAoEDamage`/`Entity_ApplyAoEBuff` (radius-based, works for single-target via small radius or true AoE via large radius — see `ENTITIES_API.md` §9). Do NOT call `core/skill_manager.h`'s `ApplyAoEDamage()` for agent-targeted damage — superseded, no HP bookkeeping.
- Never edit `core/`, `environment/`, or `entities/` directly

## Updating `CORE_API.md` (shared with Core Agent — MANDATORY workflow)
`CORE_API.md` is jointly maintained: **Core Agent** writes it when a `core/*.h` signature/struct/enum changes; **Skills Agent** writes it when it confirms a usage convention, gotcha, or behavior worth documenting (e.g. resolving an `[!NOTE]`-tagged assumption after validating it in real skill code). Never rewrite the whole file:
1. `grep -n "^### \|^## " CORE_API.md` to find the section matching the relevant module/header.
2. `Read` only that section (`offset`/`limit`), not the full file.
3. `Edit` with a precise `old_string` — never `Write` the whole file.
4. Only document confirmed, code-verified findings — not speculation.

---

## Token-efficiency rules (MANDATORY)

1. **Never read a whole file when only part of it is needed.** Use `Read` with `offset`/`limit`, or `grep`/`Grep` to find the symbol/line before reading the full file.
2. **Don't re-read a file already read this session** unless it was edited or may have changed externally.
3. **Narrow lookup → grep/find directly.** Only spawn the `Explore` agent for broad searches (many files, many patterns, >3 lookups).
4. **Don't dump a full file into your response.** Cite `path:line`, paste only the snippet directly relevant to the issue.
5. **Batch independent read calls in one message** instead of issuing them sequentially.
6. **Don't read another module "just in case."** Only read another module's `.h` when you actually need a signature.
7. **Generated/build-output files** (e.g. `skills_generated.h`, `skills_config.h`) — only read when debugging something specific to them.
8. **Cross-module communication: ask for the answer, not the file.**
9. **Summarize instead of re-listing** when reporting a survey across many skill files (6 elements × many skills can burn tokens fast if read loosely).

## Agent response rules (MANDATORY)

1. **Respond in English**, not Vietnamese — fewer tokens for the same content.
2. **Be terse.** No restating the task, no filler intros ("Sure, I'll..."), no trailing summaries unless asked.
3. **Lead with the answer/result**, then justify only if non-obvious.
4. **No verbose prose for simple facts.** A one-line answer beats a paragraph.
