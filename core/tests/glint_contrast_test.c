/* Guard the alpha-core/additive-halo blend law for glints. */
#include <stdio.h>
#include <string.h>

static int Has(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    char buf[1024];
    size_t used = 0, n;
    if (!f) return 0;
    while ((n = fread(buf + used, 1, sizeof(buf) - 1 - used, f)) != 0)
    {
        used += n;
        buf[used] = '\0';
        if (strstr(buf, needle) != NULL) { fclose(f); return 1; }
        if (used > 256) { memmove(buf, buf + used - 256, 256); used = 256; }
    }
    fclose(f);
    return 0;
}

int main(void)
{
    const char *src = "core/composition/common/vc_glint_sparkle.inl";
    int bad = 0;
    bad += !Has(src, ".radius   = radius * 0.72f");
    bad += !Has(src, ".render.blendMode = VFX_BLEND_ALPHA");
    bad += !Has(src, ".render.emissiveBoost = 2.2f");
    bad += !Has(src, ".radius   = radius * 1.25f");
    bad += !Has(src, ".render.blendMode     = VFX_BLEND_ADDITIVE");
    bad += !Has(src, ".render.emissiveBoost = 1.6f");
    printf("glint contrast blend law: %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
