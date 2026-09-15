#include <stdio.h>
#include <string.h>

static int s_checks;
static int s_failures;

#define CHECK(condition, message) do { \
    s_checks++; \
    if (condition) printf("PASS: %s\n", message); \
    else { printf("FAIL: %s\n", message); s_failures++; } \
} while (0)

static int FileContains(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) return 0;
    static char buffer[1u << 20];
    size_t count = fread(buffer, 1, sizeof(buffer) - 1u, file);
    buffer[count] = '\0';
    fclose(file);
    int found = strstr(buffer, needle) != NULL;
    return found;
}

static unsigned ReadBE32(const unsigned char *p)
{
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) |
           ((unsigned)p[2] << 8) | (unsigned)p[3];
}

static void TestMotionAtlas(void)
{
    FILE *file = fopen("assets/textures/smoke_puff_motion_8x8.png", "rb");
    CHECK(file != NULL, "smoke optical-flow atlas exists");
    if (file == NULL) return;

    unsigned char header[24] = {0};
    size_t count = fread(header, 1, sizeof(header), file);
    fclose(file);
    CHECK(count == sizeof(header), "smoke optical-flow atlas has a PNG header");
    if (count != sizeof(header)) return;
    CHECK(ReadBE32(header + 16) == 1024 && ReadBE32(header + 20) == 1024,
          "smoke motion atlas uses 128px cells (1024x1024 total)");
}

static void TestRegistryAndConsumer(void)
{
    CHECK(FileContains("assets/vfx_surface_profiles.json", "smoke_puff_motion_8x8.png"),
          "smoke profile registers its optical-flow atlas");
    CHECK(FileContains("assets/vfx_surface_profiles.json", "smoke_puff_6way_b.png"),
          "smoke profile registers 6-way Map B");
    CHECK(FileContains("core/vfx_surface_registry.h", "Texture2D lightMapB;"),
          "surface registry exposes 6-way Map B semantically");
    CHECK(FileContains("core/vfx_surface_registry.c", "profile->lightMapB"),
          "surface registry loads 6-way Map B");
    CHECK(FileContains("core/composition/common/vc_smoke_puff.inl",
                       ".render.motionTex = s_smokeFbMotionTex"),
          "smoke flipbook binds optical-flow motion vectors");
    CHECK(FileContains("core/composition/common/vc_smoke_puff.inl",
                       "smokeProfile->lightMapB"),
          "smoke composition obtains 6-way Map B from the registry");
    CHECK(FileContains("core/composition/common/vc_smoke_puff.inl",
                       "#define SMOKE_FB_MAX_SPRITES 8"),
          "whole-puff flipbooks have a strict eight-sprite overdraw budget");
    CHECK(FileContains("core/composition/common/vc_smoke_puff.inl",
                       "count > SMOKE_FB_MAX_SPRITES"),
          "smoke composition enforces the flipbook sprite budget");
    CHECK(!FileContains("core/composition/common/vc_smoke_puff.inl", "mat->body"),
          "smoke composition never injects element hue into white smoke");
}

int main(void)
{
    printf("=== smoke 6-way + optical-flow contract ===\n");
    TestMotionAtlas();
    TestRegistryAndConsumer();
    printf("---- %d checks, %d failures\n", s_checks, s_failures);
    return s_failures == 0 ? 0 : 1;
}
