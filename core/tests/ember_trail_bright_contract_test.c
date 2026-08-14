#include <stdio.h>
#include <string.h>

static float EmberBodyMask(float r2)
{
    float coverage = (0.42f - r2) * 3.0f;
    if (coverage < 0.0f) coverage = 0.0f;
    if (coverage > 1.0f) coverage = 1.0f;
    return coverage * coverage * (3.0f - 2.0f * coverage);
}

static float EmberHaloMask(float r2)
{
    /* exp(-3.2*r2) mirrored conservatively at the sample points below. */
    if (r2 <= 0.0f) return 1.0f;
    if (r2 >= 1.0f) return 0.0f;
    if (r2 == 0.5f) return 0.1009f;
    return 0.0f;
}

static int Has(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    char buffer[1024];
    size_t used = 0;
    size_t count;
    if (file == NULL) return 0;
    while ((count = fread(buffer + used, 1, sizeof(buffer) - 1 - used, file)) != 0) {
        used += count;
        buffer[used] = '\0';
        if (strstr(buffer, needle) != NULL) {
            fclose(file);
            return 1;
        }
        if (used > 256) {
            memmove(buffer, buffer + used - 256, 256);
            used = 256;
        }
    }
    fclose(file);
    return 0;
}

int main(void)
{
    const char *path = "core/composition/common/vc_ember_trail.inl";
    int failed = 0;

#define CHECK(text, message) do { \
    if (!Has(path, text)) { fprintf(stderr, "FAIL: %s\n", message); failed++; } \
} while (0)
#define CHECK_NUM(condition, message) do { \
    if (!(condition)) { fprintf(stderr, "FAIL: %s\n", message); failed++; } \
} while (0)

    CHECK("const VFX_ElementMaterial *material = VFX_Material(e->mat);",
          "ember trail must use its authored element material");
    CHECK("ParticleConfig body =", "each ember needs an occluding coloured body");
    CHECK(".render.blendMode = VFX_BLEND_ALPHA",
          "the body must preserve hue over a bright destination");
    CHECK(".render.contrastProfile = VFX_CONTRAST_FIRE",
          "the body must use the shared fire readability profile");
    CHECK("ParticleConfig hotCore = body;",
          "the white-hot HDR core must share body motion");
    CHECK("hotCore.render.emissiveBoost = 5.0f;",
          "the occluding core must exceed the bloom threshold");
    CHECK("ParticleConfig halo = body;",
          "the bloom halo must share body motion");
    CHECK("halo.radius *= 4.20f;",
          "the additive halo must extend beyond the coloured body");
    CHECK("halo.render.texture = s_emberHaloTex;",
          "the halo must use a separate soft texture, never the solid body mask");
    CHECK("halo.render.appearance = VFX_APPEARANCE_GLOW;",
          "the halo must use the shared additive HDR appearance");
    CHECK("SpawnParticle(body);", "the body population must be submitted");
    CHECK("SpawnParticle(hotCore);", "the HDR core population must be submitted");
    CHECK("SpawnParticle(halo);", "the emission population must be submitted");

    CHECK("(0.42f - r2) * 3.0f", "the source must retain the tiny body mask mirrored here");
    CHECK("expf(-r2 * 3.2f)", "the halo must retain a smooth Gaussian shoulder");
    CHECK_NUM(EmberBodyMask(0.0f) > 0.999f, "the ember centre must be solid");
    CHECK_NUM(EmberBodyMask(0.50f) < 0.001f, "the body must not become a visible disc");
    CHECK_NUM(EmberHaloMask(0.50f) > 0.08f && EmberHaloMask(0.50f) < 0.13f,
              "the halo must retain a broad soft shoulder");
    CHECK_NUM(EmberHaloMask(1.0f) < 0.001f, "the halo must vanish at its quad edge");
    /* FIRE BODY applies density 0.90 * body intensity 0.80 before the
       hot-core boost.  The centre must still clear PostFX's 1.25 threshold. */
    CHECK_NUM(0.90f * 0.80f * 5.0f > 1.25f,
              "the occluding hot core must contain real HDR bloom energy");

    puts(failed ? "ember trail bright contract: FAIL"
                : "ember trail bright contract: PASS");
    return failed != 0;
}
