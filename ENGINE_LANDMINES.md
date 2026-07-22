# ENGINE_LANDMINES.md — Cross-cutting traps every module can hit

> Lessons that are **not** owned by one module — any agent touching GL, shaders, rendering, or the device build can repeat them. Read this before touching GL/shaders or shipping to Android.
>
> Format per entry: **Symptom → Cause → Rule** (see `DOC_ARCHITECTURE.md` §6). Module-local lessons stay in `module/docs/LANDMINES.md`; only promote here when another module could hit it.

## Index

| # | Trap | Bites |
|---|---|---|
| 1 | Raylib batching hazard (depth-state leak) | Anyone drawing with immediate mode + depth state changes |
| 2 | `rlFrustum` / camera near < 1.0 → blank render | Anyone tuning camera clip planes / custom projections |
| 3 | Lit material invisible in the dark night arena | Anyone drawing lit ground/geometry |
| 4 | Mali: `fract(sin(...))` noise hash dies | Any shader using hash noise, on-device only |
| 5 | Mali: top-84px is a mandatory gesture zone | Any on-screen UI/touch target |
| 6 | Android: black screen / crash on device | Anyone changing the Android build flags |
| 7 | `CreateEmitter` needs per-frame target update; name collision | Anyone using the emitter systems |
| 8 | Per-instance uniform changes → stale-UBO scrambling (rlvk) | Any VFX that calls `SetShaderValue` between instances |

---

## 1. Raylib batching hazard (depth-state leak)

- **Symptom:** ground/environment drawn just before your effect renders with the wrong GL state — depth-write disabled, Z-buffer corrupted, soft particles broken.
- **Cause:** raylib's `rlgl` batches immediate-mode draws. State calls (`rlDisableDepthMask/Test`, `rlEnableDepthMask/Test`) change GL **immediately**, but vertices already queued via `rlBegin/rlEnd` are still un-flushed and get drawn under your new state. (Root-caused in `core/docs/PROGRESS.md` Item 3 / soft particles.)
- **Rule:** always flush the batch with `rlDrawRenderBatchActive()` **before and after** any depth mask/test change:
  ```c
  rlDrawRenderBatchActive();   // flush first
  rlDisableDepthMask(); rlDisableDepthTest();
  // ... your draw ...
  rlDrawRenderBatchActive();   // flush before restoring
  rlEnableDepthMask(); rlEnableDepthTest();
  ```

## 2. `rlFrustum` / camera near < 1.0 → blank render

- **Symptom:** whole scene renders blank/black after a camera or projection change.
- **Cause:** a near clip plane below `1.0` with the custom frustum path produces a broken projection under this engine's scale. (Logged in the meter-rescale work.)
- **Rule:** keep camera near clip ≥ `1.0`. If a change to clip planes / custom projection blanks the scene, suspect near-plane first.

## 3. Lit material invisible in the dark night arena

- **Symptom:** ground/geometry built with `EffectMaterial` (lit) renders black-on-black and disappears in the night arena.
- **Cause:** the lit shader has almost no light to work with in the dark scene, so lit surfaces collapse to black.
- **Rule:** for ground/large geometry in the night arena, use self-shaded vertex-color gradients instead of a lit material. Don't debug it as a "missing geometry" bug.

## 4. Mali: `fract(sin(...))` noise hash dies

- **Symptom:** on-device (Mali GPU, e.g. Samsung A33) only — an aura/effect turns into an invisible black hole or static; fine on desktop.
- **Cause:** `fract(sin(x))` hash noise loses precision at large domains on Mali. Note: **invisible ≠ shader failure** — a failed shader draws WHITE; invisible means the math degenerated.
- **Rule:** use a non-`sin` hash in `noise.glsl`. When something is invisible (not white) only on device, suspect a precision-degenerate hash before anything else.

## 5. Mali: top-84px is a mandatory OS gesture zone

- **Symptom:** finger taps on UI near the top of the screen do nothing on device, yet `adb shell input tap` at the same spot "works".
- **Cause:** the top 84px is a mandatory OS gesture zone that eats touches; `adb` bypasses the OS so it masks the bug.
- **Rule:** keep interactive UI below `y = 90`. Never trust `adb tap` to validate touch reachability.

## 6. Android: black screen / crash on device

- **Symptom (black screen):** app runs but shows only black; **Symptom (crash):** hard crash on launch.
- **Cause:** black screen = raylib 6.0 `CUSTOMIZE_BUILD` landmine (`EndDrawing` never swaps buffers). Crash = `-DGRAPHICS` is ignored on Android; ES2 instancing pointers are NULL on Mali.
- **Rule:** build Android with `-DOPENGL_VERSION="ES 3.0"` (not `-DGRAPHICS`, not ES2). After changing build flags, delete the raylib build cache. (Verified on Samsung A33 / Mali.)

## 7. Emitter systems: per-frame target + name collision

- **Symptom:** an emitter created with `CreateEmitter` doesn't follow its target; or edits to "EmitterSystem" affect the wrong thing.
- **Cause:** `core/emitter_system.h`'s `CreateEmitter` requires per-frame `UpdateEmitterTarget` calls. Separately, `main.c`'s `EmitterSystem_Update(dt)` is an **unrelated same-named system** in `skill_helper.c`.
- **Rule:** call `UpdateEmitterTarget` every frame for managed emitters. Don't assume the two `EmitterSystem` names are the same system — verify which one you're editing.

## 8. Per-instance uniform changes → stale-UBO scrambling (rlvk/Vulkan)

- **Symptom:** an effect renders correctly most of the time, then intermittently comes out as **small rectangles in scrambled positions or wrong colors**. Worse with several instances on screen at once, or while the camera moves; the next frame can look fine.
- **Cause:** changing a uniform between instances forces one batch flush — and one UBO snapshot — per instance. When rlvk's per-frame bump arena filled up, the UBO push was silently **skipped** and the draw inherited the *previous* push: stale `mvp` (wrong transform → scrambled quads) plus stale uniform values. Fixed in the backend (reserve-before-record, `third_party/vulkan/docs/HANDOFF.md` §7.28, guard scenario `ubo_arena`); the pattern is still the one that stresses the arena hardest.
- **Rule:** don't flush per instance when you can avoid it — carry per-instance variation in a **vertex attribute** (color/normal/UV channel) instead of a uniform, and keep uniforms to values shared by the whole batch. If you must change a uniform per instance, keep the instance count bounded. Also: never let a shader's noise domain depend on an unbounded value — wrap it with `fract()` first, or a bad frame degenerates into flat blocks (see `core/shaders/smoke_column.fs`).
