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

### Check `IsKeyPressed` collisions before binding a test key
- **Symptom:** a debug toggle also cycles the map; effect looks position-dependent when it isn't.
- **Cause:** `KEY_K` is already globally bound in `main.c` (cycle maps); raylib gives no key exclusive ownership, so both handlers fire.
- **Rule:** grep `IsKeyPressed(KEY_` across `main.c`/`sandbox/*.c` before binding any new key in a harness.
