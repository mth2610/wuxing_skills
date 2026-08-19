// P2 headless contract: persistent smoke/fire sources own independent state.
#include <stdio.h>
#include <string.h>

static int fails;
static void Check(int ok, const char *what) { printf("%s: %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++fails; }
static int Has(const char *path, const char *needle) {
    FILE *f = fopen(path, "rb"); char buf[65536]; size_t n;
    if (!f) return 0; n = fread(buf, 1, sizeof(buf) - 1, f); fclose(f); buf[n] = 0;
    return strstr(buf, needle) != NULL;
}
int main(void) {
    const char *smoke = "core/composition/common/vc_smoke_puff.inl";
    const char *fire = "core/composition/fire/flame_volume.inl";
    Check(Has(smoke, "VFX_SMOKE_EMITTER_MAX") && Has(smoke, "e->accum += dt"), "smoke rate carries a per-instance fraction");
    Check(Has(smoke, "VFX_SmokeEmitter_Stop") && Has(smoke, "VFX_KillSmokeEmitter"), "smoke owns Stop/Kill");
    Check(Has(fire, "FVOL_MAX_EMITTERS") && Has(fire, "float *bodyAccum = &emitter->bodyAccum"), "fire has no shared accumulator");
    Check(Has(fire, "emitter->lightTimer") && Has(fire, "VFX_FlameEmitter_Stop"), "fire light timer and lifecycle are per instance");
    Check(Has(fire, "emitter->legacyFeedAge > 0.25f") && Has(fire, "legacyFeedAge = 0.0f"), "legacy FlameVolume self-kills after feed stops");
    Check(Has(fire, "spriteAnimRate = Math_Mix(0.82f, 1.0f") &&
          Has(fire, "spriteFlipX = Random01() < 0.5f") &&
          Has(fire, "spriteFlipY = Random01() < 0.5f"),
          "directionless fire varies phase rate and both UV mirrors per sprite");
    Check(Has(fire, "followTarget = &emitter->pos") &&
          Has(fire, "followTargetGeneration = &emitter->generation") &&
          Has(fire, "generation = s_fvolNextGeneration"),
          "young fire follows its source and detaches safely on slot reuse");
    Check(Has(fire, "SpriteAnim_SetFrameMetadata(&s_fvolVolumeAnim") &&
          Has(fire, "flame_volume_puff_metadata.inl"),
          "volume flipbook binds bake-generated per-frame crop metadata");
    Check(Has(fire, "s_fvolRiseMul = 1.15f") &&
          Has(fire, "s_fvolWidthMul = 0.75f") &&
          Has(fire, "sqrtf(Random01()) * 0.28f") &&
          Has(fire, "Math_Mix(0.55f, 0.80f"),
          "volume emitter owns a narrow foot and stronger upward macro motion");
    /* 0.35, NOT 0.12. This assertion used to read "volume layers stay translucent
       instead of fusing into a white fireball", pinning 0.12 against an observed
       fusing at 0.18. That observation was real but predates the Dot H
       packed-sheet build and was made while tuning.cfg held a sub-1.0 bloom
       threshold that veiled the whole frame. Re-measured on the pinned harness
       (BRIGHT_BACKGROUND_VFX_SPEC.md §7.6c), 0.35 improves EVERY metric on EVERY
       background — white structure 0.095 -> 0.179, detail 0.013 -> 0.028,
       |d| 0.205 -> 0.341; dark structure 0.512 -> 0.536 — and no fusing appears.
       What the flame lacked on bright scenery was OPACITY, not light: it cannot
       out-shine a 1.0 background, so its legibility comes from occluding it.
       BOTH sites must say 0.35 — Tuning_RegisterFloat's default argument is
       assigned over the static initialiser, so changing one alone is a no-op. */
    Check(Has(fire, "s_fvolBodyLive = 190.0f") &&
          Has(fire, "s_fvolBodyAlpha = 0.35f") &&
          Has(fire, "&s_fvolBodyAlpha, 0.35f") &&
          Has(fire, "(unsigned char)(255.0f * s_fvolBodyAlpha)") &&
          Has(fire, "s_fvolHeatGain = 1.05f") &&
          Has(fire, "s_fvolEmissive = 4.8f") &&
          Has(fire, "s_fvolSmokeGain = 0.95f"),
          "the volume body is opaque enough to CUT a silhouette on bright scenery");
    Check(Has("core/composition/visual_composer.c", "SmokeEmitter_Update(dt);") && Has("core/composition/visual_composer.c", "VC_FlameEmitter_Update(dt);"), "both pools are ticked");
    return fails ? 1 : 0;
}
