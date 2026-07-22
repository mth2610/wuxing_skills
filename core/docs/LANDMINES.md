# core — Landmines

> Distilled, reusable debugging lessons for the **core** module. Format: Symptom → Cause → Rule (`DOC_ARCHITECTURE.md` §6).
> Cross-cutting traps (batching hazard, depth-test-vs-mask, `rlFrustum near<1.0`, lit-material-dark, emitter collision) live in root `ENGINE_LANDMINES.md` — read that too.
> Long session logs and open backlog are in `PROGRESS.md`, not here.

### Custom texture binding must go through `SetShaderValueTexture`
- **Symptom:** `texture(u_myTex, ...)` reads back 0 in a shader no matter which slot you bind, for a custom multi-texture shader.
- **Cause:** manual `rlActiveTextureSlot()`/`rlEnableTexture()` binding silently doesn't reach a raylib `Shader`. Hit independently by `flow_map.c` and the soft-particle depth bind.
- **Rule:** bind custom textures with `SetShaderValueTexture()` and let raylib manage the unit; don't hand-roll `rlActiveTextureSlot`/`rlEnableTexture` for a raylib `Shader`.

### Depth-linearization near/far must match the real projection, not the clip-plane globals
- **Symptom:** every depth sample crushes to near-zero; soft-particle occlusion looks uniformly "no occlusion".
- **Cause:** `rlGetCullDistanceNear/Far()` reflects `rlSetClipPlanes`, but `MyBeginMode3D`'s `rlFrustum(...)` hardcodes a different near (10.0). Two unrelated globals; using the clip-plane one is wrong.
- **Rule:** linearize depth with the near/far the actual projection uses. There is no shared source of truth — if `MyBeginMode3D`'s near/far changes, update the `SOFT_PARTICLE_SCENE_NEAR/_FAR` constants in `core/screen_distort.c` too.

### Prefer a shader-side debug view over a CPU numeric readback
- **Symptom:** a CPU 3-point depth readback gives misleading numbers even though it's "numeric".
- **Cause:** the sampled world points don't correspond to the actual visible front-surface fragment at that pixel from an oblique camera.
- **Rule:** numeric beats screenshot-guessing, but a numeric check is only as good as whether it queries the *actual rendered fragment*. Prefer a shader-side debug view (real per-fragment value) over a CPU approximation. Avoid pre-clamped debug views — they hide narrow signals; flag near-zero explicitly.

### Meter-scale: a correct skill file can still look wrong via shared code
- **Symptom:** a fully-converted skill still renders oversized/displaced effects.
- **Cause:** the shared functions it calls (`CastSkill` offsets, `SpawnImpactEffect` presets, `ProcRay_*Config` thickness, `UpdateSkillManager` enemyRadius) had un-rescaled internals.
- **Rule:** when converting a skill, trace *into* every shared function it calls and check that function's own internals are meter-scaled — don't trust a normal-looking signature. Full checklist in `PROGRESS.md` (Item 34).

### Lightning zigzag needs precomputed geometric waypoints, not physics/noise
- **Symptom:** an electric bolt drawn via a physics/noise trail renders as a straight line or a smooth "silk ribbon" sag, never a sharp zigzag.
- **Cause:** `TRAIL_TYPE_PROJECTILE` homing steer damps deviation back to straight every frame; `TRAIL_TYPE_WISP`'s `ConstrainRibbonSegment` distance-solver low-pass-filters per-node jaggedness into a flowing curve. Both are built to stay smooth by design.
- **Rule:** build a precomputed jagged polyline (perpendicular-offset kinks, no `forceField`) and drive a `TRAIL_TYPE_FOLLOWER` along it — see `SpawnLightningTrail`/`GenerateLightningWaypoints`. Don't try to make physics produce the kink.

### Check `IsKeyPressed` collisions before binding a test key
- **Symptom:** a debug toggle also cycles the map; effect looks position-dependent when it isn't.
- **Cause:** `KEY_K` is already globally bound in `main.c` (cycle maps); raylib gives no key exclusive ownership, so both handlers fire.
- **Rule:** grep `IsKeyPressed(KEY_` across `main.c`/`sandbox/*.c` before binding any new key in a harness.

---

## Tuning_RegisterFloat before Tuning_Init silently keeps the default

**Symptom.** A float registered with `Tuning_RegisterFloat` ignores the value in
`tuning.cfg` on a fresh run. The feature it drives looks dead. Editing and saving
`tuning.cfg` while the game runs makes it spring to life — which reads like a
hot-reload quirk and sends you looking in the wrong place entirely.

**Cause.** `Tuning_RegisterFloat` (`core/tuning.c:64`) only reads the config file
`if (s_configPath[0] != '\0')` — and that path is set by `Tuning_Init`. `main.c`
calls the subsystem inits (e.g. `InitParticleSystem`, :1017) well BEFORE
`Tuning_Init` (:1063), so anything registering from its own init registers before
the path exists, silently keeps `defaultValue`, and only picks the real value up
on the next mtime change.

**Rule.** Do not register tunables from a subsystem's `Init`. Register lazily on
first use (a `static bool` one-shot in the update/draw path), by which time
`Tuning_Init` has certainly run. If a default of 0 means "feature off", this bug
is invisible rather than merely wrong — see `ParticleLighting_Begin` in
`core/particle_system.c` for the shape to copy.
