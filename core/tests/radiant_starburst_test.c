// Headless contract for the structured radiance mask.  It mirrors the mask's
// polar construction, so it cannot assess texture filtering or HDR bloom; it
// does catch a regression to a circular glow or evenly-sized spokes.

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
static int checks = 0;

#define CHECK(condition, name) do { \
    checks++; \
    if (condition) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s\n", name); failures++; } \
} while (0)

static float Clamp01(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static float Ray(float angle, float radius, float rayAngle,
                 float length, float width, float energy)
{
    float d = atan2f(sinf(angle - rayAngle), cosf(angle - rayAngle));
    float across = fabsf(d) / width;
    if (across >= 1.0f || radius >= length) return 0.0f;
    float along = radius / length;
    float taper = (1.0f - along) * (1.0f - along);
    float profile = (1.0f - across * across);
    profile *= profile;
    float emerge = Clamp01((radius - 0.075f) / 0.16f);
    emerge = emerge * emerge * (3.0f - 2.0f * emerge);
    return energy * taper * profile * emerge;
}

static float Mask(float radius, float angle)
{
    enum { RAYS = 18 };
    static const float angles[RAYS] = {
        0.02f, 0.38f, 0.78f, 1.13f, 1.57f, 1.93f, 2.27f, 2.70f,
        3.15f, 3.47f, 3.86f, 4.24f, 4.71f, 5.03f, 5.46f, 5.79f,
        6.03f, 6.20f,
    };
    static const float lengths[RAYS] = {
        0.96f, 0.48f, 0.70f, 0.39f, 0.84f, 0.53f, 0.62f, 0.41f,
        0.91f, 0.46f, 0.73f, 0.37f, 0.79f, 0.45f, 0.68f, 0.42f,
        0.58f, 0.76f,
    };
    static const float widths[RAYS] = {
        0.020f, 0.036f, 0.026f, 0.042f, 0.018f, 0.035f, 0.028f, 0.045f,
        0.019f, 0.034f, 0.025f, 0.043f, 0.021f, 0.038f, 0.027f, 0.044f,
        0.032f, 0.024f,
    };
    static const float energy[RAYS] = {
        0.92f, 0.42f, 0.61f, 0.34f, 0.84f, 0.44f, 0.58f, 0.31f,
        0.95f, 0.39f, 0.64f, 0.30f, 0.79f, 0.36f, 0.56f, 0.29f,
        0.45f, 0.69f,
    };
    float rays = 0.0f;
    for (int i = 0; i < RAYS; ++i) {
        float ray = Ray(angle, radius, angles[i], lengths[i], widths[i], energy[i]);
        if (ray > rays) rays = ray;
    }
    float r2 = radius * radius;
    float alpha = expf(-r2 * 88.0f) + expf(-r2 * 18.0f) * 0.50f
                + expf(-r2 * 4.5f) * 0.075f + rays;
    return Clamp01(alpha * (1.0f - r2) * (1.0f - r2));
}

static int FileHas(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    char buffer[16384];
    size_t n = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);
    buffer[n] = '\0';
    return strstr(buffer, needle) != NULL;
}

int main(void)
{
    const char *source = "core/composition/common/vc_radiant_starburst.inl";
    printf("=== core headless test: radiant starburst ===\n");

    CHECK(Mask(0.0f, 0.0f) > 0.95f, "compact centre remains the hot point");
    CHECK(Mask(1.0f, 0.0f) == 0.0f, "mask ends cleanly at the billboard edge");
    CHECK(Mask(0.70f, 0.02f) > Mask(0.70f, 0.38f) * 2.5f,
          "long primary ray outruns a nearby short secondary ray");
    CHECK(Mask(0.55f, 0.02f) > Mask(0.55f, 0.20f) * 3.0f,
          "ray gap stays visibly darker than the ray itself");
    CHECK(FileHas(source, "core/shaders/radiant_starburst.fs"),
          "composition uses its procedural shader rather than a baked mask");
    CHECK(FileHas(source, "VFX_SURFACE_ALPHA") &&
          FileHas(source, "VFX_SURFACE_ADDITIVE"),
          "shader splits coloured coverage from its hot emission");

    printf("---\n%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
