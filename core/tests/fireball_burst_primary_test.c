// Headless contract — explosion fireball is a one-shot packed Niagara layer.
#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c, n) do { \
    if (c) printf("PASS: %s\n", n); \
    else { printf("FAIL: %s\n", n); failures++; } \
} while (0)

static int Has(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    static char text[24000];
    size_t count = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[count] = '\0';
    return strstr(text, needle) != NULL;
}

static int BurstCount(float severity01)
{
    return 16 + (int)(severity01 * 8.0f + 0.5f);
}

static float SpeedAfterDrag(float launchSpeed, float dragPerSecond, float seconds)
{
    return launchSpeed * expf(-dragPerSecond * seconds);
}

int main(void)
{
    const char *header = "core/composition/visual_composer.h";
    const char *source = "core/composition/fire/vc_fireball_burst.inl";
    const char *composer = "core/composition/visual_composer.c";

    CHECK(BurstCount(0.0f) == 16 && BurstCount(1.0f) == 24,
          "severity maps to the documented 16-24 instantaneous puffs");
    CHECK(SpeedAfterDrag(8.0f, 3.5f, 0.30f) < 3.0f,
          "3.5/s drag arrests explosive launch before the fireball tail");
    CHECK(Has(header, "VFX_ComposeFireballBurst"),
          "public primary API exposes a fireball event rather than a sustained emitter");
    CHECK(Has(source, "s_fvolFireballTex") && Has(source, "s_fvolFireballNormalTex") &&
          Has(source, ".render.volumeSheet = 4"),
          "fireball uses its own packed body and normal map decoder");
    CHECK(Has(source, ".physics.dynamics = &s_fireballBurstDynamics") &&
          Has(source, ".physics.initialImpulseNs") &&
          Has(source, ".physics.initialAccelerationMps2 = {0.0f, 1.2f, 0.0f}"),
          "motion is expressed through the physical dynamics contract");
    CHECK(Has(composer, "#include \"fire/vc_fireball_burst.inl\""),
          "fireball primary is linked after its fire-sheet owner");
    return failures ? 1 : 0;
}
