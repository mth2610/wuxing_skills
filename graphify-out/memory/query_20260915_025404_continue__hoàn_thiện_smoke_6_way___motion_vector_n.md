---
type: "query"
date: "2026-09-15T02:54:04.059018+00:00"
question: "continue: hoàn thiện smoke 6-way + motion vector nhẹ"
contributor: "graphify"
outcome: "useful"
source_nodes: ["ParticleConfig", "VFX_SurfaceRegistry_LoadTexture", "particle_lighting_test.c", "texture_packing_test.c"]
---

# Q: continue: hoàn thiện smoke 6-way + motion vector nhẹ

## Answer

Expanded from original query via vocab: [smoke, motion, vector, flow, surface, registry, profile, texture, budget, particle, lighting]. Added first-class LIGHT6 and lightMapB registry contract, registered SmokePuff Map A/Map B plus a 1024x1024 optical-flow atlas, wired motion to the smoke particle config, made fallback loading mutually exclusive with the primary set, used Map B alpha as baked AO for smoke, and kept flame emission outside 6-way lighting. Focused tests and registry validation pass; full Core remains at the existing 108/120 baseline.

## Outcome

- Signal: useful

## Source Nodes

- ParticleConfig
- VFX_SurfaceRegistry_LoadTexture
- particle_lighting_test.c
- texture_packing_test.c