# environment — Landmines

> Distilled, reusable lessons for the **environment** module. Format: Symptom → Cause → Rule (`DOC_ARCHITECTURE.md` §6).
> Cross-cutting engine traps live in root `ENGINE_LANDMINES.md`.

## Shadow map: the depth attachment was never the cost (22/10/2026)

**Symptom.** A `song_quao` frame is GPU-bound at ~20 ms (1280×720, HIGH). The 2048²
shadow target is the largest single non-content item, and `env_shadow.c` carried an
inherited note (§7.27) blaming a rlvk "sampleable depth twin" bounce at every scope
close. `WUXING_SHADOW_RES=1024` is worth **−2.88 ms** and `=512` **−4.37 ms**, which
reads like the two 16.8 MB attachment clears.

**Cause.** Three separate measurements, none of which agree with that reading:

1. `VKPROFILE` disproved the twin: the 2048² slot reports `twin_pixels=0.0`. Nothing
   bounces. That note was stale.
2. Removing the depth attachment *entirely* — the caster shader already writes
   `gl_FragCoord.z`, so the nearest-caster rule survives as a `GL_MIN` /
   `VK_BLEND_OP_MIN` blend on the R32F colour — was worth only **−0.2 ms** (4/4 paired
   runs, Q16–Q21 range 20.05–21.59 → 19.81–21.40). On this MoltenVK/Intel path a depth
   clear is all but free (fast clear); the **R32F colour clear** (~16.8 MB, ~1.0 ms) is
   the part that must really be written.
3. The rest of the resolution delta is the map casters' own fill:
   `WUXING_SONGQUAO_SKIP=shadowcasters` is worth **−1.4 ms**.

**Rule.** Price a clear by deleting the attachment (or the clear), never by halving the
resolution — resolution scales the clear *and* the geometry's fill together, so a
resolution A/B cannot separate them. Keep the min-blend capture (free, image-identical,
and it closes §7.27), and treat the shadow map's **resolution as the only remaining
lever**: every texel a caster does not touch must read 1.0 ("unshadowed"), so the colour
clear cannot be skipped or scissored away.
