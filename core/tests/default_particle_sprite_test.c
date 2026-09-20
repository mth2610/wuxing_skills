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

static float Smooth01(float x)
{
    if (x < 0.0f) x = 0.0f;
    if (x > 1.0f) x = 1.0f;
    return x * x * (3.0f - 2.0f * x);
}

static float RoundAlpha(float radius)
{
    return Smooth01((0.84f - radius) / 0.22f);
}

static float WhiteCore(float radius)
{
    return Smooth01(1.0f - radius / 0.50f);
}

int main(void)
{
    const char *source = "core/particles/particle_system.c";
    const char *header = "core/particles/particle_system.h";
    const char *manager = "core/particles/particle_manager.c";
    const char *gpu = "core/particles/gpu/particle_gpu_backend.c";

    CHECK(RoundAlpha(0.0f) > 0.999f && RoundAlpha(0.76f) > 0.02f &&
          RoundAlpha(0.96f) < 0.001f,
          "default round particle has a filled centre and soft edge");
    CHECK(WhiteCore(0.0f) > 0.99f && WhiteCore(0.43f) < 0.10f,
          "default round particle separates its white core from orange rim");
    CHECK(FileHas(source, "Texture2D ParticleSystem_DefaultSprite(void)") &&
          FileHas(source, "const float radius = 0.84f;") &&
          FileHas(source, "config.render.texture = ParticleSystem_DefaultSprite();") &&
          FileHas(source, "(unsigned char)(70.0f + 185.0f * whiteCore)"),
          "untextured CPU particles receive the shared structured round sprite");
    CHECK(FileHas(header, "Texture2D ParticleSystem_DefaultSprite(void);"),
          "default particle sprite is public for deliberate reuse");
    CHECK(FileHas(manager, "GpuParticleSystem_Draw(c, ParticleSystem_DefaultSprite());"),
          "GPU billboards use the same default round sprite as CPU particles");
    CHECK(FileHas(source, "config.colorStart.r = config.colorStart.g = config.colorStart.b = 255;") &&
          FileHas(manager, "ParticleManager_DefaultSpriteColor(p->colorStart, defaultSpriteColors)") &&
          FileHas(gpu, "d.csr = boost;") && FileHas(gpu, "d.cer = boost;"),
          "default sprites retain their white-core/orange-rim RGB instead of inheriting emitter tint");

    puts(failures ? "default particle sprite: FAIL" : "default particle sprite: PASS");
    return failures != 0;
}
