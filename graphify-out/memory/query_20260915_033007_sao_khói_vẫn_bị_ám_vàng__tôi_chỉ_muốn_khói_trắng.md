---
type: "query"
date: "2026-09-15T03:30:07.564503+00:00"
question: "sao khói vẫn bị ám vàng? tôi chỉ muốn khói trắng (nó còn kiểu nhấp nháy màu)"
contributor: "graphify"
outcome: "corrected"
correction: "White smoke must reject 100 percent of lighting and material chroma; keeping 18 percent environment chroma was incorrect for this requirement."
source_nodes: ["ParticleConfig", "particle_lighting_test.c", "VFXRender_OutputDefines"]
---

# Q: sao khói vẫn bị ám vàng? tôi chỉ muốn khói trắng (nó còn kiểu nhấp nháy màu)

## Answer

Expanded from original query via graph vocab: [smoke, particle, lighting, render, shader, color, texture, output]. Root cause: true 6-way smoke still used fragColor.rgb as albedo while vc_smoke_puff mixed mat->body into each sprite; retaining 18 percent coloured lighting also allowed yellow/purple hue and per-frame hue variation. Fixed particle_lit.fs so true 6-way non-volume smoke converts the complete light sum to luminance and uses vec3(1.0) albedo. Removed element hue injection from vc_smoke_puff.inl. Fire volume blackbody/emission remains coloured. Tests: particle_lighting 29/29, smoke_6way_motion 12/12, flame_motion_vector 23/23, full Core unchanged at 108/120 baseline.

## Outcome

- Signal: corrected
- Correction: White smoke must reject 100 percent of lighting and material chroma; keeping 18 percent environment chroma was incorrect for this requirement.

## Source Nodes

- ParticleConfig
- particle_lighting_test.c
- VFXRender_OutputDefines