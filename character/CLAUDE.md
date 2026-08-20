# Character Module Agent

## Role
Manages the **Character** module — the visual/rendering counterpart to
`entities/`'s pure gameplay logic. Owns loading a real 3D character model +
skeletal animation (raylib `Model`/`ModelAnimation`) and its playback state
machine (idle/walk/punch/kick/palm), as opposed to the procedural stick-figure
`DrawCharacter3D` (`sandbox/sandbox_core.c`) it's meant to replace once a real
asset exists. Not a `core/` engine primitive (it's specific to "a combat
character", not a generic reusable utility like particle/shader/mesh) and not
`entities/` (which forbids rendering entirely) — hence its own module.

This module is new and intentionally minimal — one shared model instance,
player-only for now. Not in the original module list; added
because `entities/entities.h`'s `BasicAttackType` (punch/kick/đá/chưởng)
needed a real visual to play, and neither `core/` nor `entities/` was the
right home (see git history / conversation that created this module for the
full reasoning).

## Scope
- **Read/write:** The entire `character/` directory (`.c`, `.h`)
- **Read (interface only):** `core/resource_manager.h` (Model/ModelAnimation loading + caching)
- **Never include:** `entities/entities.h` — keep layering clean. Callers
  (sandbox/game) map their own attack-type enum to `CharacterAnimSlot`
  themselves, one line per case.

## Directories FULLY FORBIDDEN
- `build/`
- `_deps/`
- `android.wuxing_skills/`

## Current scope (minimal — DO NOT add beyond this without explicit instruction)

1. **One shared `Model` + `ModelAnimation[]`**, loaded once via
   `CharacterModel_Load(filePath)` (cached inside `core/resource_manager.h`,
   not duplicated here). Returns `false` gracefully if the file doesn't exist
   or has no usable animation clips — never crashes, safe to call before an
   asset has been provided.
2. **`CharacterAnimSlot`** — `IDLE, WALK, PUNCH, KICK, PALM`. Clip names are
   matched to slots by case-insensitive substring (`"idle"`, `"walk"`,
   `"punch"`/`"jab"`, `"kick"`, `"palm"`/`"push"`/`"slap"`) since real Mixamo
   export names aren't known ahead of time — `TraceLog` reports which slots
   resolved/didn't on load.
3. **`CharacterAnimState`** — one per character instance (`frame`,
   `frameTimer`, `currentSlot`, `oneShotActive`). `CharacterModel_Update`
   picks IDLE/WALK when no one-shot is playing; `CharacterModel_TriggerAttack`
   starts a one-shot PUNCH/KICK/PALM that auto-reverts to IDLE on finish.
4. **Player-only.** Only one `CharacterAnimState` is driven right now
   (`sandbox/sandbox_core.h`'s `PlayerEntity.anim` and the same shared player
   in `game/game_screen.c`). Enemy/dummy still use the old
   `DrawCharacter3D` stick figure — out of scope until explicitly asked.

## Explicitly OUT of scope for now (do not build until instructed)
- Animating more than one character instance at a time — raylib's
  `UpdateModelAnimation` mutates the shared `Model`'s mesh buffer in place
  (CPU skinning); two instances animating independently in the same frame
  would clobber each other's pose. Needs per-instance mesh copies or a
  different technique before a second instance (enemy/boss) can animate.
- Animation blending/crossfade — hard frame switch on slot change today.
- Loadout/equipment visuals, cosmetics, multiple character models.

## Hard rules
- Strict C99. No malloc/free — the `Model`/`ModelAnimation*` come from
  `core/resource_manager.h`'s cache, this module never allocates/frees them
  directly.
- Never call `UnloadModel`/`UnloadModelAnimations` here — `core/resource_manager.h`
  owns that lifetime (mirrors the existing "never call UnloadTexture/UnloadShader"
  rule for other resource types).
- Every public function must be a safe no-op when `CharacterModel_IsLoaded()`
  is false — this module must never be the reason the game crashes just
  because an asset file is missing.

## Cross-agent communication
- Need Model/animation loading changes: this module owns
  `core/resource_manager.h`'s `ResourceManager_LoadModel`/`LoadModelAnimations`
  usage, but the actual cache lives in `core/` — ask Core Agent for changes to
  the cache itself, this module just calls it.
- Need a new attack type/animation slot: add to `CharacterAnimSlot`, update
  the keyword table in `character_model.c`, document here.
- sandbox/ and game/ both call into this module directly (`.h` only) — do not
  let either module's specifics leak back into `character/`.

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
