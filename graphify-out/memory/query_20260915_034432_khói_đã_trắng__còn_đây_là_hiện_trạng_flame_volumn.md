---
type: "query"
date: "2026-09-15T03:44:32.135454+00:00"
question: "khói đã trắng, còn đây là hiện trạng flame volumn"
contributor: "graphify"
outcome: "useful"
source_nodes: ["ParticleConfig", "flame_motion_test.c", "VFXRender_OutputDefines"]
---

# Q: khói đã trắng, còn đây là hiện trạng flame volumn

## Answer

Expanded from original query via graph vocab: [flame, volume, particle, shader, emission, render, texture, motion, color]. The screenshots and FLAME VOLUME matrix showed 68 concurrent whole-puff premultiplied sprites collapsing the centre near opaque and a near-linear emission gate radiating across most of each puff. Implemented a hard cap/default of 18 live volume puffs, body-size compensation 1.05, emissive gain 1.8, and compact emission gate smoothstep(0.06,0.75)^1.35. This keeps a connected silhouette while reducing estimated count-times-radius-squared fill cost about 62 percent versus 68 at size 0.88. Frame-90 dark matrix improved structure 0.571 to 0.645 and detail 0.080 to 0.118. Added flame_volume_optics_test and a reusable Core landmine. Related suites pass; full Core 109/121 with the same 12 unrelated baseline failures.

## Outcome

- Signal: useful

## Source Nodes

- ParticleConfig
- flame_motion_test.c
- VFXRender_OutputDefines