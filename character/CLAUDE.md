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
- `unreal-engine-starter-content-main/` (off-limits without explicit permission)

## Current scope

1. **One shared `Model` + `ModelAnimation[]`**, loaded once via
   `CharacterModel_Load(filePath)` (cached inside `core/resource_manager.h`,
   not duplicated here). Returns `false` gracefully if the file doesn't exist
   or has no usable animation clips — never crashes, safe to call before an
   asset has been provided.
2. **`CharacterAnimSlot`** — Multi-variant slots covering locomotion, agility,
   martial attacks, sword arts (Kiếm Pháp), casting, and hit reactions. Each slot
   holds up to `CHAR_ANIM_MAX_VARIANTS` (8) animation choices, allowing multiple
   combo/action variations per move. Supports auto-keyword matching and optional
   JSON manifest overrides (`assets/characters/player_anims.json`).
3. **`CharacterAnimState`** — one per character instance (`frame`, `frameTimer`,
   `currentSlot`, `activeVariant`, `activeAnimIndex`, `oneShotActive`, `oneShotSpeed`).
   Supports `CharacterModel_TriggerVariant`, `CharacterModel_TriggerTimed`, and
   direct by-name playback via `CharacterModel_TriggerByName(state, "clip_name", secs)`.
4. **Mixamo & Procedural Import Pipeline** (`scripts/import_character_anim.py`) —
   CLI to import any external `.fbx` (e.g. `Jumping Down.fbx`) or `.bvh` directly
   into `character/raw_model/animations/`, rebuild `assets/characters/player.glb`,
   and map to slots/manifest automatically.
5. **Modular Character System** (`character/modular_character.h/.c`) —
   Supports Heroes (players), Bosses, and Swarm Minions (clustered pose sharing).
   Enables independent equipping of `MODULAR_SLOT_BASE_BODY`, `MODULAR_SLOT_TORSO`,
   `MODULAR_SLOT_LEGS`, `MODULAR_SLOT_BOOTS`, `MODULAR_SLOT_BRACERS`,
   `MODULAR_SLOT_HEADGEAR`, `MODULAR_SLOT_BACK`, `MODULAR_SLOT_WEAPON_R`, and `MODULAR_SLOT_WEAPON_L`.
6. **Player Wardrobe & Collection System** (`character/wardrobe_system.h/.c`) —
   Declarative JSON catalog (`assets/characters/item_catalog.json`), personal
   collection unlocking via world exploration, persistent save/load, real-time
   3D synchronization with `ModularInstance`, and combat stance resolution
   (`COMBAT_STANCE_SWORD` vs `COMBAT_STANCE_UNARMED`).

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
