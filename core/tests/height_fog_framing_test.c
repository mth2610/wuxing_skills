/* Numeric framing contract plus host/shader wiring. GPU captures validate
 * projection restoration and the final visible composition separately. */
#include "core/volumetric/volumetric_fog_distance.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c, msg) do { if (!(c)) { puts("FAIL: " msg); failures++; } } while (0)

static float Coverage(float depth, float area) {
    float t = (VolumetricFog_DistantDepth(depth, area) - 0.12f) / 0.68f;
    t = fminf(1.0f, fmaxf(0.0f, t));
    return t * t * (3.0f - 2.0f * t);
}

static void Read(const char *path, char *text, size_t capacity) {
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL, "framing source is readable");
    if (!f) { text[0] = 0; return; }
    size_t n = fread(text, 1, capacity - 1, f);
    text[n] = 0;
    fclose(f);
}

int main(void) {
    float area = 4.0f / 9.0f;
    CHECK(Coverage(0, area) == 0, "camera focus stays clear of global distant fog");
    CHECK(Coverage(-1, area) == 0, "foreground stays clear");
    CHECK(Coverage(1, area) == 1, "far edge retains atmosphere");
    CHECK(Coverage(1, 0) == 0, "zero coverage disables only global fog");
    CHECK(Coverage(0.7f, area) < Coverage(0.7f, 1), "coverage contracts far footprint");
    CHECK(0.4f * Coverage(0, area) + 0.08f == 0.08f, "local mist survives clear foreground");
    char shader[16000], host[30000];
    Read("core/volumetric/shaders/height_fog.fs", shader, sizeof(shader));
    Read("core/volumetric/volumetric_fog.c", host, sizeof(host));
    CHECK(strstr(shader, "dot(endPos - u_fogFocus, u_fogForward)") != NULL, "height fog frames receiver in world space");
    CHECK(strstr(shader, "smoothstep(0.12, 0.80, framedDepth)") != NULL, "height fog shares volumetric framing curve");
    CHECK(strstr(shader, "opticalDepth *= distantFade;") != NULL, "global optical depth follows coverage");
    const char *global = strstr(shader, "opticalDepth *= distantFade;");
    const char *local = strstr(shader, "opticalDepth += localDensitySum;");
    CHECK(global && local && global < local, "local mist is added after global coverage");
    CHECK(strstr(host, "s_locHfDistantCoverage, &s_distantCoverage") != NULL, "height host uploads map coverage");
    if (failures) return 1;
    puts("PASS: height fog preserves distant framing and independent local mist");
    return 0;
}
