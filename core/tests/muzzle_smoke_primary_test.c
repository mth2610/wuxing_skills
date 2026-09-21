#include <stdio.h>
#include <string.h>

static int Has(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb"); char text[18000]; size_t n;
    if (!file) return 0;
    n = fread(text, 1, sizeof(text) - 1, file); fclose(file); text[n] = '\0';
    return strstr(text, needle) != NULL;
}

int main(void)
{
    const char *src = "core/composition/common/vc_muzzle_flash.inl";
    const char *hdr = "core/composition/visual_composer.h";
    int bad = !Has(src, "void VFX_ComposeMuzzleSmoke(") ||
              !Has(src, "VFX_SURFACE_SMOKE_PUFF_LIGHT_NIAGARA") ||
              !Has(src, ".render.smokeSheet = 1") ||
              !Has(src, ".render.normalTex = s_muzzleSmokeNormalTexture") ||
              !Has(src, "VFX_BLEND_ALPHA") ||
              !Has(src, "SpriteAnim_Init(&s_muzzleFrontAnim, 1, 4, 4, 1.0f, ANIM_ONCE)") ||
              !Has(src, "SpriteAnim_Init(&s_muzzleSideAnim, 1, 2, 2, 1.0f, ANIM_ONCE)") ||
              Has(src, "VFX_ComposeMuzzleSmoke(muzzlePos, axis, scale * 0.55f,") ||
              !Has(hdr, "void VFX_ComposeMuzzleSmoke(Vector3 muzzlePos, Vector3 forward,");
    printf("muzzle smoke primary: %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
