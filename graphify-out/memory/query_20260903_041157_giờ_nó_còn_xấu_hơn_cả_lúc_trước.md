---
type: "query"
date: "2026-09-03T04:11:57.652205+00:00"
question: "giờ nó còn xấu hơn cả lúc trước"
contributor: "graphify"
outcome: "corrected"
correction: "For structured Magic Trail, preserve the sub-Bloom carrier and only blend inverse-ACES over HDR excess using (peak - 1.25) / peak; full per-fragment hue preservation is visually wrong."
source_nodes: ["ApplyDeformUniforms", "ResolvePass"]
---

# Q: giờ nó còn xấu hơn cả lúc trước

## Answer

Full inverse-ACES correction flattened Magic Trail's structured density gradient into a saturated neon band. Replaced it with an HDR-excess-only gate after coverage; the sub-Bloom carrier stays unchanged and the white/warm matrix returns exactly to the pre-migration baseline.

## Outcome

- Signal: corrected
- Correction: For structured Magic Trail, preserve the sub-Bloom carrier and only blend inverse-ACES over HDR excess using (peak - 1.25) / peak; full per-fragment hue preservation is visually wrong.

## Source Nodes

- ApplyDeformUniforms
- ResolvePass