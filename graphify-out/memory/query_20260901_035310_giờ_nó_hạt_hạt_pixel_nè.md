---
type: "query"
date: "2026-09-01T03:53:10.356352+00:00"
question: "giờ nó hạt hạt pixel nè"
contributor: "graphify"
outcome: "useful"
source_nodes: ["Shader", "Test_CombinedAndResolution()"]
---

# Q: giờ nó hạt hạt pixel nè

## Answer

Expanded from the original query via project vocabulary: gas, volume, shader, jitter, pixel, grain, noise, resolution, filter, fire. MED and LOW gas render at quarter resolution. The stable full-range ray jitter prevented coherent bands but its low-resolution error became a visible four-pixel lattice after bilinear enlargement; the earlier broad clipped core had hidden it. The fix preserves unbiased jitter and applies a premultiplied 3x3 tent reconstruction at raymarch resolution using four bilinear half-texel taps before upscale. It removes the lattice without increasing simulation resolution or ray steps. Full Core tests, desktop build, and smoke/fire/energy background matrices pass.

## Outcome

- Signal: useful

## Source Nodes

- Shader
- Test_CombinedAndResolution()