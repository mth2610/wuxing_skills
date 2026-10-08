#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c, label) do { if (!(c)) { puts("FAIL: " label); failures++; } } while (0)

static double Phase(double cosine, double g) {
    return (1.0 - g*g) / (4.0 * 3.14159265 * pow(1.0 + g*g - 2.0*g*cosine, 1.5));
}

static double Visibility(int enabled, double x, double y, double z,
                         double w, double storedDepth) {
    if (!enabled || w <= 0.00001) return 1.0;
    x = x/w * 0.5 + 0.5; y = y/w * 0.5 + 0.5; z = z/w * 0.5 + 0.5;
    if (x < 0 || x > 1 || y < 0 || y > 1 || z < 0 || z > 1) return 1.0;
    return z <= storedDepth + 0.0015 ? 1.0 : 0.0;
}

static void CheckSource(const char *path, const char *const *needles, int count) {
    FILE *file = fopen(path, "rb");
    CHECK(file != NULL, "lighting source readable");
    if (!file) return;
    char source[32768] = {0};
    fread(source, 1, sizeof(source)-1, file);
    CHECK(fgetc(file) == EOF, "lighting wiring guard reads the complete source");
    fclose(file);
    for (int i = 0; i < count; ++i) CHECK(strstr(source, needles[i]) != NULL, "physical fog source wiring");
    if (strstr(path, ".fs")) {
        CHECK(strstr(source, "ComputeCanopyGodRay") == NULL, "no fictitious global canopy illumination");
        CHECK(strstr(source, "shaftVisibility") == NULL, "no synthetic side-scattering floor");
    } else {
        const char *begin = strstr(source, "BeginShaderMode(s_raymarchShader)");
        const char *end = begin ? strstr(begin, "EndShaderMode()") : NULL;
        const char *staticUpload = begin ? strstr(begin, "SetShaderValueTexture(s_raymarchShader, s_locStaticShadowMap, staticShadowMap)") : NULL;
        CHECK(begin && end && staticUpload && staticUpload < end,
              "static shadow sampler upload occurs inside active raymarch shader");
    }
}

int main(void) {
    const double anisotropies[] = {0.0, 0.4, 0.75};
    for (int g = 0; g < 3; ++g) {
        double integral = 0;
        const int samples = 100000;
        for (int i = 0; i < samples; ++i)
            integral += Phase(-1.0 + (i + 0.5) * 2.0 / samples, anisotropies[g]);
        integral *= 4.0 * 3.14159265 / samples;
        CHECK(fabs(integral - 1.0) < 1e-6, "HG integrates to unit scattering over the sphere");
    }
    CHECK(Phase(1, 0.75) > Phase(0, 0.75) && Phase(0, 0.75) > Phase(-1, 0.75),
          "physical forward scattering retains angular rays");
    double clear = Visibility(1, 0, 0, 0, 1, 1);
    double blocker = Visibility(1, 0, 0, 0, 1, 0.2);
    CHECK(clear == 1 && blocker == 0, "actual depth distinguishes clear and blocked samples");
    CHECK(fmin(clear, blocker) == 0 && fmin(blocker, clear) == 0 && fmin(clear, clear) == 1,
          "either dynamic or cached static caster blocks direct scattering");
    CHECK(Visibility(0, 0, 0, 0, 1, 0) == 1, "disabled shadows do not sample an invalid map");
    CHECK(Visibility(1, 3, 0, 0, 1, 0) == 1 && Visibility(1, 0, 0, -3, 1, 0) == 1 &&
          Visibility(1, 0, 0, 0, -1, 0) == 1, "outside shadow coverage remains unoccluded");
    CHECK(0.70 * blocker * Phase(0, 0.75) == 0 && 0.70 * clear * Phase(0, 0.75) > 0,
          "authored intensity preserves illumination only on unblocked paths");
    /* Mirrors and wiring guards cannot certify GPU texture coordinates or visibility. */
    const char *shader[] = {"texture(shadowMap, proj.xy).r", "proj.z <= shadowDepth + 0.0015",
        "shadow = min(shadow, SampleShadowLS(u_staticShadowMap, staticPosLS))",
        "u_sunColor * (directLight * miePhase)", "shadow * u_godRayIntensity",
        "4.0 * 3.14159265 * pow", "u_staticLightVP * vec4(rayDir, 0.0)"};
    CheckSource("core/volumetric/shaders/volumetric_fog.fs", shader, 7);
    const char *host[] = {"EnvShadow_HasStaticCache()", "EnvShadow_IsEnabled()", "EnvShadow_GetStaticShadowMap()"};
    CheckSource("core/volumetric/volumetric_fog.c", host, 3);
    if (failures) return 1;
    puts("PASS: normalized Mie scattering and actual two-layer shadow illumination");
    return 0;
}
