---
type: "query"
date: "2026-08-30T03:09:21.570748+00:00"
question: "có khả thi trên mobile ko"
contributor: "graphify"
outcome: "useful"
source_nodes: ["GfxQuality_Get()", "FluidSurface_RequestBody()", "FluidSurface_Composite()", "ParticleConfig"]
---

# Q: có khả thi trên mobile ko

## Answer

Expanded via graph vocab: [compute, depth, fallback, fluid, frame, gles, half, particle, quality, render, target, texture]. Feasible on modern Vulkan mobile only as a tightly budgeted local effect: one 24x32x24 or 32-cubed R16F atlas volume, 15-30 Hz simulation, 4-8 pressure iterations, quarter-resolution 16-32-step ray march, analytic colliders, and frame-time admission. The Samsung A33 Mali-G68 target currently measures about 30 FPS with the existing HDR/PostFX/GPU-particle chain, so 48/64-cubed multi-volume simulation is not a safe mobile baseline. GLES and overloaded frames must fall back to current volume-sheet particles.

## Outcome

- Signal: useful

## Source Nodes

- GfxQuality_Get()
- FluidSurface_RequestBody()
- FluidSurface_Composite()
- ParticleConfig