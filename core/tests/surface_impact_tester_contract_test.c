/* Generated tester contract: one fixture owns all five surface receivers. */
#include <stdio.h>
#include <string.h>

static int Has(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    static char text[180000];
    size_t count;
    if (!file) return 0;
    count = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[count] = '\0';
    return strstr(text, needle) != NULL;
}

static int Count(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    static char text[180000];
    char *cursor;
    int count = 0;
    size_t size;
    if (!file) return 0;
    size = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[size] = '\0';
    cursor = text;
    while ((cursor = strstr(cursor, needle)) != NULL) {
        count++;
        cursor += strlen(needle);
    }
    return count;
}

static long Position(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    static char text[180000];
    char *match;
    size_t size;
    if (!file) return -1;
    size = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[size] = '\0';
    match = strstr(text, needle);
    return match ? (long)(match - text) : -1;
}

int main(void)
{
    int failed = 0;
    const char *manifest = "scripts/vfx_test_manifest.json";
    const char *generator = "scripts/sync_vfx_test.py";
    const char *tester = "sandbox/vfx_test.c";

    failed += Count(manifest, "\"label\": \"SURFACE IMPACT\"") != 1;
    failed += !Has(generator, "VFX_ComposeSurfaceImpact($POS, s_surfaceImpactFixtureSurface)");
    failed += !Has(generator, "newfx_surface_impact_selector_state") ||
              !Has(generator, "newfx_surface_impact_selector_input") ||
              !Has(generator, "newfx_surface_impact_selector_ui");
    failed += !Has(tester, "Earth\", \"Fire\", \"Wood\", \"Metal\", \"Water") ||
              !Has(tester, "KEY_PERIOD") || !Has(tester, "KEY_COMMA") ||
              !Has(tester, "VFX_IMPACT_SURFACE_COUNT") ||
              !Has(tester, "VFX_ComposeSurfaceImpact(s_prefabStartPos, s_surfaceImpactFixtureSurface)");
    failed += !Has(tester, "VFX_ComposeSurfaceImpact(pos, s_surfaceImpactFixtureSurface); return false;");
    failed += !Has(tester, "SURFACE IMPACT receiver: %s   > next   , previous");
    failed += Has(tester, "if (s_surfaceImpactFixtureSurface == VFX_IMPACT_SURFACE_WATER)") ||
              Has(tester, "VFX_ComposeIceCrystal(s_prefabStartPos,");
    failed += Position(tester, "if (s_isPlayingMesh)\n") >=
              Position(tester, "@gen:newfx_surface_impact_selector_input begin");

    printf("surface impact tester contract: %s\n", failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
