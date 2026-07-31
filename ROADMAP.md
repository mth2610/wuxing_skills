# ROADMAP — Wuxing Skills

> The **one** project-wide plan/progress doc (`DOC_ARCHITECTURE.md` §4). Consolidates the two
> formerly-scattered plan docs `MODULES_ROADMAP.md` (module build-out, 07/2026) and
> `KE_HOACH_TIEP_THEO.md` (post-Net/EOS priority plan, 12/07/2026) into one English document.
> Source of gameplay intent: `nguhanhtyvo_kehoach.md` (design doc v3.5, Vietnamese).

---

## Part 1 — Module build-out (from `MODULES_ROADMAP.md`, 07/2026)

> Origin: `nguhanhtyvo_kehoach.md` (design v3.5) cross-checked against the codebase state as of
> 07/2026. This part records the **build order + API contract** used to bring up the gameplay
> modules. Each module, when started, got its own `<MODULE>_API.md` (pattern:
> `entities/docs/API.md`) and its own `CLAUDE.md`.

**Status as of 12/07/2026: Modules 1–10 COMPLETE, Module 11 half-done** (autotest 14/14 passing —
`WUXING_AUTOTEST=1 ./build/wuxing`). API docs: `entities/docs/API.md` §15-17, `maps/docs/API.md` §13,
`combat/docs/API.md`, `control/docs/API.md`, `boss/docs/API.md`, `game/docs/API.md`, `ai/docs/API.md`, `ui/docs/API.md`,
`formations/docs/API.md`, `net/docs/API.md`.
Round 2 (Step 0 + Module 8–11): FIRE/GLACIAL/TUBE skill projectiles submitted their colliders to
the combat registry; default loadout on keys 1-4; minion waves tied to boss phase; auto-target
counter-strike priority; 2 formations + River resonance; `net/` got its wire format (ENet
transport pending dependency approval + PvP milestone).
Deliberate spec deviations (documented in detail in the respective API docs): AoE/Spawn signatures
gained a team parameter; `ClashEvent` gained `CLASH_HIT_AGENT`/`otherAgentId` + a peek/BeginFrame
model; `BossDef` uses skill NAME instead of id; `FormationDef.onTick` gained power/ownerTeam;
sandbox stays as the dev harness.

> **Note (this consolidation pass):** per later session memory, Modules 1–11 are now ALL landed
> (ENet transport, EOS online backend, and Team Battle Online — Đợt A below — have since shipped).
> This section is kept as the historical build-order record; see Part 2 for what came after.

### 0. State at the time (already done — do NOT redo)

| Layer | Status |
|---|---|
| `core/` | Full VFX engine: particle, trail, force field, decal, metaball, post FX, proc ray, skill manager (cooldown/mana **formula** already in), map manager, tuning |
| `compute/` | GPU particle system working |
| `environment/` | Lighting / fog / fake shadow working |
| `maps/` | 4 maps (default_arena, bamboo_valley, meadow_night, soft_test_ground) — **no Virtual Trigger Zones yet** |
| `skills/` | ~11 skills across 5 elements — pure VFX + damage calls through entities |
| `entities/` | Minimal agent pool (`MAX_AGENTS=256`), damage entry point, vertical physics/ring-out, AoE, modifier slot, nearby query — **no team, mana, Vô Hệ yet** |
| `sandbox/` | Dev harness (WASD, autotest, debugger) — not shippable gameplay |

**Missing vs. design (as of the 07/2026 round):** Minion + AI (Module 8), HUD/Auto-targeting
(Module 9), Formations (Module 10), networking (Module 11), migrating old projectile skills to the
combat registry, loadout UI for equippedSkills.

### 1. Architecture principles (apply to EVERY new module)

1. **Independence through API:** a module only `#include`s another module's `.h`, never its `.c`.
   All communication goes through public functions declared in a header + documented in
   `<MODULE>_API.md`.
2. **Data-driven so AI can author content:** "content" (map, boss, skill, formation) must be
   **declarative data** (static arrays, config) separate from "engine" (tick/resolve logic). AI
   authoring new content = add one data file + one registration line, no engine edits.
3. **Static arrays, no malloc** (C99, Android). Fixed pools per design §VI.
4. **Logic separate from render:** gameplay modules (combat, ai, formations) must NOT include VFX
   headers; they expose events/hooks for the VFX layer (skills/core) to read and draw (pattern:
   `Entity_OnDash`).
5. **One single top-level Canvas** — no module creates its own `RenderTexture` (design §VIII).
6. **Shared arena constants:** center `(600,0,440)`, radius `1800` — defined in exactly one place
   (currently `maps/docs/API.md` §3 / entities); new modules reference it, never copy it.
   *(Later rescaled to meters — see root `CLAUDE.md`'s "Standard coordinates & scale": center
   `(6.0, 0, 4.4)`, radius `18.0`.)*
7. **Breaking API change → update the `_API.md` file BEFORE changing code.**

### 2. Module build order

| # | Module | Directory | API doc | Depends on (.h only) | Unlocks | Game Phase |
|---|---|---|---|---|---|---|
| 1 ✅ | Entities Combat v2 (Team, Mana, Vô Hệ) | `entities/` (extended) | `entities/docs/API.md` §12+ | — | every module below | 0 |
| 2 ✅ | Map Virtual Trigger Zones | `maps/` + `core/map_manager.h` | `maps/docs/API.md` §9 | map_manager | terrain modifiers, formation resonance | 0 |
| 3 ✅ | Combat — Projectile Registry + Clash Matrix | `combat/` (NEW) | `combat/docs/API.md` | entities.h | Đấu Pháp, auto-targeting, boss AI dodge | 0 |
| 4 ✅ | Player Controller (movement tech, meditation, cast) | `control/` (NEW) | `control/docs/API.md` | entities.h, skill_manager.h, combat.h | real play instead of sandbox | 0 |
| 5 ✅ | Boss (Đại Tinh Linh) | `boss/` (NEW) | `boss/docs/API.md` | entities.h, combat.h + core VFX .h (draw fn only) | Phase 0 DoD, Thái Cực | 0 |
| 6 ✅ | Thái Cực State + Phong/Lôi | `entities/` (state) + `core/post_fx` (shader) + `skills/taiji/` (2 skills) | `entities/docs/API.md` + `core/docs/API.md` | entities, combat, boss | match climax | 0 |
| 7 ✅ | Game Mode (offline match loop) | `game/` (NEW) | `game/docs/API.md` | all `.h` above | complete internal test build | 0 |
| 8 ✅ | Minion Pool + Minion AI | `ai/` (NEW) + entities archetype | `ai/docs/API.md` | entities.h, combat.h | 4v4 with minions | 1 |
| 9 ✅ | HUD + Auto-Targeting | `ui/` (NEW) | `ui/docs/API.md` | entities.h, combat.h, control.h | mobile UX | 1 |
| 10 ✅ | Formations (Trận Pháp Pool) | `formations/` (NEW) | `formations/docs/API.md` | entities.h, map zones, combat.h | area control | 2 |
| 11 ◐→✅ | Networking (ENet, peer-hosted) | `net/` (NEW) | `net/docs/API.md` | game.h, entities.h | PvP 1v1 (later landed to 4v4, see Part 2) | 1→2 |

Ordering rule: **don't skip more than one step** — module #N starts only once #N-1 builds clean +
has a passing sandbox autotest. Exception: #2 (Map Zones) and #3 (Combat) are independent and were
built in parallel by two agents.

### 3. Per-module detail

#### Module 1 — Entities Combat v2 (`entities/`)
**Goal:** turn Agent into a proper combat entity per the design: team, mana, Vô Hệ (elemental
identity).

Additions to `Agent` (non-breaking — new fields):
```c
typedef enum { TEAM_ALLY, TEAM_ENEMY, TEAM_NEUTRAL } AgentTeam;
typedef enum { ARCH_HERO, ARCH_MINION, ARCH_BOSS } AgentArchetype; // shared pool, see memory MAX_AGENTS=256

// Added to Agent:
AgentTeam      team;
AgentArchetype archetype;
float          mana, maxMana;          // Linh Khí
bool           isMeditating;           // Meditation: immobile 3s, fast mana regen
float          meditateTimer;
int            equippedSkills[4];      // equipped skill ids — source for computing Vô Hệ
```

New API:
```c
bool  Entity_SpendMana(int agentId, float cost);          // false if insufficient — skills MUST check before casting
void  Entity_StartMeditate(int agentId);                  // cancels on move/hit
int   Entity_RecomputeElement(int agentId);               // Vô Hệ: majority element among equippedSkills[4]
int   Entity_GetNearbyTargetsTeam(Vector3 c, float r, AgentTeam filter, int *out, int max); // team-filtered variant
```

Modified existing functions: `Entity_ApplyAoEDamage/Buff` gained a team parameter (fixes the
limitation noted in ENTITIES_API §9). `Entity_SpawnAgent` takes `team`/`archetype`.

**DoD:** autotest — spawn 2 teams, AoE only hits enemies, buff only hits allies; mana empty → cast
fails; 3s meditation fully restores mana; changing equippedSkills changes element per the majority
rule.

#### Module 2 — Map Virtual Trigger Zones (`maps/`)
**Goal:** turn static terrain into elemental data zones (design Pillar 2, Layer 3). Map = pure
data; modifier rules live in the consumer (entities/game), NOT in the map.

Added to `core/map_manager.h`:
```c
typedef enum { NAT_NONE, NAT_RIVER, NAT_FOREST, NAT_DESERT_ZONE } NatureZoneType;

typedef struct {
    NatureZoneType type;
    Vector3 center;   // ground-hugging, y = 0
    float   radius;   // XZ-distance check, same as Entity_GetNearbyTargets
} MapZone;

#define MAX_MAP_ZONES 16

// Each map declares in <map>.h/.c:
int            Map_GetZoneCount(void);                 // for the active map
const MapZone *Map_GetZone(int index);
NatureZoneType Map_QueryZoneAt(Vector3 pos);           // NAT_NONE if outside every zone
```

Modifier rule table (static lookup, lives in `game/` or `entities/`, NOT in map):

| Zone | Element benefited | Element penalized |
|---|---|---|
| `NAT_RIVER` | Water: -50% cooldown | Fire: -50% damage |
| `NAT_FOREST` | Wood: +50% poison, stealth (isStealthed) | Metal: -cast speed; Earth projectiles entering -50% dmg |
| `NAT_DESERT_ZONE` | Earth: +knockback | Water: -50% range |

**AI authoring new maps:** add a map = 1 directory per `maps/docs/API.md` §7 + a `static MapZone zones[]`
array + a visual cue matching the zone's position (river/forest/sand mesh). Engine code doesn't
change. Zones must have a clear visual cue (No Tutorial — players discover it themselves).

**DoD:** debug_draw renders the zone rings; an agent standing in a river gets the correct modifier
(tested through the Module 1 API); a map with no zones still runs normally.

#### Module 3 — Combat: Projectile Registry + Clash Matrix (`combat/`, NEW)
**Goal:** Đấu Pháp — the gameplay's core. Skill↔Skill (Layer 1) and standardized Skill↔Entity
(Layer 2).

**Immediate-mode design:** a skill still owns its projectile's motion + VFX; each frame it just
*submits* a collider to the registry; combat resolves collisions and returns an **event** so the
skill draws its own clash VFX. This keeps skills and combat fully decoupled.

```c
typedef enum { ELEM_WATER, ELEM_WOOD, ELEM_FIRE, ELEM_EARTH, ELEM_METAL } CombatElement; // matches Agent.currentElement

typedef enum { CLASH_MUTUAL_DESTROY, CLASH_A_WINS, CLASH_B_WINS, CLASH_PASS_THROUGH } ClashOutcome;

#define MAX_COMBAT_PROJECTILES 128

// Skill calls EVERY FRAME while the projectile is alive (immediate-mode submit):
int  Combat_SubmitProjectile(int ownerAgentId, CombatElement elem,
                             Vector3 pos, float radius, float damage,
                             float knockback, int skillInstanceId);

// End of Combat_Update(dt): resolves via CheckCollisionSpheres + the 5x5 Clash Matrix (const CPU table)
void Combat_Update(float dt);

// Skill polls results to despawn its projectile / spawn clash VFX:
typedef struct {
    int skillInstanceId;      // own projectile
    ClashOutcome outcome;
    Vector3 clashPoint;
    CombatElement otherElem;  // to pick the counter-element VFX color
} ClashEvent;
int  Combat_PollEvents(ClashEvent *out, int max);   // drains the queue each frame

// Skill↔Agent: combat itself calls Entity_ApplyDamage when a projectile hits an enemy agent.
// The 5x5 matrix is a const table: Water beats Fire, Fire beats Metal, Metal beats Wood, Wood beats Earth, Earth beats Water.
```

Convention: skills no longer call `Entity_ApplyDamage` directly for projectiles — they submit
through the registry so hit detection + Đấu Pháp + team logic live in one place. AoE/melee skills
still use `Entity_ApplyAoEDamage` directly.

**Earth balance (design §XI):** `castTime`/`projectileSpeed` are skill parameters, not combat's —
combat only resolves; the Earth-in-Forest penalty reads `Map_QueryZoneAt(pos)` inside
`Combat_Update`.

**DoD:** autotest firing 2 Water/Fire projectiles head-on → Water wins, event correct; same-team
projectiles pass through each other; 128 simultaneous projectiles with no frame drop.

#### Module 4 — Player Controller (`control/`, NEW)
**Goal:** formalize input (currently living temporarily in sandbox): movement, movement tech
(khinh công), meditation, casting 4 skills. Pre-built abstraction for touch (Phase 1).

```c
// Separate INPUT (device) from INTENT (gameplay) — later touch/gamepad/net just swap the input layer.
typedef struct {
    Vector2 moveDir;        // already normalized for the isometric camera
    bool    jump, dash, meditate;
    int     castSkillSlot;  // -1 = no cast; 0..3 = equippedSkills slot
    Vector3 aimPoint;       // aim point on the ground (mouse ray / later auto-target)
} PlayerIntent;

void Control_Init(int agentId);                 // attaches a controller to one agent in the pool
PlayerIntent Control_ReadIntent(void);          // reads device → intent
void Control_Apply(const PlayerIntent *in, float dt);
// Apply: movement (reads speedMult modifiers — a wiring gap in ENTITIES §8 at the time),
// Entity_Jump/Entity_Dash (implements REAL dash here: velocity burst + calls Entity_OnDash),
// Entity_StartMeditate, cast through skill_manager (checks Entity_SpendMana first).
```

Also a small entities-side task: implement the real `Entity_Dash` body (was a stub) + wire
`speedMult`.

**DoD:** playable in the real game: WASD movement, space jump, dash with cooldown + a grapple hook,
meditation restores mana, casting spends mana; sandbox switches to using `control/` (removes the
duplicate PlayerEntity).

#### Module 5 — Boss (Đại Tinh Linh) (`boss/`, NEW)
**Goal:** the Hắc Diện Tôn Giả boss for Phase 0 (design §V.2). Boss = one `ARCH_BOSS` agent in the
agentPool (logic) + a visual metaball/emitter layer (render) + a phase state machine.

**Split into two halves so AI can author new bosses easily:**
```c
// ---- DATA half (one file per boss: boss/<boss_name>_def.c — AI authors new bosses here) ----
typedef struct {
    const char   *name;
    float         maxHealth;
    CombatElement phaseElements[4];  // element changes per phase (Hắc Diện shifts elements)
    float         phaseHpThresholds[4]; // % HP to advance phase, [0]=1.0
    int           skillPerPhase[4];  // skill id the boss uses each phase
    void        (*drawVisual)(const Agent *self, float phaseT); // metaball/FBM/emitter — only this half includes core VFX .h
} BossDef;

// ---- ENGINE half (boss/boss_system.c — written once, not touched when adding a boss) ----
int  Boss_Spawn(const BossDef *def, Vector3 pos, AgentTeam team); // returns agentId
void Boss_Update(float dt);   // state machine: picks a target (Entity_GetNearbyTargetsTeam),
                              // casts per-phase skill, changes phase by % HP,
                              // <30% HP → requests Thái Cực (Module 6)
void Boss_Draw(void);         // calls def->drawVisual
```

Rule: `boss_system.c` does NOT include VFX headers — only `_def.c` files may (a deliberate carve-out
of §1.4, since boss visuals are 100% VFX by design).

**AI authoring:** the design's §V.2 lists 10 bosses = 10 `_def.c` files (Vatu's breathing sinf(),
Zephyrus's spiral, Tiamat's refracting jelly...). Phase 0 needs only 1 boss (Hắc Diện Tôn Giả);
v1.0's target is 3.

**DoD:** the boss can be beaten/lost to in game mode; the boss changes element per phase (pattern
groove color = visual cue); the boss fires through the combat registry; the boss dies correctly
when knocked off the ring.

#### Module 6 — Thái Cực State (Phong / Lôi)
**Goal:** the Thái Cực state (Pillar 3). Spread across 3 existing modules — NO new directory:

| Part | Owner | Content |
|---|---|---|
| Trigger + state | `entities/` | `bool taijiActive` on Agent; condition: 2 Yin + 2 Yang built into `equippedSkills` (checked in `Entity_RecomputeElement`) or boss <30% HP (called from `Boss_Update`). Immunity to elemental counters: combat reads this flag when looking up the Clash Matrix |
| Monochrome shader | `core/post_fx` | `PostFX_SetMonochrome(float intensity)` — applied to the top-level Canvas, no new render target |
| 2 ultimate skills | `skills/taiji/` | **PHONG** (Wind): pulling force field (`core/force_field` already exists) gathers agents + projectiles (combat needs `Combat_DeflectProjectilesInRadius`); **LÔI** (Thunder): purple/white lightning using `core/vfx_proc_ray` (already exists, see memory Thunder Orb) striking down at Phong's center |

Downside ("Vô Sát", risk per §XVII): Lôi drains mana extremely fast — enforced via
`Entity_SpendMana`.

**DoD:** entering Thái Cực → full screen goes black-and-white, the 4 normal skills lock, Phong
pulls in both minions and projectiles (via the registry), Lôi hits an area; running out of mana
exits the state.

#### Module 7 — Game Mode (`game/`, NEW)
**Goal:** the full Phase 0 match loop (design §X) — the one "conductor" module allowed to include
every `.h`. `main.c` shrinks to init/loop/unload; sandbox stays as the dev harness (mode switch via
flag/hotkey).

```c
typedef enum { GAME_MENU, GAME_ARENA_INTRO, GAME_FIGHTING, GAME_VICTORY, GAME_DEFEAT } GameState;

void Game_Init(void);      // loads map, spawns player (control), spawns boss (BossDef), sets up night env
void Game_Update(float dt);// canonical tick order: Control → Entities → Boss/AI → Combat → Formations → Skills(VFX) → Env
void Game_Draw(void);      // one top-level canvas: map → entities/boss → skill VFX → post fx
void Game_Unload(void);
```

Also where the **zone modifier rule table** (Module 2) lives — gameplay rules centralized in one
place.

**DoD = Phase 0 Definition of Done (design §X):** smooth controls, win/lose vs. the boss, correct
ring-out, Thái Cực works, ≥60FPS on Android.

#### Module 8 — Minion Pool + AI (`ai/`, NEW) — Phase 1
Minions are `ARCH_MINION` agents in the shared agentPool (no separate pool — per memory
MAX_AGENTS=256 mixes hero+minion+boss). The `ai/` module holds only the **brain**:
```c
void AI_Init(void);
void AI_Update(float dt);  // walks agentPool: ARCH_MINION → steers doggedly toward the enemy boss,
                           // self-detonates on approach (Entity_ApplyAoEDamage + an event for VFX);
                           // later adds a brain for ARCH_HERO (4 enemy AIs)
int  AI_SpawnMinionWave(int bossAgentId, int count); // boss spawns minions around itself
```
No VFX include — explosion/movement fire events (pattern: `Entity_OnDash`) for skills to draw.

**DoD:** bosses on both teams spawn minions, minions steer correctly, self-detonate for correct-team
damage, 40+ minions with no frame drop.

#### Module 9 — HUD + Auto-Targeting (`ui/`, NEW) — Phase 1
```c
void UI_Init(void); void UI_Update(float dt); void UI_DrawOverlay(void); // 2D pass after the top-level canvas
// Auto-target (design §XI): priority 1 = an incoming enemy projectile (read from the Combat registry) for counter-strikes;
// priority 2 = the enemy boss. Result written to PlayerIntent.aimPoint (consumed by control/).
Vector3 UI_GetAutoAimPoint(int agentId, bool *hasTarget);
```
Minimal per the No Tutorial philosophy: only HP/Mana bars + mobile virtual buttons, no instructional
text.

#### Module 10 — Formations (`formations/`, NEW) — Phase 2
```c
typedef struct {
    const char *name;
    CombatElement elem;
    float radius, duration, manaCost;
    NatureZoneType resonantZone;  // overlapping this zone → stronger (Lôi Động Trận + River)
    void (*onTick)(Vector3 center, float dt);   // logic: buff/debuff/stun via Entity_* API
    void (*drawGround)(Vector3 center, float t);// decal_system + emitter — only the data file includes VFX
} FormationDef;   // AI authors a new formation = 1 def file, same pattern as BossDef

#define MAX_FORMATIONS 4   // formationPool[4] per design §VI
int  Formation_Deploy(const FormationDef *def, Vector3 center, int ownerAgentId);
void Formation_Update(float dt);  // ticks modifiers on agents within radius (Layer 4)
```
The design's 5 formations (Cửu Thiên Lôi Động, Hàn Băng Thủy Tuyệt, ...) = 5 def files.

#### Module 11 — Networking (`net/`, NEW) — Phase 1→2
ENet on a side thread, peer-hosted, host resolves all combat (design §XI). Started only once
offline Game Mode was stable. Principle: `net/` serializes **PlayerIntent** (Module 4 already
split out intent for exactly this) and sends it to the host; the host runs the simulation and sends
back an agentPool snapshot. No other module is aware of networking. *(Later fully landed — see
"Multi-peer transport" and "EOS Online Backend" in Part 2.)*

### 4. Agent ownership table updates (recorded when each module landed)

| New agent | Owns | Extra read |
|---|---|---|
| **Combat Agent** | `combat/` | `entities/entities.h`, `core/map_manager.h` |
| **Control Agent** | `control/` | `entities/entities.h`, `core/skill_manager.h`, `combat/combat.h` |
| **Boss Agent** | `boss/` | `entities/entities.h`, `combat/combat.h`, core VFX `.h` (only inside `_def.c`) |
| **Game Agent** | `game/` | every public `.h` |
| **AI Agent** | `ai/` | `entities/entities.h`, `combat/combat.h` |
| **UI Agent** | `ui/` | `entities/entities.h`, `combat/combat.h`, `control/control.h` |
| **Formations Agent** | `formations/` | `entities/entities.h`, `core/decals/decal_system.h`, `core/map_manager.h` |

(These entries have since been folded into root `CLAUDE.md`'s Module Agents table.)

### 5. Standard module bring-up checklist (used at the time)

1. Read the corresponding section in this file + the relevant part of `nguhanhtyvo_kehoach.md`.
2. Write a complete `<MODULE>_API.md` (pattern: `entities/docs/API.md` — includes an "Explicitly
   NOT in this version" section).
3. Write the module's `CLAUDE.md` (scope, forbidden dirs, token-efficiency rules).
4. Implement `.h` first, then `.c`; static arrays; build clean with `make`.
5. Add autotest coverage in `sandbox/auto_test` for the module's DoD.
6. Update the agent table in root `CLAUDE.md` + mark this file's entry done.

---

## Part 2 — Post-Net/EOS priority plan (from `KE_HOACH_TIEP_THEO.md`, 12/07/2026)

> Reference sources: `nguhanhtyvo_kehoach.md` §IX (phase strategy) + §XVIII (v1.0 goals), Part 1
> above (Modules 1–11 ✅), `net/docs/API.md` §3b/3c. This section records **the priority order for work
> rounds that followed** — each round was meant to end with a clean build + passing autotest +
> updated API docs (no mid-round doc updates).
>
> **Status update (this consolidation pass):** per later session memory, Đợt A (below) is fully
> landed — multi-peer transport, lobby, team battle, hero-bot + handicap, cast mirroring, and
> remote-hero real-model rendering all shipped and verified online (16/16 autotest). EOS internet
> PvP (Device ID auth, 5-character lobby codes, NAT traversal) also landed and was verified against
> the real Epic backend. Đợt B/C/D below are recorded as they were planned; check current code /
> other module `PROGRESS.md` files for what has since landed versus what remains backlog.

### 0. State at the time — already done, do NOT redo

| Area | Status |
|---|---|
| Gameplay core | Modules 1–10: entities v2, map zones, combat clash, control intent, boss Hắc Diện, Thái Cực, game mode, minion AI, HUD auto-target, 2 formations — autotest 14/14 |
| Real match | VERDANT_PATH island, ring-out, minion wave tied to boss phase, Loadout UI (TAB) |
| Net LAN | ENet peer-hosted (`--host` / `--join <ip>`), protocol v2 (melee + match-state sync) |
| Net INTERNET | **Complete EOS backend**: Device ID auth, 5-character lobby codes, NAT-traversing P2P, fragmentation; TAO PHONG/NHAP MA (host/join) UI menu + HUD room code; verified end-to-end against real Epic |
| Assets pending | `ANIMATION_ASSETS_PLAN.md` — user preparing GLBs (player dash/jump/hit/die, minion, boss) |

**Gap vs. the v1.0 goal (design §XVIII):**

| v1.0 goal | Had at the time | Missing |
|---|---|---|
| Two-side PvP (vision: up to **4v4**, lobby, bot fill-in) | Invasion duel 1v1 (guest = TEAM_ENEMY fighting alongside the boss match), 1-peer transport | Multi-peer (8 players), lobby, team battle 1v1→4v4, hero-bot + handicap buff |
| 20 skills / 5 elements | ~11 skills + 2 Thái Cực | ~7–9 new skills, full range-tier coverage per element |
| ≥3 Đại Tinh Linh bosses | 1 (Hắc Diện Tôn Giả) | +2 BossDef (engine already data-driven) |
| 5 Formations | 2 | +3 FormationDef |
| Campaign minion-wave survival | Minion wave as boss-match support | Standalone mode: rising waves → capped by a boss |
| 3 terrain maps with zones | VERDANT_PATH (+ sandbox map) | Moonlit desert map (heightmap) + 1 more, full River/Forest/Desert set |
| Android + PC ≥60FPS | PC runs well | Rebuild for Android, FPS profiling, touch controls, EOS on Android |

### Đợt A — Online team battle 1v1 → 4v4 (highest priority) — LANDED 13/07/2026

**VISION (locked 13/07/2026):** the biggest match is **4v4**. Opening a room enters a **lobby**;
play can start before it's full (1v1/2v2/3v3/4v4); an uneven team is padded with **virtual players
(hero-bot AI)** + a **handicap buff** for the smaller side. The current "invade the boss match"
mode stays as a dev/test path, not a shippable mode.

Entirely within `net/` + `game/` + `ai/` (hero-bot) + `ui/` (lobby), no engine VFX touched.
Mandatory order: A1 → A2 → A3 → A4 → A5 (each step: clean build + autotest before the next).

**A1. Multi-peer transport — foundation for everything** *(L — 1-2 sessions)* ✅ 13/07
- Transport was 1-peer only (`enet_host_create(..., 1 peer)`, EOS `MaxLobbyMembers = 2`, a single
  `s_remotePuid`). Raised to **up to 7 guests + host = 8**:
  - `net_transport.c`: a static peer array `NetPeer[NET_MAX_PLAYERS-1]` (ENet peer or EOS PUID +
    state + hero agentId per player). Host receives intent **per peer**, broadcasts snapshots to
    every peer.
  - Protocol **v3**: `NET_CTRL_HELLO` carries `slot/team`; added `NET_CTRL_ROSTER` (room player
    list — resent on any change).
  - EOS: `MaxLobbyMembers = 8`; backend seam unchanged, `EosSend` gained a destination param
    (broadcast = loop over the peer list).
- **DoD:** 3 instances (1 host + 2 guests, using `WUXING_EOS_FRESH_DEVICE`) all join a room, both
  guests control their own hero, snapshot stays in sync across all 3 screens; autotest for the v3
  wire format (roster round-trip).

**A2. Lobby screen** *(M–L — 1-2 sessions)* ✅ 13/07 (with 1Hz heartbeat + 8s host timeout — EOS
doesn't self-report dead peers)
- New screen between menu and match: **8 slots split into 2 team columns** (Thanh Long / Bạch Hổ...).
  Joining players auto-balance into slots; host can drag/swap teams. Shows the room code large +
  player name list (Device ID → "P1..P8" placeholder names first, display names later).
- Empty slots show "BOT" (toggled per-slot by the host) — bot configuration decided AT the lobby,
  not when entering the match.
- Host presses **START** → sends `NET_CTRL_START` with the locked roster; every client transitions
  into the match simultaneously. Room closes once the match starts (late-join = Phase 3).
- **DoD:** 3 real players see each other in the lobby + swap teams; host starting puts all 3 into
  the correct formation; a guest leaving the lobby reopens their slot.

**A3. Team battle mode 1v1 → 4v4** *(M — 1 session)* ✅ 13/07 (verified online: full win-loss-rematch
cycle over Epic)
- `game/`: `GAME_MODE_TEAM_BATTLE` — spawns 2 opposing teams per the roster (2 spawn clusters on
  VERDANT_PATH), NO boss/minion wave.
- Win rule: **team elimination** — the team that runs out of heroes (dead/ring-out) loses; dead
  teammates do NOT respawn within the match (short match, arena pacing). ENTER = whole-room rematch
  (host resets + `NET_CTRL_STATE`).
- HUD: mini per-team ally/enemy HP bars, remaining-hero count per team.
- **DoD:** offline elimination-rule autotest (simulated 2v2, kill heroes one by one → correct team
  wins); a full 1v1 online match through win-loss-rematch.

**A4. Hero-bot AI + handicap buff** *(L — 1-2 sessions)* ✅ 13/07 (verified online 1v2-bot: +1
handicap correct across both rounds; bot correctly downs an idle host)
- **Hero-bot** (`ai/` extended — a SEPARATE brain, not the minion brain): controls ARCH_HERO agents
  on the understaffed team. Minimum playable behavior: holds range per its equipped skill kit,
  casts on cooldown+mana, dodges away from the ring edge, prioritizes low-HP/nearest targets,
  dash-dodges incoming projectiles (reads the combat snapshot — reuses the auto-target
  projectile-catching logic that already exists). Bots run ENTIRELY on the host — to a client, a
  bot looks exactly like a real player (same snapshot path), zero net-layer additions.
- **Handicap buff** (a static lookup table in `game/game_rules.c` — the single place holding this
  rule): the team with fewer than N players gets a tiered buff, e.g. baseline `+15% damage, +15%
  max HP, +25% mana regen` per missing head — tuned via sandbox tunables (`RegisterSkillTunables`
  pattern).
- Bots also count as "a person" when computing the shortfall (a bot replacing a person does NOT
  grant a buff — the buff is only for a team that agrees to play understaffed).
- **DoD:** autotest — 1v2 with a bot: bot can cast skills + doesn't ring itself out; 1v2 without a
  bot: the 1-player side gets the correct table buff; 2v2 full teams: no buff.

**A5. Remaining transport quality + sync gaps** *(M — 1 session)* ✅ 13/07 (autotest 16/16 + online
verification: players render the correct model, multiple heroes render without clobbering). Also
fixed environment issues: self-join guard (same machine forgetting FRESH_DEVICE), ForceRelays
default (NAT-proof for 2 real machines), NAT/relay diagnostic logging

> **ĐỢT A COMPLETE 13/07/2026** — online 1v1→4v4 PvP is really playable: lobby, team battle
> elimination, hero-bot + handicap, cast mirroring + interpolation, opponents rendered with real
> models. Autotest 16/16.
- **VFX event mirroring:** host broadcasts `NET_EVT_CAST {agentId, skillIndex, aimPoint}` (reliable)
  when `CastSkill` succeeds; clients cast "visual-only" (a client doesn't tick `Combat_Update`, so
  the projectile is VFX-only — damage still comes from the snapshot). Includes minion explosions +
  boss phase (for the invasion/campaign modes).
- **Snapshot interpolation:** client keeps the 2 most-recent snapshots, lerps over a ~100ms buffer
  (no prediction).
- **Loadout sync:** a client changing loadout via TAB → sends the 4 slots to the host →
  `Entity_SetEquippedSkill` + `RecomputeElement` (Vô Hệ correct for every player).
- **Zone rule for remote heroes:** `HostApplyRemoteEdges` applies zone cooldown multipliers.
- **DoD:** a real 2v2 match on two machines: each side sees the other's skills fly in the right
  direction, smooth motion, changing loadout in lobby/match reflects the correct element.

### Đợt B — Content (can run in parallel with A, separate agent)

**B1. Add 2 more Đại Tinh Linh bosses** *(M — 1 session/boss)*
- 2 new `boss/*_def.c` files following `hac_dien_ton_gia_def.c`'s pattern (data-driven BossDef,
  skills referenced by NAME). Suggested per the design: 1 crowd-control-leaning boss (Earth/Metal),
  1 fast/mobile boss (Fire/Wind) — metaball shader groove pattern swaps color to indicate element.
- **DoD:** boss selectable at match start (or random); autotest for the new boss's phase-shift.

**B2. Complete the 5 Formations** *(M — 1 session)*
- +3 `formations/*_def.c` (Cửu Thiên Lôi Động, etc., per design §VI's list), reusing the existing
  zone resonance. VFX via existing compose helpers (`vfx_proc_ray`, ribbon).
- **DoD:** 5/5 formations deployable, extended formation autotest.

**B3. Cover 20 skills / 5 elements** *(L — 2-3 sessions, split by element)*
- Audit against `skills/docs/RECIPE.md`: each element needs 4 range-tiered skills (close/mid/long/
  special). New skills MUST: be meter-scaled from the start, submit colliders to the combat
  registry, use `_params.inl` + `_tunables.inl`, be wired into `sandbox/vfx_test.c`'s NEW FX tab.
- Includes the Earth-element balance pass (design §XI): projectiles ×0.5 speed vs. Fire, 1.5s
  stationary cast — verify this is actually applied in `combat/`, add it if not.
- **DoD:** the 5-element × 4-skill table is fully filled; autotest counts the correct number of
  registered skills in the combat registry.

**B4. Campaign survival mode** *(M — 1 session)*
- `game/`: `GAME_MODE_CAMPAIGN` — rising minion waves (reuses the existing `ai/` pool), capped by a
  final boss fight; death = game over, beating the boss = level clear.
- **DoD:** playable from the menu ("6. CAMPAIGN"), headless autotest for the rising-wave curve.

**B5. New map(s)** *(M — 1 session/map)*
- Moonlit desert map (static heightmap + `NAT_DESERT_ZONE`) — named explicitly in the v1.0 goal.
- Every map needs: a clear visual zone cue (No Tutorial), a declared `MapZone[]`, its own arena
  bounds (per-map `Entity_SetArenaBounds` already exists).
- **DoD:** K-cycling to the new map doesn't leak bounds; zone modifiers match the rule table.

### Đợt C — Mobile / Android (after A is done; B may still be in progress)

**C1. Rebuild for Android + profiling** *(M — 1 session, needs the user on a real device)*
- Apply GLES Rules A–E (memory `project_android_shader_pipeline`). User measures FPS on a real
  device — target ≥60FPS, main suspects: GPU particle + post FX.
- **DoD:** APK runs, real measured FPS logged for tuning.

**C2. Touch controls** *(M–L — 1-2 sessions)*
- Left virtual joystick (move) + right-side buttons (skills 1-4, melee, dash/jump); auto-target
  already handles aim (per the Mobile Auto-Targeting design §XI).
- `control/` reads touch → same `PlayerIntent` (wire format unchanged).
- **DoD:** a full boss match playable by touch alone, no mouse/keyboard.

**C3. EOS on Android** *(S–M — 1 session)*
- SDK already has `Bin/Android`; the Android CMake branch links the `.so` + JNI init (EOS Android
  needs `EOS_Android_InitializeOptions` — see `Android/eos_android.h`).
- **DoD:** PC + Android join the same room via a room code.

### Đợt D — Feel & polish (runs in the background, interleaved between rounds)

| # | Item | Size | Note |
|---|---|---|---|
| D1 | Wire up animation GLBs once the user delivers them | S/asset | Per `ANIMATION_ASSETS_PLAN.md`; anim slots already reserved in `character/` |
| D2 | Boss pattern variety | S–M | Hắc Diện currently only drifts/strafes simply — add telegraphs + phase combos |
| D3 | Audio (OUTSIDE the design doc — proposed) | M | raylib audio; minimum: cast, hit, Đấu Pháp clash, Thái Cực sting, night ambience. Needs user approval before starting |
| D4 | Balance tuning pass | M | Playtest: Earth penalty, Thái Cực "Vô Sát" (design §XVII), mana economy — use the existing sandbox tunables |
| D5 | Net side-thread + reconnection | M | Design §XI (multi-threaded ENet) — deferrable, main-thread polling isn't a bottleneck yet |

### Out of current scope (Phase 3 — not started)

Ranking server, cosmetics, seasonal events, mid-match late-join, Firebase.
Host migration: per the locked design, "a match self-ends when the host leaves" applies as-is to
team battle too (host leaves lobby = room disbanded, host leaves match = match ends, no host
handoff).

### Suggested order & process (as planned at the time)

```
A1 (multi-peer) → A2 (lobby) → A3 (team battle) → A4 (bot+buff) → A5 (smoothing+sync)
                                  ↘ B1, B2, B3 (content agent runs in parallel from after A3)
after A5:  C1 → C2 → C3 (Android)  →  B4 → B5...
D1 slots in whenever assets arrive; D4 after each content round (A4's buff table needs D4 early).
```

Process per item (kept from the prior convention):
1. Read the relevant design section + the API doc of the module being touched.
2. Update `.h` + the API doc BEFORE changing the API (breaking change).
3. Static arrays, C99, meter-scale, no malloc; new VFX → wire into vfx_test.
4. Add/extend autotest for the DoD; clean build; **14/14+ passing** before moving to the next item.
5. Update docs ONCE at the end of the round (not mid-round).

---

## Part 3 — Đợt E: Elden-Ring-tier VFX (planned 22/07/2026)

Core-owned engine work. Full spec lives in **`core/docs/ELDEN_VFX_SPEC.md`** —
per-task implementation detail (API, files, wiring points, DoD, landmines).

**Part A — purge + fundamentals (first).** Owner's assessment 22/07: most VFX are
not good enough, only a few keepers, and the skill layer is disposable. Root cause
of the weak fundamentals (spec §0.1b, from direct source reading): every smoke /
fire / aura effect is **one surface + FBM noise**; particles are wholly unlit
(`particles.fs` is 14 lines, no light term); additive outnumbers alpha 42:23 so
smoke can never be dark; and noise is used as silhouette when it can only be
detail. `F0` triage/purge → `F1` lit particles + blend law (highest-value task in
the plan) → `F2/F3/F4` smoke, fire, character aura rebuilt on it.

**Part B — polish on top.** `E0` baseline, `E1` radial blur + anamorphic streak
bloom, `E2` VFX light bleed onto characters (`surface_lit.fs` verified not to read
the `vfx_light` list), `E3` `VFX_Sequence` choreography layer, `E5/E6` new
compositions, `E7` retrofit **stop-gate**, `E8` rlvk/Mali budget. `E4` (authored
flipbook library) runs in parallel from F1 — priority raised, since it now gates
F2/F3.

No renderer rewrite required. Cross-module: E2 coordinates with
Character/Environment, F0's skill purge and E7 are Skills, E8 is Renderer.

---

## Patch Log

| Date | Editor | Section edited | Based on which source | Tier |
|---|---|---|---|---|
| 2026-07-20 | Claude | Initial consolidation | `MODULES_ROADMAP.md`, `KE_HOACH_TIEP_THEO.md` (read directly, translated) | Ground-truth (translation), status notes cross-checked against session memory |
| 2026-07-22 | Claude | Added Part 3 — Đợt E (Elden-Ring-tier VFX) | Direct audit of `core/` this session: `post_fx.h`, `particle_system.h`, `visual_composer.h` (79 components enumerated), `vfx_light.h`, `sprite_anim.h`, `ribbon_strip.h`, `core/shaders/` and `assets/textures/` listings | Ground-truth for the "what exists" audit; the phase plan, effort sizes and the Elden Ring comparison are **inferred/proposed** — not yet validated by implementation |
