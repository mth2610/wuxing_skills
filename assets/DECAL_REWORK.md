# Decal rework gate

The current `assets/textures/decals/` catalog is legacy reference only: no new
primary may select it by filename or treat it as approved art.

Rebuild decals by surface role, not element name:

| Role | Primitive | Required channels | Examples |
|---|---|---|---|
| residue | alpha decal | opacity + edge breakup | soot, frost, moss |
| scorch | alpha/multiply decal | char mask + optional ember mask | fire impact |
| fracture | alpha decal | crack mask + normal-like value | stone/metal impact |
| wet mark | alpha decal | opacity + flow direction | water contact |

Every candidate needs: source/provenance, intended blend mode, wrap/filter,
channel semantics, tiling/seam result, and a visual owner approval. Do not use a
generic circular stamp as a substitute for a residue primitive.

## P4 surface-material contract

P4 is not a textured ground quad. A shipping Residue/Scorch mark is a
subdivided conformal mesh stamp: it follows the receiver height/normal and is
rejected on unsuitable receiver angles. Its material stack has optional body,
mask and gradient inputs with distinct channel meanings: body supplies char or
albedo, mask supplies opacity/edge erosion, and gradient supplies material
ramp/emissive control. Normal/height and roughness are authored into that stack
when the approved source needs them; no channel is inferred from a filename.

The pass keeps depth test enabled and depth writes disabled. Any future rlgl
state change flushes before and after it. Lifetime fades through the erosion
mask, not an alpha-only disappearance. Each profile declares draw/texture
budget, blend law, filter, seam, projection, provenance and owner approval in
`vfx_surface_profiles.json`.

The current legacy review is deliberately conservative: `scorch_mark.png` is a
circular rock/crater stamp and must be replaced. `VFX_ComposeScorch` now uses
the versioned ImageGen preview material `surfaces/scorch_material_v1.png`, whose
magenta key has been removed to a soft alpha matte; it remains visual-review
only, never shipping art. `decal_moss_stain.png` and
`decal_lightning_char.png` remain legacy candidates that also must be replaced
until an owner approves a source with the required channels. `impact_ring.png`
is rejected for residue. No legacy decal is an approved P4 shipping fallback.

Impact and Rune have the same registry-first gate. Impact requires organic edge
erosion, not a generic hit ring. Rune uses a separate `symbol_boundary_alpha_required`
seam contract: a glyph must stay legible under terrain projection rather than
being treated as an irregular soot mark. Neither profile has a runtime asset
until visual owner review completes.
