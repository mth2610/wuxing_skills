---
type: "query"
date: "2026-09-15T04:08:49.708822+00:00"
question: "nó vẫn bị bệt trắng, đặc biệt ở nền đen, nghi ngờ do viền khói. Có tên tách bạch rõ ràng khói và lửa, nếu muốn lửa có khói thì nên dùng sub-emmiter"
contributor: "graphify"
outcome: "corrected"
correction: "Fire and smoke must be separate populations: pure flame reads only emission and carries zero opacity; smoke is optional through an explicit sub-emitter."
source_nodes: ["ParticleConfig", "smoke_fire_emitter_test.c", "ParticleManager_DrawEmission"]
---

# Q: nó vẫn bị bệt trắng, đặc biệt ở nền đen, nghi ngờ do viền khói. Có tên tách bạch rõ ràng khói và lửa, nếu muốn lửa có khói thì nên dùng sub-emmiter

## Answer

Expanded from original query via graph vocab: [flame, smoke, emitter, particle, volume, emission, opacity, shader, texture]. Confirmed the packed flame decoder still widened flameMask with opac*0.4 and could mix reconstructed soot into the same premultiplied output. Refactored volumeSheet mode 2 into pure emission-only flame: flameMask depends only on emis and output alpha is zero. Removed flame_smoke_gain and packed smoke tint controls; optional flame_smoke_amount now attaches a separate alpha-lit death sub-emitter. Fixed the particle death sub-emitter by removing an implicit legacy-scale random velocity of plus/minus 80 m/s so child velocity now equals authored velocity plus explicit velocityInheritance. FLAME VOLUME matrix at frame 90 dark: chroma 0.702 to 0.777, absvar 59.0 to 70.4, darken zero, confirming no smoke opacity remains in the flame pass. Focused test 17/17; full Core 109/121 with same 12 baseline failures.

## Outcome

- Signal: corrected
- Correction: Fire and smoke must be separate populations: pure flame reads only emission and carries zero opacity; smoke is optional through an explicit sub-emitter.

## Source Nodes

- ParticleConfig
- smoke_fire_emitter_test.c
- ParticleManager_DrawEmission