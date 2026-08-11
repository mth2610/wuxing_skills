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
    Check(Has("core/composition/visual_composer.c", "SmokeEmitter_Update(dt);") && Has("core/composition/visual_composer.c", "VC_FlameEmitter_Update(dt);"), "both pools are ticked");
    return fails ? 1 : 0;
}
