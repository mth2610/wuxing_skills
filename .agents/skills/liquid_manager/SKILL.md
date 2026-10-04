---
name: liquid_manager
description: Owns GPU/CPU liquid simulation, Screen Space Fluid rendering, impact-to-puddle behaviour, and performance validation in Wuxing Skills. Use for water, blood, mud, or other coherent liquid effects.
---

# Liquid Manager

## Scope

Own `core/liquid/`, its shaders, its CMake entries, and public liquid API docs. Read map/environment headers only to integrate collision or depth receivers. Do not alter generic particles, composition skills, maps, or renderer code unless a documented liquid interface requires it.

## Architecture contract

Liquid has four separable layers. Never use SSF to hide a broken simulation.

1. **Simulation:** Force Field is the default impact backend. GPU PBD is an explicit, lazily initialized, single-body option; unavailable or busy GPU PBD falls back to Force Field (`core/liquid/liquid_impact.h`, `core/liquid/liquid_impact.c`). CPU PBD is a separate solver, not the live impact fallback (`core/liquid/liquid_pbd.h`). The current GPU solver applies separation and cohesion, not a full PBF/XPBD density solve (`core/liquid/shaders/liquid_pbd_gpu.comp`).
2. **Impact:** accept real `initialVelocity` in m/s, reflect only the normal component at a receiver, preserve tangential momentum, then seed a compact incoming volume/crown. Never seed all particles as a flat ground disc.
3. **Surface:** the current SSF path captures front/back envelopes and resolves thickness from their difference. HIGH uses a true 2D narrow-range depth filter at native resolution; lower tiers use separable filtering. Only the thickness chain uses half resolution (`core/liquid/liquid_surface.c`). GPU capture uses analytical impostors, while current CPU capture uses meshes; future CPU capture changes must use batched or instanced analytical sphere/ellipsoid impostors. Optics use reconstructed normals, refraction, and Beer-Lambert absorption (`core/liquid/shaders/liquid_surface.fs`).
4. **Settlement:** on valid receiver collision, reduce normal velocity, apply friction/viscosity, flatten only resting particles into ellipsoid splats, and retain a bounded puddle lifetime.

## Required workflow

Before editing, identify the failing layer: simulation, collision/settlement, surface capture, smoothing, or shading. Make changes in that layer first.

### Current SSF render-graph workflow

SSF input is submitted separately from ordinary VFX rendering. `LiquidImpact_Draw()` is a submission step (it does not shade the liquid): call it immediately before `LiquidSurface_HasPending()` inside the screen-space composite phase. It must not sit inside decal, particle, trail, or debug-category gates. Then run `LiquidSurface_Capture(camera)` and `LiquidSurface_Composite()` only when the submitted surface has pending input. A flight-only water orb therefore remains visible before it has produced any decal or impact.

For GPU PBD, validate all of these before claiming completion:

- SSBO ping-pong bindings match GLSL layouts exactly.
- Grid clear/build/solve dispatches use synchronization-safe ordering.
- GPU state is drawn directly by an instanced SSBO renderer; no CPU readback.
- `LiquidSurface_Capture` does not return early when only GPU PBD is active.
- A fast moving incoming body visibly splashes before it settles into a puddle.
- Test explicit GPU PBD and the default Force Field backend, including unavailable/busy PBD fallback. Test CPU PBD separately when changing that solver; do not raise CPU input count to imitate GPU quality.

## Performance budgets

- Existing GPU PBD uses 2,048 particles on HIGH, a 4,096-particle allocation bound, and four solver iterations (`core/liquid/liquid_pbd_gpu.c`, `core/liquid/liquid_pbd_gpu.h`). The allocation bound is not a selectable Ultra tier.
- Aggregate CPU SSF submission is bounded at 384 particles (`LIQUID_SURFACE_MAX_PARTICLES` in `core/liquid/liquid_surface.h`); this is not a CPU PBD fallback budget.
- Future capture optimization must batch or instance analytical impostors and avoid per-particle model draws; preserve the current front/back thickness contract (`core/liquid/liquid_surface.c`).

## Verification

Build `wuxing` after changes. Run the VFX impact fixture on the Vulkan backend when permitted. Inspect three phases: immediate impact, airborne crown, and settled puddle. Report measured FPS plus screenshots; a successful build alone is not visual validation.

## API discipline

Use `LiquidImpact_SpawnWater()` from gameplay. Keep all implementation files under `core/liquid/`. When public structs/functions change, regenerate `core/docs/API.md` with `bash scripts/gen_core_api_index.sh > core/docs/API.md`.

## Patch Log

| Date | Editor | Section edited | Based on which source | Tier |
|---|---|---|---|---|
| 2026-10-04 | Codex | Skill name, scope and public API references | User-authorized Liquid module naming convention | Convention |
| 2026-10-04 | Codex | Current backends, SSF reconstruction and bounded pools | `core/liquid/liquid_impact.h`, `liquid_impact.c`, `liquid_pbd.h`, `liquid_pbd_gpu.h`, `liquid_pbd_gpu.c`, `liquid_surface.h`, `liquid_surface.c`, `shaders/liquid_pbd_gpu.comp`, `shaders/liquid_surface.fs` | Ground-truth |
