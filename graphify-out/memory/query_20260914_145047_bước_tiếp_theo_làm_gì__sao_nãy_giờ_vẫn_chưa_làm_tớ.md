---
type: "query"
date: "2026-09-14T14:50:47.306945+00:00"
question: "bước tiếp theo làm gì? sao nãy giờ vẫn chưa làm tới hướng flippbok 6 way, vector motion (6 way hình như ko dùng cho lửa)"
contributor: "graphify"
outcome: "useful"
source_nodes: ["ParticleConfig", "particle_lighting_test.c", "smoke_fire_emitter_test.c"]
---

# Q: bước tiếp theo làm gì? sao nãy giờ vẫn chưa làm tới hướng flippbok 6 way, vector motion (6 way hình như ko dùng cho lửa)

## Answer

Expanded from original query via vocab: [smoke, fire, flame, particle, texture, motion, vector, light, lighting, render]. Engine đã có dual-texture 6-way mode 2 cho smoke và optical-flow motion vectors cho flame. Lõi flame vẫn emission/blackbody; synthetic 6-way mode 1 chỉ chiếu sáng soot đi kèm. Đã sửa shader để dùng mẫu 6-way đã optical-flow warp và dùng squared directional weights nhằm bảo toàn năng lượng. Bước kế tiếp là thêm motion atlas cho smoke qua surface registry và hạ footprint atlas theo texture budget.

## Outcome

- Signal: useful

## Source Nodes

- ParticleConfig
- particle_lighting_test.c
- smoke_fire_emitter_test.c