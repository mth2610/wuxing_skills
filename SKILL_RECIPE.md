# SKILL_RECIPE — One-prompt skill creation guide

Single reference for creating a new Wuxing skill from scratch in one session.
See `CORE_API.md` for full API details; this document focuses on **decisions and sequence**.

---

## 1. Pick your archetype

| Archetype | Use when | Macro needed |
|---|---|---|
| `projectile` | Skill fires something that flies (fireball, orb, bolt) | — (all 3 lifecycle fns real) |
| `ground` | Effect rises/erupts at a target point (pillars, vines) | `SKILL_EMPTY_PROJECTILE_API(Name)` |
| `path` | Effect anchored along a path between caster and target | `SKILL_EMPTY_PROJECTILE_API(Name)` |
| `attached` | Effect attached to an entity (aura, buff, trail) | `SKILL_EMPTY_PROJECTILE_API(Name)` |

Projectile archetype must implement all 3 lifecycle functions:
`Is<Name>SkillCoiling`, `Get<Name>SkillProjectiles`, `Deactivate<Name>Projectile`.
The other 3 archetypes use `SKILL_EMPTY_PROJECTILE_API(Name)` from `core/skill_boilerplate.h` to stub them out.

---

## 2. Scaffolding command

```bash
python3 scripts/new_skill.py <element> <snake_name> --archetype <archetype> [--shader]
```

**Elements:** `water` `wood` `fire` `earth` `metal` `taiji`

Example:
```bash
python3 scripts/new_skill.py earth stone_spike --archetype ground
```

This creates `skills/earth/stone_spike/` with:
- `stone_spike_skill.c` — state machine (CASTING → ACTIVE → DISSOLVE)
- `stone_spike_skill.h` — lifecycle prototypes
- `stone_spike_params.inl` — file-scope statics (tunable values live here)
- `stone_spike_tunables.inl` — RegisterSkillTunables() body (sandbox live-tune)
- `stone_spike.vs` / `.fs` — only if `--shader` is passed

The generator auto-updates `skills/skills_generated.h`.

---

## 3. Implement the state machine

Typical phases and what to call in each:

```
CASTING (0 → castDuration)
  SpawnCastEffect(pos, preset, scale)
  PlayCastSound(preset)

FLYING (projectile only, 0 → travelTime)
  Motion_Init / Motion_Step  (core/motion_controller.h)
  SpawnProjectileTrail(start, target, preset, scale, speed)

ACTIVE / IMPACT
  SpawnImpactEffect(pos, preset, scale)   ← triggers TimeFX_Hitstop if scale≥1.5
  PlayImpactSound(preset)
  ForceField_CreatePreset(FORCE_PRESET_*)
  SkillBuilder_SpawnBeam(from, to, preset, width, duration)
  SkillBuilder_SpawnOrbitals(center, preset, count, r, dur)
  SkillBuilder_SpawnAuraRing(center, preset, radius, dur)
  SkillBuilder_SpawnGroundWave(origin, dir, preset, range, speed)

CHAIN (on impact, for jump/chain skills)
  n = SkillHelper_ChainTargets(origin, jumpRadius, maxJumps, points, maxOut)
  SpawnChainLightning(points, n, scale, hopDelay)
  Entity_ApplyAoEDamage(points[i], r, dmg, kb)   ← damage is the SKILL's job

DISSOLVE (0 → dissolveTime)
  let particles/decals expire naturally — no early Unload calls
```

---

## 3b. One-liner VFX builders

For common multi-step patterns — one call instead of composing manually.
All driven by `SkillHelper_Update(dt)` in `main.c` (skills don't call Update directly).
**Damage is always the skill's responsibility** — these helpers do visuals only.

| Call | What it does | Pool |
|---|---|---|
| `SkillBuilder_SpawnBeam(from, to, element, width, dur)` | ProcRay + VFXLight at endpoints | 8 |
| `SkillBuilder_SpawnGroundWave(origin, dir, element, range, speed)` | Expanding shockwave + decal emitter | 8 |
| `SkillBuilder_SpawnOrbitals(center, element, count, radius, dur)` | N orbiting tetrahedra, randomised phase/scale | 8 groups |
| `SkillBuilder_SpawnAuraRing(center, element, radius, dur)` | Looping emitter ring + glow decal | 8 |
| `SkillHelper_ChainTargets(origin, jumpR, maxJumps, pts, maxOut)` | Nearest-neighbour jump list (returns count) | — |
| `SpawnChainLightning(points, count, scale, hopDelay)` | One staggered lightning bolt per hop | queue 32 |

Kill handles: `SkillBuilder_KillBeam(h)` / `SkillBuilder_KillAuraRing(h)`.
Orbitals and GroundWave are fire-and-forget (self-expiring, no kill handle).

---

## 3c. Attached / status effects

For effects that must **follow a moving agent** across frames:

```c
// Looping element aura (burning, frozen, shocked, blessed)
int h = StatusVFX_Attach(agentId, EFFECT_PRESET_FIRE_EXPLOSION, 3.0f);
// Re-attaching same element+agent refreshes duration — does NOT stack.
// Auto-detaches on agent death (0.5s fade-out). Wire nothing — main.c handles Update/Draw.
StatusVFX_Detach(h);   // early removal (cleanse)

// Mesh ghost trail for dashes / blade sweeps
// Call every 0.04 s while the motion is active (use a caller-side timer):
Afterimage_Spawn(model, currentTransform, ColorAlpha(ELEMENT_COLOR_METAL, 180), 0.3f);
// model is a REFERENCE — do not unload while any ghost is alive.
// Dissolves via u_dissolve over `life` seconds; depth-write off, BLEND_ALPHA pass.
```

---

## 4. Per-element preset table

| Element | EffectPresetType | ForceFieldPreset | MaterialPreset / LoadElement |
|---|---|---|---|
| Water | `EFFECT_PRESET_WATER_SPLASH` | `FORCE_PRESET_WATER_VORTEX` | `Material_LoadElement(EFFECT_PRESET_WATER_SPLASH)` |
| Wood | `EFFECT_PRESET_WOOD_BLOOM` | `FORCE_PRESET_WOOD_GROWTH` | `Material_LoadElement(EFFECT_PRESET_WOOD_BLOOM)` |
| Fire | `EFFECT_PRESET_FIRE_EXPLOSION` | `FORCE_PRESET_FIRE_UPDRAFT` | `Material_Load(MATERIAL_FIRE)` |
| Earth | `EFFECT_PRESET_EARTH_CRACK` | `FORCE_PRESET_EARTH_RUMBLE` | `Material_LoadElement(EFFECT_PRESET_EARTH_CRACK)` |
| Metal | `EFFECT_PRESET_METAL_SHARD` | `FORCE_PRESET_METAL_IMPLOSION` | `Material_LoadElement(EFFECT_PRESET_METAL_SHARD)` |
| Taiji | `EFFECT_PRESET_TAIJI_BURST` | `FORCE_PRESET_TAIJI_ORBIT` | `Material_LoadElement(EFFECT_PRESET_TAIJI_BURST)` |

---

## 5. Scale rules (1 unit = 1 meter)

| Parameter | Target range | Reasoning |
|---|---|---|
| Particle / mesh radius | 0.10–0.20 m | Hand-sized to body-sized effects |
| Force / gravity strength | 3.0–7.0 | Compare: real gravity = 9.81 m/s² |
| Particle speed | 1.0–3.0 m/s | Walking pace to sprint |
| Impact `scale` ≥ 1.5 | triggers hitstop | intentional — keep large hits impactful |

> All existing skills are now fully meter-scaled (Item 34 complete 2026-07-04).
> Any **new** skill must use meter-scale from day one — never use the old 100× numbers.

---

## 6. Memory rules

- **No `malloc`/`free`** — static arrays and pools only.
- **No `UnloadShader`/`UnloadTexture` in skill code** — use `ResourceManager_LoadShader()`;
  resource manager owns lifetimes.
- Particles/trails/decals expire naturally — don't unload them manually.

---

## 7. Tunable wiring (sandbox live-tune)

Three common bugs that silently break tuning:

1. **Value baked at Init** — reading a tunable static in `Init*Skill` and storing
   into a local instead of keeping the static. Fix: read the static each frame.
2. **`SkillForceMix` never applied** — field built in Init, not rebuilt each frame.
   Fix: call `RebuildXxxField()` inside Update.
3. **Spawn code ignoring tunable static** — passing a literal instead of the static.
   Fix: every spawn call reads the file-scope static.

---

## 8. Lint check

```bash
make lint          # from build/ after cmake ..
# or directly:
python3 scripts/lint_skill.py skills/<element>/<name>/
```

8 rules checked. Any FAIL blocks the build from being considered clean.

---

## 9. Visual verification

```bash
WUXING_VERIFY=<skill_name> ./wuxing
# → saves autotest_output/verify_<skill>_<time>.png at 0.15, 0.5, 1.0, 2.0, 3.5 s
```

---

## 10. Aesthetic checklist

- [ ] Cast VFX is readable within 0.3 s (player sees *something*)
- [ ] Impact VFX reads as the correct element color (see `ELEMENT_COLOR_*` in `skill_manager.h`)
- [ ] At least one particle system OR one mesh — not just sound + decal
- [ ] Particles don't clip underground (check y-spawn ≥ 0.05 m)
- [ ] No jarring pop-in or pop-out — use dissolve/fade phases
- [ ] Night-time legibility: emissive intensity ≥ 0.5 for primary particles
- [ ] Dash / swing motion → `Afterimage_Spawn` every 0.04 s while active
- [ ] Status effect applied → `StatusVFX_Attach` (not just a one-time burst)
- [ ] Chain skill → zero-targets case handled gracefully (`SkillHelper_ChainTargets` returns 0)
- [ ] Pool overflow check: run with F3 overlay — no red rows after a full cast sequence

See `WUXING_ART_DIRECTION.md` for full aesthetic law reference.
