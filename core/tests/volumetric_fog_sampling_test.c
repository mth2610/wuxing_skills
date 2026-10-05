#include "core/volumetric/fog_blue_noise.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c, label) do { if (!(c)) { puts("FAIL: " label); failures++; } } while (0)

static double OpticalDepth(double phase, int rotate) {
    double depth = 0.0;
    for (int i = 0; i < 20; i++) {
        double u0 = (double)i / 20.0, u1 = (double)(i + 1) / 20.0;
        double t0 = 40.0 * (1.0 - (1.0-u0)*(1.0-u0));
        double t1 = 40.0 * (1.0 - (1.0-u1)*(1.0-u1));
        double jitter = phase + (rotate ? (double)i * 0.61803398875 : 0.0);
        jitter -= floor(jitter);
        double t = t0 + (t1-t0) * (0.25 + 0.5*jitter);
        depth += exp(-0.1*t) * (t1-t0);
    }
    return depth;
}

static int SourceHas(const char *path, const char *needle) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char source[22000];
    size_t n = fread(source, 1, sizeof(source)-1, f);
    fclose(f);
    source[n] = '\0';
    return strstr(source, needle) != NULL;
}

int main(void) {
    enum { COUNT = FOG_BLUE_NOISE_SIZE * FOG_BLUE_NOISE_SIZE };
    int histogram[256] = {0};
    double sum = 0.0, difference = 0.0;
    double oldSum = 0.0, oldSquared = 0.0, newSum = 0.0, newSquared = 0.0;
    static const int bayer[16] = {0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
    for (int y = 0; y < FOG_BLUE_NOISE_SIZE; y++) {
        for (int x = 0; x < FOG_BLUE_NOISE_SIZE; x++) {
            int value = kFogBlueNoise[y*FOG_BLUE_NOISE_SIZE+x];
            histogram[value]++;
            sum += value;
            difference += fabs(value-kFogBlueNoise[((y+4)%FOG_BLUE_NOISE_SIZE)*FOG_BLUE_NOISE_SIZE+x]);
            double old = OpticalDepth((double)bayer[(y%4)*4+x%4]/16.0, 0);
            double current = OpticalDepth(((double)value+0.5)/256.0, 1);
            oldSum += old; oldSquared += old*old;
            newSum += current; newSquared += current*current;
        }
    }
    for (int i = 0; i < 256; i++) CHECK(histogram[i] == COUNT/256, "uniform sampling rank distribution");
    CHECK(fabs(sum/COUNT-127.5) < 0.00001, "balanced sampling tile");
    CHECK(difference/COUNT > 60.0, "sampling does not repeat every four pixels");
    double oldVariance = oldSquared/COUNT-(oldSum/COUNT)*(oldSum/COUNT);
    double newVariance = newSquared/COUNT-(newSum/COUNT)*(newSum/COUNT);
    CHECK(newVariance < oldVariance*0.3, "independent interval phases reduce structured integration error");
    double exact = (1.0-exp(-4.0))/0.1;
    CHECK(fabs(newSum/COUNT-exact) < exact*0.01, "smooth volume preserves mean optical depth within one percent");
    /* Numeric transport/data guards cannot certify GPU appearance. Wiring below
       ties this mirror to the shader; matched captures remain required. */
    const char *shader = "core/volumetric/shaders/volumetric_fog.fs";
    CHECK(!SourceHas(shader, "Bayer4x4"), "raymarch has no repeating Bayer grid");
    CHECK(SourceHas(shader, "texture(u_jitterTex, jitterUV)"), "raymarch reads the immutable sampling texture");
    CHECK(SourceHas(shader, "fract(dither + float(i) * 0.61803398875)"), "intervals decorrelate the ray phase");
    CHECK(SourceHas(shader, "float stepSize = t1 - t0;"), "optical path length remains unchanged");
    CHECK(!SourceHas(shader, "ComputeCanopyGodRay"), "fog has no independently animated synthetic canopy");
    CHECK(SourceHas("core/volumetric/volumetric_fog.c", "SetShaderValueTexture(s_raymarchShader, s_locJitterTex, s_jitterTex)"), "sampling texture is bound in the raymarch scope");
    printf("sampling optical-depth variance %.7f -> %.7f; mean error %.4f%%\n",
           oldVariance, newVariance, 100.0*fabs(newSum/COUNT-exact)/exact);
    if (failures) return 1;
    puts("PASS: balanced nonperiodic fog sampling, transport and shader wiring");
    return 0;
}
