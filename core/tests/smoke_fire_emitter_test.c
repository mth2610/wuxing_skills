// P2 headless contract: persistent smoke/fire sources own independent state.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fails;
static void Check(int ok, const char *what) { printf("%s: %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++fails; }
static int Has(const char *path, const char *needle)
{
    /* Reads the WHOLE file. It used to read the first N bytes into a fixed
     * buffer, and that silently degrades: the day the implementation file grows
     * past N, assertions about anything below that offset start failing with no
     * hint that truncation — rather than the code — is the cause. It happened
     * here on 20/08/2026 at 48000 bytes. See core/docs/LANDMINES.md. */
    FILE *file = fopen(path, "rb");
    char *text;
    long size;
    size_t count;
    int found;
    if (!file) return 0;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return 0; }
    size = ftell(file);
    if (size < 0) { fclose(file); return 0; }
    rewind(file);
    text = (char *)malloc((size_t)size + 1);
    if (!text) { fclose(file); return 0; }
    count = fread(text, 1, (size_t)size, file);
    fclose(file);
    text[count] = '\0';
    found = (strstr(text, needle) != NULL);
    free(text);
    return found;
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
          Has(fire, ".spriteFlipY = false"),
          "directionless fire varies phase rate and horizontal mirror without reversing rise");
    Check(Has(fire, "followTarget = &emitter->pos") &&
          Has(fire, "followTargetGeneration = &emitter->generation") &&
          Has(fire, "generation = s_fvolNextGeneration"),
          "young fire follows its source and detaches safely on slot reuse");
    Check(Has(fire, "SpriteAnim_SetFrameMetadata(&s_fvolVolumeAnim") &&
          Has(fire, "pure_flame_puff_metadata.inl"),
          "volume flipbook binds bake-generated per-frame crop metadata");
    Check(Has(fire, "s_fvolRiseMul = 1.45f") &&
          Has(fire, "s_fvolWidthMul = 0.50f") &&
          Has(fire, "sqrtf(Random01()) * 0.18f") &&
          Has(fire, "Math_Mix(0.70f, 1.05f"),
          "packed volume emitter owns a compact foot and upward macro motion");
    Check(Has(fire, "s_fvolBodyLive = 68.0f") &&
          Has(fire, "&s_fvolBodyLive, 68.0f") &&
          Has(fire, "s_fvolBodyAlpha = 0.35f") &&
          Has(fire, "&s_fvolBodyAlpha, 0.35f") &&
          Has(fire, "(unsigned char)(255.0f * s_fvolBodyAlpha)") &&
          Has(fire, "s_fvolHeatGain = 0.88f") &&
          Has(fire, "&s_fvolHeatGain, 0.88f") &&
          Has(fire, "s_fvolEmissive = 2.6f") &&
          Has(fire, "&s_fvolEmissive, 2.6f") &&
          Has(fire, ".render.volumeSheet = 2") &&
          Has(fire, ".render.motionTex = s_fvolMotionTex") &&
          Has(fire, ".render.sixWayLighting = 1") &&
          Has(fire, ".render.blendMode = VFX_BLEND_PREMULTIPLIED"),
          "packed volume fire keeps tuning defaults and material features in sync");
    Check(Has("core/composition/visual_composer.c", "SmokeEmitter_Update(dt);") && Has("core/composition/visual_composer.c", "VC_FlameEmitter_Update(dt);"), "both pools are ticked");
    return fails ? 1 : 0;
}
