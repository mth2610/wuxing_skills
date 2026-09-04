/* Headless regression guard for the Gas System premultiplied blend contract.
 *
 * Premultiplied RGBA volumes must write directly into intermediate targets
 * without BLEND_ALPHA folding alpha into RGB a second/third time.
 * This test asserts that both GasSystem_Prepare and GasSystem_DenoiseRaymarch
 * wrap their DrawTexturePro calls inside a disabled-blend window with a batch flush.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); \
        return 1; \
    } \
} while (0)

static char *ReadFile(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) return NULL;
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *text = (char *)malloc((size_t)length + 1u);
    if (text == NULL) {
        fclose(file);
        return NULL;
    }
    size_t count = fread(text, 1, (size_t)length, file);
    text[count] = '\0';
    fclose(file);
    return text;
}

static int CountWindowsWithFlush(const char *text) {
    int valid = 0;
    const char *p = text;
    while ((p = strstr(p, "rlDisableColorBlend()")) != NULL) {
        const char *end = strstr(p, "rlEnableColorBlend()");
        if (end == NULL) break;
        const char *flush = strstr(p, "rlDrawRenderBatchActive()");
        if (flush != NULL && flush < end) {
            valid++;
        }
        p = end + 1;
    }
    return valid;
}

int main(void) {
    char *source = ReadFile("core/gas/gas_system.c");
    CHECK(source != NULL, "failed to read core/gas/gas_system.c");

    /* Exactly two offscreen passes in gas_system.c: raymarch and denoise */
    int validWindows = CountWindowsWithFlush(source);
    CHECK(validWindows >= 2,
          "gas_system.c must have at least 2 disabled-blend windows with batch flush");

    /* Verify GasSystem_Prepare has rlDisableColorBlend before DrawTexturePro */
    const char *prepare = strstr(source, "void GasSystem_Prepare(");
    CHECK(prepare != NULL, "GasSystem_Prepare must exist");
    const char *prepareDraw = strstr(prepare, "DrawTexturePro(s_atlas");
    CHECK(prepareDraw != NULL, "GasSystem_Prepare must draw atlas");
    const char *prepareDisable = strstr(prepare, "rlDisableColorBlend()");
    CHECK(prepareDisable != NULL && prepareDisable < prepareDraw,
          "GasSystem_Prepare must disable blend before DrawTexturePro");

    /* Verify GasSystem_DenoiseRaymarch has rlDisableColorBlend before DrawTexturePro */
    const char *denoise = strstr(source, "static void GasSystem_DenoiseRaymarch(");
    CHECK(denoise != NULL, "GasSystem_DenoiseRaymarch must exist");
    const char *denoiseDraw = strstr(denoise, "DrawTexturePro(s_raymarchTarget.texture");
    CHECK(denoiseDraw != NULL, "GasSystem_DenoiseRaymarch must draw raymarch target");
    const char *denoiseDisable = strstr(denoise, "rlDisableColorBlend()");
    CHECK(denoiseDisable != NULL && denoiseDisable < denoiseDraw,
          "GasSystem_DenoiseRaymarch must disable blend before DrawTexturePro");

    free(source);
    puts("gas_blend_contract_test: PASS");
    return 0;
}
