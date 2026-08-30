// Headless contract for the independently-authored Astral Spear projectile.
//
// Geometry and draw submission need a real camera/GPU to judge, but the shape
// hierarchy does not: this pins the ratios that keep the projectile a spear
// instead of an orb, the narrow-core/wide-body relationship, and the broken
// halo rhythm. Source checks bind the arithmetic mirror to the production file
// and ensure this effect never becomes a wrapper around Rift Bolt/FlowShield.

#include <stdio.h>
#include <string.h>

static int Has(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    char buffer[2048];
    size_t used = 0;
    size_t count;
    if (file == NULL) return 0;
    while ((count = fread(buffer + used, 1, sizeof(buffer) - 1 - used, file)) != 0)
    {
        used += count;
        buffer[used] = '\0';
        if (strstr(buffer, needle) != NULL)
        {
            fclose(file);
            return 1;
        }
        if (used > 512)
        {
            memmove(buffer, buffer + used - 512, 512);
            used = 512;
        }
    }
    fclose(file);
    return 0;
}

static float WakeWidth(float t01, float radius)
{
    float shoulder = t01 * t01 * (3.0f - 2.0f * t01);
    return radius * 0.78f * shoulder;
}

static int HaloSegmentDraws(int segments, int gapStride, int gapWidth)
{
    int drawn = 0;
    for (int i = 0; i < segments; ++i)
        if ((i % gapStride) >= gapWidth) drawn++;
    return drawn;
}

int main(void)
{
    const char *src = "core/composition/common/vc_astral_spear.inl";
    int failed = 0;

#define CHECK(c, message) do { \
    if (!(c)) { fprintf(stderr, "FAIL: %s\n", message); failed++; } \
} while (0)

    const float radius = 0.10f;
    const float halfLength = radius * 1.85f;
    CHECK(halfLength * 2.0f > radius * 2.0f * 1.7f,
          "head silhouette must remain strongly directional");
    CHECK(WakeWidth(0.0f, radius) == 0.0f,
          "wake must close cleanly at its oldest point");
    CHECK(WakeWidth(0.5f, radius) > 0.30f * radius,
          "wake must establish a readable colored shoulder");
    CHECK(WakeWidth(1.0f, radius) < radius,
          "wake must join inside the projectile body");
    CHECK(0.16f < 0.78f,
          "white-hot filament must remain narrower than colored wake");

    {
        int drawn = HaloSegmentDraws(24, 7, 2);
        CHECK(drawn >= 15, "broken halo must retain enough arc to read as a ring");
        CHECK(drawn <= 19, "broken halo must retain visible asymmetric gaps");
    }

    CHECK(Has(src, "ASTRAL_SPEAR_HALF_LENGTH 1.85f"),
          "source must retain the directional head ratio mirrored here");
    CHECK(Has(src, "ASTRAL_SPEAR_WAKE_WIDTH 0.78f"),
          "source must retain the colored wake width mirrored here");
    CHECK(Has(src, "ASTRAL_SPEAR_CORE_WIDTH 0.16f"),
          "source must retain the narrow core width mirrored here");
    CHECK(Has(src, "ASTRAL_SPEAR_HALO_SEGMENTS 24"),
          "source must retain the broken-halo sampling mirrored here");
    CHECK(Has(src, "VFX_Material(s->mat)"),
          "all identity colors must come from the material table");
    CHECK(Has(src, "VC_MotionOrbitAxis"),
          "halo motion must use the shared arbitrary-axis motion primitive");
    CHECK(Has(src, "VFX_RENDER_PASS_BODY"),
          "projectile must submit coverage, not only additive light");
    CHECK(Has(src, "VFX_RENDER_PASS_EMISSION"),
          "projectile must keep its compact light in a separate layer");
    CHECK(Has(src, "VFX_ComposeAstralSpear("),
          "projectile must expose the fixture-discoverable Compose lifecycle");
    CHECK(Has(src, "VC_AstralSpear_Update(float dt)") &&
          Has(src, "VC_AstralSpear_Draw3D(Camera3D cam)"),
          "stateful projectile must expose the auto-wired archetype pair");
    CHECK(!Has(src, "RiftBolt") && !Has(src, "RIFT_BOLT"),
          "Astral Spear must not depend on Rift Bolt");
    CHECK(!Has(src, "VFX_ComposeFlowShield(") && !Has(src, "VFX_FlowShield_"),
          "Astral Spear must not depend on FlowShield");

    puts(failed ? "astral spear contract: FAIL" : "astral spear contract: PASS");
    return failed != 0;
}
