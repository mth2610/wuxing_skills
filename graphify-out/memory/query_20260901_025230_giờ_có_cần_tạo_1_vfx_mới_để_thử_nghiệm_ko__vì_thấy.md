---
type: "query"
date: "2026-09-01T02:52:30.553048+00:00"
question: "giờ có cần tạo 1 vfx mới để thử nghiệm ko? vì thấy các vfx cũ đâu có gì thay đổi nhiều?"
contributor: "graphify"
outcome: "useful"
source_nodes: ["visual_composer.c", "VFX_MaterialFromPreset()", "volume_optical_depth_test.c", "GfxQuality_Get()"]
---

# Q: giờ có cần tạo 1 vfx mới để thử nghiệm ko? vì thấy các vfx cũ đâu có gì thay đổi nhiều?

## Answer

Expanded from original query via graph vocab: [composition, volume, smoke, fire, energy, material, optical, preset, quality, visual, vfx]. Yes: add one dedicated GAS MATERIAL LAB fixture before authoring another production skill. Existing gas compositions inherit the new presets but do not explicitly author detailStrength, shadowStrength, or backgroundAdapt, and vortex/shockwave/flame-jet still override preset colors with existing materials, so backward-compatible core changes are intentionally subtle. The lab should run smoke, fire, and energy sequentially because Gas v1 admits one volume; fill roughly 15-25% of the screen; compare dark, white, warm, cool, and same-hue plates; sweep LOW/MED/HIGH and optical controls; and expose preset-palette versus gameplay-material A/B. After that, retune plume, vortex, shockwave, and flame jet individually. A single new gameplay VFX now would exercise only one material path and make diagnosis harder.

## Outcome

- Signal: useful

## Source Nodes

- visual_composer.c
- VFX_MaterialFromPreset()
- volume_optical_depth_test.c
- GfxQuality_Get()