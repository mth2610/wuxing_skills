---
type: "query"
date: "2026-09-01T04:20:06.347311+00:00"
question: "phân bố màu sắc ko hợp lý, nhìn nó kì kì. Hơn nữa tất cả vfx gas cũ đều bị phủ 1 lớp khói đen."
contributor: "graphify"
outcome: "useful"
source_nodes: ["Shader"]
---

# Q: phân bố màu sắc ko hợp lý, nhìn nó kì kì. Hơn nữa tất cả vfx gas cũ đều bị phủ 1 lớp khói đen.

## Answer

Expanded via graph vocabulary: [gas, volume, shader, fire, smoke, color, palette, body, core, opacity, emission]. Traversal highlighted the shared Shader path; code inspection then confirmed FIRE used a forced grey cooling carrier with rising opacity and noise-selected HDR cores. The fix removes the grey carrier, fades cooling FIRE density, maps color by thermal channels, and limits noise to modulation.

## Outcome

- Signal: useful

## Source Nodes

- Shader