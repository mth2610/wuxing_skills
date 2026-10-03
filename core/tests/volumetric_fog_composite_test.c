#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c, label) do { if (!(c)) { puts("FAIL: " label); failures++; } } while (0)

/* Numeric mirror of the reconstruction kernel. This guards transport and
   depth rejection, not GPU appearance, timing, or texture-coordinate flips. */
static double Weight(int x, int y, double difference, double threshold) {
    double t = fmin(1.0, fmax(0.0, difference / fmax(threshold, 0.001)));
    double range = 1.0 - t*t*(3.0-2.0*t);
    return (x == 0 ? 1.0 : 0.5) * (y == 0 ? 1.0 : 0.5) * range;
}

int main(void) {
    double total = 0.0, energy = 0.0, noise = 0.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            double w = Weight(x, y, 0.0, 2.0);
            total += w;
            energy += 0.3*w;
            noise += w*w;
        }
    }
    CHECK(fabs(energy/total-0.3) < 1e-12, "constant radiance and opacity survive normalized reconstruction");
    CHECK(noise/(total*total) < 0.15, "kernel reduces independent integration-noise variance");
    CHECK(Weight(1, 0, 2.0, 2.0) == 0.0, "unrelated depth layers contribute no fog");
    CHECK(Weight(1, 0, 1.0, 2.0) > 0.0, "nearby depths retain smooth support");
    CHECK(Weight(0, 0, 0.0, 0.0) == 1.0, "center sample prevents an empty denominator");
    CHECK(Weight(1, 1, 1.0, 0.5) == 0.0, "configured depth threshold controls rejection");
    FILE *f = fopen("core/volumetric/shaders/volumetric_composite.fs", "rb");
    CHECK(f != NULL, "composite shader is readable");
    if (f) {
        char source[8000];
        size_t n = fread(source, 1, sizeof(source)-1, f);
        fclose(f); source[n] = '\0';
        CHECK(strstr(source, "for (int y = -1; y <= 1; ++y)") != NULL, "shader uses the wider reconstruction kernel");
        CHECK(strstr(source, "max(u_depthThreshold, 0.001)") != NULL, "shader uses the configured depth threshold");
        CHECK(strstr(source, "sum / max(total, 0.0001)") != NULL, "shader normalizes the complete premultiplied sample");
    }
    if (failures) return 1;
    puts("PASS: fog reconstruction preserves mean transport, reduces noise and rejects other depth layers");
    return 0;
}
