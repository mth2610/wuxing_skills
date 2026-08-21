---
type: "query"
date: "2026-08-21T03:11:04.581550+00:00"
question: "Đánh giá kiến trúc core"
contributor: "graphify"
outcome: "corrected"
correction: "Scope the assessment to core only; do not classify systems intentionally outside core as missing."
source_nodes: ["skill_manager.c", "ParticleConfig", "ResourceManager_LoadShader()"]
---

# Q: Đánh giá kiến trúc core

## Answer

Correction: core is not the entire engine. Map, networking, editor, platform, and other outer modules must not be treated as missing from core. Core-specific risks are concentrated SkillManager responsibilities, VFX/render coupling, and ParticleConfig centrality; evaluate cross-module boundaries separately.

## Outcome

- Signal: corrected
- Correction: Scope the assessment to core only; do not classify systems intentionally outside core as missing.

## Source Nodes

- skill_manager.c
- ParticleConfig
- ResourceManager_LoadShader()