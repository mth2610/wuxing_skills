// Focused generation contract: combat variants remain one selectable Sweep
// Slash fixture, rather than turning into duplicate buttons or sandbox edits.

#include <stdio.h>
#include <string.h>

static char *ReadFile(const char *path)
{
    static char buffer[262144];
    FILE *file = fopen(path, "rb");
    size_t count;
    if (file == NULL) return NULL;
    count = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);
    buffer[count] = '\0';
    return buffer;
}

static int Has(const char *path, const char *needle)
{
    const char *text = ReadFile(path);
    return text != NULL && strstr(text, needle) != NULL;
}

static int Count(const char *path, const char *needle)
{
    const char *text = ReadFile(path);
    int count = 0;
    size_t length = strlen(needle);
    if (text == NULL || length == 0) return 0;
    while ((text = strstr(text, needle)) != NULL)
    {
        count++;
        text += length;
    }
    return count;
}

int main(void)
{
    int failed = 0;
    const char *header = "core/composition/visual_composer.h";
    const char *source = "core/composition/common/vc_sweep_slash.inl";
    const char *generator = "scripts/sync_vfx_test.py";
    const char *manifest = "scripts/vfx_test_manifest.json";
    const char *tester = "sandbox/vfx_test.c";

    failed += !Has(header, "VFX_SweepSlashVariant") ||
              !Has(header, "VFX_SWEEP_SLASH_REAPING_ARC") ||
              !Has(header, "VFX_ComposeSweepSlashVariant");
    failed += !Has(source, "s_sweepSlashVariants[VFX_SWEEP_SLASH_VARIANT_COUNT]") ||
              !Has(source, "SweepSlash_GetVariant") ||
              !Has(source, "VFX_ComposeSweepSlashVariant(origin, dir, mat, length, arcRad, t01,") ||
              !Has(source, "for (int row = 0; row < style->rowCount; row++)") ||
              !Has(source, "float rowRadius = rowCenter * style->rowSpacing * arcLen;") ||
              !Has(source, "float rowAngle = rowCenter * style->rowAngleOffset;");
    failed += Count(manifest, "\"label\": \"SWEEP SLASH\"") != 1;
    failed += !Has(generator, "VFX_ComposeSweepSlashVariant($POS") ||
              !Has(generator, "newfx_sweep_slash_selector_state") ||
              !Has(generator, "newfx_sweep_slash_selector_input") ||
              !Has(generator, "newfx_sweep_slash_selector_ui");
    failed += !Has(tester, "Crescent\", \"Twin Fang\", \"Heavy Crescent\", \"Rising Fan\", \"Reaping Fan") ||
              !Has(tester, "KEY_PERIOD") || !Has(tester, "KEY_COMMA") ||
              !Has(tester, "VFX_SWEEP_SLASH_VARIANT_COUNT") ||
              !Has(tester, "VFX_ComposeSweepSlashVariant(s_prefabStartPos,");
    failed += !Has(tester, "SWEEP SLASH: %s   > next   , previous") ||
              !Has(tester, "s_meshTime = 0.0f;");

    printf("sweep slash tester contract: %s\n", failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
