---
type: "query"
date: "2026-09-01T02:08:05.442448+00:00"
question: "Bây giờ, chưa cần test thực trên mobile, hãy tiến hành tiếp các phase tiếp theo (viết lại kế hoạch nếu cần)"
contributor: "graphify"
outcome: "useful"
source_nodes: ["GfxQuality_Get()", "volume_optical_depth_test.c"]
---

# Q: Bây giờ, chưa cần test thực trên mobile, hãy tiến hành tiếp các phase tiếp theo (viết lại kế hoạch nếu cần)

## Answer

Revised and implemented Phases 1-3 for the gas module: background-aware kind-specific optical reconstruction with bounded smoke/fire/energy controls; compact monotonic LOW/MED/HIGH simulation, atlas, raymarch, and noise profiles; safe deferred runtime quality-tier rebuilds; deterministic visual and performance regression tooling. Phase 4 desktop verification passed 96/96 core suites, a clean desktop build, smoke/fire/energy bloom A/B matrices, and a 40/90/140 fire lifetime matrix. Physical mobile GPU/thermal validation is intentionally deferred and documented.

## Outcome

- Signal: useful

## Source Nodes

- GfxQuality_Get()
- volume_optical_depth_test.c