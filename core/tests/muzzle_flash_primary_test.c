#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(c, n) do { if (c) printf("PASS: %s\n", n); else { printf("FAIL: %s\n", n); failures++; } } while (0)

static int Has(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    static char text[300000];
    size_t count = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[count] = '\0';
    return strstr(text, needle) != NULL;
}

int main(void)
{
    const char *muzzle = "core/composition/common/vc_muzzle_flash.inl";
    const char *profiles = "assets/vfx_surface_profiles.json";
    const char *catalog = "assets/textures/vfx/catalog.json";
    const char *header = "core/composition/visual_composer.h";
    const char *manifest = "scripts/vfx_test_manifest.json";

    CHECK(Has(catalog, "\"path\":\"weapons/muzzle_flash_sphere.png\"") &&
          Has(catalog, "\"grid\":[2,2],\"frames\":4"),
          "sphere extraction is catalogued as the visually verified 2x2 variant sheet");
    CHECK(Has(profiles, "VFX_SURFACE_MUZZLE_FLASH_FRONT_NIAGARA") &&
          Has(profiles, "VFX_SURFACE_MUZZLE_FLASH_SIDE_NIAGARA") &&
          Has(profiles, "VFX_SURFACE_MUZZLE_FLASH_SPHERE_NIAGARA"),
          "all three muzzle layers own semantic surface profiles");
    CHECK(Has(header, "void VFX_ComposeMuzzleFlash(Vector3 muzzlePos, Vector3 forward,") &&
          Has(muzzle, "void VFX_ComposeMuzzleFlash(Vector3 muzzlePos, Vector3 forward,"),
          "public one-shot muzzle primary has a direction-bearing API");
    CHECK(Has(muzzle, "VFX_FACING_CROSS_BILLBOARD") &&
          Has(muzzle, ".facingDirection = axis") &&
          Has(muzzle, ".facingAspect = 2.0f"),
          "side flame uses the authored-axis cross-billboard contract");
    CHECK(Has(muzzle, "SpriteAnim_Init(&s_muzzleFrontAnim, 1, 4, 4, 1.0f, ANIM_ONCE)") &&
          Has(muzzle, "SpriteAnim_Init(&s_muzzleSideAnim, 1, 2, 2, 1.0f, ANIM_ONCE)") &&
          Has(muzzle, "SpriteAnim_Init(&s_muzzleSphereAnim, 2, 2, 4, 1.0f, ANIM_ONCE)") &&
          Has(muzzle, ".spriteAnimRate = 0.001f"),
          "a shot holds one randomly selected spatial variant instead of playing an animation");
    CHECK(Has(muzzle, ".render.appearance = VFX_APPEARANCE_GLOW") &&
          Has(muzzle, "VFXLight_Spawn("),
          "flash declares an emissive appearance and a bounded transient light");
    CHECK(Has(manifest, "\"fn\": \"VFX_ComposeMuzzleFlash\"") &&
          Has(manifest, "\"lifecycle\": \"event\""),
          "muzzle flash is registered as a one-shot event fixture");
    CHECK(Has("scripts/sync_vfx_test.py", "\"VFX_ComposeMuzzleFlash\":") &&
          Has(manifest, "VFX_ComposeMuzzleFlash($POS, (Vector3){1.0f, 0.0f, 0.0f}, VC_MAT_FIRE, 1.5f, 1.0f)"),
          "one-shot fixture supplies full intensity instead of zero progress");

    return failures ? 1 : 0;
}
