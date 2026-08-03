# Deform module

Owns mesh displacement: `P' = P + D(P, N, mat, t)`. Public entry point:
`core/deform/mesh_deform.h`.

The vertex-space twin of `core/uv/`, and shaped the same on purpose:

| | warps | runs in |
|---|---|---|
| `core/uv/` | the sampling coordinate | fragment |
| `core/deform/` | the geometry | vertex (today: CPU, at mesh build) |

They share `UVEnvelopeKind` rather than each declaring their own. The
along-surface gate that weights a wave's amplitude, blends a texture layer and
scales a displacement is one concept; the reference technique calls it
`GradientScale(V)` and uses it to weld smoke to its emitter.

## Two noise sources, both live

- **Image** — an R8G8B8A8 sheet, seamless on both axes. Each channel is an
  independent field, so one bilinear fetch feeds one layer. Used by the trail
  system's tubes (`volume_noise.png`).
- **Procedural lattice** — no asset. Used by the beam.

`tiling.x` (around the section) applies to the image source only. The
procedural field's period around the section must divide the section exactly,
or it leaves a seam running the whole length of the body where `u = 0` meets
`u = 1`. Scaling it would break that by construction, so it is refused rather
than silently honoured.

## No GLSL mirror, deliberately

No vertex shader in this engine samples a texture. Vertex texture fetch is
therefore unproven on the rlvk backend, and a mirror written now would be a
shader nobody can run and nobody can verify — the "a shader with no consumer
has never been compiled" trap in `core/docs/LANDMINES.md`.

What exists instead is a GPU-*shaped* layout: `MeshDeform_PackGPU` emits
`vec4`-only arrays with the enum riding as a float, and `MeshDeform_Apply`
carries the usual location cache. When a consumer appears, the move is a port
rather than a redesign — and the CPU evaluator stays as the reference the
shader must reproduce.

## Consumers

`core/geometry/pm_tube.inl` (both builders — the block used to be copy-pasted).
`core/tests/mesh_deform_test.c` proves the move produced identical floats for
both noise sources; two shipped effects depend on those numbers.
