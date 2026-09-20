#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(c, n) do { if (c) printf("PASS: %s\n", n); else { printf("FAIL: %s\n", n); failures++; } } while (0)

static int FileHas(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    static char text[120000];
    size_t count = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[count] = '\0';
    return strstr(text, needle) != NULL;
}

static float SmoothStep(float a, float b, float x)
{
    float t = (x - a) / (b - a);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

static float Smooth01(float x)
{
    if (x < 0.0f) x = 0.0f;
    if (x > 1.0f) x = 1.0f;
    return x * x * (3.0f - 2.0f * x);
}

static float SparkCapsuleDistance(float x, float y)
{
    float qy = fabsf(y) - 0.62f;
    if (qy < 0.0f) qy = 0.0f;
    return sqrtf(x * x + qy * qy);
}

static float SparkCapsuleAlpha(float x, float y)
{
    return Smooth01((0.22f - SparkCapsuleDistance(x, y)) / 0.090f);
}

static float SparkCapsuleWhiteCore(float x, float y)
{
    float core = 1.0f - SparkCapsuleDistance(x, y) / (0.22f * 0.72f);
    return SmoothStep(0.30f, 0.85f, core);
}

int main(void)
{
    const char *source = "core/particles/particle_system.c";
    const char *header = "core/particles/particle_system.h";

    CHECK(SparkCapsuleAlpha(0.0f, 0.0f) > 0.999f,
          "spark capsule has a solid centre");
    CHECK(SparkCapsuleWhiteCore(0.0f, 0.0f) > 0.99f &&
          SparkCapsuleWhiteCore(0.16f, 0.0f) < 0.10f,
          "white hot core occupies the centre while the visible rim remains orange");
    CHECK(SparkCapsuleAlpha(0.18f, 0.0f) > 0.02f &&
          SparkCapsuleAlpha(0.18f, 0.0f) < 0.90f,
          "capsule fades softly through the orange rim rather than drawing a hard border");
    CHECK(SparkCapsuleAlpha(0.34f, 0.0f) < 0.001f &&
          SparkCapsuleAlpha(0.0f, 0.96f) < 0.001f,
          "streak coverage closes before every quad edge");
    CHECK(FileHas(source, "Texture2D ParticleSystem_SparkCapsuleSprite(void)") &&
          FileHas(source, "const float halfSegment = 0.62f;") &&
          FileHas(source, "const float capsuleRadius = 0.22f;") &&
          FileHas(source, "float whiteCore =") &&
          FileHas(source, "(unsigned char)(70.0f + 185.0f * whiteCore)"),
          "particle core owns the single textured white-core orange-rim capsule");
    CHECK(FileHas(header, "Texture2D ParticleSystem_SparkCapsuleSprite(void);"),
          "the spark capsule is exposed as a shared particle primitive");

    puts(failures ? "spark capsule sprite: FAIL" : "spark capsule sprite: PASS");
    return failures != 0;
}
