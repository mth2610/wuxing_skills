---
type: "query"
date: "2026-09-01T07:30:45.948540+00:00"
question: "lửa thì được rồi, nhưng năng lượng nó vẫn còn những điểm core sáng rời rạc, ngẫu nhiên, hơn nữa lửa và năng lượng ko cần viền khói; và khói tôi muốn khói trắng chứ ko phải khói đen"
contributor: "graphify"
outcome: "dead_end"
source_nodes: ["Shader", "volume_optical_depth_test.c"]
---

# Q: lửa thì được rồi, nhưng năng lượng nó vẫn còn những điểm core sáng rời rạc, ngẫu nhiên, hơn nữa lửa và năng lượng ko cần viền khói; và khói tôi muốn khói trắng chứ ko phải khói đen

## Answer

Expanded via graph vocabulary: [energy, core, fire, smoke, rim, noise, volume, shader, opacity, emission, body, color]. The traversal returned generic Shader and optical-depth nodes but not the gas channel expressions, so direct source inspection was required. ENERGY used thresholded detail noise as a core selector and full density opacity; SMOKE used a dark preset and low light floor. Fixed in the shared gas shader and preset.

## Outcome

- Signal: dead_end

## Source Nodes

- Shader
- volume_optical_depth_test.c