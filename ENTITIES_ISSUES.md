# Entities Issues / Backlog

Tasks for Entities Agent — assign when ready.

## 1. No `Entity_GetNearbyTargets()` — blocks multi-target skills, traps, and AoE zones

Came up from 3 separate directions in the same review pass, all needing the identical capability: "which agents are inside this circle/radius right now":

- **Multi-target skills**: `Update[Name]Skill(float dt, Vector3 enemyPos, float enemyRadius)` only supports 1 hardcoded target. A 4vs4 match needs a skill to check collision against any of `agentPool[8]`, not one passed-in position. (Also logged informally in conversation — not yet a CORE_ISSUES item since it requires a breaking change to the Skill lifecycle signature; that's a separate, bigger decision.)
- **Cạm bẫy (Trap) skills**: invisible trigger zones that need to detect when *any* agent steps inside, to fire an effect.
- **Tầm trung (AoE/Control) skills**: persistent area effects (Địa chấn, Cuồng phong) need to tick damage/knockback against every agent currently inside their radius, not just one.

This is also architecturally identical to what Map's planned "Virtual Trigger Zone" feature needs (`NAT_RIVER`/`FOREST`/`DESERT` per `nguhanhtyvo_kehoach.md` §III) — a circle-overlap check against active agents. Don't build this separately in Skills and Maps; it belongs once in `entities/` since it queries `agentPool` directly.

**RESOLVED 2026-06-30** — implemented in `entities/entities.c` / `entities/entities.h`, documented in `ENTITIES_API.md` §7.

~~### Proposed API~~

```c
// Returns the number of active agents found within radius of center, writing
// their indices into outIds (caller-supplied buffer, size maxIds).
int Entity_GetNearbyTargets(Vector3 center, float radius, int *outIds, int maxIds);
```

~~- Pure read query — no side effects, no damage application (that's still `Entity_ApplyDamage`, called separately by whoever uses this).~~
~~- Should support an optional team filter later (ally vs enemy) once `Agent` gains a team/side field — not in `ENTITIES_API.md` yet, don't add team filtering in this pass, just the basic radius query. Note this as a known near-term follow-up, not in scope now.~~
~~- XZ-plane distance check (ignore Y), consistent with how ring-out (`Entity_CheckRingOut`) and arena radius checks already work per `ENTITIES_API.md` §5.~~

### Not in scope for this pass

- ~~Changing the `Update[Name]Skill` signature to use this (that's a Skills/Core cross-module breaking-change decision, track separately if/when it's decided)~~ **Superseded 2026-06-30 by item #3** — turns out no signature change is needed at all. Skills call the new `Entity_ApplyAoEDamage`/`Entity_ApplyAoEBuff` themselves at the moment of impact/tick, querying targets via `Entity_GetNearbyTargets` internally. The "multi-target" problem was actually a "missing AoE entry point" problem, not a lifecycle-signature problem.
- Team/faction filtering
- Map Virtual Trigger Zone integration (Map module doesn't expose zone types yet — this function just needs to exist so Map can consume it later without Entities having to change)

## 3. Unify damage/buff application around `Entity_GetNearbyTargets` — single radius-based entry point for all skill categories

Design direction (confirmed 2026-06-30): every skill effect — regardless of category (Tầm xa, Tầm trung, Cận chiến, Bùa chú, Cạm bẫy, Buff) — should resolve through one radius-based application, differing only in radius size and call frequency (once at impact vs. ticking every frame). No more bespoke per-category damage wiring.

This also resolves item #1's previously-open "multi-target requires a Skill lifecycle signature change" concern — it doesn't. A skill determines its own effect center (impact point, self position, anchor point) and radius, then calls the new entry points itself; it doesn't need the engine to feed it multiple targets.

It also resolves the long-standing ambiguity between `core/skill_manager.h`'s `ApplyAoEDamage()` (raw math, no HP bookkeeping) and `entities/`'s `Entity_ApplyDamage()` (the real HP-owning call) — `Entity_ApplyAoEDamage` becomes the one entry point skills should call for all radius-based damage; `core/skill_manager.h`'s `ApplyAoEDamage()` is superseded for agent-targeted damage (it may still exist for non-agent uses, e.g. destructible scenery later, but skills targeting `agentPool` should stop calling it directly).

### Proposed API

```c
void Entity_ApplyAoEDamage(Vector3 center, float radius, float damage, float knockbackStrength);
void Entity_ApplyAoEBuff(Vector3 center, float radius, float speedMult, float duration);
```

- Both internally call `Entity_GetNearbyTargets(center, radius, ...)` then loop: `Entity_ApplyAoEDamage` calls `Entity_ApplyDamage` per found agent (knockback direction computed per-agent, away from `center`, scaled by `knockbackStrength`); `Entity_ApplyAoEBuff` calls `Entity_AddModifier` per found agent.
- Small radius = effectively single-target (projectile impact, melee swing). Large radius = true AoE (Tầm trung skills, buff aura covering allies). Same call, just a different number.
- `Entity_ApplyAoEBuff` affects ALL agents in radius — no ally/enemy filtering yet (same limitation as `Entity_GetNearbyTargets` item #1: no team field on `Agent` yet). Document this clearly: until team filtering exists, a buff radius will buff enemies too if they're standing inside it. Acceptable for this minimal version, but flag it loudly in `ENTITIES_API.md` so Skills Agent doesn't get surprised.
- Don't remove `core/skill_manager.h`'s `ApplyAoEDamage()` — that's Core Agent's file, out of scope here. Just stop being the recommended path for agent damage; note the supersession in `ENTITIES_API.md`, and separately flag a `CORE_API.md` doc update for Core Agent to point skill authors at `Entity_ApplyAoEDamage` instead (don't make that edit yourself — different module).

~~Action: implement both functions in `entities/entities.c`/`.h`, document in `ENTITIES_API.md` (new section, reference §4/§7/§8 since this composes existing `Entity_ApplyDamage`/`Entity_GetNearbyTargets`/`Entity_AddModifier`).~~

**RESOLVED 2026-06-30** — `Entity_ApplyAoEDamage` and `Entity_ApplyAoEBuff` implemented in `entities/entities.c`/`entities/entities.h`, both composing `Entity_GetNearbyTargets` internally (no duplicated radius-query logic). Documented in `ENTITIES_API.md` §9, with §4/§7/§8 cross-references. `core/skill_manager.h`'s `ApplyAoEDamage()` noted as superseded for agent-targeted damage (no edit made to that file — Core Agent's scope). **No team/ally-enemy filtering** — `Entity_ApplyAoEBuff` currently buffs every agent in radius, friend or foe; flagged prominently in the API doc.

## 2. No "Entity-Attached" skill pattern (Buff, future khinh công afterimage)

**Correction (2026-06-30): this item previously also listed Melee — that was wrong.** Melee/Cận chiến is NOT an attachment-pattern category at all; range tier (Tầm xa/Trung/Cận) is an independent axis from cast shape (point vs path) and from technical pattern (Flying vs Anchored). A melee skill can be a simple Anchored-point or Anchored-path skill with a small max-range value — see `CORE_ISSUES.md`'s "Anchored-along-path" skeleton item, which is general-purpose and not range-specific. Melee does not need this Entity-Attached pattern. Removed from this item's scope.

What's left genuinely needs attachment to a *moving* entity (not a fixed point or path in world space): a Buff aura that must visually follow the buffed character as they move, and (later) the khinh công dash afterimage hook already stubbed in `ENTITIES_API.md` §6 (`Entity_OnDash`).

`core/trail_system.h`'s `TRAIL_TYPE_FOLLOWER` (with `SetFollowerAxis`/`UpdateFollowerPosition`, called every frame) is already built for exactly this attachment pattern — it's just never been used as the basis for a documented skill skeleton. Note it may be overkill for a simple buff aura — re-spawning a particle/light at `Agent.position` every frame might suffice; only reach for `TRAIL_TYPE_FOLLOWER` if the visual genuinely needs ribbon geometry (e.g. a flowing aura ribbon, not just a glow).

### Action (cross-module — coordinate with Core Agent)

The underlying need — "Buff = a duration-based modifier living on the `Agent` struct" — touches `entities/`: `ENTITIES_API.md`'s `Agent` struct currently has no generic "active modifiers" field (only specific ones like `dashCooldown`, `isStealthed`). A Buff skill needs *some* place to register "Agent X gets +50% speed for 5s" that `Entity_Update` reads every frame.

1. ~~**Entities side**: add a minimal modifier slot to `Agent` (e.g. a small fixed-size array of `{float speedMult; float duration;}` or similar — keep minimal, don't over-design a full buff/debuff stacking system yet) so a skill has somewhere to write a buff to.~~ **RESOLVED 2026-06-30** — `AgentModifier modifiers[MAX_AGENT_MODIFIERS]` added to `Agent`, ticked down in `Entity_Update`, setter `Entity_AddModifier()` implemented. Documented in `ENTITIES_API.md` §8. `speedMult` is data-only — not wired into movement (no movement system exists yet).
2. ~~**Core side**: document an "Entity-Attached" skeleton in `CORE_API.md` §4 — visual stays attached to a moving `Agent` position every frame, writes to the new Entity modifier slot for the buff's duration.~~ **RESOLVED 2026-06-30** — added a fourth `CASTING → ATTACHED → DISSOLVE` skeleton to `CORE_API.md` §4, after the Anchored-Along-Path skeleton. Caches `casterPos` at cast time (no new lifecycle parameter, `Update[Name]Skill` signature untouched), calls `Entity_ApplyAoEBuff(casterPos, smallRadius, speedMult, duration)` at cast, re-spawns a `VFXLight` pulse at `casterPos` on an interval while `ATTACHED`, notes `TRAIL_TYPE_FOLLOWER` as the ribbon-geometry alternative, and `DISSOLVE`s on the skill's own local timer (no querying `entities/` for live modifier state). **Flag**: `skills/CLAUDE.md`'s allowed read list does not currently include `entities/entities.h` — needed to call `Entity_ApplyAoEBuff`. Not fixed here (not Core Agent's file).

Low priority — this is the least urgent of the open skeleton gaps since no Buff skill exists yet (current 6 skills are all Flying or Anchored).

---
*Logged from skill-pipeline review, 2026-06-30.*
