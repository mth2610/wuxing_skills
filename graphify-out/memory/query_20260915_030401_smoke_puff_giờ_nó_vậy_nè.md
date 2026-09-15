---
type: "query"
date: "2026-09-15T03:04:01.981800+00:00"
question: "smoke puff giờ nó vậy nè"
contributor: "graphify"
outcome: "useful"
source_nodes: ["ParticleConfig", "particle_lighting_test.c", "VFXRender_OutputDefines"]
---

# Q: smoke puff giờ nó vậy nè

## Answer

Expanded from original query via graph vocab: [smoke, particle, lighting, render, shader, color, texture, output]. Screenshot cho thấy smoke 6-way bị màu sunlight/ambient nhuộm quá mạnh và quá nhiều whole-puff flipbook chồng nhau. Đã giới hạn chroma ánh sáng môi trường còn 18% trong particle_lit.fs cho riêng true 6-way non-volume smoke, giữ luminance và hướng sáng; volume fire/blackbody emission không bị ảnh hưởng. Đã giới hạn flipbook smoke tối đa 8 sprite trong vc_smoke_puff.inl để giảm mush/overdraw. Regression particle_lighting 29/29, smoke_6way_motion 11/11, flame_motion_vector 23/23; full Core 108/120 với đúng 12 lỗi baseline không liên quan.

## Outcome

- Signal: useful

## Source Nodes

- ParticleConfig
- particle_lighting_test.c
- VFXRender_OutputDefines