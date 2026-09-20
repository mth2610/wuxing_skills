#include <stdio.h>
#include <string.h>

static int Has(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    char text[24000];
    size_t count;
    if (!file) return 0;
    count = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[count] = '\0';
    return strstr(text, needle) != NULL;
}

int main(void)
{
    int bad = 0;
    const char *dust = "core/composition/common/vc_impact_dust.inl";
    bad += !Has(dust, "VFX_SURFACE_SMOKE_PUFF_LIGHT_NIAGARA");
    bad += !Has(dust, "VFX_SurfaceRegistry_Get(");
    bad += !Has(dust, ".render.smokeSheet = 1");
    bad += !Has(dust, ".render.normalTex = s_impactDustNormalTex");
    bad += Has(dust, "ResourceManager_LoadTexture");
    printf("impact dust primary: %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
