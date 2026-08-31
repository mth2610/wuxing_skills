---
type: "query"
date: "2026-08-31T15:31:35.682512+00:00"
question: "tiến hành đi phase 0"
contributor: "graphify"
outcome: "useful"
source_nodes: ["PostFX_PerfSample()", "GfxQuality_Default()"]
---

# Q: tiến hành đi phase 0

## Answer

Expanded from original query via vocab: [render, matrix, capture, perf, frame, quality, test, smoke, volume]. PostFX_PerfSample() established the repository convention that CPU wall-clock around fullscreen draws is submission cost rather than GPU time, while GfxQuality_Default() exposed the tier-selection seam. Phase 0 added gas rolling telemetry, deterministic tier and bloom/kind capture controls, a timed fail-fast visual harness, LOW/MED/HIGH workload verification, and smoke/fire/energy bloom A/B baselines. The gas implementation itself was absent from the graph, so direct source inspection remained necessary.

## Outcome

- Signal: useful

## Source Nodes

- PostFX_PerfSample()
- GfxQuality_Default()