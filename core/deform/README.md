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

They also share a second principle, not yet named as a shared rule when this
file was first written: **the drive coordinate is not the raw parametric
position.** `core/uv/uv_deform.h` states it for the UV half — "pass a label
stamped on the geometry when it was created — metres of emitter path at the
moment a node was laid — not a distance from the tail." The same failure mode
exists here, on the vertex side, for a shape whose length changes over time
(a MOVING trail, not a frozen one): `core/geometry/pm_tube.inl`'s per-ring `t`
is a fraction of the CURRENT total path length, and on a moving emitter that
length pulses with instantaneous speed — sampling a noise field at raw `t`
therefore stretches/squashes the field's spatial grain in lockstep with speed,
a pure geometry artefact with nothing to do with the material being deformed
(confirmed 05/08/2026, `core/composition/common/vc_smoke_trail.inl`'s own
history: "phồng xẹp" / "một cái rụp" — the pumping and the synchronized-
reindex snap, both traced to this exact cause). `PMTubeConfig.noiseWavelength`
+ `noiseSpanLenOverride` (`core/geometry/procedural_mesh_utils.h`) are the
CPU/vertex-space instance of the same fix: re-express the drive coordinate in
real metres (optionally a time-smoothed running one,
`TrailEntity.tubeNoiseSpanLen` in `core/trails/trail_system.h`) instead of the
raw parametric fraction. Not folded into this module's own API (yet) because
it is pm_tube.inl-specific plumbing, not a generic `MeshDeformField` concept —
but the PRINCIPLE is the same one `core/uv/` already names, and a future
consumer hitting the same symptom should look here first, not re-derive it.

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
