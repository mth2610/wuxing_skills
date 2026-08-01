---
name: fluid_manager
description: Owns GPU/CPU fluid simulation, Screen Space Fluid rendering, impact-to-puddle behaviour, and performance validation in Wuxing Skills. Use for water, blood, mud, or other coherent liquid effects.
---

# Fluid Manager

## Scope

Own `core/fluid/`, its shaders, its CMake entries, and public fluid API docs. Read map/environment headers only to integrate collision or depth receivers. Do not alter generic particles, composition skills, maps, or renderer code unless a documented fluid interface requires it.

## Architecture contract

Fluid has four separable layers. Never use SSF to hide a broken simulation.

1. **Simulation:** GPU PBD is the high-quality path (2,048+ particles); CPU PBD is the fallback (<=384). Use a uniform-grid spatial hash and Jacobi/XPBD constraints. Keep position, velocity, radius, phase, and lifetime in solver state.
2. **Impact:** accept real `initialVelocity` in m/s, reflect only the normal component at a receiver, preserve tangential momentum, then seed a compact incoming volume/crown. Never seed all particles as a flat ground disc.
3. **Surface:** render instanced quad sphere/ellipsoid impostors, not sphere meshes. Capture curved depth plus additive thickness; smooth with separable bilateral filtering; shade using normal reconstruction, refraction, and Beer-Lambert absorption.
4. **Settlement:** on valid receiver collision, reduce normal velocity, apply friction/viscosity, flatten only resting particles into ellipsoid splats, and retain a bounded puddle lifetime.

## Required workflow

Before editing, identify the failing layer: simulation, collision/settlement, surface capture, smoothing, or shading. Make changes in that layer first.

For GPU PBD, validate all of these before claiming completion:

- SSBO ping-pong bindings match GLSL layouts exactly.
- Grid clear/build/solve dispatches use synchronization-safe ordering.
- GPU state is drawn directly by an instanced SSBO renderer; no CPU readback.
- `FluidSurface_Capture` does not return early when only GPU PBD is active.
- A fast moving incoming body visibly splashes before it settles into a puddle.
- Test both high GPU and no-compute CPU fallback; do not raise CPU particle count to imitate GPU quality.

## Performance budgets

- GPU: target 2,048 high / 4,096 ultra particles, 4–6 solver iterations, half-resolution SSF.
- CPU fallback: <=384 particles, no mesh-per-particle capture.
- Use one instanced draw per depth/thickness stream. Any per-particle model draw is a blocker.

## Verification

Build `wuxing` after changes. Run the VFX impact fixture on the Vulkan backend when permitted. Inspect three phases: immediate impact, airborne crown, and settled puddle. Report measured FPS plus screenshots; a successful build alone is not visual validation.

## API discipline

Use `FluidImpact_SpawnWater()` from gameplay. Keep all implementation files under `core/fluid/`. When public structs/functions change, regenerate `core/docs/API.md` with `bash scripts/gen_core_api_index.sh > core/docs/API.md`.
