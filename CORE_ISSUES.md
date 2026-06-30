# Core Issues / Backlog

Tasks for Core Agent — assign when ready.

## 1. Missing presets for Wood / Metal / Taiji (and partial Earth)

`core/skill_helper.h` preset enums only cover a subset of the 6 elements:

- `EffectPresetType` (impact, §9b): has FIRE_EXPLOSION, ICE_SHATTER, WATER_SPLASH, LIGHTNING_IMPACT, EARTH_CRACK — **missing WOOD, METAL, TAIJI**
- `EmitterPreset`: has EMITTER_FIRE, EMITTER_SNOW, EMITTER_WATER_SPURT, EMITTER_SHOCKED_SPARKS — **missing WOOD (leaves/vines), EARTH, METAL, TAIJI**
- ~~`DecalPresetType`: has DECAL_PRESET_BURN, DECAL_PRESET_CRACK, DECAL_PRESET_ICE, DECAL_PRESET_WATER — **missing WOOD, METAL, TAIJI**~~ **RESOLVED 2026-06-30** — expanded to 19 presets covering all 6 elements + 3 generic (impact_ring/glow_circle/shadow_blob), backed by a full new decal texture library under `assets/textures/decals/` and `assets/textures/generic/`. Old preset names (`BURN`/`CRACK`/`ICE`/`WATER`) kept for call-site compatibility; `ICE` now points to a real frost texture instead of the `dust_wind.png` placeholder.

Impact: Skills Agent must hand-build `ParticleConfig`/`ImpactBurstConfig` from scratch for Wood/Metal/Taiji skills instead of calling a one-line preset — more tokens, more boilerplate, more chance of drifting from art direction. (Decal preset gap closed; `EffectPresetType`/`EmitterPreset` gaps still open.)

Action: add the missing `EffectPresetType` and `EmitterPreset` enum values + implementations in `core/skill_helper.c/.h` for WOOD/METAL/TAIJI, update `CORE_API.md` §9b.

## 2. No full skill skeleton/template

CORE_API.md §4 only shows the `.h` lifecycle prototype template — no sample `.c` skeleton (state machine, static projectile array, Init/Cast/Update/Draw/Unload wiring). Compare to `MAP_API.md` §6 which has a complete `desert_lava.c` sample.

Impact: every new skill forces the Skills Agent to re-derive the same boilerplate (state enum CASTING→FLYING→IMPACT→DISSOLVE, static array + active flag pattern, call order across lifecycle functions) from scattered API docs instead of copying a known-good skeleton.

Action: add a minimal complete `.c` skeleton example to CORE_API.md §4, right after the `.h` template.

## 3. No "Cast Effect" preset (only Impact)

`SpawnImpactEffect()` exists for collision moments, but there's no equivalent for the cast/windup moment (energy gathering aura around caster before launch). Skills currently build this manually from raw particle/light/decal calls.

Action: add `SpawnCastEffect(Vector3 pos, EffectPresetType preset, float scale)` (or new `CastPresetType`) to `core/skill_helper.h/.c`.

## 4. No batch/streak decal helper

`DecalSystem_Add` is single-call only. Effects that lay multiple decals along a path (e.g. thorns growing along a line, scorch trail) require a manual loop in skill code every time.

Action: consider `DecalSystem_AddStreak(const Vector3 *points, int count, ...)` or similar helper — low priority, nice-to-have.

---
*Logged from skill-pipeline review, 2026-06-30.*
