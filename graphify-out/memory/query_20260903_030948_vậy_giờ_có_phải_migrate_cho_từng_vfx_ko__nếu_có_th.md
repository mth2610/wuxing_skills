---
type: "query"
date: "2026-09-03T03:09:48.431214+00:00"
question: "vậy giờ có phải migrate cho từng vfx ko? nếu có thì hãy migrate cho trail magic trước"
contributor: "graphify"
outcome: "useful"
source_nodes: ["TRAIL_PRESET_MAGIC", "ApplyDeformUniforms", "ResolvePass"]
---

# Q: vậy giờ có phải migrate cho từng vfx ko? nếu có thì hãy migrate cho trail magic trước

## Answer

Expanded from original query via graph vocab: [trail, ribbon, swept, deform, material, output, shader, additive, vfx]. Có: các producer custom phải opt-in riêng. TRAIL_PRESET_MAGIC đi qua ApplyDeformUniforms và ResolvePass; đã migrate riêng VFX_CONTRAST_MAGIC sang VFX_TonemapSafeHDR sau coverage, các profile trail khác giữ đường cũ. Focused tests, 10/10 trail suites, shader validators, build và TRAIL MAGIC bright-background matrix đều đạt; full Core 103/104 với default_particle_asset_test là lỗi không liên quan.

## Outcome

- Signal: useful

## Source Nodes

- TRAIL_PRESET_MAGIC
- ApplyDeformUniforms
- ResolvePass