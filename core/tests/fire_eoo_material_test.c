#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c, n) do { if (c) printf("PASS: %s\n", n); else { printf("FAIL: %s\n", n); failures++; } } while (0)

static int Has(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    static char text[280000];
    size_t count = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[count] = '\0';
    return strstr(text, needle) != NULL;
}

static float DensityFromTransmittance(float g) { return 1.0f - g; }
static float EmissionFromEoo(float r, float coverage) { return r * coverage; }

int main(void)
{
    const char *shader = "core/particles/shaders/particle_lit.fs";
    const char *runtime = "core/particles/particle_system.c";
    const char *fire = "core/composition/fire/flame_volume.inl";
    const char *profiles = "assets/vfx_surface_profiles.json";
    const char *packing = "assets/TEXTURE_PACKING.md";

    CHECK(DensityFromTransmittance(0.15f) > DensityFromTransmittance(0.90f),
          "FIRE_EOO G decodes as transmittance rather than smoke density");
    CHECK(EmissionFromEoo(0.8f, 0.7f) > EmissionFromEoo(0.2f, 0.7f),
          "FIRE_EOO R drives covered emission");
    CHECK(Has(packing, "| `FIRE_EOO` | `emission` | `transmittance` | `unused` | `opacity` |") &&
          Has(profiles, "FIRE_EOO | R:emission/CLAMP | G:transmittance/CLAMP | B:unused/CLAMP | A:opacity/CLAMP"),
          "packing spec and surface manifest declare the fire EOO contract");
    CHECK(Has(fire, "s_fvolRoilNormalTex = roilProfile != NULL ? roilProfile->normalMap") &&
          Has(fire, "s_fvolFireballNormalTex = fireballProfile != NULL ? fireballProfile->normalMap") &&
          Has(fire, ".render.volumeSheet = (useRoil || useFireball) ? 4 : 0") &&
          Has(fire, ".render.normalTex = useRoil ? s_fvolRoilNormalTex"),
          "Ambient Fire binds each extracted body to its normal companion in mode 4");
    CHECK(Has(runtime, "p->normalTexId != 0 && s_locNormalTex >= 0") &&
          !Has(runtime, "p->smokeSheet && p->normalTexId != 0 && s_locNormalTex >= 0"),
          "particle runtime binds normals for every packed material mode");
    CHECK(Has(shader, "if (u_volumeSheet > 3.5)") &&
          Has(shader, "float density = clamp(1.0 - texelColor.g, 0.0, 1.0)") &&
          Has(shader, "vec4 normalSample = texture(u_normalTex, sampleUV)"),
          "mode 4 decodes emission, transmittance and normal data before packed fire");
    return failures ? 1 : 0;
}
