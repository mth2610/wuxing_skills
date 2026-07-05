# Project: Metric VFX & Burst Test

## Architecture
- `sandbox/vfx_test.c`: An interactive testbed application for visualizing various VFX presets. Contains UI drawing code, update loops, camera setups, and test tab handlers.
- `core/visual_composer.c`: Core module for composing complex VFX patterns (projectiles, impact bursts, force fields, etc.) using primitive systems like particles, decals, trails, and shaders.
- Data Flow: `vfx_test.c` processes user clicks and calls composition functions from `visual_composer.c` passing coordinate vectors (`Vector3`) and configuration structs.

## Milestones
| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| 1 | Exploration & Mapping | Investigate existing coordinates/forces and map tab structures | None | DONE |
| 2 | Code Conversion & Implementation | Convert `vfx_test.c` to metric, update projectile forces in `visual_composer.c`, and implement BURST tab | M1 | DONE |
| 3 | Review & Challenger Verification | Perform code review, compile, and execute test cases to verify visual/behavioral aspects | M2 | DONE |
| 4 | Forensic Audit | Perform integrity checks to ensure genuine implementation | M3 | DONE |

## Interface Contracts
### `vfx_test.c` ↔ `visual_composer.c`
- `VFX_ComposeProjectileTrail`: Composes projectile VFX. Needs updated forces internally.
- `VFX_ComposeTriggerImpactBurst`: Composes a multi-step impact burst (distortion, decal, light, radial particles). Receives an `ImpactBurstConfig` struct.
- Coordinates, sizes, and velocities passed between the testbed and the composer are now strictly in meters (1.0f = 1 meter).
