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
