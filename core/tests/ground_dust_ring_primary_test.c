// Headless contract — extracted NE_GroundDust is a sparse, flat radial burst.
#include <stdio.h>
#include <string.h>

static int failures = 0;
static int checks = 0;
#define CHECK(c, n) do { checks++; if (c) printf("PASS: %s\n", n); else { printf("FAIL: %s\n", n); failures++; } } while (0)

static int Has(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    char text[65536];
    size_t n;
    if (!f) return 0;
    n = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[n] = '\0';
    return strstr(text, needle) != NULL;
}

int main(void)
{
    const char *src = "core/composition/common/vc_ground_dust_ring.inl";
    const char *api = "core/composition/visual_composer.h";
    CHECK(Has(api, "void VFX_ComposeGroundDustRing(Vector3 pos, VC_MaterialId matId, float scale,"),
          "ground dust ring is a public primary");
    CHECK(Has(src, "#define GROUND_DUST_RING_MIN_PARTICLES 25"),
          "low severity retains a readable sparse ring");
    CHECK(Has(src, "#define GROUND_DUST_RING_MAX_PARTICLES 40"),
          "high severity reaches the extracted 25-40 particle budget");
    CHECK(Has(src, "float ringRadius = Math_Mix(0.30f, 1.15f, severity01) * scale;"),
          "severity expands the ring rather than multiplying cloud density");
    CHECK(Has(src, ".physics.initialImpulseNs = {cosf(angle) * speed,") &&
          Has(src, "Math_Mix(0.015f, 0.055f, Random01()) * scale"),
          "the burst expresses horizontal launch and dust lift as radial impulse");
    CHECK(Has(src, "float speed = Math_Mix(6.0f, 12.0f, Random01()) * scale;") &&
          Has(src, ".linearDragPerSecond = 7.0f") &&
          Has(src, ".physics.dynamics = &s_groundDustRingDynamics"),
          "the ring launches at Niagara shockwave speed, then brakes through heavy physical drag");
    CHECK(!Has(src, ".facingMode = VFX_FACING_GROUND_PLANE,") &&
          Has(src, "camera-facing cards preserve the ring under the current renderer"),
          "ground ring uses reliable camera-facing dust cards");
    CHECK(Has(src, "const Color baseDust = {210, 202, 186, 112};") &&
          Has(src, "pos.y + 0.08f * scale,") &&
          Has(src, ".radius = Math_Mix(0.32f, 0.55f, Random01()) * scale,"),
          "ground-aligned cards retain enough opacity, clearance and area to be visible");
    CHECK(Has(src, ".render.texture = s_groundDustRingTex,"),
          "ring uses the extracted smoke-puff body atlas");
    CHECK(Has(src, ".render.normalTex = s_groundDustRingNormalTex,"),
          "ring uses the extracted companion normal map");
    CHECK(Has(src, ".render.blendMode = VFX_BLEND_ALPHA,"),
          "dust remains an alpha body, not additive glow");
    printf("%d/%d passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
